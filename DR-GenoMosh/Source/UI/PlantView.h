#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Genetic/PlantEngine.h"

namespace gmosh
//==============================================================================
// The plant view: the twelve branches around a ring of pitch classes
// (design doc section 12).
//
//   * the ring shows which note plays which branch, and rotates with Rotation;
//   * each branch is drawn with a length proportional to its growth;
//   * dragging outwards from a segment grows that branch;
//   * clicking a segment plays the note it maps to.
//
// The drawing is a deterministic fingerprint of the genome, not an attempt at
// botany: same seed, same plant.
//==============================================================================
{
class PlantView : public juce::Component
{
public:
    struct BranchControl
    {
        juce::AudioParameterFloat* length = nullptr;
    };

    void setParameters (std::array<juce::AudioParameterFloat*, kNumBranches> lengthParameters,
                        juce::AudioParameterFloat* rotationParameter)
    {
        branchParams = lengthParameters;
        rotation = rotationParameter;
    }

    /// Called from the editor's timer with the values currently in force.
    void update (const PlantSnapshot& snapshot,
                 const std::array<float, kNumBranches>& lengths,
                 float rotationValue,
                 int activeBranch,
                 float wheelValue);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (int note, float velocity)> onPlayNote;
    std::function<void (int)> onPlantBranch;

private:
    int segmentAt (juce::Point<float> position) const;
    float ringRadius() const { return juce::jmin (getWidth(), getHeight()) * 0.42f; }
    void drawBranch (juce::Graphics&, float cx, float cy, float angle,
                     float length, int depth, Rng&, const juce::Colour&) const;
    static juce::Colour branchColour (const BranchDirection& direction, float hueShift);

    std::array<juce::AudioParameterFloat*, kNumBranches> branchParams {};
    juce::AudioParameterFloat* rotation = nullptr;

    PlantSnapshot snapshot;
    std::array<float, kNumBranches> lengths {};
    float rotationValue = 0.0f;
    float wheel = 1.0f;
    int active = -1;
    int pressedSegment = -1;
    bool dragging = false;
    float pressedDistance = 0.0f;

    static const char* const noteNames[12];
};

} // namespace gmosh
