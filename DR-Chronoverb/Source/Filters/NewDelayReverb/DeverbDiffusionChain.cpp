#include "DeverbDiffusionChain.h"

void DeverbDiffusionChain::Prepare(double newSampleRate, std::array<float, MaxStages> stageTunings,
    float jitterRate, float jitterDepth)
{
    sampleRate = std::max(1.0, newSampleRate);
    stageTuningsMs = stageTunings;

    jitterLfoRate = jitterRate;
    jitterLfoDepth = jitterDepth;

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

        lfoRates[i] = (juce::MathConstants<float>::twoPi * jitterLfoRate * rateVariance)
                      / static_cast<float>(sampleRate);
    }

    rebuildStageDelays();
    Reset();

    gainSlewCoefficient = 1.0f / (0.01f * static_cast<float>(sampleRate));         // ~10 ms

    // Prevent startup
    targetQualityCompensation  = 1.0f;

    distributedTuningsMs = buildDistributedTunings(activeStages);
    distributedGainMultipliers = buildDistributedGains(targetStageGains, activeStages);
}

void DeverbDiffusionChain::Reset()
{
    for (auto& allpass : allpasses)
        allpass.Clear();

    for (size_t i = 0; i < MaxStages; ++i)
        lfoPhases[i] = (juce::MathConstants<float>::twoPi / MaxStages) * static_cast<float>(i);
}

void DeverbDiffusionChain::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = std::clamp(newAmount01, 0.0f, 1.0f);
}

void DeverbDiffusionChain::SetDiffusionSize(float newSize01)
{
    //size01 = std::clamp(newSize01, 0.0f, 1.0f);
    size01 = newSize01;
    rebuildStageDelays();
}

void DeverbDiffusionChain::SetDiffusionQuality(int newStageCount)
{
    activeStages = static_cast<size_t>(std::clamp(newStageCount, 1, MaxStages));

    distributedTuningsMs = buildDistributedTunings(activeStages);
    distributedGainMultipliers = buildDistributedGains(targetStageGains, activeStages);

    rebuildStageDelays();

    // No compensation
    targetQualityCompensation = 1.0f;
}

void DeverbDiffusionChain::SetStageGains(float baseGain, std::array<float, MaxStages> stageGains)
{
    targetBaseGain = baseGain;

    for (size_t i = 0; i < MaxStages; ++i)
        targetStageGains[i] = baseGain * stageGains[i];

    distributedGainMultipliers = buildDistributedGains(targetStageGains, activeStages);

    // No compensation — gain redistribution handles consistency
    targetQualityCompensation  = 1.0f;
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
            (distributedGainMultipliers[stageIndex] - currentStageGains[stageIndex]);

        allpasses[stageIndex].SetGain(currentStageGains[stageIndex]);

        lfoPhases[stageIndex] += lfoRates[stageIndex];

        if (lfoPhases[stageIndex] >= juce::MathConstants<float>::twoPi)
            lfoPhases[stageIndex] -= juce::MathConstants<float>::twoPi;

        const float scale = 0.25f + (0.75f * size01);
        const float baseDelayMs = distributedTuningsMs[stageIndex] * scale;

        // Only apply LFO offset when signal is present
        const float lfoOffsetMs = lfoActive
            ? std::sin(lfoPhases[stageIndex]) * jitterLfoDepth
            : 0.0f;

        const float modulatedDelayMs = std::max(1.0f, baseDelayMs + lfoOffsetMs);
        allpasses[stageIndex].SetTargetDelayMilliseconds(modulatedDelayMs);

        sample = allpasses[stageIndex].ProcessSample(sample);
    }

    return sample;
}

//region Utils

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

std::array<float, DeverbDiffusionChain::MaxStages>
DeverbDiffusionChain::buildDistributedGains(
    const std::array<float, MaxStages>& source,
    size_t outputStages)
{
    std::array<float, MaxStages> distributed {};

    constexpr size_t sourceCount = MaxStages;
    const size_t clampedOutputStages = std::clamp(outputStages, size_t{1}, sourceCount);

    // Compute mean gain across the full source so all quality levels use
    // a consistent per-stage g value rather than sampling high/low endpoints.
    float meanGain = 0.0f;

    for (float v : source)
        meanGain += v;

    meanGain /= static_cast<float>(sourceCount);

    if (clampedOutputStages == sourceCount)
    {
        // Full quality — use exact source values
        distributed = source;
        return distributed;
    }

    for (size_t stageIndex = 0; stageIndex < clampedOutputStages; ++stageIndex)
    {
        const float normalizedPosition =
            static_cast<float>(stageIndex) / static_cast<float>(clampedOutputStages - 1 + (clampedOutputStages == 1 ? 1 : 0));

        // Interpolate between the mean (at low quality) and the actual
        // source endpoint (at high quality) to avoid the endpoint-sampling
        // problem that causes g=0.92 at quality 2.
        const float sourceIndexFloat =
            normalizedPosition * static_cast<float>(sourceCount - 1);

        const auto sourceIndexA = static_cast<size_t>(std::floor(sourceIndexFloat));
        const size_t sourceIndexB = std::min(sourceIndexA + 1, sourceCount - 1);
        const float fraction = sourceIndexFloat - static_cast<float>(sourceIndexA);

        const float sampledGain = source[sourceIndexA] * (1.0f - fraction)
                                + source[sourceIndexB] * fraction;

        // Blend sampled value toward mean for reduced stage counts.
        // At full quality (clampedOutputStages==MaxStages) this branch isn't reached.
        const float qualityT = static_cast<float>(clampedOutputStages - 1)
                             / static_cast<float>(sourceCount - 1);

        distributed[stageIndex] = meanGain + qualityT * (sampledGain - meanGain);
    }

    return distributed;
}
//endregion