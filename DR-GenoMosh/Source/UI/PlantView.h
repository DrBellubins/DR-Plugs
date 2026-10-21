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
// Note gating follows the reference instrument: a clicked segment sounds while
// the button is held, and a *dragged* segment sounds only while the branch is
// actually being changed, so the note is released as soon as the pointer stops
// moving (see timerCallback).
//
// The drawing is a deterministic fingerprint of the genome, not an attempt at
// botany: same seed, same plant.
//==============================================================================
{
class PlantView : public juce::Component
              , private juce::Timer
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

    /// Ends a note this view started, e.g. when the editor closes.
    void releaseAllNotes();

    /// onPlayNote is called with a velocity for note on, and with 0 for note off.
    std::function<void (int note, float velocity)> onPlayNote;
    std::function<void (int)> onPlantBranch;

private:
    void timerCallback() override;

    int pitchClassForSegment (int segment) const;
    int noteForSegment (int segment) const;
    void startNote (int midiNote);
    void endNote();

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
    int heldNote = -1;
    bool dragging = false;
    float pressedDistance = 0.0f;
    juce::Point<float> lastPointer;
    double lastMoveMs = 0.0;

    /// A dragged branch stops sounding this long after the pointer stops moving.
    static constexpr double kIdleReleaseMs = 250.0;
    /// Pointer movement that counts as "still changing the branch", in pixels.
    static constexpr float kMoveThresholdPx = 2.0f;
    /// Ring distance that turns a click into a drag.
    static constexpr float kDragThreshold = 0.03f;

    static const char* const noteNames[12];
};

} // namespace gmosh
