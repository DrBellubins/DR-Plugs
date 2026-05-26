#include "Reverb.h"

// TODO: Doesn't scale with delay time (similar to Deelay)
// TODO: Reverb IR lasts around 2000 ms.
// TODO: Scale delayTimeScaled to IR length (maxDelayMS - IRLength)
// TODO: Run UpdateSize(delayTimeScaled * diffusionSize)

// TODO: Wet signal has white noise (DC artifacts??)

void Reverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    delayTimeSegment.PepareToPlay(newSampleRate);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();

    // Diffusion
    diffusion = std::make_unique<DiffusionChain>();
    diffusion->Prepare(sampleRate);

    if (diffusion) diffusion->ClearState();

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

void Reverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();
}

float Reverb::ProcessSample(float inputSample)
{
    const float timeScale = juce::jlimit(0.1f,
        3.0f, delayTimeSegment.DelayTimeMilliseconds / irLengthMs);

    diffusion->UpdateSize(diffusionSize * timeScale);

    // 1) Input + feedback
    const float inputFeedback = inputSample + lastFeedback;

    // 2) Diffusion
    const float diffused = diffusion->ProcessSample(inputFeedback);

    // 3) Damping
    const float damped = damping->ProcessSample(diffused, 7000.0f);

    // 4) Recirculation
    lastFeedback = damped * feedbackGain;

    return damped;
}

// --- Parameters ---
void Reverb::SetHostTempo(float bpm)
{
    hostBPM = bpm;

    delayTimeSegment.SetHostTempo(bpm);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();
}

void Reverb::SetDelayTime(float newDelayTime)
{
    delayTimeSegment.SetDelayTime(newDelayTime);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();
}

void Reverb::SetDelayMode(int newDelayMode)
{
    delayTimeSegment.SetDelayMode(newDelayMode);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();
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
    diffusionSize = newDiffusionSize;
}

void Reverb::SetDiffusionQuality(int newDiffusionQuality)
{
    diffusionQualityStages = newDiffusionQuality;
    diffusionRebuildPending.store(true, std::memory_order_release);
}

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

    // Delay
    if (diffusion != nullptr)
    {
        diffusion->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }
}

void Reverb::updateFeedbackGainFromFeedbackTime()
{
    const float normalized = juce::jlimit(0.0f, 1.0f, feedbackTimeSeconds / 10.0f);
    const float curved = std::sqrt(normalized);
    feedbackGain = std::max(0.0f, std::min(0.85f * curved, 0.95f));
}

//endregion