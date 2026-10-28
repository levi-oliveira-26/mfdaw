#include "UI/SmartChordsComponent.h"

#include "Core/ChordGenerator.h"
#include "UI/Theme.h"

namespace mf
{

SmartChordsComponent::SmartChordsComponent (ProjectState& projectState)
    : state (projectState)
{
    for (auto& label : chordLabels)
    {
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, theme::col (theme::kBgDeep));
        label.setFont (theme::makeFont (28.0f, true));
        addAndMakeVisible (label);
    }

    state.getTree().addListener (this);
    refreshChords();
}

SmartChordsComponent::~SmartChordsComponent()
{
    state.getTree().removeListener (this);
}

void SmartChordsComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::col (theme::kSurfaceBase));

    g.setColour (theme::col (theme::kTextPrimary));
    g.setFont (theme::makeFont (22.0f, true));
    g.drawText ("Smart Chords", getLocalBounds().removeFromTop (48),
                juce::Justification::centredLeft);

    g.setColour (theme::col (theme::kBrandHarmony));
    for (const auto& label : chordLabels)
        g.fillRoundedRectangle (label.getBounds().toFloat(), 8.0f);
}

void SmartChordsComponent::resized()
{
    auto area = getLocalBounds().reduced (theme::kPad);
    area.removeFromTop (48 + theme::kGrid);

    const int gap = theme::kGrid;
    const int cardWidth = juce::jmax (64, (area.getWidth() - gap * 3) / 4);
    auto cards = area.removeFromTop (juce::jmin (144, area.getHeight()));

    for (auto& label : chordLabels)
    {
        label.setBounds (cards.removeFromLeft (cardWidth));
        cards.removeFromLeft (gap);
    }
}

void SmartChordsComponent::refreshChords()
{
    const auto progression = ChordGenerator::generate (state.getKeyRoot(), state.getScale());

    for (size_t i = 0; i < chordLabels.size(); ++i)
        chordLabels[i].setText (progression[i], juce::dontSendNotification);

    repaint();
}

void SmartChordsComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == IDs::keyRoot || property == IDs::scale)
        refreshChords();
}

void SmartChordsComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) {}
void SmartChordsComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) {}
} // namespace mf
