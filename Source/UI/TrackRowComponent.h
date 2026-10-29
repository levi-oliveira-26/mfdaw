#pragma once

#include <functional>
#include <vector>
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
 * Lane (Slice 02): clipes de compassos inteiros. Duplo clique no vazio cria; arrastar
 * o corpo move; arrastar a borda direita redimensiona; botão direito remove. Durante
 * o arrasto só existe uma pré-visualização local — o ProjectState é gravado UMA vez
 * no mouseUp (um passo de undo). Colisões e limites são decididos pelo domínio
 * (ProjectState::canPlaceClip), nunca pela UI.
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

    /** Largura de 1 compasso: a lane (kPlaylistBars compassos) é ajustada à largura disponível. */
    [[nodiscard]] static float barWidthFor (int totalRowWidth) noexcept
    {
        return juce::jmax (1.0f, static_cast<float> (totalRowWidth - kHeaderWidth) / static_cast<float> (kPlaylistBars));
    }

    TrackRowComponent (ProjectState& stateToUse, const TrackModel& model);
    ~TrackRowComponent() override = default;

    [[nodiscard]] int getTrackId() const noexcept { return trackId; }

    /** Relê nome, fader e clipes do ProjectState sem disparar callbacks. */
    void refreshFromState();

    /** Chamado quando o botão de ação é clicado: (ação do domínio, trackId). */
    std::function<void (TrackAction, int)> onAction;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class DragMode { None, Move, ResizeRight };
    static constexpr int kResizeHandle = 10;   // px da borda direita que redimensionam

    [[nodiscard]] float barWidth() const noexcept { return barWidthFor (getWidth()); }
    [[nodiscard]] int clipIndexAtX (int x) const noexcept;               // -1 se vazio / fora da lane
    [[nodiscard]] juce::Rectangle<float> clipBounds (const ClipData& c) const noexcept;
    void showClipMenu (int clipId);

    ProjectState& state;
    const int trackId;
    const TrackType type;
    const TrackAction action;

    std::vector<ClipData> clips;     // cópia local; relida em refreshFromState()
    DragMode dragMode { DragMode::None };
    ClipData dragOriginal;
    ClipData dragPreview;

    juce::Label nameLabel;
    juce::Slider fader;
    juce::TextButton actionButton;
    juce::TextButton removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackRowComponent)
};
} // namespace mf
