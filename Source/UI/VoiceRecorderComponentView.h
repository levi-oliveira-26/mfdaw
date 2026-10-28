#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Engine/MainProcessor.h"

namespace mf
{
class VoiceRecorderComponentView final : public juce::Component,
                                         private juce::Timer
{
public:
    explicit VoiceRecorderComponentView (MainProcessor& processor);
    ~VoiceRecorderComponentView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    MainProcessor& engine;
    ProjectState& state;
    AudioRecorder& recorder;

    juce::TextButton recordButton;
    juce::Label countInLabel;
    juce::Rectangle<int> vuArea;
    juce::Rectangle<int> waveformArea;
    std::array<float, 96> waveformEnvelope {};
    std::size_t waveformPosition { 0 };
    float displayedLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceRecorderComponentView)
};
} // namespace mf
