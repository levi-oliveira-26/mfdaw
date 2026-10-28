#include "UI/SmartChordsComponent.h"

#include "UI/Theme.h"

namespace mf
{
namespace
{
constexpr const char* kNoteNames[12] =
{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

int transposeNote (int root, int interval) noexcept
{
    return (root + interval + 12) % 12;
}

juce::String chordName (int root, int interval, bool minor = false)
{
    juce::String name { kNoteNames[transposeNote (root, interval)] };
    return minor ? name + "m" : name;
}

std::array<juce::String, 4> buildProgression (int root, Scale scale)
{
    if (scale == Scale::Minor)
    {
        return { chordName (root, 0, true), chordName (root, 8),
                 chordName (root, 3), chordName (root, 10) };
    }

    return { chordName (root, 0), chordName (root, 7),
             chordName (root, 9, true), chordName (root, 5) };
}
} // namespace

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
    const auto progression = buildProgression (state.getKeyRoot(), state.getScale());

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
