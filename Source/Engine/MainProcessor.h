#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/ProjectState.h"
#include "Core/SpscQueue.h"
#include "Engine/DrumVoice.h"
#include "Engine/AudioRecorder.h"
#include "Engine/EngineEvent.h"
#include "Engine/MelodySequencer.h"
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
    [[nodiscard]] AudioRecorder& getAudioRecorder() noexcept { return *audioRecorder; }
    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    void requestStop() noexcept;
    void requestChordPreview (int chordIndex, bool pressed) noexcept;
    void requestNotePreview (int midiNote, bool pressed) noexcept;
    void requestRecordingCountIn() noexcept;
    void cancelRecordingCountIn() noexcept;
    [[nodiscard]] int getRecordingCountIn() const noexcept { return recordingCountIn.load (std::memory_order_relaxed); }
    [[nodiscard]] int getCurrentBar() const noexcept { return currentBar.load (std::memory_order_relaxed); }
    [[nodiscard]] bool consumeRecordingStart() noexcept { return recordingStartSignal.exchange (false, std::memory_order_acq_rel); }

    /** Compasso (0-based, 0..kPlaylistBars-1, com loop) do passo mais recente. Thread-safe. */
    [[nodiscard]] int getPlayheadBar() const noexcept { return playheadBar.load (std::memory_order_relaxed); }

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
    /** Reenvia tipo + fader de TODOS os slots da Playlist (add/remove/load). */
    void pushPlaylistState();
    /** Reenvia a máscara de compassos da Bateria e de cada faixa de Melodia (clipes mudaram). */
    void pushArrangementState();
    /** Reenvia notas + compassos de TODOS os slots de Melodia (e limpa os demais). */
    void pushMelodyState();
    /** Uma nota de Melodia entrou/saiu da árvore: envia só essa mudança ao slot da faixa. */
    void postMelodyNoteChange (const juce::ValueTree& trackNode, const juce::ValueTree& noteNode, bool on);

    // ----- Audio thread -----
    void drainEvents() noexcept;
    void recomputeTiming() noexcept;
    /** Recalcula o ganho pré-mix de cada barramento a partir dos slots (sem alocar). */
    void recomputeBusGains() noexcept;
    void triggerStep (int step) noexcept;
    /** Inicia as notas de Melodia do passo (slots de Melodia audíveis no compasso). */
    void triggerMelodyStep (int step, int bar) noexcept;
    /** Solta as notas de Melodia que ainda soam (fim do passo / pause / stop). */
    void releaseMelodyVoices() noexcept;
    /** Dispara o passo (voz + nota MIDI) se a bateria estiver audível no compasso do passo. */
    void triggerStepInBar (int step, int bar, int sampleOffset, juce::MidiBuffer& midi) noexcept;
    [[nodiscard]] int getSwingOffsetSamples (int step) const noexcept;

    static constexpr float kMasterGain = 0.7f;

    ProjectState projectState;
    SampleManager sampleManager;
    std::unique_ptr<AudioRecorder> audioRecorder;
    // 8192 eventos (128 KiB): um ressincronismo completo (32 slots x 128 notas de Melodia
    // + máscaras + velocities) cabe com folga.
    SpscQueue<EngineEvent, 8192> uiToAudio;

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
    int swingTriggerBar { 0 };         // compasso do passo adiado (o swing pode cruzar a barra)

    // Arranjo (cópias da audio thread; chegam por EngineEvent::SetDrumBars).
    int drumBarMask { 0 };             // 0 = modo rascunho (padrão toca sempre)
    int arrangementBar { 0 };          // compasso do passo mais recente agendado
    bool hasScheduledSinceStop { false };
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

    // Fader pré-mix por faixa da Playlist (cópias da audio thread; chegam via
    // EngineEvent::SetTrackType/SetTrackGain). Slice 01: cada TIPO de faixa
    // alimenta um barramento — Bateria -> drumBusGain; Harmonia/Melodia ->
    // previewBusGain (pré-escuta). Vale a primeira faixa do tipo; sem faixa = 1.0.
    // Gravação/Amostra recebem DSP próprio nas próximas fatias.
    static constexpr float kGainSmoothing = 0.002f;
    std::array<int, kMaxPlaylistTracks> slotTypes {};    // -1 = slot vazio (preenchido no ctor)
    std::array<float, kMaxPlaylistTracks> slotGains {};  // ganho linear (preenchido no ctor)
    float drumBusGain { 1.0f };
    float previewBusGain { 1.0f };
    float drumGainSmoothed { 1.0f };
    float previewGainSmoothed { 1.0f };

    // Melodia por faixa (Slice 04): padrões + vozes. Cada voz lembra o slot de origem
    // para ser mixada com o fader da própria faixa.
    struct MelodyVoice : PreviewVoice { int slot { 0 }; };
    static constexpr std::size_t kNumMelodyVoices = 24;
    MelodySequencer melodySequencer;
    std::array<MelodyVoice, kNumMelodyVoices> melodyVoices {};
    std::size_t melodyStealIndex { 0 };
    std::array<float, kMaxPlaylistTracks> slotGainSmoothed {};   // ganho suavizado por slot (preenchido no ctor)

    // Canal Audio -> UI.
    std::atomic<int> playheadStep { -1 };
    std::atomic<int> currentBar { 0 };
    std::atomic<int> playheadBar { 0 };
    std::atomic<int> recordingStartBar { 0 };
    std::atomic<std::uint32_t> droppedEvents { 0 };

    // Gravacao com quatro beats de pre-roll, alinhada ao primeiro passo da
    // proxima barra. Toda comunicacao com UI/audio usa atomicos ou a fila.
    std::atomic<bool> recordingCountInRequested { false };
    std::atomic<bool> recordingCountInCancelled { false };
    std::atomic<bool> recordingStartSignal { false };
    std::atomic<int> recordingCountIn { 0 };
    bool recordingCountInActive { false };
    int recordingCountInBeats { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainProcessor)
};

} // namespace mf
