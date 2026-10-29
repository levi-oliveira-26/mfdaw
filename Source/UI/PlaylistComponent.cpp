#include "UI/PlaylistComponent.h"

#include "UI/TrackPresentation.h"
#include "UI/Theme.h"

namespace mf
{
PlaylistComponent::PlaylistComponent (ProjectState& stateToUse)
    : state (stateToUse)
{
    addTrackButton.setButtonText (theme::txt ("+ Adicionar faixa"));
    addTrackButton.setColour (juce::TextButton::buttonColourId, theme::col (theme::kBrandHarmony));
    addTrackButton.setColour (juce::TextButton::textColourOffId, theme::col (theme::kBgDeep));
    addTrackButton.onClick = [this] { showAddTrackMenu(); };
    addAndMakeVisible (addTrackButton);

    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (12);
    addAndMakeVisible (viewport);

    state.getTree().addListener (this);
    rebuildRows();
}

PlaylistComponent::~PlaylistComponent()
{
    cancelPendingUpdate();
    state.getTree().removeListener (this);
    viewport.setViewedComponent (nullptr, false);
    rows.clear();
}

// ===========================================================================
// Pintura / layout
// ===========================================================================
void PlaylistComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    g.fillAll (col (kSurfaceBase));

    // Barra de título
    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (22.0f, true));
    g.drawText ("Playlist", getLocalBounds().removeFromTop (kToolbarHeight),
                juce::Justification::centredLeft, false);

    // Régua de compassos
    const auto ruler = getLocalBounds().withTrimmedTop (kToolbarHeight).removeFromTop (kRulerHeight);
    g.setColour (col (kBgDeep));
    g.fillRect (ruler);

    g.setColour (col (kTextMuted));
    g.setFont (makeFont (13.0f));
    g.drawText ("Faixas", ruler.withWidth (TrackRowComponent::kHeaderWidth).reduced (kPad, 0),
                juce::Justification::centredLeft, false);

    int bar = 1;
    for (int x = TrackRowComponent::kHeaderWidth; x < ruler.getRight(); x += TrackRowComponent::kBarWidth, ++bar)
    {
        g.setColour (col (kBorderSubtle));
        g.drawVerticalLine (x, static_cast<float> (ruler.getY()), static_cast<float> (ruler.getBottom()));

        g.setColour (col (kTextMuted));
        g.drawText (juce::String (bar),
                    juce::Rectangle<int> (x + kGrid, ruler.getY(), 48, ruler.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    // Estado vazio: o projeto nasce sem faixas.
    if (rows.isEmpty())
    {
        auto empty = getLocalBounds().withTrimmedTop (kToolbarHeight + kRulerHeight).reduced (kPad * 2);

        g.setColour (col (kTextPrimary));
        g.setFont (makeFont (24.0f, true));
        g.drawText (txt ("Seu projeto está vazio"), empty.removeFromTop (48),
                    juce::Justification::centred, false);

        g.setColour (col (kTextMuted));
        g.setFont (makeFont (16.0f));
        g.drawFittedText (txt ("Toque em \"+ Adicionar faixa\" para escolher por onde começar: "
                               "Bateria, Harmonia, Melodia, Voz ou Amostra."),
                          empty.removeFromTop (64), juce::Justification::centredTop, 2);
    }
}

void PlaylistComponent::resized()
{
    auto area = getLocalBounds();

    auto toolbar = area.removeFromTop (kToolbarHeight);
    addTrackButton.setBounds (toolbar.removeFromRight (192));

    area.removeFromTop (kRulerHeight);
    viewport.setBounds (area);

    layoutRows();
}

void PlaylistComponent::layoutRows()
{
    const int width = juce::jmax (1, viewport.getMaximumVisibleWidth());
    rowsHolder.setSize (width, juce::jmax (1, rows.size() * TrackRowComponent::kHeight));

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * TrackRowComponent::kHeight, width, TrackRowComponent::kHeight);
}

// ===========================================================================
// Reatividade (message thread)
// ===========================================================================
void PlaylistComponent::valueTreePropertyChanged (juce::ValueTree& changedTree, const juce::Identifier&)
{
    if (! changedTree.hasType (IDs::playlistTrack))
        return;

    const int id = static_cast<int> (changedTree.getProperty (IDs::trackId, -1));
    for (auto* row : rows)
    {
        if (row->getTrackId() == id)
        {
            row->refreshFromState();
            break;
        }
    }
}

void PlaylistComponent::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    // Também cobre carregar projeto: a raiz troca o nó PLAYLIST inteiro.
    if (parent.hasType (IDs::playlist) || child.hasType (IDs::playlist))
        triggerAsyncUpdate();
}

void PlaylistComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (parent.hasType (IDs::playlist) || child.hasType (IDs::playlist))
        triggerAsyncUpdate();
}

void PlaylistComponent::valueTreeRedirected (juce::ValueTree&)
{
    triggerAsyncUpdate();
}

void PlaylistComponent::handleAsyncUpdate()
{
    const int previousCount = rows.size();
    rebuildRows();

    // Faixa nova => rola até ela.
    if (rows.size() > previousCount)
        viewport.setViewPosition (0, juce::jmax (0, rowsHolder.getHeight() - viewport.getHeight()));
}

void PlaylistComponent::rebuildRows()
{
    rows.clear();

    for (int i = 0; i < state.getNumTracks(); ++i)
    {
        if (const auto track = state.getTrackByIndex (i))
        {
            auto* row = rows.add (new TrackRowComponent (state, *track));
            row->onAction = [this] (TrackAction action, int trackId)
            {
                if (onTrackAction)
                    onTrackAction (action, trackId);
            };
            rowsHolder.addAndMakeVisible (row);
        }
    }

    addTrackButton.setEnabled (state.getNumTracks() < kMaxPlaylistTracks);
    layoutRows();
    repaint();
}

void PlaylistComponent::showAddTrackMenu()
{
    juce::PopupMenu menu;

    for (int type = 0; type < kNumTrackTypes; ++type)
    {
        juce::PopupMenu::Item item (presentation::typeLabel (static_cast<TrackType> (type)));
        item.itemID = 1 + type;
        item.colour = presentation::colourFor (static_cast<TrackType> (type));
        menu.addItem (item);
    }

    juce::Component::SafePointer<PlaylistComponent> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton),
                        [safeThis] (int result)
                        {
                            if (safeThis == nullptr || result <= 0)
                                return;

                            safeThis->state.addTrack (static_cast<TrackType> (result - 1));
                        });
}

} // namespace mf
