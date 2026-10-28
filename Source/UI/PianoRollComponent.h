#pragma once

#include <array>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ProjectState.h"
#include "Core/ScaleLock.h"
#include "Engine/MainProcessor.h"

namespace mf
{
class PianoRollComponent final : public juce::Component,
                                 private juce::ValueTree::Listener
{
public:
    explicit PianoRollComponent (ProjectState& projectState, MainProcessor& processor);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    static constexpr int numRows = 12;
    static constexpr int numSteps = 16;

    [[nodiscard]] int noteAt (const juce::Point<int>& position, int* row = nullptr, int* step = nullptr) const;
    int toggleNote (const juce::Point<int>& position);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    ProjectState& state;
    MainProcessor& processor;
    std::array<std::array<bool, numSteps>, numRows> notes {};
    int previewNote { -1 };
    int previewRow { -1 };
    int previewStep { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
} // namespace mf
