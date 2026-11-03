#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

#include "Core/ClipModel.h"
#include "Core/TakeModel.h"
#include "Core/TrackType.h"

namespace mf
{
/** Áudio de um take já carregado (mono, float). Criado e destruído SÓ na message thread. */
struct TakeBuffer
{
    std::vector<float> samples;
    double sampleRate { 44100.0 };
};

/**
 * TakePlayer — reprodução dos takes de voz dentro dos clipes (Slice 07).
 *
 * Header-only e sem JUCE (testável sem o motor de áudio).
 *
 * Regra de ouro: a audio thread NUNCA libera memória. Os TakeBuffer pertencem à
 * message thread, que publica um ponteiro cru por slot (`publish`). Para saber
 * quando um buffer antigo pode ser destruído usa-se o padrão "hazard pointer":
 * a audio thread anuncia (`hazard`) o ponteiro que está usando antes de lê-lo, e
 * a message thread só destrói buffers que não estão publicados nem anunciados
 * (`isRetired`).
 *
 * Por passo, cada slot decide: fora de um clipe -> solta (fade curto); no passo
 * exato de início do take -> começa do zero. Faixa de Voz sem clipe é silenciosa
 * (máscara 0 = mudo, ao contrário do "rascunho" da Bateria/Melodia).
 */
class TakePlayer
{
public:
    static constexpr int kSlots = kMaxPlaylistTracks;
    static constexpr double kFadeSeconds = 0.005;

    // ---------------- Message thread ----------------
    void publish (int slot, const TakeBuffer* buffer) noexcept
    {
        if (isValidSlot (slot))
            published[idx (slot)].store (buffer, std::memory_order_seq_cst);
    }

    /** true se `buffer` não está publicado nem em uso: pode ser destruído. */
    [[nodiscard]] bool isRetired (const TakeBuffer* buffer) const noexcept
    {
        for (int s = 0; s < kSlots; ++s)
            if (published[idx (s)].load (std::memory_order_seq_cst) == buffer
                || hazard[idx (s)].load (std::memory_order_seq_cst) == buffer)
                return false;
        return true;
    }

    // ---------------- Audio thread ----------------
    void setBars (int slot, int barMask) noexcept
    {
        if (isValidSlot (slot))
            barMasks[idx (slot)] = barMask & ((1 << kPlaylistBars) - 1);
    }

    void setStartStep (int slot, int startStep) noexcept
    {
        if (isValidSlot (slot))
            startSteps[idx (slot)] = take::isValidStartStep (startStep) ? startStep : -1;
    }

    /** Início de cada bloco: protege os buffers publicados e reinicia vozes cujo take mudou. */
    void beginBlock() noexcept
    {
        for (int s = 0; s < kSlots; ++s)
        {
            auto& slotHazard = hazard[idx (s)];
            const auto& slotPublished = published[idx (s)];

            const TakeBuffer* p = slotPublished.load (std::memory_order_seq_cst);
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                slotHazard.store (p, std::memory_order_seq_cst);
                const TakeBuffer* again = slotPublished.load (std::memory_order_seq_cst);
                if (again == p)
                    break;
                p = again;
            }

            if (slotPublished.load (std::memory_order_seq_cst) != p)   // publicação em corrida: silêncio neste bloco
            {
                p = nullptr;
                slotHazard.store (nullptr, std::memory_order_seq_cst);
            }

            auto& voice = voices[idx (s)];
            if (voice.buffer != p)
            {
                voice = Voice {};
                voice.buffer = p;
            }
        }
    }

    /** Chamado a cada passo agendado (compasso 0-based, passo 0..15). */
    void triggerStep (int bar, int step) noexcept
    {
        const int absoluteStep = bar * kStepsPerBar + step;

        for (int s = 0; s < kSlots; ++s)
        {
            auto& voice = voices[idx (s)];
            const int start = startSteps[idx (s)];

            if (voice.buffer == nullptr || start < 0)
                continue;

            const int mask = barMasks[idx (s)];
            if (mask == 0 || ! clip::isBarAudible (mask, bar))
            {
                voice.target = false;
                continue;
            }

            if (absoluteStep == start)
            {
                voice.pos = 0.0;
                voice.target = true;
                voice.active = true;
            }
        }
    }

    /** Pause / Stop: todas as vozes soltam. */
    void releaseAll() noexcept
    {
        for (auto& voice : voices)
            voice.target = false;
    }

    /** Uma amostra da soma de todas as vozes, cada uma vezes o ganho do próprio slot. */
    [[nodiscard]] float process (double hostSampleRate, const std::array<float, kSlots>& slotGains) noexcept
    {
        float sum = 0.0f;
        for (int s = 0; s < kSlots; ++s)
            if (voices[idx (s)].active)
                sum += processVoice (voices[idx (s)], hostSampleRate) * slotGains[idx (s)];
        return sum;
    }

    [[nodiscard]] bool isPlaying (int slot) const noexcept
    {
        return isValidSlot (slot) && voices[idx (slot)].active;
    }

private:
    struct Voice
    {
        const TakeBuffer* buffer { nullptr };
        double pos { 0.0 };
        float env { 0.0f };
        bool target { false };
        bool active { false };
    };

    [[nodiscard]] static float processVoice (Voice& v, double hostSampleRate) noexcept
    {
        const float rampStep = static_cast<float> (1.0 / (kFadeSeconds * hostSampleRate));
        v.env = v.target ? (v.env + rampStep > 1.0f ? 1.0f : v.env + rampStep)
                         : (v.env - rampStep < 0.0f ? 0.0f : v.env - rampStep);

        const auto& samples = v.buffer->samples;
        const auto index = static_cast<std::size_t> (v.pos);

        if ((! v.target && v.env <= 0.0f) || index + 1 >= samples.size())
        {
            v.active = false;
            v.env = 0.0f;
            return 0.0f;
        }

        const auto frac = static_cast<float> (v.pos - static_cast<double> (index));
        const float sample = samples[index] + (samples[index + 1] - samples[index]) * frac;
        v.pos += v.buffer->sampleRate / hostSampleRate;
        return sample * v.env;
    }

    [[nodiscard]] static constexpr bool isValidSlot (int slot) noexcept { return slot >= 0 && slot < kSlots; }
    [[nodiscard]] static constexpr std::size_t idx (int slot) noexcept { return static_cast<std::size_t> (slot); }

    std::array<std::atomic<const TakeBuffer*>, kSlots> published {};
    std::array<std::atomic<const TakeBuffer*>, kSlots> hazard {};
    std::array<int, kSlots> barMasks {};
    std::array<int, kSlots> startSteps = [] { std::array<int, kSlots> a {}; a.fill (-1); return a; }();
    std::array<Voice, kSlots> voices {};
};

} // namespace mf
