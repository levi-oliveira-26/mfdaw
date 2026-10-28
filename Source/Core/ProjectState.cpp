#include "Core/ProjectState.h"

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

    // Presente inicial: bumbo "four on the floor" (passos 1, 5, 9, 13) para que
    // o primeiro "Tocar" já produza som.
    tree.getChildWithName (IDs::sequencer).getChild (0).setProperty (IDs::mask, 0x1111, nullptr);

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
