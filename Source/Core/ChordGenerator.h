#pragma once

#include <array>
#include <juce_core/juce_core.h>

#include "Core/ProjectState.h"

namespace mf
{
class ChordGenerator final
{
public:
    [[nodiscard]] static std::array<juce::String, 4> generate (int root, Scale scale);
    [[nodiscard]] static std::array<int, 3> generateNotes (int root, Scale scale, int progressionIndex);
};
} // namespace mf
