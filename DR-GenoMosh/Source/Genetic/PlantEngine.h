#pragma once

#include "Curves.h"
#include "Mutation.h"

#include <array>
#include <atomic>

namespace gmosh
//==============================================================================
// The plant: the authoritative seed plus the 12 branch directions, handed to
// the audio thread as an immutable snapshot (design doc section 3).
//
// The message thread owns the seed and computes the branch directions; the
// audio thread only ever scales a direction by the current effective distance,
// which is what lets the mod wheel open the plant live without any allocation.
//==============================================================================
{
struct PlantSnapshot
{
    uint64_t seedId = 0;
    Genome seed{};
    std::array<BranchDirection, kNumBranches * kNumDirections> directions{};
    MutationParams mutation;
    int revision = 0;

    const BranchDirection& direction (int branch, int variant = 0) const
    {
        return directions[static_cast<std::size_t> (branch * kNumDirections + variant)];
    }
};

/// Lock free triple buffer, one writer (message thread) one reader (audio
/// thread), preallocated: no allocation, no locks.
class PlantHandoff
{
public:
    /// Message thread: get the buffer to write into, fill it, then publish().
    PlantSnapshot& beginWrite();
    void publish();

    /// Audio thread: call once per block, then use current() freely.
    void acquire();
    const PlantSnapshot& current() const { return slots_[static_cast<std::size_t> (activeSlot)]; }

private:
    std::array<PlantSnapshot, 3> slots_;
    std::atomic<int> published { 0 };
    std::atomic<int> held { 0 };
    int writeSlot = 1;   ///< message thread only
    int activeSlot = 0;  ///< audio thread only
};

/// Build all 12 branch directions for a seed (off the audio thread).
void buildPlant (PlantSnapshot& out, uint64_t seedId, const Genome& seed,
                 const MutationParams& mutation);

/// Which branch a note plays: its pitch class (design doc 5.3).
int branchForNote (int midiNote);

/// Resolve the genome a voice should use, including the guard rail retries of
/// 5.6 ("nudge or re-roll with the next seed in the hash chain").
Genome resolveBranchGenome (const PlantSnapshot& plant, int branch, float distance,
                            BranchTraits& traitsOut);

/// Effective distance for a branch given the wheel (design doc 5.3).
inline float effectiveDistance (float branchLength, float wheel, float wheelScale)
{
    return clamp01 (branchLength * wheel * wheelScale);
}

} // namespace gmosh
