#pragma once

#include "GeneTable.h"

#include <cmath>

namespace gmosh
{
//==============================================================================
// Normalized <-> real world mapping (design doc 4.3) and boundary handling.
//
// The genome only ever stores [0,1]; every real value is derived from it here.
// That keeps mutation uniform in "gene space" and makes the ranges tunable data.
//==============================================================================

inline float clamp01 (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float clampf  (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf   (float a, float b, float t) { return a + (b - a) * t; }

//==============================================================================
// Deliberate quantisation (design doc 5.7.5).
//
// A few genes are not continuous on purpose: a_freq is a seven step octave grid
// and flt_type selects a topology. Quantisation happens in gene space, so the
// stored genome stays a [0,1] float and every step is a multiple of
// 1 / (steps - 1). Note that this makes "deviation grows with distance" false
// in *audio* space across a step (5.4); the tests assert it in gene space.
inline float quantiseNorm (float v, unsigned int steps)
{
    if (steps <= 1)
        return clamp01 (v);

    const float n = static_cast<float> (steps - 1);
    return std::round (clamp01 (v) * n) / n;
}

inline float mapGene (const GeneInfo& g, float norm)
{
    const float v = clamp01 (norm);

    switch (g.curve)
    {
        case Curve::Exp:
            return g.minValue * std::pow (g.maxValue / g.minValue, v);

        case Curve::Squared:
            return g.minValue + v * v * (g.maxValue - g.minValue);

        case Curve::Stepped:
            return g.minValue + quantiseNorm (v, g.steps) * (g.maxValue - g.minValue);

        case Curve::Bipolar:
        {
            const float t = v * 2.0f - 1.0f;
            return t >= 0.0f ? t * g.maxValue : t * -g.minValue;
        }

        case Curve::Linear:
        default:
            return g.minValue + v * (g.maxValue - g.minValue);
    }
}

inline float mapGene (int gene, float norm) { return mapGene (kGenes[gene], norm); }

inline float unmapGene (const GeneInfo& g, float value)
{
    switch (g.curve)
    {
        case Curve::Exp:
            return std::log (value / g.minValue) / std::log (g.maxValue / g.minValue);

        case Curve::Squared:
            return std::sqrt ((value - g.minValue) / (g.maxValue - g.minValue));

        case Curve::Stepped:
            // The inverse of a quantised curve is itself quantised: mapping back
            // gives the step that produced the value, not a free float.
            return quantiseNorm ((value - g.minValue) / (g.maxValue - g.minValue), g.steps);

        case Curve::Bipolar:
            return value >= 0.0f
                 ? 0.5f + 0.5f * value / g.maxValue
                 : 0.5f + 0.5f * value / -g.minValue;

        case Curve::Linear:
        default:
            return (value - g.minValue) / (g.maxValue - g.minValue);
    }
}

inline float unmapGene (int gene, float value) { return unmapGene (kGenes[gene], value); }

//==============================================================================
// Boundary handling (design doc 5.2 step 4).
//
// The document suggests reflecting rather than clamping so genes do not pile up
// at 0 and 1. Reflection is continuous but not monotonic, which makes growing a
// branch visibly "bounce". softCompress is used instead: displacement beyond the
// gene's soft range is compressed exponentially towards the hard limit.
//
// Properties (all required by 5.1):
//   * identity for x <= softHi when moving up, so distance 0 returns the seed
//     exactly and small distances are exact;
//   * strictly monotonic in x, so growing a branch is a smooth morph;
//   * never reaches 0 or 1 unless the seed is already there.
inline float softCompress (float x, float anchor, float softLo, float softHi)
{
    if (x > softHi)
    {
        const float base = softHi > anchor ? softHi : anchor;
        const float span = 1.0f - base;
        if (span <= 1.0e-6f)
            return base;
        return base + span * (1.0f - std::exp (-(x - base) / span));
    }

    if (x < softLo)
    {
        const float base = softLo < anchor ? softLo : anchor;
        const float span = base;
        if (span <= 1.0e-6f)
            return base;
        return base - span * (1.0f - std::exp (-(base - x) / span));
    }

    return x;
}

/// The alternative mentioned in the design document, kept for A/B testing.
inline float reflect01 (float x)
{
    while (x < 0.0f || x > 1.0f)
    {
        if (x < 0.0f) x = -x;
        if (x > 1.0f) x = 2.0f - x;
    }
    return x;
}

//==============================================================================
// Ratio snapping (design doc 5.5).
//
// Oscillator frequency genes are free semitone offsets; atonality blends between
// the nearest consonant ratio and the raw value, so random patches are tuneful
// by default and inharmonic only when asked for:
//
//   atonality 0 -> the nearest table entry (harmonic, consonant)
//   atonality 1 -> the raw gene value (free, inharmonic)
inline float snapToRatio (float semitones, float atonality)
{
    static const float ratios[] = {
        -36.0f, -24.0f, -19.017f, -17.004f, -12.0f, -10.0f, -8.844f, -7.02f,
        -5.0f, -4.0f, -3.863f, 0.0f, 3.863f, 4.0f, 5.0f, 7.02f, 8.844f,
        10.0f, 12.0f, 15.0f, 17.004f, 19.017f, 22.0f, 24.0f, 27.863f, 31.02f, 34.0f, 36.0f
    };
    constexpr int numRatios = static_cast<int> (sizeof (ratios) / sizeof (ratios[0]));

    const float limit = ratios[numRatios - 1];
    float v = clampf (semitones, -limit, limit);

    int best = 0;
    float bestDist = 1.0e9f;
    for (int i = 0; i < numRatios; ++i)
    {
        const float d = std::abs (ratios[i] - v);
        if (d < bestDist) { bestDist = d; best = i; }
    }

    // Note the order: the *snapped* value is the atonality == 0 end. Blending
    // the other way round makes every branch sing a random pitch when the
    // atonality macro is closed, which is the opposite of 5.5.
    return lerpf (ratios[best], v, clamp01 (atonality));
}

} // namespace gmosh
