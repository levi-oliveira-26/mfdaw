#pragma once

namespace mf
{
// ---------------------------------------------------------------------------
// Vocabulário do domínio da Playlist (Arrangement View).
// Sem dependência de JUCE: pode ser usado por domínio, engine, UI e testes.
// ---------------------------------------------------------------------------

/** Os 5 tipos de faixa que o usuário pode empilhar na Playlist. */
enum class TrackType : int
{
    Drum    = 0,   // Sequenciador 16x16 (+ samples e swing)
    Harmony = 1,   // Smart Chords
    Melody  = 2,   // Piano Roll com Scale Lock
    Record  = 3,   // Gravação de voz / áudio
    Sample  = 4    // Edição de áudio (corte, normalizar, envelope)
};

/** Comando de domínio disparado pelo botão de ação do cabeçalho da faixa.
 *  A UI decide COMO apresentá-lo; o domínio só diz O QUE a faixa sabe fazer. */
enum class TrackAction : int
{
    OpenStepSequencer,
    OpenSmartChords,
    OpenPianoRoll,
    ToggleRecording,
    OpenSampleEditor
};

inline constexpr int kNumTrackTypes     = 5;
inline constexpr int kMaxPlaylistTracks = 32;   // também é o nº de "slots" de ganho na engine

[[nodiscard]] constexpr bool isValidTrackType (int rawType) noexcept
{
    return rawType >= 0 && rawType < kNumTrackTypes;
}

} // namespace mf
