#include "Delay.h"

void Delay::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    delayTimeSegment.PepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();

    // Delay line
    InternalDelayLine = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);

    InternalDelayLine->Clear();
    InternalDelayLine->SetSampleRate(sampleRate);

    // Diffusion Read
    diffusionRead = std::make_unique<DiffusionChain>();
    diffusionRead->Prepare(sampleRate);

    if (diffusionRead) diffusionRead->ClearState();

    // Diffusion Write
    diffusionWrite = std::make_unique<DiffusionChain>();
    diffusionWrite->Prepare(sampleRate);

    if (diffusionWrite) diffusionWrite->ClearState();

    // Damping
    damping = std::make_unique<DampingFilter>();
    damping->Prepare(sampleRate);

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

float Delay::ProcessSample(float inputSample)
{
    diffusionRead->UpdateSize(diffusionSize);
    diffusionWrite->UpdateSize(diffusionSize);

    // 1) Input + feedback
    const float inputFeedback = inputSample + lastFeedback;

    // 2) Pre write diffusion
    const float diffused = diffusionWrite->ProcessSample(inputFeedback);

    // 3) Blend between clean tap -> diffused tap (diff amt 0.0 -> 0.5)
    const float inputFeedbackGain = std::cos(diffusionAmount * juce::MathConstants<float>::halfPi);
    const float diffusionGain = std::sin(diffusionAmount * juce::MathConstants<float>::halfPi);

    const float feedbackWrite = (inputFeedback * inputFeedbackGain) + (diffused  * diffusionGain);

    // 4) Write to delay line
    InternalDelayLine->PushSample(feedbackWrite);

    // 5) Read nominal and early tap
    smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
            (delayTimeSegment.DelayTimeMilliseconds - smoothedCenteredReadDelayMilliseconds);

    const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;

    const float earlyReadMilliseconds =
        std::max(1.0f, delayTimeSegment.DelayTimeMilliseconds - staticDiffusionCompensationMilliseconds);

    const float nominalTap = InternalDelayLine->ReadFeedbackBuffer(nominalReadMilliseconds);
    const float earlyTap = InternalDelayLine->ReadFeedbackBuffer(earlyReadMilliseconds);

    // 6) Diffuse the early tap (second pass)
    const float diffusedEarly = diffusionRead->ProcessSample(earlyTap);

    // 7) Blend between nominal tap -> early tap (diff amt 0.0 -> 0.5)
    const float diffusionDrive = juce::jlimit(0.0f, 1.0f, diffusionAmount * 1.25f);
    const float nominalTapGain = std::pow(1.0f - diffusionDrive, 4.0f);   // collapses to 0 at drive >= 1
    const float earlyTapGain = std::sin(diffusionDrive * juce::MathConstants<float>::halfPi);

    const float blendedTap = nominalTap * nominalTapGain + diffusedEarly * earlyTapGain;

    // 8) Damping
    const float damped = damping->ProcessSample(blendedTap, 7000.0f);

    // 9) Recirculation
    lastFeedback = damped * feedbackGain;

    return blendedTap;
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

    // Delay
    if (diffusionRead != nullptr)
    {
        diffusionRead->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    if (diffusionWrite != nullptr)
    {
        diffusionWrite->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    totalDelayDiffusionMilliseconds = 0.0f;

    if (diffusionRead != nullptr)
    {
        for (float stageDelayMilliseconds : diffusionRead->perStageDelayMs)
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