#include "PlantGeometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gmosh
{
//==============================================================================
void PlantGeometry::growTree(std::vector<Stroke>& outStrokes, float fromX, float fromY, float angle,
                             float length, int depth, Rng& randomGenerator, float& reach)
{
    if (depth <= 0 || length < 0.02f)
        return;

    const float bend = randomGenerator.gaussianClamped(0.22f);
    const float toX = fromX + std::cos(angle) * length;
    const float toY = fromY + std::sin(angle) * length;
    const float controlAngle = angle + bend * 0.5f;
    const float controlX = fromX + std::cos(controlAngle) * length * 0.5f;
    const float controlY = fromY + std::sin(controlAngle) * length * 0.5f;

    outStrokes.push_back({ fromX, fromY, controlX, controlY, toX, toY, static_cast<float>(depth) });

    // A quadratic Bezier lives inside the convex hull of its three points, so
    // bounding those bounds the stroke.
    reach = std::max(reach, std::sqrt(toX * toX + toY * toY));
    reach = std::max(reach, std::sqrt(controlX * controlX + controlY * controlY));

    const int children = depth > 2 ? 2 : (randomGenerator.chance(0.6f) ? 2 : 1);

    for (int childIndex = 0; childIndex < children; ++childIndex)
    {
        const float sideSign = childIndex == 0 ? -1.0f : 1.0f;
        const float spread = 0.30f + randomGenerator.uniform() * 0.45f;
        growTree(outStrokes, toX, toY, angle + sideSign * spread + bend * 0.3f,
                 length * (0.48f + randomGenerator.uniform() * 0.16f), depth - 1, randomGenerator, reach);
    }
}

//==============================================================================
void PlantGeometry::branchColour(const BranchDirection& direction, float& hue, float& saturation,
                                 float& brightness)
{
    hue = std::fmod(direction.hue, 1.0f);
    saturation = 0.45f + 0.45f * direction.displacement[g_flt_freq];
    brightness = 0.55f + 0.40f * direction.displacement[g_a_form];
    saturation = juce::jlimit(0.2f, 0.95f, saturation);
    brightness = juce::jlimit(0.3f, 1.0f, brightness);
}

//==============================================================================
PlantGeometry::BranchShape PlantGeometry::shapeFor(const BranchDirection& direction)
{
    BranchShape shape;
    float reach = 0.0f;
    Rng randomGenerator(direction.hash);
    growTree(shape.strokes, 0.0f, 0.0f, 0.0f, 1.0f, 4, randomGenerator, reach);

    // The starting angle is the slot's own direction, so the unit tree is
    // grown along +x and the front end rotates it into place. Reach is measured
    // from the seed, which is rotation independent.
    shape.reach = std::max(reach, 1.0e-3f);
    branchColour(direction, shape.hue, shape.saturation, shape.brightness);
    return shape;
}

//==============================================================================
juce::var PlantGeometry::toJson(const PlantSnapshot& snapshot)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("seedId", juce::String(snapshot.seedId));

    juce::Array<juce::var> branches;

    for (int branchIndex = 0; branchIndex < kNumBranches; ++branchIndex)
    {
        const BranchShape shape = shapeFor(snapshot.direction(branchIndex, 0));

        auto* branch = new juce::DynamicObject();
        branch->setProperty("hue", shape.hue);
        branch->setProperty("sat", shape.saturation);
        branch->setProperty("bri", shape.brightness);
        branch->setProperty("reach", shape.reach);

        juce::Array<juce::var> strokes;

        for (const auto& stroke : shape.strokes)
        {
            juce::Array<juce::var> values;
            values.add(static_cast<double>(stroke.x0));
            values.add(static_cast<double>(stroke.y0));
            values.add(static_cast<double>(stroke.cx));
            values.add(static_cast<double>(stroke.cy));
            values.add(static_cast<double>(stroke.x1));
            values.add(static_cast<double>(stroke.y1));
            values.add(static_cast<double>(stroke.depth));
            strokes.add(juce::var(values));
        }

        branch->setProperty("strokes", juce::var(strokes));
        branches.add(juce::var(branch));
    }

    root->setProperty("branches", juce::var(branches));
    return juce::var(root);
}

} // namespace gmosh
