#pragma once

#include <cmath>
#include <limits>

namespace mf::fader
{
/**
 * Escala do Fader de Volume Pré-Mix (gain staging).
 *
 * O modelo guarda a POSIÇÃO do fader (0..1) — é o que o usuário "sente".
 * A conversão para dB/ganho linear é uma função pura, sem estado, segura para
 * qualquer thread:
 *
 *   posição 0.00          -> silêncio (-inf dB)
 *   posição 0.00 .. 0.75  -> -60 dB .. 0 dB (linear em dB)
 *   posição 0.75          -> 0 dB (ganho unitário)  <- padrão ao criar a faixa
 *   posição 0.75 .. 1.00  -> 0 dB .. +6 dB
 */
inline constexpr float kDefaultPosition = 0.75f;
inline constexpr float kUnityPosition   = 0.75f;
inline constexpr float kMinDb           = -60.0f;
inline constexpr float kMaxDb           = 6.0f;

/** Ganho máximo (+6 dB) em "por mil": limite do valor transportado na fila. */
inline constexpr int kMaxGainPermille   = 2000;

[[nodiscard]] inline float clampPosition (float position) noexcept
{
    if (! (position > 0.0f))       // também captura NaN
        return 0.0f;

    return position > 1.0f ? 1.0f : position;
}

[[nodiscard]] inline float toDecibels (float position) noexcept
{
    const float p = clampPosition (position);

    if (p <= 0.0f)
        return -std::numeric_limits<float>::infinity();

    if (p <= kUnityPosition)
        return kMinDb + (p / kUnityPosition) * (0.0f - kMinDb);

    return ((p - kUnityPosition) / (1.0f - kUnityPosition)) * kMaxDb;
}

[[nodiscard]] inline float toGain (float position) noexcept
{
    const float p = clampPosition (position);

    if (p <= 0.0f)
        return 0.0f;

    return std::pow (10.0f, toDecibels (p) / 20.0f);
}

/** Ganho linear -> inteiro "por mil" (cabe em um EngineEvent trivialmente copiável). */
[[nodiscard]] inline int gainToPermille (float gain) noexcept
{
    if (! (gain > 0.0f))
        return 0;

    const int permille = static_cast<int> (std::lround (gain * 1000.0f));
    return permille > kMaxGainPermille ? kMaxGainPermille : permille;
}

[[nodiscard]] inline float permilleToGain (int permille) noexcept
{
    const int clamped = permille < 0 ? 0 : (permille > kMaxGainPermille ? kMaxGainPermille : permille);
    return static_cast<float> (clamped) * 0.001f;
}

} // namespace mf::fader
