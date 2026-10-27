#pragma once

#include <cstdint>

namespace mf
{
/**
 * Evento enviado da UI (message thread) para o motor de áudio (audio thread)
 * através da SpscQueue.
 *
 * Requisitos de tempo real: trivialmente copiável, tamanho fixo, sem ponteiros
 * para memória dinâmica e sem strings. Qualquer dado maior deve ser
 * decomposto em vários eventos pequenos.
 *
 *   SetBpm       : a = BPM
 *   SetPlaying   : a = 1 (tocar) ou 0 (parar)
 *   SetTrackMask : a = índice da pista (0..3), b = máscara de 16 bits dos passos
 */
struct EngineEvent
{
    enum class Type : std::uint8_t { SetBpm, SetPlaying, SetTrackMask };

    Type type { Type::SetBpm };
    std::int32_t a { 0 };
    std::int32_t b { 0 };

    [[nodiscard]] static EngineEvent setBpm (int bpm) noexcept       { return { Type::SetBpm, bpm, 0 }; }
    [[nodiscard]] static EngineEvent setPlaying (bool on) noexcept   { return { Type::SetPlaying, on ? 1 : 0, 0 }; }
    [[nodiscard]] static EngineEvent setTrackMask (int track, int mask) noexcept
    {
        return { Type::SetTrackMask, track, mask };
    }
};

} // namespace mf
