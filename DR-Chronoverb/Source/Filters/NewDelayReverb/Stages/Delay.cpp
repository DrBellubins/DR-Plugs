#include "Delay.h"

void Delay::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    delayTimeSegment.PepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();

    // Delay line
    InternalDelayLineLeft = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);
    InternalDelayLineRight = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);

    InternalDelayLineLeft->Clear();
    InternalDelayLineRight->Clear();

    InternalDelayLineLeft->SetSampleRate(sampleRate);
    InternalDelayLineRight->SetSampleRate(sampleRate);

    // Diffusion Read
    diffusionReadLeft = std::make_unique<DiffusionChain>();
    diffusionReadRight = std::make_unique<DiffusionChain>();

    diffusionReadLeft->Prepare(sampleRate);
    diffusionReadRight->Prepare(sampleRate);

    if (diffusionReadLeft) diffusionReadLeft->ClearState();
    if (diffusionReadRight) diffusionReadRight->ClearState();

    // Diffusion Write
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
    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();
}

std::pair<float, float> Delay::ProcessSample(float inputSampleL, float inputSampleR)
{
    diffusionReadLeft->UpdateSize(diffusionSize);
    diffusionWriteLeft->UpdateSize(diffusionSize);

    diffusionReadRight->UpdateSize(diffusionSize);
    diffusionWriteRight->UpdateSize(diffusionSize);

    // 1) Input + feedback
    const float inputFeedbackLeft = inputSampleL + lastFeedbackL;
    const float inputFeedbackRight = inputSampleR + lastFeedbackR;

    // 2) Pre write diffusion
    const float diffusedLeft = diffusionWriteLeft->ProcessSample(inputFeedbackLeft);
    const float diffusedRight = diffusionWriteRight->ProcessSample(inputFeedbackRight);

    // 3) Blend between clean tap -> diffused tap (diff amt 0.0 -> 0.5)
    const float inputFeedbackGain = std::cos(diffusionAmount * juce::MathConstants<float>::halfPi);
    const float diffusionGain = std::sin(diffusionAmount * juce::MathConstants<float>::halfPi);

    const float feedbackWriteLeft = (inputFeedbackLeft * inputFeedbackGain) + (diffusedLeft  * diffusionGain);
    const float feedbackWriteRight = (inputFeedbackRight * inputFeedbackGain) + (diffusedRight * diffusionGain);

    // 4) Write to delay line
    InternalDelayLineLeft->PushSample(feedbackWriteLeft);
    InternalDelayLineRight->PushSample(feedbackWriteRight);

    // 5) Read nominal and early tap
    smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
            (delayTimeSegment.DelayTimeMilliseconds - smoothedCenteredReadDelayMilliseconds);

    const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;

    const float earlyReadMilliseconds =
        std::max(1.0f, delayTimeSegment.DelayTimeMilliseconds - staticDiffusionCompensationMilliseconds);

    const float nominalTapLeft = InternalDelayLineLeft->ReadFeedbackBuffer(nominalReadMilliseconds);
    const float nominalTapRight = InternalDelayLineRight->ReadFeedbackBuffer(nominalReadMilliseconds);

    const float earlyTapLeft = InternalDelayLineLeft->ReadFeedbackBuffer(earlyReadMilliseconds);
    const float earlyTapRight = InternalDelayLineRight->ReadFeedbackBuffer(earlyReadMilliseconds);

    // 6) Diffuse the early tap (second pass)
    const float diffusedEarlyLeft = diffusionReadLeft->ProcessSample(earlyTapLeft);
    const float diffusedEarlyRight = diffusionReadRight->ProcessSample(earlyTapRight);

    // 7) Blend between nominal tap -> early tap (diff amt 0.0 -> 0.5)
    const float diffusionDrive = juce::jlimit(0.0f, 1.0f, diffusionAmount * 1.25f);
    const float nominalTapGain = std::pow(1.0f - diffusionDrive, 4.0f);   // collapses to 0 at drive >= 1
    const float earlyTapGain = std::sin(diffusionDrive * juce::MathConstants<float>::halfPi);

    const float blendedTapLeft = nominalTapLeft * nominalTapGain + diffusedEarlyLeft * earlyTapGain;
    const float blendedTapRight = nominalTapRight * nominalTapGain + diffusedEarlyRight * earlyTapGain;

    // 8) Damping
    const float dampedLeft = dampingLeft->ProcessSample(blendedTapLeft, 7000.0f);
    const float dampedRight = dampingRight->ProcessSample(blendedTapRight, 7000.0f);

    // 9) Recirculation
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
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();
}

void Delay::SetDelayMode(int newDelayMode)
{
    delayTimeSegment.SetDelayMode(newDelayMode);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();
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
    diffusionQualityStages = newDiffusionQuality;
    diffusionRebuildPending.store(true, std::memory_order_release);
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
        auto decorrelatedTunings = DecorrelateTunings(Tunings);

        diffusionReadRight->Configure(diffusionQualityStages,
            diffusionSize, 0.0f, 0.5f, decorrelatedTunings);
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
    const float normalized = juce::jlimit(0.0f, 1.0f, feedbackTimeSeconds / 10.0f);
    const float curved = std::sqrt(normalized);
    feedbackGain = std::max(0.0f, std::min(0.85f * curved, 0.95f));
}

/*void Delay::updateFilters() const
{
    const float lpHz = map01ToRange(lowpassCutoff,  500.0f, 9000.0f);
    const float hpHz = map01ToRange(highpassCutoff,  10.0f, 2000.0f);

    auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate,  lpHz);
    auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, hpHz);

    *lowpass.coefficients = *lpCoeffs;
    *highpass.coefficients = *hpCoeffs;
}*/

// endregion