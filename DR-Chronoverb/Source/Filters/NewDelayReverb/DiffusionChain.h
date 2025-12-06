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

        jitterPhase.resize(numberOfStages);
        jitterRate.resize(numberOfStages);

        // Init random LFOs per stage
        for (int i = 0; i < numberOfStages; ++i)
        {
            jitterPhase[i] = float(rand()) / float(RAND_MAX); // random start
            jitterRate[i] = 0.001f + 0.004f * (float(rand()) / float(RAND_MAX)); // 0.001..0.005
        }
    }

    // Process a single sample through the diffusion chain.
    float ProcessSample(float inputSample)
    {
        if (stages.empty())
            return inputSample;

        float sample = inputSample;

        for (size_t i = 0; i < stages.size(); ++i)
        {
            float baseDelayMs = perStageDelayMs[i];

            // Slow sine-modulated jitter
            float jitterAmount = 0.02f * baseDelayMs; // ±2% jitter (tunable)
            float phase = jitterPhase[i];
            jitterPhase[i] += jitterRate[i];
            if (jitterPhase[i] > 1.0f) jitterPhase[i] -= 1.0f;
            float jitterMs = jitterAmount * std::sin(2.0f * float(M_PI) * phase);
            float finalDelayMs = baseDelayMs + jitterMs;

            stages[i]->SetDelayMilliseconds(finalDelayMs);

            sample = stages[i]->ProcessSample(sample);
        }

        sampleCounter++;
        return sample;
    }

private:
    double sampleRate = 48000.0;
    std::vector<std::unique_ptr<DiffusionAllpass>> stages;

    int cachedStageCount = 6;
    float cachedSize01 = 0.0f;

    std::vector<float> perStageDelayMs;

    std::vector<float> jitterPhase; // Per-stage phase
    std::vector<float> jitterRate;  // Per-stage rate (e.g., 0.001..0.01 per sample, randomized)

    int sampleCounter;
};