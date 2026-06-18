#include "DeverbDiffusionChain.h"

void DeverbDiffusionChain::Prepare(double newSampleRate)
{
    sampleRate = std::max(1.0, newSampleRate);

    totalTuningMs = 0.0f;

    for (int i = 0; i < MaxStages; ++i)
        totalTuningMs += stageTuningsMs[i];

    for (auto& allpass : allpasses)
        allpass.Prepare(sampleRate);

    rebuildStageDelays();
    updateStageGains();
    Reset();
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
    updateStageGains();
}

void DeverbDiffusionChain::SetSize(float newSize01)
{
    //size01 = std::clamp(newSize01, 0.0f, 1.0f);
    size01 = newSize01;
    rebuildStageDelays();
}

void DeverbDiffusionChain::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = std::clamp(newAmount01, 0.0f, 1.0f);
    updateStageGains();
}

float DeverbDiffusionChain::ProcessSample(float inputSample)
{
    float sample = inputSample;

    for (int stageIndex = 0; stageIndex < activeStages; ++stageIndex)
        sample = allpasses[stageIndex].ProcessSample(sample);

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

void DeverbDiffusionChain::updateStageGains()
{
    const float gainDrive = std::min(1.0f, diffusionAmount * 2.0f);
    const float baseGain = gainDrive * MaxAllpassGain;

    static constexpr float stageMultipliers[MaxStages] =
    {
        1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f
    };

    for (int stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
        allpasses[stageIndex].SetGain(baseGain * stageMultipliers[stageIndex]);
}

/*void DeverbDiffusionChain::updateStageGains()
{
    const float gainDrive = std::min(1.0f, diffusionAmount * 2.0f);
    const float baseGain = gainDrive * MaxAllpassGain;

    for (int stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
    {
        const float stageTaper =
            juce::jmap(static_cast<float>(stageIndex),
                       0.0f, static_cast<float>(MaxStages - 1),
                       1.00f, 0.82f);

        allpasses[stageIndex].SetGain(baseGain * stageTaper);
    }
}*/

/*void DeverbDiffusionChain::updateStageGains()
{
    // Gain saturates by amount=0.5 so the chain character is largely established
    // in the lower half, while the upper half behaves more like stronger path exposure.
    const float gainDrive = std::min(1.0f, diffusionAmount * 2.0f);
    const float gain = gainDrive * MaxAllpassGain;

    for (int stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
        allpasses[stageIndex].SetGain(gain);
}*/