#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Core/ProjectState.h"

namespace mf
{
/**
 * Uma linha da Playlist: cabeçalho da faixa (esquerda) + lane de clipes (direita).
 *
 * Cabeçalho (Progressive Disclosure — só o essencial):
 *   [ nome editável (duplo clique) ............ [x] ]
 *   [ fader de volume pré-mix + dB ... [ação]       ]
 *
 * A linha é uma VIEW burra: cada controle apenas grava no ProjectState (via
 * TrackModel) e a Playlist chama refreshFromState() quando o estado muda. Ela
 * guarda só o trackId; nunca ponteiros para o modelo. Nenhuma comunicação com a
 * audio thread acontece aqui — o MainProcessor traduz a mudança do ValueTree em
 * EngineEvent na fila SPSC.
 */
class TrackRowComponent final : public juce::Component
{
public:
    static constexpr int kHeight      = 112;   // 8 + 44 + 8 + 44 + 8  (alvos de toque >= 44)
    static constexpr int kHeaderWidth = 336;
    static constexpr int kBarWidth    = 128;   // largura de 1 compasso na lane (zoom fixo no slice 01)

    TrackRowComponent (ProjectState& stateToUse, const TrackModel& model);
    ~TrackRowComponent() override = default;

    [[nodiscard]] int getTrackId() const noexcept { return trackId; }

    /** Relê nome e fader do ProjectState sem disparar callbacks. */
    void refreshFromState();

    /** Chamado quando o botão de ação é clicado: (ação do domínio, trackId). */
    std::function<void (TrackAction, int)> onAction;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ProjectState& state;
    const int trackId;
    const TrackType type;
    const TrackAction action;

    juce::Label nameLabel;
    juce::Slider fader;
    juce::TextButton actionButton;
    juce::TextButton removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackRowComponent)
};
} // namespace mf
