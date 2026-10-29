#include "Core/TrackModel.h"

#include "Core/FaderScale.h"
#include "Core/ProjectState.h"

namespace mf
{
std::unique_ptr<TrackModel> TrackModel::create (const juce::ValueTree& node, juce::UndoManager* undoManager)
{
    if (! node.isValid() || node.getType() != IDs::playlistTrack)
        return nullptr;

    const int rawType = static_cast<int> (node.getProperty (IDs::trackType, -1));
    if (! isValidTrackType (rawType))
        return nullptr;

    switch (static_cast<TrackType> (rawType))
    {
        case TrackType::Drum:    return std::make_unique<DrumTrack> (node, undoManager);
        case TrackType::Harmony: return std::make_unique<HarmonyTrack> (node, undoManager);
        case TrackType::Melody:  return std::make_unique<MelodyTrack> (node, undoManager);
        case TrackType::Record:  return std::make_unique<RecordTrack> (node, undoManager);
        case TrackType::Sample:  return std::make_unique<SampleTrack> (node, undoManager);
    }

    return nullptr;
}

juce::ValueTree TrackModel::createNode (TrackType type, int trackId, const juce::String& name)
{
    juce::ValueTree node (IDs::playlistTrack);
    node.setProperty (IDs::trackId, trackId, nullptr);
    node.setProperty (IDs::trackType, static_cast<int> (type), nullptr);
    node.setProperty (IDs::name, name, nullptr);
    node.setProperty (IDs::gain, static_cast<double> (fader::kDefaultPosition), nullptr);
    return node;
}

int TrackModel::getId() const
{
    return static_cast<int> (node.getProperty (IDs::trackId, -1));
}

juce::String TrackModel::getName() const
{
    return node.getProperty (IDs::name, "").toString();
}

void TrackModel::setName (const juce::String& newName)
{
    const auto trimmed = newName.trim();
    if (trimmed.isEmpty())
        return;

    node.setProperty (IDs::name, trimmed, undoManager);
}

float TrackModel::getGain() const
{
    return fader::clampPosition (static_cast<float> (node.getProperty (IDs::gain,
                                                                       static_cast<double> (fader::kDefaultPosition))));
}

void TrackModel::setGain (float faderPosition)
{
    node.setProperty (IDs::gain, static_cast<double> (fader::clampPosition (faderPosition)), undoManager);
}

} // namespace mf
