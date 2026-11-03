#pragma once

#include <cstdint>

#include "Core/HarmonyChord.h"

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
 *   SetTrackType  : a = slot da Playlist, b = TrackType (0..4) ou -1 (slot vazio)
 *   SetTrackGain  : a = slot da Playlist, b = ganho pré-mix em "por mil" (0..2000)
 *  SetDrumBars   : a = máscara de compassos (bit n = compasso n) cobertos por clipes de Bateria; 0 = sem arranjo
 *  SetMelodyNote : a = slot, b = passo (0..15), c = altura MIDI | (ligada ? 0x100 : 0)
 *  SetMelodyClear: a = slot (esvazia só as NOTAS do slot; os compassos vão por SetSlotBars)
 *  SetSlotBars   : a = slot, b = máscara de compassos cobertos pelos clipes da faixa; 0 = rascunho
 *  SetHarmonyChord: a = slot, b = cartão (0..3) | (ligado ? 0x100 : 0), c = 3 notas MIDI empacotadas (harmony::packNotes)
 *  SetClipRegion: a = slot, b = índice da região, c = passo inicial, d = passo final exclusivo, e = índice do take
 *
 * "Slot" é o índice da faixa na Playlist (0..kMaxPlaylistTracks-1).
 */
struct EngineEvent
{
    enum class Type : std::uint8_t { SetBpm, SetPlaying, SetTrackMask, SetStepVelocity, ChordNote,
                                     SetTrackType, SetTrackGain, SetDrumBars,
                                     SetMelodyNote, SetMelodyClear, SetSlotBars, SetHarmonyChord,
                                     SetTakeStart, SetClipRegion };

    Type type { Type::SetBpm };
    std::int32_t a { 0 };
    std::int32_t b { 0 };
    std::int32_t c { 0 };
    std::int32_t d { 0 };
    std::int32_t e { 0 };

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

    [[nodiscard]] static EngineEvent chordNote (int note, bool isOn) noexcept
    {
        return { Type::ChordNote, note, isOn ? 1 : 0, 100 };
    }

    [[nodiscard]] static EngineEvent setTrackType (int slot, int trackType) noexcept
    {
        return { Type::SetTrackType, slot, trackType, 0 };
    }

    [[nodiscard]] static EngineEvent setDrumBars (int barMask) noexcept
    {
        return { Type::SetDrumBars, barMask, 0, 0 };
    }

    [[nodiscard]] static EngineEvent setMelodyNote (int slot, int step, int pitch, bool on) noexcept
    {
        return { Type::SetMelodyNote, slot, step, pitch | (on ? 0x100 : 0) };
    }

    [[nodiscard]] static EngineEvent setMelodyClear (int slot) noexcept
    {
        return { Type::SetMelodyClear, slot, 0, 0 };
    }

    [[nodiscard]] static EngineEvent setHarmonyChord (int slot, int card, bool armed, int n0, int n1, int n2) noexcept
    {
        return { Type::SetHarmonyChord, slot, card | (armed ? 0x100 : 0), harmony::packNotes (n0, n1, n2) };
    }

    [[nodiscard]] static EngineEvent setSlotBars (int slot, int barMask) noexcept
    {
        return { Type::SetSlotBars, slot, barMask, 0 };
    }

    [[nodiscard]] static EngineEvent setTakeStart (int slot, int startStep) noexcept
    {
        return { Type::SetTakeStart, slot, startStep, 0 };
    }

    [[nodiscard]] static EngineEvent setClipRegion (int slot, int region, int startStep, int endStep, int takeIndex) noexcept
    {
        return { Type::SetClipRegion, slot, region, startStep, endStep, takeIndex };
    }

    [[nodiscard]] static EngineEvent setTrackGain (int slot, int gainPermille) noexcept
    {
        return { Type::SetTrackGain, slot, gainPermille, 0 };
    }
};

} // namespace mf
