#include "UI/MainEditor.h"

#include "UI/TrackPresentation.h"

namespace mf
{
namespace
{
using namespace theme;

constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

} // namespace

// ===========================================================================
MainEditor::MainEditor (MainProcessor& processorToEdit)
    : juce::AudioProcessorEditor (processorToEdit),
      engine (processorToEdit),
      state (processorToEdit.getProjectState()),
      playlist (processorToEdit.getProjectState()),
      stepGrid (processorToEdit.getProjectState(), processorToEdit.getSampleManager(),
                processorToEdit.getSampleRate()),
      smartChords (processorToEdit.getProjectState(), processorToEdit),
      pianoRoll (processorToEdit.getProjectState(), processorToEdit),
      voiceRecorder (processorToEdit)
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
        lastPlayheadBars = 0.0f;
        playlist.setPlayheadBars (0.0f);
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

    // ----- Playlist (única área de conteúdo ao iniciar) -----
    playlist.onTrackAction = [this] (TrackAction action, int trackId) { handleTrackAction (action, trackId); };
    addAndMakeVisible (playlist);

    // ----- Gaveta do editor da faixa (oculta até o usuário acionar uma faixa) -----
    closeDetailButton.setButtonText (txt ("Fechar editor"));
    closeDetailButton.onClick = [this] { closeDetailPanel(); };
    addChildComponent (closeDetailButton);

    addChildComponent (stepGrid);
    addChildComponent (smartChords);
    addChildComponent (pianoRoll);
    addChildComponent (voiceRecorder);
    addChildComponent (placeholder);

    // ----- Reatividade -----
    state.getTree().addListener (this);
    state.getSession().addListener (this);
    refreshFromState();
    startTimerHz (30); // atualiza o playhead (lido de um std::atomic<int>)

    setResizable (true, true);
    setResizeLimits (960, 640, 4096, 2304);
    setSize (1280, 800);
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
// Janela hospedeira: tela cheia no Standalone
// ===========================================================================
void MainEditor::parentHierarchyChanged()
{
    if (fullScreenRequested)
        return;

    // No Standalone o pai é a janela do JUCE (DocumentWindow). Como VST3 o pai é
    // a janela do host e nada acontece: quem manda no tamanho é o host.
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        fullScreenRequested = true;

        juce::Component::SafePointer<juce::DocumentWindow> safeWindow (window);
        juce::MessageManager::callAsync ([safeWindow]
        {
            if (safeWindow != nullptr)
                safeWindow->setFullScreen (true);
        });
    }
}

// ===========================================================================
// Layout e pintura
// ===========================================================================
void MainEditor::paint (juce::Graphics& g)
{
    g.fillAll (col (kBgDeep));

    if (! detailOpen)
        return;

    g.setColour (col (kSurfaceBase));
    g.fillRoundedRectangle (detailArea.toFloat(), 8.0f);
    g.setColour (col (kBorderSubtle));
    g.drawRoundedRectangle (detailArea.toFloat().reduced (0.5f), 8.0f, 1.0f);

    if (const auto track = state.getTrackById (detailTrackId))
    {
        g.setColour (presentation::colourFor (track->getType()));
        g.setFont (makeFont (18.0f, true));
        g.drawText (track->getName(), detailArea.reduced (kPad).removeFromTop (kRowH).withTrimmedRight (152),
                    juce::Justification::centredLeft, true);
    }
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

    area.removeFromTop (kPad);

    // --- Gaveta (embaixo) + Playlist (resto) ---
    if (detailOpen)
    {
        const int detailHeight = juce::jmin (area.getHeight() / 2, juce::jmax (320, area.getHeight() * 55 / 100));
        detailArea = area.removeFromBottom (detailHeight);
        area.removeFromBottom (kPad);

        auto inner = detailArea.reduced (kPad);
        auto header = inner.removeFromTop (kRowH);
        closeDetailButton.setBounds (header.removeFromRight (144));
        inner.removeFromTop (kGrid);

        stepGrid.setBounds (inner);
        smartChords.setBounds (inner);
        pianoRoll.setBounds (inner);
        voiceRecorder.setBounds (inner);
        placeholder.setBounds (inner);
    }
    else
    {
        detailArea = {};
    }

    playlist.setBounds (area);
}

// ===========================================================================
// Gaveta do editor da faixa
// ===========================================================================
void MainEditor::handleTrackAction (TrackAction action, int trackId)
{
    // Acionar a mesma faixa duas vezes fecha a gaveta.
    if (detailOpen && detailTrackId == trackId)
    {
        closeDetailPanel();
        return;
    }

    detailOpen = true;
    detailTrackId = trackId;
    detailAction = action;
    updateDetailPanel();
    resized();
    repaint();
}

void MainEditor::closeDetailPanel()
{
    detailOpen = false;
    detailTrackId = -1;
    updateDetailPanel();
    resized();
    repaint();
}

void MainEditor::updateDetailPanel()
{
    closeDetailButton.setVisible (detailOpen);
    stepGrid.setVisible (detailOpen && detailAction == TrackAction::OpenStepSequencer);
    smartChords.setVisible (detailOpen && detailAction == TrackAction::OpenSmartChords);
    pianoRoll.setVisible (detailOpen && detailAction == TrackAction::OpenPianoRoll);
    pianoRoll.setTrackId (detailOpen && detailAction == TrackAction::OpenPianoRoll ? detailTrackId : -1);
    voiceRecorder.setVisible (detailOpen && detailAction == TrackAction::ToggleRecording);

    const bool showPlaceholder = detailOpen && detailAction == TrackAction::OpenSampleEditor;
    placeholder.setVisible (showPlaceholder);

    if (showPlaceholder)
        placeholder.setContent (txt ("Editor de amostras"),
                                txt ("Corte de silêncio, Normalizar e Envelope chegam em uma próxima fatia."),
                                presentation::colourFor (TrackType::Sample));
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
    const int step = engine.getPlayheadStep();
    stepGrid.setPlayheadStep (step);

    // Playhead global: compasso (atomic escrito pela audio thread) + fração do passo.
    // Pausado (step < 0) mantém a última posição; só o Stop volta ao início.
    if (step >= 0)
    {
        const int bar = engine.getPlayheadBar();
        lastPlayheadBars = static_cast<float> (bar) + static_cast<float> (step) / static_cast<float> (kNumSteps);
        playlist.setPlayheadBars (lastPlayheadBars);
    }
}

bool MainEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        if (key.getModifiers().isShiftDown())
            state.getUndoManager().redo();
        else
            state.getUndoManager().undo();

        return true;
    }

    if (! key.isKeyCode (juce::KeyPress::spaceKey))
        return false;

    state.setPlaying (! state.isPlaying());
    return true;
}

void MainEditor::refreshFromState()
{
    const bool playing = state.isPlaying();

    // Transporte
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setColour (juce::TextButton::buttonOnColourId, col (kActionRecord));
    pauseButton.setEnabled (playing);
    stopButton.setEnabled (playing);

    bpmSlider.setValue (state.getBpm(), juce::dontSendNotification);

    keyBox.setSelectedId (1 + state.getKeyRoot() + (state.getScale() == Scale::Minor ? 12 : 0),
                          juce::dontSendNotification);

    // A faixa que alimentava a gaveta foi removida (ou o projeto foi recarregado).
    if (detailOpen && state.indexOfTrack (detailTrackId) < 0)
        closeDetailPanel();
}

} // namespace mf
