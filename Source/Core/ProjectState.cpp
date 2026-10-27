#include "Core/ProjectState.h"

namespace mf
{
namespace
{
constexpr const char* kDefaultTrackNames[kNumTracks] = { "Kick", "Snare", "Hi-Hat", "Perc" };

[[nodiscard]] bool isTrackValid (int track) noexcept { return track >= 0 && track < kNumTracks; }
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

    // Arquivo corrompido / editado à mão: força BPM dentro dos limites.
    const int currentBpm = getBpm();
    const int clampedBpm = juce::jlimit (kMinBpm, kMaxBpm, currentBpm);
    if (clampedBpm != currentBpm)
        tree.setProperty (IDs::bpm, clampedBpm, nullptr);

    auto sequencer = tree.getOrCreateChildWithName (IDs::sequencer, nullptr);

    while (sequencer.getNumChildren() < kNumTracks)
    {
        juce::ValueTree newTrack (IDs::track);
        newTrack.setProperty (IDs::name, kDefaultTrackNames[sequencer.getNumChildren()], nullptr);
        newTrack.setProperty (IDs::mask, 0, nullptr);
        sequencer.appendChild (newTrack, nullptr);
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

// ---------------------------------------------------------------------------
// Step sequencer
// ---------------------------------------------------------------------------
juce::String ProjectState::getTrackName (int track) const
{
    if (! isTrackValid (track))
        return {};

    return tree.getChildWithName (IDs::sequencer).getChild (track).getProperty (IDs::name, "").toString();
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

// ---------------------------------------------------------------------------
// Persistência (Card 0.2)
// ---------------------------------------------------------------------------
std::unique_ptr<juce::XmlElement> ProjectState::createXml() const
{
    return tree.createXml();
}

bool ProjectState::loadFromXml (const juce::XmlElement& xml)
{
    auto loaded = juce::ValueTree::fromXml (xml);

    if (! loaded.isValid() || loaded.getType() != IDs::project)
        return false;

    // Copia para a MESMA árvore raiz: todos os listeners (processador, UI)
    // continuam registrados e são notificados das mudanças.
    tree.copyPropertiesAndChildrenFrom (loaded, nullptr);
    ensureStructure();

    undoManager.clearUndoHistory();
    markSaved();
    return true;
}

bool ProjectState::saveToFile (const juce::File& file)
{
    const auto xml = createXml();

    if (xml == nullptr || ! file.replaceWithText (xml->toString()))
        return false;

    markSaved();
    return true;
}

bool ProjectState::loadFromFile (const juce::File& file)
{
    if (const auto xml = juce::parseXML (file))
        return loadFromXml (*xml);

    return false;
}

// ---------------------------------------------------------------------------
// Estado "sujo" (isModified)
// ---------------------------------------------------------------------------
void ProjectState::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
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
