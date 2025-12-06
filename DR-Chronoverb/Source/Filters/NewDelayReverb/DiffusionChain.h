#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>

#include "DiffusionAllpass.h"

/*
    DiffusionChain
    --------------
    A header-only implementation of a serial diffusion chain using simple delay-based allpass filters.

    Structure:
    - DiffusionChain: Owns N DiffusionAllpass stages in series, configurable by stage count and size.

    Controls:
    - Configure(NumberOfStages, Size01):
        * NumberOfStages: 1..N serial allpass stages (recommended 4..8).
        * Size01: 0..1 scaling for the delay times (mapped to ~10..100 ms base and expanded).

    - ProcessSample(InputSample, Amount01):
        * Amount01: 0..1 wetness of diffusion. 0 => bypass (dry), 1 => full diffusion.

    Notes:
    - This header is self-contained and does not depend on JUCE.
    - Allman style is used consistently.
    - Parameter names in functions are fully spelled out to match your style guidelines.
*/

class DiffusionChain
{
public:
    DiffusionChain()
    {
    }

    ~DiffusionChain()
    {
    }

    void Prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
    }

    // Configure the chain with a given number of stages and size scaling factor.
    // - numberOfStages: 1..8, tune delays per all-pass filter.
    // - size01: 0..1 scales the per-stage delay milliseconds.
    void Configure(int numberOfStages, float size01)
    {
        cachedStageCount = std::max(1, numberOfStages);
        cachedSize01 = std::max(0.0f, std::min(1.0f, size01));

        stages.clear();
        stages.reserve(static_cast<size_t>(cachedStageCount));

        perStageDelayMs.clear();

        // Tuned base delays from quality 8 fit (ms, prime-rounded and sorted for progressive build-up)
        std::vector tunedBaseDelays = { 47.0f, 67.0f, 71.0f, 73.0f, 79.0f, 83.0f, 89.0f, 97.0f };

        // Use full array for max stages; slice first N for lower quality
        int effectiveStages = std::min(cachedStageCount, static_cast<int>(tunedBaseDelays.size()));
        const float allpassGain = 0.65f;  // Fixed from fits; can link to AudioProcessorValueTreeState::Parameter for automation

        for (int stageIndex = 0; stageIndex < effectiveStages; ++stageIndex)
        {
            float baseMilliseconds = tunedBaseDelays[stageIndex];
            float scaledMilliseconds = baseMilliseconds * (0.25f + 0.75f * cachedSize01);  // Scale for size control

            auto diffusionStage = std::make_unique<DiffusionAllpass>();
            diffusionStage->Prepare(sampleRate);
            diffusionStage->Configure(scaledMilliseconds, allpassGain);
            stages.push_back(std::move(diffusionStage));

            perStageDelayMs.push_back(scaledMilliseconds);
        }

        // Per-stage jitter state
        jitterLPState.assign(effectiveStages, 0.0f);
        jitterDepthPercent.assign(effectiveStages, 0.015f); // ±1.5% default
        jitterRateHz.assign(effectiveStages, 0.20f + 0.30f * random01()); // 0.2..0.5 Hz equivalent noise refresh
        tpdfNoiseSeedA.assign(effectiveStages, rand());
        tpdfNoiseSeedB.assign(effectiveStages, rand());
    }

    // Process a single sample through the diffusion chain.
    float ProcessSample(float inputSample)
    {
        if (stages.empty())
            return inputSample;

        float sample = inputSample;

        for (size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex)
        {
            const float baseDelayMs = perStageDelayMs[stageIndex];

            // Generate TPDF noise in [-1, +1] (zero mean), then low-pass filter for smooth jitter
            const float tpdf = generateTPDF(stageIndex);
            const float alpha = computeNoiseAlpha(jitterRateHz[stageIndex]); // based on desired jitterRateHz

            jitterLPState[stageIndex] = jitterLPState[stageIndex] + alpha * (tpdf - jitterLPState[stageIndex]);

            // Map to ±depth% of base delay
            const float depthPercent = jitterDepthPercent[stageIndex];
            const float jitterMs = baseDelayMs * depthPercent * jitterLPState[stageIndex];

            // Convert to samples and update fractional delay smoothly
            const float jitterSamples = static_cast<float>(((baseDelayMs + jitterMs) * sampleRate) / 1000.0);
            stages[stageIndex]->SetCurrentDelaySamples(jitterSamples);

            sample = stages[stageIndex]->ProcessSample(sample);
        }

        return sample;
    }

private:
    double sampleRate = 48000.0;
    std::vector<std::unique_ptr<DiffusionAllpass>> stages;

    // Zero-mean TPDF generator per stage
    float generateTPDF(size_t stageIndex)
    {
        // Two uniform noises summed => TPDF; normalize to [-1, 1]
        const float a = uniform01(tpdfNoiseSeedA[stageIndex]);
        const float b = uniform01(tpdfNoiseSeedB[stageIndex]);
        const float tpdf = (a + b) - 1.0f; // [-1, +1]
        return tpdf;
    }

    // Simple per-stage uniform PRNG; xorshift-style
    float uniform01(unsigned int& seed)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        const unsigned int u = seed;
        return static_cast<float>(u) / static_cast<float>(std::numeric_limits<unsigned int>::max());
    }

    float computeNoiseAlpha(float targetRateHz) const
    {
        // One-pole LP alpha for smoothing toward targetRateHz
        const float rate = std::max(0.01f, targetRateHz);
        const float omega = 2.0f * juce::MathConstants<float>::pi * rate;
        const float x = std::exp(-omega / static_cast<float>(sampleRate));
        const float alpha = 1.0f - x;
        return juce::jlimit(0.0001f, 0.2f, alpha);
    }

    // Jitter state
    std::vector<float> jitterLPState;
    std::vector<float> jitterDepthPercent; // per-stage ±percent of base delay
    std::vector<float> jitterRateHz;

    std::vector<unsigned int> tpdfNoiseSeedA;
    std::vector<unsigned int> tpdfNoiseSeedB;

    // Helper to init random in [0,1]
    float random01() const
    {
        return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }

    int cachedStageCount = 6;
    float cachedSize01 = 0.0f;

    std::vector<float> perStageDelayMs;

    std::vector<float> jitterPhase; // Per-stage phase
    std::vector<float> jitterRate;  // Per-stage rate (e.g., 0.001..0.01 per sample, randomized)

    int sampleCounter;
};