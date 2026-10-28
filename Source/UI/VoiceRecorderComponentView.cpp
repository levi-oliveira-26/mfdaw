#include "UI/VoiceRecorderComponentView.h"

#include "UI/Theme.h"

namespace mf
{
VoiceRecorderComponentView::VoiceRecorderComponentView (MainProcessor& processor)
    : engine (processor),
      state (processor.getProjectState()),
      recorder (processor.getAudioRecorder())
{
    recordButton.setButtonText (juce::String ("Gravar voz"));
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (theme::kActionRecord));
    recordButton.onClick = [this]
    {
        state.setRecording (! state.isRecording());
        recordButton.repaint();
    };
    addAndMakeVisible (recordButton);

    startTimerHz (30);
}

VoiceRecorderComponentView::~VoiceRecorderComponentView()
{
    stopTimer();
}

void VoiceRecorderComponentView::paint (juce::Graphics& g)
{
    using namespace theme;

    g.fillAll (col (kSurfaceBase));
    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (20.0f, true));
    g.drawText ("Passo 4 - Voz", getLocalBounds().removeFromTop (44), juce::Justification::centredLeft, false);

    g.setColour (col (kBorderSubtle));
    g.drawRoundedRectangle (vuArea.toFloat(), 6.0f, 1.0f);
    g.drawRoundedRectangle (waveformArea.toFloat(), 6.0f, 1.0f);

    const float level = juce::jlimit (0.0f, 1.0f, displayedLevel);
    const int segments = 24;
    const int segmentWidth = juce::jmax (2, (vuArea.getWidth() - (segments - 1) * 3) / segments);

    for (int i = 0; i < segments; ++i)
    {
        const float value = static_cast<float> (i + 1) / static_cast<float> (segments);
        if (value <= level)
        {
            juce::Colour segmentColour = juce::Colour (0xff2E7D32);
            if (value > 0.75f)
                segmentColour = juce::Colour (0xffD32F2F);
            else if (value > 0.58f)
                segmentColour = juce::Colour (0xffF9A825);

            g.setColour (segmentColour);
            g.fillRect (vuArea.getX() + i * (segmentWidth + 3),
                        vuArea.getY(), segmentWidth, vuArea.getHeight());
        }
    }

    const auto envelope = waveformArea.reduced (8);
    if (envelope.getWidth() > 1 && envelope.getHeight() > 1)
    {
        g.setColour (juce::Colour (kBrandVoice));
        const float columnWidth = static_cast<float> (envelope.getWidth()) / static_cast<float> (waveformEnvelope.size());
        const float centreY = envelope.getCentreY();
        const float halfHeight = envelope.getHeight() * 0.5f;

        for (std::size_t i = 0; i < waveformEnvelope.size(); ++i)
        {
            const float amplitude = juce::jlimit (0.0f, 1.0f, waveformEnvelope[i]);
            const float barHeight = juce::jmax (1.0f, amplitude * halfHeight);
            const float x = envelope.getX() + static_cast<float> (i) * columnWidth;
            g.fillRect (x, centreY - barHeight, juce::jmax (1.0f, columnWidth - 1.0f), barHeight * 2.0f);
        }
    }
}

void VoiceRecorderComponentView::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (44);
    recordButton.setBounds (area.removeFromTop (36).removeFromRight (160));
    area.removeFromTop (16);

    vuArea = area.removeFromTop (42);
    area.removeFromTop (18);
    waveformArea = area;
}

void VoiceRecorderComponentView::timerCallback()
{
    juce::ignoreUnused (engine);

    const float targetLevel = juce::jlimit (0.0f, 1.0f, recorder.getInputLevel() * 4.0f);
    displayedLevel = juce::jmax (targetLevel, displayedLevel * 0.82f);

    waveformEnvelope[waveformPosition] = targetLevel;
    waveformPosition = (waveformPosition + 1) % waveformEnvelope.size();

    recordButton.setToggleState (state.isRecording(), juce::dontSendNotification);
    recordButton.setButtonText (state.isRecording() ? "Parar gravacao" : "Gravar voz");
    repaint();
}
} // namespace mf
