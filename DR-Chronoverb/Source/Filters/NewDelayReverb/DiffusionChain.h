#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <limits>

#include <juce_core/juce_core.h>

#include "DiffusionAllpass.h"

class DiffusionChain
{
public:
    // Delay-mode tunings: shorter, natural-spacing delays for discrete-tap blur.
    std::vector<float> delayTunings =
    {
        10.0f, 15.0f, 22.5f, 33.75f, 50.6f, 75.9f, 113.9f, 170.8f
    };

    // Reverb-mode tunings: longer, prime-spaced delays for lush modal density.
    std::vector<float> reverbTunings =
    {
        29.0f, 37.0f, 43.0f, 53.0f, 71.0f, 89.0f, 113.0f, 149.0f
    };

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

    // Delay-quality configuration.
    // numberOfStages: 1..8
    // size01: 0..1 scales per-stage delay lengths.
    //
    // Stage delays are distributed across the FULL tuning list regardless of
    // numberOfStages, so reducing quality makes the diffusion sparser (fewer
    // taps spread across the same time span) rather than shorter.
    void Configure(int numberOfStages, float size01)
    {
        cachedStageCount = std::max(1, numberOfStages);
        cachedSize01     = std::max(0.0f, std::min(1.0f, size01));

        stages.clear();
        stages.reserve(static_cast<size_t>(cachedStageCount));
        perStageDelayMs.clear();

        const std::vector<float> finalDelays =
            BuildQualityDistributedStageDelays(delayTunings, cachedStageCount, cachedSize01);

        for (float stageDelayMilliseconds : finalDelays)
        {
            auto diffusionStage = std::make_unique<DiffusionAllpass>();
            diffusionStage->Prepare(sampleRate);
            diffusionStage->Configure(stageDelayMilliseconds, 0.7f);
            stages.push_back(std::move(diffusionStage));
            perStageDelayMs.push_back(stageDelayMilliseconds);
        }

        const int effectiveStages = static_cast<int>(perStageDelayMs.size());

        jitterLPState.assign(effectiveStages, 0.0f);
        jitterDepthPercent.assign(effectiveStages, 0.005f);
        jitterRateHz.assign(effectiveStages, 0.20f + 0.30f * random01());
        tpdfNoiseSeedA.assign(effectiveStages, static_cast<unsigned int>(rand()));
        tpdfNoiseSeedB.assign(effectiveStages, static_cast<unsigned int>(rand()));
    }

    // Reverb-quality configuration.
    // Same distributed-resampling logic, using the longer reverb tunings.
    void ConfigureAsReverb(int numberOfStages, float size01)
    {
        cachedStageCount = std::max(1, numberOfStages);
        cachedSize01     = std::max(0.0f, std::min(1.0f, size01));

        stages.clear();
        stages.reserve(static_cast<size_t>(cachedStageCount));
        perStageDelayMs.clear();

        const std::vector<float> finalDelays =
            BuildQualityDistributedStageDelays(reverbTunings, cachedStageCount, cachedSize01);

        for (float stageDelayMilliseconds : finalDelays)
        {
            auto diffusionStage = std::make_unique<DiffusionAllpass>();
            diffusionStage->Prepare(sampleRate);
            diffusionStage->Configure(stageDelayMilliseconds, 0.7f);
            stages.push_back(std::move(diffusionStage));
            perStageDelayMs.push_back(stageDelayMilliseconds);
        }

        const int effectiveStages = static_cast<int>(perStageDelayMs.size());

        jitterLPState.assign(effectiveStages, 0.0f);
        jitterDepthPercent.assign(effectiveStages, 0.003f);
        jitterRateHz.assign(effectiveStages, 0.10f + 0.20f * random01());
        tpdfNoiseSeedA.assign(effectiveStages, static_cast<unsigned int>(rand()));
        tpdfNoiseSeedB.assign(effectiveStages, static_cast<unsigned int>(rand()));
    }

    float ProcessSample(float inputSample)
    {
        if (stages.empty())
            return inputSample;

        float sample = inputSample;

        for (size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex)
        {
            const float baseDelayMs = perStageDelayMs[stageIndex];

            const float tpdf  = generateTPDF(stageIndex);
            const float alpha = computeNoiseAlpha(jitterRateHz[stageIndex]);

            jitterLPState[stageIndex] +=
                alpha * (tpdf - jitterLPState[stageIndex]);

            const float depthPercent = jitterDepthPercent[stageIndex];
            const float jitterMs     = baseDelayMs * depthPercent * jitterLPState[stageIndex];

            const float jitterSamples =
                static_cast<float>(((baseDelayMs + jitterMs) * sampleRate) / 1000.0);

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

    std::vector<float>        jitterLPState;
    std::vector<float>        jitterDepthPercent;
    std::vector<float>        jitterRateHz;
    std::vector<unsigned int> tpdfNoiseSeedA;
    std::vector<unsigned int> tpdfNoiseSeedB;

    int   cachedStageCount = 6;
    float cachedSize01     = 0.0f;

    // Distributes numberOfStages taps evenly across the full sourceTunings span.
    //
    // Q1  -> one tap near the centre of the tuning list
    // Q2  -> one tap at each end
    // Q4  -> four taps spread across the full list
    // Q8  -> all eight tunings (at full quality)
    //
    // This means lowering quality never shortens the overall diffusion time —
    // it only reduces tap density, giving a sparser, more echoy character.
    std::vector<float> BuildQualityDistributedStageDelays(
        const std::vector<float>& sourceTunings,
        int numberOfStages,
        float size01) const
    {
        const int clampedStageCount = std::max(1, numberOfStages);
        const float clampedSize01   = std::max(0.0f, std::min(1.0f, size01));

        if (sourceTunings.empty())
            return {};

        const int sourceCount = static_cast<int>(sourceTunings.size());
        const int outputCount = std::min(clampedStageCount, sourceCount);

        std::vector<float> finalDelays;
        finalDelays.reserve(static_cast<size_t>(outputCount));

        if (outputCount == 1)
        {
            const int centerIndex = sourceCount / 2;
            const float scaledMilliseconds =
                sourceTunings[static_cast<size_t>(centerIndex)]
                * (0.25f + 0.75f * clampedSize01);

            finalDelays.push_back(scaledMilliseconds);
            return finalDelays;
        }

        for (int stageIndex = 0; stageIndex < outputCount; ++stageIndex)
        {
            const float normalizedPosition =
                static_cast<float>(stageIndex) / static_cast<float>(outputCount - 1);

            const float sourceIndexFloat =
                normalizedPosition * static_cast<float>(sourceCount - 1);

            const int sourceIndexA =
                static_cast<int>(std::floor(sourceIndexFloat));

            const int sourceIndexB =
                std::min(sourceIndexA + 1, sourceCount - 1);

            const float fraction =
                sourceIndexFloat - static_cast<float>(sourceIndexA);

            const float interpolatedMilliseconds =
                sourceTunings[static_cast<size_t>(sourceIndexA)] * (1.0f - fraction)
                + sourceTunings[static_cast<size_t>(sourceIndexB)] * fraction;

            const float scaledMilliseconds =
                interpolatedMilliseconds * (0.25f + 0.75f * clampedSize01);

            finalDelays.push_back(scaledMilliseconds);
        }

        return finalDelays;
    }

    float generateTPDF(size_t stageIndex)
    {
        const float a = uniform01(tpdfNoiseSeedA[stageIndex]);
        const float b = uniform01(tpdfNoiseSeedB[stageIndex]);
        return (a + b) - 1.0f;
    }

    float uniform01(unsigned int& seed)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed)
               / static_cast<float>(std::numeric_limits<unsigned int>::max());
    }

    float computeNoiseAlpha(float targetRateHz) const
    {
        const float rate  = std::max(0.01f, targetRateHz);
        const float omega = 2.0f * juce::MathConstants<float>::pi * rate;
        const float x     = std::exp(-omega / static_cast<float>(sampleRate));
        return juce::jlimit(0.0001f, 0.2f, 1.0f - x);
    }

    float random01() const
    {
        return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    }
};