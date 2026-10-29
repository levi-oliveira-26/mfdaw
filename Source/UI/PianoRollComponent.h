#pragma once

#include <array>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ProjectState.h"
#include "Core/ScaleLock.h"
#include "Engine/MainProcessor.h"

namespace mf
{
/**
 * Piano Roll guiado com Scale Lock e rolagem vertical de várias oitavas.
 *
 * Cobre C1..B7 (MIDI 24..107). As linhas fora do tom do projeto continuam
 * existindo, mas ficam escurecidas e rejeitam cliques (ScaleLock::isNoteInScale).
 * Rolagem: roda do mouse / trackpad. O rótulo de cada linha liberada é o nome
 * da nota com oitava ("C4"), não o número MIDI.
 */
class PianoRollComponent final : public juce::Component,
                                 private juce::ValueTree::Listener
{
public:
    explicit PianoRollComponent (ProjectState& projectState, MainProcessor& processor);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    /** Faixa de Melodia editada (-1 = nenhuma: a grade fica vazia e não aceita cliques).
     *  Recarrega as notas do ProjectState; nunca guarda ponteiro para o modelo. */
    void setTrackId (int newTrackId);

    /** Nome da nota com oitava; MIDI 60 = "C4". */
    [[nodiscard]] static juce::String noteName (int midiNote);

private:
    static constexpr int kLowestNote  = kMelodyLowestNote;    // C1
    static constexpr int kHighestNote = kMelodyHighestNote;   // B7
    static constexpr int numRows  = kHighestNote - kLowestNote + 1;
    static constexpr int numSteps = kMelodySteps;
    static constexpr int kRowHeight = 24;

    /** Nota MIDI sob o ponto (ou -1 se fora da grade/da escala). `row`/`step` são -1 fora da grade. */
    [[nodiscard]] int noteAt (const juce::Point<int>& position, int* row = nullptr, int* step = nullptr) const;
    int toggleNote (const juce::Point<int>& position);
    void clampScroll();
    /** Relê as notas da faixa (cache da UI) a partir do ProjectState. */
    void reloadNotes();
    /** Liga/desliga a nota no domínio; devolve true se a mudança foi aceita. */
    bool commitNote (int row, int step, bool on);
    void scrollToNote (int midiNote);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    ProjectState& state;
    MainProcessor& processor;
    int trackId { -1 };
    std::array<std::array<bool, numSteps>, numRows> notes {};   // cache; linha 0 = nota mais aguda
    float scrollY { 0.0f };                                       // deslocamento vertical em pixels
    bool initialScrollDone { false };
    int previewNote { -1 };
    int previewRow { -1 };
    int previewStep { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
} // namespace mf
