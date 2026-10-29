#include <algorithm>

#include <juce_core/juce_core.h>

#include "Core/ClipModel.h"
#include "Core/ProjectState.h"
#include "Core/SpscQueue.h"
#include "Engine/EngineEvent.h"

namespace mf::tests
{
/** Vertical Slice 02: clipes de compassos inteiros nas lanes da Playlist. */
class ClipTests final : public juce::UnitTest
{
public:
    ClipTests() : juce::UnitTest ("Playlist Clip Tests", "Playlist") {}

    void runTest() override
    {
        beginTest ("Pure range math: bounds, overlap and start clamping");
        {
            expect (clip::isValidRange (0, 1));
            expect (clip::isValidRange (kPlaylistBars - 2, 2));
            expect (! clip::isValidRange (-1, 2));
            expect (! clip::isValidRange (0, 0));
            expect (! clip::isValidRange (kPlaylistBars - 1, 2));

            expect (clip::overlaps (0, 4, 3, 2));
            expect (! clip::overlaps (0, 4, 4, 2));      // vizinhos encostados não colidem
            expect (! clip::overlaps (4, 2, 0, 4));
            expect (clip::overlaps (2, 1, 0, 8));        // contido

            expectEquals (clip::clampStart (-5, 2), 0);
            expectEquals (clip::clampStart (99, 4), kPlaylistBars - 4);
            expectEquals (clip::clampStart (3, 4), 3);
        }

        beginTest ("A new track has no clips; clips need a valid track");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Drum);

            expect (state.getClips (track).empty());
            expect (state.getClips (999).empty());
            expectEquals (state.addClip (999, 0, 2), -1);
        }

        beginTest ("addClip creates ordered clips and rejects overlap / out-of-range");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);

            const int late  = state.addClip (track, 8, 2);
            const int early = state.addClip (track, 0, 4);
            expect (late > 0 && early > 0 && late != early);

            expectEquals (state.addClip (track, 2, 2), -1);                    // cai dentro de [0,4)
            expectEquals (state.addClip (track, 3, 6), -1);                    // invade [8,10)
            expectEquals (state.addClip (track, kPlaylistBars - 1, 2), -1);    // passa do fim
            expectEquals (state.addClip (track, -1, 2), -1);
            expectEquals (state.addClip (track, 4, 0), -1);
            expect (state.addClip (track, 4, 4) > 0);                          // encosta nos dois vizinhos

            const auto clips = state.getClips (track);
            expectEquals (static_cast<int> (clips.size()), 3);
            expect (std::is_sorted (clips.begin(), clips.end(),
                                    [] (const ClipData& a, const ClipData& b) { return a.startBar < b.startBar; }));
        }

        beginTest ("Clips of different tracks may overlap; ids are unique across the playlist");
        {
            ProjectState state;
            const int a = state.addTrack (TrackType::Drum);
            const int b = state.addTrack (TrackType::Harmony);

            const int ca = state.addClip (a, 0, 4);
            const int cb = state.addClip (b, 0, 4);
            expect (ca > 0 && cb > 0 && ca != cb);
        }

        beginTest ("setClipRange moves and resizes, refuses collisions, and is a no-op when unchanged");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Drum);
            const int one = state.addClip (track, 0, 2);
            const int two = state.addClip (track, 4, 2);

            expect (state.setClipRange (track, one, 1, 3));                    // [1,4) encosta em [4,6)
            expect (! state.setClipRange (track, one, 2, 3));                  // [2,5) invade o 2º
            expect (! state.setClipRange (track, one, 0, 0));
            expect (! state.setClipRange (track, 12345, 0, 1));
            expect (state.setClipRange (track, two, 4, 2));                    // idêntico: ok

            const auto clips = state.getClips (track);
            expectEquals (clips[0].startBar, 1);
            expectEquals (clips[0].lengthBars, 3);
            expectEquals (clips[1].startBar, 4);

            // Um clipe nunca colide consigo mesmo.
            expect (state.canPlaceClip (track, 1, 3, one));
            expect (! state.canPlaceClip (track, 1, 3));
        }

        beginTest ("getMaxClipLengthAt reports the room available for a new clip");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Sample);
            state.addClip (track, 2, 2);                                       // ocupa [2,4)

            expectEquals (state.getMaxClipLengthAt (track, 0), 2);             // até o clipe
            expectEquals (state.getMaxClipLengthAt (track, 2), 0);             // ocupado
            expectEquals (state.getMaxClipLengthAt (track, 3), 0);
            expectEquals (state.getMaxClipLengthAt (track, 4), kPlaylistBars - 4);
            expectEquals (state.getMaxClipLengthAt (track, kPlaylistBars), 0);
            expectEquals (state.getMaxClipLengthAt (track, -1), 0);
            expectEquals (state.getMaxClipLengthAt (999, 0), 0);
        }

        beginTest ("removeClip works; ids are not reused afterwards");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Drum);
            const int first = state.addClip (track, 0, 2);

            expect (state.removeClip (track, first));
            expect (! state.removeClip (track, first));
            expect (state.getClips (track).empty());

            const int second = state.addClip (track, 0, 2);
            expect (second != first);
        }

        beginTest ("Removing a track discards its clips");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Drum);
            state.addClip (track, 0, 2);
            state.removeTrack (track);

            expect (state.getClips (track).empty());
            expectEquals (state.getNumTracks(), 0);
        }

        beginTest ("Each user operation is ONE undo step");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);
            const int id = state.addClip (track, 0, 2);

            expect (state.setClipRange (track, id, 5, 4));                     // move + redimensiona
            state.getUndoManager().undo();
            auto clips = state.getClips (track);
            expectEquals (static_cast<int> (clips.size()), 1);
            expectEquals (clips[0].startBar, 0);                               // voltou os DOIS campos
            expectEquals (clips[0].lengthBars, 2);

            state.getUndoManager().undo();                                     // desfaz a criação
            expect (state.getClips (track).empty());
            expectEquals (state.getNumTracks(), 1);                            // a faixa continua

            state.getUndoManager().redo();
            expectEquals (static_cast<int> (state.getClips (track).size()), 1);
        }

        beginTest ("Save/load round-trips clips and the id counter");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Drum);
            const int a = state.addClip (track, 0, 4);
            const int b = state.addClip (track, 8, 2);
            state.removeClip (track, a);

            ProjectState restored;
            expect (restored.loadFromJsonString (state.createJsonString()));

            const auto clips = restored.getClips (track);
            expectEquals (static_cast<int> (clips.size()), 1);
            expectEquals (clips[0].id, b);
            expectEquals (clips[0].startBar, 8);
            expectEquals (clips[0].lengthBars, 2);

            const int next = restored.addClip (track, 0, 1);
            expect (next != a && next != b);
        }

        beginTest ("A corrupt file is sanitised: out-of-range, overlapping and duplicate-id clips");
        {
            ProjectState source;
            const int track = source.addTrack (TrackType::Drum);
            source.addClip (track, 0, 2);   // id 1
            source.addClip (track, 4, 2);   // id 2
            source.addClip (track, 8, 2);   // id 3
            source.addClip (track, 12, 2);  // id 4

            auto json = source.createJsonString();
            json = json.replace ("\"startBar\": 4", "\"startBar\": 1")        // colide com [0,2)
                       .replace ("\"startBar\": 12", "\"startBar\": 15")       // 15+2 > 16
                       .replace ("\"clipId\": 3", "\"clipId\": 1");           // id duplicado

            ProjectState loaded;
            expect (loaded.loadFromJsonString (json));

            const auto clips = loaded.getClips (track);
            expectEquals (static_cast<int> (clips.size()), 2);                 // sobram [0,2) e [8,10)
            expect (clips[0].id > 0 && clips[1].id > 0 && clips[0].id != clips[1].id);

            for (const auto& c : clips)
                expect (clip::isValidRange (c.startBar, c.lengthBars));
            expect (! clip::overlaps (clips[0].startBar, clips[0].lengthBars,
                                      clips[1].startBar, clips[1].lengthBars));
        }

        beginTest ("Arrangement gate math: bar masks, loop and draft mode");
        {
            expectEquals (clip::barMaskFor (0, 1), 0b1);
            expectEquals (clip::barMaskFor (2, 3), 0b11100);
            expectEquals (clip::barMaskFor (kPlaylistBars - 1, 1), 1 << (kPlaylistBars - 1));
            expectEquals (clip::barMaskFor (kPlaylistBars - 1, 2), 0);          // inválido
            expectEquals (clip::barMaskFor (-1, 2), 0);

            expectEquals (clip::nextBar (0), 1);
            expectEquals (clip::nextBar (kPlaylistBars - 1), 0);                // loop

            for (int bar = 0; bar < kPlaylistBars; ++bar)
                expect (clip::isBarAudible (0, bar));                           // rascunho: sempre audível

            expect (clip::isBarAudible (0b11, 0));
            expect (clip::isBarAudible (0b11, 1));
            expect (! clip::isBarAudible (0b11, 2));
            expect (! clip::isBarAudible (0b11, -1));
            expect (! clip::isBarAudible (0b11, kPlaylistBars));
        }

        beginTest ("getDrumBarMask: only Drum tracks count, union across tracks, follows edits and undo");
        {
            ProjectState state;
            expectEquals (state.getDrumBarMask(), 0);

            const int drumA = state.addTrack (TrackType::Drum);
            const int drumB = state.addTrack (TrackType::Drum);
            const int melody = state.addTrack (TrackType::Melody);

            state.addClip (melody, 0, 8);
            expectEquals (state.getDrumBarMask(), 0);                           // Melodia não entra

            const int a = state.addClip (drumA, 0, 2);
            state.addClip (drumB, 1, 3);                                        // [1,4) sobrepõe [0,2) em outra faixa
            expectEquals (state.getDrumBarMask(), 0b1111);

            expect (state.setClipRange (drumA, a, 8, 2));
            expectEquals (state.getDrumBarMask(), 0b1110 | (0b11 << 8));

            state.getUndoManager().undo();
            expectEquals (state.getDrumBarMask(), 0b1111);

            state.removeTrack (drumB);
            expectEquals (state.getDrumBarMask(), 0b11);
        }

        beginTest ("SetDrumBars travels through the lock-free queue as a fixed-size event");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Drum);
            state.addClip (track, 4, 4);

            SpscQueue<EngineEvent, 8> queue;
            expect (queue.push (EngineEvent::setDrumBars (state.getDrumBarMask())));

            EngineEvent event;
            expect (queue.pop (event));
            expect (event.type == EngineEvent::Type::SetDrumBars);
            expectEquals (event.a, 0b11110000);
        }
    }
};

static ClipTests clipTests;

} // namespace mf::tests
