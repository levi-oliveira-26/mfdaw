#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/ProjectState.h"
#include "Core/SpscQueue.h"
#include "Engine/DrumVoice.h"
#include "Engine/EngineEvent.h"
#include "Engine/SampleManager.h"

namespace mf
{
/**
 * MainProcessor — Núcleo de áudio da M.F. DAW.
 *
 * Duas "metades" com regras de thread estritamente separadas:
 *
 *  MESSAGE THREAD (UI / ValueTree)
 *    - Possui o ProjectState.
 *    - Escuta mudanças na árvore e as traduz em EngineEvent, publicados na
 *      SpscQueue (ÚNICO produtor = message thread).
 *
 *  AUDIO THREAD (processBlock)
 *    - ÚNICO consumidor da fila. Aplica os eventos em cópias locais
 *      (bpm, máscaras, flag de play) — jamais lê o ValueTree.
 *    - Sem new/malloc, sem I/O, sem mutex.
 *    - Publica a posição do playhead para a UI por um std::atomic<int>.
 */
class MainProcessor final : public juce::AudioProcessor,
                            private juce::ValueTree::Listener
{
public:
    MainProcessor();
    ~MainProcessor() override;

    // --- Acesso para a UI (message thread) ---
    [[nodiscard]] ProjectState& getProjectState() noexcept { return projectState; }
    [[nodiscard]] SampleManager& getSampleManager() noexcept { return sampleManager; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    void requestStop() noexcept;
    void requestChordPreview (int chordIndex, bool pressed) noexcept;
    void requestNotePreview (int midiNote, bool pressed) noexcept;

    /** Passo (0..15) que acabou de ser disparado, ou -1 se parado. Thread-safe. */
    [[nodiscard]] int getPlayheadStep() const noexcept { return playheadStep.load (std::memory_order_relaxed); }

    /** Eventos que não couberam na fila (diagnóstico; deveria ser sempre 0). */
    [[nodiscard]] std::uint32_t getDroppedEventCount() const noexcept { return droppedEvents.load (std::memory_order_relaxed); }

    // --- juce::AudioProcessor ---
    const juce::String getName() const override                    { return "M.F. DAW"; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                                { return true; }

    bool acceptsMidi() const override                              { return true; }
    bool producesMidi() const override                             { return false; }
    double getTailLengthSeconds() const override                   { return 0.0; }

    int getNumPrograms() override                                  { return 1; }
    int getCurrentProgram() override                               { return 0; }
    void setCurrentProgram (int) override                          {}
    const juce::String getProgramName (int) override              { return {}; }
    void changeProgramName (int, const juce::String&) override     {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    // ----- Message thread -----
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;

    /** Publica um evento (somente message thread = único produtor). */
    void post (const EngineEvent& event) noexcept;
    /** Reenvia ao motor tudo o que ele precisa saber (bpm, play, máscaras). */
    void pushFullState();

    // ----- Audio thread -----
    void drainEvents() noexcept;
    void recomputeTiming() noexcept;
    void triggerStep (int step) noexcept;
    [[nodiscard]] int getSwingOffsetSamples (int step) const noexcept;

    static constexpr float kMasterGain = 0.7f;

    ProjectState projectState;
    SampleManager sampleManager;
    SpscQueue<EngineEvent, 1024> uiToAudio;

    // Estado exclusivo da audio thread (cópias locais; nunca acessadas pela UI).
    double sampleRate { 44100.0 };
    int bpm { 120 };
    bool playing { false };
    int nextStep { 0 };
    std::int64_t playheadSampleIndex { 0 };
    double samplesPerStep { 0.0 };
    double samplesToNextStep { 0.0 };
    int swingTriggerCountdown { -1 };
    int swingTriggerStep { -1 };
    std::array<int, kNumTracks> masks {};
    std::array<std::array<std::uint8_t, kNumSteps>, kNumTracks> velocities {};
    std::array<float, kNumTracks> velocityGains { 1.0f };
    std::array<float, kNumTracks> samplePositions {};
    std::array<SampleManager::SamplePtr, kNumTracks> activeSamples {};
    std::array<DrumVoice, kNumTracks> voices {};

    struct PreviewVoice
    {
        int note { -1 };
        float phase { 0.0f };
        float envelope { 0.0f };
        float velocity { 0.0f };
        [[nodiscard]] float process (double sampleRate) noexcept;
        void start (int midiNote, float gain) noexcept;
        void release() noexcept { velocity = 0.0f; }
    };
    std::array<PreviewVoice, 12> previewVoices {};

    // Canal Audio -> UI.
    std::atomic<int> playheadStep { -1 };
    std::atomic<std::uint32_t> droppedEvents { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainProcessor)
};

} // namespace mf
