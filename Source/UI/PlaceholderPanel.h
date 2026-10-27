#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "UI/Theme.h"

namespace mf
{
/** Painel "em breve" para os passos do Wizard ainda não implementados (e para o Modo Livre). */
class PlaceholderPanel final : public juce::Component
{
public:
    void setContent (const juce::String& newTitle, const juce::String& newDescription, juce::Colour newAccent)
    {
        title = newTitle;
        description = newDescription;
        accent = newAccent;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();

        g.setColour (accent);
        g.setFont (theme::makeFont (28.0f, true));
        g.drawText (title, area.removeFromTop (48), juce::Justification::centredLeft);

        area.removeFromTop (theme::kGrid);

        g.setColour (theme::col (theme::kTextMuted));
        g.setFont (theme::makeFont (16.0f));
        g.drawFittedText (description, area, juce::Justification::topLeft, 6);
    }

private:
    juce::String title, description;
    juce::Colour accent { theme::col (theme::kTextPrimary) };
};

} // namespace mf
