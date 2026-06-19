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

    {
        constexpr float Pi = juce::MathConstants<float>::pi;
        const float omega = 2.0f * Pi * std::max(0.01f, jitterRateHz);
        jitterAlpha = std::exp(-omega / static_cast<float>(sampleRate));
        jitterIntervalSamples = std::max(1, static_cast<int>(std::round(sampleRate / jitterRateHz)));
    }

    for (size_t i = 0; i < MaxStages; ++i)
    {
        jitterTargets[i] = 0.0f;
        jitterSmoothedOffsets[i] = 0.0f;

        allpasses[i].SetJitterSmoothingAlpha(jitterAlpha);
    }

    jitterCountdown = jitterIntervalSamples;

    rebuildStageDelays();
    Reset();

    gainSlewCoefficient = 1.0f / (0.01f * static_cast<float>(sampleRate));
}

void DeverbDiffusionChain::Reset()
{
    for (auto& allpass : allpasses)
        allpass.Clear();

    jitterCountdown = jitterIntervalSamples;

    for (size_t i = 0; i < MaxStages; ++i)
    {
        jitterTargets[i] = 0.0f;
        jitterSmoothedOffsets[i] = 0.0f;
    }
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

void DeverbDiffusionChain::SetJitterRate(float newRateHz)
{
    jitterRateHz = std::max(0.01f, newRateHz);

    constexpr float Pi = juce::MathConstants<float>::pi;
    const float omega = 2.0f * Pi * jitterRateHz;
    jitterAlpha = std::exp(-omega / static_cast<float>(sampleRate));

    jitterIntervalSamples = std::max(1, static_cast<int>(std::round(sampleRate / jitterRateHz)));

    for (auto& allpass : allpasses)
        allpass.SetJitterSmoothingAlpha(jitterAlpha);
}

void DeverbDiffusionChain::SetJitterDepth(float newDepthMs)
{
    jitterDepthMs = std::max(0.0f, newDepthMs);
}

void DeverbDiffusionChain::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = std::clamp(newAmount01, 0.0f, 1.0f);
}

float DeverbDiffusionChain::ProcessSample(float inputSample)
{
    float sample = inputSample;

    if (--jitterCountdown <= 0)
    {
        jitterCountdown = jitterIntervalSamples;
        updateJitterTargets();

        for (size_t i = 0; i < MaxStages; ++i)
            jitterSmoothedOffsets[i] += (1.0f - jitterAlpha) * (jitterTargets[i] - jitterSmoothedOffsets[i]);

        pushJitterTargetsToAllpasses();
    }

    for (size_t stageIndex = 0; stageIndex < activeStages; ++stageIndex)
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

    for (size_t stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
    {
        const float delayMs = stageTuningsMs[stageIndex] * scale;
        allpasses[stageIndex].SetDelayMilliseconds(delayMs);

        if (stageIndex < activeStages)
            totalChainDelayMs += delayMs;
    }
}

void DeverbDiffusionChain::updateJitterTargets()
{
    for (size_t i = 0; i < MaxStages; ++i)
        jitterTargets[i] = jitterDist(rng) * jitterDepthMs;
}

void DeverbDiffusionChain::pushJitterTargetsToAllpasses()
{
    const float scale = 0.25f + (0.75f * size01);

    for (size_t i = 0; i < MaxStages; ++i)
    {
        const float baseDelayMs = stageTuningsMs[i] * scale;
        const float targetDelayMs = std::max(1.0f, baseDelayMs + jitterSmoothedOffsets[i]);
        allpasses[i].SetTargetDelayMilliseconds(targetDelayMs);
    }
}