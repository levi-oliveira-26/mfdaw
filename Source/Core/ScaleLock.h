#pragma once

#include "Core/ProjectState.h"

namespace mf
{
enum class Key : int
{
    C = 0, CSharp = 1, D = 2, DSharp = 3, E = 4, F = 5,
    FSharp = 6, G = 7, GSharp = 8, A = 9, ASharp = 10, B = 11
};

class ScaleLock final
{
public:
    [[nodiscard]] static bool isNoteInScale (int midiNote, Key key, Scale scale) noexcept;
    [[nodiscard]] static bool isNoteInScale (int midiNote, int key, Scale scale) noexcept;
    [[nodiscard]] static bool isNoteInScale (int midiNote, const ProjectState& state) noexcept;
};
} // namespace mf
