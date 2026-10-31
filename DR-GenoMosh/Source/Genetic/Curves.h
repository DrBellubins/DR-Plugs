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

inline float clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
inline float clampf(float value, float lowValue, float highValue)
{
    return value < lowValue ? lowValue : (value > highValue ? highValue : value);
}
inline float lerpf(float fromValue, float toValue, float mix)
{
    return fromValue + (toValue - fromValue) * mix;
}

inline float mapGene(const GeneInfo& geneInfo, float normalized)
{
    const float value = clamp01(normalized);

    switch (geneInfo.curve)
    {
        case Curve::Exp:
            return geneInfo.minValue * std::pow(geneInfo.maxValue / geneInfo.minValue, value);

        case Curve::Squared:
            return geneInfo.minValue + value * value * (geneInfo.maxValue - geneInfo.minValue);

        case Curve::Cubic:
            return geneInfo.minValue + value * value * value * (geneInfo.maxValue - geneInfo.minValue);

        case Curve::Bipolar:
        {
            const float bipolar = value * 2.0f - 1.0f;
            return bipolar >= 0.0f ? bipolar * geneInfo.maxValue : bipolar * -geneInfo.minValue;
        }

        case Curve::Linear:
        default:
            return geneInfo.minValue + value * (geneInfo.maxValue - geneInfo.minValue);
    }
}

inline float mapGene(std::size_t geneIndex, float normalized)
{
    return mapGene(kGenes[geneIndex], normalized);
}

inline float unmapGene(const GeneInfo& geneInfo, float value)
{
    switch (geneInfo.curve)
    {
        case Curve::Exp:
            return std::log(value / geneInfo.minValue) / std::log(geneInfo.maxValue / geneInfo.minValue);

        case Curve::Squared:
            return std::sqrt((value - geneInfo.minValue) / (geneInfo.maxValue - geneInfo.minValue));

        case Curve::Cubic:
            return std::cbrt((value - geneInfo.minValue) / (geneInfo.maxValue - geneInfo.minValue));

        case Curve::Bipolar:
            return value >= 0.0f ? 0.5f + 0.5f * value / geneInfo.maxValue
                                 : 0.5f + 0.5f * value / -geneInfo.minValue;

        case Curve::Linear:
        default:
            return (value - geneInfo.minValue) / (geneInfo.maxValue - geneInfo.minValue);
    }
}

inline float unmapGene(std::size_t geneIndex, float value) { return unmapGene(kGenes[geneIndex], value); }

//==============================================================================
// Boundary handling (design doc 5.2 step 4).
//
// The document suggests reflecting rather than clamping so genes do not pile up
// at 0 and 1. Reflection is continuous but not monotonic, which makes growing a
// branch visibly "bounce". softCompress is used instead: displacement beyond the
// gene's soft range is compressed exponentially towards the hard limit.
//
// Properties (all required by 5.1):
//   * identity for values at or below softHigh when moving up, so distance 0
//     returns the seed exactly and small distances are exact;
//   * strictly monotonic in the value, so growing a branch is a smooth morph;
//   * never reaches 0 or 1 unless the seed is already there.
inline float softCompress(float value, float anchor, float softLow, float softHigh)
{
    if (value > softHigh)
    {
        const float base = softHigh > anchor ? softHigh : anchor;
        const float span = 1.0f - base;

        if (span <= 1.0e-6f)
            return base;

        return base + span * (1.0f - std::exp(-(value - base) / span));
    }

    if (value < softLow)
    {
        const float base = softLow < anchor ? softLow : anchor;
        const float span = base;

        if (span <= 1.0e-6f)
            return base;

        return base - span * (1.0f - std::exp(-(base - value) / span));
    }

    return value;
}

/// The alternative mentioned in the design document, kept for A/B testing.
inline float reflect01(float value)
{
    float reflected = value;

    while (reflected < 0.0f || reflected > 1.0f)
    {
        if (reflected < 0.0f)
            reflected = -reflected;

        if (reflected > 1.0f)
            reflected = 2.0f - reflected;
    }

    return reflected;
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
inline float snapToRatio(float semitones, float atonality)
{
    static const float kRatioTable[] = { -36.0f,  -24.0f, -19.017f, -17.004f, -12.0f, -10.0f, -8.844f,
                                         -7.02f,  -5.0f,  -4.0f,    -3.863f,  0.0f,   3.863f, 4.0f,
                                         5.0f,    7.02f,  8.844f,   10.0f,    12.0f,  15.0f,  17.004f,
                                         19.017f, 22.0f,  24.0f,    27.863f,  31.02f, 34.0f,  36.0f };

    constexpr int kNumRatios = static_cast<int>(sizeof(kRatioTable) / sizeof(kRatioTable[0]));

    const float furthestRatio = kRatioTable[kNumRatios - 1];
    const float clampedValue = clampf(semitones, -furthestRatio, furthestRatio);

    int bestIndex = 0;
    float bestDistance = 1.0e9f;

    for (int entryIndex = 0; entryIndex < kNumRatios; ++entryIndex)
    {
        const float distance = std::abs(kRatioTable[entryIndex] - clampedValue);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = entryIndex;
        }
    }

    // Note the order: the *snapped* value is the atonality == 0 end. Blending
    // the other way round makes every branch sing a random pitch when the
    // atonality macro is closed, which is the opposite of 5.5.
    return lerpf(kRatioTable[bestIndex], clampedValue, clamp01(atonality));
}

} // namespace gmosh
