#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/MainProcessor.h"
#include "UI/MfLookAndFeel.h"
#include "UI/PlaceholderPanel.h"
#include "UI/StepGridComponent.h"
#include "UI/SmartChordsComponent.h"

namespace mf
{
/**
 * MainEditor — Janela principal da M.F. DAW.
 *
 *   [ Transporte: Play | Pause | Stop | BPM | Tom | Modo Livre/Assistente ]
 *   [ Navegação do Wizard:  1 Ritmo  2 Harmonia  3 Melodia  4 Voz  5 Mixagem ]
 *   [ Área de conteúdo do passo atual (ou Timeline no Modo Livre)           ]
 *   [ Voltar                                                       Avançar ]
 *
 * Máquina de estados basal: o passo atual (1..5) e o modo (Wizard/Livre) vivem
 * em ProjectState::session. A UI é 100% reativa: todo controle apenas grava no
 * ProjectState; refreshFromState() relê o estado e atualiza os widgets.
 */
class MainEditor final : public juce::AudioProcessorEditor,
                         private juce::ValueTree::Listener,
                         private juce::Timer,
                         private juce::KeyListener
{
public:
    explicit MainEditor (MainProcessor& processorToEdit);
    ~MainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void timerCallback() override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    /** Relê o ProjectState e atualiza todos os widgets (sem disparar callbacks). */
    void refreshFromState();

    // A ordem importa: o LookAndFeel é destruído por último.
    MfLookAndFeel laf;
    MainProcessor& engine;
    ProjectState& state;

    // Transporte
    juce::Label brandLabel;
    juce::TextButton playButton;
    juce::TextButton pauseButton;
    juce::TextButton stopButton;
    juce::Slider bpmSlider;
    juce::ComboBox keyBox;
    juce::TextButton modeButton;

    // Navegação do Wizard
    std::array<juce::TextButton, kNumWizardSteps> stepButtons;
    juce::TextButton backButton;
    juce::TextButton nextButton;

    // Conteúdo
    StepGridComponent stepGrid;
    SmartChordsComponent smartChords;
    PlaceholderPanel placeholder;
    juce::Rectangle<int> contentArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainEditor)
};

} // namespace mf
