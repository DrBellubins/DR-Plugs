#include "DeverbDiffusionChain.h"

void DeverbDiffusionChain::Prepare(double newSampleRate)
{
    sampleRate = std::max(1.0, newSampleRate);

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
    size01 = std::clamp(newSize01, 0.0f, 1.0f);
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

float DeverbDiffusionChain::GetBlendAmount() const
{
    // Equal-power style reveal/blend curve.
    return std::sin(diffusionAmount * juce::MathConstants<float>::halfPi);
}

float DeverbDiffusionChain::GetCompensationMs() const
{
    // At amount=0 -> full compensation
    // At amount=1 -> no compensation
    const float reveal = GetBlendAmount();
    return totalChainDelayMs * (1.0f - reveal);
}

void DeverbDiffusionChain::rebuildStageDelays()
{
    // Keep the same scaling concept you wanted:
    // tight at low size, wide at high size.
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
    // Gain saturates by amount=0.5, matching your proposed design.
    const float gainDrive = std::min(1.0f, diffusionAmount * 2.0f);
    const float gain = gainDrive * MaxAllpassGain;

    for (int stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
        allpasses[stageIndex].SetGain(gain);
}