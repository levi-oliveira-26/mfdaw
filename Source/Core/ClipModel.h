#pragma once

namespace mf
{
// ---------------------------------------------------------------------------
// Clipes da Playlist (Vertical Slice 02) — vocabulário puro, sem JUCE.
//
// Um clipe ocupa um intervalo de compassos INTEIROS [startBar, startBar+lengthBars)
// dentro de UMA faixa. Clipes da mesma faixa nunca se sobrepõem.
// ---------------------------------------------------------------------------

/** Tamanho da Playlist no Slice 02: 16 compassos (a lane é ajustada à largura, sem rolagem horizontal). */
inline constexpr int kPlaylistBars   = 16;
inline constexpr int kMinClipBars    = 1;
inline constexpr int kDefaultClipBars = 2;

/** Cópia imutável de um clipe (valor leve, seguro para guardar na UI). */
struct ClipData
{
    int id         { -1 };
    int startBar   { 0 };
    int lengthBars { 0 };

    [[nodiscard]] constexpr bool isValid() const noexcept { return id > 0; }
    [[nodiscard]] constexpr int  endBar() const noexcept  { return startBar + lengthBars; }
};

namespace clip
{
[[nodiscard]] constexpr bool isValidRange (int startBar, int lengthBars) noexcept
{
    return startBar >= 0 && lengthBars >= kMinClipBars && startBar + lengthBars <= kPlaylistBars;
}

[[nodiscard]] constexpr bool overlaps (int startA, int lengthA, int startB, int lengthB) noexcept
{
    return startA < startB + lengthB && startB < startA + lengthA;
}

/** Leva o início para dentro do projeto para um clipe de `lengthBars` caber. */
[[nodiscard]] constexpr int clampStart (int startBar, int lengthBars) noexcept
{
    const int latest = kPlaylistBars - (lengthBars < kMinClipBars ? kMinClipBars : lengthBars);
    return startBar < 0 ? 0 : (startBar > latest ? latest : startBar);
}

/** Máscara de bits dos compassos [startBar, startBar+lengthBars); 0 se o intervalo é inválido. */
[[nodiscard]] constexpr int barMaskFor (int startBar, int lengthBars) noexcept
{
    return isValidRange (startBar, lengthBars) ? (((1 << lengthBars) - 1) << startBar) : 0;
}

/** Compasso seguinte, com loop ao fim da Playlist. */
[[nodiscard]] constexpr int nextBar (int bar) noexcept
{
    return (bar + 1) % kPlaylistBars;
}

/**
 * A bateria está audível neste compasso?
 *   barMask == 0  -> "modo rascunho": ainda não há clipe de bateria, o padrão toca sempre
 *                    (editar o ritmo antes de arranjar continua dando retorno sonoro);
 *   barMask != 0  -> arranjo: só soa dentro de clipes de faixas de bateria.
 * Função pura e sem alocação: é chamada pela audio thread.
 */
[[nodiscard]] constexpr bool isBarAudible (int barMask, int bar) noexcept
{
    return barMask == 0 || (bar >= 0 && bar < kPlaylistBars && ((barMask >> bar) & 1) != 0);
}
} // namespace clip

} // namespace mf
