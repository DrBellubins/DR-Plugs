#include "PlantEngine.h"

namespace gmosh
{
//==============================================================================
PlantSnapshot& PlantHandoff::beginWrite()
{
    // Never touch the slot the audio thread has published, nor the one it is
    // currently reading: with three slots there is always a third one free.
    const int publishedSlot = published.load(std::memory_order_relaxed);
    const int heldSlot = held.load(std::memory_order_relaxed);

    for (int slotIndex = 0; slotIndex < 3; ++slotIndex)
    {
        if (slotIndex != publishedSlot && slotIndex != heldSlot)
        {
            writeSlot = slotIndex;
            break;
        }
    }

    return slots[static_cast<std::size_t>(writeSlot)];
}

void PlantHandoff::publish() { published.store(writeSlot, std::memory_order_release); }

void PlantHandoff::acquire()
{
    const int publishedSlot = published.load(std::memory_order_acquire);
    held.store(publishedSlot, std::memory_order_relaxed);
    activeSlot = publishedSlot;
}

//==============================================================================
void buildPlant(PlantSnapshot& snapshotOut, uint64_t seedId, const Genome& seed,
                const MutationParams& mutation)
{
    snapshotOut.seedId = seedId;
    snapshotOut.seed = seed;
    snapshotOut.mutation = mutation;

    // The atonality ceiling belongs to the plant, not to the voice: the seed
    // genome the audio thread sees is already inside it.
    applyAtonality(snapshotOut.seed, mutation.atonality);

    for (int branchIndex = 0; branchIndex < kNumBranches; ++branchIndex)
    {
        for (int variantIndex = 0; variantIndex < kNumDirections; ++variantIndex)
        {
            snapshotOut.directions[static_cast<std::size_t>(branchIndex * kNumDirections + variantIndex)] =
                buildBranchDirection(seedId, branchIndex, variantIndex, mutation);
        }
    }

    ++snapshotOut.revision;
}

//==============================================================================
// Which branch a note plays: its pitch class (design doc 5.3). Notes in other
// octaves share the branch.
int branchForNote(int midiNote)
{
    int branchIndex = midiNote % kNumBranches;

    if (branchIndex < 0)
        branchIndex += kNumBranches;

    return branchIndex;
}

//==============================================================================
Genome resolveBranchGenome(const PlantSnapshot& plantSnapshot, int branchIndex, float distance,
                           BranchTraits& outTraits)
{
    // Guard rail 5.6: try the branch's direction variants and keep the first
    // one that passes, bounded by kNumDirections.
    Genome bestGenome = plantSnapshot.seed;
    int bestVariantIndex = 0;
    float bestScore = -1.0e9f;

    for (int variantIndex = 0; variantIndex < kNumDirections; ++variantIndex)
    {
        const BranchDirection& branchDirection = plantSnapshot.direction(branchIndex, variantIndex);
        const Genome candidate =
            applyDistance(plantSnapshot.seed, branchDirection, distance, plantSnapshot.mutation);
        const float score = safetyScore(candidate);

        if (score > bestScore)
        {
            bestScore = score;
            bestGenome = candidate;
            bestVariantIndex = variantIndex;
        }

        if (score > 0.85f)
            break;
    }

    // The seed and the branch directions are both inside the atonality ceiling,
    // but their sum is not automatically, so the resolved genome is gated too.
    // Idempotent, and cheap: five genes, once per note on.
    applyAtonality(bestGenome, plantSnapshot.mutation.atonality);

    outTraits = resolveTraits(plantSnapshot.direction(branchIndex, bestVariantIndex).hash, distance,
                              plantSnapshot.mutation);
    return bestGenome;
}

} // namespace gmosh
