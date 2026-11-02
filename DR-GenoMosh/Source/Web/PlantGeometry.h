#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "../Genetic/PlantEngine.h"

namespace gmosh
//==============================================================================
// The authoritative picture of the plant.
//
// The web UI draws and animates the plant, but it must never invent its shape:
// the tree here is the same deterministic growth the previous native PlantView
// drew, in unit space (root stem length 1, origin at the seed), together with
// the reach that unit tree measures. The front end scales those strokes to the
// ring, which is what guarantees a branch of a given length reaches exactly the
// handle that length asks for, at any window size, and that the plant on screen
// cannot drift away from the plant the audio thread is resolving.
//==============================================================================
{
class PlantGeometry
{
  public:
    /// { branches: [{ hue, sat, bri, reach, strokes: [[x0,y0,cx,cy,x1,y1,depth]] }] }
    static juce::var toJson(const PlantSnapshot& snapshot);

  private:
    struct Stroke
    {
        float x0, y0, cx, cy, x1, y1, depth;
    };

    struct BranchShape
    {
        std::vector<Stroke> strokes;
        float hue = 0.0f;
        float saturation = 0.0f;
        float brightness = 0.0f;
        float reach = 1.0f;
    };

    PlantGeometry() = delete;

    /// Recursive, unit space. `reach` tracks the farthest point from the seed.
    static void growTree(std::vector<Stroke>& outStrokes, float fromX, float fromY, float angle, float length,
                         int depth, Rng& randomGenerator, float& reach);

    /// Hue comes from the branch itself, saturation/brightness from a couple of
    /// timbre genes, so the plant is a fingerprint of the sound (design doc 12).
    static void branchColour(const BranchDirection& direction, float& hue, float& saturation,
                             float& brightness);

    static BranchShape shapeFor(const BranchDirection& direction);
};

} // namespace gmosh
