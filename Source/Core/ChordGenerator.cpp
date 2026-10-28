#include "Core/ChordGenerator.h"

namespace mf
{
namespace
{
constexpr const char* kNoteNames[12] =
{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

int transposeNote (int root, int interval) noexcept
{
    return (root + interval + 12) % 12;
}

juce::String chordName (int root, int interval, bool minor = false)
{
    juce::String name { kNoteNames[transposeNote (root, interval)] };
    return minor ? name + "m" : name;
}
} // namespace

std::array<juce::String, 4> ChordGenerator::generate (int root, Scale scale)
{
    if (scale == Scale::Minor)
        return { chordName (root, 0, true), chordName (root, 8),
                 chordName (root, 3), chordName (root, 10) };

    return { chordName (root, 0), chordName (root, 7),
             chordName (root, 9, true), chordName (root, 5) };
}
} // namespace mf
