#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>

#include "DiffusionAllpass.h"

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
    void Configure(
        const std::vector<float>& sourceTunings,
        int numberOfStages,
        float size01,
        float jitterDepthPercentValue,
        float jitterRateMinimumHz,
        float jitterRateRangeHz)
    {
        cachedStageCount = std::max(1, numberOfStages);
        cachedSize01 = std::max(0.0f, std::min(1.0f, size01));

        stages.clear();
        stages.reserve(static_cast<size_t>(cachedStageCount));
        perStageDelayMs.clear();

        const std::vector<float> finalDelays =
            BuildQualityDistributedStageDelays(sourceTunings, cachedStageCount, cachedSize01);

        for (float stageDelayMilliseconds : finalDelays)
        {
            const float stageGain = 0.7f;

            auto diffusionStage = std::make_unique<DiffusionAllpass>();
            diffusionStage->Prepare(sampleRate);
            diffusionStage->Configure(stageDelayMilliseconds, stageGain);
            stages.push_back(std::move(diffusionStage));
            perStageDelayMs.push_back(stageDelayMilliseconds);
        }

        const int effectiveStages = static_cast<int>(perStageDelayMs.size());

        jitterLPState.assign(static_cast<size_t>(effectiveStages), 0.0f);
        jitterDepthPercent.assign(static_cast<size_t>(effectiveStages), jitterDepthPercentValue);
        jitterRateHz.assign(static_cast<size_t>(effectiveStages), 0.0f);
        tpdfNoiseSeedA.assign(static_cast<size_t>(effectiveStages), static_cast<unsigned int>(rand()));
        tpdfNoiseSeedB.assign(static_cast<size_t>(effectiveStages), static_cast<unsigned int>(rand()));

        for (int stageIndex = 0; stageIndex < effectiveStages; ++stageIndex)
        {
            jitterRateHz[static_cast<size_t>(stageIndex)] =
                jitterRateMinimumHz + jitterRateRangeHz * random01();
        }
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

    void SetGlobalGain(float newGain)
    {
        for (auto& stage : stages)
            stage->SetGain(newGain);
    }

    void ClearState()
    {
        for (auto& stage : stages)
            stage->Clear();

        std::fill(jitterLPState.begin(), jitterLPState.end(), 0.0f);
    }

    std::vector<float> perStageDelayMs;

private:
    double sampleRate = 48000.0;
    std::vector<std::unique_ptr<DiffusionAllpass>> stages;

    std::vector<float> BuildQualityDistributedStageDelays(
    const std::vector<float>& sourceTunings,
    int numberOfStages,
    float size01) const
    {
        const int clampedStageCount = std::max(1, numberOfStages);
        const float clampedSize01 = std::max(0.0f, std::min(1.0f, size01));

        if (sourceTunings.empty())
        {
            return {};
        }

        const int sourceCount = static_cast<int>(sourceTunings.size());
        const int outputCount = std::min(clampedStageCount, sourceCount);

        std::vector<float> finalDelays;
        finalDelays.reserve(static_cast<size_t>(outputCount));

        if (outputCount == 1)
        {
            const int centerIndex = sourceCount / 2;
            const float scaledMilliseconds = sourceTunings[centerIndex] * (0.25f + 0.75f * clampedSize01);
            finalDelays.push_back(scaledMilliseconds);
            return finalDelays;
        }

        for (int stageIndex = 0; stageIndex < outputCount; ++stageIndex)
        {
            const float normalizedPosition =
                static_cast<float>(stageIndex) / static_cast<float>(outputCount - 1);

            const float sourceIndexFloat =
                normalizedPosition * static_cast<float>(sourceCount - 1);

            const int sourceIndexA = static_cast<int>(std::floor(sourceIndexFloat));
            const int sourceIndexB = std::min(sourceIndexA + 1, sourceCount - 1);
            const float fraction = sourceIndexFloat - static_cast<float>(sourceIndexA);

            const float interpolatedMilliseconds =
                sourceTunings[static_cast<size_t>(sourceIndexA)] * (1.0f - fraction)
                + sourceTunings[static_cast<size_t>(sourceIndexB)] * fraction;

            const float scaledMilliseconds =
                interpolatedMilliseconds * (0.25f + 0.75f * clampedSize01);

            finalDelays.push_back(scaledMilliseconds);
        }

        return finalDelays;
    }

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

    std::vector<float> jitterPhase; // Per-stage phase
    std::vector<float> jitterRate;  // Per-stage rate (e.g., 0.001..0.01 per sample, randomized)

    int sampleCounter = 0;
};