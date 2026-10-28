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

std::array<int, 3> ChordGenerator::generateNotes (int root, Scale scale, int progressionIndex)
{
    static constexpr std::array<std::array<int, 3>, 4> majorIntervals {{
        {{ 0, 4, 7 }}, {{ 7, 11, 14 }}, {{ 9, 12, 16 }}, {{ 5, 9, 12 }}
    }};
    static constexpr std::array<std::array<int, 3>, 4> minorIntervals {{
        {{ 0, 3, 7 }}, {{ 8, 12, 15 }}, {{ 3, 7, 10 }}, {{ 10, 14, 17 }}
    }};

    if (progressionIndex < 0 || progressionIndex >= 4)
        return {};

    const auto& intervals = scale == Scale::Minor ? minorIntervals : majorIntervals;
    std::array<int, 3> notes {};
    const int safeRoot = juce::jlimit (0, 11, root);

    for (size_t i = 0; i < notes.size(); ++i)
        notes[i] = juce::jlimit (0, 127, 48 + safeRoot + intervals[static_cast<size_t> (progressionIndex)][i]);

    return notes;
}
} // namespace mf
