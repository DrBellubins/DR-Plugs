#include "Delay.h"

void Delay::PrepareToPlay(double newSampleRate, Filters& filters)
{
    sampleRate = newSampleRate;
    filtersInput = &filters;

    delayTimeSegment.PrepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMilliseconds();

    // Delay line
    delayLineLeft = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);
    delayLineRight = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);

    delayLineLeft->Clear();
    delayLineRight->Clear();

    delayLineLeft->SetSampleRate(sampleRate);
    delayLineRight->SetSampleRate(sampleRate);

    // Diffusion Read
    diffusionReadLeft = std::make_unique<DiffusionChain>();
    diffusionReadRight = std::make_unique<DiffusionChain>();

    diffusionReadLeft->Prepare(sampleRate);
    diffusionReadRight->Prepare(sampleRate);

    if (diffusionReadLeft) diffusionReadLeft->ClearState();
    if (diffusionReadRight) diffusionReadRight->ClearState();

    // Diffusion write
    diffusionWriteLeft = std::make_unique<DiffusionChain>();
    diffusionWriteRight = std::make_unique<DiffusionChain>();

    diffusionWriteLeft->Prepare(sampleRate);
    diffusionWriteRight->Prepare(sampleRate);

    if (diffusionWriteLeft) diffusionWriteLeft->ClearState();
    if (diffusionWriteRight) diffusionWriteRight->ClearState();

    // Damping
    dampingLeft = std::make_unique<DampingFilter>();
    dampingRight = std::make_unique<DampingFilter>();

    dampingLeft->Prepare(sampleRate);
    dampingRight->Prepare(sampleRate);

    dampingLeft->SetCutoffHz(7000.0f);
    dampingRight->SetCutoffHz(7000.0f);

    // Various
    lastBuiltQualityStages = -1;
    lastBuiltSize = -1.0f;

    rebuildDiffusionIfNeeded();
    updateFeedbackGainFromFeedbackTime();

    smoothedCenteredReadDelayMilliseconds = delayTimeSegment.DelayTimeMilliseconds;
    readDelaySlewCoefficient = 1.0f / (0.02f * static_cast<float>(sampleRate));
}

void Delay::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    const float timeScale = std::clamp(delayTimeSegment.DelayTimeMilliseconds / 1000.0f,
    0.05f, 1.0f); // normalize to max ms range

    diffusionReadLeft->UpdateSize(diffusionSize * timeScale);
    diffusionReadRight->UpdateSize(diffusionSize * timeScale);

    diffusionWriteLeft->UpdateSize(diffusionSize * timeScale);
    diffusionWriteRight->UpdateSize(diffusionSize * timeScale);

    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();
}

std::pair<float, float> Delay::ProcessSample(float inputSampleL, float inputSampleR)
{
    // 1) Input + feedback
    float inputFeedbackLeft = inputSampleL + lastFeedbackL;
    float inputFeedbackRight = inputSampleR + lastFeedbackR;

    // 2) Pre-filters (optional)
    if (filtersOrder == 1)
    {
        auto [filteredL, filteredR] =
            filtersInput->ProcessSample(inputFeedbackLeft, inputFeedbackRight);

        inputFeedbackLeft = filteredL;
        inputFeedbackRight = filteredR;
    }

    // 3) Pre write diffusion
    float feedbackWriteLeft = inputFeedbackLeft;
    float feedbackWriteRight = inputFeedbackRight;

    if (diffusionAmount > 0.0001f)
    {
        const float diffusedWriteLeft = diffusionWriteLeft->ProcessSample(inputFeedbackLeft);
        const float diffusedWriteRight = diffusionWriteRight->ProcessSample(inputFeedbackRight);

        // 4) Write-side blend between clean tap -> diffused tap (diff amt 0.0 -> 0.5)
        const float writeBlend01 =
            std::clamp(diffusionAmount * 2.0f, 0.0f, 1.0f);

        const float writeCleanGain =
            std::cos(writeBlend01 * juce::MathConstants<float>::halfPi);

        const float writeDiffusionGain =
            std::sin(writeBlend01 * juce::MathConstants<float>::halfPi);

        feedbackWriteLeft =
            (inputFeedbackLeft * writeCleanGain) + (diffusedWriteLeft * writeDiffusionGain);

        feedbackWriteRight =
            (inputFeedbackRight * writeCleanGain) + (diffusedWriteRight * writeDiffusionGain);
    }

    // 5) Write to delay line
    delayLineLeft->PushSample(feedbackWriteLeft);
    delayLineRight->PushSample(feedbackWriteRight);

    // 6) Read nominal and early tap
    smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
        (delayTimeSegment.DelayTimeMilliseconds - smoothedCenteredReadDelayMilliseconds);

    const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;

    const float earlyReadMilliseconds =
        std::max(1.0f, nominalReadMilliseconds - staticDiffusionCompensationMilliseconds);

    const float nominalTapLeft = delayLineLeft->ReadFeedbackBuffer(nominalReadMilliseconds);
    const float nominalTapRight = delayLineRight->ReadFeedbackBuffer(nominalReadMilliseconds);

    const float earlyTapLeft = delayLineLeft->ReadFeedbackBuffer(earlyReadMilliseconds);
    const float earlyTapRight = delayLineRight->ReadFeedbackBuffer(earlyReadMilliseconds);

    // 7) Diffuse the early tap (second pass)
    float hybridTapLeft = nominalTapLeft;
    float hybridTapRight = nominalTapRight;

    if (diffusionAmount > 0.0001f)
    {
        const float diffusedEarlyLeft = diffusionReadLeft->ProcessSample(earlyTapLeft);
        const float diffusedEarlyRight = diffusionReadRight->ProcessSample(earlyTapRight);

        // 8) Read-side blend between nominal tap -> early tap (diff amt 0.0 -> 0.5)
        const float lowerHalf01 =
            std::clamp(diffusionAmount * 2.0f, 0.0f, 1.0f);

        const float nominalGain = std::pow(1.0f - lowerHalf01, 3.0f);
        const float earlyGain = std::sin(lowerHalf01 * juce::MathConstants<float>::halfPi) * 0.75f;

        const float lowerHalfMakeupGain = 1.0f + (0.12f * std::sin(lowerHalf01 * juce::MathConstants<float>::pi));

        hybridTapLeft =
            ((nominalTapLeft * nominalGain) + (diffusedEarlyLeft * earlyGain)) * lowerHalfMakeupGain;

        hybridTapRight =
            ((nominalTapRight * nominalGain) + (diffusedEarlyRight * earlyGain)) * lowerHalfMakeupGain;
    }

    // 9) Damping
    const float dampedLeft = dampingLeft->ProcessSample(hybridTapLeft);
    const float dampedRight = dampingRight->ProcessSample(hybridTapRight);

    // 10) Recirculation
    lastFeedbackL = dampedLeft * feedbackGain;
    lastFeedbackR = dampedRight * feedbackGain;

    return std::make_pair(dampedLeft, dampedRight);
}

//region Parameters

void Delay::SetHostTempo(float bpm)
{
    hostBPM = bpm;
    delayTimeSegment.SetHostTempo(bpm);
}

void Delay::SetDelayTime(float newDelayTime)
{
    delayTimeSegment.SetDelayTime(newDelayTime);
}

void Delay::SetDelayMode(int newDelayMode)
{
    delayTimeSegment.SetDelayMode(newDelayMode);
}

void Delay::SetFeedbackTime(float newFeedbackTime)
{
    feedbackTimeSeconds = newFeedbackTime;
    updateFeedbackGainFromFeedbackTime();
}

void Delay::SetDiffusionAmount(float newDiffusionAmount)
{
    diffusionAmount = newDiffusionAmount;
}

void Delay::SetDiffusionSize(float newDiffusionSize)
{
    diffusionSize = newDiffusionSize * tuningLengthMultiplier;
}

void Delay::SetDiffusionQuality(int newDiffusionQuality)
{
    // Limit quality to save on CPU usage.
    diffusionQualityStages = std::max(newDiffusionQuality / 2, 1);
    diffusionRebuildPending.store(true, std::memory_order_release);
}

void Delay::SetFiltersOrder(int newOrder)
{
    filtersOrder = newOrder;
}

//endregion

//region Update Functions

void Delay::rebuildDiffusionIfNeeded()
{
    if (diffusionQualityStages == lastBuiltQualityStages
        && diffusionSize == lastBuiltSize)
    {
        return;
    }

    lastBuiltQualityStages = diffusionQualityStages;
    lastBuiltSize = diffusionSize;

    // Read
    if (diffusionReadLeft != nullptr)
    {
        diffusionReadLeft->Configure(diffusionQualityStages,
            diffusionSize, 0.0f, 0.5f, Tunings);
    }

    if (diffusionReadRight != nullptr)
    {
        //auto decorrelatedTunings = DecorrelateTunings(Tunings);

        diffusionReadRight->Configure(diffusionQualityStages,
            diffusionSize, 0.0f, 0.5f, Tunings);
    }

    // Write
    if (diffusionWriteLeft != nullptr)
    {
        diffusionWriteLeft->Configure(diffusionQualityStages,
            diffusionSize, 0.0f, 0.5f, Tunings);
    }

    if (diffusionWriteRight != nullptr)
    {
        auto decorrelatedTunings = DecorrelateTunings(Tunings);

        diffusionWriteRight->Configure(diffusionQualityStages,
            diffusionSize, 0.0f, 0.5f, decorrelatedTunings);
    }

    totalDelayDiffusionMilliseconds = 0.0f;

    if (diffusionReadLeft != nullptr)
    {
        for (float stageDelayMilliseconds : diffusionReadLeft->perStageDelayMs)
            totalDelayDiffusionMilliseconds += stageDelayMilliseconds;
    }

    const float baseCompensation = totalDelayDiffusionMilliseconds * centeredSwellRatio;
    staticDiffusionCompensationMilliseconds = baseCompensation * diffusionCompensationBias;
}

void Delay::updateFeedbackGainFromFeedbackTime()
{
    const float normalized = std::clamp(feedbackTimeSeconds / 10.0f, 0.0f, 1.0f);
    const float curved = std::sqrt(normalized);
    feedbackGain = std::max(0.0f, std::min(0.85f * curved, 0.95f));
}

// endregion