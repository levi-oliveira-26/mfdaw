#include <juce_core/juce_core.h>

#include "Core/ProjectState.h"
#include "Core/TakeModel.h"

namespace mf::tests
{
/** Vertical Slice 06: takes de voz na faixa de Voz + clipe automático. */
class TakeTests final : public juce::UnitTest
{
public:
    TakeTests() : juce::UnitTest ("Voice Take Tests", "Takes") {}

    void runTest() override
    {
        runMathTests();
        runDomainTests();
        runPersistenceTests();
        runClipTakeLinkTests();
    }

private:
    void runMathTests()
    {
        beginTest ("take::barsCovered rounds up from the exact start step (120 BPM: 1 bar = 2 s)");
        {
            expectEquals (take::barsCovered (0, 1.0, 120), 1);
            expectEquals (take::barsCovered (0, 2.0, 120), 1);
            expectEquals (take::barsCovered (0, 2.5, 120), 2);
            expectEquals (take::barsCovered (8, 1.5, 120), 2);    // começa no meio do compasso 0
            expectEquals (take::barsCovered (16, 1.0, 120), 1);
            expectEquals (take::barsCovered (0, 3600.0, 120), kPlaylistBars);   // limitado
            expectEquals (take::startBarOf (0), 0);
            expectEquals (take::startBarOf (15), 0);
            expectEquals (take::startBarOf (16), 1);
        }

        beginTest ("Take validity bounds");
        {
            expect (take::isValidStartStep (0) && take::isValidStartStep (kPlaylistSteps - 1));
            expect (! take::isValidStartStep (-1) && ! take::isValidStartStep (kPlaylistSteps));
            expect (take::isValidDuration (0.5));
            expect (! take::isValidDuration (0.0) && ! take::isValidDuration (-1.0));
            expect (! take::isValidDuration (kMaxTakeSeconds + 1.0));
        }
    }

    void runDomainTests()
    {
        beginTest ("A take is accepted only on a Record track with valid data");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            const int drum = state.addTrack (TrackType::Drum);

            expectEquals (state.addRecordedTake (drum, "a.wav", 0, 1.0), -1);
            expectEquals (state.addRecordedTake (999, "a.wav", 0, 1.0), -1);
            expectEquals (state.addRecordedTake (voice, "", 0, 1.0), -1);
            expectEquals (state.addRecordedTake (voice, "a.wav", -1, 1.0), -1);
            expectEquals (state.addRecordedTake (voice, "a.wav", 0, 0.0), -1);
            expect (state.getTakes (voice).empty());
            expect (state.getClips (voice).empty());
            expect (state.getTakes (drum).empty());
        }

        beginTest ("Recording creates take + active + clip as ONE undo step");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);

            const int id = state.addRecordedTake (voice, "a.wav", 16, 3.0);   // compasso 1, 3 s -> 2 compassos
            expect (id > 0);
            expectEquals (static_cast<int> (state.getTakes (voice).size()), 1);
            expectEquals (state.getActiveTakeId (voice), id);

            const auto clips = state.getClips (voice);
            expectEquals (static_cast<int> (clips.size()), 1);
            expectEquals (clips[0].startBar, 1);
            expectEquals (clips[0].lengthBars, 2);

            state.getUndoManager().undo();
            expect (state.getTakes (voice).empty());
            expect (state.getClips (voice).empty());
            expectEquals (state.getActiveTakeId (voice), -1);

            state.getUndoManager().redo();
            expectEquals (static_cast<int> (state.getTakes (voice).size()), 1);
            expectEquals (static_cast<int> (state.getClips (voice).size()), 1);
        }

        beginTest ("Auto clip is clamped to free space; occupied start keeps the take without a clip");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addClip (voice, 3, 2) > 0);   // compassos 3..4

            const int a = state.addRecordedTake (voice, "a.wav", 16, 20.0);   // começa no 1, quer 10 compassos
            expect (a > 0);
            auto clips = state.getClips (voice);
            expectEquals (static_cast<int> (clips.size()), 2);
            bool foundClamped = false;
            for (const auto& c : clips)
                if (c.startBar == 1) { foundClamped = (c.lengthBars == 2); }
            expect (foundClamped);

            const int b = state.addRecordedTake (voice, "b.wav", 3 * 16, 1.0);   // início ocupado
            expect (b > 0);
            expectEquals (static_cast<int> (state.getClips (voice).size()), 2);
            expectEquals (state.getActiveTakeId (voice), b);
            expectEquals (static_cast<int> (state.getTakes (voice).size()), 2);
        }

        beginTest ("Active take selection and removal");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            const int t1 = state.addRecordedTake (voice, "1.wav", 0, 1.0);
            const int t2 = state.addRecordedTake (voice, "2.wav", 32, 1.0);
            const int t3 = state.addRecordedTake (voice, "3.wav", 64, 1.0);
            expect (t1 < t2 && t2 < t3);
            expectEquals (state.getActiveTakeId (voice), t3);

            expect (state.setActiveTake (voice, t1));
            expectEquals (state.getActiveTakeId (voice), t1);
            expect (! state.setActiveTake (voice, 12345));
            expectEquals (state.getActiveTakeId (voice), t1);

            expect (state.removeTake (voice, t1));                       // ativo removido -> mais recente
            expectEquals (state.getActiveTakeId (voice), t3);
            expect (! state.removeTake (voice, t1));
            expect (state.removeTake (voice, t2));
            expectEquals (state.getActiveTakeId (voice), t3);            // ativo intacto
            expect (state.removeTake (voice, t3));
            expectEquals (state.getActiveTakeId (voice), -1);

            const int t4 = state.addRecordedTake (voice, "4.wav", 0, 1.0);
            expect (t4 > t3);                                             // ids nunca reaparecem
        }

        beginTest ("Per-track take cap");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            for (int i = 0; i < kMaxTakesPerTrack; ++i)
                expect (state.addRecordedTake (voice, "x" + juce::String (i) + ".wav", 0, 0.5) > 0);
            expectEquals (state.addRecordedTake (voice, "over.wav", 0, 0.5), -1);
        }
    }

    void runClipTakeLinkTests()
    {
        beginTest ("Recorded clips retain their take identity and new voice clips use active take");
        {
            ProjectState state;
            const int voice = state.addTrack (TrackType::Record);
            const int t1 = state.addRecordedTake (voice, "one.wav", 0, 1.0);
            const int t2 = state.addRecordedTake (voice, "two.wav", 32, 1.0);
            auto clips = state.getClips (voice);
            expectEquals (static_cast<int> (clips.size()), 2);
            expectEquals (clips[0].takeId, t1);
            expectEquals (clips[1].takeId, t2);
            expect (state.setActiveTake (voice, t1));
            const int added = state.addClip (voice, 4, 1);
            expect (added > 0);
            clips = state.getClips (voice);
            for (const auto& c : clips)
                if (c.id == added) expectEquals (c.takeId, t1);
            const int emptyVoice = state.addTrack (TrackType::Record);
            expectEquals (state.addClip (emptyVoice, 0, 1), -1);
        }

        beginTest ("Removing a take removes linked clips in one undo transaction");
        {
            ProjectState state;
            const int voice = state.addTrack (TrackType::Record);
            const int t1 = state.addRecordedTake (voice, "one.wav", 0, 1.0);
            const int t2 = state.addRecordedTake (voice, "two.wav", 32, 1.0);
            expect (state.removeTake (voice, t1));
            expectEquals (static_cast<int> (state.getTakes (voice).size()), 1);
            expectEquals (static_cast<int> (state.getClips (voice).size()), 1);
            state.getUndoManager().undo();
            expectEquals (static_cast<int> (state.getTakes (voice).size()), 2);
            expectEquals (static_cast<int> (state.getClips (voice).size()), 2);
            expectEquals (state.getClips (voice)[0].takeId, t1);
            state.getUndoManager().redo();
            expectEquals (static_cast<int> (state.getTakes (voice).size()), 1);
            expectEquals (state.getClips (voice)[0].takeId, t2);
        }

        beginTest ("Slice 06 clips migrate to active take and orphan references are cleared");
        {
            ProjectState source;
            const int voice = source.addTrack (TrackType::Record);
            const int takeId = source.addRecordedTake (voice, "one.wav", 0, 1.0);
            auto playlist = source.getTree().getChildWithName (IDs::playlist);
            auto voiceNode = playlist.getChild (source.indexOfTrack (voice));
            voiceNode.getChildWithName (IDs::clip).removeProperty (IDs::takeId, nullptr);

            ProjectState loaded;
            expect (loaded.loadFromJsonString (source.createJsonString()));
            expectEquals (loaded.getClips (voice)[0].takeId, takeId);

            auto loadedPlaylist = loaded.getTree().getChildWithName (IDs::playlist);
            auto loadedVoice = loadedPlaylist.getChild (loaded.indexOfTrack (voice));
            loadedVoice.getChildWithName (IDs::clip).setProperty (IDs::takeId, 99999, nullptr);
            ProjectState sanitized;
            expect (sanitized.loadFromJsonString (loaded.createJsonString()));
            expectEquals (sanitized.getClips (voice)[0].takeId, -1);
        }
    }

    void runPersistenceTests()
    {
        beginTest ("Takes survive save/load; removing the track discards them");
        {
            ProjectState source;
            source.setBpm (120);
            const int voice = source.addTrack (TrackType::Record);
            const int t1 = source.addRecordedTake (voice, "one.wav", 0, 1.0);
            const int t2 = source.addRecordedTake (voice, "two.wav", 48, 4.5);
            source.setActiveTake (voice, t1);

            ProjectState loaded;
            expect (loaded.loadFromJsonString (source.createJsonString()));
            const auto takes = loaded.getTakes (voice);
            expectEquals (static_cast<int> (takes.size()), 2);
            expectEquals (takes[1].id, t2);
            expectEquals (takes[1].startStep, 48);
            expectEquals (takes[1].startBar(), 3);
            expectWithinAbsoluteError (takes[1].durationSeconds, 4.5, 1e-9);
            expectEquals (loaded.getActiveTakeId (voice), t1);
            expectEquals (static_cast<int> (loaded.getClips (voice).size()), 2);

            expect (source.removeTrack (voice));
            expect (source.getTakes (voice).empty());
        }

        beginTest ("Sanitizer drops corrupt takes and keeps ids unique");
        {
            ProjectState source;
            source.setBpm (120);
            const int voice = source.addTrack (TrackType::Record);
            const int drum = source.addTrack (TrackType::Drum);
            const int good = source.addRecordedTake (voice, "ok.wav", 0, 1.0);
            source.addRecordedTake (voice, "dup.wav", 16, 1.0);

            auto playlist = source.getTree().getChildWithName (IDs::playlist);
            auto voiceNode = playlist.getChild (source.indexOfTrack (voice));
            voiceNode.getChild (voiceNode.getNumChildren() - 1).setProperty (IDs::takeId, good, nullptr);   // id duplicado

            juce::ValueTree bad (IDs::take);
            bad.setProperty (IDs::takeId, 77, nullptr);
            bad.setProperty (IDs::filePath, "", nullptr);                 // sem arquivo
            bad.setProperty (IDs::takeStartStep, 0, nullptr);
            bad.setProperty (IDs::durationSeconds, 1.0, nullptr);
            voiceNode.appendChild (bad, nullptr);

            juce::ValueTree onDrum (IDs::take);                           // take em faixa que não é de Voz
            onDrum.setProperty (IDs::takeId, 78, nullptr);
            onDrum.setProperty (IDs::filePath, "d.wav", nullptr);
            onDrum.setProperty (IDs::takeStartStep, 0, nullptr);
            onDrum.setProperty (IDs::durationSeconds, 1.0, nullptr);
            playlist.getChild (source.indexOfTrack (drum)).appendChild (onDrum, nullptr);

            voiceNode.setProperty (IDs::activeTake, 999, nullptr);        // ativo inexistente

            ProjectState loaded;
            expect (loaded.loadFromJsonString (source.createJsonString()));

            const auto takes = loaded.getTakes (voice);
            expectEquals (static_cast<int> (takes.size()), 2);
            expect (takes[0].id != takes[1].id && takes[0].id > 0 && takes[1].id > 0);
            expect (loaded.getTakes (drum).empty());
            expectEquals (loaded.getActiveTakeId (voice), takes.back().id);   // ativo inválido -> mais recente
            expect (! loaded.getTree().getChildWithName (IDs::playlist)
                          .getChild (loaded.indexOfTrack (voice)).hasProperty (IDs::activeTake));
            expect (loaded.getClips (voice).size() == 2);                 // takes não viram clipes

            const int next = loaded.addRecordedTake (voice, "n.wav", 0, 1.0);
            expect (next > takes[0].id && next > takes[1].id);
        }
    }
};

static TakeTests takeTests;

} // namespace mf::tests
