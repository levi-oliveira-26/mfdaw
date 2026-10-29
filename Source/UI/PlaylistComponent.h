#pragma once

#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ProjectState.h"
#include "UI/TrackRowComponent.h"

namespace mf
{
/**
 * PlaylistComponent — Arrangement View (a "tela" do projeto).
 *
 *   [ Playlist                                   [ + Adicionar faixa ] ]
 *   [ Faixas | 1 | 2 | 3 | ...   (régua de compassos)                  ]
 *   [ cabeçalho  | lane      ]  <- uma TrackRowComponent por faixa      ]
 *   [ ...                                                               ]
 *
 * Nasce VAZIA. Reativa: escuta o ValueTree do ProjectState e reconstrói as
 * linhas quando a lista de faixas muda (de forma assíncrona, para nunca
 * destruir uma linha dentro do próprio callback de clique). Mudanças de nome /
 * fader apenas atualizam a linha afetada. A Playlist nunca fala com a engine:
 * só grava no ProjectState.
 */
class PlaylistComponent final : public juce::Component,
                                private juce::ValueTree::Listener,
                                private juce::AsyncUpdater
{
public:
    explicit PlaylistComponent (ProjectState& stateToUse);
    ~PlaylistComponent() override;

    /** Botão de ação de alguma faixa foi acionado: (ação do domínio, trackId). */
    std::function<void (TrackAction, int)> onTrackAction;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kToolbarHeight = 48;
    static constexpr int kRulerHeight   = 32;

    void handleAsyncUpdate() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;

    void rebuildRows();
    void layoutRows();
    void showAddTrackMenu();

    ProjectState& state;
    juce::TextButton addTrackButton;
    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<TrackRowComponent> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};
} // namespace mf
