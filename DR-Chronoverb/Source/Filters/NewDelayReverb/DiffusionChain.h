#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <cassert>
#include <limits>

#include <juce_audio_basics/juce_audio_basics.h>

#include "DiffusionAllpass.h"

class DiffusionChain
{
public:
    DiffusionChain() {}
    ~DiffusionChain() {}

    void Prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
    }

    void Configure(int numberOfStages, float size01, float jitterPercent,
        float jitterRate, const std::vector<float>& tunings)
    {
        cachedStageCount = std::max(1, numberOfStages);
        cachedSize01 = std::max(0.0f, std::min(1.0f, size01));

        stages.clear();
        stages.reserve(static_cast<size_t>(cachedStageCount));
        perStageDelayMs.clear();

        const std::vector<float> finalDelays =
            BuildQualityDistributedStageDelays(tunings, cachedStageCount, cachedSize01);

        baseStageDelayMsAtFullSize =
            BuildQualityDistributedStageDelays(tunings, cachedStageCount, 1.0f);

        for (float stageDelayMilliseconds : finalDelays)
        {
            auto diffusionStage = std::make_unique<DiffusionAllpass>();
            diffusionStage->Prepare(sampleRate);
            diffusionStage->Configure(stageDelayMilliseconds, 0.7f);
            stages.push_back(std::move(diffusionStage));
            perStageDelayMs.push_back(stageDelayMilliseconds);
        }

        // Initialize the live scaled delay — starts equal to the configured delay.
        currentScaledDelayMs = perStageDelayMs;

        const int effectiveStages = static_cast<int>(perStageDelayMs.size());

        jitterLPState.assign(effectiveStages, 0.0f);
        jitterDepthPercent.assign(effectiveStages, jitterPercent);
        jitterRateHz.assign(effectiveStages, jitterRate * random01());

        tpdfNoiseSeedA.assign(effectiveStages, static_cast<unsigned int>(rand()));
        tpdfNoiseSeedB.assign(effectiveStages, static_cast<unsigned int>(rand()));
    }

    /*void ConfigureAsReverb(int numberOfStages, float size01, const std::vector<float>& reverbTunings)
    {
        cachedStageCount = std::max(1, numberOfStages);
        cachedSize01 = std::max(0.0f, std::min(1.0f, size01));

        stages.clear();
        stages.reserve(static_cast<size_t>(cachedStageCount));
        perStageDelayMs.clear();

        const std::vector<float> finalDelays =
            BuildQualityDistributedStageDelays(reverbTunings, cachedStageCount, cachedSize01);

        baseStageDelayMsAtFullSize =
            BuildQualityDistributedStageDelays(reverbTunings, cachedStageCount, 1.0f);

        for (float stageDelayMilliseconds : finalDelays)
        {
            auto diffusionStage = std::make_unique<DiffusionAllpass>();
            diffusionStage->Prepare(sampleRate);
            diffusionStage->Configure(stageDelayMilliseconds, 0.7f);
            stages.push_back(std::move(diffusionStage));
            perStageDelayMs.push_back(stageDelayMilliseconds);
        }

        // Initialize the live scaled delay — starts equal to the configured delay.
        currentScaledDelayMs = perStageDelayMs;

        const int effectiveStages = static_cast<int>(perStageDelayMs.size());

        jitterLPState.assign(effectiveStages, 0.0f);
        jitterDepthPercent.assign(effectiveStages, 0.003f);
        jitterRateHz.assign(effectiveStages, 0.10f + 0.20f * random01());

        tpdfNoiseSeedA.assign(effectiveStages, static_cast<unsigned int>(rand()));
        tpdfNoiseSeedB.assign(effectiveStages, static_cast<unsigned int>(rand()));
    }*/

    float ProcessSample(float inputSample)
    {
        if (stages.empty())
            return inputSample;

        float sample = inputSample;

        for (size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex)
        {
            // Use the live scaled delay (slewed by UpdateSize) as the base for jitter.
            // This is the fix: previously perStageDelayMs was used here, which never
            // changed and overwrote whatever UpdateSize had set.
            const float liveBaseDelayMs = currentScaledDelayMs[stageIndex];

            const float tpdf  = generateTPDF(stageIndex);
            const float alpha = computeNoiseAlpha(jitterRateHz[stageIndex]);

            jitterLPState[stageIndex] +=
                alpha * (tpdf - jitterLPState[stageIndex]);

            const float depthPercent = jitterDepthPercent[stageIndex];
            const float jitterMs     = liveBaseDelayMs * depthPercent * jitterLPState[stageIndex];

            const float jitterSamples =
                static_cast<float>(((liveBaseDelayMs + jitterMs) * sampleRate) / 1000.0);

            stages[stageIndex]->SetCurrentDelaySamples(jitterSamples);
            sample = stages[stageIndex]->ProcessSample(sample);
        }

        return sample;
    }

    // Smoothly slews currentScaledDelayMs toward the target defined by newSize01.
    // ProcessSample reads currentScaledDelayMs, so this now has real effect.
    void UpdateSize(float newSize01)
    {
        cachedSize01 = juce::jlimit(0.0f, 1.0f, newSize01);
        const float Scale = 0.25f + 0.75f * cachedSize01;

        for (size_t StageIndex = 0;
             StageIndex < stages.size() && StageIndex < baseStageDelayMsAtFullSize.size();
             ++StageIndex)
        {
            const float TargetMs = baseStageDelayMsAtFullSize[StageIndex] * Scale;

            // Slew currentScaledDelayMs in ms-space so the jitter base tracks smoothly.
            // This produces the intended "pitch warp" on size changes.
            constexpr float kMaxDeltaMs = 0.05f * 1000.0f / 48000.0f; // ~1ms/sec at 48kHz
            const float delta = juce::jlimit(-kMaxDeltaMs, kMaxDeltaMs,
                                             TargetMs - currentScaledDelayMs[StageIndex]);
            currentScaledDelayMs[StageIndex] += delta;
        }
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

    // Live, slewed delay values used as the base in ProcessSample.
    // Initialized from perStageDelayMs and slewed toward target by UpdateSize.
    std::vector<float> currentScaledDelayMs;

    std::vector<float> jitterLPState;
    std::vector<float> jitterDepthPercent;
    std::vector<float> jitterRateHz;
    std::vector<unsigned int> tpdfNoiseSeedA;
    std::vector<unsigned int> tpdfNoiseSeedB;

    int cachedStageCount = 6;
    float cachedSize01 = 0.0f;

    std::vector<float> baseStageDelayMsAtFullSize;

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