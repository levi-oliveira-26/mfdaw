#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/ProjectState.h"
#include "Core/SpscQueue.h"
#include "Engine/DrumVoice.h"
#include "Engine/EngineEvent.h"

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

    static constexpr float kMasterGain = 0.7f;

    ProjectState projectState;
    SpscQueue<EngineEvent, 1024> uiToAudio;

    // Estado exclusivo da audio thread (cópias locais; nunca acessadas pela UI).
    double sampleRate { 44100.0 };
    int bpm { 120 };
    bool playing { false };
    int nextStep { 0 };
    double samplesPerStep { 0.0 };
    double samplesToNextStep { 0.0 };
    std::array<int, kNumTracks> masks {};
    std::array<DrumVoice, kNumTracks> voices {};

    // Canal Audio -> UI.
    std::atomic<int> playheadStep { -1 };
    std::atomic<std::uint32_t> droppedEvents { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainProcessor)
};

} // namespace mf
