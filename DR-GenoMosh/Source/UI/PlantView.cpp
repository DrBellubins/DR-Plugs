#include "PlantView.h"

#include <cmath>
#include <functional>

namespace gmosh
{
const char* const PlantView::noteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

//==============================================================================
void PlantView::update (const PlantSnapshot& newSnapshot,
                        const std::array<float, kNumBranches>& newLengths,
                        float newRotation,
                        int activeBranch,
                        float wheelValue)
{
    snapshot = newSnapshot;
    lengths = newLengths;
    rotationValue = newRotation;
    active = activeBranch;
    wheel = wheelValue;
    repaint();
}

//==============================================================================
juce::Colour PlantView::branchColour (const BranchDirection& direction, float hueShift)
{
    // Hue comes from the branch itself, saturation/brightness from a couple of
    // timbre genes, so the plant is a fingerprint of the sound (design doc 12).
    const float hue = std::fmod (direction.hue + hueShift, 1.0f);
    const float saturation = 0.45f + 0.45f * direction.dir[g_flt_freq];
    const float brightness = 0.55f + 0.40f * direction.dir[g_a_form];
    return juce::Colour::fromHSV (hue, juce::jlimit (0.2f, 0.95f, saturation),
                                  juce::jlimit (0.3f, 1.0f, brightness), 1.0f);
}

//==============================================================================
void PlantView::drawBranch (juce::Graphics& g, float cx, float cy, float angle,
                            float length, int depth, Rng& rng, const juce::Colour& colour) const
{
    if (depth <= 0 || length < 2.0f)
        return;

    const float bend = rng.gaussianClamped (0.22f);
    const float endX = cx + std::cos (angle) * length;
    const float endY = cy + std::sin (angle) * length;
    const float midX = cx + std::cos (angle + bend * 0.5f) * length * 0.5f;
    const float midY = cy + std::sin (angle + bend * 0.5f) * length * 0.5f;

    juce::Path stem;
    stem.startNewSubPath (cx, cy);
    stem.quadraticTo (midX, midY, endX, endY);

    g.setColour (colour.withAlpha (0.35f + 0.55f * static_cast<float> (depth) / 4.0f));
    g.strokePath (stem, juce::PathStrokeType (static_cast<float> (depth) * 0.7f));

    const int children = depth > 2 ? 2 : (rng.chance (0.6f) ? 2 : 1);
    for (int i = 0; i < children; ++i)
    {
        const float side = i == 0 ? -1.0f : 1.0f;
        const float spread = 0.30f + rng.uniform() * 0.45f;
        drawBranch (g, endX, endY, angle + side * spread + bend * 0.3f,
                    length * (0.48f + rng.uniform() * 0.16f), depth - 1, rng, colour);
    }
}

//==============================================================================
void PlantView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const float radius = ringRadius();

    g.setColour (juce::Colour (0xff15181a));
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (juce::Colour (0xff2a2f33));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);

    const float segmentWidth = juce::MathConstants<float>::twoPi / static_cast<float> (kNumBranches);

    for (int b = 0; b < kNumBranches; ++b)
    {
        const float angle = juce::MathConstants<float>::halfPi
                          + static_cast<float> (b) * segmentWidth;

        const int pitchClass = ((b - static_cast<int> (std::floor (rotationValue))) % kNumBranches + kNumBranches) % kNumBranches;

        const float len = juce::jlimit (0.0f, 1.0f, lengths[static_cast<std::size_t> (b)]);
        const float effective = len * juce::jlimit (0.0f, 1.0f, wheel);

        const BranchDirection& direction = snapshot.direction (b, 0);
        const juce::Colour colour = branchColour (direction, 0.0f);

        //--------------------------------------------------------------------
        // The plant itself.
        Rng rng (direction.hash);

        // Gentle idle sway so the plant is never completely still.
        const float sway = std::sin (static_cast<float> (juce::Time::getMillisecondCounterHiRes() * 0.0004)
                                   + static_cast<float> (b)) * 0.02f;

        const float stemLength = radius * (0.10f + 0.72f * effective);
        drawBranch (g, centre.x, centre.y, angle + sway, stemLength, 4, rng, colour);

        // Seed marker at the centre.
        g.setColour (colour.withAlpha (0.9f));
        g.fillEllipse (centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);

        //--------------------------------------------------------------------
        // Growth handle on the ring.
        const float handleRadius = radius * (0.30f + 0.68f * len);
        const float hx = centre.x + std::cos (angle) * handleRadius;
        const float hy = centre.y + std::sin (angle) * handleRadius;

        g.setColour (b == active ? juce::Colours::white : colour);
        g.fillEllipse (hx - 5.0f, hy - 5.0f, 10.0f, 10.0f);

        //--------------------------------------------------------------------
        // Pitch class label on the ring.
        const float lx = centre.x + std::cos (angle) * (radius + 18.0f);
        const float ly = centre.y + std::sin (angle) * (radius + 18.0f);

        g.setColour (b == active ? juce::Colours::white : juce::Colours::lightgrey);
        g.setFont (13.0f);
        g.drawText (noteNames[static_cast<std::size_t> (pitchClass)],
                    juce::Rectangle<float> (lx - 20.0f, ly - 10.0f, 40.0f, 20.0f),
                    juce::Justification::centred, false);
    }
}

//==============================================================================
int PlantView::segmentAt (juce::Point<float> position) const
{
    const auto centre = getLocalBounds().getCentre().toFloat();
    const float dx = position.x - centre.x;
    const float dy = position.y - centre.y;

    if (std::sqrt (dx * dx + dy * dy) > ringRadius() * 1.25f)
        return -1;

    const float segmentWidth = juce::MathConstants<float>::twoPi / static_cast<float> (kNumBranches);
    const float angle = std::atan2 (dy, dx);
    float relative = angle - juce::MathConstants<float>::halfPi;

    int segment = static_cast<int> (std::round (relative / segmentWidth));
    segment %= kNumBranches;
    if (segment < 0)
        segment += kNumBranches;

    return segment;
}

//==============================================================================
void PlantView::mouseDown (const juce::MouseEvent& event)
{
    pressedSegment = segmentAt (event.position);
    dragging = false;

    if (pressedSegment < 0)
        return;

    const auto centre = getLocalBounds().getCentre().toFloat();
    const float pdx = event.position.x - centre.x;
    const float pdy = event.position.y - centre.y;
    pressedDistance = std::sqrt (pdx * pdx + pdy * pdy) / ringRadius();

    if (event.mods.isRightButtonDown() && onPlantBranch != nullptr)
    {
        onPlantBranch (pressedSegment);
        pressedSegment = -1;
        return;
    }

    const int pitchClass = ((pressedSegment - static_cast<int> (std::floor (rotationValue))) % kNumBranches + kNumBranches) % kNumBranches;

    if (onPlayNote != nullptr)
        onPlayNote (60 + pitchClass, 0.85f);
}

void PlantView::mouseDrag (const juce::MouseEvent& event)
{
    if (pressedSegment < 0)
        return;

    const auto centre = getLocalBounds().getCentre().toFloat();
    const float dx = event.position.x - centre.x;
    const float dy = event.position.y - centre.y;
    const float distance = std::sqrt (dx * dx + dy * dy) / ringRadius();

    if (std::abs (distance - pressedDistance) > 0.03f)
        dragging = true;

    if (dragging && branchParams[static_cast<std::size_t> (pressedSegment)] != nullptr)
    {
        branchParams[static_cast<std::size_t> (pressedSegment)]
            ->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, distance));
    }
}

void PlantView::mouseUp (const juce::MouseEvent&)
{
    if (pressedSegment >= 0 && ! dragging && onPlayNote != nullptr)
    {
        const int pitchClass = ((pressedSegment - static_cast<int> (std::floor (rotationValue))) % kNumBranches + kNumBranches) % kNumBranches;
        onPlayNote (60 + pitchClass, 0.0f);
    }

    pressedSegment = -1;
    dragging = false;
}

} // namespace gmosh
