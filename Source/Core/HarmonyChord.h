#pragma once

#include "Core/ClipModel.h"

namespace mf
{
// ---------------------------------------------------------------------------
// Harmonia por faixa (Vertical Slice 05) — vocabulário puro, sem JUCE.
//
// Cada HarmonyTrack tem a progressão de 4 acordes dos Smart Chords (cartões
// 1..4). O cartão (compasso % 4) soa durante o compasso inteiro, repetindo a
// cada 4 compassos, mas só se estiver "ligado" na faixa e o compasso estiver
// coberto por um clipe da faixa. Os acordes derivam da tonalidade do projeto
// (mudar o tom transpõe a progressão).
// ---------------------------------------------------------------------------
inline constexpr int kHarmonyCards       = 4;
inline constexpr int kHarmonyAllCardsMask = (1 << kHarmonyCards) - 1;   // padrão: todos ligados

namespace harmony
{
/** Cartão (0..3) que soa no compasso (0-based). */
[[nodiscard]] constexpr int cardForBar (int bar) noexcept
{
    return bar < 0 ? 0 : bar % kHarmonyCards;
}

[[nodiscard]] constexpr bool isValidCard (int card) noexcept
{
    return card >= 0 && card < kHarmonyCards;
}

/** Empacota 3 notas MIDI (0..127) em um inteiro de 24 bits (para o EngineEvent). */
[[nodiscard]] constexpr int packNotes (int n0, int n1, int n2) noexcept
{
    return (n0 & 0xFF) | ((n1 & 0xFF) << 8) | ((n2 & 0xFF) << 16);
}

[[nodiscard]] constexpr int unpackNote (int packed, int index) noexcept
{
    return (packed >> (8 * index)) & 0xFF;
}
} // namespace harmony

} // namespace mf
