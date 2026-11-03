#pragma once

#include <cmath>

#include "Core/ClipModel.h"

namespace mf
{
// ---------------------------------------------------------------------------
// Takes de voz (Vertical Slice 06) — vocabulário puro, sem JUCE.
//
// Cada gravação numa faixa de Voz vira um TAKE (arquivo de áudio no disco). A
// posição de início é medida em PASSOS desde o início da Playlist
// (compasso * 16 + passo), porque a gravação começa numa batida, não
// necessariamente no início de um compasso.
// ---------------------------------------------------------------------------
inline constexpr int kMaxTakesPerTrack  = 32;
inline constexpr int kStepsPerBar       = 16;
inline constexpr int kPlaylistSteps     = kPlaylistBars * kStepsPerBar;
inline constexpr double kMaxTakeSeconds = 3600.0;

namespace take
{
[[nodiscard]] constexpr bool isValidStartStep (int startStep) noexcept
{
    return startStep >= 0 && startStep < kPlaylistSteps;
}

[[nodiscard]] inline bool isValidDuration (double seconds) noexcept
{
    return std::isfinite (seconds) && seconds > 0.0 && seconds <= kMaxTakeSeconds;
}

/** Compasso (0-based) onde o take começa. */
[[nodiscard]] constexpr int startBarOf (int startStep) noexcept { return startStep / kStepsPerBar; }

/**
 * Quantos compassos (contando o compasso do início, mesmo que parcial) o take cobre.
 * 1 compasso = 4 batidas = 240 / bpm segundos. Sempre >= 1.
 */
[[nodiscard]] inline int barsCovered (int startStep, double durationSeconds, int bpm) noexcept
{
    const double secondsPerBar = 240.0 / static_cast<double> (bpm < 1 ? 1 : bpm);
    const double startFraction = static_cast<double> (startStep % kStepsPerBar) / kStepsPerBar;
    const double bars = std::ceil (startFraction + durationSeconds / secondsPerBar - 1.0e-9);
    return bars < 1.0 ? 1 : (bars > kPlaylistBars ? kPlaylistBars : static_cast<int> (bars));
}
} // namespace take

} // namespace mf
