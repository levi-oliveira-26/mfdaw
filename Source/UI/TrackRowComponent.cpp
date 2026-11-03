#include "UI/TrackRowComponent.h"

#include <cmath>
#include <memory>
#include <tuple>

#include <juce_audio_formats/juce_audio_formats.h>

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

    // Não recarrega no meio de um arrasto: a pré-visualização manda até o mouseUp.
    if (dragMode == DragMode::None)
    {
        clips = state.getClips (trackId);
        takes = type == TrackType::Record ? state.getTakes (trackId) : std::vector<TakeData> {};
        refreshTakeVisuals();
    }

    repaint();
}

void TrackRowComponent::setSelectedClipId (int clipId)
{
    if (selectedClipId == clipId)
        return;

    selectedClipId = clipId;
    repaint();
}

const TrackRowComponent::TakeVisual* TrackRowComponent::findTakeVisual (int takeId) const noexcept
{
    for (const auto& visual : takeVisuals)
        if (visual.id == takeId)
            return &visual;

    return nullptr;
}

void TrackRowComponent::refreshTakeVisuals()
{
    if (type != TrackType::Record)
    {
        takeVisuals.clear();
        return;
    }

    for (const auto& clip : clips)
    {
        if (clip.takeId < 0 || findTakeVisual (clip.takeId) != nullptr)
            continue;

        auto takeIt = std::find_if (takes.begin(), takes.end(),
                                    [&clip] (const TakeData& takeData) { return takeData.id == clip.takeId; });
        if (takeIt == takes.end())
            continue;

        TakeVisual visual;
        visual.id = takeIt->id;
        visual.filePath = takeIt->filePath;
        visual.label = "Take " + juce::String (static_cast<int> (std::distance (takes.begin(), takeIt)) + 1);

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (takeIt->filePath)));

        if (reader != nullptr && reader->lengthInSamples > 0 && reader->numChannels > 0)
        {
            constexpr int kBins = static_cast<int> (std::tuple_size<decltype (visual.peaks)>::value);
            const auto totalSamples = reader->lengthInSamples;
            const int windowSize = static_cast<int> (juce::jmin<int64> (2048, juce::jmax<int64> (1, totalSamples / kBins)));
            juce::AudioBuffer<float> window (1, windowSize);

            for (int bin = 0; bin < kBins; ++bin)
            {
                const auto latestStart = juce::jmax<int64> (0, totalSamples - windowSize);
                const auto startSample = kBins > 1 ? latestStart * bin / (kBins - 1) : 0;
                window.clear();
                if (reader->read (&window, 0, windowSize, startSample, true, false))
                {
                    float peak = 0.0f;
                    for (int sample = 0; sample < windowSize; ++sample)
                        peak = juce::jmax (peak, std::abs (window.getSample (0, sample)));
                    visual.peaks[static_cast<std::size_t> (bin)] = juce::jlimit (0.0f, 1.0f, peak);
                }
            }
        }

        takeVisuals.push_back (std::move (visual));
    }

    takeVisuals.erase (std::remove_if (takeVisuals.begin(), takeVisuals.end(),
                                       [this] (const TakeVisual& visual)
                                       {
                                           return std::none_of (takes.begin(), takes.end(),
                                                                [&visual] (const TakeData& t)
                                                                {
                                                                    return t.id == visual.id && t.filePath == visual.filePath;
                                                                });
                                       }),
                       takeVisuals.end());
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

    // Lane: grade de compassos (largura proporcional) + clipes.
    g.setColour (col (kBgDeep));
    g.fillRect (bounds.withTrimmedLeft (kHeaderWidth));

    const float bw = barWidth();
    g.setColour (col (kBorderSubtle));
    for (int bar = 1; bar < kPlaylistBars; ++bar)
        g.drawVerticalLine (kHeaderWidth + juce::roundToInt (bar * bw), 0.0f, static_cast<float> (bounds.getHeight()));

    g.setFont (makeFont (13.0f, true));
    for (const auto& c : clips)
    {
        const bool isDragged = dragMode != DragMode::None && c.id == dragOriginal.id;
        const auto rect = clipBounds (isDragged ? dragPreview : c);

        g.setColour (accent.withAlpha (isDragged ? 1.0f : 0.85f));
        g.fillRoundedRectangle (rect, 4.0f);

        // Alça de redimensionar (plana, sem sombra).
        g.setColour (col (kBgDeep).withAlpha (0.35f));
        g.fillRect (rect.withLeft (rect.getRight() - 4.0f).reduced (0.0f, 8.0f));

        if (type == TrackType::Record)
        {
            const auto* visual = findTakeVisual (c.takeId);
            auto labelArea = rect.reduced (6.0f, 4.0f);
            labelArea = labelArea.removeFromTop (18.0f);
            g.setColour (col (kBgDeep));
            g.drawText (visual != nullptr ? visual->label : juce::String ("Sem take"),
                        labelArea, juce::Justification::centredLeft, true);

            if (visual != nullptr)
            {
                auto waveArea = rect.reduced (5.0f, 4.0f).withTrimmedTop (24.0f).withTrimmedBottom (5.0f);
                const float centreY = waveArea.getCentreY();
                const float halfHeight = juce::jmax (1.0f, waveArea.getHeight() * 0.48f);
                const float columnWidth = waveArea.getWidth() / static_cast<float> (visual->peaks.size());
                g.setColour (col (kBgDeep).withAlpha (0.85f));

                for (std::size_t i = 0; i < visual->peaks.size(); ++i)
                {
                    const float amplitude = juce::jmax (0.04f, visual->peaks[i]);
                    const float barHeight = amplitude * halfHeight;
                    const float x = waveArea.getX() + static_cast<float> (i) * columnWidth;
                    g.fillRect (x, centreY - barHeight,
                                juce::jmax (1.0f, columnWidth - 1.0f), barHeight * 2.0f);
                }
            }
        }
        else
        {
            g.setColour (col (kBgDeep));
            g.drawText (nameLabel.getText(), rect.reduced (8.0f, 0.0f).withTrimmedRight (6.0f),
                        juce::Justification::centredLeft, true);
        }

        if (c.id == selectedClipId)
        {
            g.setColour (col (kTextPrimary));
            g.drawRoundedRectangle (rect.reduced (1.0f), 4.0f, 2.0f);
        }
    }

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

// ---------------------------------------------------------------------------
// Clipes: geometria e interação (lane = à direita do cabeçalho)
// ---------------------------------------------------------------------------
juce::Rectangle<float> TrackRowComponent::clipBounds (const ClipData& c) const noexcept
{
    const float bw = barWidth();
    return { static_cast<float> (kHeaderWidth) + static_cast<float> (c.startBar) * bw + 1.0f, 8.0f,
             static_cast<float> (c.lengthBars) * bw - 2.0f, static_cast<float> (getHeight()) - 17.0f };
}

int TrackRowComponent::clipIndexAtX (int x) const noexcept
{
    if (x < kHeaderWidth)
        return -1;

    for (std::size_t i = 0; i < clips.size(); ++i)
    {
        const auto rect = clipBounds (clips[i]);
        if (static_cast<float> (x) >= rect.getX() && static_cast<float> (x) < rect.getRight())
            return static_cast<int> (i);
    }

    return -1;
}

void TrackRowComponent::mouseMove (const juce::MouseEvent& event)
{
    const int index = clipIndexAtX (event.x);
    const bool onHandle = index >= 0
                       && static_cast<float> (event.x) >= clipBounds (clips[static_cast<std::size_t> (index)]).getRight()
                                                          - static_cast<float> (kResizeHandle);

    setMouseCursor (onHandle ? juce::MouseCursor::LeftRightResizeCursor
                             : (index >= 0 ? juce::MouseCursor::DraggingHandCursor
                                           : juce::MouseCursor::NormalCursor));
}

void TrackRowComponent::mouseDown (const juce::MouseEvent& event)
{
    dragMode = DragMode::None;

    const int index = clipIndexAtX (event.x);
    if (index < 0)
        return;

    const auto hit = clips[static_cast<std::size_t> (index)];

    if (event.mods.isPopupMenu())
    {
        showClipMenu (hit.id);
        return;
    }

    if (! event.mods.isLeftButtonDown())
        return;

    if (onClipSelected)
        onClipSelected (trackId, hit.id);

    dragOriginal = dragPreview = hit;
    dragMode = static_cast<float> (event.x) >= clipBounds (hit).getRight() - static_cast<float> (kResizeHandle)
                   ? DragMode::ResizeRight
                   : DragMode::Move;
}

void TrackRowComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (dragMode == DragMode::None)
        return;

    const int deltaBars = juce::roundToInt (static_cast<float> (event.getDistanceFromDragStartX()) / barWidth());
    ClipData candidate = dragOriginal;

    if (dragMode == DragMode::Move)
    {
        candidate.startBar = clip::clampStart (dragOriginal.startBar + deltaBars, dragOriginal.lengthBars);
    }
    else
    {
        candidate.lengthBars = juce::jlimit (kMinClipBars, kPlaylistBars - dragOriginal.startBar,
                                             dragOriginal.lengthBars + deltaBars);
    }

    // O domínio decide se a posição é válida; se não for, a pré-visualização fica onde estava.
    if (state.canPlaceClip (trackId, candidate.startBar, candidate.lengthBars, dragOriginal.id)
        && (candidate.startBar != dragPreview.startBar || candidate.lengthBars != dragPreview.lengthBars))
    {
        dragPreview = candidate;
        repaint();
    }
}

void TrackRowComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragMode == DragMode::None)
        return;

    dragMode = DragMode::None;   // antes de gravar: o refresh disparado pela árvore já recarrega os clipes

    if (dragPreview.startBar != dragOriginal.startBar || dragPreview.lengthBars != dragOriginal.lengthBars)
        state.setClipRange (trackId, dragOriginal.id, dragPreview.startBar, dragPreview.lengthBars);

    clips = state.getClips (trackId);
    repaint();
}

void TrackRowComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (event.x < kHeaderWidth || clipIndexAtX (event.x) >= 0)
        return;

    const int bar = juce::jlimit (0, kPlaylistBars - 1,
                                  static_cast<int> (static_cast<float> (event.x - kHeaderWidth) / barWidth()));
    const int room = state.getMaxClipLengthAt (trackId, bar);

    if (room >= kMinClipBars)
        state.addClip (trackId, bar, juce::jmin (kDefaultClipBars, room));
}

void TrackRowComponent::showClipMenu (int clipId)
{
    juce::PopupMenu menu;
    menu.addItem (1, theme::txt ("Remover clipe"));

    juce::Component::SafePointer<TrackRowComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis, clipId] (int result)
    {
        if (safeThis != nullptr && result == 1)
            safeThis->state.removeClip (safeThis->trackId, clipId);
    });
}

} // namespace mf
