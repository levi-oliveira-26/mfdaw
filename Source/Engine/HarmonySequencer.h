#pragma once

#include <array>
#include <cstdint>

#include "Core/ClipModel.h"
#include "Core/HarmonyChord.h"
#include "Core/TrackType.h"

namespace mf
{
/**
 * HarmonySequencer — progressões de Harmonia de todos os slots da Playlist, no
 * lado da AUDIO THREAD (irmão do MelodySequencer).
 *
 * Cópia compacta do ProjectState, recebida por EngineEvent (SetHarmonyChord /
 * SetSlotBars) e lida a cada compasso. Só arrays de tamanho fixo: nada de
 * alocação, travas ou ValueTree. Header-only e sem JUCE, logo testável sem o
 * motor de áudio.
 *
 * Por compasso, o cartão (compasso % 4) de cada slot audível soa se estiver
 * ligado. Máscara de compassos 0 = "modo rascunho" (sem clipes: toca sempre),
 * a mesma regra da Bateria e da Melodia (clip::isBarAudible).
 */
class HarmonySequencer
{
public:
    static constexpr int kSlots = kMaxPlaylistTracks;

    struct Chord
    {
        bool armed { false };
        std::array<std::uint8_t, 3> notes { 0, 0, 0 };
    };

    void setChord (int slot, int card, bool armed, int n0, int n1, int n2) noexcept
    {
        if (! isValidSlot (slot) || ! harmony::isValidCard (card))
            return;

        auto& chord = chords[static_cast<std::size_t> (slot)][static_cast<std::size_t> (card)];
        chord.armed = armed;
        chord.notes = { clampNote (n0), clampNote (n1), clampNote (n2) };
    }

    void setBars (int slot, int barMask) noexcept
    {
        if (isValidSlot (slot))
            barMasks[static_cast<std::size_t> (slot)] = barMask & ((1 << kPlaylistBars) - 1);
    }

    [[nodiscard]] bool isAudible (int slot, int bar) const noexcept
    {
        return isValidSlot (slot) && clip::isBarAudible (barMasks[static_cast<std::size_t> (slot)], bar);
    }

    [[nodiscard]] bool isArmed (int slot, int card) const noexcept
    {
        return isValidSlot (slot) && harmony::isValidCard (card)
               && chords[static_cast<std::size_t> (slot)][static_cast<std::size_t> (card)].armed;
    }

    [[nodiscard]] Chord getChord (int slot, int card) const noexcept
    {
        return isValidSlot (slot) && harmony::isValidCard (card)
                   ? chords[static_cast<std::size_t> (slot)][static_cast<std::size_t> (card)]
                   : Chord {};
    }

    /** Chama fn(nota) para cada nota do acorde que soa neste compasso (nenhuma se inaudível/desligado). */
    template <typename Fn>
    void forEachNoteInBar (int slot, int bar, Fn&& fn) const noexcept
    {
        if (! isAudible (slot, bar))
            return;

        const auto& chord = chords[static_cast<std::size_t> (slot)][static_cast<std::size_t> (harmony::cardForBar (bar))];
        if (! chord.armed)
            return;

        for (const auto note : chord.notes)
            fn (static_cast<int> (note));
    }

private:
    [[nodiscard]] static constexpr bool isValidSlot (int slot) noexcept { return slot >= 0 && slot < kSlots; }
    [[nodiscard]] static constexpr std::uint8_t clampNote (int note) noexcept
    {
        return static_cast<std::uint8_t> (note < 0 ? 0 : (note > 127 ? 127 : note));
    }

    std::array<std::array<Chord, kHarmonyCards>, kSlots> chords {};
    std::array<int, kSlots> barMasks {};
};

} // namespace mf
