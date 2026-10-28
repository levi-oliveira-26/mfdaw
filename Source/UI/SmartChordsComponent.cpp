#include "UI/SmartChordsComponent.h"

#include "Core/ChordGenerator.h"
#include "UI/Theme.h"

namespace mf
{

SmartChordsComponent::SmartChordsComponent (ProjectState& projectState, MainProcessor& audioProcessor)
    : state (projectState), processor (audioProcessor)
{
    for (auto& label : chordLabels)
    {
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, theme::col (theme::kBgDeep));
        label.setFont (theme::makeFont (28.0f, true));
        label.setInterceptsMouseClicks (false, false);
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

void SmartChordsComponent::mouseDown (const juce::MouseEvent& event)
{
    for (size_t i = 0; i < chordLabels.size(); ++i)
    {
        if (chordLabels[i].getBounds().contains (event.getPosition()))
        {
            pressedChord = static_cast<int> (i);
            processor.requestChordPreview (pressedChord, true);
            repaint();
            break;
        }
    }
}

void SmartChordsComponent::mouseUp (const juce::MouseEvent&)
{
    if (pressedChord >= 0)
    {
        processor.requestChordPreview (pressedChord, false);
        pressedChord = -1;
        repaint();
    }
}

void SmartChordsComponent::mouseDrag (const juce::MouseEvent& event)
{
    int chordUnderPointer = -1;

    for (size_t i = 0; i < chordLabels.size(); ++i)
    {
        if (chordLabels[i].getBounds().contains (event.getPosition()))
        {
            chordUnderPointer = static_cast<int> (i);
            break;
        }
    }

    if (chordUnderPointer == pressedChord)
        return;

    if (pressedChord >= 0)
        processor.requestChordPreview (pressedChord, false);

    pressedChord = chordUnderPointer;
    if (pressedChord >= 0)
        processor.requestChordPreview (pressedChord, true);

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
