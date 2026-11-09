#include <juce_core/juce_core.h>

#include "Core/ProjectState.h"

namespace mf::tests
{
/** Vertical Slice 10: trim esquerdo e divisão de clipes. */
class ClipTrimSplitTests final : public juce::UnitTest
{
public:
    ClipTrimSplitTests() : juce::UnitTest ("Clip Trim and Split Tests", "Clips") {}

    void runTest() override
    {
        beginTest ("barDurationMs follows the BPM (4/4)");
        {
            expectEquals (clip::barDurationMs (120), 2000);
            expectEquals (clip::barDurationMs (60), 4000);
            expectEquals (clip::barDurationMs (90), 2667);
            expectEquals (clip::barDurationMs (0), 0);
        }

        beginTest ("Trim on a pattern clip moves the start and keeps the end; no offset is created");
        {
            ProjectState state;
            const int drum = state.addTrack (TrackType::Drum);
            const int id = state.addClip (drum, 2, 4);   // [2,6)

            expect (state.trimClipStart (drum, id, 4));
            auto clips = state.getClips (drum);
            expectEquals (clips[0].startBar, 4);
            expectEquals (clips[0].lengthBars, 2);
            expectEquals (clips[0].offsetMs, 0);

            expect (state.trimClipStart (drum, id, 0));  // abrir é livre em padrões
            clips = state.getClips (drum);
            expectEquals (clips[0].startBar, 0);
            expectEquals (clips[0].lengthBars, 6);

            state.getUndoManager().undo();
            clips = state.getClips (drum);
            expectEquals (clips[0].startBar, 4);
            expectEquals (clips[0].lengthBars, 2);
        }

        beginTest ("Trim validation: same start, past the end, negative, unknown ids");
        {
            ProjectState state;
            const int drum = state.addTrack (TrackType::Drum);
            const int id = state.addClip (drum, 2, 4);
            expect (! state.trimClipStart (drum, id, 2));
            expect (! state.trimClipStart (drum, id, 6));
            expect (! state.trimClipStart (drum, id, 7));
            expect (! state.trimClipStart (drum, id, -1));
            expect (! state.trimClipStart (drum, 999, 3));
            expect (! state.trimClipStart (999, id, 3));
            expect (! state.canTrimClipStart (drum, id, 6));
            expect (state.canTrimClipStart (drum, id, 5));
            expectEquals (state.getClips (drum)[0].startBar, 2);
        }

        beginTest ("Opening to the left cannot invade the neighbouring clip");
        {
            ProjectState state;
            const int drum = state.addTrack (TrackType::Drum);
            state.addClip (drum, 0, 4);                      // [0,4)
            const int b = state.addClip (drum, 6, 4);        // [6,10)
            expect (state.trimClipStart (drum, b, 4));       // [4,10): colado, ok
            expect (! state.trimClipStart (drum, b, 3));     // invadiria [0,4)
        }

        beginTest ("Audio clips: trim moves the offset by whole bars and can be re-opened up to the audio start");
        {
            ProjectState state;
            state.setBpm (120);                              // 1 compasso = 2000 ms
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 20.0) > 0);   // 10 compassos [0,10)
            const int id = state.getClips (voice)[0].id;

            expect (state.trimClipStart (voice, id, 3));
            auto c = state.getClips (voice)[0];
            expectEquals (c.startBar, 3);
            expectEquals (c.lengthBars, 7);
            expectEquals (c.offsetMs, 6000);

            expect (state.trimClipStart (voice, id, 1));     // reabre 2 compassos
            c = state.getClips (voice)[0];
            expectEquals (c.startBar, 1);
            expectEquals (c.offsetMs, 2000);

            expect (state.trimClipStart (voice, id, 0));
            c = state.getClips (voice)[0];
            expectEquals (c.offsetMs, 0);
            expect (! state.trimClipStart (voice, id, -1));  // antes do início do áudio: impossível

            state.getUndoManager().undo();                   // UM passo: volta a start=1, offset=2000
            c = state.getClips (voice)[0];
            expectEquals (c.startBar, 1);
            expectEquals (c.offsetMs, 2000);
        }

        beginTest ("Trim cannot skip past the end of the audio");
        {
            ProjectState state;
            state.setBpm (60);                               // 1 compasso = 4000 ms
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 3.5) > 0);    // 1 compasso
            const int id = state.getClips (voice)[0].id;
            expect (state.setClipRange (voice, id, 0, 3));
            expect (! state.trimClipStart (voice, id, 1));   // offset 4000 >= 3500: ficaria mudo
            expect (! state.canTrimClipStart (voice, id, 1));
        }

        beginTest ("Split divides a clip in two, the second continuing the audio, in ONE undo step");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            const int take = state.addRecordedTake (voice, "a.wav", 0, 20.0);
            const int id = state.getClips (voice)[0].id;

            const int second = state.splitClip (voice, id, 4);
            expect (second > 0 && second != id);
            const auto clips = state.getClips (voice);
            expectEquals (static_cast<int> (clips.size()), 2);
            expectEquals (clips[0].startBar, 0);
            expectEquals (clips[0].lengthBars, 4);
            expectEquals (clips[0].offsetMs, 0);
            expectEquals (clips[1].id, second);
            expectEquals (clips[1].startBar, 4);
            expectEquals (clips[1].lengthBars, 6);
            expectEquals (clips[1].offsetMs, 8000);
            expectEquals (clips[1].takeId, take);

            state.getUndoManager().undo();
            expectEquals (static_cast<int> (state.getClips (voice).size()), 1);
            expectEquals (state.getClips (voice)[0].lengthBars, 10);
        }

        beginTest ("Splitting a trimmed clip accumulates the offset; invalid split points are refused");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            state.addRecordedTake (voice, "a.wav", 0, 20.0);
            const int id = state.getClips (voice)[0].id;
            expect (state.trimClipStart (voice, id, 3));     // [3,10) offset 6000

            expectEquals (state.splitClip (voice, id, 3), -1);    // no início
            expectEquals (state.splitClip (voice, id, 10), -1);   // no fim
            expectEquals (state.splitClip (voice, id, 2), -1);    // fora
            expectEquals (state.splitClip (voice, 999, 5), -1);
            expectEquals (state.splitClip (999, id, 5), -1);
            expectEquals (static_cast<int> (state.getClips (voice).size()), 1);

            expect (state.splitClip (voice, id, 5) > 0);
            expectEquals (state.getClips (voice)[1].offsetMs, 6000 + 2 * 2000);
        }

        beginTest ("Split on a pattern clip creates no take link or offset");
        {
            ProjectState state;
            const int mel = state.addTrack (TrackType::Melody);
            const int id = state.addClip (mel, 0, 4);
            const int second = state.splitClip (mel, id, 2);
            expect (second > 0);
            for (const auto& c : state.getClips (mel))
            {
                expectEquals (c.takeId, -1);
                expectEquals (c.offsetMs, 0);
            }
        }

        beginTest ("Offsets persist; changing a clip's audio resets it; corrupt offsets are sanitised");
        {
            ProjectState source;
            source.setBpm (120);
            const int voice = source.addTrack (TrackType::Record);
            const int drum = source.addTrack (TrackType::Drum);
            source.addRecordedTake (voice, "a.wav", 0, 20.0);
            const int second = source.addRecordedTake (voice, "b.wav", 0, 20.0);   // início ocupado: sem clipe novo
            const int id = source.getClips (voice)[0].id;
            const int dClip = source.addClip (drum, 0, 2);
            source.trimClipStart (voice, id, 2);

            ProjectState loaded;
            expect (loaded.loadFromJsonString (source.createJsonString()));
            expectEquals (loaded.getClips (voice)[0].offsetMs, 4000);

            // corrupção: offset negativo no áudio e offset em clipe de bateria
            auto playlist = source.getTree().getChildWithName (IDs::playlist);
            auto voiceNode = playlist.getChild (source.indexOfTrack (voice));
            for (int c = 0; c < voiceNode.getNumChildren(); ++c)
                if (voiceNode.getChild (c).hasType (IDs::clip))
                    voiceNode.getChild (c).setProperty (IDs::clipOffsetMs, -300, nullptr);
            auto drumNode = playlist.getChild (source.indexOfTrack (drum));
            for (int c = 0; c < drumNode.getNumChildren(); ++c)
                if (drumNode.getChild (c).hasType (IDs::clip))
                    drumNode.getChild (c).setProperty (IDs::clipOffsetMs, 5000, nullptr);

            ProjectState bad;
            expect (bad.loadFromJsonString (source.createJsonString()));
            expectEquals (bad.getClips (voice)[0].offsetMs, 0);
            expectEquals (bad.getClips (drum)[0].offsetMs, 0);
            expect (dClip > 0);

            // trocar o áudio do clipe zera o trim (era do áudio anterior)
            expect (source.setClipTake (voice, id, second));
            expectEquals (source.getClips (voice)[0].offsetMs, 0);
        }
    }
};

static ClipTrimSplitTests clipTrimSplitTests;

} // namespace mf::tests
