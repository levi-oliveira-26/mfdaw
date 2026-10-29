#include "Engine/MainProcessor.h"

#include <algorithm>
#include <cmath>

#include "Core/FaderScale.h"
#include "UI/MainEditor.h"

namespace mf
{
// ===========================================================================
// Construção / destruição  (MESSAGE THREAD)
// ===========================================================================
MainProcessor::MainProcessor()
    : juce::AudioProcessor (BusesProperties()
                            .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      audioRecorder (std::make_unique<AudioRecorder>())
{
    voices[0].setKind (DrumVoice::Kind::Kick);
    voices[1].setKind (DrumVoice::Kind::Snare);
    voices[2].setKind (DrumVoice::Kind::HiHat);
    voices[3].setKind (DrumVoice::Kind::Perc);

    for (auto& voice : voices)
        voice.prepare (sampleRate);

    for (auto& trackVelocities : velocities)
        trackVelocities.fill (100);

    // Slots da Playlist: vazios, ganho unitário (antes de qualquer áudio rodar).
    slotTypes.fill (-1);
    slotGains.fill (1.0f);

    recomputeTiming();
    audioRecorder->prepare (sampleRate, 0);

    projectState.getTree().addListener (this);
    projectState.getSession().addListener (this);

    // Sincroniza o motor com o estado inicial do projeto.
    pushFullState();
}

MainProcessor::~MainProcessor()
{
    projectState.getTree().removeListener (this);
    projectState.getSession().removeListener (this);
}

// ===========================================================================
// Ponte ValueTree -> Fila lock-free  (MESSAGE THREAD, único produtor)
// ===========================================================================
void MainProcessor::requestStop() noexcept
{
    post (EngineEvent::stop());
}

void MainProcessor::requestChordPreview (int chordIndex, bool pressed) noexcept
{
    chordIndex = juce::jlimit (0, 3, chordIndex);
    const int root = projectState.getKeyRoot();
    const bool minor = projectState.getScale() == Scale::Minor;

    static constexpr std::array<std::array<int, 3>, 4> majorIntervals {{
        {{ 0, 4, 7 }}, {{ 7, 11, 14 }}, {{ 9, 12, 16 }}, {{ 5, 9, 12 }}
    }};
    static constexpr std::array<std::array<int, 3>, 4> minorIntervals {{
        {{ 0, 3, 7 }}, {{ 8, 12, 15 }}, {{ 3, 7, 10 }}, {{ 10, 14, 17 }}
    }};

    const auto& intervals = minor ? minorIntervals : majorIntervals;
    for (const int interval : intervals[static_cast<std::size_t> (chordIndex)])
        post (EngineEvent::chordNote (48 + root + interval, pressed));
}

float MainProcessor::PreviewVoice::process (double sampleRate) noexcept
{
    if (note < 0)
        return 0.0f;

    constexpr float pi = 3.14159265358979323846f;
    const float frequency = 440.0f * std::pow (2.0f, (note - 69) / 12.0f);
    phase += frequency / static_cast<float> (sampleRate);
    if (phase >= 1.0f)
        phase -= 1.0f;

    envelope += (velocity - envelope) * (velocity > envelope ? 0.004f : 0.002f);
    if (velocity == 0.0f && envelope < 0.0001f)
        note = -1;

    return std::sin (phase * 2.0f * pi) * envelope * 0.10f;
}

void MainProcessor::PreviewVoice::start (int midiNote, float gain) noexcept
{
    note = midiNote;
    velocity = gain;
}

void MainProcessor::requestNotePreview (int midiNote, bool pressed) noexcept
{
    if (midiNote >= 0 && midiNote <= 127)
        post (EngineEvent::chordNote (midiNote, pressed));
}

void MainProcessor::requestRecordingCountIn() noexcept
{
    recordingStartSignal.store (false, std::memory_order_release);
    recordingCountInCancelled.store (false, std::memory_order_release);
    recordingCountIn.store (4, std::memory_order_relaxed);
    recordingCountInRequested.store (true, std::memory_order_release);
}

void MainProcessor::cancelRecordingCountIn() noexcept
{
    recordingCountInRequested.store (false, std::memory_order_release);
    recordingCountInCancelled.store (true, std::memory_order_release);
    recordingCountIn.store (0, std::memory_order_relaxed);
}

void MainProcessor::post (const EngineEvent& event) noexcept
{
    if (! uiToAudio.push (event))
        droppedEvents.fetch_add (1, std::memory_order_relaxed); // fila cheia: contabiliza
}

void MainProcessor::pushFullState()
{
    post (EngineEvent::setBpm (projectState.getBpm()));
    post (EngineEvent::setPlaying (projectState.isPlaying()));

    for (int track = 0; track < kNumTracks; ++track)
    {
        post (EngineEvent::setTrackMask (track, projectState.getTrackMask (track)));

        for (int step = 0; step < kNumSteps; ++step)
            post (EngineEvent::setStepVelocity (track, step, projectState.getStepVelocity (track, step)));
    }

    pushPlaylistState();
}

void MainProcessor::pushPlaylistState()
{
    const int numTracks = projectState.getNumTracks();

    // Todos os slots são reenviados: ao remover uma faixa do meio, os slots
    // seguintes "sobem" e a engine precisa enxergar a nova ordem.
    for (int slot = 0; slot < kMaxPlaylistTracks; ++slot)
    {
        const auto track = slot < numTracks ? projectState.getTrackByIndex (slot) : nullptr;

        post (EngineEvent::setTrackType (slot, track != nullptr ? static_cast<int> (track->getType()) : -1));
        post (EngineEvent::setTrackGain (slot, track != nullptr
                                                   ? fader::gainToPermille (fader::toGain (track->getGain()))
                                                   : 1000));
    }
}

void MainProcessor::valueTreePropertyChanged (juce::ValueTree& changedTree, const juce::Identifier& property)
{
    if (changedTree == projectState.getTree())
    {
        if (property == IDs::bpm)
            post (EngineEvent::setBpm (projectState.getBpm()));
    }
    else if (changedTree == projectState.getSession())
    {
        if (property == IDs::playing)
            post (EngineEvent::setPlaying (projectState.isPlaying()));
        else if (property == IDs::recording)
        {
            if (projectState.isRecording())
            {
                const auto destination = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                             .getChildFile ("mfdaw-recording.wav");
                recordingStartBar.store (getCurrentBar(), std::memory_order_relaxed);
                audioRecorder->startRecording (destination);
            }
            else
            {
                audioRecorder->stopRecording();
                if (audioRecorder->getCurrentFile().existsAsFile())
                    projectState.addAudioClip ({ audioRecorder->getCurrentFile().getFullPathName(),
                                                 recordingStartBar.load (std::memory_order_relaxed),
                                                 audioRecorder->getRecordedSeconds() });
            }
        }
    }
    else if (changedTree.hasType (IDs::playlistTrack))
    {
        // Fader pré-mix arrastado (ou desfeito): só o slot afetado viaja pela fila.
        const int slot = changedTree.getParent().indexOf (changedTree);

        if (property == IDs::gain && slot >= 0 && slot < kMaxPlaylistTracks)
        {
            const float position = static_cast<float> (changedTree.getProperty (IDs::gain,
                                                                                static_cast<double> (fader::kDefaultPosition)));
            post (EngineEvent::setTrackGain (slot, fader::gainToPermille (fader::toGain (position))));
        }
    }
    else if (changedTree.hasType (IDs::track))
    {
        const int track = changedTree.getParent().indexOf (changedTree);

        if (property == IDs::mask && track >= 0 && track < kNumTracks)
            post (EngineEvent::setTrackMask (track, projectState.getTrackMask (track)));
        else if (property.toString().startsWith ("velocity") && track >= 0 && track < kNumTracks)
        {
            const int step = property.toString().substring (8).getIntValue();

            if (step >= 0 && step < kNumSteps)
                post (EngineEvent::setStepVelocity (track, step, projectState.getStepVelocity (track, step)));
        }
    }
}

void MainProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    // Faixa adicionada à Playlist => ressincroniza os slots.
    if (parent.hasType (IDs::playlist))
    {
        pushPlaylistState();
        return;
    }

    // Sequenciador substituído (ex.: carregar projeto) => ressincroniza.
    if (parent == projectState.getTree() || parent.hasType (IDs::sequencer))
        pushFullState();
}

void MainProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (IDs::playlist))
    {
        pushPlaylistState();
        return;
    }

    if (parent == projectState.getTree() || parent.hasType (IDs::sequencer))
        pushFullState();
}

void MainProcessor::valueTreeRedirected (juce::ValueTree&)
{
    pushFullState();
}

// ===========================================================================
// Ciclo de vida do áudio
// ===========================================================================
void MainProcessor::prepareToPlay (double newSampleRate, int /*samplesPerBlock*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    audioRecorder->prepare (sampleRate, 0);

    for (auto& voice : voices)
        voice.prepare (sampleRate);

    recomputeTiming();
}

bool MainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    const bool inSupported = in.isDisabled() || in == juce::AudioChannelSet::stereo()
                          || in == juce::AudioChannelSet::mono();
    const bool outSupported = out == juce::AudioChannelSet::stereo()
                           || out == juce::AudioChannelSet::mono();
    return inSupported && outSupported;
}

// ===========================================================================
// AUDIO THREAD — nada abaixo pode alocar, bloquear ou fazer I/O
// ===========================================================================
void MainProcessor::recomputeTiming() noexcept
{
    // 1 passo = 1/16 de compasso = 1/4 de batida.
    samplesPerStep = sampleRate * 60.0 / (static_cast<double> (bpm) * 4.0);
}

void MainProcessor::drainEvents() noexcept
{
    EngineEvent event;

    while (uiToAudio.pop (event))
    {
        switch (event.type)
        {
            case EngineEvent::Type::SetBpm:
                bpm = std::clamp (static_cast<int> (event.a), kMinBpm, kMaxBpm);
                recomputeTiming();
                break;

            case EngineEvent::Type::SetPlaying:
            {
                if (event.a == 2)
                {
                    playing = false;
                    nextStep = 0;
                    playheadSampleIndex = 0;
                    samplesToNextStep = 0.0;
                    swingTriggerCountdown = -1;
                    swingTriggerStep = -1;
                    playheadStep.store (-1, std::memory_order_relaxed);
                    currentBar.store (0, std::memory_order_relaxed);   // Stop = volta ao início da Playlist
                }
                else if (event.a == 1)
                {
                    playing = true;
                }
                else
                {
                    playing = false;
                    swingTriggerCountdown = -1;
                    swingTriggerStep = -1;
                    playheadStep.store (-1, std::memory_order_relaxed);
                }
                break;
            }

            case EngineEvent::Type::SetTrackMask:
                if (event.a >= 0 && event.a < kNumTracks)
                    masks[static_cast<std::size_t> (event.a)] = static_cast<int> (event.b);
                break;

            case EngineEvent::Type::SetStepVelocity:
                if (event.a >= 0 && event.a < kNumTracks
                    && event.b >= 0 && event.b < kNumSteps
                    && event.c >= 0 && event.c <= 127)
                {
                    velocities[static_cast<std::size_t> (event.a)][static_cast<std::size_t> (event.b)]
                        = static_cast<std::uint8_t> (event.c);
                }
                break;

            case EngineEvent::Type::SetTrackType:
                if (event.a >= 0 && event.a < kMaxPlaylistTracks)
                {
                    slotTypes[static_cast<std::size_t> (event.a)] = isValidTrackType (event.b) ? event.b : -1;
                    recomputeBusGains();
                }
                break;

            case EngineEvent::Type::SetTrackGain:
                if (event.a >= 0 && event.a < kMaxPlaylistTracks)
                {
                    slotGains[static_cast<std::size_t> (event.a)] = fader::permilleToGain (event.b);
                    recomputeBusGains();
                }
                break;

            case EngineEvent::Type::ChordNote:
                if (event.a >= 0 && event.a <= 127)
                {
                    if (event.b != 0)
                    {
                        for (auto& voice : previewVoices)
                        {
                            if (voice.note < 0 || voice.note == event.a)
                            {
                                voice.start (event.a, static_cast<float> (event.c) / 127.0f);
                                break;
                            }
                        }
                    }
                    else
                    {
                        for (auto& voice : previewVoices)
                        {
                            if (voice.note == event.a)
                            {
                                voice.release();
                                break;
                            }
                        }
                    }
                }
                break;
        }
    }
}

void MainProcessor::recomputeBusGains() noexcept
{
    // Primeira faixa de cada tipo manda no seu barramento; sem faixa = ganho 1.
    float drum = 1.0f;
    float harmony = 1.0f;
    float melody = 1.0f;
    bool hasDrum = false, hasHarmony = false, hasMelody = false;

    for (std::size_t slot = 0; slot < slotTypes.size(); ++slot)
    {
        switch (slotTypes[slot])
        {
            case static_cast<int> (TrackType::Drum):
                if (! hasDrum) { drum = slotGains[slot]; hasDrum = true; }
                break;
            case static_cast<int> (TrackType::Harmony):
                if (! hasHarmony) { harmony = slotGains[slot]; hasHarmony = true; }
                break;
            case static_cast<int> (TrackType::Melody):
                if (! hasMelody) { melody = slotGains[slot]; hasMelody = true; }
                break;
            default:
                break;
        }
    }

    drumBusGain = drum;
    previewBusGain = hasHarmony ? harmony : melody;

    // Parado não há "rampa" audível: assume o alvo direto (evita transiente ao dar Play).
    if (! playing)
        drumGainSmoothed = drumBusGain;
}

int MainProcessor::getSwingOffsetSamples (int step) const noexcept
{
    if ((step & 1) == 0)
        return 0;

    const float amount = juce::jlimit (0.0f, 1.0f, projectState.getSwingAmount());
    const double maximumOffset = samplesPerStep * 0.5;
    return juce::jlimit (0, static_cast<int> (samplesPerStep * 0.5),
                         static_cast<int> (std::round (maximumOffset * amount)));
}

void MainProcessor::triggerStep (int step) noexcept
{
    for (std::size_t track = 0; track < static_cast<std::size_t> (kNumTracks); ++track)
    {
        if (((masks[track] >> step) & 1) != 0)
        {
            velocityGains[track] = static_cast<float> (velocities[track][static_cast<std::size_t> (step)]) / 127.0f;
            activeSamples[track] = sampleManager.getSample (static_cast<int> (track));
            samplePositions[track] = 0.0f;

            if (activeSamples[track] == nullptr)
                voices[track].trigger();
        }
    }
}

void MainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Eventos MIDI de entrada serão tratados nos Passos 2/3 (acordes / piano roll).

    drainEvents();

    if (recordingCountInCancelled.exchange (false, std::memory_order_acq_rel))
    {
        recordingCountInActive = false;
        recordingCountInBeats = 0;
        recordingStartSignal.store (false, std::memory_order_release);
    }

    if (! recordingCountInActive
        && recordingCountInRequested.load (std::memory_order_acquire)
        && playing)
    {
        recordingCountInRequested.store (false, std::memory_order_release);
        recordingCountInActive = true;
        recordingCountInBeats = 4;
        recordingCountIn.store (4, std::memory_order_relaxed);
    }

    if (audioRecorder->isRecording())
        audioRecorder->pushAudio (buffer);
    buffer.clear();

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (playing)
            ++playheadSampleIndex;

        if (swingTriggerCountdown >= 0)
        {
            --swingTriggerCountdown;

            if (swingTriggerCountdown <= 0)
            {
                triggerStep (swingTriggerStep);

                for (int track = 0; track < kNumTracks; ++track)
                {
                    if (((masks[static_cast<std::size_t> (track)] >> swingTriggerStep) & 1) != 0)
                    {
                        const auto note = juce::MidiMessage::noteOn (10, 36 + track, 1.0f);
                        midi.addEvent (note, i);
                    }
                }

                swingTriggerCountdown = -1;
                swingTriggerStep = -1;
            }
        }

        if (playing)
        {
            if (samplesToNextStep <= 0.0)
            {
                const int scheduledStep = nextStep;
                const int swingOffset = getSwingOffsetSamples (scheduledStep);

                if (recordingCountInActive && (scheduledStep % 4) == 0)
                {
                    if (recordingCountInBeats > 0)
                    {
                        recordingCountIn.store (recordingCountInBeats, std::memory_order_relaxed);
                        if (recordingCountInBeats == 1)
                            recordingCountInBeats = 0;
                        else
                            --recordingCountInBeats;
                    }
                    else
                    {
                        recordingStartSignal.store (true, std::memory_order_release);
                        recordingCountIn.store (0, std::memory_order_relaxed);
                        recordingCountInActive = false;
                    }
                }

                if (swingOffset > 0)
                {
                    swingTriggerStep = scheduledStep;
                    swingTriggerCountdown = swingOffset;
                }
                else
                {
                    triggerStep (scheduledStep);

                    for (int track = 0; track < kNumTracks; ++track)
                    {
                        if (((masks[static_cast<std::size_t> (track)] >> scheduledStep) & 1) != 0)
                        {
                            const auto note = juce::MidiMessage::noteOn (10, 36 + track, 1.0f);
                            midi.addEvent (note, i);
                        }
                    }
                }

                playheadStep.store (scheduledStep, std::memory_order_relaxed);
                if (scheduledStep == 0 && playheadSampleIndex > 0)
                    currentBar.fetch_add (1, std::memory_order_relaxed);
                nextStep = (scheduledStep + 1) % kNumSteps;
                samplesToNextStep += samplesPerStep;
            }

            samplesToNextStep -= 1.0;
        }

        float drumMix = 0.0f;
        for (std::size_t track = 0; track < static_cast<std::size_t> (kNumTracks); ++track)
        {
            if (activeSamples[track] != nullptr)
            {
                const auto& sampleBuffer = activeSamples[track]->buffer;
                const int sampleCount = sampleBuffer.getNumSamples();
                const int position = static_cast<int> (samplePositions[track]);

                if (position < sampleCount)
                {
                    const float fraction = samplePositions[track] - static_cast<float> (position);
                    const int nextPosition = juce::jmin (sampleCount - 1, position + 1);
                    float sample = 0.0f;

                    for (int channel = 0; channel < sampleBuffer.getNumChannels(); ++channel)
                    {
                        const float a = sampleBuffer.getSample (channel, position);
                        const float b = sampleBuffer.getSample (channel, nextPosition);
                        sample += a + fraction * (b - a);
                    }

                    sample /= static_cast<float> (juce::jmax (1, sampleBuffer.getNumChannels()));

                    constexpr int fadeSamples = 32;
                    const int remaining = sampleCount - position;
                    const float startFade = juce::jmin (1.0f,
                                                        static_cast<float> (position + 1) / fadeSamples);
                    const float endFade = juce::jmin (1.0f,
                                                      static_cast<float> (remaining) / fadeSamples);
                    sample *= juce::jmin (startFade, endFade);

                    drumMix += sample * velocityGains[track];
                    [[maybe_unused]] const float voiceEnvelope = voices[track].process(); // drena envelopes sintéticas durante custom sample playback
                    samplePositions[track] += 1.0f;
                }
                else
                {
                    activeSamples[track].reset();
                }
            }
            else
            {
                const float voiceSample = voices[track].process();
                drumMix += voiceSample * velocityGains[track];
            }
        }

        float previewMix = 0.0f;
        for (auto& previewVoice : previewVoices)
            previewMix += previewVoice.process (sampleRate);

        // Fader pré-mix (gain staging) por barramento. Interpolação de 1 polo
        // por amostra elimina "zipper noise" ao arrastar o fader.
        if (playing)
            drumGainSmoothed += (drumBusGain - drumGainSmoothed) * kGainSmoothing;
        else
            drumGainSmoothed = drumBusGain;

        previewGainSmoothed += (previewBusGain - previewGainSmoothed) * kGainSmoothing;

        const float mix = std::clamp ((drumMix * drumGainSmoothed + previewMix * previewGainSmoothed) * kMasterGain,
                                      -1.0f, 1.0f);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, mix);
    }
}

// ===========================================================================
// Editor e persistência  (MESSAGE THREAD)
// ===========================================================================
juce::AudioProcessorEditor* MainProcessor::createEditor()
{
    return new MainEditor (*this);
}

void MainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Hosts chamam isto na message thread; o ValueTree não é thread-safe.
    const auto jsonText = projectState.createJsonString();
    const auto json = jsonText.toUTF8();
    destData.reset();
    destData.append (json, static_cast<size_t> (jsonText.getNumBytesAsUTF8()));
}

void MainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto json = juce::String::fromUTF8 (static_cast<const char*> (data),
                                               static_cast<size_t> (sizeInBytes));

    if (projectState.loadFromJsonString (json))
        pushFullState(); // garante o motor em dia mesmo se nada "mudou" na árvore
}

} // namespace mf

// Ponto de entrada exigido pelo juce_audio_plugin_client (Standalone e VST3).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mf::MainProcessor();
}
