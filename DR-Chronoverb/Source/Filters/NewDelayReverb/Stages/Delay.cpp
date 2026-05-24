#include "Delay.h"

void Delay::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    const int maxDelaySamples = static_cast<int>(std::ceil(1.0 * sampleRate));
    delayLineLeft = std::make_unique<DelayLine>(maxDelaySamples);
    delayLineRight = std::make_unique<DelayLine>(maxDelaySamples);

    delayLineLeft->Clear();
    delayLineRight->Clear();

    delayLineLeft->SetSampleRate(sampleRate);
    delayLineRight->SetSampleRate(sampleRate);

    // Diffusion Read
    delayDiffusionReadLeft = std::make_unique<DiffusionChain>();
    delayDiffusionReadRight = std::make_unique<DiffusionChain>();

    delayDiffusionReadLeft->Prepare(sampleRate);
    delayDiffusionReadRight->Prepare(sampleRate);

    if (delayDiffusionReadLeft) delayDiffusionReadLeft->ClearState();
    if (delayDiffusionReadRight) delayDiffusionReadRight->ClearState();

    // Diffusion Write
    delayDiffusionWriteLeft = std::make_unique<DiffusionChain>();
    delayDiffusionWriteRight = std::make_unique<DiffusionChain>();

    delayDiffusionWriteLeft->Prepare(sampleRate);
    delayDiffusionWriteRight->Prepare(sampleRate);

    if (delayDiffusionWriteLeft) delayDiffusionWriteLeft->ClearState();
    if (delayDiffusionWriteRight) delayDiffusionWriteRight->ClearState();
}

void Delay::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{

}

// --- Parameters ---
void Delay::SetHostTempo(float bpm)
{
    hostBpm = bpm;
    updateDelayMillisecondsFromNormalized();
}

void Delay::SetDelayTime(float newDelayTime)
{
    delayTimeNormalized = newDelayTime;
    updateDelayMillisecondsFromNormalized();
}

void Delay::SetDelayMode(int newDelayMode)
{
    delayMode = newDelayMode;
    updateDelayMillisecondsFromNormalized();
}

void Delay::SetFeedbackTime(float newFeedbackTime)
{
    feedbackTimeSeconds = newFeedbackTime;
    updateFeedbackGainFromFeedbackTime();
}

void Delay::SetDiffusionSize(float newDiffusionSize)
{
    diffusionSize = newDiffusionSize;
}

void Delay::SetDiffusionQuality(int newDiffusionQuality)
{
    diffusionQualityStages = newDiffusionQuality;
}

//region Update Functions

void Delay::updateDelayMillisecondsFromNormalized()
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
        static constexpr float snapPositions[5]   = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
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

        const float quarterNoteMs = 60000.0f / hostBpm;
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
        1,
        static_cast<int>(std::round((delayMilliseconds * static_cast<float>(sampleRate)) / 1000.0f))
    );

    // Keep counters in range after timing changes
    echoWriteCounterL = juce::jlimit(0, writePeriodSamples - 1, echoWriteCounterL);
    echoWriteCounterR = juce::jlimit(0, writePeriodSamples - 1, echoWriteCounterR);
}

void Delay::rebuildDiffusionIfNeeded()
{
    if (diffusionQualityStages == lastBuiltQualityStages
        && diffusionSize == lastBuiltSize01)
    {
        return;
    }

    lastBuiltQualityStages = diffusionQualityStages;
    lastBuiltSize01 = diffusionSize;

    // Delay
    if (delayDiffusionReadLeft != nullptr)
    {
        delayDiffusionReadLeft->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    if (delayDiffusionReadRight != nullptr)
    {
        delayDiffusionReadRight->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    if (delayDiffusionWriteLeft != nullptr)
    {
        delayDiffusionWriteLeft->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    if (delayDiffusionWriteRight != nullptr)
    {
        delayDiffusionWriteRight->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    totalDelayDiffusionMilliseconds = 0.0f;

    if (delayDiffusionReadLeft != nullptr)
    {
        for (float stageDelayMilliseconds : delayDiffusionReadLeft->perStageDelayMs)
            totalDelayDiffusionMilliseconds += stageDelayMilliseconds;
    }

    const float baseCompensation = totalDelayDiffusionMilliseconds * centeredSwellRatio;
    staticDiffusionCompensationMilliseconds = baseCompensation * diffusionCompensationBias;
}

// endregion