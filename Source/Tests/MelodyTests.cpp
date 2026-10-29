#include <type_traits>
#include <vector>

#include <juce_core/juce_core.h>

#include "Core/MelodyNote.h"
#include "Core/ProjectState.h"
#include "Core/SpscQueue.h"
#include "Engine/EngineEvent.h"
#include "Engine/MelodySequencer.h"

namespace mf::tests
{
/** Vertical Slice 04: notas de Melodia por faixa + sequenciador da audio thread. */
class MelodyTests final : public juce::UnitTest
{
public:
    MelodyTests() : juce::UnitTest ("Melody Track Tests", "Melody") {}

    void runTest() override
    {
        runDomainTests();
        runSequencerTests();
        runEventTests();
    }

private:
    static std::vector<int> pitchesAt (const MelodySequencer& seq, int slot, int step)
    {
        std::vector<int> result;
        seq.forEachNote (slot, step, [&result] (int pitch) { result.push_back (pitch); });
        return result;
    }

    void runDomainTests()
    {
        beginTest ("Note range math");
        {
            expect (melody::isValidNote (0, kMelodyLowestNote));
            expect (melody::isValidNote (kMelodySteps - 1, kMelodyHighestNote));
            expect (! melody::isValidNote (-1, 60));
            expect (! melody::isValidNote (kMelodySteps, 60));
            expect (! melody::isValidNote (0, kMelodyLowestNote - 1));
            expect (! melody::isValidNote (0, kMelodyHighestNote + 1));
        }

        beginTest ("Only Melody tracks accept notes");
        {
            ProjectState state;
            const int drum = state.addTrack (TrackType::Drum);
            const int harmony = state.addTrack (TrackType::Harmony);
            const int melodyTrack = state.addTrack (TrackType::Melody);

            expect (! state.setMelodyNote (drum, 0, 60, true));
            expect (! state.setMelodyNote (harmony, 0, 60, true));
            expect (! state.setMelodyNote (999, 0, 60, true));
            expect (state.setMelodyNote (melodyTrack, 0, 60, true));
            expect (state.getMelodyNotes (drum).empty());
        }

        beginTest ("setMelodyNote is idempotent and validates step/pitch");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);

            expect (state.setMelodyNote (track, 3, 64, true));
            expect (state.setMelodyNote (track, 3, 64, true));            // já ligada: ok, sem duplicar
            expectEquals (static_cast<int> (state.getMelodyNotes (track).size()), 1);
            expect (state.hasMelodyNote (track, 3, 64));

            expect (state.setMelodyNote (track, 3, 64, false));
            expect (state.setMelodyNote (track, 3, 64, false));           // já apagada: ok
            expect (! state.hasMelodyNote (track, 3, 64));

            expect (! state.setMelodyNote (track, 16, 60, true));
            expect (! state.setMelodyNote (track, -1, 60, true));
            expect (! state.setMelodyNote (track, 0, 10, true));
            expect (! state.setMelodyNote (track, 0, 120, true));
            expect (state.getMelodyNotes (track).empty());
        }

        beginTest ("Notes are returned sorted by (step, pitch)");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);
            state.setMelodyNote (track, 8, 60, true);
            state.setMelodyNote (track, 0, 67, true);
            state.setMelodyNote (track, 0, 60, true);

            const auto notes = state.getMelodyNotes (track);
            expectEquals (static_cast<int> (notes.size()), 3);
            expectEquals (notes[0].step, 0);  expectEquals (notes[0].pitch, 60);
            expectEquals (notes[1].step, 0);  expectEquals (notes[1].pitch, 67);
            expectEquals (notes[2].step, 8);  expectEquals (notes[2].pitch, 60);
        }

        beginTest ("The per-track note limit is enforced");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);
            int added = 0;

            for (int step = 0; step < kMelodySteps && added < kMaxMelodyNotesPerTrack; ++step)
                for (int pitch = kMelodyLowestNote; pitch <= kMelodyHighestNote && added < kMaxMelodyNotesPerTrack; ++pitch)
                    if (state.setMelodyNote (track, step, pitch, true))
                        ++added;

            expectEquals (added, kMaxMelodyNotesPerTrack);
            expect (! state.setMelodyNote (track, 15, 107, true));        // 129ª: recusada
            expect (state.setMelodyNote (track, 0, kMelodyLowestNote, false));   // apagar sempre funciona
            expect (state.setMelodyNote (track, 15, 107, true));          // abriu uma vaga
        }

        beginTest ("Two melody tracks keep independent patterns");
        {
            ProjectState state;
            const int a = state.addTrack (TrackType::Melody);
            const int b = state.addTrack (TrackType::Melody);
            state.setMelodyNote (a, 0, 60, true);

            expect (state.hasMelodyNote (a, 0, 60));
            expect (! state.hasMelodyNote (b, 0, 60));
        }

        beginTest ("Notes and clips coexist in the same track node");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);
            state.setMelodyNote (track, 0, 60, true);
            const int clipId = state.addClip (track, 2, 4);
            state.setMelodyNote (track, 1, 62, true);

            expect (clipId > 0);
            expectEquals (static_cast<int> (state.getClips (track).size()), 1);
            expectEquals (static_cast<int> (state.getMelodyNotes (track).size()), 2);
            expectEquals (state.getTrackBarMask (track), 0b111100);
            expect (state.setClipRange (track, clipId, 0, 2));
            expectEquals (state.getTrackBarMask (track), 0b11);
            expectEquals (state.getTrackBarMask (999), 0);
        }

        beginTest ("A gesture is ONE undo step when the UI opens a transaction");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);

            state.getUndoManager().beginNewTransaction();
            state.setMelodyNote (track, 0, 60, true);
            state.setMelodyNote (track, 1, 62, true);                     // mesmo gesto (arrasto)
            state.setMelodyNote (track, 2, 64, true);

            state.getUndoManager().undo();
            expect (state.getMelodyNotes (track).empty());
            expectEquals (state.getNumTracks(), 1);

            state.getUndoManager().redo();
            expectEquals (static_cast<int> (state.getMelodyNotes (track).size()), 3);
        }

        beginTest ("Removing the track discards its notes");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);
            state.setMelodyNote (track, 0, 60, true);
            state.removeTrack (track);

            expect (state.getMelodyNotes (track).empty());
            expect (! state.hasMelodyNote (track, 0, 60));
        }

        beginTest ("Save/load round-trips the notes");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);
            state.setMelodyNote (track, 0, 60, true);
            state.setMelodyNote (track, 4, 72, true);
            state.addClip (track, 0, 4);

            ProjectState restored;
            expect (restored.loadFromJsonString (state.createJsonString()));

            const auto notes = restored.getMelodyNotes (track);
            expectEquals (static_cast<int> (notes.size()), 2);
            expect (restored.hasMelodyNote (track, 4, 72));
            expectEquals (static_cast<int> (restored.getClips (track).size()), 1);
        }

        beginTest ("A corrupt file is sanitised (invalid notes, duplicates, notes on non-Melody tracks)");
        {
            ProjectState source;
            const int drum = source.addTrack (TrackType::Drum);
            const int track = source.addTrack (TrackType::Melody);
            source.setMelodyNote (track, 0, 60, true);    // vai ficar com altura inválida
            source.setMelodyNote (track, 1, 62, true);    // vai duplicar a de baixo
            source.setMelodyNote (track, 2, 64, true);    // íntegra
            source.setMelodyNote (track, 3, 65, true);    // passo inválido
            source.addClip (track, 0, 2);

            // Injeta uma nota diretamente numa faixa de Bateria (arquivo editado à mão).
            juce::ValueTree stray (IDs::note);
            stray.setProperty (IDs::noteStep, 0, nullptr);
            stray.setProperty (IDs::notePitch, 60, nullptr);
            source.getTree().getChildWithName (IDs::playlist).getChild (source.indexOfTrack (drum))
                  .appendChild (stray, nullptr);

            auto json = source.createJsonString();
            json = json.replace ("\"pitch\": 60", "\"pitch\": 5")
                       .replace ("\"step\": 3", "\"step\": 99")
                       .replace ("\"step\": 1,", "\"step\": 2,")
                       .replace ("\"pitch\": 62", "\"pitch\": 64");

            ProjectState loaded;
            expect (loaded.loadFromJsonString (json));

            const auto notes = loaded.getMelodyNotes (track);
            expectEquals (static_cast<int> (notes.size()), 1);            // só (2,64) sobrevive
            expectEquals (notes[0].step, 2);
            expectEquals (notes[0].pitch, 64);
            expect (loaded.getMelodyNotes (drum).empty());
            expectEquals (static_cast<int> (loaded.getClips (track).size()), 1);   // clipe intacto
        }
    }

    void runSequencerTests()
    {
        beginTest ("MelodySequencer stores notes across the whole MIDI range (both 64-bit words)");
        {
            MelodySequencer seq;
            for (const int pitch : { 0, 1, 63, 64, 65, 107, 127 })
                seq.setNote (2, 5, pitch, true);

            const auto found = pitchesAt (seq, 2, 5);
            expect (found == std::vector<int> ({ 0, 1, 63, 64, 65, 107, 127 }));   // ordem grave -> agudo
            expect (seq.hasNote (2, 5, 64));
            expect (! seq.hasNote (2, 5, 66));
            expect (! seq.hasNote (2, 4, 64));
            expect (! seq.hasNote (3, 5, 64));                                    // outro slot

            seq.setNote (2, 5, 64, false);
            expect (! seq.hasNote (2, 5, 64));
            expect (seq.hasNote (2, 5, 63) && seq.hasNote (2, 5, 65));            // vizinhas intactas
        }

        beginTest ("MelodySequencer ignores out-of-range slots, steps and pitches safely");
        {
            MelodySequencer seq;
            seq.setNote (-1, 0, 60, true);
            seq.setNote (MelodySequencer::kSlots, 0, 60, true);
            seq.setNote (0, -1, 60, true);
            seq.setNote (0, kMelodySteps, 60, true);
            seq.setNote (0, 0, -1, true);
            seq.setNote (0, 0, 128, true);
            seq.setBars (-5, 3);

            expect (! seq.hasNote (-1, 0, 60));
            expect (pitchesAt (seq, 0, 0).empty());
            expect (pitchesAt (seq, -1, 0).empty());
            expect (pitchesAt (seq, 0, 99).empty());
            expect (! seq.isAudible (-1, 0));
        }

        beginTest ("Bar gating: mask 0 = draft (always), otherwise only the covered bars");
        {
            MelodySequencer seq;
            expect (seq.isAudible (0, 0) && seq.isAudible (0, 15));               // rascunho

            seq.setBars (0, 0b1100);                                              // compassos 2 e 3
            expect (! seq.isAudible (0, 0));
            expect (! seq.isAudible (0, 1));
            expect (seq.isAudible (0, 2));
            expect (seq.isAudible (0, 3));
            expect (! seq.isAudible (0, 4));
            expect (seq.isAudible (1, 0));                                        // outro slot continua em rascunho

            seq.setBars (0, 0xFFFFFF);                                            // bits extras são descartados
            expectEquals (seq.getBars (0), (1 << kPlaylistBars) - 1);
        }

        beginTest ("clearNotes keeps the bar mask; clearSlot wipes both");
        {
            MelodySequencer seq;
            seq.setNote (4, 0, 60, true);
            seq.setBars (4, 0b1);

            seq.clearNotes (4);
            expect (pitchesAt (seq, 4, 0).empty());
            expectEquals (seq.getBars (4), 1);

            seq.setNote (4, 0, 60, true);
            seq.clearSlot (4);
            expect (pitchesAt (seq, 4, 0).empty());
            expectEquals (seq.getBars (4), 0);
        }

        beginTest ("Domain -> engine mirror: a song of clips yields exactly the audible notes");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Melody);
            state.setMelodyNote (track, 0, 60, true);
            state.setMelodyNote (track, 0, 64, true);
            state.setMelodyNote (track, 8, 67, true);
            state.addClip (track, 1, 2);                                          // compassos 1 e 2

            // Espelha o que MainProcessor faz via fila: notas + máscara.
            MelodySequencer seq;
            for (const auto& note : state.getMelodyNotes (track))
                seq.setNote (0, note.step, note.pitch, true);
            seq.setBars (0, state.getTrackBarMask (track));

            int triggered = 0;
            for (int bar = 0; bar < kPlaylistBars; ++bar)
                for (int step = 0; step < kMelodySteps; ++step)
                    if (seq.isAudible (0, bar))
                        seq.forEachNote (0, step, [&triggered] (int) { ++triggered; });

            expectEquals (triggered, 3 * 2);                                      // 3 notas x 2 compassos
        }
    }

    void runEventTests()
    {
        beginTest ("Melody events are trivially copyable and survive the SPSC queue");
        {
            expect (std::is_trivially_copyable_v<EngineEvent>);
            expect (std::is_standard_layout_v<MelodySequencer>);

            SpscQueue<EngineEvent, 16> queue;
            expect (queue.push (EngineEvent::setMelodyClear (3)));
            expect (queue.push (EngineEvent::setMelodyNote (3, 7, 100, true)));
            expect (queue.push (EngineEvent::setMelodyNote (3, 7, 100, false)));
            expect (queue.push (EngineEvent::setSlotBars (3, 0b101)));

            EngineEvent e;
            MelodySequencer seq;

            // Aplica como drainEvents().
            while (queue.pop (e))
            {
                switch (e.type)
                {
                    case EngineEvent::Type::SetMelodyNote:
                        seq.setNote (e.a, e.b, e.c & 0xFF, (e.c & 0x100) != 0);
                        expectEquals (e.c & 0xFF, 100);
                        break;
                    case EngineEvent::Type::SetMelodyClear: seq.clearNotes (e.a); break;
                    case EngineEvent::Type::SetSlotBars:    seq.setBars (e.a, e.b); break;
                    default: expect (false); break;
                }
            }

            expect (! seq.hasNote (3, 7, 100));                                   // ligada e depois desligada
            expectEquals (seq.getBars (3), 0b101);

            // Codificação on/off.
            expect ((EngineEvent::setMelodyNote (0, 0, 60, true).c & 0x100) != 0);
            expect ((EngineEvent::setMelodyNote (0, 0, 60, false).c & 0x100) == 0);
        }
    }
};

static MelodyTests melodyTests;

} // namespace mf::tests
