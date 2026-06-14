#include "Reverb.h"

void Reverb::PrepareToPlay(double newSampleRate, Filters& filters)
{
    sampleRate = newSampleRate;
    filtersInput = &filters;

    // Delay time
    delayTimeSegment.PrepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMilliseconds();

    // Diffusion
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

void Reverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();
}

std::pair<float, float> Reverb::ProcessSample(float inputSampleL, float inputSampleR)
{
    const float timeScale = juce::jlimit(0.1f,
        3.0f, delayTimeSegment.DelayTimeMilliseconds / irLengthMs);

    diffusionLeft->UpdateSize(diffusionSize * timeScale);
    diffusionRight->UpdateSize(diffusionSize * timeScale);

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

    // 3) Diffusion
    const float diffusedLeft = diffusionLeft->ProcessSample(inputFeedbackLeft);
    const float diffusedRight = diffusionRight->ProcessSample(inputFeedbackRight);

    // 4) Damping
    const float dampedLeft = dampingLeft->ProcessSample(diffusedLeft, 7000.0f);
    const float dampedRight = dampingRight->ProcessSample(diffusedRight, 7000.0f);

    // 5) Recirculation

    // Compensate feedback so RT60 stays constant as chain length shrinks.
    //const float scaledFeedback = std::pow(feedbackGain, 1.0f / std::max(0.01f, timeScale));
    //const float clampedFeedback = juce::jlimit(0.0f, 0.97f, scaledFeedback);

    lastFeedbackL = dampedLeft * feedbackGain;
    lastFeedbackR = dampedRight * feedbackGain;

    return std::make_pair(dampedLeft, dampedRight);
}

//region Parameters

void Reverb::SetHostTempo(float bpm)
{
    hostBPM = bpm;

    delayTimeSegment.SetHostTempo(bpm);
}

void Reverb::SetDelayTime(float newDelayTime)
{
    delayTimeSegment.SetDelayTime(newDelayTime);
}

void Reverb::SetDelayMode(int newDelayMode)
{
    delayTimeSegment.SetDelayMode(newDelayMode);
}

void Reverb::SetFeedbackTime(float newFeedbackTime)
{
    feedbackTimeSeconds = newFeedbackTime;
    updateFeedbackGainFromFeedbackTime();
}

void Reverb::SetDiffusionAmount(float newDiffusionAmount)
{
    diffusionAmount = newDiffusionAmount;
}

void Reverb::SetDiffusionSize(float newDiffusionSize)
{
    diffusionSize = newDiffusionSize * tuningLengthMultiplier;
}

void Reverb::SetDiffusionQuality(int newDiffusionQuality)
{
    diffusionQualityStages = newDiffusionQuality;
    diffusionRebuildPending.store(true, std::memory_order_release);
}

//endregion

//region Update Functions

void Reverb::rebuildDiffusionIfNeeded()
{
    if (diffusionQualityStages == lastBuiltQualityStages
        && diffusionSize == lastBuiltSize)
    {
        return;
    }

    lastBuiltQualityStages = diffusionQualityStages;
    lastBuiltSize = diffusionSize;

    // Diffusion
    if (diffusionLeft != nullptr)
    {
        diffusionLeft->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    if (diffusionRight != nullptr)
    {
        auto decorrelatedTunings = DecorrelateTunings(Tunings);

        diffusionRight->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, decorrelatedTunings);
    }
}

void Reverb::updateFeedbackGainFromFeedbackTime()
{
    const float normalized = juce::jlimit(0.0f, 1.0f, feedbackTimeSeconds / 10.0f);
    const float curved = std::sqrt(normalized);
    feedbackGain = std::max(0.0f, std::min(0.85f * curved, 0.95f));
}

//endregion