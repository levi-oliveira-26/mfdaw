#include "UI/VoiceRecorderComponentView.h"

#include <algorithm>

#include "UI/Theme.h"

namespace mf
{
VoiceRecorderComponentView::VoiceRecorderComponentView (MainProcessor& processor)
    : engine (processor),
      state (processor.getProjectState()),
      recorder (processor.getAudioRecorder())
{
    recordButton.setButtonText (juce::String ("Gravar voz"));
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (theme::kActionRecord));
    recordButton.onClick = [this]
    {
        if (state.isRecording())
        {
            state.setRecording (false);
            engine.cancelRecordingCountIn();
        }
        else if (engine.getRecordingCountIn() > 0)
        {
            state.setRecording (false);
            engine.cancelRecordingCountIn();
        }
        else
        {
            engine.requestRecordingCountIn();
            if (! state.isPlaying())
                state.setPlaying (true);
        }
        recordButton.repaint();
    };
    addAndMakeVisible (recordButton);

    countInLabel.setJustificationType (juce::Justification::centred);
    countInLabel.setFont (theme::makeFont (18.0f, true));
    countInLabel.setColour (juce::Label::textColourId, juce::Colour (theme::kBrandVoice));
    addAndMakeVisible (countInLabel);

    startTimerHz (30);
}

VoiceRecorderComponentView::~VoiceRecorderComponentView()
{
    stopTimer();
}

void VoiceRecorderComponentView::setTrackId (int newTrackId)
{
    trackId = newTrackId;
    engine.setRecordingTarget (trackId);
    repaint();
}

void VoiceRecorderComponentView::setSelectedClip (int newTrackId, int clipId)
{
    selectedClipTrackId = newTrackId;
    selectedClipId = clipId;
    selectedClipTakeId = -1;

    if (selectedClipTrackId >= 0 && selectedClipId >= 0)
        for (const auto& clip : state.getClips (selectedClipTrackId))
            if (clip.id == selectedClipId)
            {
                selectedClipTakeId = clip.takeId;
                break;
            }

    repaint();
}

int VoiceRecorderComponentView::takeRowAt (juce::Point<int> position) const
{
    if (! takesArea.contains (position))
        return -1;

    const int row = (position.y - takesArea.getY() - kTakeRowHeight) / kTakeRowHeight;   // 1ª linha = título
    return position.y - takesArea.getY() >= kTakeRowHeight && row >= 0 && row < static_cast<int> (takes.size()) ? row : -1;
}

void VoiceRecorderComponentView::mouseDown (const juce::MouseEvent& event)
{
    const int row = takeRowAt (event.getPosition());
    if (row < 0 || trackId < 0 || selectedClipTrackId != trackId || selectedClipId < 0)
        return;

    // Escolher um take altera o clipe selecionado, não o take ativo global da faixa.
    if (state.setClipTake (selectedClipTrackId, selectedClipId, takes[static_cast<std::size_t> (row)].id))
        selectedClipTakeId = takes[static_cast<std::size_t> (row)].id;

    repaint();
}

void VoiceRecorderComponentView::paint (juce::Graphics& g)
{
    using namespace theme;

    g.fillAll (col (kSurfaceBase));
    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (20.0f, true));
    g.drawText (txt ("Gravação de voz"), getLocalBounds().removeFromTop (44), juce::Justification::centredLeft, false);

    g.setColour (col (kBorderSubtle));
    g.drawRoundedRectangle (vuArea.toFloat(), 6.0f, 1.0f);
    g.drawRoundedRectangle (waveformArea.toFloat(), 6.0f, 1.0f);

    const float level = juce::jlimit (0.0f, 1.0f, displayedLevel);
    const int segments = 24;
    const int segmentWidth = juce::jmax (2, (vuArea.getWidth() - (segments - 1) * 3) / segments);

    for (int i = 0; i < segments; ++i)
    {
        const float value = static_cast<float> (i + 1) / static_cast<float> (segments);
        if (value <= level)
        {
            juce::Colour segmentColour = juce::Colour (0xff2E7D32);
            if (value > 0.75f)
                segmentColour = juce::Colour (0xffD32F2F);
            else if (value > 0.58f)
                segmentColour = juce::Colour (0xffF9A825);

            g.setColour (segmentColour);
            g.fillRect (vuArea.getX() + i * (segmentWidth + 3),
                        vuArea.getY(), segmentWidth, vuArea.getHeight());
        }
    }

    const auto envelope = waveformArea.reduced (8);
    if (envelope.getWidth() > 1 && envelope.getHeight() > 1)
    {
        g.setColour (juce::Colour (kBrandVoice));
        const float columnWidth = static_cast<float> (envelope.getWidth()) / static_cast<float> (waveformEnvelope.size());
        const float centreY = static_cast<float> (envelope.getCentreY());
        const float halfHeight = static_cast<float> (envelope.getHeight()) * 0.5f;

        for (std::size_t i = 0; i < waveformEnvelope.size(); ++i)
        {
            const float amplitude = juce::jlimit (0.0f, 1.0f, waveformEnvelope[i]);
            const float barHeight = juce::jmax (1.0f, amplitude * halfHeight);
            const float x = envelope.getX() + static_cast<float> (i) * columnWidth;
            g.fillRect (x, centreY - barHeight, juce::jmax (1.0f, columnWidth - 1.0f), barHeight * 2.0f);
        }
    }

    // Lista de takes (mais recente primeiro); o ativo é o que a faixa usa.
    if (takesArea.getHeight() >= kTakeRowHeight)
    {
        auto list = takesArea;
        g.setColour (col (kTextPrimary));
        g.setFont (makeFont (15.0f, true));
        g.drawText (takes.empty() ? txt ("Nenhuma gravação ainda — grave sua voz!")
                                  : (selectedClipTrackId == trackId && selectedClipId >= 0
                                         ? txt ("Escolha um take para o clipe selecionado")
                                         : txt ("Selecione um clipe de Voz na Playlist primeiro")),
                    list.removeFromTop (kTakeRowHeight), juce::Justification::centredLeft, false);

        const int maxRows = list.getHeight() / kTakeRowHeight;
        for (int i = 0; i < static_cast<int> (takes.size()) && i < maxRows; ++i)
        {
            const auto& t = takes[static_cast<std::size_t> (i)];
            auto row = list.removeFromTop (kTakeRowHeight).reduced (0, 2);
            const bool selectedForClip = selectedClipTrackId == trackId
                                       && selectedClipId >= 0 && t.id == selectedClipTakeId;
            const bool active = t.id == activeTakeId;

            g.setColour (selectedForClip ? juce::Colour (kBrandVoice).withAlpha (0.22f)
                                         : col (kSurfaceBase).brighter (0.05f));
            g.fillRoundedRectangle (row.toFloat(), 4.0f);
            g.setColour (selectedForClip ? juce::Colour (kBrandVoice) : col (kTextPrimary));
            g.setFont (makeFont (14.0f, selectedForClip));
            g.drawText (txt ("Gravação ") + juce::String (static_cast<int> (takes.size()) - i)
                            + "  ·  " + txt ("compasso ") + juce::String (t.startBar() + 1)
                            + "  ·  " + juce::String (t.durationSeconds, 1) + " s"
                            + (selectedForClip ? txt ("  ·  no clipe selecionado")
                                               : (active ? txt ("  ·  ativo na faixa") : juce::String())),
                        row.reduced (10, 0), juce::Justification::centredLeft, false);
        }
    }
}

void VoiceRecorderComponentView::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (44);
    auto controls = area.removeFromTop (48);
    recordButton.setBounds (controls.removeFromRight (160));
    countInLabel.setBounds (controls.removeFromRight (70));
    area.removeFromTop (16);

    vuArea = area.removeFromTop (42);
    area.removeFromTop (18);
    takesArea = area.removeFromBottom (juce::jmin (area.getHeight() / 2, kTakeRowHeight * 6));
    area.removeFromBottom (12);
    waveformArea = area;
}

void VoiceRecorderComponentView::timerCallback()
{
    if (engine.consumeRecordingStart() && ! state.isRecording())
        state.setRecording (true);

    // Takes mudam raramente (ValueTree), mas ler ≤32 nós a 30 Hz é barato e evita listeners extras.
    if (trackId >= 0)
    {
        auto latest = state.getTakes (trackId);
        std::reverse (latest.begin(), latest.end());
        takes = std::move (latest);
        activeTakeId = state.getActiveTakeId (trackId);
        selectedClipTakeId = -1;
        if (selectedClipTrackId == trackId && selectedClipId >= 0)
            for (const auto& clip : state.getClips (trackId))
                if (clip.id == selectedClipId)
                {
                    selectedClipTakeId = clip.takeId;
                    break;
                }
    }
    else
    {
        takes.clear();
        activeTakeId = -1;
    }

    const float targetLevel = juce::jlimit (0.0f, 1.0f, recorder.getInputLevel() * 4.0f);
    displayedLevel = juce::jmax (targetLevel, displayedLevel * 0.82f);

    waveformEnvelope[waveformPosition] = targetLevel;
    waveformPosition = (waveformPosition + 1) % waveformEnvelope.size();

    recordButton.setToggleState (state.isRecording(), juce::dontSendNotification);

    if (state.isRecording())
        recordButton.setButtonText (theme::txt ("Parar gravação"));
    else
    {
        const int countIn = engine.getRecordingCountIn();
        recordButton.setButtonText (countIn > 0 ? "Cancelar REC" : "Gravar voz");
        countInLabel.setText (countIn > 0 ? juce::String (countIn) : juce::String(),
                              juce::dontSendNotification);
    }

    if (state.isRecording())
        countInLabel.setText (juce::String(), juce::dontSendNotification);
    repaint();
}
} // namespace mf
