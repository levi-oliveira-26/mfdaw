#include "Engine/MainProcessor.h"

#include <algorithm>

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
        post (EngineEvent::setTrackMask (track, projectState.getTrackMask (track)));
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
    else if (changedTree.hasType (IDs::track) && property == IDs::mask)
    {
        const int track = changedTree.getParent().indexOf (changedTree);

        if (track >= 0 && track < kNumTracks)
            post (EngineEvent::setTrackMask (track, projectState.getTrackMask (track)));
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
                    playheadStep.store (-1, std::memory_order_relaxed);
                }

                playing = shouldPlay;
                break;
            }

            case EngineEvent::Type::SetTrackMask:
                if (event.a >= 0 && event.a < kNumTracks)
                    masks[static_cast<std::size_t> (event.a)] = static_cast<int> (event.b);
                break;
        }
    }
}

void MainProcessor::triggerStep (int step) noexcept
{
    for (std::size_t track = 0; track < static_cast<std::size_t> (kNumTracks); ++track)
        if (((masks[track] >> step) & 1) != 0)
            voices[track].trigger();
}

void MainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    // Eventos MIDI de entrada serão tratados nos Passos 2/3 (acordes / piano roll).

    drainEvents();
    buffer.clear();

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        if (playing)
        {
            if (samplesToNextStep <= 0.0)
            {
                triggerStep (nextStep);
                playheadStep.store (nextStep, std::memory_order_relaxed);
                nextStep = (nextStep + 1) % kNumSteps;
                samplesToNextStep += samplesPerStep;
            }

            samplesToNextStep -= 1.0;
        }

        float mix = 0.0f;
        for (auto& voice : voices)
            mix += voice.process();

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
