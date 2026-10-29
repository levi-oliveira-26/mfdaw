#include <cmath>
#include <type_traits>

#include <juce_core/juce_core.h>

#include "Core/FaderScale.h"
#include "Core/ProjectState.h"
#include "Core/SpscQueue.h"
#include "Core/TrackModel.h"
#include "Core/TrackType.h"
#include "Engine/EngineEvent.h"
#include "UI/PianoRollComponent.h"

namespace mf::tests
{
/** Pivot / Vertical Slice 01: domínio da Playlist, faixas polimórficas e fader pré-mix. */
class PlaylistTests final : public juce::UnitTest
{
public:
    PlaylistTests() : juce::UnitTest ("Playlist Arrangement Tests", "Playlist") {}

    void runTest() override
    {
        beginTest ("A new project starts EMPTY (no tracks, no pre-loaded drum pattern)");
        {
            ProjectState state;
            expectEquals (state.getNumTracks(), 0);
            expect (state.getTrackByIndex (0) == nullptr);
            expect (state.getTrackById (1) == nullptr);
            expectEquals (state.getTrackMask (0), 0);
        }

        beginTest ("addTrack creates the right polymorphic subclass for each of the 5 types");
        {
            ProjectState state;
            const TrackType types[] = { TrackType::Drum, TrackType::Harmony, TrackType::Melody,
                                        TrackType::Record, TrackType::Sample };
            const TrackAction actions[] = { TrackAction::OpenStepSequencer, TrackAction::OpenSmartChords,
                                            TrackAction::OpenPianoRoll, TrackAction::ToggleRecording,
                                            TrackAction::OpenSampleEditor };

            for (int i = 0; i < 5; ++i)
            {
                const int id = state.addTrack (types[i]);
                expect (id > 0);

                const auto track = state.getTrackById (id);
                expect (track != nullptr);
                if (track == nullptr)
                    continue;

                expect (track->getType() == types[i]);
                expect (track->getPrimaryAction() == actions[i]);
                expectEquals (track->getId(), id);
                expectWithinAbsoluteError (track->getGain(), fader::kDefaultPosition, 0.0001f);
            }

            expectEquals (state.getNumTracks(), 5);
            expect (dynamic_cast<DrumTrack*> (state.getTrackByIndex (0).get()) != nullptr);
            expect (dynamic_cast<HarmonyTrack*> (state.getTrackByIndex (1).get()) != nullptr);
            expect (dynamic_cast<MelodyTrack*> (state.getTrackByIndex (2).get()) != nullptr);
            expect (dynamic_cast<RecordTrack*> (state.getTrackByIndex (3).get()) != nullptr);
            expect (dynamic_cast<SampleTrack*> (state.getTrackByIndex (4).get()) != nullptr);
        }

        beginTest ("Default names are numbered per type");
        {
            ProjectState state;
            const int a = state.addTrack (TrackType::Drum);
            const int b = state.addTrack (TrackType::Drum);
            const int c = state.addTrack (TrackType::Melody);

            expectEquals (state.getTrackById (a)->getName(), juce::String ("Bateria 1"));
            expectEquals (state.getTrackById (b)->getName(), juce::String ("Bateria 2"));
            expectEquals (state.getTrackById (c)->getName(), juce::String ("Melodia 1"));
        }

        beginTest ("trackIds are never reused after a removal");
        {
            ProjectState state;
            const int a = state.addTrack (TrackType::Drum);
            const int b = state.addTrack (TrackType::Harmony);
            expect (state.removeTrack (b));
            const int c = state.addTrack (TrackType::Harmony);

            expect (c != a && c != b);
            expect (! state.removeTrack (b));
            expect (! state.removeTrack (9999));
            expectEquals (state.getNumTracks(), 2);
        }

        beginTest ("The playlist is capped at kMaxPlaylistTracks and rejects invalid types");
        {
            ProjectState state;
            for (int i = 0; i < kMaxPlaylistTracks; ++i)
                expect (state.addTrack (TrackType::Sample) > 0);

            expectEquals (state.addTrack (TrackType::Drum), -1);
            expectEquals (state.getNumTracks(), kMaxPlaylistTracks);

            ProjectState other;
            expectEquals (other.addTrack (static_cast<TrackType> (42)), -1);
            expectEquals (other.getNumTracks(), 0);
        }

        beginTest ("Name editing trims and ignores empty input");
        {
            ProjectState state;
            const int id = state.addTrack (TrackType::Record);
            auto track = state.getTrackById (id);

            track->setName ("  Minha voz  ");
            expectEquals (state.getTrackById (id)->getName(), juce::String ("Minha voz"));

            track->setName ("   ");
            expectEquals (state.getTrackById (id)->getName(), juce::String ("Minha voz"));
        }

        beginTest ("Pre-mix fader: default is unity (0 dB), position is clamped, scale is monotonic");
        {
            expectWithinAbsoluteError (fader::toDecibels (fader::kUnityPosition), 0.0f, 0.0001f);
            expectWithinAbsoluteError (fader::toGain (fader::kUnityPosition), 1.0f, 0.0001f);
            expectEquals (fader::toGain (0.0f), 0.0f);
            expect (std::isinf (fader::toDecibels (0.0f)));
            expectWithinAbsoluteError (fader::toDecibels (1.0f), fader::kMaxDb, 0.0001f);
            expectEquals (fader::clampPosition (-3.0f), 0.0f);
            expectEquals (fader::clampPosition (7.0f), 1.0f);
            expectEquals (fader::clampPosition (std::nanf ("")), 0.0f);

            float previous = -1.0f;
            for (int i = 0; i <= 100; ++i)
            {
                const float gain = fader::toGain (static_cast<float> (i) / 100.0f);
                expect (gain >= previous);
                previous = gain;
            }

            ProjectState state;
            const int id = state.addTrack (TrackType::Drum);
            state.getTrackById (id)->setGain (5.0f);
            expectEquals (state.getTrackById (id)->getGain(), 1.0f);
            state.getTrackById (id)->setGain (-1.0f);
            expectEquals (state.getTrackById (id)->getGain(), 0.0f);
        }

        beginTest ("Gain transport (per-mille) round-trips and saturates at +6 dB");
        {
            expectEquals (fader::gainToPermille (1.0f), 1000);
            expectEquals (fader::gainToPermille (0.0f), 0);
            expectEquals (fader::gainToPermille (-1.0f), 0);
            expectEquals (fader::gainToPermille (100.0f), fader::kMaxGainPermille);
            expectWithinAbsoluteError (fader::permilleToGain (1000), 1.0f, 0.0001f);
            expectEquals (fader::permilleToGain (-5), 0.0f);
            expectEquals (fader::permilleToGain (1 << 20), fader::permilleToGain (fader::kMaxGainPermille));
        }

        beginTest ("Playlist edits are undoable");
        {
            ProjectState state;
            const int id = state.addTrack (TrackType::Melody);
            expectEquals (state.getNumTracks(), 1);

            state.getUndoManager().undo();
            expectEquals (state.getNumTracks(), 0);

            state.getUndoManager().redo();
            expectEquals (state.getNumTracks(), 1);
            expect (state.getTrackById (id) != nullptr);
        }

        beginTest ("Save/load round-trips the playlist (order, type, name, gain, id counter)");
        {
            ProjectState state;
            const int drum = state.addTrack (TrackType::Drum);
            const int voice = state.addTrack (TrackType::Record);
            state.getTrackById (voice)->setName ("Refrão");
            state.getTrackById (voice)->setGain (0.4f);
            state.removeTrack (drum);
            const int sample = state.addTrack (TrackType::Sample);

            ProjectState restored;
            expect (restored.loadFromJsonString (state.createJsonString()));
            expectEquals (restored.getNumTracks(), 2);

            const auto first = restored.getTrackByIndex (0);
            expect (first != nullptr && first->getType() == TrackType::Record);
            expectEquals (first->getName(), state.getTrackById (voice)->getName());
            expectWithinAbsoluteError (first->getGain(), 0.4f, 0.0001f);
            expect (restored.getTrackById (sample) != nullptr);

            // O contador sobrevive: nada que já existiu volta a ter o mesmo id.
            const int next = restored.addTrack (TrackType::Harmony);
            expect (next != drum && next != voice && next != sample);
        }

        beginTest ("A hand-edited/corrupt file is sanitised (bad types dropped, duplicate ids fixed)");
        {
            ProjectState source;
            source.addTrack (TrackType::Drum);
            source.addTrack (TrackType::Harmony);
            source.addTrack (TrackType::Melody);

            auto json = source.createJsonString();
            // Duplica um id e injeta um tipo inexistente.
            json = json.replace ("\"trackId\": 2", "\"trackId\": 1")
                       .replace ("\"trackType\": 2", "\"trackType\": 99");

            ProjectState loaded;
            expect (loaded.loadFromJsonString (json));
            expectEquals (loaded.getNumTracks(), 2);

            const auto a = loaded.getTrackByIndex (0);
            const auto b = loaded.getTrackByIndex (1);
            expect (a != nullptr && b != nullptr);
            expect (a->getId() != b->getId());
            expect (a->getId() > 0 && b->getId() > 0);
        }

        beginTest ("Playlist -> Engine messages are fixed-size, allocation-free and fit the SPSC queue");
        {
            expect (std::is_trivially_copyable_v<EngineEvent>);
            expect (std::is_trivially_destructible_v<EngineEvent>);

            SpscQueue<EngineEvent, 16> queue;
            expect (queue.push (EngineEvent::setTrackType (3, static_cast<int> (TrackType::Drum))));
            expect (queue.push (EngineEvent::setTrackGain (3, fader::gainToPermille (fader::toGain (0.5f)))));

            EngineEvent event;
            expect (queue.pop (event));
            expect (event.type == EngineEvent::Type::SetTrackType);
            expectEquals (event.a, 3);
            expectEquals (event.b, static_cast<int> (TrackType::Drum));

            expect (queue.pop (event));
            expect (event.type == EngineEvent::Type::SetTrackGain);
            expectEquals (event.a, 3);
            expect (event.b > 0 && event.b < 1000);
        }

        beginTest ("Piano Roll note names use scientific pitch (MIDI 60 = C4), never raw numbers");
        {
            expectEquals (PianoRollComponent::noteName (60), juce::String ("C4"));
            expectEquals (PianoRollComponent::noteName (61), juce::String ("C#4"));
            expectEquals (PianoRollComponent::noteName (24), juce::String ("C1"));
            expectEquals (PianoRollComponent::noteName (107), juce::String ("B7"));
            expectEquals (PianoRollComponent::noteName (-1), juce::String());
            expectEquals (PianoRollComponent::noteName (128), juce::String());
        }
    }
};

static PlaylistTests playlistTests;

} // namespace mf::tests
