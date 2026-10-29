#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "Core/ClipModel.h"
#include "Core/MelodyNote.h"
#include "Core/TrackType.h"

namespace mf
{
/**
 * MelodySequencer — padrões de Melodia de todos os slots da Playlist, no lado
 * da AUDIO THREAD.
 *
 * É uma cópia compacta do que está no ProjectState: chega por EngineEvent
 * (SetMelodyNote / SetMelodyClear / SetSlotBars) e é lida pela audio thread a
 * cada passo. Nada aqui aloca, bloqueia ou toca no ValueTree: apenas arrays de
 * tamanho fixo (kSlots x 16 passos x 128 alturas MIDI em 2 palavras de 64 bits
 * = 8 KiB) e operações de bits. Por ser header-only e sem JUCE, é testável sem
 * subir o motor de áudio.
 *
 * Gate por clipes: o padrão de um slot só soa nos compassos cobertos pela
 * máscara do slot (bit n = compasso n). Máscara 0 = "modo rascunho" (sem
 * clipes: toca sempre), a mesma regra da Bateria (ver clip::isBarAudible).
 */
class MelodySequencer
{
public:
    static constexpr int kSlots = kMaxPlaylistTracks;

    /** Esvazia só as notas do slot (a máscara de compassos é enviada à parte). */
    void clearNotes (int slot) noexcept
    {
        if (! isValidSlot (slot))
            return;

        for (auto& word : bits[static_cast<std::size_t> (slot)])
            word = { 0u, 0u };
    }

    /** Esvazia o slot por inteiro (notas e máscara de compassos). */
    void clearSlot (int slot) noexcept
    {
        clearNotes (slot);
        setBars (slot, 0);
    }

    void setNote (int slot, int step, int pitch, bool on) noexcept
    {
        if (! isValidSlot (slot) || step < 0 || step >= kMelodySteps || pitch < 0 || pitch > 127)
            return;

        auto& word = bits[static_cast<std::size_t> (slot)][static_cast<std::size_t> (step)]
                         [static_cast<std::size_t> (pitch >> 6)];
        const std::uint64_t flag = std::uint64_t { 1 } << (pitch & 63);
        word = on ? (word | flag) : (word & ~flag);
    }

    void setBars (int slot, int barMask) noexcept
    {
        if (isValidSlot (slot))
            barMasks[static_cast<std::size_t> (slot)] = barMask & ((1 << kPlaylistBars) - 1);
    }

    [[nodiscard]] bool hasNote (int slot, int step, int pitch) const noexcept
    {
        if (! isValidSlot (slot) || step < 0 || step >= kMelodySteps || pitch < 0 || pitch > 127)
            return false;

        return ((bits[static_cast<std::size_t> (slot)][static_cast<std::size_t> (step)]
                     [static_cast<std::size_t> (pitch >> 6)] >> (pitch & 63)) & 1u) != 0;
    }

    /** O slot soa neste compasso? (máscara 0 = rascunho = sempre) */
    [[nodiscard]] bool isAudible (int slot, int bar) const noexcept
    {
        return isValidSlot (slot) && clip::isBarAudible (barMasks[static_cast<std::size_t> (slot)], bar);
    }

    [[nodiscard]] int getBars (int slot) const noexcept
    {
        return isValidSlot (slot) ? barMasks[static_cast<std::size_t> (slot)] : 0;
    }

    /** Chama fn(pitch) para cada nota do passo, da mais grave para a mais aguda. */
    template <typename Fn>
    void forEachNote (int slot, int step, Fn&& fn) const noexcept
    {
        if (! isValidSlot (slot) || step < 0 || step >= kMelodySteps)
            return;

        const auto& words = bits[static_cast<std::size_t> (slot)][static_cast<std::size_t> (step)];

        for (std::size_t w = 0; w < words.size(); ++w)
        {
            std::uint64_t remaining = words[w];
            while (remaining != 0)
            {
                fn (static_cast<int> (w) * 64 + std::countr_zero (remaining));
                remaining &= remaining - 1;   // zera o bit mais baixo
            }
        }
    }

private:
    [[nodiscard]] static constexpr bool isValidSlot (int slot) noexcept { return slot >= 0 && slot < kSlots; }

    std::array<std::array<std::array<std::uint64_t, 2>, kMelodySteps>, kSlots> bits {};
    std::array<int, kSlots> barMasks {};
};

} // namespace mf
