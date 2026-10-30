#include "Mutation.h"
#include "Curves.h"

#include <algorithm>

namespace gmosh
{
//==============================================================================
BranchDirection buildBranchDirection (uint64_t seedId, int branch, int variant,
                                      const MutationParams& mp)
{
    BranchDirection out;
    out.hash = branchHash (seedId, branch, variant);

    Rng rng (out.hash);

    // 1. One coherent direction per gene group, plus a per gene direction.
    float groupDir[GrpCount];
    for (int g = 0; g < GrpCount; ++g)
        groupDir[g] = rng.gaussianClamped (1.0f);

    const float coherence = clamp01 (mp.coherence);

    for (std::size_t i = 0; i < kNumGenes; ++i)
    {
        const GeneInfo& gi = kGenes[i];

        if ((gi.flags & kFlagNever) != 0)
        {
            // 5.7.6: the output surface of the instrument never moves under
            // growth. Both halves of the mechanism (a zero direction here and an
            // explicit skip in applyDistance) are held by testGrowthExempt.
            out.dir[i] = 0.0f;
            continue;
        }

        const float indiv = rng.gaussianClamped (1.0f);
        const float dir = lerpf (indiv, groupDir[gi.group], coherence);

        // 3. Per gene sensitivity is where the taste lives (design doc 5.2).
        float amount = dir * gi.sensitivity * mp.globalScale;

        // Atonality ceiling: a branch may not walk into pitch territory beyond
        // what the plant is allowed to have. The resolved genome is gated again
        // (applyAtonality), this only keeps the direction honest.
        if ((gi.flags & kFlagPitch) != 0)
            amount *= clamp01 (mp.atonality);

        out.dir[i] = amount;
    }

    // UI fingerprint: hue from a couple of timbre genes plus the hash, so two
    // different seeds always look different in the plant view.
    const float hueSeed = static_cast<float> (out.hash % 100000u) / 100000.0f;
    out.hue = hueSeed;

    return out;
}

//==============================================================================
namespace
{
    /// How far a pitch capable gene may sit from its neutral point, in gene
    /// space. Taken from the table (4.3) rather than inferred from the curve:
    /// a bipolar gene's neutral is the middle, an amount gene's is its end.
    float neutralSpan (const GeneInfo& gi)
    {
        return gi.neutral > 0.0f ? std::max (gi.neutral, 1.0f - gi.neutral) : 1.0f;
    }
}

void applyAtonality (Genome& genome, float atonality)
{
    const float at = clamp01 (atonality);

    for (std::size_t i = 0; i < kNumGenes; ++i)
    {
        const GeneInfo& gi = kGenes[i];

        if ((gi.flags & kFlagPitch) == 0)
            continue;

        const float neutral = gi.neutral;
        const float span = neutralSpan (gi);

        genome[i] = clampf (genome[i], neutral - span * at, neutral + span * at);
    }
}

//==============================================================================
void applyCouplings (Genome& g)
{
    // The couplings of 5.6, as one line edits in gene space. A few of these
    // still reason about their genes with Rev 1 meanings (vol_dcy as a time,
    // flt_freq as absolute hertz); Docs/Rev3-Alignment-Plan.md 6.3 tracks that,
    // and M3/M5/M7 recalibrate them once those destinations exist.
    // --- resonance safety (5.6): no self oscillation, and less resonance the
    //     higher the cutoff sits, which is where a resonant filter screams.
    const float qCap = 0.55f + 0.45f * (1.0f - g[g_flt_freq]);
    g[g_flt_q] = std::min (g[g_flt_q], qCap * (1.0f - 0.5f * g[g_saturate]));

    // --- a low cutoff plus downward filter mod is just silence.
    if (g[g_flt_freq] < 0.25f && g[g_flt_mod] < 0.0f)
        g[g_flt_mod] = -g[g_flt_mod] * 0.4f;

    // --- never let both sources go fully dull at once: if neither oscillator
    //     has any noise and both waveforms sit at pure sine, open one up.
    const float energy = std::max (std::max (g[g_a_form], g[g_b_form]),
                                   std::max (g[g_a_noise], g[g_b_noise]));
    if (energy < 0.12f)
        g[g_a_form] = 0.12f + (0.12f - energy) * 0.5f;

    // --- ...and never let the amplitude envelope close everything off: a
    //     branch that cannot sound is not a branch, it is a bug. The sustain
    //     gate of 4.2 (< 0.5 = no sustain) lands with M1, which is also where
    //     this gets a duration aware version.
    const float ampOpen = std::max (g[g_vol_sus], 0.35f * (1.0f - g[g_vol_dcy]));
    if (ampOpen < 0.08f)
        g[g_vol_sus] = 0.18f;

    // --- keep the two filters from stacking their resonance into a howl.
    if (g[g_flt_sep] > 0.5f && g[g_flt_q] > 0.5f)
        g[g_flt_sep] = 0.5f + (1.0f - g[g_flt_q]) * 0.5f * g[g_flt_sep];

    for (std::size_t i = 0; i < kNumGenes; ++i)
        g[i] = clamp01 (g[i]);
}

//==============================================================================
Genome applyDistance (const Genome& seed, const BranchDirection& direction,
                      float distance, const MutationParams& mp)
{
    Genome out = seed;

    const float d = clamp01 (distance);
    if (d <= 0.0f)
        return out; // 5.1.3: distance 0 returns the seed exactly.

    // 2. Non linear distance response.
    const float step = std::pow (d, mp.distanceExponent);
    const float pull = clamp01 (mp.pullToNeutral);

    for (std::size_t i = 0; i < kNumGenes; ++i)
    {
        const GeneInfo& gi = kGenes[i];

        // 5.7.6: growth exempt genes are copied through untouched. Their
        // direction is already zero, but the neutral pull below is not, and a
        // room size or master EQ that drifts as a branch grows is exactly the
        // "disruption of the audio" the reference's own help text warns about.
        if ((gi.flags & kFlagNever) != 0)
        {
            out[i] = seed[i];
            continue;
        }

        // 3. Displacement, biased toward the gene's neutral value (5.2). The
        //    bias is proportional to the same distance term as the direction, so
        //    it cannot move anything at distance 0 and stays idempotent under
        //    re-planting.
        const float x = seed[i]
                      + direction.dir[i] * step
                      + pull * (gi.neutral - seed[i]) * step;

        // 4. Boundary handling: compress past the soft range, never clamp.
        out[i] = softCompress (x, seed[i], gi.softMin, gi.softMax);
    }

    // 6. Couplings and guard rails.
    applyCouplings (out);
    return out;
}

//==============================================================================
BranchTraits resolveTraits (uint64_t seedHash, float distance, const MutationParams& mp)
{
    BranchTraits traits;

    const float p = std::pow (clamp01 (distance), 3.0f) * mp.structuralRate;
    if (p <= 0.0f)
        return traits;

    // One fixed random threshold per trait: monotonic in distance, so a trait
    // flips once as a branch grows and stays flipped.
    Rng rng (hash::combine (seedHash, 0x5EED1E5ull));

    const float t0 = rng.uniform();
    const float t1 = rng.uniform();
    const float t2 = rng.uniform();
    const float t3 = rng.uniform();

    if (p > t0) traits.filterMode = static_cast<unsigned char> (rng.integer (3));
    if (p > t1) traits.fmDirection = static_cast<unsigned char> (rng.integer (2));
    if (p > t2) traits.modSource = static_cast<unsigned char> (rng.integer (3));
    if (p > t3) traits.oscBMode = static_cast<unsigned char> (rng.integer (3));

    return traits;
}

//==============================================================================
float safetyScore (const Genome& g)
{
    // Rough "will this be an audible, non horrible sound" score. Used only to
    // pick between the kNumDirections variants of a branch (5.6 silence
    // rejection); it is deliberately cheap and coarse.
    float score = 1.0f;

    // Needs to be able to sound at all.
    score -= 0.9f * std::max (0.0f, 0.10f - std::max (g[g_vol_sus],
                                                      0.4f * (1.0f - g[g_vol_dcy])));

    // Needs something to hear: source energy and an open enough filter.
    const float energy = std::max (std::max (g[g_a_form], g[g_a_noise]),
                                   std::max (g[g_b_form], g[g_b_noise]));
    score -= 0.6f * std::max (0.0f, 0.15f - energy);
    score -= 0.8f * std::max (0.0f, 0.12f - g[g_flt_freq]);

    // Penalise the usual offenders.
    score -= 0.5f * std::max (0.0f, g[g_flt_q] - 0.75f);
    score -= 0.4f * std::max (0.0f, g[g_a_noise] + g[g_b_noise] - 1.1f);
    score -= 0.3f * std::max (0.0f, g[g_fm_amt] - 0.8f);
    score -= 0.3f * std::max (0.0f, g[g_vol_atk] - 0.85f); // never a swell instead of a note

    return score;
}

//==============================================================================
float loudnessCompensation (const Genome& g)
{
    // Estimate where the branch sits level wise and pull it back towards the
    // seed (5.6). Sources of extra loudness: resonance, FM sidebands, sub,
    // noise and drive.
    const float boost = 1.0f
                      + 1.7f * g[g_flt_q] * g[g_flt_q]
                      + 0.8f * g[g_fm_amt]
                      + 0.7f * g[g_sub_am]
                      + 0.5f * std::max (g[g_a_noise], g[g_b_noise])
                      + 0.9f * g[g_saturate];

    return 1.0f / std::sqrt (boost);
}

//==============================================================================
namespace
{
    /// Spread of the seed randomiser, in gene space. The reference's randomise
    /// has "a spread of about a quarter of the range" (5.7 / 11); gaussianClamped
    /// is bounded by its scale, so this is also the maximum excursion.
    constexpr float kSeedSpread = 0.25f;
}

void randomizeGenome (Genome& genome, Rng& rng, float atonality)
{
    const float at = clamp01 (atonality);

    for (std::size_t i = 0; i < kNumGenes; ++i)
    {
        const GeneInfo& gi = kGenes[i];

        // 5.7.6: the growth exempt genes are the player's output surface, not
        // part of the plant, so a fresh seed leaves every one of them neutral.
        if ((gi.flags & kFlagNever) != 0 || gi.sensitivity <= 0.0f)
        {
            genome[i] = gi.neutral;
            continue;
        }

        // 5.7.1 + 11: draw around the value where nothing happens, reflect at
        // the rails rather than pile up on them, then honour the gene's soft
        // range as an extra clamp. Centring on neutral is what makes "mostly
        // nothing" the most likely outcome of a random seed, which is the whole
        // point of the neutral convention.
        float v = gi.neutral + rng.gaussianClamped (kSeedSpread);
        v = reflect01 (v);
        v = clampf (v, gi.softMin, gi.softMax);

        // Pitch capable genes are drawn inside the atonality band rather than
        // clipped into it afterwards, so a low atonality seed still has a
        // spread of tunings rather than a pile on the ceiling.
        if ((gi.flags & kFlagPitch) != 0)
            v = gi.neutral + (v - gi.neutral) * at;

        genome[i] = clamp01 (v);
    }

    // A fresh seed still has to be able to hold a note. vol_sus is read through
    // its activation gate from M1 (below 0.5 the note does not sustain at all),
    // so this floor sits above the gate rather than at the old half way point;
    // M3 replaces it with a duration aware version of the same check, and the
    // reverb line that used to sit here went away on its own when rvb_mix's
    // neutral became "dry" (5.7.1).
    genome[g_vol_sus] = std::max (genome[g_vol_sus], 0.68f);

    applyCouplings (genome);
}

} // namespace gmosh
