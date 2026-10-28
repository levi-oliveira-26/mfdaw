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
 *   SetPlaying   : a = 1 (play), 0 (pause), 2 (stop)
 *   SetTrackMask  : a = pista, b = máscara de 16 bits dos passos
 *   SetStepVelocity: a = pista, b = passo, c = velocity MIDI (0..127)
 */
struct EngineEvent
{
    enum class Type : std::uint8_t { SetBpm, SetPlaying, SetTrackMask, SetStepVelocity };

    Type type { Type::SetBpm };
    std::int32_t a { 0 };
    std::int32_t b { 0 };
    std::int32_t c { 0 };

    [[nodiscard]] static EngineEvent setBpm (int bpm) noexcept       { return { Type::SetBpm, bpm, 0 }; }
    [[nodiscard]] static EngineEvent setPlaying (bool on) noexcept   { return { Type::SetPlaying, on ? 1 : 0, 0 }; }
    [[nodiscard]] static EngineEvent stop() noexcept                 { return { Type::SetPlaying, 2, 0 }; }
    [[nodiscard]] static EngineEvent setTrackMask (int track, int mask) noexcept
    {
        return { Type::SetTrackMask, track, mask, 0 };
    }

    [[nodiscard]] static EngineEvent setStepVelocity (int track, int step, int velocity) noexcept
    {
        return { Type::SetStepVelocity, track, step, velocity };
    }
};

} // namespace mf
