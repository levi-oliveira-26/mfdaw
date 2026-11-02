#pragma once

#include <array>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ProjectState.h"
#include "Engine/MainProcessor.h"

namespace mf
{
class SmartChordsComponent final : public juce::Component,
                                   private juce::ValueTree::Listener
{
public:
    explicit SmartChordsComponent (ProjectState& projectState, MainProcessor& processor);
    ~SmartChordsComponent() override;

    /** Faixa de Harmonia editada (-1 = nenhuma: os cartões ficam apagados e não podem ser ligados).
     *  Guarda só o id; o estado "ligado" é relido do ProjectState. */
    void setTrackId (int newTrackId);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void refreshChords();
    /** Relê quais cartões estão ligados na faixa e atualiza as cores. */
    void refreshArmed();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    ProjectState& state;
    MainProcessor& processor;
    std::array<juce::Label, 4> chordLabels;
    int pressedChord { -1 };
    int trackId { -1 };
    int armedMask { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartChordsComponent)
};
} // namespace mf
