#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <juce_data_structures/juce_data_structures.h>

#include "Core/TrackModel.h"
#include "Core/TrackType.h"

namespace mf
{
// ---------------------------------------------------------------------------
// Constantes de domínio (limites do MVP — Card 0.1 / 1.1)
// ---------------------------------------------------------------------------
inline constexpr int kNumTracks      = 16;   // pistas do sequenciador 16 x 16
inline constexpr int kNumSteps       = 16;   // passos do sequenciador
inline constexpr int kNumWizardSteps = 5;    // Ritmo, Harmonia, Melodia, Voz, Mix
inline constexpr int kMinBpm         = 40;
inline constexpr int kMaxBpm         = 240;
inline constexpr const char* kProjectFileExtension = ".mfdaw";

enum class Scale : int { Major = 0, Minor = 1 };

struct AudioClipModel
{
    juce::String filePath;
    int startBar { 0 };
    double durationSeconds { 0.0 };
};

/** Identificadores de propriedades / tipos da árvore de estado. */
namespace IDs
{
    // --- Projeto persistente (salvo em disco) ---
    inline const juce::Identifier project    { "MFDAW_PROJECT" };
    inline const juce::Identifier name       { "name" };
    inline const juce::Identifier bpm        { "bpm" };
    inline const juce::Identifier keyRoot    { "keyRoot" };      // 0 = C ... 11 = B
    inline const juce::Identifier scale      { "scale" };        // 0 = maior, 1 = menor
    inline const juce::Identifier sampleRate { "sampleRate" };
    inline const juce::Identifier bitDepth   { "bitDepth" };
    inline const juce::Identifier swing      { "swing" };
    inline const juce::Identifier formatVersion { "formatVersion" };
    inline const juce::Identifier sequencer  { "SEQUENCER" };
    inline const juce::Identifier harmony    { "HARMONY" };
    inline const juce::Identifier chord      { "CHORD" };
    inline const juce::Identifier slot       { "slot" };
    inline const juce::Identifier notes      { "notes" };
    inline const juce::Identifier track      { "TRACK" };
    inline const juce::Identifier mask       { "mask" };         // bit n = passo n ativo

    // --- Playlist (Arrangement View): lista polimórfica de faixas ---
    inline const juce::Identifier playlist      { "PLAYLIST" };
    inline const juce::Identifier playlistTrack { "PLAYLIST_TRACK" };
    inline const juce::Identifier trackId       { "trackId" };       // id estável (nunca reutilizado)
    inline const juce::Identifier trackType     { "trackType" };     // ver enum TrackType
    inline const juce::Identifier gain          { "gain" };          // posição do fader pré-mix 0..1
    inline const juce::Identifier nextTrackId   { "nextTrackId" };   // contador monotônico (nó PLAYLIST)

    // --- Voz / clips de audio ---
    inline const juce::Identifier voiceTracks { "VOICE_TRACKS" };
    inline const juce::Identifier audioClip   { "AUDIO_CLIP" };
    inline const juce::Identifier filePath    { "filePath" };
    inline const juce::Identifier startBar    { "startBar" };
    inline const juce::Identifier durationSeconds { "durationSeconds" };

    // --- Sessão transitória (NÃO salva, NÃO suja o projeto) ---
    inline const juce::Identifier session     { "SESSION" };
    inline const juce::Identifier currentStep { "currentStep" }; // 1..5 (Wizard)
    inline const juce::Identifier freeMode    { "freeMode" };    // Wizard <-> Timeline
    inline const juce::Identifier playing     { "playing" };
    inline const juce::Identifier recording   { "recording" };
}

/**
 * ProjectState — Domínio reativo da DAW (Card 0.1 / 0.2).
 *
 * Encapsula duas árvores juce::ValueTree:
 *   - `tree`    : dados do projeto (persistentes). Qualquer mudança aciona
 *                 isModified().
 *   - `session` : estado transitório de UI/transporte (passo atual, modo livre,
 *                 play/stop). Não é serializado nem marca o projeto como sujo.
 *
 * REGRAS DE THREAD:
 *   - Esta classe só deve ser usada pela MESSAGE THREAD.
 *   - A audio thread NUNCA toca neste objeto: ela recebe cópias dos valores
 *     por meio da SpscQueue<EngineEvent> (ver MainProcessor).
 *   - isModified() é atômico e pode ser lido de qualquer thread.
 */
class ProjectState final : private juce::ValueTree::Listener
{
public:
    ProjectState();
    ~ProjectState() override;

    ProjectState (const ProjectState&) = delete;
    ProjectState& operator= (const ProjectState&) = delete;

    // --- Acesso às árvores (para registrar juce::ValueTree::Listener) ---
    [[nodiscard]] juce::ValueTree& getTree() noexcept     { return tree; }
    [[nodiscard]] juce::ValueTree& getSession() noexcept  { return session; }
    [[nodiscard]] juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    // --- Projeto: leitura ---
    [[nodiscard]] juce::String getName() const;
    [[nodiscard]] int          getBpm() const;
    [[nodiscard]] int          getKeyRoot() const;
    [[nodiscard]] Scale        getScale() const;
    [[nodiscard]] double       getSampleRate() const;
    [[nodiscard]] int          getBitDepth() const;

    // --- Projeto: escrita (valores são limitados ao intervalo válido) ---
    void setName (const juce::String& newName);
    void setBpm (int newBpm);
    void setKey (int root, Scale newScale);
    void setSampleRate (double newRate);

    // --- Harmonia persistente (Card 2.1) ---
    void setHarmonyChord (int slot, const std::array<int, 3>& midiNotes);
    [[nodiscard]] juce::String getHarmonyNotes (int slot) const;

    // --- Groove do step sequencer (Card R-04) ---
    [[nodiscard]] float getSwingAmount() const noexcept
    {
        return swingAmount.load (std::memory_order_acquire);
    }

    void setSwingAmount (float newAmount);

    // --- Step sequencer (Card 1.1) ---
    [[nodiscard]] juce::String getTrackName (int track) const;
    void setTrackName (int track, const juce::String& newName);
    [[nodiscard]] int          getTrackMask (int track) const;
    [[nodiscard]] bool         isStepActive (int track, int step) const;
    void toggleStep (int track, int step);
    [[nodiscard]] int  getStepVelocity (int track, int step) const;
    void               setStepVelocity (int track, int step, int velocity);

    // --- Playlist / Arrangement View (Pivot, Vertical Slice 01) ---
    // O projeto nasce VAZIO: 0 faixas. Cada faixa é um nó PLAYLIST_TRACK; os
    // objetos TrackModel devolvidos são visões descartáveis sobre esses nós.
    /** Acrescenta uma faixa ao final. Devolve o trackId, ou -1 se a playlist estiver cheia. */
    int addTrack (TrackType type);
    bool removeTrack (int trackId);
    [[nodiscard]] int getNumTracks() const;
    [[nodiscard]] int indexOfTrack (int trackId) const;
    [[nodiscard]] std::unique_ptr<TrackModel> getTrackByIndex (int index);
    [[nodiscard]] std::unique_ptr<TrackModel> getTrackById (int trackId);

    // --- Voz / Audio Clips (Card 4.1) ---
    void addAudioClip (const AudioClipModel& clip);
    [[nodiscard]] int getNumAudioClips() const;
    [[nodiscard]] AudioClipModel getAudioClip (int index) const;

    // --- Sessão (transitória) ---
    // LEGADO (pré-pivot): currentStep/freeMode pertenciam ao Wizard de abas
    // estáticas. Nenhuma UI nova os utiliza; remover na próxima fatia.
    [[nodiscard]] int  getCurrentStep() const;     // 1..5
    void               setCurrentStep (int step);
    [[nodiscard]] bool isFreeMode() const;
    void               setFreeMode (bool free);
    [[nodiscard]] bool isPlaying() const;
    void               setPlaying (bool shouldPlay);
    [[nodiscard]] bool isRecording() const noexcept;
    void               setRecording (bool shouldRecord);

    // --- Persistência e estado "sujo" (Card 0.2) ---
    [[nodiscard]] bool isModified() const noexcept { return modified.load (std::memory_order_acquire); }
    void markSaved() noexcept                       { modified.store (false, std::memory_order_release); }

    [[nodiscard]] juce::var createJson() const;
    [[nodiscard]] juce::String createJsonString() const;
    bool loadFromJson (const juce::var& json);
    bool loadFromJsonString (const juce::String& json);

    bool saveToFile (const juce::File& file);
    bool loadFromFile (const juce::File& file);

private:
    // juce::ValueTree::Listener — qualquer alteração no projeto o marca como sujo.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

    /** Garante propriedades/pistas padrão (projeto novo ou arquivo incompleto). */
    void ensureStructure();

    /** Remove nós inválidos/excedentes e garante ids únicos, nome e fader. */
    void sanitizePlaylist();

    /** Próximo trackId (monotônico: um id removido nunca é reaproveitado). */
    int allocateTrackId();

    juce::ValueTree tree;
    juce::ValueTree session;
    juce::UndoManager undoManager;
    std::atomic<bool> modified { false };
    std::atomic<float> swingAmount { 0.0f };
};

} // namespace mf
