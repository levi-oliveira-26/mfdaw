#include "Engine/MainProcessor.h"

#include <algorithm>
#include <cmath>

#include "UI/MainEditor.h"

namespace mf
{
// ===========================================================================
// Construção / destruição  (MESSAGE THREAD)
// ===========================================================================
MainProcessor::MainProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    voices[0].setKind (DrumVoice::Kind::Kick);
    voices[1].setKind (DrumVoice::Kind::Snare);
    voices[2].setKind (DrumVoice::Kind::HiHat);
    voices[3].setKind (DrumVoice::Kind::Perc);

    for (auto& voice : voices)
        voice.prepare (sampleRate);

    for (auto& trackVelocities : velocities)
        trackVelocities.fill (100);

    recomputeTiming();

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
    // Sequenciador substituído (ex.: carregar projeto) => ressincroniza.
    if (parent == projectState.getTree() || parent.hasType (IDs::sequencer))
        pushFullState();
}

void MainProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
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

    for (auto& voice : voices)
        voice.prepare (sampleRate);

    recomputeTiming();
}

bool MainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
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
                const bool shouldPlay = event.a != 0;

                if (shouldPlay && ! playing)
                {
                    nextStep = 0;
                    samplesToNextStep = 0.0; // dispara o passo 1 imediatamente
                }
                else if (! shouldPlay)
                {
                    swingTriggerCountdown = -1;
                    swingTriggerStep = -1;
                    playheadStep.store (-1, std::memory_order_relaxed);
                }

                playing = shouldPlay;
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
        }
    }
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
    buffer.clear();

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
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
                nextStep = (scheduledStep + 1) % kNumSteps;
                samplesToNextStep += samplesPerStep;
            }

            samplesToNextStep -= 1.0;
        }

        float mix = 0.0f;
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

                    mix += sample * velocityGains[track];
                    juce::ignoreUnused (voices[track].process()); // drena envelopes sintéticas durante custom sample playback
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
                mix += voiceSample * velocityGains[track];
            }
        }

        mix = std::clamp (mix * kMasterGain, -1.0f, 1.0f);

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
