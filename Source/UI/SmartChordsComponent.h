#pragma once

#include <array>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ProjectState.h"

namespace mf
{
class SmartChordsComponent final : public juce::Component,
                                   private juce::ValueTree::Listener
{
public:
    explicit SmartChordsComponent (ProjectState& projectState);
    ~SmartChordsComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void refreshChords();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    ProjectState& state;
    std::array<juce::Label, 4> chordLabels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartChordsComponent)
};
} // namespace mf
