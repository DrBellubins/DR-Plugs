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

    distributedTuningsMs = buildDistributedTunings(activeStages);
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
    distributedTuningsMs = buildDistributedTunings(activeStages);
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

    // Track signal energy to gate LFO modulation
    const float absInput = std::abs(inputSample);
    if (absInput > chainEnvelope)
        chainEnvelope = absInput;
    else
        chainEnvelope *= 0.9999f; // slow release

    const bool lfoActive = (chainEnvelope > LfoGateThreshold);

    for (size_t stageIndex = 0; stageIndex < activeStages; ++stageIndex)
    {
        currentStageGains[stageIndex] += gainSlewCoefficient *
            (targetStageGains[stageIndex] - currentStageGains[stageIndex]);

        allpasses[stageIndex].SetGain(currentStageGains[stageIndex]);

        lfoPhases[stageIndex] += lfoRates[stageIndex];

        if (lfoPhases[stageIndex] >= juce::MathConstants<float>::twoPi)
            lfoPhases[stageIndex] -= juce::MathConstants<float>::twoPi;

        const float scale = 0.25f + (0.75f * size01);
        const float baseDelayMs = distributedTuningsMs[stageIndex] * scale;

        // Only apply LFO offset when signal is present
        const float lfoOffsetMs = lfoActive
            ? std::sin(lfoPhases[stageIndex]) * LfoDepthMs
            : 0.0f;

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
    const float sizeScale = 0.25f + (0.75f * size01);

    float distributedTotal = 0.0f;

    for (size_t i = 0; i < activeStages; ++i)
        distributedTotal += distributedTuningsMs[i];

    const float preserveScale = (distributedTotal > 0.0f) ? (totalTuningMs / distributedTotal) : 1.0f;

    totalChainDelayMs = 0.0f;

    for (size_t stageIndex = 0; stageIndex < activeStages; ++stageIndex)
    {
        const float delayMs = distributedTuningsMs[stageIndex] * preserveScale * sizeScale;

        allpasses[stageIndex].SetDelayMilliseconds(delayMs);
        allpasses[stageIndex].SetTargetDelayMilliseconds(delayMs);

        totalChainDelayMs += delayMs;
    }
}

std::array<float, DeverbDiffusionChain::MaxStages>
DeverbDiffusionChain::buildDistributedTunings(size_t outputStages) const
{
    std::array<float, MaxStages> distributed{};

    constexpr size_t sourceCount = MaxStages;
    const size_t clampedOutputStages = std::clamp(outputStages, size_t{1}, sourceCount);

    if (clampedOutputStages == 1)
    {
        // Average the whole chain rather than grabbing the middle tuning,
        // so a single active stage still represents the full size character.
        float sum = 0.0f;
        for (float tuning : stageTuningsMs)
            sum += tuning;

        distributed[0] = sum / static_cast<float>(sourceCount);
        return distributed;
    }

    for (size_t stageIndex = 0; stageIndex < clampedOutputStages; ++stageIndex)
    {
        const float normalizedPosition =
            static_cast<float>(stageIndex) / static_cast<float>(clampedOutputStages - 1);

        const float sourceIndexFloat = normalizedPosition * static_cast<float>(sourceCount - 1);

        const size_t sourceIndexA = static_cast<size_t>(std::floor(sourceIndexFloat));
        const size_t sourceIndexB = std::min(sourceIndexA + 1, sourceCount - 1);

        const float fraction = sourceIndexFloat - static_cast<float>(sourceIndexA);

        distributed[stageIndex] = stageTuningsMs[sourceIndexA] * (1.0f - fraction)
                                 + stageTuningsMs[sourceIndexB] * fraction;
    }

    return distributed;
}