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

//==============================================================================
// The neutral value (design doc 5.7.1).
//
// Every gene has one value where nothing happens: the middle for a bipolar
// control, the quiet end for an amount, and for a handful of genes the reference
//'s own factory value (osc_mix 0.7937, flt_type 0.6667, flt_freq 0.75). Those
// are recorded in the table, so "nothing happens" is one lookup rather than an
// assumption about 0.5 that would be wrong for three quarters of the table.
//
// neutralOf is the seed of the other two curation mechanisms: growth pulls
// toward it (5.2 step 3, Mutation.cpp) and random seeds are drawn around it
// (5.7.1 / 11). A genome made entirely of neutrals is the instrument at rest;
// Tests/EngineTests.cpp (testMappingNeutralIsRest) holds the mapping layer to
// exactly that, which is M1's "a gene at neutral changes nothing audible" gate.
inline float neutralOf (const GeneInfo& g) { return g.neutral; }
inline float neutralOf (int gene)          { return kGenes[gene].neutral; }

/// The instrument at rest: every gene at its neutral value.
inline Genome neutralGenome()
{
    Genome out{};
    for (std::size_t i = 0; i < kNumGenes; ++i)
        out[i] = kGenes[i].neutral;
    return out;
}

/// True when this gene documents an activation dead zone (5.7.3). A gene with
/// the default 0,1 pair has no gate and passes through unchanged.
inline bool hasGate (const GeneInfo& g) { return g.gateLow > 0.0f || g.gateHigh < 1.0f; }

//==============================================================================
// Activation gates and dead zones (design doc 5.7.3).
//
// "A large share of genes are off over part of their range: vol_sus (<0.5 =
// none), vol_fade (>0.75 only), rvb_atk (>0.66 only), lfo_dly (>0.5 only),
// mod_vel (<0.1 = none), mod_sh (only very high values). This is the cheapest
// possible way to make 'mostly nothing' the most likely outcome of a random
// value."
//
// gated() is a *gene space* edit: it moves the part of the range the reference
// leaves dead out of the way, and stretches what is left across the whole of
// [0,1], so a gene at gateLow maps to its quiet end and gateHigh to its loud
// end. For vol_sus that is exactly the documented max(0, 2(v-0.5)): the dead
// zone and the quiet end are the same value. It is deliberately not applied to
// genomes: the genome is the truth, the gate is how it is *read*, and baking a
// gate into a genome would make growth move values the player can see.
//
// gateOpen() is the other half: "is this feature switched on at all". Values
// that map to a rate rather than a level (mod_sh's S&H rate) need the switch,
// because their quiet end is a fast rate, not an off.
//
// The lower boundary belongs to the dead zone. That is not a detail: for every
// gene the reference documents a gate for, gateLow *is* the neutral value, so
// "at neutral" and "switched off" have to be the same answer or 5.7.1 and
// 5.7.3 contradict each other. The top boundary belongs to the gene.
inline bool gateOpen (const GeneInfo& g, float norm)
{
    const float v = clamp01 (norm);
    return ! hasGate (g) || (v > g.gateLow && v <= g.gateHigh);
}

inline float gated (const GeneInfo& g, float norm)
{
    const float v = clamp01 (norm);
    if (! hasGate (g))
        return v;

    // The table keeps gateLow < gateHigh (asserted by testTableMatchesDoc), so
    // the division below cannot see a zero span.
    return clamp01 ((v - g.gateLow) / (g.gateHigh - g.gateLow));
}

/// The pair every level destination uses: the gene stretched across what its
/// gate leaves alive. Below gateLow that lands on the quiet end of the curve
/// (sustain 0, no fade, no velocity scaling), which is the documented
/// behaviour. A destination whose "off" is *not* the quiet end - mod_sh's S&H
/// rate, where the quiet end is the fastest rate - asks gateOpen() instead.
inline float mappedGated (const GeneInfo& g, float norm)
{
    return mapGene (g, gated (g, norm));
}

/// What a gene contributes when its gate is shut: its curve at gene space 0.
inline float quietEnd (const GeneInfo& g) { return mapGene (g, 0.0f); }

//==============================================================================
// Soft rails instead of hard clamps (design doc 5.7.4).
//
// "flt_kf: nothing below 0.25, exact above 0.75, smooth in between. Prefer this
// for any 'amount of tracking' control." The rails are a smoothstep across the
// gene's documented rail pair, i.e. a 0..1 strength: 0 at or below lo, 1 at or
// above hi. Multiply a signed amount by it to soften both ends of a tracking
// control without ever clamping it.
inline float smoothStep (float t)
{
    const float x = clamp01 (t);
    return x * x * (3.0f - 2.0f * x);
}

inline float softRails (float norm, float lo, float hi)
{
    if (! std::isfinite (lo) || ! std::isfinite (hi) || hi <= lo)
        return clamp01 (norm);   // no rails documented
    return smoothStep ((clamp01 (norm) - lo) / (hi - lo));
}

//==============================================================================
// The non-linear, musical curves of 5.7.7.
//
// osc_mix is documented as 50/50 at about 0.79, and the reference's factory
// state sits at 0.7937 - not at the middle of its range. The equal power pan
// law itself is symmetric, so the gene is warped in front of it: 0 stays A
// alone, 1 stays B alone, and the documented neutral lands on the equal power
// point. The result is in "mix space" (u), where 0.5 is equal power; Voice
// consumes u directly, and mix_mod adds to u rather than to the gene value.
inline constexpr float kMixGeneCentre = 0.7937f;   ///< osc_mix's documented neutral

inline float equalPowerMixSpace (float geneValue, float centre = kMixGeneCentre)
{
    const float v = clamp01 (geneValue);
    const float c = clampf (centre, 0.05f, 0.95f);
    return v <= c ? 0.5f * v / c
                  : 0.5f + 0.5f * (v - c) / (1.0f - c);
}

//==============================================================================

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
// Note: nothing in the engine calls this since M1. applyAtonality used to snap
// every kFlagRatio gene unconditionally, which made b_freq a ratio-table value
// at *every* atonality - the opposite of 5.5, which asks for a blend. The snap
// comes back in M8 as part of R7's converge(), on b_freq only, blended by the
// same atonality that bounds the pitch genes (Tests: the free-at-1 assertion in
// testAtonalityCeiling). It stays here, and tested, so that step is a one line
// change rather than a rewrite.
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
