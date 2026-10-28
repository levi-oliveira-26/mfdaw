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
};
} // namespace mf
