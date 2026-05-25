#include "Reverb.h"

// TODO: Doesn't scale with delay time (similar to Deelay)
void Reverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    // Diffusion
    diffusion = std::make_unique<DiffusionChain>();
    diffusion->Prepare(sampleRate);

    if (diffusion) diffusion->ClearState();

    // Damping
    damping = std::make_unique<DampingFilter>();
    damping->Prepare(sampleRate);

    // Various
    lastBuiltQualityStages = -1;
    lastBuiltSize01 = -1.0f;

    updateDelayMillisecondsFromNormalized();
    rebuildDiffusionIfNeeded();
    updateFeedbackGainFromFeedbackTime();

    smoothedCenteredReadDelayMilliseconds = delayMilliseconds;
    readDelaySlewCoefficient = 1.0f / (0.02f * static_cast<float>(sampleRate));
}

void Reverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();
}

float Reverb::ProcessSample(float inputSample)
{
    diffusion->UpdateSize(diffusionSize);

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
    updateDelayMillisecondsFromNormalized();
}

void Reverb::SetDelayTime(float newDelayTime)
{
    delayTimeNormalized = newDelayTime;
    updateDelayMillisecondsFromNormalized();
}

void Reverb::SetDelayMode(int newDelayMode)
{
    delayMode = newDelayMode;
    updateDelayMillisecondsFromNormalized();
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

void Reverb::updateDelayMillisecondsFromNormalized()
{
    if (delayMode == 0) // ms — free range 1..1000 ms
    {
        const float baseMs = map01ToRange(delayTimeNormalized, 1.0f, 1000.0f);
        delayMilliseconds = std::max(1.0f, baseMs);

        // ms mode: fixed glide
        readDelaySlewCoefficient =
            1.0f / (0.08f * static_cast<float>(sampleRate));
    }
    else
    {
        // Beat-synced modes. Knob snaps to:
        // 0, 0.25, 0.5, 0.75, 1.0 => 1/1, 1/2, 1/4, 1/8, 1/16
        static constexpr float snapPositions[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        static constexpr float beatMultipliers[5] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };

        int stepIndex = 0;
        float smallestDistance = std::abs(delayTimeNormalized - snapPositions[0]);

        for (int i = 1; i < 5; ++i)
        {
            const float distance = std::abs(delayTimeNormalized - snapPositions[i]);

            if (distance < smallestDistance)
            {
                smallestDistance = distance;
                stepIndex = i;
            }
        }

        const float quarterNoteMs = 60000.0f / hostBPM;
        float beatMs = beatMultipliers[stepIndex] * quarterNoteMs;

        if (delayMode == 2)      // triplet
            beatMs *= (2.0f / 3.0f);
        else if (delayMode == 3) // dotted
            beatMs *= 1.5f;

        delayMilliseconds = juce::jlimit(1.0f, 1000.0f, beatMs);

        const float slewSeconds = std::max(0.05f, delayMilliseconds / 1000.0f);
        readDelaySlewCoefficient =
            1.0f / (slewSeconds * static_cast<float>(sampleRate));
    }

    // IMPORTANT: always update write period for all modes
    writePeriodSamples = std::max(
        1, static_cast<int>(std::round((delayMilliseconds * static_cast<float>(sampleRate)) / 1000.0f)));

    // Keep counters in range after timing changes
    echoWriteCounter = juce::jlimit(0, writePeriodSamples - 1, echoWriteCounter);
}

void Reverb::rebuildDiffusionIfNeeded()
{
    if (diffusionQualityStages == lastBuiltQualityStages
        && diffusionSize == lastBuiltSize01)
    {
        return;
    }

    lastBuiltQualityStages = diffusionQualityStages;
    lastBuiltSize01 = diffusionSize;

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