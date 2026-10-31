#include "GeneTable.h"

namespace gmosh
{
//==============================================================================
// The table itself is generated from the same macro that generated the enum,
// so kGenes[i] always corresponds to GeneId i.
#define GMO_GENE_TABLE(id, label, curve, lo, hi, group, sens, softLo, softHi, flags)                         \
    { #id, label, curve, lo, hi, group, sens, softLo, softHi, static_cast<unsigned int>(flags) },

const GeneInfo kGenes[kNumGenes] = { GENO_MOSH_GENE_LIST(GMO_GENE_TABLE) };

#undef GMO_GENE_TABLE

const char* const kGroupNames[GrpCount] = { "Amplitude",    "Mod Envelope", "Loop Envelope", "LFO",
                                            "Oscillator A", "Oscillator B", "Mix",           "Filter",
                                            "Effects",      "Movement" };

static_assert(sizeof(kGenes) / sizeof(kGenes[0]) == kNumGenes, "gene table and gene enum are out of sync");

} // namespace gmosh
