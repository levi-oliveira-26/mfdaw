#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "UI/Theme.h"

namespace mf
{
/**
 * LookAndFeel plano e vetorial (sem gradientes, sem sombras, sem blur):
 * apenas retângulos arredondados preenchidos, baratos para a UI thread.
 */
class MfLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MfLookAndFeel()
    {
        using namespace theme;

        setColour (juce::ResizableWindow::backgroundColourId, col (kBgDeep));

        setColour (juce::TextButton::buttonColourId,   col (kSurfaceElevated));
        setColour (juce::TextButton::buttonOnColourId, col (kBrandHarmony));
        setColour (juce::TextButton::textColourOffId,  col (kTextPrimary));
        setColour (juce::TextButton::textColourOnId,   col (kBgDeep));

        setColour (juce::Label::textColourId, col (kTextPrimary));

        setColour (juce::ComboBox::backgroundColourId, col (kSurfaceElevated));
        setColour (juce::ComboBox::textColourId,       col (kTextPrimary));
        setColour (juce::ComboBox::outlineColourId,    col (kBorderSubtle));
        setColour (juce::ComboBox::arrowColourId,      col (kTextMuted));

        setColour (juce::PopupMenu::backgroundColourId,            col (kSurfaceElevated));
        setColour (juce::PopupMenu::textColourId,                  col (kTextPrimary));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col (kBrandHarmony));
        setColour (juce::PopupMenu::highlightedTextColourId,       col (kBgDeep));

        setColour (juce::Slider::textBoxTextColourId,       col (kTextPrimary));
        setColour (juce::Slider::textBoxBackgroundColourId, col (kSurfaceElevated));
        setColour (juce::Slider::textBoxOutlineColourId,    col (kBorderSubtle));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto colour = backgroundColour;

        if (! button.isEnabled())
            colour = colour.withMultipliedAlpha (0.35f);
        else if (shouldDrawButtonAsDown)
            colour = colour.brighter (0.2f);
        else if (shouldDrawButtonAsHighlighted)
            colour = colour.brighter (0.1f);

        g.setColour (colour);
        g.fillRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 6.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return theme::makeFont (15.0f, true);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        juce::ignoreUnused (slider);

        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius,
                             0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff444444));
        g.strokePath (track, juce::PathStrokeType (3.0f));

        juce::Path valueTrack;
        valueTrack.addCentredArc (centre.x, centre.y, radius, radius,
                                  0.0f, rotaryStartAngle, angle, true);
        g.setColour (juce::Colour (0xffff4c4c));
        g.strokePath (valueTrack, juce::PathStrokeType (3.0f));

        g.setColour (juce::Colour (0xffd9d9d9));
        g.fillEllipse (centre.x - radius * 0.58f, centre.y - radius * 0.58f,
                       radius * 1.16f, radius * 1.16f);

        g.setColour (juce::Colours::white);
        juce::Line<float> pointer (centre.getX(), centre.getY(),
                                   centre.getX(), centre.getY() - radius * 0.42f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle,
                                                                  centre.getX(), centre.getY()));
        g.drawLine (pointer, 2.0f);
    }
};

} // namespace mf
