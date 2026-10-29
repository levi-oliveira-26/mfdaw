#pragma once

namespace mf
{
// ---------------------------------------------------------------------------
// Notas da faixa de Melodia (Vertical Slice 04) — vocabulário puro, sem JUCE.
//
// Cada MelodyTrack guarda UM padrão de 1 compasso (16 passos), como a Bateria:
// o padrão se repete em todos os compassos cobertos pelos clipes da faixa.
// Uma nota dura 1 passo (1/16).
// ---------------------------------------------------------------------------
inline constexpr int kMelodySteps        = 16;
inline constexpr int kMelodyLowestNote   = 24;    // C1  (limite inferior do Piano Roll)
inline constexpr int kMelodyHighestNote  = 107;   // B7  (limite superior do Piano Roll)
inline constexpr int kMaxMelodyNotesPerTrack = 128;

struct MelodyNote
{
    int step  { 0 };
    int pitch { 60 };
};

namespace melody
{
[[nodiscard]] constexpr bool isValidNote (int step, int pitch) noexcept
{
    return step >= 0 && step < kMelodySteps && pitch >= kMelodyLowestNote && pitch <= kMelodyHighestNote;
}
} // namespace melody

} // namespace mf
