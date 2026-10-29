#pragma once

#include <memory>
#include <juce_data_structures/juce_data_structures.h>

#include "Core/TrackType.h"

namespace mf
{
/**
 * TrackModel — base polimórfica das faixas da Playlist (DOMÍNIO).
 *
 * É uma "visão com comportamento" sobre um nó juce::ValueTree do tipo
 * PLAYLIST_TRACK: o ValueTree continua sendo a única fonte da verdade (salvar,
 * carregar, undo e listeners funcionam sem código extra) e cada subclasse
 * acrescenta apenas o que é específico do tipo.
 *
 * REGRAS DE THREAD: somente MESSAGE THREAD. A audio thread nunca vê esta
 * classe — ela recebe cópias de ganho/tipo pela SpscQueue<EngineEvent>.
 *
 * Instâncias são baratas e descartáveis (alocam só na message thread). Não
 * guarde ponteiros para elas além do escopo de uma operação: guarde o trackId
 * e peça uma nova via ProjectState::getTrackById().
 */
class TrackModel
{
public:
    virtual ~TrackModel() = default;

    TrackModel (const TrackModel&) = delete;
    TrackModel& operator= (const TrackModel&) = delete;

    /** Fábrica: devolve a subclasse correta para o nó, ou nullptr se inválido. */
    [[nodiscard]] static std::unique_ptr<TrackModel> create (const juce::ValueTree& node,
                                                             juce::UndoManager* undoManager);

    /** Cria o nó (esquema único das faixas) já com id, tipo, nome e fader padrão. */
    [[nodiscard]] static juce::ValueTree createNode (TrackType type, int trackId, const juce::String& name);

    // --- Comportamento específico do tipo ---
    [[nodiscard]] virtual TrackType getType() const noexcept = 0;
    [[nodiscard]] virtual juce::String getBaseName() const = 0;          // "Bateria", "Harmonia"...
    [[nodiscard]] virtual TrackAction getPrimaryAction() const noexcept = 0;

    // --- Comum a todas as faixas ---
    [[nodiscard]] int getId() const;
    [[nodiscard]] juce::String getName() const;
    void setName (const juce::String& newName);

    /** Posição do fader pré-mix (0..1). Ver Core/FaderScale.h. */
    [[nodiscard]] float getGain() const;
    void setGain (float faderPosition);

    [[nodiscard]] const juce::ValueTree& getNode() const noexcept { return node; }

protected:
    TrackModel (juce::ValueTree nodeToWrap, juce::UndoManager* undo)
        : node (std::move (nodeToWrap)), undoManager (undo) {}

private:
    juce::ValueTree node;
    juce::UndoManager* undoManager;
};

// ---------------------------------------------------------------------------
// Os 5 tipos concretos
// ---------------------------------------------------------------------------
class DrumTrack final : public TrackModel
{
public:
    DrumTrack (juce::ValueTree n, juce::UndoManager* um) : TrackModel (std::move (n), um) {}

    TrackType getType() const noexcept override          { return TrackType::Drum; }
    juce::String getBaseName() const override            { return "Bateria"; }
    TrackAction getPrimaryAction() const noexcept override { return TrackAction::OpenStepSequencer; }
};

class HarmonyTrack final : public TrackModel
{
public:
    HarmonyTrack (juce::ValueTree n, juce::UndoManager* um) : TrackModel (std::move (n), um) {}

    TrackType getType() const noexcept override          { return TrackType::Harmony; }
    juce::String getBaseName() const override            { return "Harmonia"; }
    TrackAction getPrimaryAction() const noexcept override { return TrackAction::OpenSmartChords; }
};

class MelodyTrack final : public TrackModel
{
public:
    MelodyTrack (juce::ValueTree n, juce::UndoManager* um) : TrackModel (std::move (n), um) {}

    TrackType getType() const noexcept override          { return TrackType::Melody; }
    juce::String getBaseName() const override            { return "Melodia"; }
    TrackAction getPrimaryAction() const noexcept override { return TrackAction::OpenPianoRoll; }
};

class RecordTrack final : public TrackModel
{
public:
    RecordTrack (juce::ValueTree n, juce::UndoManager* um) : TrackModel (std::move (n), um) {}

    TrackType getType() const noexcept override          { return TrackType::Record; }
    juce::String getBaseName() const override            { return "Voz"; }
    TrackAction getPrimaryAction() const noexcept override { return TrackAction::ToggleRecording; }
};

class SampleTrack final : public TrackModel
{
public:
    SampleTrack (juce::ValueTree n, juce::UndoManager* um) : TrackModel (std::move (n), um) {}

    TrackType getType() const noexcept override          { return TrackType::Sample; }
    juce::String getBaseName() const override            { return "Amostra"; }
    TrackAction getPrimaryAction() const noexcept override { return TrackAction::OpenSampleEditor; }
};

} // namespace mf
