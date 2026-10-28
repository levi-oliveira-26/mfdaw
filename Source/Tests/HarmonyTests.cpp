#include <array>
#include <atomic>
#include <type_traits>

#include <juce_core/juce_core.h>

#include "Core/ChordGenerator.h"
#include "Core/ProjectState.h"
#include "Core/SpscQueue.h"
#include "Engine/EngineEvent.h"

namespace mf::tests
{
class HarmonySmartChordsTests final : public juce::UnitTest
{
public:
    HarmonySmartChordsTests() : juce::UnitTest ("Smart Chords Harmony Tests", "Harmony") {}

    void runTest() override
    {
        beginTest ("C major generates the expected MIDI triads");
        expectNotes (ChordGenerator::generateNotes (0, Scale::Major, 0), 48, 52, 55);
        expectNotes (ChordGenerator::generateNotes (0, Scale::Major, 1), 55, 59, 62);
        expectNotes (ChordGenerator::generateNotes (0, Scale::Major, 2), 57, 60, 64);
        expectNotes (ChordGenerator::generateNotes (0, Scale::Major, 3), 53, 57, 60);

        beginTest ("A minor generates the expected MIDI triads");
        expectNotes (ChordGenerator::generateNotes (9, Scale::Minor, 0), 57, 60, 64);
        expectNotes (ChordGenerator::generateNotes (9, Scale::Minor, 1), 65, 69, 72);
        expectNotes (ChordGenerator::generateNotes (9, Scale::Minor, 2), 60, 64, 67);
        expectNotes (ChordGenerator::generateNotes (9, Scale::Minor, 3), 67, 71, 74);

        beginTest ("G major generates the expected MIDI triads");
        expectNotes (ChordGenerator::generateNotes (7, Scale::Major, 0), 55, 59, 62);
        expectNotes (ChordGenerator::generateNotes (7, Scale::Major, 1), 62, 66, 69);
        expectNotes (ChordGenerator::generateNotes (7, Scale::Major, 2), 64, 67, 71);
        expectNotes (ChordGenerator::generateNotes (7, Scale::Major, 3), 60, 64, 67);

        beginTest ("Chord names react to key and scale");
        expectEquals (ChordGenerator::generate (0, Scale::Major)[0], juce::String ("C"));
        expectEquals (ChordGenerator::generate (9, Scale::Minor)[0], juce::String ("Am"));
        expectEquals (ChordGenerator::generate (7, Scale::Major)[0], juce::String ("G"));

        beginTest ("Chord MIDI events are allocation-free lock-free queue messages");
        expect (std::is_trivially_copyable_v<EngineEvent>);
        expect (std::is_trivially_destructible_v<EngineEvent>);
        expect (std::atomic<std::size_t>::is_always_lock_free);

        SpscQueue<EngineEvent, 16> queue;
        for (int note : ChordGenerator::generateNotes (0, Scale::Major, 0))
            expect (queue.push (EngineEvent::chordNote (note, true)));

        for (int expected : { 48, 52, 55 })
        {
            EngineEvent event;
            expect (queue.pop (event));
            expect (event.type == EngineEvent::Type::ChordNote);
            expectEquals (event.a, expected);
            expectEquals (event.b, 1);
        }

        beginTest ("Harmony chord persistence round-trips through ProjectState ValueTree");
        ProjectState state;
        const auto notes = ChordGenerator::generateNotes (0, Scale::Major, 0);
        state.setHarmonyChord (0, notes);

        const auto stored = state.getHarmonyNotes (0);
        expectEquals (stored, juce::String ("48,52,55"));
        expectEquals (state.getTree().getChildWithName (IDs::harmony).getNumChildren(), 1);

        const juce::String json = state.createJsonString();
        ProjectState restored;
        expect (restored.loadFromJsonString (json));
        expectEquals (restored.getHarmonyNotes (0), juce::String ("48,52,55"));
    }

private:
    void expectNotes (const std::array<int, 3>& actual, int first, int second, int third)
    {
        expectEquals (actual[0], first);
        expectEquals (actual[1], second);
        expectEquals (actual[2], third);
    }
};

static HarmonySmartChordsTests harmonySmartChordsTests;
} // namespace mf::tests
