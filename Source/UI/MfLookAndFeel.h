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
};

} // namespace mf
