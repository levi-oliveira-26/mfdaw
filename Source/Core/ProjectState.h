#pragma once

#include <atomic>
#include <juce_data_structures/juce_data_structures.h>

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
    inline const juce::Identifier track      { "TRACK" };
    inline const juce::Identifier mask       { "mask" };         // bit n = passo n ativo

    // --- Sessão transitória (NÃO salva, NÃO suja o projeto) ---
    inline const juce::Identifier session     { "SESSION" };
    inline const juce::Identifier currentStep { "currentStep" }; // 1..5 (Wizard)
    inline const juce::Identifier freeMode    { "freeMode" };    // Wizard <-> Timeline
    inline const juce::Identifier playing     { "playing" };
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

    // --- Sessão (transitória) ---
    [[nodiscard]] int  getCurrentStep() const;     // 1..5
    void               setCurrentStep (int step);
    [[nodiscard]] bool isFreeMode() const;
    void               setFreeMode (bool free);
    [[nodiscard]] bool isPlaying() const;
    void               setPlaying (bool shouldPlay);

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

    juce::ValueTree tree;
    juce::ValueTree session;
    juce::UndoManager undoManager;
    std::atomic<bool> modified { false };
    std::atomic<float> swingAmount { 0.0f };
};

} // namespace mf
