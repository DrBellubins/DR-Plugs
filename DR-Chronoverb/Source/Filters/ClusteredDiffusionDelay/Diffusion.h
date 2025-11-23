#pragma once

#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <juce_core/juce_core.h>
#include "DelayLine.h"

// Diffusion
// - Builds and applies a symmetric multi-tap diffuser around a nominal delay.
// - Offsets are deterministic, symmetric, and scaled by size; negative offsets realized via lookahead shift.
// - Crossfades between base tap and cluster using equal-power AmountA/AmountB.

class Diffusion
{
public:
    struct TapLayout
    {
        // Normalized symmetric offsets in [-1..+1], center excluded (e.g., -0.8, -0.4, +0.4, +0.8).
        std::vector<float> NormalizedOffsets;

        // Per-tap weights (pre-normalization).
        std::vector<float> Weights;

        // Sum of weights and normalization factor (1/sum).
        float WeightSum = 1.0f;
        float WeightNorm = 1.0f;
    };

    // Map quality [0..1] -> number of symmetric tap pairs [1..8].
    static int QualityToTapPairs(float DiffusionQuality)
    {
        int Pairs = 1 + static_cast<int>(std::round(juce::jlimit(0.0f, 1.0f, DiffusionQuality) * 7.0f));
        return juce::jlimit(1, 8, Pairs);
    }

    // Recompute the tap layout using a prime-like spacing, symmetric about zero, sorted by proximity to center.
    static void RecomputeTapLayout(Diffusion::TapLayout& Layout, float DiffusionQuality)
    {
        static constexpr int PrimeLikeSequence[8] = { 2, 3, 5, 7, 11, 13, 17, 19 };

        const int PairCount = Diffusion::QualityToTapPairs(DiffusionQuality);
        const int MaxPrime = PrimeLikeSequence[std::min(PairCount - 1, 7)];

        Layout.NormalizedOffsets.clear();
        Layout.NormalizedOffsets.reserve(static_cast<size_t>(PairCount * 2));

        // Generate symmetric offsets
        for (int PairIndex = 0; PairIndex < PairCount; ++PairIndex)
        {
            const int PrimeValue = PrimeLikeSequence[PairIndex];
            float Normalized = static_cast<float>(PrimeValue) / static_cast<float>(MaxPrime);
            Normalized = juce::jlimit(0.0f, 1.0f, Normalized);

            Layout.NormalizedOffsets.push_back(-Normalized);
            Layout.NormalizedOffsets.push_back(+Normalized);
        }

        // Sort by absolute closeness to center so nearby taps contribute first
        std::sort(Layout.NormalizedOffsets.begin(),
                  Layout.NormalizedOffsets.end(),
                  [](float A, float B)
                  {
                      return std::abs(A) < std::abs(B);
                  });

        // Create a gentle per-tap weight falloff
        const int TotalTaps = static_cast<int>(Layout.NormalizedOffsets.size());

        Layout.Weights.assign(static_cast<size_t>(TotalTaps), 1.0f);

        float Weight = 1.0f;
        const float FalloffPerTap = 0.08f;

        for (int TapIndex = 0; TapIndex < TotalTaps; ++TapIndex)
        {
            Layout.Weights[static_cast<size_t>(TapIndex)] = Weight;
            Weight = std::max(0.25f, Weight - FalloffPerTap);
        }

        Layout.WeightSum = std::accumulate(Layout.Weights.begin(), Layout.Weights.end(), 0.0f);
        Layout.WeightNorm = (Layout.WeightSum > 0.0f ? 1.0f / Layout.WeightSum : 1.0f);
    }

    // Compute the wet echo sample for one channel at the current sample index.
    // - BaseDelaySamples: nominal delay in samples
    // - SpreadSamples: cluster spread in samples
    // - LookaheadSamples: fixed positive shift to emulate negative delays causally
    // - AmountA/AmountB: equal-power crossfade weights between base tap and cluster
    static inline float ComputeWetEcho(const DelayLine::State& DelayState,
                                       float BaseDelaySamples,
                                       float SpreadSamples,
                                       float LookaheadSamples,
                                       const Diffusion::TapLayout& Layout,
                                       float AmountA,
                                       float AmountB)
    {
        // Base nominal tap
        const float BaseTap = DelayLine::Read(DelayState, BaseDelaySamples);

        // Cluster sum across symmetric offsets
        float ClusterSum = 0.0f;
        const int TotalTaps = static_cast<int>(Layout.NormalizedOffsets.size());

        for (int TapIndex = 0; TapIndex < TotalTaps; ++TapIndex)
        {
            const float NormalizedOffset = Layout.NormalizedOffsets[static_cast<size_t>(TapIndex)];
            const float SignedOffsetSamples = NormalizedOffset * SpreadSamples;

            // Shift by Lookahead so negative offsets remain causal
            float EffectiveDelaySamples = BaseDelaySamples + LookaheadSamples + SignedOffsetSamples;

            // Instead of allowing negative (later clamped to 0), raise to small positive epsilon
            if (EffectiveDelaySamples < 1.0f)
                EffectiveDelaySamples = 1.0f; // Avoid collapsing multiple taps to index zero

            float TapSample = DelayLine::Read(DelayState, EffectiveDelaySamples);

            TapSample *= Layout.Weights[static_cast<size_t>(TapIndex)];
            ClusterSum += TapSample;
        }

        const float DiffusedCluster = ClusterSum * Layout.WeightNorm;

        // Equal-power crossfade between base tap and cluster
        const float WetEcho = (AmountA * BaseTap) + (AmountB * DiffusedCluster);

        return WetEcho;
    }
};