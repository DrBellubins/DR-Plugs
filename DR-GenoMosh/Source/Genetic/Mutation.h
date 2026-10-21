#pragma once

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

    /// A generation time ceiling on tonality, not a modulation destination.
    ///
    /// It bounds how far any kFlagPitch gene (a_freq, b_freq, lfo_amt,
    /// drift_amt, det_spread) may sit from its neutral point, in the seed, in
    /// every branch direction and in every resolved branch genome:
    ///
    ///   0 -> oscillator offsets are unison and there is no vibrato, drift or
    ///        per voice detune: nothing in the plant can move the pitch.
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
struct BranchTraits
{
    unsigned char filterMode = 0; ///< 0 = low, 1 = band, 2 = high
    unsigned char fmDirection = 0;///< 0 = B modulates A, 1 = A modulates B
    unsigned char modSource = 0;  ///< 0 = mod env, 1 = loop env, 2 = LFO
    unsigned char oscBMode = 0;   ///< 0 = sync, 1 = ring mod, 2 = sample & hold
};

/// Deterministic identity of a branch of a given seed.
inline uint64_t branchHash (uint64_t seedId, int branch, int variant = 0)
{
    return hash::combine (hash::combine (seedId, static_cast<uint64_t> (branch)),
                          static_cast<uint64_t> (variant));
}

/// Step 1 of 5.2: build the direction vector for one branch.
BranchDirection buildBranchDirection (uint64_t seedId, int branch, int variant,
                                      const MutationParams& mp);

/// Steps 2 to 4 of 5.2: displace the seed along a branch direction.
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
void applyAtonality (Genome& genome, float atonality);

/// Rough "this branch will make a usable sound" score in [0,1].
float safetyScore (const Genome& genome);

/// Level compensation gain (5.6): keeps branches within a few dB of the seed.
float loudnessCompensation (const Genome& genome);

/// Random seed inside the curated soft ranges (design doc 11). Pitch capable
/// genes are drawn inside the atonality ceiling instead of the full range, so a
/// low atonality seed is spread over its allowed band rather than pinned to the
/// edge of it.
void randomizeGenome (Genome& genome, Rng& rng, float atonality = 1.0f);

} // namespace gmosh
