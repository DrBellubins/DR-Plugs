#include "DeverbDiffusionChain.h"

void DeverbDiffusionChain::Prepare(double newSampleRate, std::array<float, MaxStages> stageTunings)
{
    sampleRate = std::max(1.0, newSampleRate);
    stageTuningsMs = stageTunings;

    totalTuningMs = 0.0f;

    for (int i = 0; i < MaxStages; ++i)
        totalTuningMs += stageTuningsMs[i];

    for (auto& allpass : allpasses)
        allpass.Prepare(sampleRate);

    rebuildStageDelays();
    Reset();

    gainSlewCoefficient = 1.0f / (0.01f * static_cast<float>(sampleRate)); // ~10 ms
}

void DeverbDiffusionChain::Reset()
{
    for (auto& allpass : allpasses)
        allpass.Clear();
}

void DeverbDiffusionChain::SetQuality(int newStageCount)
{
    activeStages = std::clamp(newStageCount, 1, MaxStages);
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

    for (int stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
        targetStageGains[stageIndex] = baseGain * stageGains[stageIndex];
}

void DeverbDiffusionChain::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = std::clamp(newAmount01, 0.0f, 1.0f);
}

float DeverbDiffusionChain::ProcessSample(float inputSample)
{
    float sample = inputSample;

    for (int stageIndex = 0; stageIndex < activeStages; ++stageIndex)
    {
        currentStageGains[stageIndex] += gainSlewCoefficient *
            (targetStageGains[stageIndex] - currentStageGains[stageIndex]);

        allpasses[stageIndex].SetGain(currentStageGains[stageIndex]);
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

    for (int stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
    {
        const float delayMs = stageTuningsMs[stageIndex] * scale;
        allpasses[stageIndex].SetDelayMilliseconds(delayMs);

        if (stageIndex < activeStages)
            totalChainDelayMs += delayMs;
    }
}