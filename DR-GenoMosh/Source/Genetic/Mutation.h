#pragma once

#include "Curves.h"
#include "GeneTable.h"
#include "Rng.h"

namespace gmosh
{
//==============================================================================
// The genetic engine (design doc section 5).
//
// Everything here is pure data in / data out with no JUCE dependency, so it can
// be unit tested and driven from a command line harness.
//==============================================================================

struct MutationParams
{
    /// 0 = every gene walks on its own, 1 = a whole group moves as one.
    /// Coherent groups are what makes a branch sound like a timbral relative
    /// instead of 42 independent random walks.
    float coherence = 0.55f;

    /// Distance response: gentle near the seed, wilder far out (5.2 step 2).
    float distanceExponent = 1.8f;

    /// Largest displacement in gene space at distance 1.
    float globalScale = 0.65f;

    /// How strongly discrete/structural mutations fire with distance (5.4).
    float structuralRate = 0.85f;

    /// How strongly growth pulls every gene back toward the value where nothing
    /// happens (5.2 step 3, 5.7.1). This is new in Rev 3 and it is the reason
    /// a long grown branch tends to sound "milder" rather than extreme: the
    /// displacement is biased by
    ///
    ///   pullToNeutral * (gi.neutral - seed[g]) * d
    ///
    /// so a walk that has wandered away from the middle of its range walks back
    /// toward it as the branch grows. It is a bias, not a clamp: distance 0 is
    /// still exactly the seed, and a gene the seed holds at neutral does not
    /// move from it because of this term. Growth exempt genes (kFlagNever) are
    /// skipped before it is applied.
    float pullToNeutral = 0.35f;

    /// A generation time ceiling on tonality, not a modulation destination.
    ///
    /// It bounds how far any kFlagPitch gene (a_freq, b_freq, a_mod, b_mod,
    /// lfo_amt) may sit from its neutral point, in the seed, in every branch
    /// direction and in every resolved branch genome:
    ///
    ///   0 -> oscillator offsets are unison, there is no vibrato and no
    ///        envelope pitch modulation: nothing in the plant can move the pitch.
    ///   1 -> the full range of those genes is available.
    ///
    /// A ceiling rather than a multiplier, so a seed drawn at atonality 0.8 and
    /// later viewed at 0.2 is pulled in, and going back restores it. It is
    /// applied when the plant is built, never while a note sounds.
    float atonality = 0.5f;
};

/// Per branch fixed "direction of mutation", computed off the audio thread.
/// The audio thread only has to scale it by the current effective distance,
/// which is what makes the mod wheel able to open a branch live.
struct BranchDirection
{
    Genome dir{};      ///< already multiplied by gene sensitivity + globalScale
    uint64_t hash = 0;
    float hue = 0.0f;  ///< UI colour fingerprint (design doc 12)
    float score = 0.0f;
};

/// Discrete, distance gated structural changes (design doc 5.4).
/// Decision R10: a trait may only offset a stepped gene by one step and swap
/// which source drives a destination, never replace a gene. Two traits went away
/// because of exactly that:
///
///   modSource   - M3: there is one envelope now (6.1.1), nothing left to select.
///   filterMode  - removed by M3, which measured it. It replaced the filter
///                 topology outright, and every branch that grew into "band" or
///                 "high" lost its fundamental to a 5-20 kHz pass band: 16 of
///                 the 60 batch cases came out 17-25 dB under their seed with a
///                 2-22 kHz centroid. The gene for topology is flt_type, and M5
///                 gives it the four topologies plus the loudness model they
///                 need. Nothing is hidden by removing it: until M5 the voice is
///                 lowpass only, which is what a seed already is.
//
// What is left is two things a gene cannot be: which oscillator modulates which,
// and whether B is hard synced or ring modulated. oscBMode's "sample & hold"
// mode went away at M4 for exactly R10's reason: holding B is what the b_sh
// *gene* does, at a rate both the player and growth can set, so a trait that
// also did it was a second unaddressable copy of a documented control. Mode 0 is
// "no trait", which is what a branch that has not crossed its threshold gets.
struct BranchTraits
{
    unsigned char fmDirection = 0;///< 0 = B modulates A, 1 = A modulates B
    unsigned char oscBMode = 0;   ///< 0 = no trait, 1 = sync, 2 = ring mod
};

//==============================================================================
// What the guard rails actually hear (plan 6.3, milestone M3).
//
// 5.6's checks reason about genomes, but since M1 a genome is read through
// gates, rails and curves, and since M3 env_time is the length of the note.
// A rail that reads raw gene values is therefore lying: with vol_sus gated at
// 0.5, a raw 0.55 is barely a sustain at all, and a branch with a 60 ms
// duration is quiet for a reason the level compensator cannot see. These four
// helpers are the effective values, computed the same way the mapping layer
// computes them, and the couplings, the safety score and the loudness estimate
// all read *them*.
inline float effectiveSustain (const Genome& g)
{
    return mappedGated (kGenes[g_vol_sus], g[g_vol_sus]);
}

inline float effectiveFade (const Genome& g)
{
    return mappedGated (kGenes[g_vol_fade], g[g_vol_fade]);
}

/// The gene's duration in seconds. The table's own curve decides it; >= 0.9 is
/// the reference's "infinite", which for a level estimate means "as long as the
/// key is held", so it is reported as the cap.
inline float effectiveDurationSeconds (const Genome& g)
{
    constexpr float kCap = 8.0f;   // beyond this, the note is as long as any key
    if (g[g_env_time] >= 0.9f)
        return kCap;
    return std::min (kCap, mapGene (kGenes[g_env_time], g[g_env_time]));
}

/// The note length the guard rails treat as "a whole note", i.e. the duty a
/// sustaining note has. Pinned to about what env_time's neutral value maps to
/// (1.2 s), so the compensator neither favours nor punishes a neutral plant.
inline constexpr float kNominalNoteSeconds = 1.2f;

/// How much of a held note the envelope actually sounds, 0..1. This is what a
/// listener measures as loud over a fixed window, and the only reason the
/// batch's worst offenders were 17-24 dB under their seed: a note that stops
/// sustaining has no RMS to compensate.
inline float envelopeDuty (const Genome& g)
{
    const float sus  = effectiveSustain (g);
    const float dur  = effectiveDurationSeconds (g);
    const float fade = effectiveFade (g);

    // A sustaining note sounds for as long as the key is down; a one shot is
    // audible for about its shape, shortened by the second fade.
    const float oneShot = clamp01 (dur / kNominalNoteSeconds) * (1.0f - 0.6f * fade);
    return clampf (lerpf (oneShot, 1.0f, sus), 0.05f, 1.0f);
}

/// Deterministic identity of a branch of a given seed.
inline uint64_t branchHash (uint64_t seedId, int branch, int variant = 0)
{
    return hash::combine (hash::combine (seedId, static_cast<uint64_t> (branch)),
                          static_cast<uint64_t> (variant));
}

/// Step 1 of 5.2: build the direction vector for one branch.
BranchDirection buildBranchDirection (uint64_t seedId, int branch, int variant,
                                      const MutationParams& mp);

/// Steps 2 to 4 of 5.2: displace the seed along a branch direction, biased
/// toward each gene's neutral value, and leave the growth exempt genes alone.
/// distance = 0 returns the seed exactly (requirement 5.1.3).
Genome applyDistance (const Genome& seed, const BranchDirection& direction,
                      float distance, const MutationParams& mp);

/// Step 5: structural mutations. Deterministic *and* monotonic in distance:
/// each trait has a fixed random threshold, so growing a branch flips it once
/// and it never flips back while the branch keeps growing.
BranchTraits resolveTraits (uint64_t hash, float distance, const MutationParams& mp);

/// Section 5.6 guard rails, applied to a resolved genome in place.
void applyCouplings (Genome& genome);

/// The atonality ceiling above, applied to a genome in place. Idempotent:
/// applying it twice is the same as applying it once, which is what lets a
/// planted branch (a resolved genome fed back in as the next seed) be gated
/// again without ratcheting the tuning down a little more every time.
///
/// Until M1 this also snapped every kFlagRatio gene onto the ratio ladder at
/// *every* atonality, which is not what 5.5 describes (there, the snap is the
/// atonality == 0 end of a blend). The unconditional snap is gone; the blended
/// version returns in M8 with converge() (decision R7, deviation D-4).
void applyAtonality (Genome& genome, float atonality);

/// Rough "this branch will make a usable sound" score in [0,1].
float safetyScore (const Genome& genome);

/// Level compensation gain (5.6): keeps branches within a few dB of the seed.
float loudnessCompensation (const Genome& genome);

/// Random seed inside the curated ranges (design doc 11): a gaussian centred on
/// each gene's neutral value, reflected at the 0/1 rails, then clamped to the
/// gene's soft range. That is the reference's own randomise ("a gaussian
/// centred on each gene's default, reflected at the rails, spread of about a
/// quarter of the range") with softMin/softMax as the extra per-gene clamp on
/// top. Pitch capable genes are drawn inside the atonality band instead of the
/// full range, so a low atonality seed is spread over its allowed band rather
/// than pinned to the edge of it.
void randomizeGenome (Genome& genome, Rng& rng, float atonality = 1.0f);

} // namespace gmosh
