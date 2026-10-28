#include "UI/MainEditor.h"

namespace mf
{
namespace
{
using namespace theme;

constexpr const char* kStepButtonTexts[kNumWizardSteps] = {
    "1  Ritmo", "2  Harmonia", "3  Melodia", "4  Voz", "5  Mixagem"
};

constexpr const char* kStepTitles[kNumWizardSteps] = {
    "Passo 1 - Ritmo", "Passo 2 - Harmonia", "Passo 3 - Melodia", "Passo 4 - Voz", "Passo 5 - Mixagem Básica"
};

constexpr const char* kStepDescriptions[kNumWizardSteps] = {
    "",
    "Escolha o tom e o estilo e o Smart Chords gera a progressão de acordes, com pré-escuta ao clicar "
    "em cada bloco. (Card 2.1 - em breve)",
    "Piano Roll guiado com Scale Lock: só as notas do tom escolhido no Passo 2 ficam disponíveis. "
    "(Card 3.1 - em breve)",
    "Grave sua voz com um clique e veja a forma de onda na tela. (Card 4.1 - em breve)",
    "Console com 4 faders + Master e os knobs Espaço (reverb) e Brilho (EQ 3 bandas). (Card 5.1 - em breve)"
};

constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

} // namespace

// ===========================================================================
MainEditor::MainEditor (MainProcessor& processorToEdit)
    : juce::AudioProcessorEditor (processorToEdit),
      engine (processorToEdit),
      state (processorToEdit.getProjectState()),
      stepGrid (processorToEdit.getProjectState(), processorToEdit.getSampleManager(),
                processorToEdit.getSampleRate()),
      smartChords (processorToEdit.getProjectState(), processorToEdit),
      pianoRoll (processorToEdit.getProjectState(), processorToEdit)
{
    setLookAndFeel (&laf);

    // ----- Transporte -----
    addKeyListener (this);

    brandLabel.setText ("M.F. DAW", juce::dontSendNotification);
    brandLabel.setFont (makeFont (22.0f, true));
    addAndMakeVisible (brandLabel);

    playButton.setComponentID ("transport-play");
    playButton.onClick = [this] { state.setPlaying (true); };
    addAndMakeVisible (playButton);

    pauseButton.setComponentID ("transport-pause");
    pauseButton.onClick = [this] { state.setPlaying (false); };
    addAndMakeVisible (pauseButton);

    stopButton.setComponentID ("transport-stop");
    stopButton.onClick = [this]
    {
        engine.requestStop();
        state.setPlaying (false);
    };
    addAndMakeVisible (stopButton);

    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setRange (kMinBpm, kMaxBpm, 1.0);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 96, kRowH);
    bpmSlider.onValueChange = [this] { state.setBpm (static_cast<int> (bpmSlider.getValue())); };
    addAndMakeVisible (bpmSlider);

    for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
        for (int root = 0; root < 12; ++root)
            keyBox.addItem (juce::String (kNoteNames[root]) + (scaleIndex == 0 ? " Maior" : " Menor"),
                            1 + root + 12 * scaleIndex);

    keyBox.onChange = [this]
    {
        const int id = keyBox.getSelectedId();

        if (id > 0)
            state.setKey ((id - 1) % 12, (id - 1) >= 12 ? Scale::Minor : Scale::Major);
    };
    addAndMakeVisible (keyBox);

    modeButton.onClick = [this] { state.setFreeMode (! state.isFreeMode()); };
    addAndMakeVisible (modeButton);

    // ----- Navegação do Wizard -----
    for (int i = 0; i < kNumWizardSteps; ++i)
    {
        auto& button = stepButtons[static_cast<std::size_t> (i)];
        button.setButtonText (txt (kStepButtonTexts[i]));
        button.setColour (juce::TextButton::buttonOnColourId, stepColour (i + 1));
        button.setColour (juce::TextButton::textColourOffId, col (kTextMuted));
        button.onClick = [this, i] { state.setCurrentStep (i + 1); };
        addAndMakeVisible (button);
    }

    backButton.setButtonText (txt ("Voltar"));
    backButton.onClick = [this] { state.setCurrentStep (state.getCurrentStep() - 1); };
    addAndMakeVisible (backButton);

    nextButton.setButtonText (txt ("Avançar"));
    nextButton.onClick = [this] { state.setCurrentStep (state.getCurrentStep() + 1); };
    addAndMakeVisible (nextButton);

    // ----- Conteúdo -----
    addChildComponent (stepGrid);
    addChildComponent (smartChords);
    addChildComponent (pianoRoll);
    addChildComponent (placeholder);

    // ----- Reatividade -----
    state.getTree().addListener (this);
    state.getSession().addListener (this);
    refreshFromState();
    startTimerHz (30); // atualiza o playhead (lido de um std::atomic<int>)

    setResizable (true, true);
    setResizeLimits (800, 520, 1600, 1000);
    setSize (960, 600);
}

MainEditor::~MainEditor()
{
    removeKeyListener (this);
    stopTimer();
    state.getTree().removeListener (this);
    state.getSession().removeListener (this);
    setLookAndFeel (nullptr);
}

// ===========================================================================
// Layout e pintura
// ===========================================================================
void MainEditor::paint (juce::Graphics& g)
{
    g.fillAll (col (kBgDeep));

    g.setColour (col (kSurfaceBase));
    g.fillRoundedRectangle (contentArea.toFloat(), 8.0f);

    g.setColour (col (kBorderSubtle));
    g.drawRoundedRectangle (contentArea.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void MainEditor::resized()
{
    auto area = getLocalBounds().reduced (kPad);

    // --- Barra de transporte ---
    auto transport = area.removeFromTop (kRowH);
    brandLabel.setBounds (transport.removeFromLeft (128));
    transport.removeFromLeft (kPad);
    playButton.setBounds (transport.removeFromLeft (72));
    transport.removeFromLeft (kPad / 2);
    pauseButton.setBounds (transport.removeFromLeft (72));
    transport.removeFromLeft (kPad / 2);
    stopButton.setBounds (transport.removeFromLeft (72));
    transport.removeFromLeft (kPad);
    bpmSlider.setBounds (transport.removeFromLeft (232));
    transport.removeFromLeft (kPad);
    keyBox.setBounds (transport.removeFromLeft (152));
    modeButton.setBounds (transport.removeFromRight (176));

    area.removeFromTop (kPad);

    // --- Navegação dos 5 passos ---
    auto nav = area.removeFromTop (kRowH);
    const int gap = kGrid;
    const int buttonW = (nav.getWidth() - gap * (kNumWizardSteps - 1)) / kNumWizardSteps;

    for (auto& button : stepButtons)
    {
        button.setBounds (nav.removeFromLeft (buttonW));
        nav.removeFromLeft (gap);
    }

    area.removeFromTop (kPad);

    // --- Rodapé (Voltar / Avançar) ---
    auto footer = area.removeFromBottom (kRowH);
    backButton.setBounds (footer.removeFromLeft (136));
    nextButton.setBounds (footer.removeFromRight (136));

    area.removeFromBottom (kPad);

    // --- Conteúdo ---
    contentArea = area;
    const auto inner = contentArea.reduced (kPad);
    stepGrid.setBounds (inner);
    smartChords.setBounds (inner);
    pianoRoll.setBounds (inner);
    placeholder.setBounds (inner);
}

// ===========================================================================
// Reatividade
// ===========================================================================
void MainEditor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) { refreshFromState(); }
void MainEditor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)             { refreshFromState(); }
void MainEditor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)      { refreshFromState(); }
void MainEditor::valueTreeRedirected (juce::ValueTree&)                               { refreshFromState(); }

void MainEditor::timerCallback()
{
    stepGrid.setPlayheadStep (engine.getPlayheadStep());
}

bool MainEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (! key.isKeyCode (juce::KeyPress::spaceKey))
        return false;

    state.setPlaying (! state.isPlaying());
    return true;
}

void MainEditor::refreshFromState()
{
    const int  step     = state.getCurrentStep();
    const bool freeMode = state.isFreeMode();
    const bool playing  = state.isPlaying();

    // Transporte
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setColour (juce::TextButton::buttonOnColourId, col (kActionRecord));
    pauseButton.setEnabled (playing);
    stopButton.setEnabled (playing);

    bpmSlider.setValue (state.getBpm(), juce::dontSendNotification);

    keyBox.setSelectedId (1 + state.getKeyRoot() + (state.getScale() == Scale::Minor ? 12 : 0),
                          juce::dontSendNotification);

    modeButton.setButtonText (txt (freeMode ? "Modo Assistente" : "Modo Livre"));

    // Navegação (desabilitada no Modo Livre)
    for (int i = 0; i < kNumWizardSteps; ++i)
    {
        auto& button = stepButtons[static_cast<std::size_t> (i)];
        button.setEnabled (! freeMode);
        button.setToggleState (! freeMode && (i + 1) == step, juce::dontSendNotification);
    }

    backButton.setEnabled (! freeMode && step > 1);
    nextButton.setEnabled (! freeMode && step < kNumWizardSteps);

    // Conteúdo
    const bool showGrid = ! freeMode && step == 1;
    const bool showSmartChords = ! freeMode && step == 2;
    const bool showPianoRoll = ! freeMode && step == 3;
    stepGrid.setVisible (showGrid);
    smartChords.setVisible (showSmartChords);
    pianoRoll.setVisible (showPianoRoll);
    placeholder.setVisible (! showGrid && ! showSmartChords && ! showPianoRoll);

    if (freeMode)
        placeholder.setContent (txt ("Modo Livre - Timeline"),
                                txt ("Visualização de arranjo em pistas paralelas (Card 6.1 - em breve). "
                                     "Seu projeto continua intacto: volte ao Modo Assistente quando quiser."),
                                col (kTextPrimary));
    else if (! showGrid && ! showSmartChords && ! showPianoRoll)
        placeholder.setContent (txt (kStepTitles[step - 1]), txt (kStepDescriptions[step - 1]), stepColour (step));

    repaint();
}

} // namespace mf
