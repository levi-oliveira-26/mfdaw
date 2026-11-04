#include "UI/SampleEditorComponentView.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "UI/Theme.h"

namespace mf
{
namespace
{
void styleHint (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (theme::makeFont (13.0f));
    label.setColour (juce::Label::textColourId, theme::col (theme::kTextMuted));
    label.setJustificationType (juce::Justification::centredLeft);
}
} // namespace

SampleEditorComponentView::SampleEditorComponentView (MainProcessor& processor)
    : state (processor.getProjectState())
{
    using theme::txt;

    importButton.setButtonText (txt ("Importar áudio"));
    importButton.setColour (juce::TextButton::buttonColourId, juce::Colour (theme::kBrandSample));
    importButton.setColour (juce::TextButton::textColourOffId, theme::col (theme::kSurfaceBase));
    importButton.onClick = [this] { importSample(); };
    addAndMakeVisible (importButton);

    styleHint (statusLabel, txt ("Traga um som seu (WAV, MP3, AIFF ou FLAC) para a faixa."));
    addAndMakeVisible (statusLabel);

    trimToggle.setButtonText (txt ("Cortar silêncio"));
    normalizeToggle.setButtonText (txt ("Normalizar volume"));
    trimToggle.onClick = [this] { commitEdit(); };
    normalizeToggle.onClick = [this] { commitEdit(); };
    addAndMakeVisible (trimToggle);
    addAndMakeVisible (normalizeToggle);

    styleHint (trimHint, txt ("Tira o silêncio do começo e do fim do som."));
    styleHint (normalizeHint, txt ("Deixa o volume o mais alto possível, sem estourar."));
    addAndMakeVisible (trimHint);
    addAndMakeVisible (normalizeHint);

    for (auto* slider : { &fadeInSlider, &fadeOutSlider })
    {
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        slider->setRange (0.0, static_cast<double> (SampleEdit::kMaxFadeMs), 10.0);
        slider->setTextValueSuffix (" ms");
        slider->onValueChange = [this, slider]
        {
            if (! syncingControls && ! slider->isMouseButtonDown())
                commitEdit();   // teclado / roda do mouse; no arrasto só confirma ao soltar
        };
        slider->onDragEnd = [this] { commitEdit(); };
        addAndMakeVisible (slider);
    }

    styleHint (fadeInLabel, txt ("Entrada suave (o som sobe devagar)"));
    styleHint (fadeOutLabel, txt ("Saída suave (o som some devagar)"));
    addAndMakeVisible (fadeInLabel);
    addAndMakeVisible (fadeOutLabel);

    startTimerHz (10);
}

SampleEditorComponentView::~SampleEditorComponentView()
{
    stopTimer();
}

void SampleEditorComponentView::setTrackId (int newTrackId)
{
    if (trackId == newTrackId)
        return;

    trackId = newTrackId;
    shownEditId = -1;
    timerCallback();
}

void SampleEditorComponentView::setSelectedClip (int newTrackId, int clipId)
{
    selectedClipTrackId = newTrackId;
    selectedClipId = clipId;
    selectedClipSampleId = -1;

    if (selectedClipTrackId >= 0 && selectedClipId >= 0)
        for (const auto& clip : state.getClips (selectedClipTrackId))
            if (clip.id == selectedClipId)
            {
                selectedClipSampleId = clip.takeId;
                break;
            }

    repaint();
}

void SampleEditorComponentView::importSample()
{
    if (trackId < 0)
        return;

    chooser = std::make_unique<juce::FileChooser> (theme::txt ("Escolha um áudio"), juce::File {},
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, targetTrack = trackId] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;

                              juce::AudioFormatManager formats;
                              formats.registerBasicFormats();
                              const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

                              if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
                              {
                                  statusLabel.setText (theme::txt ("Não consegui abrir esse arquivo. Tente WAV ou MP3."),
                                                       juce::dontSendNotification);
                                  return;
                              }

                              const double seconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
                              const int id = state.addSample (targetTrack, file.getFullPathName(), seconds);

                              statusLabel.setText (id > 0 ? theme::txt ("Áudio adicionado: ") + file.getFileName()
                                                          : theme::txt ("Não deu para adicionar (áudio muito longo ou limite de 32 atingido)."),
                                                   juce::dontSendNotification);
                          });
}

SampleEdit SampleEditorComponentView::editFromControls() const
{
    return SampleEdit { trimToggle.getToggleState(), normalizeToggle.getToggleState(),
                        static_cast<int> (fadeInSlider.getValue()),
                        static_cast<int> (fadeOutSlider.getValue()) }.clamped();
}

void SampleEditorComponentView::commitEdit()
{
    if (syncingControls || trackId < 0 || activeId < 0)
        return;

    state.setSampleEdit (trackId, activeId, editFromControls());
}

void SampleEditorComponentView::syncControlsFromState()
{
    const bool hasActive = activeId >= 0;
    for (auto* c : std::initializer_list<juce::Component*> { &trimToggle, &normalizeToggle, &fadeInSlider, &fadeOutSlider })
        c->setEnabled (hasActive);

    const auto edit = hasActive ? state.getSampleEdit (trackId, activeId) : SampleEdit {};
    if (shownEditId == activeId && shownEdit == edit)
        return;

    shownEditId = activeId;
    shownEdit = edit;

    const juce::ScopedValueSetter<bool> guard (syncingControls, true);
    trimToggle.setToggleState (edit.trimSilence, juce::dontSendNotification);
    normalizeToggle.setToggleState (edit.normalize, juce::dontSendNotification);
    fadeInSlider.setValue (edit.fadeInMs, juce::dontSendNotification);
    fadeOutSlider.setValue (edit.fadeOutMs, juce::dontSendNotification);
}

void SampleEditorComponentView::timerCallback()
{
    auto latest = trackId >= 0 ? state.getTakes (trackId) : std::vector<TakeData> {};
    std::reverse (latest.begin(), latest.end());
    samples = std::move (latest);
    activeId = trackId >= 0 ? state.getActiveTakeId (trackId) : -1;
    syncControlsFromState();
    repaint();
}

int SampleEditorComponentView::rowAt (juce::Point<int> position) const
{
    if (! listArea.contains (position))
        return -1;

    const int row = (position.y - listArea.getY() - kRowHeight) / kRowHeight;   // 1ª linha = título
    return position.y - listArea.getY() >= kRowHeight && row >= 0 && row < static_cast<int> (samples.size()) ? row : -1;
}

void SampleEditorComponentView::mouseDown (const juce::MouseEvent& event)
{
    const int row = rowAt (event.getPosition());
    if (row < 0 || trackId < 0)
        return;

    const int id = samples[static_cast<std::size_t> (row)].id;
    state.setActiveTake (trackId, id);   // este é o áudio que os controles abaixo editam

    if (selectedClipTrackId == trackId && selectedClipId >= 0 && state.setClipTake (trackId, selectedClipId, id))
        selectedClipSampleId = id;

    timerCallback();
}

void SampleEditorComponentView::paint (juce::Graphics& g)
{
    using namespace theme;

    g.fillAll (col (kSurfaceBase));
    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (20.0f, true));
    g.drawText (txt ("Editor de amostras"), getLocalBounds().removeFromTop (44), juce::Justification::centredLeft, false);

    if (listArea.getHeight() < kRowHeight)
        return;

    auto list = listArea;
    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (15.0f, true));
    g.drawText (samples.empty() ? txt ("Nenhum áudio ainda. Toque em “Importar áudio”.")
                                : txt ("Seus áudios (toque para editar; com um clipe selecionado, ele passa a tocar este áudio)"),
                list.removeFromTop (kRowHeight), juce::Justification::centredLeft, false);

    const int maxRows = list.getHeight() / kRowHeight;
    for (int i = 0; i < static_cast<int> (samples.size()) && i < maxRows; ++i)
    {
        const auto& s = samples[static_cast<std::size_t> (i)];
        auto row = list.removeFromTop (kRowHeight).reduced (0, 2);
        const bool active = s.id == activeId;
        const bool onSelectedClip = s.id == selectedClipSampleId;

        g.setColour (active ? juce::Colour (kBrandSample).withAlpha (0.28f) : col (kSurfaceBase).brighter (0.05f));
        g.fillRoundedRectangle (row.toFloat(), 4.0f);
        g.setColour (active ? col (kTextPrimary) : col (kTextMuted));
        g.setFont (makeFont (14.0f, active));
        const auto dot = txt ("  ·  ");
        g.drawText (juce::File (s.filePath).getFileName() + dot + juce::String (s.durationSeconds, 1) + " s"
                        + (s.edit.isDefault() ? juce::String() : dot + txt ("editado"))
                        + (onSelectedClip ? dot + txt ("no clipe selecionado") : juce::String()),
                    row.reduced (10, 0), juce::Justification::centredLeft, true);
    }
}

void SampleEditorComponentView::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (44);

    auto top = area.removeFromTop (40);
    importButton.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (12);
    statusLabel.setBounds (top);
    area.removeFromTop (12);

    // Controles ficam embaixo; a lista usa o espaço que sobra.
    auto controls = area.removeFromBottom (4 * 34 + 2 * 34);
    listArea = area.reduced (0, 4);

    auto row = controls.removeFromTop (34);
    trimToggle.setBounds (row.removeFromLeft (210));
    trimHint.setBounds (row);
    row = controls.removeFromTop (34);
    normalizeToggle.setBounds (row.removeFromLeft (210));
    normalizeHint.setBounds (row);

    fadeInLabel.setBounds (controls.removeFromTop (34));
    fadeInSlider.setBounds (controls.removeFromTop (34));
    fadeOutLabel.setBounds (controls.removeFromTop (34));
    fadeOutSlider.setBounds (controls.removeFromTop (34));
}
} // namespace mf
