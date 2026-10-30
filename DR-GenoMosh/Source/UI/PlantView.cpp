#include "PlantView.h"

#include <algorithm>
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
                        int activeBranch,
                        float wheelValue)
{
    // Do not copy the snapshot: at 50 genes it is ~8 kB and this runs on the
    // editor timer while a drag is in progress. The drawing only needs the per
    // branch fingerprint below, and only when the plant was actually rebuilt.
    if (newSnapshot.revision != snapshotRevision)
    {
        snapshotRevision = newSnapshot.revision;

        for (int b = 0; b < kNumBranches; ++b)
        {
            const BranchDirection& direction = newSnapshot.direction (b, 0);
            BranchVisual& v = visuals[static_cast<std::size_t> (b)];
            v.hash = direction.hash;
            v.hue = direction.hue;
            v.saturation = direction.dir[g_flt_freq];
            v.brightness = direction.dir[g_a_form];
        }
    }

    lengths = newLengths;
    active = activeBranch;
    wheel = wheelValue;
    repaint();
}

//==============================================================================
juce::Colour PlantView::branchColour (const BranchVisual& visual, float hueShift)
{
    // Hue comes from the branch itself, saturation/brightness from a couple of
    // timbre genes, so the plant is a fingerprint of the sound (design doc 12).
    const float hue = std::fmod (visual.hue + hueShift, 1.0f);
    const float saturation = 0.45f + 0.45f * visual.saturation;
    const float brightness = 0.55f + 0.40f * visual.brightness;
    return juce::Colour::fromHSV (hue, juce::jlimit (0.2f, 0.95f, saturation),
                                  juce::jlimit (0.3f, 1.0f, brightness), 1.0f);
}

//==============================================================================
// The tree is grown in unit space, so its shape is independent of how far the
// branch has grown. Measuring the farthest control point and scaling afterwards
// is exactly equivalent to drawing the same tree at the growth radius, and it is
// what guarantees the plant stays inside the ring for any length or wheel value.
void PlantView::growTree (std::vector<Twig>& out, juce::Point<float> from, float angle,
                          float length, int depth, Rng& rng, float& reach) const
{
    if (depth <= 0 || length < 0.02f)
        return;

    const float bend = rng.gaussianClamped (0.22f);
    const auto dir   = juce::Point<float> (std::cos (angle), std::sin (angle));
    const auto curve = juce::Point<float> (std::cos (angle + bend * 0.5f),
                                           std::sin (angle + bend * 0.5f));

    const auto to      = from + dir * length;
    const auto control = from + curve * length * 0.5f;

    out.push_back ({ from, control, to, static_cast<float> (depth) });

    // A quadratic Bezier lives inside the convex hull of its three points, so
    // bounding those bounds the stroke.
    reach = std::max (reach, to.getDistanceFromOrigin());
    reach = std::max (reach, control.getDistanceFromOrigin());

    const int children = depth > 2 ? 2 : (rng.chance (0.6f) ? 2 : 1);
    for (int i = 0; i < children; ++i)
    {
        const float side = i == 0 ? -1.0f : 1.0f;
        const float spread = 0.30f + rng.uniform() * 0.45f;
        growTree (out, to, angle + side * spread + bend * 0.3f,
                  length * (0.48f + rng.uniform() * 0.16f), depth - 1, rng, reach);
    }
}

void PlantView::drawPlant (juce::Graphics& g, juce::Point<float> centre, float angle,
                           float targetReach, uint64_t branchHash,
                           const juce::Colour& colour) const
{
    std::vector<Twig> twigs;
    twigs.reserve (32);

    float reach = 0.0f;
    Rng rng (branchHash);
    growTree (twigs, { 0.0f, 0.0f }, angle, 1.0f, 4, rng, reach);

    if (twigs.empty() || reach < 1.0e-4f)
        return;

    const float scale = targetReach / reach;

    for (const auto& twig : twigs)
    {
        const auto from = centre + twig.from * scale;
        const auto ctl  = centre + twig.control * scale;
        const auto to   = centre + twig.to * scale;

        juce::Path stem;
        stem.startNewSubPath (from);
        stem.quadraticTo (ctl, to);

        g.setColour (colour.withAlpha (0.35f + 0.55f * twig.depth / 4.0f));
        g.strokePath (stem, juce::PathStrokeType (twig.depth * 0.7f));
    }
}

//==============================================================================
void PlantView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const float radius = ringRadius();

    g.setColour (juce::Colour (0xff15181a));
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (juce::Colour (0xff2a2f33));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 2.0f);

    const float segmentWidth = juce::MathConstants<float>::twoPi / static_cast<float> (kNumBranches);
    const double now = juce::Time::getMillisecondCounterHiRes();

    for (int slot = 0; slot < kNumBranches; ++slot)
    {
        const float angle = juce::MathConstants<float>::halfPi
                          + static_cast<float> (slot) * segmentWidth;

        const int branch = slot;   // the ring is the keyboard: pitch class -> branch
        const bool isActive = branch == active;

        const float len = juce::jlimit (0.0f, 1.0f, lengths[static_cast<std::size_t> (branch)]);
        const float effective = len * juce::jlimit (0.0f, 1.0f, wheel);

        const BranchVisual& visual = visuals[static_cast<std::size_t> (branch)];
        const juce::Colour colour = branchColour (visual, 0.0f);

        //--------------------------------------------------------------------
        // The plant itself, scaled to the growth radius of this branch.
        // Gentle idle sway so the plant is never completely still.
        const float sway = std::sin (static_cast<float> (now * 0.0004)
                                   + static_cast<float> (slot)) * 0.02f;

        const float target = radius * (kMinReach + (kMaxReach - kMinReach) * effective);
        drawPlant (g, centre, angle + sway, target, visual.hash, colour);

        //--------------------------------------------------------------------
        // Growth handle on the ring, at the radius the branch reaches when the
        // wheel is fully open.
        const float handleRadius = radius * (0.30f + (kMaxReach - 0.30f) * len);
        const float hx = centre.x + std::cos (angle) * handleRadius;
        const float hy = centre.y + std::sin (angle) * handleRadius;

        g.setColour (isActive ? juce::Colours::white : colour);
        g.fillEllipse (hx - 5.0f, hy - 5.0f, 10.0f, 10.0f);

        //--------------------------------------------------------------------
        // Pitch class label on the ring. This ring is the keyboard and stays
        // put; Rotation moves the plant underneath it.
        const float lx = centre.x + std::cos (angle) * (radius + 18.0f);
        const float ly = centre.y + std::sin (angle) * (radius + 18.0f);

        g.setColour (isActive ? juce::Colours::white : juce::Colours::lightgrey);
        g.setFont (13.0f);
        g.drawText (noteNames[static_cast<std::size_t> (slot)],
                    juce::Rectangle<float> (lx - 20.0f, ly - 10.0f, 40.0f, 20.0f),
                    juce::Justification::centred, false);
    }

    // Seed marker at the centre.
    g.setColour (juce::Colours::lightgoldenrodyellow.withAlpha (0.9f));
    g.fillEllipse (centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
}

//==============================================================================
// Note gating. One note at a time is ever held by this view, which is what
// keeps a stuck note from surviving a drag.
//==============================================================================
void PlantView::startNote (int midiNote)
{
    if (heldNote == midiNote)
        return;

    endNote();
    heldNote = midiNote;

    if (onPlayNote != nullptr)
        onPlayNote (midiNote, 0.85f);

    startTimer (30);   // watches for the pointer going still
}

void PlantView::endNote()
{
    stopTimer();

    if (heldNote < 0)
        return;

    if (onPlayNote != nullptr)
        onPlayNote (heldNote, 0.0f);   // velocity 0 means note off

    heldNote = -1;
}

void PlantView::releaseAllNotes()
{
    endNote();
}

//==============================================================================
// While a branch is being dragged the note follows the movement only: stop
// moving and the note is released, move again and it re articulates.
void PlantView::timerCallback()
{
    if (heldNote < 0 || ! dragging || pressedSlot < 0)
        return;

    if (juce::Time::getMillisecondCounterHiRes() - lastMoveMs < kIdleReleaseMs)
        return;

    endNote();
}

//==============================================================================
int PlantView::slotAt (juce::Point<float> position) const
{
    const auto centre = getLocalBounds().getCentre().toFloat();
    const float dx = position.x - centre.x;
    const float dy = position.y - centre.y;

    if (std::sqrt (dx * dx + dy * dy) > ringRadius() * 1.25f)
        return -1;

    const float segmentWidth = juce::MathConstants<float>::twoPi / static_cast<float> (kNumBranches);
    const float angle = std::atan2 (dy, dx);
    const float relative = angle - juce::MathConstants<float>::halfPi;

    int slot = static_cast<int> (std::round (relative / segmentWidth));
    slot %= kNumBranches;
    if (slot < 0)
        slot += kNumBranches;

    return slot;
}

//==============================================================================
void PlantView::mouseDown (const juce::MouseEvent& event)
{
    pressedSlot = slotAt (event.position);
    pressedBranch = pressedSlot;
    dragging = false;
    lastPointer = event.position;
    lastMoveMs = juce::Time::getMillisecondCounterHiRes();

    if (pressedBranch < 0)
        return;

    const auto centre = getLocalBounds().getCentre().toFloat();
    const float pdx = event.position.x - centre.x;
    const float pdy = event.position.y - centre.y;
    pressedDistance = std::sqrt (pdx * pdx + pdy * pdy) / ringRadius();

    if (event.mods.isRightButtonDown())
    {
        if (onPlantBranch != nullptr)
            onPlantBranch (pressedBranch);
        pressedSlot = -1;
        pressedBranch = -1;
        return;
    }

    startNote (noteForSlot (pressedSlot));
}

void PlantView::mouseDrag (const juce::MouseEvent& event)
{
    if (pressedBranch < 0)
        return;

    const auto centre = getLocalBounds().getCentre().toFloat();
    const float dx = event.position.x - centre.x;
    const float dy = event.position.y - centre.y;
    const float distance = std::sqrt (dx * dx + dy * dy) / ringRadius();

    if (std::abs (distance - pressedDistance) > kDragThreshold)
        dragging = true;

    const float mx = event.position.x - lastPointer.x;
    const float my = event.position.y - lastPointer.y;

    if (std::sqrt (mx * mx + my * my) > kMoveThresholdPx)
    {
        lastPointer = event.position;
        lastMoveMs = juce::Time::getMillisecondCounterHiRes();

        // The note was let go while the pointer was still: bring it back.
        if (dragging)
            startNote (noteForSlot (pressedSlot));
    }

    if (dragging && branchParams[static_cast<std::size_t> (pressedBranch)] != nullptr)
    {
        branchParams[static_cast<std::size_t> (pressedBranch)]
            ->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, distance));
    }
}

void PlantView::mouseUp (const juce::MouseEvent&)
{
    // Always release, dragged or not: this is the note off that used to be
    // skipped whenever a drag had happened.
    endNote();

    pressedSlot = -1;
    pressedBranch = -1;
    dragging = false;
}

} // namespace gmosh
