#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "../Genetic/PlantEngine.h"

namespace gmosh
//==============================================================================
// The plant view: the twelve branches around a ring of pitch classes
// (design doc section 12).
//
//   * the ring is the keyboard: slot i always shows pitch class i, and that is
//     the branch the note plays (exactly branchForNote on the audio side);
//   * each branch is drawn with a length proportional to its growth, and the
//     whole tree is normalised so it can never grow past the ring;
//   * dragging outwards from a slot grows the branch under the pointer;
//   * clicking a slot plays the note it shows.
//
// Note gating follows the reference instrument: a clicked slot sounds while the
// button is held, and a *dragged* slot sounds only while the branch is actually
// being changed, so the note is released as soon as the pointer stops moving
// (see timerCallback).
//
// The drawing is a deterministic fingerprint of the genome, not an attempt at
// botany: same seed, same plant.
//==============================================================================
{
class PlantView : public juce::Component
                , private juce::Timer
{
public:
    void setParameters (std::array<juce::AudioParameterFloat*, kNumBranches> lengthParameters)
    {
        branchParams = lengthParameters;
    }

    /// Called from the editor's timer with the values currently in force.
    void update (const PlantSnapshot& snapshot,
                 const std::array<float, kNumBranches>& lengths,
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
    /// One stroke of a branch, in "unit" space (root stem length 1, origin at
    /// the seed). The tree is built in unit space, measured, then scaled to the
    /// growth target, which is what keeps it inside the ring without clamping
    /// the plant's shape.
    struct Twig
    {
        juce::Point<float> from, control, to;
        float depth = 0.0f;
    };

    void timerCallback() override;

    /// Everything the drawing needs from one branch of the plant. A
    /// PlantSnapshot is ~8 kB (50 genes x 12 branches x 3 directions), and this
    /// view is refreshed from the editor timer, so only these four values are
    /// kept, and only when the plant's revision changes.
    struct BranchVisual
    {
        uint64_t hash = 0;      ///< the tree shape is grown from this
        float hue = 0.0f;
        float saturation = 0.5f;
        float brightness = 0.5f;
    };

    /// A ring slot plays the branch of its own pitch class, which is exactly
    /// what branchForNote does on the audio side.
    int noteForSlot (int slot) const { return 60 + slot; }
    void startNote (int midiNote);
    void endNote();

    int slotAt (juce::Point<float> position) const;
    float ringRadius() const { return juce::jmin (getWidth(), getHeight()) * 0.42f; }

    /// Recursive, unit space. `reach` tracks the farthest point from the seed.
    void growTree (std::vector<Twig>& out, juce::Point<float> from, float angle,
                   float length, int depth, Rng& rng, float& reach) const;
    void drawPlant (juce::Graphics&, juce::Point<float> centre, float angle,
                    float targetReach, uint64_t branchHash, const juce::Colour&) const;
    static juce::Colour branchColour (const BranchVisual& visual, float hueShift);

    std::array<juce::AudioParameterFloat*, kNumBranches> branchParams {};

    std::array<BranchVisual, kNumBranches> visuals {};
    int snapshotRevision = -1;
    std::array<float, kNumBranches> lengths {};
    float wheel = 1.0f;
    int active = -1;
    int pressedSlot = -1;
    int pressedBranch = -1;
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

    /// Longest reach of a plant, as a fraction of the ring radius. The growth
    /// handle sits at the same radius for the same length, so a fully open
    /// branch reaches its handle and nothing ever leaves the circle.
    static constexpr float kMaxReach = 0.92f;
    static constexpr float kMinReach = 0.10f;

    static const char* const noteNames[12];
};

} // namespace gmosh
