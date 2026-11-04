#include "Engine/MainProcessor.h"

#include <algorithm>
#include <cmath>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Core/ChordGenerator.h"
#include "Core/FaderScale.h"
#include "Engine/SampleProcessing.h"
#include "UI/MainEditor.h"

namespace mf
{
class MainProcessor::TakeLoadJob final : public juce::ThreadPoolJob
{
public:
    TakeLoadJob (MainProcessor& ownerIn, int slotIn, int takeIndexIn, int generationIn,
                 juce::String keyIn, juce::String pathIn, SampleEdit editIn)
        : juce::ThreadPoolJob ("M.F. DAW take loader"), owner (ownerIn), slot (slotIn),
          takeIndex (takeIndexIn), generation (generationIn), key (std::move (keyIn)),
          path (std::move (pathIn)), edit (editIn) {}

    JobStatus runJob() override
    {
        auto buffer = MainProcessor::loadTakeBuffer (path, edit);
        owner.finishTakeLoad (slot, takeIndex, generation, key, std::move (buffer));
        return jobHasFinished;
    }

private:
    MainProcessor& owner;
    int slot;
    int takeIndex;
    int generation;
    juce::String key;     // identifica "arquivo + edições": muda quando o usuário edita
    juce::String path;
    SampleEdit edit;
};

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
    for (auto& gain : slotGains)
        gain.store (1.0f, std::memory_order_relaxed);
    slotGainSmoothed.fill (1.0f);

    recomputeTiming();
    audioRecorder->prepare (sampleRate, 0);

    projectState.getTree().addListener (this);
    projectState.getSession().addListener (this);

    // Sincroniza o motor com o estado inicial do projeto.
    masterGain.store (projectState.getMasterGain(), std::memory_order_release);
    masterMuted.store (projectState.isMasterMuted(), std::memory_order_release);
    masterSolo.store (projectState.isMasterSolo(), std::memory_order_release);
    masterBrightness.store (projectState.getMasterBrightness(), std::memory_order_release);
    masterSpace.store (projectState.getMasterSpace(), std::memory_order_release);
    pushFullState();
    startTimerHz (10); // recolhe buffers aposentados após a audio thread terminar de usá-los
}

MainProcessor::~MainProcessor()
{
    stopTimer();
    takeLoaderPool.removeAllJobs (true, -1);
    cancelPendingUpdate();
    {
        const juce::ScopedLock lock (takeLoadLock);
        completedTakeLoads.clear();
    }
    projectState.getTree().removeListener (this);
    projectState.getSession().removeListener (this);
    takePlayer.releaseAll();
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

void MainProcessor::setRecordingTarget (int trackId) noexcept
{
    recordingTargetTrack.store (trackId, std::memory_order_relaxed);
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

void MainProcessor::pushArrangementState()
{
    post (EngineEvent::setDrumBars (projectState.getDrumBarMask()));

    // Máscara de compassos de CADA slot (0 para vazios / não-Melodia): assim um slot
    // que deixou de ser Melodia nunca guarda uma máscara velha.
    const int numTracks = projectState.getNumTracks();
    for (int slot = 0; slot < kMaxPlaylistTracks; ++slot)
    {
        const auto track = slot < numTracks ? projectState.getTrackByIndex (slot) : nullptr;
        const bool hasPattern = track != nullptr
                                && (track->getType() == TrackType::Melody || track->getType() == TrackType::Harmony
                                    || track->getType() == TrackType::Record);
        post (EngineEvent::setSlotBars (slot, hasPattern ? projectState.getTrackBarMask (track->getId()) : 0));
    }
    pushClipRegions();
}

std::shared_ptr<TakeBuffer> MainProcessor::loadTakeBuffer (const juce::String& path, const SampleEdit& edit)
{
    const juce::File file (path);
    if (! file.existsAsFile())
        return nullptr;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0
        || static_cast<double> (reader->lengthInSamples) > reader->sampleRate * kMaxPlayableTakeSeconds)
        return nullptr;

    const auto length = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> temp (static_cast<int> (reader->numChannels), length);
    reader->read (&temp, 0, length, 0, true, true);

    auto result = std::make_shared<TakeBuffer>();
    result->sampleRate = reader->sampleRate;
    result->samples.assign (static_cast<std::size_t> (length), 0.0f);

    const float channelScale = 1.0f / static_cast<float> (temp.getNumChannels());   // mono = média dos canais
    for (int ch = 0; ch < temp.getNumChannels(); ++ch)
    {
        const float* in = temp.getReadPointer (ch);
        for (int i = 0; i < length; ++i)
            result->samples[static_cast<std::size_t> (i)] += in[i] * channelScale;
    }

    // Edições da faixa de Amostra: processadas aqui (thread de carga), nunca na audio thread.
    if (! edit.isDefault())
        result->samples = sampleproc::process (result->samples, result->sampleRate, edit);

    return result;
}

void MainProcessor::syncTakes()
{
    const int numTracks = projectState.getNumTracks();

    for (int slot = 0; slot < kMaxPlaylistTracks; ++slot)
    {
        const auto track = slot < numTracks ? projectState.getTrackByIndex (slot) : nullptr;
        const auto slotIndex = static_cast<std::size_t> (slot);
        const bool isAudioTrack = track != nullptr
                                  && (track->getType() == TrackType::Record || track->getType() == TrackType::Sample);
        const auto takes = isAudioTrack ? projectState.getTakes (track->getId()) : std::vector<TakeData> {};
        std::array<bool, kMaxTakesPerTrack> requiredTakes {};

        if (isAudioTrack)
            for (const auto& clip : projectState.getClips (track->getId()))
                for (int i = 0; i < static_cast<int> (takes.size()); ++i)
                    if (takes[static_cast<std::size_t> (i)].id == clip.takeId)
                        requiredTakes[static_cast<std::size_t> (i)] = true;

        for (int takeIndex = 0; takeIndex < kMaxTakesPerTrack; ++takeIndex)
        {
            const auto takeIdx = static_cast<std::size_t> (takeIndex);
            const bool needed = takeIndex < static_cast<int> (takes.size()) && requiredTakes[takeIdx];
            const juce::String path = needed ? takes[static_cast<std::size_t> (takeIndex)].filePath : juce::String {};
            const SampleEdit edit = needed ? takes[static_cast<std::size_t> (takeIndex)].edit : SampleEdit {};
            // A chave inclui as edições: mexer num controle recarrega o áudio já processado.
            const juce::String key = path.isEmpty() ? juce::String {}
                                                    : path + "|" + juce::String (edit.signature());

            if (key == takePaths[slotIndex][takeIdx])
                continue;

            takePaths[slotIndex][takeIdx] = key;
            const int generation = ++takeLoadGeneration[slotIndex][takeIdx];

            // Retire the old publication immediately. A clip cannot accidentally
            // play a previous take while the replacement WAV is still loading.
            takePlayer.publishTake (slot, takeIndex, nullptr);
            auto old = std::move (takeBuffers[slotIndex][takeIdx]);
            if (old != nullptr)
                takeGraveyard.push_back (std::move (old));

            if (path.isNotEmpty())
                takeLoaderPool.addJob (new TakeLoadJob (*this, slot, takeIndex, generation, key, path, edit), true);
        }
    }

    pushClipRegions();
    takeGraveyard.erase (std::remove_if (takeGraveyard.begin(), takeGraveyard.end(),
                                         [this] (const std::shared_ptr<TakeBuffer>& b) { return takePlayer.isRetired (b.get()); }),
                         takeGraveyard.end());
}

void MainProcessor::finishTakeLoad (int slot, int takeIndex, int generation, const juce::String& path,
                                    std::shared_ptr<TakeBuffer> buffer)
{
    const juce::ScopedLock lock (takeLoadLock);
    completedTakeLoads.push_back ({ slot, takeIndex, generation, path, std::move (buffer) });
    triggerAsyncUpdate();
}

void MainProcessor::handleAsyncUpdate()
{
    std::vector<TakeLoadResult> completed;
    {
        const juce::ScopedLock lock (takeLoadLock);
        completed.swap (completedTakeLoads);
    }

    for (auto& result : completed)
    {
        if (result.slot < 0 || result.slot >= kMaxPlaylistTracks
            || result.takeIndex < 0 || result.takeIndex >= kMaxTakesPerTrack)
            continue;

        const auto slot = static_cast<std::size_t> (result.slot);
        const auto takeIndex = static_cast<std::size_t> (result.takeIndex);
        if (takeLoadGeneration[slot][takeIndex] != result.generation
            || takePaths[slot][takeIndex] != result.path)
            continue; // resultado obsoleto: o projeto mudou durante a leitura

        takeBuffers[slot][takeIndex] = std::move (result.buffer);
        takePlayer.publishTake (result.slot, result.takeIndex, takeBuffers[slot][takeIndex].get());
    }

    takeGraveyard.erase (std::remove_if (takeGraveyard.begin(), takeGraveyard.end(),
                                         [this] (const std::shared_ptr<TakeBuffer>& b) { return takePlayer.isRetired (b.get()); }),
                         takeGraveyard.end());
}
void MainProcessor::timerCallback()
{
    takeGraveyard.erase (std::remove_if (takeGraveyard.begin(), takeGraveyard.end(),
                                         [this] (const std::shared_ptr<TakeBuffer>& b) { return takePlayer.isRetired (b.get()); }),
                         takeGraveyard.end());
}

void MainProcessor::pushClipRegions()
{
    const int numTracks = projectState.getNumTracks();
    for (int slot = 0; slot < kMaxPlaylistTracks; ++slot)
    {
        const auto track = slot < numTracks ? projectState.getTrackByIndex (slot) : nullptr;
        int regionIndex = 0;

        if (track != nullptr && (track->getType() == TrackType::Record || track->getType() == TrackType::Sample))
        {
            const auto takes = projectState.getTakes (track->getId());
            for (const auto& clip : projectState.getClips (track->getId()))
            {
                int takeIndex = -1;
                for (int i = 0; i < static_cast<int> (takes.size()); ++i)
                    if (takes[static_cast<std::size_t> (i)].id == clip.takeId)
                    {
                        takeIndex = i;
                        break;
                    }

                const int startStep = clip.startBar * kStepsPerBar;
                const int endStep = (clip.startBar + clip.lengthBars) * kStepsPerBar;
                post (EngineEvent::setClipRegion (slot, regionIndex, startStep, endStep, takeIndex));
                ++regionIndex;
                if (regionIndex >= TakePlayer::kMaxRegionsPerSlot)
                    break;
            }
        }

        for (; regionIndex < TakePlayer::kMaxRegionsPerSlot; ++regionIndex)
            post (EngineEvent::setClipRegion (slot, regionIndex, -1, -1, -1));
    }
}

void MainProcessor::pushHarmonySlot (int slot)
{
    if (slot < 0 || slot >= kMaxPlaylistTracks)
        return;

    const auto track = slot < projectState.getNumTracks() ? projectState.getTrackByIndex (slot) : nullptr;
    const bool isHarmony = track != nullptr && track->getType() == TrackType::Harmony;
    const int armedMask = isHarmony ? projectState.getHarmonyArmedMask (track->getId()) : 0;

    for (int card = 0; card < kHarmonyCards; ++card)
    {
        // Os acordes derivam da tonalidade ATUAL do projeto (mudar o tom transpõe a progressão).
        const auto notes = ChordGenerator::generateNotes (projectState.getKeyRoot(), projectState.getScale(), card);
        post (EngineEvent::setHarmonyChord (slot, card, isHarmony && ((armedMask >> card) & 1) != 0,
                                            notes[0], notes[1], notes[2]));
    }
}

void MainProcessor::pushHarmonyState()
{
    for (int slot = 0; slot < kMaxPlaylistTracks; ++slot)
        pushHarmonySlot (slot);
}

void MainProcessor::pushMelodyState()
{
    const int numTracks = projectState.getNumTracks();

    for (int slot = 0; slot < kMaxPlaylistTracks; ++slot)
    {
        post (EngineEvent::setMelodyClear (slot));

        const auto track = slot < numTracks ? projectState.getTrackByIndex (slot) : nullptr;
        if (track == nullptr || track->getType() != TrackType::Melody)
            continue;

        for (const auto& note : projectState.getMelodyNotes (track->getId()))
            post (EngineEvent::setMelodyNote (slot, note.step, note.pitch, true));
    }
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

    pushMelodyState();        // slots mudam de lugar: notas de Melodia acompanham
    pushHarmonyState();       // ... e os cartões de Harmonia
    pushArrangementState();   // remover/carregar faixa muda os clipes (bateria e melodia)
    syncTakes();              // ... e o take ativo de cada faixa de Voz
}

void MainProcessor::valueTreePropertyChanged (juce::ValueTree& changedTree, const juce::Identifier& property)
{
    if (changedTree == projectState.getTree())
    {
        if (property == IDs::masterGain)
            masterGain.store (projectState.getMasterGain(), std::memory_order_release);
        else if (property == IDs::masterMuted)
            masterMuted.store (projectState.isMasterMuted(), std::memory_order_release);
        else if (property == IDs::masterSolo)
            masterSolo.store (projectState.isMasterSolo(), std::memory_order_release);
        else if (property == IDs::masterBrightness)
            masterBrightness.store (projectState.getMasterBrightness(), std::memory_order_release);
        else if (property == IDs::masterSpace)
            masterSpace.store (projectState.getMasterSpace(), std::memory_order_release);
    }

    if (changedTree == projectState.getTree())
    {
        if (property == IDs::bpm)
            post (EngineEvent::setBpm (projectState.getBpm()));
        else if (property == IDs::keyRoot || property == IDs::scale)
            pushHarmonyState();   // os acordes são derivados do tom
    }
    else if (changedTree == projectState.getSession())
    {
        if (property == IDs::playing)
            post (EngineEvent::setPlaying (projectState.isPlaying()));
        else if (property == IDs::recording)
        {
            if (projectState.isRecording())
            {
                // Um arquivo por take: gravar de novo nunca sobrescreve o take anterior.
                const auto takesDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                          .getChildFile ("M.F. DAW").getChildFile ("Takes");
                takesDir.createDirectory();
                const auto destination = takesDir.getChildFile (
                    "take-" + juce::String::toHexString (juce::Time::currentTimeMillis()) + ".wav");
                activeRecordingTrackId = recordingTargetTrack.load (std::memory_order_relaxed);
                audioRecorder->startRecording (destination);
            }
            else
            {
                audioRecorder->stopRecording();
                const auto file = audioRecorder->getCurrentFile();
                if (file.existsAsFile())
                {
                    const double seconds = audioRecorder->getRecordedSeconds();
                    const int startStep = juce::jlimit (0, kPlaylistSteps - 1,
                                                        recordingStartStep.load (std::memory_order_relaxed));

                    if (projectState.addRecordedTake (activeRecordingTrackId, file.getFullPathName(), startStep, seconds) < 0)
                        projectState.addAudioClip ({ file.getFullPathName(), startStep / kStepsPerBar, seconds });   // sem faixa de Voz: legado
                }
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
        else if (property == IDs::chordMask)
            pushHarmonySlot (slot);   // cartão ligado/desligado (inclui undo/redo)
        else if (property == IDs::activeTake)
            syncTakes();              // outro take escolhido (inclui undo/redo)
    }
    else if (changedTree.hasType (IDs::take))
    {
        // Edição da faixa de Amostra mudou (inclui undo/redo): recarrega o áudio processado.
        if (property == IDs::trimSilence || property == IDs::normalizeAudio
            || property == IDs::fadeInMs || property == IDs::fadeOutMs)
            syncTakes();
    }
    else if (changedTree.hasType (IDs::clip))
    {
        // Clipe movido / redimensionado (ou desfeito): só interessa a posição.
        if (property == IDs::startBar || property == IDs::lengthBars)
        {
            pushArrangementState();
            syncTakes();
        }
        else if (property == IDs::takeId)
            syncTakes();
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

void MainProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    // Faixa adicionada à Playlist => ressincroniza os slots.
    if (parent.hasType (IDs::playlist))
    {
        pushPlaylistState();
        return;
    }

    if (parent.hasType (IDs::playlistTrack))
    {
        if (child.hasType (IDs::note))
            postMelodyNoteChange (parent, child, true);   // nota criada (ou refeita por redo)
        else if (child.hasType (IDs::take))
            syncTakes();
        else
        {
            pushArrangementState();
            if (child.hasType (IDs::clip))
                syncTakes();
        }
        return;
    }

    // Sequenciador substituído (ex.: carregar projeto) => ressincroniza.
    if (parent == projectState.getTree() || parent.hasType (IDs::sequencer))
        pushFullState();
}

void MainProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (parent.hasType (IDs::playlist))
    {
        pushPlaylistState();
        return;
    }

    if (parent.hasType (IDs::playlistTrack))
    {
        if (child.hasType (IDs::note))
            postMelodyNoteChange (parent, child, false);  // nota apagada (ou desfeita)
        else if (child.hasType (IDs::take))
            syncTakes();
        else
        {
            pushArrangementState();
            if (child.hasType (IDs::clip))
                syncTakes();
        }
        return;
    }

    if (parent == projectState.getTree() || parent.hasType (IDs::sequencer))
        pushFullState();
}

void MainProcessor::postMelodyNoteChange (const juce::ValueTree& trackNode, const juce::ValueTree& noteNode, bool on)
{
    const int slot = trackNode.getParent().indexOf (trackNode);

    if (slot >= 0 && slot < kMaxPlaylistTracks)
        post (EngineEvent::setMelodyNote (slot,
                                          static_cast<int> (noteNode.getProperty (IDs::noteStep, 0)),
                                          static_cast<int> (noteNode.getProperty (IDs::notePitch, 60)),
                                          on));
}

void MainProcessor::valueTreeRedirected (juce::ValueTree&)
{
    masterGain.store (projectState.getMasterGain(), std::memory_order_release);
    masterMuted.store (projectState.isMasterMuted(), std::memory_order_release);
    masterSolo.store (projectState.isMasterSolo(), std::memory_order_release);
    masterBrightness.store (projectState.getMasterBrightness(), std::memory_order_release);
    masterSpace.store (projectState.getMasterSpace(), std::memory_order_release);
    pushFullState();
}

// ===========================================================================
// Ciclo de vida do áudio
// ===========================================================================
void MainProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    audioRecorder->prepare (sampleRate, 0);

    for (auto& voice : voices)
        voice.prepare (sampleRate);

    masterEqLowState.fill (0.0f);
    masterEqHighState.fill (0.0f);

    juce::dsp::Reverb::Parameters reverbParams;
    reverbParams.roomSize = 0.25f;
    reverbParams.damping = 0.5f;
    reverbParams.wetLevel = 0.0f;
    reverbParams.dryLevel = 1.0f;
    reverbParams.width = 1.0f;
    masterReverb.setParameters (reverbParams);
    masterReverb.prepare ({ sampleRate,
                            static_cast<juce::uint32> (juce::jmax (1, samplesPerBlock)),
                            static_cast<juce::uint32> (juce::jmax (1, getMainBusNumOutputChannels())) });

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
                    arrangementBar = 0;
                    hasScheduledSinceStop = false;
                    playheadStep.store (-1, std::memory_order_relaxed);
                    playheadBar.store (0, std::memory_order_relaxed);
                    currentBar.store (0, std::memory_order_relaxed);   // Stop = volta ao início da Playlist
                    releaseMelodyVoices();
                    releaseHarmonyVoices();
                    takePlayer.releaseAll();
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
                    releaseMelodyVoices();
                    releaseHarmonyVoices();
                    takePlayer.releaseAll();
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

            case EngineEvent::Type::SetDrumBars:
                drumBarMask = event.a & ((1 << kPlaylistBars) - 1);
                break;

            case EngineEvent::Type::SetMelodyNote:
                melodySequencer.setNote (event.a, event.b, event.c & 0xFF, (event.c & 0x100) != 0);
                break;

            case EngineEvent::Type::SetMelodyClear:
                melodySequencer.clearNotes (event.a);
                break;

            case EngineEvent::Type::SetSlotBars:
                melodySequencer.setBars (event.a, event.b);
                harmonySequencer.setBars (event.a, event.b);
                break;

            case EngineEvent::Type::SetClipRegion:
                takePlayer.setClipRegion (event.a, event.b, event.c, event.d, event.e);
                break;

            case EngineEvent::Type::SetHarmonyChord:
                harmonySequencer.setChord (event.a, event.b & 0xFF, (event.b & 0x100) != 0,
                                           harmony::unpackNote (event.c, 0), harmony::unpackNote (event.c, 1),
                                           harmony::unpackNote (event.c, 2));
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
                    slotGains[static_cast<std::size_t> (event.a)].store (
                        fader::permilleToGain (event.b), std::memory_order_relaxed);
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
                if (! hasDrum) { drum = slotGains[slot].load (std::memory_order_relaxed); hasDrum = true; }
                break;
            case static_cast<int> (TrackType::Harmony):
                if (! hasHarmony) { harmony = slotGains[slot].load (std::memory_order_relaxed); hasHarmony = true; }
                break;
            case static_cast<int> (TrackType::Melody):
                if (! hasMelody) { melody = slotGains[slot].load (std::memory_order_relaxed); hasMelody = true; }
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

void MainProcessor::triggerStepInBar (int step, int bar, int sampleOffset, juce::MidiBuffer& midi) noexcept
{
    // Melodia primeiro: as notas do passo anterior terminam e as deste começam,
    // independentemente de a Bateria estar audível neste compasso.
    releaseMelodyVoices();
    triggerMelodyStep (step, bar);
    takePlayer.triggerStep (bar, step);

    if (step == 0)   // o acorde dura o compasso inteiro: troca só no primeiro passo
    {
        releaseHarmonyVoices();
        triggerHarmonyBar (bar);
    }

    // Fora de um clipe de Bateria o passo é pulado (nem voz, nem nota MIDI).
    if (! clip::isBarAudible (drumBarMask, bar))
        return;

    triggerStep (step);

    for (int track = 0; track < kNumTracks; ++track)
    {
        if (((masks[static_cast<std::size_t> (track)] >> step) & 1) != 0)
            midi.addEvent (juce::MidiMessage::noteOn (10, 36 + track, 1.0f), sampleOffset);
    }
}

void MainProcessor::releaseMelodyVoices() noexcept
{
    for (auto& voice : melodyVoices)
        voice.release();
}

void MainProcessor::triggerMelodyStep (int step, int bar) noexcept
{
    for (int slot = 0; slot < kMaxPlaylistTracks; ++slot)
    {
        if (slotTypes[static_cast<std::size_t> (slot)] != static_cast<int> (TrackType::Melody)
            || ! melodySequencer.isAudible (slot, bar))
            continue;

        melodySequencer.forEachNote (slot, step, [this, slot] (int pitch) noexcept
        {
            auto* target = &allocateVoice (melodyVoices, melodyStealIndex);
            target->slot = slot;
            target->start (pitch, 0.8f);
        });
    }
}

void MainProcessor::releaseHarmonyVoices() noexcept
{
    for (auto& voice : harmonyVoices)
        voice.release();
}

void MainProcessor::triggerHarmonyBar (int bar) noexcept
{
    for (int slot = 0; slot < kMaxPlaylistTracks; ++slot)
    {
        if (slotTypes[static_cast<std::size_t> (slot)] != static_cast<int> (TrackType::Harmony))
            continue;

        harmonySequencer.forEachNoteInBar (slot, bar, [this, slot] (int pitch) noexcept
        {
            auto& voice = allocateVoice (harmonyVoices, harmonyStealIndex);
            voice.slot = slot;
            voice.start (pitch, 0.6f);
        });
    }
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
    takePlayer.beginBlock();

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
                triggerStepInBar (swingTriggerStep, swingTriggerBar, i, midi);

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

                // Compasso (0-based, com loop) do passo que está sendo agendado: o primeiro
                // passo 0 depois do Stop é o compasso 0; cada passo 0 seguinte avança um.
                if (scheduledStep == 0 && hasScheduledSinceStop)
                    arrangementBar = clip::nextBar (arrangementBar);
                hasScheduledSinceStop = true;

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
                        // Posição exata da gravação (passo absoluto na Playlist), publicada ANTES do sinal.
                        recordingStartStep.store (arrangementBar * kStepsPerBar + scheduledStep, std::memory_order_relaxed);
                        recordingStartSignal.store (true, std::memory_order_release);
                        recordingCountIn.store (0, std::memory_order_relaxed);
                        recordingCountInActive = false;
                    }
                }

                if (swingOffset > 0)
                {
                    swingTriggerStep = scheduledStep;
                    swingTriggerBar = arrangementBar;
                    swingTriggerCountdown = swingOffset;
                }
                else
                {
                    triggerStepInBar (scheduledStep, arrangementBar, i, midi);
                }

                playheadBar.store (arrangementBar, std::memory_order_relaxed);
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

        // Melodia: cada voz usa o fader (suavizado) da PRÓPRIA faixa.
        for (std::size_t slot = 0; slot < slotGainSmoothed.size(); ++slot)
        {
            const float targetGain = slotGains[slot].load (std::memory_order_relaxed);
            slotGainSmoothed[slot] += (targetGain - slotGainSmoothed[slot]) * kGainSmoothing;
        }

        float melodyMix = 0.0f;
        for (auto& voice : melodyVoices)
            melodyMix += voice.process (sampleRate) * slotGainSmoothed[static_cast<std::size_t> (voice.slot)];

        float harmonyMix = 0.0f;
        for (auto& voice : harmonyVoices)
            harmonyMix += voice.process (sampleRate) * slotGainSmoothed[static_cast<std::size_t> (voice.slot)];

        const float takeMix = takePlayer.process (sampleRate, slotGainSmoothed);

        float mix = std::clamp ((drumMix * drumGainSmoothed + previewMix * previewGainSmoothed
                                 + melodyMix + harmonyMix + takeMix) * kMasterGain,
                                -1.0f, 1.0f);

        const bool muted = masterMuted.load (std::memory_order_acquire)
                           && ! masterSolo.load (std::memory_order_acquire);
        if (muted)
            mix = 0.0f;

        for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
        {
            const float lowCoefficient = juce::jlimit (0.001f, 0.999f,
                                                       1.0f / (1.0f + static_cast<float> (sampleRate) / 150.0f));
            const float highCoefficient = juce::jlimit (0.001f, 0.999f,
                                                        1.0f / (1.0f + static_cast<float> (sampleRate) / 3500.0f));

            masterEqLowState[static_cast<std::size_t> (ch)]
                += lowCoefficient * (mix - masterEqLowState[static_cast<std::size_t> (ch)]);
            masterEqHighState[static_cast<std::size_t> (ch)]
                += highCoefficient * (mix - masterEqHighState[static_cast<std::size_t> (ch)]);

            const float lowBand = masterEqLowState[static_cast<std::size_t> (ch)];
            const float highBand = mix - masterEqHighState[static_cast<std::size_t> (ch)];
            const float midBand = mix - lowBand - highBand;
            const float brightness = masterBrightness.load (std::memory_order_acquire);
            const float masterProcessed = lowBand + midBand + highBand * (0.5f + brightness);   // 0.5 = neutro (ganho 1.0)
            const float gain = masterGain.load (std::memory_order_acquire);

            buffer.setSample (ch, i, juce::jlimit (-1.0f, 1.0f, masterProcessed * gain));
        }
    }

    juce::dsp::Reverb::Parameters reverbParams;
    const float space = masterSpace.load (std::memory_order_acquire);
    reverbParams.roomSize = 0.25f + space * 0.75f;
    reverbParams.damping = 0.55f;
    // juce::Reverb multiplica o seco por 2 e o molhado por 3 internamente: dryLevel 0.5 = ganho unitário.
    reverbParams.wetLevel = space * 0.25f;
    reverbParams.dryLevel = 0.5f * (1.0f - space * 0.25f);
    reverbParams.width = 1.0f;
    masterReverb.setParameters (reverbParams);

    if (numChannels >= 2)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        masterReverb.process (context);
    }

    // A cauda do reverb soma ao sinal já limitado: limita de novo para nunca passar de 0 dBFS.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            data[i] = juce::jlimit (-1.0f, 1.0f, data[i]);
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
