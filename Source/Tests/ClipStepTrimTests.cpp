#include <juce_core/juce_core.h>

#include "Core/ProjectState.h"

namespace mf::tests
{
/** Vertical Slice 12: trim e divisão de clipes de áudio por passo (1/16 de compasso). */
class ClipStepTrimTests final : public juce::UnitTest
{
public:
    ClipStepTrimTests() : juce::UnitTest ("Clip Step Trim Tests", "Clips") {}

    void runTest() override
    {
        beginTest ("layoutFromSteps and stepsToMs");
        {
            const auto a = clip::layoutFromSteps (5, 37);   // [5,37) -> compassos 0..3
            expectEquals (a.startBar, 0);
            expectEquals (a.lengthBars, 3);
            expectEquals (a.leadSteps, 5);
            expectEquals (a.tailSteps, 11);

            const auto b = clip::layoutFromSteps (32, 64);  // alinhado: sem folgas
            expectEquals (b.startBar, 2);
            expectEquals (b.lengthBars, 2);
            expectEquals (b.leadSteps, 0);
            expectEquals (b.tailSteps, 0);

            expectEquals (clip::stepsToMs (16, 120), 2000);
            expectEquals (clip::stepsToMs (1, 120), 125);
            expectEquals (clip::stepsToMs (1, 90), 167);
        }

        beginTest ("Audio: trim left by steps moves the offset by exact milliseconds; one undo step");
        {
            ProjectState state;
            state.setBpm (120);                                           // 1 passo = 125 ms
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 20.0) > 0);  // compassos [0,10)
            const int id = state.getClips (voice)[0].id;

            expect (state.trimClipStartStep (voice, id, 5));
            auto c = state.getClips (voice)[0];
            expectEquals (c.startBar, 0);
            expectEquals (c.lengthBars, 10);
            expectEquals (c.leadSteps, 5);
            expectEquals (c.startStep(), 5);
            expectEquals (c.endStep(), 160);
            expectEquals (c.offsetMs, 625);

            expect (state.trimClipStartStep (voice, id, 21));
            c = state.getClips (voice)[0];
            expectEquals (c.startBar, 1);
            expectEquals (c.leadSteps, 5);
            expectEquals (c.offsetMs, 2000 + 625);

            expect (! state.trimClipStartStep (voice, id, 21));   // sem mudança
            expect (state.trimClipStartStep (voice, id, 0));      // volta ao início do áudio
            expectEquals (state.getClips (voice)[0].offsetMs, 0);
            expect (! state.trimClipStartStep (voice, id, -1));

            state.getUndoManager().undo();
            expectEquals (state.getClips (voice)[0].startStep(), 21);
            expectEquals (state.getClips (voice)[0].offsetMs, 2625);
        }

        beginTest ("Audio: trim right by steps, and it can be re-opened again");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 8.0) > 0);   // 4 compassos
            const int id = state.getClips (voice)[0].id;

            expect (state.trimClipEndStep (voice, id, 40));                // termina no passo 40
            auto c = state.getClips (voice)[0];
            expectEquals (c.lengthBars, 3);
            expectEquals (c.tailSteps, 8);
            expectEquals (c.endStep(), 40);
            expectEquals (c.offsetMs, 0);

            expect (state.trimClipEndStep (voice, id, 64));
            c = state.getClips (voice)[0];
            expectEquals (c.lengthBars, 4);
            expectEquals (c.tailSteps, 0);

            expect (! state.trimClipEndStep (voice, id, 0));               // fim antes do início
            state.getUndoManager().undo();
            expectEquals (state.getClips (voice)[0].endStep(), 40);
        }

        beginTest ("Neighbours can share a bar; occupancy is measured in steps");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 8.0) > 0);   // a: [0,4)
            const int a = state.getClips (voice)[0].id;
            expect (state.trimClipEndStep (voice, a, 24));                  // a: passos [0,24)

            expectEquals (state.getMaxClipLengthAt (voice, 1), 0);          // compasso 1 parcialmente ocupado
            expectEquals (state.getMaxClipLengthAt (voice, 2), kPlaylistBars - 2);

            expect (state.addRecordedTake (voice, "b.wav", 32, 8.0) > 0);   // b: compassos [2,6)
            const int b = state.getClips (voice).back().id;
            expectEquals (state.getClips (voice).back().startStep(), 32);
            expect (state.trimClipStartStep (voice, b, 56));                // b: [56,96), offset 3000 ms
            expect (state.trimClipEndStep (voice, a, 40));                  // a: [0,40) termina no meio do compasso 2; b só começa em 56

            expect (state.canTrimClipStartStep (voice, b, 40));             // colado em a
            expect (! state.canTrimClipStartStep (voice, b, 39));           // invadiria a
            expect (state.canTrimClipEndStep (voice, a, 56));               // colado em b
            expect (! state.canTrimClipEndStep (voice, a, 57));             // invadiria b
            expectEquals (state.getMaxClipLengthAt (voice, 2), 0);          // compasso 2 tem a (até 40)
            expectEquals (state.getMaxClipLengthAt (voice, 6), kPlaylistBars - 6);
        }

        beginTest ("Cannot open before the audio start nor skip past its end");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 2.0) > 0);   // 1 compasso
            const int id = state.getClips (voice)[0].id;
            expect (state.setClipRange (voice, id, 2, 1));                  // [32,48)
            expect (! state.canTrimClipStartStep (voice, id, 31));          // antes do áudio (offset < 0)
            expect (state.canTrimClipStartStep (voice, id, 40));
            expect (! state.canTrimClipStartStep (voice, id, 48));          // zero passos
        }

        beginTest ("Pattern clips reject step operations");
        {
            ProjectState state;
            const int drum = state.addTrack (TrackType::Drum);
            const int id = state.addClip (drum, 2, 4);
            expect (! state.trimClipStartStep (drum, id, 40));
            expect (! state.trimClipEndStep (drum, id, 90));
            expect (state.splitClipAtStep (drum, id, 40) < 0);
            expect (state.splitClipAtStep (drum, id, 64) > 0);              // linha de compasso: ok
            expectEquals ((int) state.getClips (drum).size(), 2);
        }

        beginTest ("splitClipAtStep: second clip continues the audio; shared bar; undo");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 8.0) > 0);   // [0,4)
            const int id = state.getClips (voice)[0].id;

            const int newId = state.splitClipAtStep (voice, id, 22);       // meio do compasso 1
            expect (newId > 0);
            const auto clips = state.getClips (voice);
            expectEquals ((int) clips.size(), 2);

            expectEquals (clips[0].id, id);
            expectEquals (clips[0].startStep(), 0);
            expectEquals (clips[0].endStep(), 22);
            expectEquals (clips[0].lengthBars, 2);
            expectEquals (clips[0].tailSteps, 10);

            expectEquals (clips[1].id, newId);
            expectEquals (clips[1].startStep(), 22);
            expectEquals (clips[1].endStep(), 64);
            expectEquals (clips[1].startBar, 1);
            expectEquals (clips[1].leadSteps, 6);
            expectEquals (clips[1].offsetMs, 22 * 125);
            expectEquals (clips[1].takeId, clips[0].takeId);

            expect (state.splitClipAtStep (voice, id, 0) < 0);              // fora do clipe
            expect (state.splitClipAtStep (voice, id, 22) < 0);             // no fim do clipe

            state.getUndoManager().undo();
            expectEquals ((int) state.getClips (voice).size(), 1);
            expectEquals (state.getClips (voice)[0].endStep(), 64);
        }

        beginTest ("splitClip(bar) still works and keeps fine trims of the ends");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 8.0) > 0);
            const int id = state.getClips (voice)[0].id;
            expect (state.trimClipStartStep (voice, id, 3));
            expect (state.trimClipEndStep (voice, id, 60));

            const int newId = state.splitClip (voice, id, 2);
            expect (newId > 0);
            const auto clips = state.getClips (voice);
            expectEquals (clips[0].startStep(), 3);
            expectEquals (clips[0].endStep(), 32);
            expectEquals (clips[1].startStep(), 32);
            expectEquals (clips[1].endStep(), 60);
            expectEquals (clips[1].offsetMs, (32) * 125);
        }

        beginTest ("Moving a fine-trimmed clip keeps its lead/tail; stepwise neighbours still fit");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 4.0) > 0);   // [0,2)
            const int id = state.getClips (voice)[0].id;
            expect (state.trimClipEndStep (voice, id, 24));                 // [0,24) -> 2 compassos, tail 8
            expect (state.setClipRange (voice, id, 4, 2));
            const auto c = state.getClips (voice)[0];
            expectEquals (c.startBar, 4);
            expectEquals (c.tailSteps, 8);
            expectEquals (c.endStep(), 4 * 16 + 24);
            expect (state.canPlaceClip (voice, 4, 2, id));
        }

        beginTest ("Persistence: lead/tail round-trip; sanitizer clamps and clears invalid values");
        {
            ProjectState state;
            state.setBpm (120);
            const int voice = state.addTrack (TrackType::Record);
            expect (state.addRecordedTake (voice, "a.wav", 0, 8.0) > 0);
            const int id = state.getClips (voice)[0].id;
            expect (state.trimClipStartStep (voice, id, 5));
            expect (state.trimClipEndStep (voice, id, 50));

            const auto json = state.createJsonString();
            expect (json.contains ("\"leadSteps\": 5"));
            expect (json.contains ("\"tailSteps\": 14"));

            ProjectState loaded;
            expect (loaded.loadFromJsonString (json));
            const auto c = loaded.getClips (loaded.getTrackByIndex (0)->getId())[0];
            expectEquals (c.leadSteps, 5);
            expectEquals (c.tailSteps, 14);

            // Valores absurdos em áudio são limitados ao intervalo 0..15.
            ProjectState abused;
            expect (abused.loadFromJsonString (json.replace ("\"leadSteps\": 5", "\"leadSteps\": 99")
                                                   .replace ("\"tailSteps\": 14", "\"tailSteps\": -4")));
            const auto ac = abused.getClips (abused.getTrackByIndex (0)->getId())[0];
            expectEquals (ac.leadSteps, 15);
            expectEquals (ac.tailSteps, 0);

            // Lead/tail numa trilha de padrão é descartado.
            ProjectState patterns;
            const int drum = patterns.addTrack (TrackType::Drum);
            expect (patterns.addClip (drum, 0, 2) > 0);
            const auto djson = patterns.createJsonString();
            expect (djson.contains ("\"lengthBars\": 2"));
            ProjectState reloaded;
            expect (reloaded.loadFromJsonString (djson.replace ("\"lengthBars\": 2", "\"lengthBars\": 2, \"leadSteps\": 4")));
            const auto dc = reloaded.getClips (reloaded.getTrackByIndex (0)->getId())[0];
            expectEquals (dc.leadSteps, 0);
            expectEquals (dc.startStep(), 0);
        }
    }
};

static ClipStepTrimTests clipStepTrimTests;
} // namespace mf::tests
