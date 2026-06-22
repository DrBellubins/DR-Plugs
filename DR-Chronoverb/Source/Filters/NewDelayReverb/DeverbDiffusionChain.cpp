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

    updateStageJitterDepths();
    initializeDriftState();

    rebuildStageDelays();
    Reset();

    gainSlewCoefficient = 1.0f / (0.01f * static_cast<float>(sampleRate));         // ~10 ms

    // Prevent startup
    targetQualityCompensation  = 1.0f;

    distributedTuningsMs = buildDistributedTunings(activeStages);
    distributedGainMultipliers = buildDistributedGains(targetStageGains, activeStages);

    // Depths depend on distributed tunings / stage layout too, so refresh once more after distribution exists.
    updateStageJitterDepths();
    initializeDriftState();
}

void DeverbDiffusionChain::Reset()
{
    for (auto& allpass : allpasses)
        allpass.Clear();

    chainEnvelope = 0.0f;

    initializeDriftState();
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
    updateStageJitterDepths();
}

void DeverbDiffusionChain::SetDiffusionQuality(int newStageCount)
{
    activeStages = static_cast<size_t>(std::clamp(newStageCount, 1, MaxStages));

    distributedTuningsMs = buildDistributedTunings(activeStages);
    distributedGainMultipliers = buildDistributedGains(targetStageGains, activeStages);

    rebuildStageDelays();

    // No compensation
    targetQualityCompensation = 1.0f;

    updateStageJitterDepths();
    initializeDriftState();
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

    const float absInput = std::abs(inputSample);

    if (absInput > chainEnvelope)
        chainEnvelope = absInput;
    else
        chainEnvelope *= 0.9999f;

    const bool driftActive = (chainEnvelope > LfoGateThreshold);

    for (size_t stageIndex = 0; stageIndex < activeStages; ++stageIndex)
    {
        currentStageGains[stageIndex] += gainSlewCoefficient *
            (distributedGainMultipliers[stageIndex] - currentStageGains[stageIndex]);

        allpasses[stageIndex].SetGain(currentStageGains[stageIndex]);

        const float scale = 0.25f + (0.75f * size01);
        const float baseDelayMs = distributedTuningsMs[stageIndex] * scale;

        float driftOffsetMs = 0.0f;

        if (driftActive)
        {
            if (driftSamplesUntilRetarget[stageIndex] <= 0)
                retargetStageDrift(stageIndex);

            driftCurrentMs[stageIndex] += driftStepPerSample[stageIndex];
            --driftSamplesUntilRetarget[stageIndex];

            driftOffsetMs = driftCurrentMs[stageIndex];
        }

        const float modulatedDelayMs = std::max(1.0f, baseDelayMs + driftOffsetMs);
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

void DeverbDiffusionChain::initializeDriftState()
{
    for (size_t stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
    {
        driftCurrentMs[stageIndex] = 0.0f;
        driftTargetMs[stageIndex] = 0.0f;
        driftStepPerSample[stageIndex] = 0.0f;
        driftSamplesUntilRetarget[stageIndex] = 0;

        retargetStageDrift(stageIndex);
    }
}

void DeverbDiffusionChain::retargetStageDrift(size_t stageIndex)
{
    const float depthMs = stageJitterDepthMs[stageIndex]
                        * getQualityJitterScale()
                        * getAmountJitterScale()
                        * jitterGlobalDepthScale;

    const float nextTargetMs = random.nextFloat() * 2.0f * depthMs - depthMs;

    const float retargetMs = juce::jmap(random.nextFloat(),
        driftRetargetMinMs, driftRetargetMaxMs);

    const int samplesToTarget = std::max(1,
        static_cast<int>(std::round((retargetMs * 0.001f) * static_cast<float>(sampleRate))));

    driftTargetMs[stageIndex] = nextTargetMs;
    driftSamplesUntilRetarget[stageIndex] = samplesToTarget;
    driftStepPerSample[stageIndex] =
        (driftTargetMs[stageIndex] - driftCurrentMs[stageIndex]) / static_cast<float>(samplesToTarget);
}

void DeverbDiffusionChain::updateStageJitterDepths()
{
    // Safety behavior:
    // - shortest stages get far less relative modulation
    // - longer stages can move a little more
    // - values remain subtle enough to avoid chorus-y sweep
    for (size_t stageIndex = 0; stageIndex < MaxStages; ++stageIndex)
    {
        const float tuningMs =
            (stageIndex < distributedTuningsMs.size() && distributedTuningsMs[stageIndex] > 0.0f)
                ? distributedTuningsMs[stageIndex]
                : stageTuningsMs[stageIndex];

        // Relative depth: about 0.6% to 1.8% depending on tuning length,
        // clamped to a very conservative absolute range.
        const float relativeDepth = tuningMs * juce::jmap(tuningMs,
            3.0f, 83.0f,
            0.006f, 0.018f);

        stageJitterDepthMs[stageIndex] = juce::jlimit(0.008f, 0.09f, relativeDepth);
    }
}

float DeverbDiffusionChain::getQualityJitterScale() const
{
    // Fewer active stages = more exposed comb structure,
    // so reduce jitter depth accordingly.
    const float quality01 =
        static_cast<float>(activeStages - 1) / static_cast<float>(MaxStages - 1);

    return juce::jmap(quality01, 0.45f, 1.0f);
}

float DeverbDiffusionChain::getAmountJitterScale() const
{
    // Conservative below 0.5 where the tap structure is more exposed.
    // More freedom in the higher diffuse/wash region.
    if (diffusionAmount <= 0.5f)
    {
        const float lower01 = juce::jlimit(0.0f, 1.0f, diffusionAmount / 0.5f);
        return juce::jmap(lower01, 0.35f, 0.75f);
    }

    const float upper01 = juce::jlimit(0.0f, 1.0f, (diffusionAmount - 0.5f) / 0.5f);
    return juce::jmap(upper01, 0.75f, 1.0f);
}

//endregion