#include "Mutation.h"
#include "Curves.h"

#include <algorithm>

namespace gmosh
{
//==============================================================================
BranchDirection buildBranchDirection(uint64_t seedId, int branchIndex, int variantIndex,
                                     const MutationParams& params)
{
    BranchDirection result;
    result.hash = branchHash(seedId, branchIndex, variantIndex);

    Rng randomGenerator(result.hash);

    // 1. One coherent direction per gene group, plus a per gene direction.
    float groupDirection[GrpCount];

    for (int groupIndex = 0; groupIndex < GrpCount; ++groupIndex)
        groupDirection[groupIndex] = randomGenerator.gaussianClamped(1.0f);

    const float coherence = clamp01(params.coherence);

    for (std::size_t geneIndex = 0; geneIndex < kNumGenes; ++geneIndex)
    {
        const GeneInfo& geneInfo = kGenes[geneIndex];

        if ((geneInfo.flags & kFlagNever) != 0)
        {
            result.displacement[geneIndex] = 0.0f;
            continue;
        }

        const float individualDirection = randomGenerator.gaussianClamped(1.0f);
        const float blendedDirection = lerpf(individualDirection, groupDirection[geneInfo.group], coherence);

        // 3. Per gene sensitivity is where the taste lives (design doc 5.2).
        float geneAmount = blendedDirection * geneInfo.sensitivity * params.globalScale;

        // Atonality ceiling: a branch may not walk into pitch territory beyond
        // what the plant is allowed to have. The resolved genome is gated again
        // (applyAtonality), this only keeps the direction honest.
        if ((geneInfo.flags & kFlagPitch) != 0)
            geneAmount *= clamp01(params.atonality);

        result.displacement[geneIndex] = geneAmount;
    }

    // UI fingerprint: hue from a couple of timbre genes plus the hash, so two
    // different seeds always look different in the plant view.
    const float hueSeed = static_cast<float>(result.hash % 100000u) / 100000.0f;
    result.hue = hueSeed;

    return result;
}

//==============================================================================
namespace
{
/// Where a gene sits when it has no effect on pitch at all: the middle for
/// bipolar genes, zero for the unipolar amounts.
float pitchNeutral(const GeneInfo& geneInfo) { return (geneInfo.flags & kFlagRatio) != 0 ? 0.5f : 0.0f; }
} // namespace

void applyAtonality(Genome& genome, float atonality)
{
    const float ceiling = clamp01(atonality);

    for (std::size_t geneIndex = 0; geneIndex < kNumGenes; ++geneIndex)
    {
        const GeneInfo& geneInfo = kGenes[geneIndex];

        if ((geneInfo.flags & kFlagPitch) == 0)
            continue;

        const float neutral = pitchNeutral(geneInfo);
        const float span = neutral > 0.0f ? 0.5f : 1.0f;

        genome[geneIndex] = clampf(genome[geneIndex], neutral - span * ceiling, neutral + span * ceiling);

        if ((geneInfo.flags & kFlagRatio) != 0)
        {
            // 5.5: the oscillator offsets live on the ratio ladder, so a branch
            // is harmonically related to the key that plays it whatever the
            // atonality is; atonality decides how far up the ladder it may go.
            // Snapping is a fixed point (a table entry snaps to itself), so the
            // whole function stays idempotent.
            genome[geneIndex] = unmapGene(geneInfo, snapToRatio(mapGene(geneInfo, genome[geneIndex]), 0.0f));
        }
    }
}

//==============================================================================
void applyCouplings(Genome& genome)
{
    // --- resonance safety (5.6): no self oscillation, and less resonance the
    //     higher the cutoff sits, which is where a resonant filter screams.
    const float resonanceCeiling = 0.55f + 0.45f * (1.0f - genome[g_flt_freq]);
    genome[g_flt_q] = std::min(genome[g_flt_q], resonanceCeiling * (1.0f - 0.5f * genome[g_sat_drive]));

    // --- a low cutoff plus downward filter mod is just silence.
    if (genome[g_flt_freq] < 0.25f && genome[g_flt_mod] < 0.0f)
        genome[g_flt_mod] = -genome[g_flt_mod] * 0.4f;

    // --- never let both sources go fully dull at once: if neither oscillator
    //     has any noise and both waveforms sit at pure sine, open one up.
    const float energy = std::max(std::max(genome[g_a_form], genome[g_b_form]),
                                  std::max(genome[g_a_noise], genome[g_b_noise]));

    if (energy < 0.12f)
        genome[g_a_form] = 0.12f + (0.12f - energy) * 0.5f;

    // --- ...and never let the amplitude envelope close everything off: a
    //     branch that cannot sound is not a branch, it is a bug.
    const float ampOpen = std::max(genome[g_vol_sus], 0.35f * (1.0f - genome[g_vol_dcy]));

    if (ampOpen < 0.08f)
        genome[g_vol_sus] = 0.18f;

    // --- keep the two filters from stacking their resonance into a howl.
    if (genome[g_flt_sep] > 0.5f && genome[g_flt_q] > 0.5f)
        genome[g_flt_sep] = 0.5f + (1.0f - genome[g_flt_q]) * 0.5f * genome[g_flt_sep];

    for (std::size_t geneIndex = 0; geneIndex < kNumGenes; ++geneIndex)
        genome[geneIndex] = clamp01(genome[geneIndex]);
}

//==============================================================================
Genome applyDistance(const Genome& seed, const BranchDirection& direction, float distance,
                     const MutationParams& params)
{
    Genome out = seed;

    const float clampedDistance = clamp01(distance);

    // 5.1.3: distance 0 must give back the seed exactly, so it does not even
    // run the displacement.
    if (clampedDistance <= 0.0f)
        return out;

    // 2. Non linear distance response.
    const float step = std::pow(clampedDistance, params.distanceExponent);

    for (std::size_t geneIndex = 0; geneIndex < kNumGenes; ++geneIndex)
    {
        const GeneInfo& geneInfo = kGenes[geneIndex];
        const float displacedValue = seed[geneIndex] + direction.displacement[geneIndex] * step;

        // 4. Boundary handling: compress past the soft range, never clamp.
        out[geneIndex] = softCompress(displacedValue, seed[geneIndex], geneInfo.softMin, geneInfo.softMax);
    }

    // 6. Couplings and guard rails.
    applyCouplings(out);
    return out;
}

//==============================================================================
BranchTraits resolveTraits(uint64_t seedHash, float distance, const MutationParams& params)
{
    BranchTraits traits;

    const float triggerProbability = std::pow(clamp01(distance), 3.0f) * params.structuralRate;

    if (triggerProbability <= 0.0f)
        return traits;

    // One fixed random threshold per trait: monotonic in distance, so a trait
    // flips once as a branch grows and stays flipped.
    Rng randomGenerator(Hash::combine(seedHash, 0x5EED1E5ull));

    const float thresholdFilterMode = randomGenerator.uniform();
    const float thresholdFmDirection = randomGenerator.uniform();
    const float thresholdModSource = randomGenerator.uniform();
    const float thresholdOscBMode = randomGenerator.uniform();

    if (triggerProbability > thresholdFilterMode)
        traits.filterMode = static_cast<unsigned char>(randomGenerator.integer(3));

    if (triggerProbability > thresholdFmDirection)
        traits.fmDirection = static_cast<unsigned char>(randomGenerator.integer(2));

    if (triggerProbability > thresholdModSource)
        traits.modSource = static_cast<unsigned char>(randomGenerator.integer(3));

    if (triggerProbability > thresholdOscBMode)
        traits.oscBMode = static_cast<unsigned char>(randomGenerator.integer(3));

    return traits;
}

//==============================================================================
float safetyScore(const Genome& genome)
{
    // Rough "will this be an audible, non horrible sound" score. Used only to
    // pick between the kNumDirections variants of a branch (5.6 silence
    // rejection); it is deliberately cheap and coarse.
    float score = 1.0f;

    // Needs to be able to sound at all.
    score -= 0.9f * std::max(0.0f, 0.10f - std::max(genome[g_vol_sus], 0.4f * (1.0f - genome[g_vol_dcy])));

    // Needs something to hear: source energy and an open enough filter.
    const float energy = std::max(std::max(genome[g_a_form], genome[g_a_noise]),
                                  std::max(genome[g_b_form], genome[g_b_noise]));
    score -= 0.6f * std::max(0.0f, 0.15f - energy);
    score -= 0.8f * std::max(0.0f, 0.12f - genome[g_flt_freq]);

    // Penalise the usual offenders.
    score -= 0.5f * std::max(0.0f, genome[g_flt_q] - 0.75f);
    score -= 0.4f * std::max(0.0f, genome[g_a_noise] + genome[g_b_noise] - 1.1f);
    score -= 0.3f * std::max(0.0f, genome[g_fm_amt] - 0.8f);
    // A four second swell is not a note, it is a pad.
    score -= 0.3f * std::max(0.0f, genome[g_vol_atk] - 0.85f);

    return score;
}

//==============================================================================
float loudnessCompensation(const Genome& genome)
{
    // Estimate where the branch sits level wise and pull it back towards the
    // seed (5.6). Sources of extra loudness: resonance, FM sidebands, sub,
    // noise and drive.
    const float boost = 1.0f + 1.7f * genome[g_flt_q] * genome[g_flt_q] + 0.8f * genome[g_fm_amt]
                        + 0.7f * genome[g_sub_am] + 0.5f * std::max(genome[g_a_noise], genome[g_b_noise])
                        + 0.9f * genome[g_sat_drive];

    return 1.0f / std::sqrt(boost);
}

//==============================================================================
void randomizeGenome(Genome& genome, Rng& randomGenerator, float atonality)
{
    const float ceiling = clamp01(atonality);

    for (std::size_t geneIndex = 0; geneIndex < kNumGenes; ++geneIndex)
    {
        const GeneInfo& geneInfo = kGenes[geneIndex];
        float geneValue = randomGenerator.range(geneInfo.softMin, geneInfo.softMax);

        // Pitch capable genes are drawn inside the atonality band rather than
        // clipped into it afterwards, so a low atonality seed still has a
        // spread of tunings rather than a pile on the ceiling.
        if ((geneInfo.flags & kFlagPitch) != 0)
        {
            const float neutral = pitchNeutral(geneInfo);
            geneValue = neutral + (geneValue - neutral) * ceiling;
        }

        genome[geneIndex] = geneValue;
    }

    // Bias a fresh seed towards something that behaves like an instrument
    // before the guard rails run: audible attack, some sustain, filter open.
    genome[g_vol_atk] = std::min(genome[g_vol_atk], 0.35f);
    genome[g_vol_sus] = std::max(genome[g_vol_sus], 0.30f);
    genome[g_flt_freq] = std::max(genome[g_flt_freq], 0.35f);
    genome[g_fx_mix] *= 0.7f;

    applyCouplings(genome);
}

} // namespace gmosh
