#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ProjectState.h"

namespace mf
{
/**
 * Grade vetorial 4 x 16 do Passo 1 — Ritmo (Card 1.1).
 *
 * Clique em uma célula alterna o passo via ProjectState::toggleStep(); a
 * própria grade escuta o ValueTree para se redesenhar. A UI nunca fala com a
 * audio thread diretamente: o MainProcessor traduz a mudança de estado em um
 * EngineEvent na fila lock-free.
 */
class StepGridComponent final : public juce::Component,
                                private juce::ValueTree::Listener
{
public:
    explicit StepGridComponent (ProjectState& stateToUse);
    ~StepGridComponent() override;

    /** Passo em execução (0..15) ou -1. Chamado pelo Timer do editor. */
    void setPlayheadStep (int step);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    [[nodiscard]] juce::Rectangle<float> getGridBounds() const;
    [[nodiscard]] juce::Rectangle<float> getCellBounds (int track, int step) const;

    static constexpr float kLabelWidth = 96.0f;

    ProjectState& state;
    int playheadStep { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGridComponent)
};

} // namespace mf
