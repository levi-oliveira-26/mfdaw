#include "UI/StepGridComponent.h"

#include "UI/Theme.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace mf
{
StepGridComponent::StepGridComponent (ProjectState& stateToUse, SampleManager& sampleManagerToUse,
                                      double targetSampleRateToUse)
    : state (stateToUse),
      sampleManager (sampleManagerToUse),
      targetSampleRate (targetSampleRateToUse)
{
    swingSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    swingSlider.setRange (0.0, 100.0, 1.0);
    swingSlider.setValue (state.getSwingAmount() * 100.0,
                          juce::dontSendNotification);
    swingSlider.setTextValueSuffix ("%");
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 20);
    swingSlider.addListener (this);
    addAndMakeVisible (swingSlider);

    state.getTree().addListener (this);
}

StepGridComponent::~StepGridComponent()
{
    swingSlider.removeListener (this);
    state.getTree().removeListener (this);
}

void StepGridComponent::resized()
{
    swingSlider.setBounds (getWidth() - 88, 4, 76, 44);
}

void StepGridComponent::sliderValueChanged (juce::Slider* slider)
{
    if (slider == &swingSlider)
        state.setSwingAmount (static_cast<float> (swingSlider.getValue() / 100.0));
}

void StepGridComponent::setPlayheadStep (int step)
{
    if (step != playheadStep)
    {
        playheadStep = step;
        repaint();
    }
}

void StepGridComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    repaint();
}

juce::Rectangle<float> StepGridComponent::getGridBounds() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (kLabelWidth)
        .withTrimmedTop (52.0f);
}

juce::Rectangle<float> StepGridComponent::getCellBounds (int track, int step) const
{
    const auto grid  = getGridBounds();
    const float cellW = grid.getWidth()  / static_cast<float> (kNumSteps);
    const float cellH = grid.getHeight() / static_cast<float> (kNumTracks);

    return { grid.getX() + cellW * static_cast<float> (step),
             grid.getY() + cellH * static_cast<float> (track),
             cellW, cellH };
}

void StepGridComponent::paint (juce::Graphics& g)
{
    using namespace theme;

    const auto rhythm        = col (kBrandRhythm);
    const auto gridBackground = rhythm.withAlpha (0.08f);
    const auto groupA        = col (kSurfaceElevated);
    const auto groupB        = col (kSurfaceElevated).interpolatedWith (col (kSurfaceBase), 0.55f);
    const auto active        = rhythm.withAlpha (0.92f);
    const auto blockDivider  = rhythm.withAlpha (0.32f);
    const auto playhead      = col (kTextPrimary).withAlpha (0.16f);
    const auto grid          = getGridBounds();

    g.fillAll (col (kSurfaceBase));
    g.setColour (col (kTextPrimary));
    g.setFont (makeFont (13.0f, true));
    g.drawText ("Swing", juce::Rectangle<float> (getWidth() - 88.0f, 0.0f, 76.0f, 16.0f),
                juce::Justification::centred);
    g.setColour (gridBackground);
    g.fillRect (grid);

    g.setFont (makeFont (14.0f, true));

    for (int track = 0; track < kNumTracks; ++track)
    {
        const auto firstCell = getCellBounds (track, 0);
        g.setColour (col (kTextMuted));
        g.drawText (state.getTrackName (track),
                    juce::Rectangle<float> (0.0f, firstCell.getY(), kLabelWidth - 8.0f, firstCell.getHeight()),
                    juce::Justification::centredLeft);

        for (int step = 0; step < kNumSteps; ++step)
        {
            const auto cell = getCellBounds (track, step).reduced (2.0f);
            const bool firstStepInBlock = (step % 4) == 0;

            g.setColour (state.isStepActive (track, step) ? active
                                                          : (firstStepInBlock ? groupA.withAlpha (0.82f)
                                                                              : groupB.withAlpha (0.82f)));
            g.fillRect (cell);

            if (state.isStepActive (track, step))
            {
                const float velocity = static_cast<float> (state.getStepVelocity (track, step)) / 127.0f;
                const float barWidth = juce::jmax (1.0f, cell.getWidth() * 0.12f);
                const float barHeight = juce::jlimit (0.0f, cell.getHeight(), cell.getHeight() * velocity);

                g.setColour (col (kTextPrimary).withAlpha (0.72f));
                g.fillRect (cell.getRight() - barWidth - 1.0f,
                            cell.getBottom() - barHeight,
                            barWidth,
                            barHeight);
            }

            if (step == playheadStep)
            {
                g.setColour (playhead);
                g.fillRect (cell);
            }
        }
    }

    const float cellWidth = grid.getWidth() / static_cast<float> (kNumSteps);
    for (int block = 1; block < 4; ++block)
    {
        const float x = grid.getX() + cellWidth * static_cast<float> (block * 4);
        g.setColour (blockDivider);
        g.fillRect (x - 0.75f, grid.getY(), 1.5f, grid.getHeight());
    }
}

void StepGridComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto grid = getGridBounds();

    if (e.position.x >= 0.0f && e.position.x < kLabelWidth && e.position.y >= grid.getY()
        && e.position.y < grid.getBottom())
    {
        const int track = juce::jlimit (0, kNumTracks - 1,
            static_cast<int> ((e.position.y - grid.getY()) / (grid.getHeight() / kNumTracks)));

        selectedTrack = track;
        fileChooser = std::make_unique<juce::FileChooser> ("Choose sample", juce::File {},
                                                            "*.wav;*.mp3;*.aif;*.aiff");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, track] (const juce::FileChooser& chooser)
            {
                const auto file = chooser.getResult();
                if (! file.existsAsFile())
                    return;

                SampleManager& manager = sampleManager;
                const double outputSampleRate = targetSampleRate;
                juce::Thread::launch ([&manager, track, file, outputSampleRate]
                {
                    juce::AudioFormatManager formats;
                    formats.registerBasicFormats();

                    std::unique_ptr<juce::AudioFormatReader> reader (
                        formats.createReaderFor (file));
                    if (reader == nullptr)
                        return;

                    juce::AudioBuffer<float> decoded (
                        juce::jmax (1, static_cast<int> (reader->numChannels)),
                        juce::jmax (1, static_cast<int> (reader->lengthInSamples)));
                    if (! reader->read (&decoded, 0, decoded.getNumSamples(), 0, true, true))
                        return;

                    juce::AudioBuffer<float> converted (decoded);
                    if (std::abs (reader->sampleRate - outputSampleRate) > 1.0 && outputSampleRate > 0.0)
                    {
                        const double ratio = reader->sampleRate / outputSampleRate;
                        const int convertedLength = juce::jmax (1,
                            static_cast<int> (std::ceil ((decoded.getNumSamples() - 1) / ratio) + 1));
                        juce::AudioBuffer<float> resampled (decoded.getNumChannels(), convertedLength);

                        for (int channel = 0; channel < resampled.getNumChannels(); ++channel)
                            for (int sample = 0; sample < convertedLength; ++sample)
                            {
                                const double sourcePosition = sample * ratio;
                                const int sourceIndex = juce::jlimit (0, decoded.getNumSamples() - 1,
                                                                       static_cast<int> (sourcePosition));
                                const int nextIndex = juce::jmin (decoded.getNumSamples() - 1, sourceIndex + 1);
                                const float fraction = static_cast<float> (sourcePosition - sourceIndex);
                                const float a = decoded.getSample (channel, sourceIndex);
                                const float b = decoded.getSample (channel, nextIndex);
                                resampled.setSample (channel, sample, a + fraction * (b - a));
                            }

                        converted = std::move (resampled);
                    }

                    manager.setSample (track,
                        std::make_shared<const AudioSample> (std::move (converted)));
                });
            });
        return;
    }

    if (! grid.contains (e.position))
        return;

    const float cellW = grid.getWidth()  / static_cast<float> (kNumSteps);
    const float cellH = grid.getHeight() / static_cast<float> (kNumTracks);

    const int step  = juce::jlimit (0, kNumSteps - 1, static_cast<int> ((e.position.x - grid.getX()) / cellW));
    const int track = juce::jlimit (0, kNumTracks - 1, static_cast<int> ((e.position.y - grid.getY()) / cellH));

    if (e.mods.isShiftDown())
    {
        velocityTrack = track;
        velocityStep = step;

        const auto cell = getCellBounds (track, step);
        const float normalized = juce::jlimit (0.0f, 1.0f,
                                               (cell.getBottom() - e.position.y) / cell.getHeight());
        state.setStepVelocity (track, step, static_cast<int> (normalized * 127.0f));
        return;
    }

    state.toggleStep (track, step);
    repaint();
}

void StepGridComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (velocityTrack < 0 || velocityStep < 0 || ! e.mods.isShiftDown())
        return;

    const auto cell = getCellBounds (velocityTrack, velocityStep);
    const float normalized = juce::jlimit (0.0f, 1.0f,
                                           (cell.getBottom() - e.position.y) / cell.getHeight());
    const int velocity = juce::jlimit (0, 127,
                                       static_cast<int> (normalized * 127.0f));

    if (state.getStepVelocity (velocityTrack, velocityStep) != velocity)
        state.setStepVelocity (velocityTrack, velocityStep, velocity);
}

void StepGridComponent::mouseUp (const juce::MouseEvent&)
{
    velocityTrack = -1;
    velocityStep = -1;
}

} // namespace mf
