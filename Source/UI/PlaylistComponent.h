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

    /** Posição do playhead em compassos, 0 <= bars < kPlaylistBars (fracionária). Barato:
     *  só repinta o overlay quando o pixel muda. Chamado pela message thread (timer da UI). */
    void setPlayheadBars (float bars);

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

    /** Linha fina e transparente ao mouse por cima da régua + lanes. */
    class PlayheadOverlay final : public juce::Component
    {
    public:
        PlayheadOverlay() { setInterceptsMouseClicks (false, false); }
        void setLineX (int newX);
        void paint (juce::Graphics&) override;
    private:
        int lineX { -1 };
    };

    [[nodiscard]] int laneRowWidth() const;      // largura das linhas (== régua)
    void updatePlayheadX();
    void refreshRowForTrackNode (const juce::ValueTree& trackNode);

    void rebuildRows();
    void layoutRows();
    void showAddTrackMenu();

    ProjectState& state;
    juce::TextButton addTrackButton;
    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<TrackRowComponent> rows;
    PlayheadOverlay playheadOverlay;
    float playheadBars { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};
} // namespace mf
