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
    // - numberOfStages: recommended 4..8 for typical diffusion density.
    // - size01: 0..1 scales the per-stage delay milliseconds.
    void Configure(int numberOfStages, float size01)
    {
        cachedStageCount = std::max(1, numberOfStages);
        cachedSize01 = std::max(0.0f, std::min(1.0f, size01));

        stages.clear();
        stages.reserve(static_cast<size_t>(cachedStageCount));

        perStageDelayMs.clear();

        for (int stageIndex = 0; stageIndex < cachedStageCount; ++stageIndex)
        {
            const float positionAlongChain01 = (cachedStageCount > 1)
                ? (static_cast<float>(stageIndex) / static_cast<float>(cachedStageCount - 1))
                : 0.0f;

            const float baseMilliseconds = 10.0f + (90.0f * positionAlongChain01);

            const float scaledMilliseconds =
                baseMilliseconds * (0.25f + 0.75f * (0.25f + cachedSize01));

            const float allpassGain = 0.65f;

            auto diffusionStage = std::make_unique<DiffusionAllpass>();
            diffusionStage->Prepare(sampleRate);
            diffusionStage->Configure(scaledMilliseconds, allpassGain);
            stages.push_back(std::move(diffusionStage));

            perStageDelayMs.push_back(scaledMilliseconds);
        }

        updateEstimatedGroupDelayMs();
        updateEstimatedClusterWidthMs();
    }

    // Process a single sample through the diffusion chain with crossfade amount.
    // amount01 == 0.0 -> dry passthrough
    // amount01 == 1.0 -> fully diffused
    float ProcessSample(float inputSample, float amount01)
    {
        const float amountClamped = std::max(0.0f, std::min(1.0f, amount01));

        if (stages.empty() || amountClamped <= 0.0001f)
            return inputSample;

        float x = inputSample;

        // Serial allpass chain
        for (auto& stage : stages)
            x = stage->ProcessSample(x);

        // Linear crossfade dry/wet
        return inputSample * (1.0f - amountClamped) + x * amountClamped;
    }

    float GetEstimatedGroupDelayMilliseconds() const
    {
        return estimatedGroupDelayMs;
    }

    float GetEstimatedClusterWidthMilliseconds() const
    {
        return estimatedClusterWidthMs;
    }

private:
    double sampleRate = 48000.0;
    std::vector<std::unique_ptr<DiffusionAllpass>> stages;

    int cachedStageCount = 6;
    float cachedSize01 = 0.0f;

    std::vector<float> perStageDelayMs;
    float estimatedGroupDelayMs = 0.0f;
    float estimatedClusterWidthMs = 0.0f;

    void updateEstimatedGroupDelayMs()
    {
        // Simple estimate: sum of per-stage delays.
        // This approximates low-frequency group delay of cascaded delay-based allpasses.
        float sumMs = 0.0f;

        for (float delayMs : perStageDelayMs)
            sumMs += delayMs;

        estimatedGroupDelayMs = sumMs;
    }

    void updateEstimatedClusterWidthMs()
    {
        // Simple width heuristic:
        // - Width grows with number of stages and their delays.
        // - Use RMS of per-stage delays times sqrt(stageCount) to approximate spread,
        //   then scale slightly to keep the leading edge early but not too far.
        if (perStageDelayMs.empty())
        {
            estimatedClusterWidthMs = 0.0f;
            return;
        }

        double sumSquares = 0.0;

        for (float d : perStageDelayMs)
            sumSquares += static_cast<double>(d) * static_cast<double>(d);

        const double rms = std::sqrt(sumSquares / static_cast<double>(perStageDelayMs.size()));
        const double spreadFactor = std::sqrt(static_cast<double>(perStageDelayMs.size()));

        // Tweak factor ~0.8 to keep half-width compensation perceptually centered toward the tap.
        estimatedClusterWidthMs = static_cast<float>(rms * spreadFactor * 0.8);
    }
};