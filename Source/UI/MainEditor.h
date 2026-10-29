#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/TrackType.h"
#include "Engine/MainProcessor.h"
#include "UI/MfLookAndFeel.h"
#include "UI/PianoRollComponent.h"
#include "UI/PlaceholderPanel.h"
#include "UI/PlaylistComponent.h"
#include "UI/SmartChordsComponent.h"
#include "UI/StepGridComponent.h"
#include "UI/VoiceRecorderComponentView.h"

namespace mf
{
/**
 * MainEditor — Janela principal da M.F. DAW (Pivot: Arrangement View).
 *
 *   [ Transporte: M.F. DAW | Play | Pause | Stop | BPM | Tom             ]
 *   [ PlaylistComponent — projeto VAZIO; faixas empilhadas verticalmente  ]
 *   [ Gaveta do editor da faixa (só aparece ao acionar o botão de ação)   ]
 *
 * Substitui o antigo Wizard de abas estáticas. Os editores existentes (grade
 * de passos, Smart Chords, Piano Roll, gravador) são reaproveitados como
 * "gaveta" aberta pelo botão de ação de cada faixa. A UI é reativa: todo
 * controle só grava no ProjectState; nada fala com a audio thread.
 *
 * No Standalone a janela hospedeira (JUCE) é colocada em tela cheia na
 * primeira exibição; como plugin (VST3) o host controla a janela.
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
    void parentHierarchyChanged() override;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void timerCallback() override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    /** Relê o ProjectState e atualiza o transporte (sem disparar callbacks). */
    void refreshFromState();

    // --- Gaveta do editor da faixa ---
    void handleTrackAction (TrackAction action, int trackId);
    void closeDetailPanel();
    void updateDetailPanel();

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

    // Playlist (Arrangement View)
    PlaylistComponent playlist;

    // Gaveta: editores reaproveitados
    juce::TextButton closeDetailButton;
    StepGridComponent stepGrid;
    SmartChordsComponent smartChords;
    PianoRollComponent pianoRoll;
    VoiceRecorderComponentView voiceRecorder;
    PlaceholderPanel placeholder;

    juce::Rectangle<int> detailArea;
    bool detailOpen { false };
    int detailTrackId { -1 };
    TrackAction detailAction { TrackAction::OpenStepSequencer };

    bool fullScreenRequested { false };
    float lastPlayheadBars { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainEditor)
};

} // namespace mf
