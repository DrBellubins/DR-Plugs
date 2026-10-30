#include "GeneTable.h"

namespace gmosh
{
//==============================================================================
// The table itself is generated from the same macro that generated the enum,
// so kGenes[i] always corresponds to GeneId i. The group colour is derived from
// the group rather than repeated in every row, so the two can never disagree.
#define GMO_GENE_TABLE(id, label, help, curve, lo, hi, neutral, gateLo, gateHi, group, steps, sens, softLo, softHi, flags) \
    { #id, label, help, curve, lo, hi, neutral, gateLo, gateHi, group, steps,    \
      sens, softLo, softHi, static_cast<unsigned int> (flags),                   \
      groupColour (group) },

const GeneInfo kGenes[kNumGenes] = {
    GENO_MOSH_GENE_LIST (GMO_GENE_TABLE)
};

#undef GMO_GENE_TABLE

const char* const kGroupNames[GrpCount] = {
    "Envelope", "Volume", "Modulation", "LFO",
    "Oscillator A", "Mix / FM", "Oscillator B",
    "Filter", "Saturation", "Reverb", "Output",
    "GenoMosh additions"
};

static_assert (sizeof (kGenes) / sizeof (kGenes[0]) == kNumGenes,
               "gene table and gene enum are out of sync");
static_assert (GrpCount == 12, "kGroupNames must have one name per group");

} // namespace gmosh
