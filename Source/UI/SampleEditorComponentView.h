#pragma once

#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Engine/MainProcessor.h"

namespace mf
{
/**
 * Editor da faixa de Amostra (Slice 09): importar áudio, escolher qual áudio editar
 * e ajustar Cortar silêncio / Normalizar / Entrada e Saída suaves.
 * As edições são não destrutivas (o arquivo original nunca muda) e guardadas no projeto.
 */
class SampleEditorComponentView final : public juce::Component,
                                        private juce::Timer
{
public:
    explicit SampleEditorComponentView (MainProcessor& processor);
    ~SampleEditorComponentView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

    /** Faixa de Amostra exibida (-1 = nenhuma). */
    void setTrackId (int newTrackId);
    /** Clipe selecionado na Playlist que receberá o áudio escolhido na lista. */
    void setSelectedClip (int selectedTrackId, int clipId);

private:
    void timerCallback() override;
    void importSample();
    void commitEdit();
    void syncControlsFromState();
    [[nodiscard]] int rowAt (juce::Point<int> position) const;
    [[nodiscard]] SampleEdit editFromControls() const;

    static constexpr int kRowHeight = 30;

    ProjectState& state;

    int trackId { -1 };
    std::vector<TakeData> samples;      // mais recente primeiro
    int activeId { -1 };
    int selectedClipTrackId { -1 };
    int selectedClipId { -1 };
    int selectedClipSampleId { -1 };
    SampleEdit shownEdit;
    int shownEditId { -1 };
    bool syncingControls { false };

    juce::TextButton importButton;
    juce::Label statusLabel;
    juce::ToggleButton trimToggle, normalizeToggle;
    juce::Slider fadeInSlider, fadeOutSlider;
    juce::Label trimHint, normalizeHint, fadeInLabel, fadeOutLabel;
    juce::Rectangle<int> listArea;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEditorComponentView)
};
} // namespace mf
