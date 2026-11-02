// ============================================================================
// SeedDistribution - not a test, a measuring stick.
//
// Prints what randomizeGenome actually hands the envelope code: how long a
// random seed's note is, how long one pluck is, how many repeats fit in the
// note, and which of the three genes decided that. Read it after every change
// to the amp / loop rows of GeneTable.h or to the envelope constants in
// Engine/VoiceParams.cpp - those three genes are combined with a min() and a
// clamp, so changing one of them in isolation is guesswork without this.
//
// Build and run (same one liner as the engine harness, plus this file):
//
//   cd DR-Plugs/DR-GenoMosh
//   g++ -std=c++17 -O2 -Wall -I Source Source/Genetic/*.cpp
//       Source/Engine/*.cpp Tests/SeedDistribution.cpp -o /tmp/gmosh_seeds -lm
//   (one command, the line break above is for legibility)
//
// Which knob moves which line:
//   GeneTable.h softLo/softHi of env_time / vol_fade / env_loop
//       -> the "gene values drawn" block, and everything below it.
//   GeneTable.h min/max/curve of env_time
//       -> "note length" (env_time is the limiter in most seeds).
//   VoiceParams.cpp kPluckPeriodFast / kPluckPeriodSlow
//       -> "pluck period" and "period clamped" (motion vs one slow swell).
//   VoiceParams.cpp kFadeMinCycles/kFadeMaxCycles/kInfiniteFadeGene
//       -> "repeats" and the "no fade" count.
//   VoiceParams.cpp kInfiniteDurationGene / kInfiniteFadeGene vs softMax
//       -> the two REACHABLE? lines. A "never" there means random seeds can
//          not reach that state at all, because randomizeGenome only ever draws
//          inside [softLo, softHi].
//
// The kInfinite* constants are file local to VoiceParams.cpp; keep the copies
// below in sync when tuning them (they are only read by the REACHABLE? lines).
// ============================================================================

#include "Engine/VoiceParams.h"
#include "Genetic/Mutation.h"
#include "Genetic/Rng.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
using namespace gmosh;

constexpr int kSamples = 4000;

// Mirror of the constants in Engine/VoiceParams.cpp, for the limiter lines and
// the REACHABLE? lines at the bottom. Keep in sync while tuning them.
constexpr float kProbeInfiniteDurationGene = 0.90f;
constexpr float kProbeInfiniteFadeGene = 0.95f;
constexpr float kProbeFadeMinCycles = 1.0f;
constexpr float kProbeFadeMaxCycles = 64.0f;
constexpr float kProbePluckPeriodFast = 0.030f;
constexpr float kProbePluckPeriodSlow = 16.0f;

/// The number of repeats vol_fade asks for (VoiceParams.cpp's expRange).
float probeFadeCycles(float gene)
{
    return kProbeFadeMinCycles * std::pow(kProbeFadeMaxCycles / kProbeFadeMinCycles, clamp01(gene));
}

/// The pluck period env_loop asks for, before the clamp to the note length.
float probePluckPeriod(float gene)
{
    return kProbePluckPeriodFast * std::pow(kProbePluckPeriodSlow / kProbePluckPeriodFast, clamp01(gene));
}

float percentile(std::vector<float> values, double fraction)
{
    std::sort(values.begin(), values.end());
    const std::size_t index =
        std::min(values.size() - 1, static_cast<std::size_t>(fraction * (values.size() - 1)));
    return values[index];
}

void printRow(const char* label, const std::vector<float>& values, const char* unit)
{
    std::printf("  %-22s", label);

    for (double fraction : { 0.05, 0.25, 0.50, 0.75, 0.95 })
        std::printf(" %7.2f%s", percentile(values, fraction), unit);

    std::printf("\n");
}

void printHeader()
{
    std::printf("  %-22s", "");
    std::printf("     p05      p25      p50      p75      p95\n");
}

const char* reachLabel(float infiniteGene, float softMax)
{
    return infiniteGene <= softMax ? "reachable" : "NEVER (softMax is below it)";
}
} // namespace

int main()
{
    Rng randomGenerator(Hash::fromString("seed-distribution"));

    std::vector<float> geneValues[kNumGenes];
    std::vector<float> noteSeconds;
    std::vector<float> pluckPeriod;
    std::vector<float> repeatCount;

    int ceilingDecides = 0;
    int fadeDecides = 0;
    int noFadeAtAll = 0;
    int periodClamped = 0;

    std::printf("random seed distribution, %d seeds\n\n", kSamples);

    for (int sample = 0; sample < kSamples; ++sample)
    {
        Genome seed;
        randomizeGenome(seed, randomGenerator);

        for (std::size_t geneIndex = 0; geneIndex < kNumGenes; ++geneIndex)
            geneValues[geneIndex].push_back(seed[geneIndex]);

        VoiceParams params;
        const BranchTraits traits;
        const MacroValues macros;
        resolveVoiceParams(seed, traits, macros, 60, 1.0f, params);

        // Re-derive the two fade lengths the mapping takes the minimum of, so
        // the report can say which one is actually binding. Both are computed
        // from the *unclamped* period, which is what the mapping compared.
        const float period = params.envShape.Period;
        const float askedPeriod = probePluckPeriod(seed[g_env_loop]);
        const float fadeAsked = seed[g_vol_fade] >= kProbeInfiniteFadeGene
                                    ? 1.0e9f
                                    : probeFadeCycles(seed[g_vol_fade]) * askedPeriod;
        const float ceilingAsked =
            seed[g_env_time] >= kProbeInfiniteDurationGene ? 1.0e9f : mapGene(g_env_time, seed[g_env_time]);

        if (params.envShape.Duration <= 0.0f)
        {
            ++noFadeAtAll;
            noteSeconds.push_back(1.0e4f);
            repeatCount.push_back(1.0e4f / period);
        }
        else
        {
            noteSeconds.push_back(params.envShape.Duration);
            repeatCount.push_back(params.envShape.Duration / period);

            if (ceilingAsked <= fadeAsked)
                ++ceilingDecides;
            else
                ++fadeDecides;
        }

        pluckPeriod.push_back(period);

        if (params.envShape.Duration > 0.0f && period >= params.envShape.Duration - 1.0e-4f)
            ++periodClamped;
    }

    printHeader();
    printRow("env_time (gene)", geneValues[g_env_time], "");
    printRow("vol_fade (gene)", geneValues[g_vol_fade], "");
    printRow("env_loop (gene)", geneValues[g_env_loop], "");
    printRow("vol_floor (gene)", geneValues[g_vol_floor], "");
    std::printf("\n");
    printRow("note length", noteSeconds, "s");
    printRow("pluck period", pluckPeriod, "s");
    printRow("repeats in note", repeatCount, "");

    std::printf("\n  env_time ceiling is the limiter : %4.1f%%\n", 100.0 * ceilingDecides / kSamples);
    std::printf("  vol_fade is the limiter           : %4.1f%%\n", 100.0 * fadeDecides / kSamples);
    std::printf("  period longer than the note       : %4.1f%%  (one swell, no repeats)\n",
                100.0 * periodClamped / kSamples);
    std::printf("  note runs to key up               : %4.1f%%\n", 100.0 * noFadeAtAll / kSamples);

    std::printf("\n  can a random seed reach the no-ceiling state? %s\n",
                reachLabel(kProbeInfiniteDurationGene, kGenes[g_env_time].softMax));
    std::printf("  can a random seed reach the no-fade state?    %s\n",
                reachLabel(kProbeInfiniteFadeGene, kGenes[g_vol_fade].softMax));

    return 0;
}
