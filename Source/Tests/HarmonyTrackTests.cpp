#include <vector>

#include <juce_core/juce_core.h>

#include "Core/ChordGenerator.h"
#include "Core/HarmonyChord.h"
#include "Core/ProjectState.h"
#include "Core/SpscQueue.h"
#include "Engine/EngineEvent.h"
#include "Engine/HarmonySequencer.h"

namespace mf::tests
{
/** Vertical Slice 05: progressão de Harmonia por faixa + sequenciador da audio thread. */
class HarmonyTrackTests final : public juce::UnitTest
{
public:
    HarmonyTrackTests() : juce::UnitTest ("Harmony Track Tests", "Harmony") {}

    void runTest() override
    {
        runDomainTests();
        runSequencerTests();
    }

private:
    static std::vector<int> notesInBar (const HarmonySequencer& seq, int slot, int bar)
    {
        std::vector<int> result;
        seq.forEachNoteInBar (slot, bar, [&result] (int note) { result.push_back (note); });
        return result;
    }

    void runDomainTests()
    {
        beginTest ("Card math: bar -> card, validity and note packing");
        {
            expectEquals (harmony::cardForBar (0), 0);
            expectEquals (harmony::cardForBar (3), 3);
            expectEquals (harmony::cardForBar (4), 0);
            expectEquals (harmony::cardForBar (15), 3);
            expectEquals (harmony::cardForBar (-1), 0);
            expect (harmony::isValidCard (0) && harmony::isValidCard (3));
            expect (! harmony::isValidCard (-1) && ! harmony::isValidCard (4));

            for (const int n : { 0, 1, 60, 127 })
            {
                const int packed = harmony::packNotes (n, 127 - n, 64);
                expectEquals (harmony::unpackNote (packed, 0), n);
                expectEquals (harmony::unpackNote (packed, 1), 127 - n);
                expectEquals (harmony::unpackNote (packed, 2), 64);
            }
        }

        beginTest ("A new Harmony track has all 4 cards armed; other tracks report 0");
        {
            ProjectState state;
            const int harmonyTrack = state.addTrack (TrackType::Harmony);
            const int drum = state.addTrack (TrackType::Drum);

            expectEquals (state.getHarmonyArmedMask (harmonyTrack), kHarmonyAllCardsMask);
            expectEquals (state.getHarmonyArmedMask (drum), 0);
            expectEquals (state.getHarmonyArmedMask (999), 0);
        }

        beginTest ("setHarmonyCardArmed validates track type and card, and is idempotent");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Harmony);
            const int melodyTrack = state.addTrack (TrackType::Melody);

            expect (! state.setHarmonyCardArmed (melodyTrack, 0, false));
            expect (! state.setHarmonyCardArmed (999, 0, false));
            expect (! state.setHarmonyCardArmed (track, -1, false));
            expect (! state.setHarmonyCardArmed (track, 4, false));

            expect (state.setHarmonyCardArmed (track, 1, false));
            expectEquals (state.getHarmonyArmedMask (track), 0b1101);
            expect (state.setHarmonyCardArmed (track, 1, false));           // já desligado: ok
            expectEquals (state.getHarmonyArmedMask (track), 0b1101);
            expect (state.setHarmonyCardArmed (track, 1, true));
            expectEquals (state.getHarmonyArmedMask (track), kHarmonyAllCardsMask);
        }

        beginTest ("Each effective toggle is ONE undo step; no-ops leave no history");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Harmony);

            state.setHarmonyCardArmed (track, 0, false);
            state.setHarmonyCardArmed (track, 0, false);                    // no-op
            state.setHarmonyCardArmed (track, 2, false);
            expectEquals (state.getHarmonyArmedMask (track), 0b1010);

            state.getUndoManager().undo();
            expectEquals (state.getHarmonyArmedMask (track), 0b1110);
            state.getUndoManager().undo();
            expectEquals (state.getHarmonyArmedMask (track), kHarmonyAllCardsMask);
            state.getUndoManager().redo();
            expectEquals (state.getHarmonyArmedMask (track), 0b1110);
        }

        beginTest ("Harmony tracks are independent and the setting survives save/load");
        {
            ProjectState state;
            const int a = state.addTrack (TrackType::Harmony);
            const int b = state.addTrack (TrackType::Harmony);
            state.setHarmonyCardArmed (a, 0, false);
            state.setHarmonyCardArmed (a, 3, false);

            expectEquals (state.getHarmonyArmedMask (b), kHarmonyAllCardsMask);

            ProjectState restored;
            expect (restored.loadFromJsonString (state.createJsonString()));
            expectEquals (restored.getHarmonyArmedMask (a), 0b0110);
            expectEquals (restored.getHarmonyArmedMask (b), kHarmonyAllCardsMask);
        }

        beginTest ("A corrupt file is sanitised (mask out of range, mask on a non-Harmony track)");
        {
            ProjectState source;
            const int harmonyTrack = source.addTrack (TrackType::Harmony);
            const int drum = source.addTrack (TrackType::Drum);

            auto playlist = source.getTree().getChildWithName (IDs::playlist);
            playlist.getChild (source.indexOfTrack (harmonyTrack)).setProperty (IDs::chordMask, 0xFF, nullptr);
            playlist.getChild (source.indexOfTrack (drum)).setProperty (IDs::chordMask, 3, nullptr);

            ProjectState loaded;
            expect (loaded.loadFromJsonString (source.createJsonString()));

            expectEquals (loaded.getHarmonyArmedMask (harmonyTrack), kHarmonyAllCardsMask);   // 0xFF -> 0xF
            expectEquals (loaded.getHarmonyArmedMask (drum), 0);
            expect (! loaded.getTree().getChildWithName (IDs::playlist)
                          .getChild (loaded.indexOfTrack (drum)).hasProperty (IDs::chordMask));
        }

        beginTest ("Removing a Harmony track discards its settings");
        {
            ProjectState state;
            const int track = state.addTrack (TrackType::Harmony);
            state.setHarmonyCardArmed (track, 0, false);
            state.removeTrack (track);

            expectEquals (state.getHarmonyArmedMask (track), 0);
            expect (! state.setHarmonyCardArmed (track, 0, true));
        }
    }

    void runSequencerTests()
    {
        beginTest ("HarmonySequencer stores chords per slot/card and clamps notes");
        {
            HarmonySequencer seq;
            seq.setChord (3, 2, true, 48, 52, 55);
            seq.setChord (3, 1, true, -5, 200, 127);

            const auto chord = seq.getChord (3, 2);
            expect (chord.armed);
            expectEquals (static_cast<int> (chord.notes[0]), 48);
            expectEquals (static_cast<int> (chord.notes[2]), 55);

            const auto clamped = seq.getChord (3, 1);
            expectEquals (static_cast<int> (clamped.notes[0]), 0);
            expectEquals (static_cast<int> (clamped.notes[1]), 127);

            expect (seq.isArmed (3, 2));
            expect (! seq.isArmed (3, 0));
            expect (! seq.isArmed (4, 2));                                  // outro slot
        }

        beginTest ("HarmonySequencer ignores invalid slots and cards safely");
        {
            HarmonySequencer seq;
            seq.setChord (-1, 0, true, 60, 64, 67);
            seq.setChord (HarmonySequencer::kSlots, 0, true, 60, 64, 67);
            seq.setChord (0, -1, true, 60, 64, 67);
            seq.setChord (0, kHarmonyCards, true, 60, 64, 67);
            seq.setBars (-3, 1);

            expect (! seq.isArmed (-1, 0));
            expect (! seq.isArmed (0, 0));
            expect (notesInBar (seq, -1, 0).empty());
            expect (! seq.isAudible (-1, 0));
            expect (! seq.getChord (99, 0).armed);
        }

        beginTest ("Only the armed card of the bar sounds, repeating every 4 bars");
        {
            HarmonySequencer seq;
            seq.setChord (0, 0, true, 48, 52, 55);
            seq.setChord (0, 1, false, 55, 59, 62);                         // desligado
            seq.setChord (0, 2, true, 57, 60, 64);
            seq.setChord (0, 3, true, 53, 57, 60);

            expect (notesInBar (seq, 0, 0) == std::vector<int> ({ 48, 52, 55 }));
            expect (notesInBar (seq, 0, 1).empty());                        // cartão 1 desligado
            expect (notesInBar (seq, 0, 2) == std::vector<int> ({ 57, 60, 64 }));
            expect (notesInBar (seq, 0, 4) == notesInBar (seq, 0, 0));      // repete
            expect (notesInBar (seq, 0, 5).empty());
            expect (notesInBar (seq, 0, 15) == std::vector<int> ({ 53, 57, 60 }));
        }

        beginTest ("Bar gating: mask 0 = draft (always), otherwise only covered bars");
        {
            HarmonySequencer seq;
            seq.setChord (0, 0, true, 48, 52, 55);

            expect (! notesInBar (seq, 0, 0).empty());                      // rascunho
            expect (! notesInBar (seq, 0, 8).empty());

            seq.setBars (0, 0b1);                                           // só o compasso 0
            expect (! notesInBar (seq, 0, 0).empty());
            expect (notesInBar (seq, 0, 4).empty());                        // mesmo cartão, mas fora do clipe
            expect (notesInBar (seq, 0, 8).empty());

            seq.setBars (0, 0xFFFFFF);
            expectEquals (seq.isAudible (0, 15) ? 1 : 0, 1);
        }

        beginTest ("Domain -> engine mirror: clips + armed cards yield exactly the audible chords");
        {
            ProjectState state;
            state.setKey (0, Scale::Major);
            const int track = state.addTrack (TrackType::Harmony);
            state.addClip (track, 0, 4);                                    // compassos 0..3
            state.setHarmonyCardArmed (track, 1, false);                    // cartão do compasso 1 mudo
            state.setHarmonyCardArmed (track, 3, false);                    // cartão do compasso 3 mudo

            // Espelha o que MainProcessor::pushHarmonySlot + pushArrangementState enviam.
            HarmonySequencer seq;
            const int armed = state.getHarmonyArmedMask (track);
            for (int card = 0; card < kHarmonyCards; ++card)
            {
                const auto n = ChordGenerator::generateNotes (state.getKeyRoot(), state.getScale(), card);
                seq.setChord (0, card, ((armed >> card) & 1) != 0, n[0], n[1], n[2]);
            }
            seq.setBars (0, state.getTrackBarMask (track));

            int soundingBars = 0, notes = 0;
            for (int bar = 0; bar < kPlaylistBars; ++bar)
            {
                const auto inBar = notesInBar (seq, 0, bar);
                if (! inBar.empty())
                    ++soundingBars;
                notes += static_cast<int> (inBar.size());
            }

            expectEquals (soundingBars, 2);                                 // compassos 0 e 2
            expectEquals (notes, 6);
            expect (notesInBar (seq, 0, 0) == std::vector<int> ({ 48, 52, 55 }));   // C maior
        }

        beginTest ("Changing the key transposes the progression (chords come from the project key)");
        {
            ProjectState state;
            state.setKey (0, Scale::Major);
            const auto inC = ChordGenerator::generateNotes (state.getKeyRoot(), state.getScale(), 0);
            state.setKey (2, Scale::Major);
            const auto inD = ChordGenerator::generateNotes (state.getKeyRoot(), state.getScale(), 0);

            expectEquals (inD[0] - inC[0], 2);
            expectEquals (inD[1] - inC[1], 2);
        }

        beginTest ("Harmony events are trivially copyable and decode like drainEvents()");
        {
            expect (std::is_trivially_copyable_v<EngineEvent>);

            SpscQueue<EngineEvent, 16> queue;
            expect (queue.push (EngineEvent::setHarmonyChord (5, 3, true, 0, 60, 127)));
            expect (queue.push (EngineEvent::setHarmonyChord (5, 2, false, 48, 52, 55)));
            expect (queue.push (EngineEvent::setSlotBars (5, 0b11)));

            HarmonySequencer seq;
            EngineEvent e;
            while (queue.pop (e))
            {
                if (e.type == EngineEvent::Type::SetHarmonyChord)
                    seq.setChord (e.a, e.b & 0xFF, (e.b & 0x100) != 0,
                                  harmony::unpackNote (e.c, 0), harmony::unpackNote (e.c, 1), harmony::unpackNote (e.c, 2));
                else if (e.type == EngineEvent::Type::SetSlotBars)
                    seq.setBars (e.a, e.b);
                else
                    expect (false);
            }

            expect (seq.isArmed (5, 3));
            expect (! seq.isArmed (5, 2));
            expectEquals (static_cast<int> (seq.getChord (5, 3).notes[0]), 0);
            expectEquals (static_cast<int> (seq.getChord (5, 3).notes[2]), 127);
            expect (! seq.isAudible (5, 2));                                // fora da máscara 0b11
        }
    }
};

static HarmonyTrackTests harmonyTrackTests;

} // namespace mf::tests
