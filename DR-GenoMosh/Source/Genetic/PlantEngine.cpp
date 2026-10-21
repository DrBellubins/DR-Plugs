#include "PlantEngine.h"

#include <cmath>

namespace gmosh
{
//==============================================================================
PlantSnapshot& PlantHandoff::beginWrite()
{
    // Never touch the slot the audio thread has published, nor the one it is
    // currently reading: with three slots there is always a third one free.
    const int p = published.load (std::memory_order_relaxed);
    const int h = held.load (std::memory_order_relaxed);

    for (int i = 0; i < 3; ++i)
    {
        if (i != p && i != h)
        {
            writeSlot = i;
            break;
        }
    }

    return slots_[static_cast<std::size_t> (writeSlot)];
}

void PlantHandoff::publish()
{
    published.store (writeSlot, std::memory_order_release);
}

void PlantHandoff::acquire()
{
    const int p = published.load (std::memory_order_acquire);
    held.store (p, std::memory_order_relaxed);
    activeSlot = p;
}

//==============================================================================
void buildPlant (PlantSnapshot& out, uint64_t seedId, const Genome& seed,
                 const MutationParams& mutation)
{
    out.seedId = seedId;
    out.seed = seed;
    out.mutation = mutation;

    for (int b = 0; b < kNumBranches; ++b)
    {
        for (int v = 0; v < kNumDirections; ++v)
        {
            out.directions[static_cast<std::size_t> (b * kNumDirections + v)] =
                buildBranchDirection (seedId, b, v, mutation);
        }
    }

    ++out.revision;
}

//==============================================================================
int branchForNote (int midiNote, float rotation)
{
    const int r = static_cast<int> (std::floor (rotation));
    int b = (midiNote + r) % kNumBranches;
    if (b < 0)
        b += kNumBranches;
    return b;
}

//==============================================================================
Genome resolveBranchGenome (const PlantSnapshot& plant, int branch, float distance,
                            BranchTraits& traitsOut)
{
    // Guard rail 5.6: try the branch's direction variants and keep the first
    // one that passes, bounded by kNumDirections.
    Genome best = plant.seed;
    int bestVariant = 0;
    float bestScore = -1.0e9f;

    for (int v = 0; v < kNumDirections; ++v)
    {
        const BranchDirection& dir = plant.direction (branch, v);
        const Genome candidate = applyDistance (plant.seed, dir, distance, plant.mutation);
        const float score = safetyScore (candidate);

        if (score > bestScore)
        {
            bestScore = score;
            best = candidate;
            bestVariant = v;
        }

        if (score > 0.85f)
            break;
    }

    traitsOut = resolveTraits (plant.direction (branch, bestVariant).hash,
                               distance, plant.mutation);
    return best;
}

} // namespace gmosh
