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
    void mouseDown (const juce::MouseEvent& event) override;

    /** Faixa de Voz exibida/alvo da gravação (-1 = nenhuma). */
    void setTrackId (int newTrackId);
    /** Clipe selecionado na Playlist que receberá o take escolhido. */
    void setSelectedClip (int selectedTrackId, int clipId);

private:
    void timerCallback() override;

    static constexpr int kTakeRowHeight = 30;
    [[nodiscard]] int takeRowAt (juce::Point<int> position) const;

    int trackId { -1 };
    std::vector<TakeData> takes;       // mais recente primeiro
    int activeTakeId { -1 };
    int selectedClipTrackId { -1 };
    int selectedClipId { -1 };
    int selectedClipTakeId { -1 };
    juce::Rectangle<int> takesArea;

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
