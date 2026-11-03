#include "Core/ProjectState.h"

#include <algorithm>

#include "Core/FaderScale.h"

namespace mf
{
namespace
{
constexpr const char* kDefaultTrackNames[kNumTracks] = {
    "Kick", "Snare", "Hi-Hat", "Perc",
    "Track 05", "Track 06", "Track 07", "Track 08",
    "Track 09", "Track 10", "Track 11", "Track 12",
    "Track 13", "Track 14", "Track 15", "Track 16"
};

[[nodiscard]] bool isTrackValid (int track) noexcept { return track >= 0 && track < kNumTracks; }

[[nodiscard]] juce::Identifier velocityProperty (int step)
{
    return juce::Identifier ("velocity" + juce::String (step));
}

[[nodiscard]] juce::var valueTreeToJson (const juce::ValueTree& source)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("type", source.getType().toString());

    auto* propertiesObject = new juce::DynamicObject();
    for (int i = 0; i < source.getNumProperties(); ++i)
    {
        const auto name = source.getPropertyName (i);
        propertiesObject->setProperty (name, source[name]);
    }

    object->setProperty ("properties", juce::var (propertiesObject));

    juce::Array<juce::var> children;
    for (int i = 0; i < source.getNumChildren(); ++i)
        children.add (valueTreeToJson (source.getChild (i)));

    object->setProperty ("children", juce::var (children));
    return juce::var (object);
}

[[nodiscard]] juce::ValueTree valueTreeFromJson (const juce::var& json)
{
    const auto* object = json.getDynamicObject();
    if (object == nullptr)
        return juce::ValueTree();

    const auto type = object->getProperty ("type").toString();
    if (type.isEmpty())
        return juce::ValueTree();

    juce::ValueTree result { juce::Identifier (type) };

    if (const auto* propertiesObject = object->getProperty ("properties").getDynamicObject())
    {
        const auto& properties = propertiesObject->getProperties();
        for (int i = 0; i < properties.size(); ++i)
        {
            const auto name = properties.getName (i);
            result.setProperty (name, properties[name], nullptr);
        }
    }

    if (const auto* children = object->getProperty ("children").getArray())
    {
        for (const auto& childJson : *children)
        {
            auto child = valueTreeFromJson (childJson);
            if (child.isValid())
                result.appendChild (child, nullptr);
        }
    }

    return result;
}
} // namespace

// ---------------------------------------------------------------------------
// Construção
// ---------------------------------------------------------------------------
ProjectState::ProjectState()
    : tree (IDs::project),
      session (IDs::session)
{
    // Padrões inteligentes (Card 0.1): 120 BPM, C Maior, 44.1 kHz, 24 bits.
    ensureStructure();

    // PIVOT: o projeto nasce VAZIO. Nenhum padrão rítmico pré-carregado e a
    // Playlist começa com 0 faixas; o usuário monta o fluxo adicionando faixas.

    session.setProperty (IDs::currentStep, 1, nullptr);
    session.setProperty (IDs::freeMode, false, nullptr);
    session.setProperty (IDs::playing, false, nullptr);
    session.setProperty (IDs::recording, false, nullptr);

    swingAmount.store (juce::jlimit (0.0f, 1.0f,
                                     static_cast<float> (tree.getProperty (IDs::swing, 0.0))),
                       std::memory_order_release);
    modified.store (false, std::memory_order_release);

    // Só começamos a escutar depois da montagem inicial (projeto novo = limpo).
    tree.addListener (this);
}

ProjectState::~ProjectState()
{
    tree.removeListener (this);
}

void ProjectState::ensureStructure()
{
    const auto ensure = [this] (const juce::Identifier& id, const juce::var& defaultValue)
    {
        if (! tree.hasProperty (id))
            tree.setProperty (id, defaultValue, nullptr);
    };

    ensure (IDs::name, "Novo Projeto");
    ensure (IDs::bpm, 120);
    ensure (IDs::keyRoot, 0);
    ensure (IDs::scale, 0);
    ensure (IDs::sampleRate, 44100.0);
    ensure (IDs::bitDepth, 24);
    ensure (IDs::formatVersion, 1);
    ensure (IDs::swing, 0.0);

    // Arquivo corrompido / editado à mão: força BPM dentro dos limites.
    const int currentBpm = getBpm();
    const int clampedBpm = juce::jlimit (kMinBpm, kMaxBpm, currentBpm);
    if (clampedBpm != currentBpm)
        tree.setProperty (IDs::bpm, clampedBpm, nullptr);

    tree.getOrCreateChildWithName (IDs::harmony, nullptr);
    tree.getOrCreateChildWithName (IDs::voiceTracks, nullptr);
    sanitizePlaylist();

    auto sequencer = tree.getOrCreateChildWithName (IDs::sequencer, nullptr);

    while (sequencer.getNumChildren() < kNumTracks)
    {
        juce::ValueTree newTrack (IDs::track);
        newTrack.setProperty (IDs::name, kDefaultTrackNames[sequencer.getNumChildren()], nullptr);
        newTrack.setProperty (IDs::mask, 0, nullptr);
        sequencer.appendChild (newTrack, nullptr);
    }

    for (int track = 0; track < kNumTracks; ++track)
    {
        auto trackTree = sequencer.getChild (track);
        if (! trackTree.hasProperty (IDs::name))
            trackTree.setProperty (IDs::name, kDefaultTrackNames[track], nullptr);

        for (int step = 0; step < kNumSteps; ++step)
        {
            if (! trackTree.hasProperty (velocityProperty (step)))
                trackTree.setProperty (velocityProperty (step), 100, nullptr);
        }
    }
}

void ProjectState::sanitizePlaylist()
{
    auto playlist = tree.getOrCreateChildWithName (IDs::playlist, nullptr);

    // 1) Descarta nós inválidos (arquivo editado à mão) e os que excedem o limite.
    for (int i = playlist.getNumChildren(); --i >= 0;)
    {
        const auto child = playlist.getChild (i);
        const bool valid = i < kMaxPlaylistTracks
                        && child.hasType (IDs::playlistTrack)
                        && isValidTrackType (static_cast<int> (child.getProperty (IDs::trackType, -1)));

        if (! valid)
            playlist.removeChild (i, nullptr);
    }

    // 2) Ids únicos e positivos; contador monotônico acima de todos eles.
    int highestId = static_cast<int> (playlist.getProperty (IDs::nextTrackId, 1)) - 1;
    for (int i = 0; i < playlist.getNumChildren(); ++i)
        highestId = juce::jmax (highestId, static_cast<int> (playlist.getChild (i).getProperty (IDs::trackId, 0)));

    juce::Array<int> usedIds;
    for (int i = 0; i < playlist.getNumChildren(); ++i)
    {
        auto child = playlist.getChild (i);
        int id = static_cast<int> (child.getProperty (IDs::trackId, 0));

        if (id <= 0 || usedIds.contains (id))
        {
            id = ++highestId;
            child.setProperty (IDs::trackId, id, nullptr);
        }

        usedIds.add (id);

        if (! child.hasProperty (IDs::gain))
            child.setProperty (IDs::gain, static_cast<double> (fader::kDefaultPosition), nullptr);

        if (! child.hasProperty (IDs::name))
        {
            if (const auto model = TrackModel::create (child, nullptr))
                child.setProperty (IDs::name, model->getBaseName(), nullptr);
        }
    }

    if (static_cast<int> (playlist.getProperty (IDs::nextTrackId, 0)) != highestId + 1)
        playlist.setProperty (IDs::nextTrackId, highestId + 1, nullptr);

    // 3) Clipes: só nós CLIP válidos, dentro do projeto, sem sobreposição e com ids únicos.
    int highestClipId = static_cast<int> (playlist.getProperty (IDs::nextClipId, 1)) - 1;
    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        const auto trackNode = playlist.getChild (t);
        for (int c = 0; c < trackNode.getNumChildren(); ++c)
            highestClipId = juce::jmax (highestClipId,
                                        static_cast<int> (trackNode.getChild (c).getProperty (IDs::clipId, 0)));
    }

    juce::Array<int> usedClipIds;
    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        auto trackNode = playlist.getChild (t);
        std::vector<ClipData> kept;

        for (int c = 0; c < trackNode.getNumChildren();)
        {
            auto node = trackNode.getChild (c);

            if (node.hasType (IDs::note) || node.hasType (IDs::take))   // validados nos passos 4 e 6
            {
                ++c;
                continue;
            }

            const int start  = static_cast<int> (node.getProperty (IDs::startBar, -1));
            const int length = static_cast<int> (node.getProperty (IDs::lengthBars, 0));

            bool valid = node.hasType (IDs::clip) && clip::isValidRange (start, length);
            for (const auto& other : kept)
                if (valid && clip::overlaps (start, length, other.startBar, other.lengthBars))
                    valid = false;

            if (! valid)
            {
                trackNode.removeChild (c, nullptr);
                continue;
            }

            int id = static_cast<int> (node.getProperty (IDs::clipId, 0));
            if (id <= 0 || usedClipIds.contains (id))
            {
                id = ++highestClipId;
                node.setProperty (IDs::clipId, id, nullptr);
            }

            usedClipIds.add (id);
            kept.push_back ({ id, start, length, static_cast<int> (node.getProperty (IDs::takeId, -1)) });
            ++c;
        }
    }

    if (static_cast<int> (playlist.getProperty (IDs::nextClipId, 0)) != highestClipId + 1)
        playlist.setProperty (IDs::nextClipId, highestClipId + 1, nullptr);

    // 4) Notas: só em faixas de Melodia; passo/altura válidos; sem duplicatas; limite por faixa.
    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        auto trackNode = playlist.getChild (t);
        const bool isMelody = static_cast<int> (trackNode.getProperty (IDs::trackType, -1))
                              == static_cast<int> (TrackType::Melody);
        juce::Array<int> seen;

        for (int c = 0; c < trackNode.getNumChildren();)
        {
            auto node = trackNode.getChild (c);
            if (! node.hasType (IDs::note))
            {
                ++c;
                continue;
            }

            const int step  = static_cast<int> (node.getProperty (IDs::noteStep, -1));
            const int pitch = static_cast<int> (node.getProperty (IDs::notePitch, -1));
            const int key   = step * 128 + pitch;

            if (! isMelody || ! melody::isValidNote (step, pitch) || seen.contains (key)
                || seen.size() >= kMaxMelodyNotesPerTrack)
            {
                trackNode.removeChild (c, nullptr);
                continue;
            }

            seen.add (key);
            ++c;
        }
    }

    // 5) Máscara de cartões da Harmonia: só em faixas de Harmonia e dentro dos 4 bits.
    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        auto trackNode = playlist.getChild (t);
        if (! trackNode.hasProperty (IDs::chordMask))
            continue;

        if (static_cast<int> (trackNode.getProperty (IDs::trackType, -1)) != static_cast<int> (TrackType::Harmony))
            trackNode.removeProperty (IDs::chordMask, nullptr);
        else
        {
            const int raw = static_cast<int> (trackNode.getProperty (IDs::chordMask, kHarmonyAllCardsMask));
            if ((raw & kHarmonyAllCardsMask) != raw)
                trackNode.setProperty (IDs::chordMask, raw & kHarmonyAllCardsMask, nullptr);
        }
    }

    // 6) Takes de voz: só em faixas de Voz; arquivo, início e duração válidos; ids únicos; limite por faixa.
    int highestTakeId = static_cast<int> (playlist.getProperty (IDs::nextTakeId, 1)) - 1;
    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        const auto trackNode = playlist.getChild (t);
        for (int c = 0; c < trackNode.getNumChildren(); ++c)
            highestTakeId = juce::jmax (highestTakeId,
                                        static_cast<int> (trackNode.getChild (c).getProperty (IDs::takeId, 0)));
    }

    juce::Array<int> usedTakeIds;
    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        auto trackNode = playlist.getChild (t);
        const bool isRecord = static_cast<int> (trackNode.getProperty (IDs::trackType, -1))
                              == static_cast<int> (TrackType::Record);
        int keptOnTrack = 0;
        juce::Array<int> keptIds;

        for (int c = 0; c < trackNode.getNumChildren();)
        {
            auto node = trackNode.getChild (c);
            if (! node.hasType (IDs::take))
            {
                ++c;
                continue;
            }

            const bool valid = isRecord
                            && node.getProperty (IDs::filePath, "").toString().isNotEmpty()
                            && take::isValidStartStep (static_cast<int> (node.getProperty (IDs::takeStartStep, -1)))
                            && take::isValidDuration (static_cast<double> (node.getProperty (IDs::durationSeconds, 0.0)))
                            && keptOnTrack < kMaxTakesPerTrack;

            if (! valid)
            {
                trackNode.removeChild (c, nullptr);
                continue;
            }

            int id = static_cast<int> (node.getProperty (IDs::takeId, 0));
            if (id <= 0 || usedTakeIds.contains (id))
            {
                id = ++highestTakeId;
                node.setProperty (IDs::takeId, id, nullptr);
            }

            usedTakeIds.add (id);
            keptIds.add (id);
            ++keptOnTrack;
            ++c;
        }

        // Take ativo que não existe mais (ou faixa que não é de Voz): a propriedade sai.
        if (trackNode.hasProperty (IDs::activeTake)
            && ! keptIds.contains (static_cast<int> (trackNode.getProperty (IDs::activeTake, -1))))
            trackNode.removeProperty (IDs::activeTake, nullptr);
    }

    if (static_cast<int> (playlist.getProperty (IDs::nextTakeId, 0)) != highestTakeId + 1)
        playlist.setProperty (IDs::nextTakeId, highestTakeId + 1, nullptr);

    // 7) Vínculos clipe -> take: limpa referências órfãs e migra clipes do Slice 06
    // (sem takeId) para o take ativo/mais recente da faixa de Voz.
    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        auto trackNode = playlist.getChild (t);
        const bool isRecord = static_cast<int> (trackNode.getProperty (IDs::trackType, -1))
                              == static_cast<int> (TrackType::Record);
        juce::Array<int> validTakeIds;
        int latestTakeId = -1;

        for (int c = 0; c < trackNode.getNumChildren(); ++c)
        {
            const auto child = trackNode.getChild (c);
            if (child.hasType (IDs::take))
            {
                const int id = static_cast<int> (child.getProperty (IDs::takeId, -1));
                validTakeIds.add (id);
                latestTakeId = id;
            }
        }

        int activeId = static_cast<int> (trackNode.getProperty (IDs::activeTake, -1));
        if (! validTakeIds.contains (activeId))
            activeId = latestTakeId;

        for (int c = 0; c < trackNode.getNumChildren(); ++c)
        {
            auto child = trackNode.getChild (c);
            if (! child.hasType (IDs::clip))
                continue;

            if (! isRecord)
            {
                if (child.hasProperty (IDs::takeId))
                    child.removeProperty (IDs::takeId, nullptr);
                continue;
            }

            if (! child.hasProperty (IDs::takeId))
            {
                if (activeId > 0)
                    child.setProperty (IDs::takeId, activeId, nullptr);
                continue;
            }

            const int linkedId = static_cast<int> (child.getProperty (IDs::takeId, -1));
            if (! validTakeIds.contains (linkedId))
                child.removeProperty (IDs::takeId, nullptr);
        }
    }
}

int ProjectState::allocateTrackId()
{
    auto playlist = tree.getChildWithName (IDs::playlist);
    int next = static_cast<int> (playlist.getProperty (IDs::nextTrackId, 1));

    for (int i = 0; i < playlist.getNumChildren(); ++i)
        next = juce::jmax (next, static_cast<int> (playlist.getChild (i).getProperty (IDs::trackId, 0)) + 1);

    playlist.setProperty (IDs::nextTrackId, next + 1, nullptr);
    return next;
}

int ProjectState::allocateClipId()
{
    auto playlist = tree.getChildWithName (IDs::playlist);
    int next = static_cast<int> (playlist.getProperty (IDs::nextClipId, 1));

    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        const auto trackNode = playlist.getChild (t);
        for (int c = 0; c < trackNode.getNumChildren(); ++c)
            next = juce::jmax (next, static_cast<int> (trackNode.getChild (c).getProperty (IDs::clipId, 0)) + 1);
    }

    playlist.setProperty (IDs::nextClipId, next + 1, nullptr);   // contador não é desfeito: ids nunca voltam
    return next;
}

int ProjectState::allocateTakeId()
{
    auto playlist = tree.getChildWithName (IDs::playlist);
    int next = static_cast<int> (playlist.getProperty (IDs::nextTakeId, 1));

    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        const auto trackNode = playlist.getChild (t);
        for (int c = 0; c < trackNode.getNumChildren(); ++c)
            next = juce::jmax (next, static_cast<int> (trackNode.getChild (c).getProperty (IDs::takeId, 0)) + 1);
    }

    playlist.setProperty (IDs::nextTakeId, next + 1, nullptr);   // contador não é desfeito: ids nunca voltam
    return next;
}

juce::ValueTree ProjectState::findTrackNode (int trackId) const
{
    const int index = indexOfTrack (trackId);
    return index < 0 ? juce::ValueTree() : tree.getChildWithName (IDs::playlist).getChild (index);
}

// ---------------------------------------------------------------------------
// Playlist / Arrangement View (Pivot)
// ---------------------------------------------------------------------------
int ProjectState::addTrack (TrackType type)
{
    if (! isValidTrackType (static_cast<int> (type)))
        return -1;

    auto playlist = tree.getChildWithName (IDs::playlist);
    if (! playlist.isValid() || playlist.getNumChildren() >= kMaxPlaylistTracks)
        return -1;

    int sameTypeCount = 0;
    for (int i = 0; i < playlist.getNumChildren(); ++i)
        if (static_cast<int> (playlist.getChild (i).getProperty (IDs::trackType, -1)) == static_cast<int> (type))
            ++sameTypeCount;

    // Nome padrão vem da própria subclasse ("Bateria 1", "Bateria 2", ...).
    const auto probe = TrackModel::create (TrackModel::createNode (type, 0, {}), nullptr);
    if (probe == nullptr)
        return -1;

    const int id = allocateTrackId();
    const auto name = probe->getBaseName() + " " + juce::String (sameTypeCount + 1);

    // O nó já nasce completo (id, tipo, nome, fader) antes de entrar na árvore:
    // quem escuta valueTreeChildAdded enxerga um estado consistente.
    undoManager.beginNewTransaction();
    playlist.appendChild (TrackModel::createNode (type, id, name), &undoManager);
    return id;
}

bool ProjectState::removeTrack (int trackId)
{
    const int index = indexOfTrack (trackId);
    if (index < 0)
        return false;

    undoManager.beginNewTransaction();
    tree.getChildWithName (IDs::playlist).removeChild (index, &undoManager);
    return true;
}

int ProjectState::getNumTracks() const
{
    return tree.getChildWithName (IDs::playlist).getNumChildren();
}

int ProjectState::indexOfTrack (int trackId) const
{
    const auto playlist = tree.getChildWithName (IDs::playlist);

    for (int i = 0; i < playlist.getNumChildren(); ++i)
        if (static_cast<int> (playlist.getChild (i).getProperty (IDs::trackId, -1)) == trackId)
            return i;

    return -1;
}

std::unique_ptr<TrackModel> ProjectState::getTrackByIndex (int index)
{
    const auto playlist = tree.getChildWithName (IDs::playlist);
    if (index < 0 || index >= playlist.getNumChildren())
        return nullptr;

    return TrackModel::create (playlist.getChild (index), &undoManager);
}

std::unique_ptr<TrackModel> ProjectState::getTrackById (int trackId)
{
    return getTrackByIndex (indexOfTrack (trackId));
}

// ---------------------------------------------------------------------------
// Clipes (Vertical Slice 02)
// ---------------------------------------------------------------------------
bool ProjectState::canPlaceClip (int trackId, int startBar, int lengthBars, int ignoreClipId) const
{
    const auto trackNode = findTrackNode (trackId);
    if (! trackNode.isValid() || ! clip::isValidRange (startBar, lengthBars))
        return false;

    for (int c = 0; c < trackNode.getNumChildren(); ++c)
    {
        const auto other = trackNode.getChild (c);
        if (! other.hasType (IDs::clip) || static_cast<int> (other.getProperty (IDs::clipId, -1)) == ignoreClipId)
            continue;

        if (clip::overlaps (startBar, lengthBars,
                            static_cast<int> (other.getProperty (IDs::startBar, 0)),
                            static_cast<int> (other.getProperty (IDs::lengthBars, 0))))
            return false;
    }

    return true;
}

int ProjectState::addClip (int trackId, int startBar, int lengthBars)
{
    if (! canPlaceClip (trackId, startBar, lengthBars))
        return -1;

    auto trackNode = findTrackNode (trackId);
    const bool isVoice = trackNode.isValid ()
                         && static_cast<int> (trackNode.getProperty (IDs::trackType, -1)) == static_cast<int> (TrackType::Record);
    const int linkedTakeId = isVoice ? getActiveTakeId (trackId) : -1;

    // Clipes de Voz precisam identificar o áudio que devem reproduzir.
    if (isVoice && linkedTakeId < 0)
        return -1;

    const int id = allocateClipId();

    juce::ValueTree node (IDs::clip);
    node.setProperty (IDs::clipId, id, nullptr);
    node.setProperty (IDs::startBar, startBar, nullptr);
    node.setProperty (IDs::lengthBars, lengthBars, nullptr);
    if (isVoice)
        node.setProperty (IDs::takeId, linkedTakeId, nullptr);

    undoManager.beginNewTransaction();
    trackNode.appendChild (node, &undoManager);
    return id;
}

bool ProjectState::removeClip (int trackId, int clipId)
{
    auto trackNode = findTrackNode (trackId);

    for (int c = 0; c < trackNode.getNumChildren(); ++c)
    {
        if (trackNode.getChild (c).hasType (IDs::clip)
            && static_cast<int> (trackNode.getChild (c).getProperty (IDs::clipId, -1)) == clipId)
        {
            undoManager.beginNewTransaction();
            trackNode.removeChild (c, &undoManager);
            return true;
        }
    }

    return false;
}

bool ProjectState::setClipRange (int trackId, int clipId, int startBar, int lengthBars)
{
    auto trackNode = findTrackNode (trackId);

    for (int c = 0; c < trackNode.getNumChildren(); ++c)
    {
        auto node = trackNode.getChild (c);
        if (! node.hasType (IDs::clip) || static_cast<int> (node.getProperty (IDs::clipId, -1)) != clipId)
            continue;

        if (! canPlaceClip (trackId, startBar, lengthBars, clipId))
            return false;

        if (static_cast<int> (node.getProperty (IDs::startBar, -1)) == startBar
            && static_cast<int> (node.getProperty (IDs::lengthBars, -1)) == lengthBars)
            return true;   // nada a fazer (não suja o projeto nem o histórico)

        undoManager.beginNewTransaction();
        node.setProperty (IDs::startBar, startBar, &undoManager);
        node.setProperty (IDs::lengthBars, lengthBars, &undoManager);
        return true;
    }

    return false;
}


bool ProjectState::setClipTake (int trackId, int clipId, int takeId)
{
    auto trackNode = findTrackNode (trackId);
    if (! isRecordTrackNode (trackNode))
        return false;

    if (takeId != -1)
    {
        const auto takes = getTakes (trackId);
        if (std::none_of (takes.begin(), takes.end(),
                          [takeId] (const TakeData& takeData) { return takeData.id == takeId; }))
            return false;
    }

    for (int c = 0; c < trackNode.getNumChildren(); ++c)
    {
        auto node = trackNode.getChild (c);
        if (! node.hasType (IDs::clip)
            || static_cast<int> (node.getProperty (IDs::clipId, -1)) != clipId)
            continue;

        if (static_cast<int> (node.getProperty (IDs::takeId, -1)) == takeId)
            return true;

        undoManager.beginNewTransaction();
        node.setProperty (IDs::takeId, takeId, &undoManager);
        return true;
    }

    return false;
}

std::vector<ClipData> ProjectState::getClips (int trackId) const
{
    std::vector<ClipData> result;
    const auto trackNode = findTrackNode (trackId);

    for (int c = 0; c < trackNode.getNumChildren(); ++c)
    {
        const auto node = trackNode.getChild (c);
        if (node.hasType (IDs::clip))
            result.push_back ({ static_cast<int> (node.getProperty (IDs::clipId, -1)),
                                static_cast<int> (node.getProperty (IDs::startBar, 0)),
                                static_cast<int> (node.getProperty (IDs::lengthBars, 0)),
                                static_cast<int> (node.getProperty (IDs::takeId, -1)) });
    }

    std::sort (result.begin(), result.end(),
               [] (const ClipData& a, const ClipData& b) { return a.startBar < b.startBar; });
    return result;
}

int ProjectState::getDrumBarMask() const
{
    int mask = 0;
    const auto playlist = tree.getChildWithName (IDs::playlist);

    for (int t = 0; t < playlist.getNumChildren(); ++t)
    {
        const auto trackNode = playlist.getChild (t);
        if (static_cast<int> (trackNode.getProperty (IDs::trackType, -1)) != static_cast<int> (TrackType::Drum))
            continue;

        for (int c = 0; c < trackNode.getNumChildren(); ++c)
        {
            const auto node = trackNode.getChild (c);
            if (node.hasType (IDs::clip))
                mask |= clip::barMaskFor (static_cast<int> (node.getProperty (IDs::startBar, 0)),
                                          static_cast<int> (node.getProperty (IDs::lengthBars, 0)));
        }
    }

    return mask;
}

int ProjectState::getTrackBarMask (int trackId) const
{
    int mask = 0;

    for (const auto& c : getClips (trackId))
        mask |= clip::barMaskFor (c.startBar, c.lengthBars);

    return mask;
}

// ---------------------------------------------------------------------------
// Takes de voz (Vertical Slice 06)
// ---------------------------------------------------------------------------
namespace
{
bool isRecordTrackNode (const juce::ValueTree& trackNode)
{
    return trackNode.isValid()
           && static_cast<int> (trackNode.getProperty (IDs::trackType, -1)) == static_cast<int> (TrackType::Record);
}

TakeData readTake (const juce::ValueTree& node)
{
    return { static_cast<int> (node.getProperty (IDs::takeId, -1)),
             node.getProperty (IDs::filePath, "").toString(),
             static_cast<int> (node.getProperty (IDs::takeStartStep, 0)),
             static_cast<double> (node.getProperty (IDs::durationSeconds, 0.0)) };
}
} // namespace

int ProjectState::addRecordedTake (int trackId, const juce::String& filePath, int startStep, double durationSeconds)
{
    auto trackNode = findTrackNode (trackId);

    if (! isRecordTrackNode (trackNode) || filePath.trim().isEmpty()
        || ! take::isValidStartStep (startStep) || ! take::isValidDuration (durationSeconds)
        || static_cast<int> (getTakes (trackId).size()) >= kMaxTakesPerTrack)
        return -1;

    const int id = allocateTakeId();

    juce::ValueTree node (IDs::take);
    node.setProperty (IDs::takeId, id, nullptr);
    node.setProperty (IDs::filePath, filePath, nullptr);
    node.setProperty (IDs::takeStartStep, startStep, nullptr);
    node.setProperty (IDs::durationSeconds, durationSeconds, nullptr);

    // Clipe que cobre o take, cortado para caber (nunca invade outro clipe; se o compasso
    // inicial já está ocupado não há clipe, mas o take fica salvo).
    const int startBar = take::startBarOf (startStep);
    const int clipBars = juce::jmin (take::barsCovered (startStep, durationSeconds, getBpm()),
                                     getMaxClipLengthAt (trackId, startBar));

    undoManager.beginNewTransaction();   // take + ativo + clipe = UM passo de undo
    trackNode.appendChild (node, &undoManager);
    trackNode.setProperty (IDs::activeTake, id, &undoManager);

    if (clipBars >= kMinClipBars)
    {
        juce::ValueTree clipNode (IDs::clip);
        clipNode.setProperty (IDs::clipId, allocateClipId(), nullptr);
        clipNode.setProperty (IDs::startBar, startBar, nullptr);
        clipNode.setProperty (IDs::lengthBars, clipBars, nullptr);
        clipNode.setProperty (IDs::takeId, id, nullptr);
        trackNode.appendChild (clipNode, &undoManager);
    }

    return id;
}

std::vector<TakeData> ProjectState::getTakes (int trackId) const
{
    std::vector<TakeData> result;
    const auto trackNode = findTrackNode (trackId);

    for (int c = 0; c < trackNode.getNumChildren(); ++c)
        if (trackNode.getChild (c).hasType (IDs::take))
            result.push_back (readTake (trackNode.getChild (c)));

    return result;   // a ordem dos filhos é a ordem de gravação
}

int ProjectState::getActiveTakeId (int trackId) const
{
    const auto takes = getTakes (trackId);
    if (takes.empty())
        return -1;

    const int wanted = static_cast<int> (findTrackNode (trackId).getProperty (IDs::activeTake, -1));
    for (const auto& t : takes)
        if (t.id == wanted)
            return wanted;

    return takes.back().id;   // sem ativo válido: o mais recente
}

bool ProjectState::setActiveTake (int trackId, int takeId)
{
    auto trackNode = findTrackNode (trackId);
    if (! isRecordTrackNode (trackNode))
        return false;

    for (const auto& t : getTakes (trackId))
    {
        if (t.id != takeId)
            continue;

        if (static_cast<int> (trackNode.getProperty (IDs::activeTake, -1)) != takeId)
        {
            undoManager.beginNewTransaction();
            trackNode.setProperty (IDs::activeTake, takeId, &undoManager);
        }

        return true;
    }

    return false;
}

bool ProjectState::removeTake (int trackId, int takeId)
{
    auto trackNode = findTrackNode (trackId);
    if (! isRecordTrackNode (trackNode))
        return false;

    for (int c = 0; c < trackNode.getNumChildren(); ++c)
    {
        const auto node = trackNode.getChild (c);
        if (! node.hasType (IDs::take) || static_cast<int> (node.getProperty (IDs::takeId, -1)) != takeId)
            continue;

        const bool wasActive = getActiveTakeId (trackId) == takeId;

        undoManager.beginNewTransaction();

        // Apaga primeiro todos os clipes vinculados; take + clipes + ativo são
        // uma única operação de undo/redo.
        for (int i = trackNode.getNumChildren(); --i >= 0;)
        {
            const auto clipNode = trackNode.getChild (i);
            if (clipNode.hasType (IDs::clip)
                && static_cast<int> (clipNode.getProperty (IDs::takeId, -1)) == takeId)
                trackNode.removeChild (i, &undoManager);
        }

        // Reencontrar o take porque a remoção de clipes pode mudar os índices.
        for (int i = 0; i < trackNode.getNumChildren(); ++i)
        {
            const auto takeNode = trackNode.getChild (i);
            if (takeNode.hasType (IDs::take)
                && static_cast<int> (takeNode.getProperty (IDs::takeId, -1)) == takeId)
            {
                trackNode.removeChild (i, &undoManager);
                break;
            }
        }

        if (wasActive)
        {
            const auto remaining = getTakes (trackId);
            if (remaining.empty())
                trackNode.removeProperty (IDs::activeTake, &undoManager);
            else
                trackNode.setProperty (IDs::activeTake, remaining.back().id, &undoManager);
        }

        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Harmonia por faixa (Vertical Slice 05)
// ---------------------------------------------------------------------------
int ProjectState::getHarmonyArmedMask (int trackId) const
{
    const auto trackNode = findTrackNode (trackId);

    if (! trackNode.isValid()
        || static_cast<int> (trackNode.getProperty (IDs::trackType, -1)) != static_cast<int> (TrackType::Harmony))
        return 0;

    return static_cast<int> (trackNode.getProperty (IDs::chordMask, kHarmonyAllCardsMask)) & kHarmonyAllCardsMask;
}

bool ProjectState::setHarmonyCardArmed (int trackId, int card, bool armed)
{
    auto trackNode = findTrackNode (trackId);

    if (! trackNode.isValid() || ! harmony::isValidCard (card)
        || static_cast<int> (trackNode.getProperty (IDs::trackType, -1)) != static_cast<int> (TrackType::Harmony))
        return false;

    const int current = getHarmonyArmedMask (trackId);
    const int updated = armed ? (current | (1 << card)) : (current & ~(1 << card));

    if (updated != current)
    {
        undoManager.beginNewTransaction();
        trackNode.setProperty (IDs::chordMask, updated, &undoManager);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Notas de Melodia (Vertical Slice 04)
// ---------------------------------------------------------------------------
namespace
{
int indexOfNote (const juce::ValueTree& trackNode, int step, int pitch)
{
    for (int c = 0; c < trackNode.getNumChildren(); ++c)
    {
        const auto node = trackNode.getChild (c);
        if (node.hasType (IDs::note)
            && static_cast<int> (node.getProperty (IDs::noteStep, -1)) == step
            && static_cast<int> (node.getProperty (IDs::notePitch, -1)) == pitch)
            return c;
    }

    return -1;
}

int countNotes (const juce::ValueTree& trackNode)
{
    int count = 0;
    for (int c = 0; c < trackNode.getNumChildren(); ++c)
        if (trackNode.getChild (c).hasType (IDs::note))
            ++count;

    return count;
}
} // namespace

bool ProjectState::setMelodyNote (int trackId, int step, int pitch, bool on)
{
    auto trackNode = findTrackNode (trackId);

    if (! trackNode.isValid()
        || static_cast<int> (trackNode.getProperty (IDs::trackType, -1)) != static_cast<int> (TrackType::Melody)
        || ! melody::isValidNote (step, pitch))
        return false;

    const int existing = indexOfNote (trackNode, step, pitch);

    if (on)
    {
        if (existing >= 0)
            return true;

        if (countNotes (trackNode) >= kMaxMelodyNotesPerTrack)
            return false;

        juce::ValueTree node (IDs::note);
        node.setProperty (IDs::noteStep, step, nullptr);
        node.setProperty (IDs::notePitch, pitch, nullptr);
        trackNode.appendChild (node, &undoManager);
        return true;
    }

    if (existing >= 0)
        trackNode.removeChild (existing, &undoManager);

    return true;
}

bool ProjectState::hasMelodyNote (int trackId, int step, int pitch) const
{
    return indexOfNote (findTrackNode (trackId), step, pitch) >= 0;
}

std::vector<MelodyNote> ProjectState::getMelodyNotes (int trackId) const
{
    std::vector<MelodyNote> result;
    const auto trackNode = findTrackNode (trackId);

    for (int c = 0; c < trackNode.getNumChildren(); ++c)
    {
        const auto node = trackNode.getChild (c);
        if (node.hasType (IDs::note))
            result.push_back ({ static_cast<int> (node.getProperty (IDs::noteStep, 0)),
                                static_cast<int> (node.getProperty (IDs::notePitch, 60)) });
    }

    std::sort (result.begin(), result.end(), [] (const MelodyNote& a, const MelodyNote& b)
               { return a.step != b.step ? a.step < b.step : a.pitch < b.pitch; });
    return result;
}

int ProjectState::getMaxClipLengthAt (int trackId, int startBar, int ignoreClipId) const
{
    if (! findTrackNode (trackId).isValid() || startBar < 0 || startBar >= kPlaylistBars)
        return 0;

    int limit = kPlaylistBars - startBar;

    for (const auto& other : getClips (trackId))
    {
        if (other.id == ignoreClipId)
            continue;

        if (other.startBar <= startBar && startBar < other.endBar())
            return 0;

        if (other.startBar > startBar)
            limit = juce::jmin (limit, other.startBar - startBar);
    }

    return limit;
}

// ---------------------------------------------------------------------------
// Projeto — leitura
// ---------------------------------------------------------------------------
juce::String ProjectState::getName() const   { return tree.getProperty (IDs::name, "Novo Projeto").toString(); }
int ProjectState::getBpm() const             { return static_cast<int> (tree.getProperty (IDs::bpm, 120)); }
int ProjectState::getKeyRoot() const         { return static_cast<int> (tree.getProperty (IDs::keyRoot, 0)); }
double ProjectState::getSampleRate() const   { return static_cast<double> (tree.getProperty (IDs::sampleRate, 44100.0)); }
int ProjectState::getBitDepth() const        { return static_cast<int> (tree.getProperty (IDs::bitDepth, 24)); }

Scale ProjectState::getScale() const
{
    return static_cast<int> (tree.getProperty (IDs::scale, 0)) == 1 ? Scale::Minor : Scale::Major;
}

// ---------------------------------------------------------------------------
// Projeto — escrita (todas as mudanças passam pelo UndoManager)
// ---------------------------------------------------------------------------
void ProjectState::setName (const juce::String& newName)
{
    tree.setProperty (IDs::name, newName, &undoManager);
}

void ProjectState::setBpm (int newBpm)
{
    tree.setProperty (IDs::bpm, juce::jlimit (kMinBpm, kMaxBpm, newBpm), &undoManager);
}

void ProjectState::setKey (int root, Scale newScale)
{
    tree.setProperty (IDs::keyRoot, juce::jlimit (0, 11, root), &undoManager);
    tree.setProperty (IDs::scale, newScale == Scale::Minor ? 1 : 0, &undoManager);
}

void ProjectState::setSampleRate (double newRate)
{
    if (newRate > 0.0)
        tree.setProperty (IDs::sampleRate, newRate, &undoManager);
}

void ProjectState::setHarmonyChord (int slot, const std::array<int, 3>& midiNotes)
{
    if (slot < 0 || slot >= 4)
        return;

    auto harmony = tree.getChildWithName (IDs::harmony);
    juce::ValueTree chordTree;

    for (int i = 0; i < harmony.getNumChildren(); ++i)
    {
        auto child = harmony.getChild (i);
        if (static_cast<int> (child.getProperty (IDs::slot, -1)) == slot)
        {
            chordTree = child;
            break;
        }
    }

    juce::String noteList;
    for (size_t i = 0; i < midiNotes.size(); ++i)
    {
        if (i != 0)
            noteList += ",";
        noteList += juce::String (juce::jlimit (0, 127, midiNotes[i]));
    }

    if (chordTree.isValid())
    {
        chordTree.setProperty (IDs::notes, noteList, &undoManager);
    }
    else
    {
        chordTree = juce::ValueTree (IDs::chord);
        chordTree.setProperty (IDs::slot, slot, &undoManager);
        chordTree.setProperty (IDs::notes, noteList, &undoManager);
        harmony.appendChild (chordTree, &undoManager);
    }
}

void ProjectState::addAudioClip (const AudioClipModel& clip)
{
    if (clip.filePath.isEmpty() || clip.durationSeconds <= 0.0)
        return;

    juce::ValueTree clipTree (IDs::audioClip);
    clipTree.setProperty (IDs::filePath, clip.filePath, &undoManager);
    clipTree.setProperty (IDs::startBar, juce::jmax (0, clip.startBar), &undoManager);
    clipTree.setProperty (IDs::durationSeconds, juce::jmax (0.0, clip.durationSeconds), &undoManager);
    tree.getChildWithName (IDs::voiceTracks).appendChild (clipTree, &undoManager);
}

int ProjectState::getNumAudioClips() const
{
    return tree.getChildWithName (IDs::voiceTracks).getNumChildren();
}

AudioClipModel ProjectState::getAudioClip (int index) const
{
    const auto clips = tree.getChildWithName (IDs::voiceTracks);
    if (index < 0 || index >= clips.getNumChildren())
        return {};

    const auto clip = clips.getChild (index);
    return { clip.getProperty (IDs::filePath, {}).toString(),
             static_cast<int> (clip.getProperty (IDs::startBar, 0)),
             static_cast<double> (clip.getProperty (IDs::durationSeconds, 0.0)) };
}

juce::String ProjectState::getHarmonyNotes (int slot) const
{
    const auto harmony = tree.getChildWithName (IDs::harmony);
    for (int i = 0; i < harmony.getNumChildren(); ++i)
    {
        const auto chordTree = harmony.getChild (i);
        if (static_cast<int> (chordTree.getProperty (IDs::slot, -1)) == slot)
            return chordTree.getProperty (IDs::notes, "").toString();
    }
    return {};
}

void ProjectState::setSwingAmount (float newAmount)
{
    const float limitedAmount = juce::jlimit (0.0f, 1.0f, newAmount);
    swingAmount.store (limitedAmount, std::memory_order_release);
    tree.setProperty (IDs::swing, limitedAmount, &undoManager);
}

// ---------------------------------------------------------------------------
// Step sequencer
// ---------------------------------------------------------------------------
juce::String ProjectState::getTrackName (int track) const
{
    if (! isTrackValid (track))
        return {};

    return tree.getChildWithName (IDs::sequencer).getChild (track).getProperty (IDs::name, "").toString();
}

void ProjectState::setTrackName (int track, const juce::String& newName)
{
    if (! isTrackValid (track))
        return;

    const auto trimmedName = newName.trim();
    if (trimmedName.isEmpty())
        return;

    auto trackTree = tree.getChildWithName (IDs::sequencer).getChild (track);
    trackTree.setProperty (IDs::name, trimmedName, &undoManager);
}

int ProjectState::getTrackMask (int track) const
{
    if (! isTrackValid (track))
        return 0;

    // Sempre lido da árvore viva (sem cache) para nunca devolver dados obsoletos
    // quando um projeto é recarregado.
    return static_cast<int> (tree.getChildWithName (IDs::sequencer).getChild (track).getProperty (IDs::mask, 0));
}

bool ProjectState::isStepActive (int track, int step) const
{
    if (step < 0 || step >= kNumSteps)
        return false;

    return ((getTrackMask (track) >> step) & 1) != 0;
}

void ProjectState::toggleStep (int track, int step)
{
    if (! isTrackValid (track) || step < 0 || step >= kNumSteps)
        return;

    auto trackTree = tree.getChildWithName (IDs::sequencer).getChild (track);
    trackTree.setProperty (IDs::mask, getTrackMask (track) ^ (1 << step), &undoManager);
}

int ProjectState::getStepVelocity (int track, int step) const
{
    if (! isTrackValid (track) || step < 0 || step >= kNumSteps)
        return 100;

    const auto trackTree = tree.getChildWithName (IDs::sequencer).getChild (track);
    return juce::jlimit (0, 127, static_cast<int> (trackTree.getProperty (velocityProperty (step), 100)));
}

void ProjectState::setStepVelocity (int track, int step, int velocity)
{
    if (! isTrackValid (track) || step < 0 || step >= kNumSteps)
        return;

    auto trackTree = tree.getChildWithName (IDs::sequencer).getChild (track);
    trackTree.setProperty (velocityProperty (step), juce::jlimit (0, 127, velocity), &undoManager);
}

// ---------------------------------------------------------------------------
// Sessão (transitória: nunca suja o projeto, nunca entra no undo)
// ---------------------------------------------------------------------------
int ProjectState::getCurrentStep() const
{
    return juce::jlimit (1, kNumWizardSteps, static_cast<int> (session.getProperty (IDs::currentStep, 1)));
}

void ProjectState::setCurrentStep (int step)
{
    session.setProperty (IDs::currentStep, juce::jlimit (1, kNumWizardSteps, step), nullptr);
}

bool ProjectState::isFreeMode() const { return static_cast<bool> (session.getProperty (IDs::freeMode, false)); }
void ProjectState::setFreeMode (bool free) { session.setProperty (IDs::freeMode, free, nullptr); }

bool ProjectState::isPlaying() const { return static_cast<bool> (session.getProperty (IDs::playing, false)); }
void ProjectState::setPlaying (bool shouldPlay) { session.setProperty (IDs::playing, shouldPlay, nullptr); }
bool ProjectState::isRecording() const noexcept
{
    return static_cast<bool> (session.getProperty (IDs::recording, false));
}
void ProjectState::setRecording (bool shouldRecord)
{
    session.setProperty (IDs::recording, shouldRecord, nullptr);
}

// ---------------------------------------------------------------------------
// Persistência (Card 0.2)
// ---------------------------------------------------------------------------
juce::var ProjectState::createJson() const
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    return valueTreeToJson (tree);
}

juce::String ProjectState::createJsonString() const
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    return juce::JSON::toString (createJson(), true);
}

bool ProjectState::loadFromJson (const juce::var& json)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    auto loaded = valueTreeFromJson (json);

    if (! loaded.isValid() || loaded.getType() != IDs::project)
        return false;

    // Mantém a raiz existente para não perder os listeners de UI, engine e undo.
    tree.copyPropertiesAndChildrenFrom (loaded, nullptr);
    ensureStructure();
    swingAmount.store (juce::jlimit (0.0f, 1.0f,
                                     static_cast<float> (tree.getProperty (IDs::swing, 0.0))),
                       std::memory_order_release);

    undoManager.clearUndoHistory();
    markSaved();
    return true;
}

bool ProjectState::loadFromJsonString (const juce::String& json)
{
    const auto parsed = juce::JSON::parse (json);
    return parsed.isVoid() ? false : loadFromJson (parsed);
}

bool ProjectState::saveToFile (const juce::File& file)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    auto projectFile = file;
    projectFile = projectFile.withFileExtension (juce::String (kProjectFileExtension));

    const auto parentDirectory = projectFile.getParentDirectory();
    if (! parentDirectory.isDirectory() && ! parentDirectory.createDirectory())
        return false;

    if (! projectFile.replaceWithText (createJsonString(), false, false))
        return false;

    markSaved();
    return true;
}

bool ProjectState::loadFromFile (const juce::File& file)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (file.getFileExtension().compareIgnoreCase (kProjectFileExtension) != 0)
        return false;

    const auto text = file.loadFileAsString();
    if (! text.isEmpty())
        return loadFromJsonString (text);

    return false;
}

// ---------------------------------------------------------------------------
// Estado "sujo" (isModified)
// ---------------------------------------------------------------------------
void ProjectState::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == IDs::swing)
        swingAmount.store (juce::jlimit (0.0f, 1.0f,
                                         static_cast<float> (tree.getProperty (IDs::swing, 0.0))),
                           std::memory_order_release);

    modified.store (true, std::memory_order_release);
}

void ProjectState::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    modified.store (true, std::memory_order_release);
}

void ProjectState::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    modified.store (true, std::memory_order_release);
}

} // namespace mf
