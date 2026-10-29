#include "UI/TrackRowComponent.h"

#include <cmath>

#include "Core/FaderScale.h"
#include "UI/TrackPresentation.h"
#include "UI/Theme.h"

namespace mf
{
TrackRowComponent::TrackRowComponent (ProjectState& stateToUse, const TrackModel& model)
    : state (stateToUse),
      trackId (model.getId()),
      type (model.getType()),
      action (model.getPrimaryAction())
{
    const auto accent = presentation::colourFor (type);

    // ----- Nome editável (duplo clique) -----
    nameLabel.setEditable (false, true, false);
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setFont (theme::makeFont (18.0f, true));
    nameLabel.setColour (juce::Label::textColourId, theme::col (theme::kTextPrimary));
    nameLabel.onTextChange = [this]
    {
        if (auto track = state.getTrackById (trackId))
            track->setName (nameLabel.getText());   // vazio é ignorado pelo modelo

        refreshFromState();                          // restaura o texto se foi rejeitado
    };
    addAndMakeVisible (nameLabel);

    // ----- Fader de volume pré-mix (gain staging) -----
    fader.setSliderStyle (juce::Slider::LinearHorizontal);
    fader.setRange (0.0, 1.0, 0.0);
    fader.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
    fader.setTextBoxIsEditable (false);
    fader.setDoubleClickReturnValue (true, static_cast<double> (fader::kDefaultPosition));
    fader.setColour (juce::Slider::trackColourId, accent);
    fader.setColour (juce::Slider::backgroundColourId, theme::col (theme::kBorderSubtle));
    fader.setColour (juce::Slider::thumbColourId, theme::col (theme::kTextPrimary));
    fader.textFromValueFunction = [] (double position)
    {
        const float db = fader::toDecibels (static_cast<float> (position));
        return std::isinf (db) ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    };
    fader.onValueChange = [this]
    {
        if (auto track = state.getTrackById (trackId))
            track->setGain (static_cast<float> (fader.getValue()));
    };
    addAndMakeVisible (fader);

    // ----- Botão de ação (o que fazer com esta faixa) -----
    actionButton.setButtonText (presentation::actionLabel (action));
    actionButton.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.85f));
    actionButton.setColour (juce::TextButton::textColourOffId, theme::col (theme::kBgDeep));
    actionButton.onClick = [this]
    {
        if (onAction)
            onAction (action, trackId);
    };
    addAndMakeVisible (actionButton);

    // ----- Remover faixa -----
    removeButton.setButtonText (theme::txt ("×"));
    removeButton.onClick = [this] { state.removeTrack (trackId); };   // a Playlist reconstrói de forma assíncrona
    addAndMakeVisible (removeButton);

    refreshFromState();
}

void TrackRowComponent::refreshFromState()
{
    const auto track = state.getTrackById (trackId);
    if (track == nullptr)
        return;

    const auto name = track->getName();
    if (nameLabel.getText() != name)
        nameLabel.setText (name, juce::dontSendNotification);

    const double gain = static_cast<double> (track->getGain());
    if (fader.getValue() != gain)
        fader.setValue (gain, juce::dontSendNotification);
}

void TrackRowComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto bounds = getLocalBounds();
    const auto accent = presentation::colourFor (type);
    const auto header = bounds.withWidth (kHeaderWidth);

    // Cabeçalho
    g.setColour (col (kSurfaceElevated));
    g.fillRect (header);
    g.setColour (accent);
    g.fillRect (header.withWidth (kGrid));

    // Lane (clipes entram nas próximas fatias): grade de compassos.
    g.setColour (col (kBgDeep));
    g.fillRect (bounds.withTrimmedLeft (kHeaderWidth));

    g.setColour (col (kBorderSubtle));
    for (int x = kHeaderWidth + kBarWidth; x < bounds.getRight(); x += kBarWidth)
        g.drawVerticalLine (x, 0.0f, static_cast<float> (bounds.getHeight()));

    g.setColour (col (kBorderSubtle));
    g.drawHorizontalLine (bounds.getBottom() - 1, 0.0f, static_cast<float> (bounds.getRight()));
    g.drawVerticalLine (kHeaderWidth - 1, 0.0f, static_cast<float> (bounds.getHeight()));
}

void TrackRowComponent::resized()
{
    using namespace theme;

    auto inner = getLocalBounds().withWidth (kHeaderWidth)
                                 .withTrimmedLeft (kGrid)   // faixa de cor
                                 .reduced (kGrid);

    auto top = inner.removeFromTop (kMinTouch);
    removeButton.setBounds (top.removeFromRight (kMinTouch));
    top.removeFromRight (kGrid);
    nameLabel.setBounds (top);

    inner.removeFromTop (kGrid);

    auto bottom = inner.removeFromTop (kMinTouch);
    actionButton.setBounds (bottom.removeFromRight (128));
    bottom.removeFromRight (kGrid);
    fader.setBounds (bottom);
}

} // namespace mf
