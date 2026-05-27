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
    diffusionLeft = std::make_unique<DiffusionChain>();
    diffusionRight = std::make_unique<DiffusionChain>();

    diffusionLeft->Prepare(sampleRate);
    diffusionRight->Prepare(sampleRate);

    if (diffusionLeft) diffusionLeft->ClearState();
    if (diffusionRight) diffusionRight->ClearState();

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
    diffusionLeft->UpdateSize(diffusionSize);

    diffusionRight->UpdateSize(diffusionSize);

    // 1) Input + feedback
    const float inputFeedbackLeft = inputSampleL + lastFeedbackL;
    const float inputFeedbackRight = inputSampleR + lastFeedbackR;

    // 2) Pre write diffusion
    const float diffusedLeft = diffusionLeft->ProcessSample(inputFeedbackLeft);
    const float diffusedRight = diffusionRight->ProcessSample(inputFeedbackRight);

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

    //const float earlyReadMilliseconds =
    //    std::max(1.0f, delayTimeSegment.DelayTimeMilliseconds - staticDiffusionCompensationMilliseconds);

    const float nominalTapLeft = InternalDelayLineLeft->ReadFeedbackBuffer(nominalReadMilliseconds);
    const float nominalTapRight = InternalDelayLineRight->ReadFeedbackBuffer(nominalReadMilliseconds);

    /*const float earlyTapLeft = InternalDelayLineLeft->ReadFeedbackBuffer(earlyReadMilliseconds);
    const float earlyTapRight = InternalDelayLineRight->ReadFeedbackBuffer(earlyReadMilliseconds);

    // 6) Diffuse the early tap (second pass)
    const float diffusedEarlyLeft = diffusionLeft->ProcessSample(earlyTapLeft);
    const float diffusedEarlyRight = diffusionRight->ProcessSample(earlyTapRight);

    // 7) Blend between nominal tap -> early tap (diff amt 0.0 -> 0.5)
    auto [nominalTapGain, earlyTapGain] = GetDelayDiffusedTapGain(diffusionAmount, 1.25f);

    const float blendedTapLeft = nominalTapLeft * nominalTapGain + diffusedEarlyLeft * earlyTapGain;
    const float blendedTapRight = nominalTapRight * nominalTapGain + diffusedEarlyRight * earlyTapGain;*/

    // 8) Damping
    const float dampedLeft = dampingLeft->ProcessSample(nominalTapLeft, 7000.0f);
    const float dampedRight = dampingRight->ProcessSample(nominalTapRight, 7000.0f);

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

    if (diffusionLeft != nullptr)
    {
        diffusionLeft->Configure(diffusionQualityStages,
            diffusionSize, 0.0f, 0.5f, Tunings);
    }

    if (diffusionRight != nullptr)
    {
        auto decorrelatedTunings = DecorrelateTunings(Tunings);

        diffusionRight->Configure(diffusionQualityStages,
            diffusionSize, 0.0f, 0.5f, decorrelatedTunings);
    }

    totalDelayDiffusionMilliseconds = 0.0f;

    if (diffusionLeft != nullptr)
    {
        for (float stageDelayMilliseconds : diffusionLeft->perStageDelayMs)
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