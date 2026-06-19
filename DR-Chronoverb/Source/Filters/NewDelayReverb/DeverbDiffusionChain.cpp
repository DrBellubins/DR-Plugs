#include "DeverbDiffusionChain.h"

void DeverbDiffusionChain::Prepare(double newSampleRate, std::array<float, MaxStages> stageTunings)
{
    sampleRate = std::max(1.0, newSampleRate);
    stageTuningsMs = stageTunings;

    totalTuningMs = 0.0f;

    for (size_t i = 0; i < MaxStages; ++i)
        totalTuningMs += stageTuningsMs[i];

    for (auto& allpass : allpasses)
        allpass.Prepare(sampleRate);

    // Initialize LFO phases spread evenly to decorrelate stages
    for (size_t i = 0; i < MaxStages; ++i)
    {
        const float phaseOffset = (juce::MathConstants<float>::twoPi / MaxStages) * static_cast<float>(i);
        lfoPhases[i] = phaseOffset;

        // Each stage gets a slightly different rate to prevent synchronised beating
        const float rateVariance = 1.0f + (static_cast<float>(i) * 0.07f);  // 0%, 7%, 14%... per stage

        lfoRates[i] = (juce::MathConstants<float>::twoPi * LfoBaseRateHz * rateVariance)
                      / static_cast<float>(sampleRate);
    }

    rebuildStageDelays();
    Reset();

    gainSlewCoefficient = 1.0f / (0.01f * static_cast<float>(sampleRate));
}

void DeverbDiffusionChain::Reset()
{
    for (auto& allpass : allpasses)
        allpass.Clear();

    for (size_t i = 0; i < MaxStages; ++i)
        lfoPhases[i] = (juce::MathConstants<float>::twoPi / MaxStages) * static_cast<float>(i);
}

void DeverbDiffusionChain::SetQuality(int newStageCount)
{
    activeStages = static_cast<size_t>(std::clamp(newStageCount, 1, MaxStages));
    rebuildStageDelays();
}

void DeverbDiffusionChain::SetSize(float newSize01)
{
    //size01 = std::clamp(newSize01, 0.0f, 1.0f);
    size01 = newSize01;
    rebuildStageDelays();
}

void DeverbDiffusionChain::SetStageGains(float baseGain, std::array<float, MaxStages> stageGains)
{
    targetBaseGain = baseGain;

    for (size_t stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
        targetStageGains[stageIndex] = baseGain * stageGains[stageIndex];
}

void DeverbDiffusionChain::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = std::clamp(newAmount01, 0.0f, 1.0f);
}

float DeverbDiffusionChain::ProcessSample(float inputSample)
{
    float sample = inputSample;

    for (size_t stageIndex = 0; stageIndex < activeStages; ++stageIndex)
    {
        currentStageGains[stageIndex] += gainSlewCoefficient *
            (targetStageGains[stageIndex] - currentStageGains[stageIndex]);

        allpasses[stageIndex].SetGain(currentStageGains[stageIndex]);

        lfoPhases[stageIndex] += lfoRates[stageIndex];

        if (lfoPhases[stageIndex] >= juce::MathConstants<float>::twoPi)
            lfoPhases[stageIndex] -= juce::MathConstants<float>::twoPi;

        const float scale = 0.25f + (0.75f * size01);
        const float baseDelayMs = stageTuningsMs[stageIndex] * scale;
        const float lfoOffsetMs = std::sin(lfoPhases[stageIndex]) * LfoDepthMs;
        const float modulatedDelayMs = std::max(1.0f, baseDelayMs + lfoOffsetMs);

        allpasses[stageIndex].SetTargetDelayMilliseconds(modulatedDelayMs);

        sample = allpasses[stageIndex].ProcessSample(sample);
    }

    return sample;
}

float DeverbDiffusionChain::GetTotalTuningMs() const
{
    return totalTuningMs;
}

void DeverbDiffusionChain::rebuildStageDelays()
{
    const float scale = 0.25f + (0.75f * size01);

    totalChainDelayMs = 0.0f;

    for (size_t stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
    {
        const float delayMs = stageTuningsMs[stageIndex] * scale;
        allpasses[stageIndex].SetDelayMilliseconds(delayMs);

        // Also set target to prevent discontinuity
        allpasses[stageIndex].SetTargetDelayMilliseconds(delayMs);

        if (stageIndex < activeStages)
            totalChainDelayMs += delayMs;
    }
}