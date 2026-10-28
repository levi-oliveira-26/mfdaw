#include "Core/ScaleLock.h"

#include <array>

namespace mf
{
namespace
{
constexpr std::array<int, 7> majorIntervals = { 0, 2, 4, 5, 7, 9, 11 };
constexpr std::array<int, 7> naturalMinorIntervals = { 0, 2, 3, 5, 7, 8, 10 };
}

bool ScaleLock::isNoteInScale (int midiNote, Key key, Scale scale) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return false;

    const auto& intervals = scale == Scale::Major ? majorIntervals : naturalMinorIntervals;
    const int pitchClass = midiNote % 12;
    const int keyPitchClass = static_cast<int> (key);

    for (const int interval : intervals)
    {
        if (pitchClass == (keyPitchClass + interval) % 12)
            return true;
    }

    return false;
}

bool ScaleLock::isNoteInScale (int midiNote, int key, Scale scale) noexcept
{
    if (key < 0 || key > 11)
        return false;

    return isNoteInScale (midiNote, static_cast<Key> (key), scale);
}

bool ScaleLock::isNoteInScale (int midiNote, const ProjectState& state) noexcept
{
    return isNoteInScale (midiNote, state.getKeyRoot(), state.getScale());
}
} // namespace mf
