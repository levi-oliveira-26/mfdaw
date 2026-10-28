#include <juce_core/juce_core.h>

#include "Core/ProjectState.h"
#include "Core/ScaleLock.h"

namespace mf::tests
{
class ScaleLockTests final : public juce::UnitTest
{
public:
    ScaleLockTests() : juce::UnitTest ("Scale Lock Tests", "Melody") {}

    void runTest() override
    {
        beginTest ("C major contains C and excludes C sharp");
        expect (ScaleLock::isNoteInScale (60, Key::C, Scale::Major));
        expect (! ScaleLock::isNoteInScale (61, Key::C, Scale::Major));
        expect (ScaleLock::isNoteInScale (72, Key::C, Scale::Major));

        beginTest ("A minor contains A, C and E and excludes sharps");
        expect (ScaleLock::isNoteInScale (69, Key::A, Scale::Minor));
        expect (ScaleLock::isNoteInScale (60, Key::A, Scale::Minor));
        expect (ScaleLock::isNoteInScale (64, Key::A, Scale::Minor));
        expect (! ScaleLock::isNoteInScale (61, Key::A, Scale::Minor));

        beginTest ("G major contains scale notes and rejects F natural");
        expect (ScaleLock::isNoteInScale (67, Key::G, Scale::Major));
        expect (ScaleLock::isNoteInScale (66, Key::G, Scale::Major));
        expect (! ScaleLock::isNoteInScale (65, Key::G, Scale::Major));

        beginTest ("Integer key API matches the Key enum API");
        expect (ScaleLock::isNoteInScale (60, 0, Scale::Major));
        expect (! ScaleLock::isNoteInScale (61, 0, Scale::Major));
        expect (! ScaleLock::isNoteInScale (60, -1, Scale::Major));
        expect (! ScaleLock::isNoteInScale (60, 12, Scale::Major));

        beginTest ("ProjectState supplies the reactive key and scale to Scale Lock");
        ProjectState state;
        state.setKey (0, Scale::Major);
        expect (ScaleLock::isNoteInScale (60, state));
        expect (! ScaleLock::isNoteInScale (61, state));

        state.setKey (9, Scale::Minor);
        expect (ScaleLock::isNoteInScale (69, state));
        expect (! ScaleLock::isNoteInScale (70, state));

        beginTest ("MIDI bounds are rejected safely");
        expect (! ScaleLock::isNoteInScale (-1, Key::C, Scale::Major));
        expect (! ScaleLock::isNoteInScale (128, Key::C, Scale::Major));
    }
};

static ScaleLockTests scaleLockTests;
} // namespace mf::tests
