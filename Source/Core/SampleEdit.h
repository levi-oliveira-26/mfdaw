#pragma once

#include <algorithm>
#include <cstdint>

namespace mf
{
/**
 * Edições NÃO destrutivas de um áudio da faixa de Amostra (Slice 09).
 * O arquivo original nunca é alterado: as edições são aplicadas ao carregar o áudio
 * para reprodução (fora da audio thread). Puro, sem JUCE.
 */
struct SampleEdit
{
    static constexpr int kMaxFadeMs = 2000;

    bool trimSilence { false };   // corta silêncio do início e do fim
    bool normalize   { false };   // sobe o volume até o pico ficar em -1 dBFS
    int  fadeInMs    { 0 };       // entrada suave
    int  fadeOutMs   { 0 };       // saída suave

    [[nodiscard]] constexpr bool isDefault() const noexcept
    {
        return ! trimSilence && ! normalize && fadeInMs == 0 && fadeOutMs == 0;
    }

    [[nodiscard]] constexpr SampleEdit clamped() const noexcept
    {
        return { trimSilence, normalize,
                 std::clamp (fadeInMs, 0, kMaxFadeMs), std::clamp (fadeOutMs, 0, kMaxFadeMs) };
    }

    /** Chave compacta (para detectar mudança de edição e recarregar o áudio). */
    [[nodiscard]] constexpr std::int64_t signature() const noexcept
    {
        const auto c = clamped();
        return (c.trimSilence ? 1 : 0) | (c.normalize ? 2 : 0)
               | (static_cast<std::int64_t> (c.fadeInMs) << 2)
               | (static_cast<std::int64_t> (c.fadeOutMs) << 14);
    }

    [[nodiscard]] constexpr bool operator== (const SampleEdit& o) const noexcept
    {
        return trimSilence == o.trimSilence && normalize == o.normalize
               && fadeInMs == o.fadeInMs && fadeOutMs == o.fadeOutMs;
    }
};
} // namespace mf
