#include "Delay.h"

void Delay::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    // Delay line
    const int maxDelaySamples = static_cast<int>(std::ceil(1.0 * sampleRate));
    delayLine = std::make_unique<DelayLine>(maxDelaySamples);

    delayLine->Clear();

    delayLine->SetSampleRate(sampleRate);

    // Diffusion Read
    delayDiffusionRead = std::make_unique<DiffusionChain>();
    delayDiffusionRead->Prepare(sampleRate);

    if (delayDiffusionRead) delayDiffusionRead->ClearState();

    // Diffusion Write
    delayDiffusionWrite = std::make_unique<DiffusionChain>();
    delayDiffusionWrite->Prepare(sampleRate);

    if (delayDiffusionWrite) delayDiffusionWrite->ClearState();

    // Force full rebuild
    lastBuiltQualityStages = -1;
    lastBuiltSize01 = -1.0f;

    updateDelayMillisecondsFromNormalized();
    rebuildDiffusionIfNeeded();
    updateFeedbackGainFromFeedbackTime();

    // Various
    damping = std::make_unique<DampingFilter>();
    damping->Prepare(sampleRate);

    smoothedCenteredReadDelayMilliseconds = delayMilliseconds;
    readDelaySlewCoefficient = 1.0f / (0.02f * static_cast<float>(sampleRate));
}

void Delay::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();

    //if (filterRebuildPending.exchange(false, std::memory_order_acq_rel))
    //    updateFilters();
}

float Delay::ProcessSample(float inputSample)
{
    delayDiffusionRead->UpdateSize(diffusionSize);
    delayDiffusionWrite->UpdateSize(diffusionSize);

    // 1) Input + feedback
    float inputFeedback = inputSample + lastFeedback;

    // 2) Pre write diffusion
    const float diffused = delayDiffusionWrite->ProcessSample(inputFeedback);

    // 3) Blend between clean tap -> diffused tap
    const float inputFeedbackGain = std::cos(diffusionAmount * juce::MathConstants<float>::halfPi);
    const float diffusionGain = std::sin(diffusionAmount * juce::MathConstants<float>::halfPi);

    const float feedbackWrite = (inputFeedback * inputFeedbackGain) + (diffused  * diffusionGain);

    // 4) Write to delay line
    delayLine->PushSample(feedbackWrite);

    // 5) Read nominal and early tap
    smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
            (delayMilliseconds - smoothedCenteredReadDelayMilliseconds);

    const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;

    const float earlyReadMilliseconds =
        std::max(1.0f, delayMilliseconds - staticDiffusionCompensationMilliseconds);

    const float nominalTap = delayLine->ReadFeedbackBuffer(nominalReadMilliseconds);
    const float earlyTap = delayLine->ReadFeedbackBuffer(earlyReadMilliseconds);

    // 6) Diffuse the early tap (second pass)
    const float diffusedEarly = delayDiffusionRead->ProcessSample(earlyTap);

    // 7) Blend between nominal tap -> early tap
    const float diffusionDrive = juce::jlimit(0.0f, 1.0f, diffusionAmount * 2.0f);
    const float nominalTapGain = std::pow(1.0f - diffusionDrive, 4.0f);   // collapses to 0 at drive >= 1
    const float earlyTapGain = std::sin(diffusionDrive * juce::MathConstants<float>::halfPi);

    const float blendedTap = nominalTap * nominalTapGain + diffusedEarly * earlyTapGain;

    // 8) Damping
    const float dampedDiffused = damping->ProcessSample(blendedTap, lowpassCutoff);

    // 9) Recirculation
    lastFeedback = dampedDiffused * feedbackGain;

    return blendedTap;
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

void Delay::SetDiffusionAmount(float newDiffusionAmount)
{
    diffusionAmount = newDiffusionAmount;
}

void Delay::SetDiffusionSize(float newDiffusionSize)
{
    diffusionSize = newDiffusionSize;
}

void Delay::SetDiffusionQuality(int newDiffusionQuality)
{
    diffusionQualityStages = newDiffusionQuality;
    diffusionRebuildPending.store(true, std::memory_order_release);
}

/*void Delay::SetLowpassCutoff(float newLowpassCutoff)
{
    lowpassCutoff = newLowpassCutoff;
    filterRebuildPending.store(true, std::memory_order_release);
}

void Delay::SetHighpassCutoff(float newHighpassCutoff)
{
    highpassCutoff = newHighpassCutoff;
    filterRebuildPending.store(true, std::memory_order_release);
}*/

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
    echoWriteCounter = juce::jlimit(0, writePeriodSamples - 1, echoWriteCounter);
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
    if (delayDiffusionRead != nullptr)
    {
        delayDiffusionRead->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    if (delayDiffusionWrite != nullptr)
    {
        delayDiffusionWrite->Configure(diffusionQualityStages,
            diffusionSize, 0.005f, 0.5f, Tunings);
    }

    totalDelayDiffusionMilliseconds = 0.0f;

    if (delayDiffusionRead != nullptr)
    {
        for (float stageDelayMilliseconds : delayDiffusionRead->perStageDelayMs)
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