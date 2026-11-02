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
    using namespace theme;

    g.fillAll (col (kSurfaceBase));

    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (22.0f, true));
    g.drawText (txt ("Smart Chords · Harmonia"), getLocalBounds().removeFromTop (48),
                juce::Justification::centredLeft);

    // Cartão ligado = preenchido (soa); desligado = só contorno (silêncio naquele compasso).
    for (size_t i = 0; i < chordLabels.size(); ++i)
    {
        const auto bounds = chordLabels[i].getBounds().toFloat();
        const bool armed = ((armedMask >> i) & 1) != 0;

        g.setColour (col (kBrandHarmony).withAlpha (pressedChord == static_cast<int> (i) ? 1.0f : (armed ? 0.85f : 0.0f)));
        g.fillRoundedRectangle (bounds, 8.0f);

        if (! armed)
        {
            g.setColour (col (kBrandHarmony));
            g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 2.0f);
        }

        g.setColour (col (kTextMuted));
        g.setFont (makeFont (13.0f));
        g.drawText (txt ("Compasso ") + juce::String (static_cast<int> (i) + 1) + txt (" (e a cada 4)"),
                    juce::Rectangle<int> (chordLabels[i].getX(), chordLabels[i].getBottom() + kGrid,
                                          chordLabels[i].getWidth(), 20),
                    juce::Justification::centred, true);
    }

    g.setColour (col (kTextMuted));
    g.setFont (makeFont (14.0f));
    g.drawFittedText (trackId < 0 ? txt ("Abra uma faixa de Harmonia para editar a progressão.")
                                  : txt ("Toque num cartão para ouvir. Duplo clique liga ou desliga o acorde "
                                         "daquele compasso."),
                      getLocalBounds().reduced (kPad).removeFromBottom (44), juce::Justification::centredLeft, 2);
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

void SmartChordsComponent::setTrackId (int newTrackId)
{
    if (newTrackId == trackId)
        return;

    trackId = newTrackId;
    refreshArmed();
}

void SmartChordsComponent::refreshArmed()
{
    armedMask = trackId >= 0 ? state.getHarmonyArmedMask (trackId) : 0;

    for (size_t i = 0; i < chordLabels.size(); ++i)
        chordLabels[i].setColour (juce::Label::textColourId,
                                  ((armedMask >> i) & 1) != 0 ? theme::col (theme::kBgDeep)
                                                              : theme::col (theme::kBrandHarmony));

    repaint();
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

void SmartChordsComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (trackId < 0)
        return;

    for (size_t i = 0; i < chordLabels.size(); ++i)
    {
        if (chordLabels[i].getBounds().contains (event.getPosition()))
        {
            const bool armed = ((armedMask >> i) & 1) != 0;
            state.setHarmonyCardArmed (trackId, static_cast<int> (i), ! armed);   // o listener atualiza a tela
            break;
        }
    }
}

void SmartChordsComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == IDs::keyRoot || property == IDs::scale)
        refreshChords();
    else if (property == IDs::chordMask)
        refreshArmed();   // inclui undo/redo
}

// Estrutura da Playlist trocada (carregar projeto, remover a faixa aberta): relê o estado.
void SmartChordsComponent::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (parent.hasType (IDs::playlist) || child.hasType (IDs::playlist))
        refreshArmed();
}

void SmartChordsComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (parent.hasType (IDs::playlist) || child.hasType (IDs::playlist))
        refreshArmed();
}
} // namespace mf
