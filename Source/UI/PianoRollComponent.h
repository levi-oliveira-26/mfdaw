#pragma once

#include <array>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ProjectState.h"
#include "Core/ScaleLock.h"

namespace mf
{
class PianoRollComponent final : public juce::Component,
                                 private juce::ValueTree::Listener
{
public:
    explicit PianoRollComponent (ProjectState& projectState);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;

private:
    static constexpr int numRows = 12;
    static constexpr int numSteps = 16;

    void toggleNote (const juce::Point<int>& position);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    ProjectState& state;
    std::array<std::array<bool, numSteps>, numRows> notes {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
} // namespace mf
