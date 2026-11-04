#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <vector>
#include <juce_data_structures/juce_data_structures.h>

#include "Core/ClipModel.h"
#include "Core/HarmonyChord.h"
#include "Core/MelodyNote.h"
#include "Core/SampleEdit.h"
#include "Core/TakeModel.h"
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

/** Um take de voz gravado numa faixa de Voz (cópia imutável para a UI). */
struct TakeData
{
    int id { -1 };
    juce::String filePath;
    int startStep { 0 };            // compasso * 16 + passo em que a gravação começou
    double durationSeconds { 0.0 };
    SampleEdit edit;                // só faixas de Amostra (Slice 09)

    [[nodiscard]] int startBar() const noexcept { return take::startBarOf (startStep); }
};

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
    inline const juce::Identifier masterGain { "masterGain" };
    inline const juce::Identifier masterMuted { "masterMuted" };
    inline const juce::Identifier masterSolo { "masterSolo" };
    inline const juce::Identifier masterBrightness { "masterBrightness" };
    inline const juce::Identifier masterSpace { "masterSpace" };
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

    // --- Clipes da Playlist (filhos de PLAYLIST_TRACK); startBar é compartilhado ---
    inline const juce::Identifier clip          { "CLIP" };
    inline const juce::Identifier clipId        { "clipId" };        // único na Playlist, nunca reutilizado
    inline const juce::Identifier lengthBars    { "lengthBars" };
    inline const juce::Identifier nextClipId    { "nextClipId" };    // contador monotônico (nó PLAYLIST)

    // --- Notas da faixa de Melodia (filhos de PLAYLIST_TRACK do tipo Melody) ---
    inline const juce::Identifier note          { "NOTE" };
    inline const juce::Identifier noteStep      { "step" };          // 0..15
    inline const juce::Identifier notePitch     { "pitch" };         // MIDI 24..107

    // --- Takes de voz (filhos de PLAYLIST_TRACK do tipo Record) ---
    inline const juce::Identifier take          { "TAKE" };
    inline const juce::Identifier takeId        { "takeId" };        // único na Playlist, nunca reutilizado
    inline const juce::Identifier takeStartStep { "startStep" };
    inline const juce::Identifier activeTake    { "activeTake" };    // propriedade da faixa: takeId em uso
    inline const juce::Identifier nextTakeId    { "nextTakeId" };    // contador monotônico (nó PLAYLIST)
    // --- Edições da faixa de Amostra (propriedades do TAKE; Slice 09) ---
    inline const juce::Identifier trimSilence   { "trimSilence" };
    inline const juce::Identifier normalizeAudio { "normalizeAudio" };
    inline const juce::Identifier fadeInMs      { "fadeInMs" };
    inline const juce::Identifier fadeOutMs     { "fadeOutMs" };

    // --- Harmonia por faixa (propriedade do PLAYLIST_TRACK do tipo Harmony) ---
    inline const juce::Identifier chordMask     { "chordMask" };     // bit n = cartão n ligado (ausente = todos)

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

    // --- Mixer Master (Card 5.1) ---
    [[nodiscard]] float getMasterGain() const noexcept { return masterGain.load (std::memory_order_acquire); }
    [[nodiscard]] bool isMasterMuted() const noexcept { return masterMuted.load (std::memory_order_acquire); }
    [[nodiscard]] bool isMasterSolo() const noexcept { return masterSolo.load (std::memory_order_acquire); }
    [[nodiscard]] float getMasterBrightness() const noexcept { return masterBrightness.load (std::memory_order_acquire); }
    [[nodiscard]] float getMasterSpace() const noexcept { return masterSpace.load (std::memory_order_acquire); }
    void setMasterGain (float gain);
    void setMasterMuted (bool muted);
    void setMasterSolo (bool solo);
    void setMasterBrightness (float brightness);
    void setMasterSpace (float space);

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

    // --- Clipes (Slice 02) ---
    // Intervalos de compassos inteiros, sem sobreposição dentro da faixa. Cada
    // operação pública é UMA transação de undo. Falhas devolvem -1/false e não
    // alteram o estado.
    /** `true` se [startBar, startBar+lengthBars) cabe no projeto e não colide com outro
     *  clipe da faixa (ignorando `ignoreClipId`, usado ao mover/redimensionar). */
    [[nodiscard]] bool canPlaceClip (int trackId, int startBar, int lengthBars, int ignoreClipId = -1) const;
    /** Cria um clipe; devolve o clipId ou -1. */
    int addClip (int trackId, int startBar, int lengthBars);
    bool removeClip (int trackId, int clipId);
    /** Move e/ou redimensiona de uma só vez (um único passo de undo). */
    bool setClipRange (int trackId, int clipId, int startBar, int lengthBars);
    /** Vincula um clipe de Voz a um take da mesma faixa; -1 remove o vínculo. */
    bool setClipTake (int trackId, int clipId, int takeId);
    /** Clipes da faixa em ordem crescente de início (vazio se a faixa não existe). */
    [[nodiscard]] std::vector<ClipData> getClips (int trackId) const;
    /** Máscara de compassos (bit n = compasso n) coberta pelos clipes de UMA faixa; 0 = sem clipes. */
    [[nodiscard]] int getTrackBarMask (int trackId) const;

    // --- Takes de voz (Slice 06) ---
    // Só faixas do tipo Record. addRecordedTake é UMA transação de undo que cria o take,
    // o marca como ativo e, se houver espaço, cria o clipe que o cobre na lane (o
    // comprimento vem da duração e do BPM; é cortado para não invadir outro clipe).
    // Devolve o takeId ou -1 (faixa errada, arquivo vazio, início/duração inválidos ou limite).
    int addRecordedTake (int trackId, const juce::String& filePath, int startStep, double durationSeconds);
    [[nodiscard]] std::vector<TakeData> getTakes (int trackId) const;          // em ordem de gravação
    [[nodiscard]] int getActiveTakeId (int trackId) const;                      // -1 se não há takes
    bool setActiveTake (int trackId, int takeId);
    /** Remove o take (o arquivo no disco NÃO é apagado aqui). Se era o ativo, o mais recente assume. */
    bool removeTake (int trackId, int takeId);

    // --- Faixa de Amostra (Slice 09) ---
    // Cada áudio importado é um TAKE da faixa (mesma maquinaria dos takes de voz: clipes ligados,
    // regiões, carregamento assíncrono). addSample é UMA transação de undo: cria o áudio, o marca
    // como ativo e cria o clipe no primeiro compasso livre (startBar < 0) ou em startBar.
    // Devolve o id ou -1 (faixa não é de Amostra, arquivo vazio, duração inválida ou limite).
    int addSample (int trackId, const juce::String& filePath, double durationSeconds, int startBar = -1);
    [[nodiscard]] SampleEdit getSampleEdit (int trackId, int takeId) const;
    /** Muda as edições (valores são limitados). No-op, sem histórico, se nada mudou. */
    bool setSampleEdit (int trackId, int takeId, const SampleEdit& edit);

    // --- Harmonia (Slice 05) ---
    // Máscara dos cartões dos Smart Chords ligados na faixa (bit n = cartão n; padrão: todos).
    // 0 para faixas inexistentes ou que não são de Harmonia.
    [[nodiscard]] int getHarmonyArmedMask (int trackId) const;
    /** Liga/desliga um cartão. Falha (false) se a faixa não é de Harmonia ou o cartão é inválido.
     *  Idempotente. Cada chamada efetiva é UM passo de undo. */
    bool setHarmonyCardArmed (int trackId, int card, bool armed);

    // --- Notas de Melodia (Slice 04) ---
    // Só faixas do tipo Melody aceitam notas. Idempotente: ligar uma nota já ligada
    // (ou desligar uma ausente) devolve true sem alterar nada. Devolve false se a faixa
    // não é de Melodia, o passo/altura são inválidos ou o limite por faixa foi atingido.
    // NÃO abre transação de undo: a UI agrupa um gesto inteiro com
    // getUndoManager().beginNewTransaction() no mouseDown.
    bool setMelodyNote (int trackId, int step, int pitch, bool on);
    [[nodiscard]] bool hasMelodyNote (int trackId, int step, int pitch) const;
    /** Notas da faixa ordenadas por (passo, altura). */
    [[nodiscard]] std::vector<MelodyNote> getMelodyNotes (int trackId) const;

    /** Maior comprimento que um clipe iniciado em `startBar` pode ter (0 = posição ocupada/inválida). */
    /** União dos compassos cobertos por clipes de faixas de Bateria (bit n = compasso n). 0 = nenhum. */
    [[nodiscard]] int getDrumBarMask() const;
    [[nodiscard]] int getMaxClipLengthAt (int trackId, int startBar, int ignoreClipId = -1) const;

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

    /** Próximo clipId (monotônico, único em toda a Playlist). */
    int allocateClipId();

    /** Próximo takeId (monotônico, único em toda a Playlist). */
    int allocateTakeId();

    /** Nó PLAYLIST_TRACK do id (inválido se não existir). */
    [[nodiscard]] juce::ValueTree findTrackNode (int trackId) const;

    juce::ValueTree tree;
    juce::ValueTree session;
    juce::UndoManager undoManager;
    std::atomic<bool> modified { false };
    std::atomic<float> swingAmount { 0.0f };
    std::atomic<float> masterGain { 1.0f };
    std::atomic<bool> masterMuted { false };
    std::atomic<bool> masterSolo { false };
    std::atomic<float> masterBrightness { 0.5f };
    std::atomic<float> masterSpace { 0.0f };
};

} // namespace mf
