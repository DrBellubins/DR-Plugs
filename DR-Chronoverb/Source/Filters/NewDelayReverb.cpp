#include "NewDelayReverb.h"

#include <cmath>
#include <algorithm>
#include <array>

NewDelayReverb::NewDelayReverb()
{
}

NewDelayReverb::~NewDelayReverb()
{
}

void NewDelayReverb::PrepareToPlay(double newSampleRate, float initialHostTempoBpm)
{
    sampleRate = newSampleRate;
    hostTempoBpm = initialHostTempoBpm;

    // Prepare IIR filters with spec before first use
    juce::dsp::ProcessSpec filterSpec;
    filterSpec.sampleRate = sampleRate;
    filterSpec.maximumBlockSize = 4096; // safe upper bound
    filterSpec.numChannels = 1;

    // Initialize HP/LP filters
    lowpassL.prepare(filterSpec);
    lowpassR.prepare(filterSpec);
    highpassL.prepare(filterSpec);
    highpassR.prepare(filterSpec);

    // Force a coefficient update on first block
    filterRebuildPending.store(true, std::memory_order_release);
    updateFilters();
    filterRebuildPending.store(false, std::memory_order_release);

    // Create main delay lines with 1000 ms max buffer
    const int maxDelaySamples = static_cast<int>(std::ceil(1.0 * sampleRate)); // 1000 ms
    mainDelayLeft.reset(new DelayLine(maxDelaySamples));
    mainDelayRight.reset(new DelayLine(maxDelaySamples));

    // Create delay diffusion chains
    delayDiffusionLeft.reset(new DiffusionChain());
    delayDiffusionRight.reset(new DiffusionChain());

    delayDiffusionLeft->Prepare(sampleRate);
    delayDiffusionRight->Prepare(sampleRate);

    // Force rebuild on every PrepareToPlay — new chain objects are always unconfigured.
    // The guard in rebuildDiffusionIfNeeded() is only useful mid-session (audio thread).
    lastBuiltQualityStages = -1;
    lastBuiltSize01 = -1.0f;

    // Initial diffusion config (safe here; not concurrently processing)
    rebuildDiffusionIfNeeded();

    // Always wipe audio state on prepare, even when config is unchanged.
    // This prevents stale allpass buffer contents from leaking into offline renders.
    if (delayDiffusionLeft)  delayDiffusionLeft->ClearState();
    if (delayDiffusionRight) delayDiffusionRight->ClearState();

    smoothedDelayReverbDiffBlend = 0.0f;
    diffusionRebuildPending.store(false, std::memory_order_release);

    // Damping filters (feedback path)
    dampingLeft.reset(new DampingFilter());
    dampingRight.reset(new DampingFilter());
    dampingLeft->Prepare(sampleRate);
    dampingRight->Prepare(sampleRate);

    updateDelayMillisecondsFromNormalized();
    updateFeedbackGainFromFeedbackTime();

    // Spread setup
    updateStereoSpread();

    wetInputPitchShifterLeft.Prepare(sampleRate, 512);
    wetInputPitchShifterRight.Prepare(sampleRate, 512);

    wetInputPitchShifterLeft.SetEnabled(true);
    wetInputPitchShifterRight.SetEnabled(true);

    echoSampleCounterL = 0;
    echoSampleCounterR = 0;

    lastFeedbackL = 0.0f;
    lastFeedbackR = 0.0f;

    kBlendSlewCoeff = 1.0f / (0.01f * static_cast<float>(sampleRate));

    smoothedCenteredReadDelayMilliseconds = delayMilliseconds;
    readDelaySlewCoefficient = 1.0f / (0.02f * static_cast<float>(sampleRate)); // 20 ms smoothing

    // Clear delay lines
    mainDelayLeft->Clear();
    mainDelayRight->Clear();
}

void NewDelayReverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples = audioBuffer.getNumSamples();

    if (numChannels < 1 || numSamples <= 0)
    {
        return;
    }

    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
    {
        rebuildDiffusionIfNeeded();
    }

    if (filterRebuildPending.exchange(false, std::memory_order_acq_rel))
    {
        updateFilters();
    }

    float* leftData = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float dryLeft = leftData[sampleIndex];
        const float dryRight = (rightData != nullptr ? rightData[sampleIndex] : dryLeft);

        // Smooth diffusion amount for automation safety
        smoothedDelayReverbDiffBlend += kBlendSlewCoeff * (diffusionAmount01 - smoothedDelayReverbDiffBlend);
        const float diffusionAmountSmoothed = juce::jlimit(0.0f, 1.0f, smoothedDelayReverbDiffBlend);

        // 1: Optional pre filtering on dry
        float filteredDryLeft = dryLeft;
        float filteredDryRight = dryRight;

        if (hplpPrePost01 < 0.5f)
        {
            filteredDryLeft = highpassL.processSample(filteredDryLeft);
            filteredDryLeft = lowpassL.processSample(filteredDryLeft);

            filteredDryRight = highpassR.processSample(filteredDryRight);
            filteredDryRight = lowpassR.processSample(filteredDryRight);
        }

        // 2: Sum input + feedback
        float preLeft = filteredDryLeft + lastFeedbackL;
        float preRight = filteredDryRight + lastFeedbackR;

        // 3: Progressive pitch shift (shimmer)
        float pitchedFeedbackLeft = preLeft;
        float pitchedFeedbackRight = preRight;

        if (pitchShift01 >= 0.5f)
        {
            pitchedFeedbackLeft = wetInputPitchShifterLeft.ProcessSample(preLeft);
            pitchedFeedbackRight = wetInputPitchShifterRight.ProcessSample(preRight);

            // Advance echo boundary counters
            const int delaySamplesInt = static_cast<int>(std::round((delayMilliseconds * sampleRate) / 1000.0));

            ++echoSampleCounterL;

            if (echoSampleCounterL >= delaySamplesInt)
            {
                echoSampleCounterL = 0;
                wetInputPitchShifterLeft.OnNewEchoBoundary();
            }

            ++echoSampleCounterR;

            if (echoSampleCounterR >= delaySamplesInt)
            {
                echoSampleCounterR = 0;
                wetInputPitchShifterRight.OnNewEchoBoundary();
            }
        }

        // 4: Diffuse before write (amount-controlled)
        float writeLeft = pitchedFeedbackLeft;
        float writeRight = pitchedFeedbackRight;

        if (diffusionAmountSmoothed > 0.0001f)
        {
            const float diffusedWriteLeft = delayDiffusionLeft->ProcessSample(preLeft);
            const float diffusedWriteRight = delayDiffusionRight->ProcessSample(preRight);

            const float cleanWriteGain = std::cos(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);
            const float diffusedWriteGain = std::sin(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);

            writeLeft = pitchedFeedbackLeft * cleanWriteGain + diffusedWriteLeft * diffusedWriteGain;
            writeRight = pitchedFeedbackRight * cleanWriteGain + diffusedWriteRight * diffusedWriteGain;
        }

        // 5: IMPORTANT: write to delay line before reading
        mainDelayLeft->PushSample(writeLeft);
        mainDelayRight->PushSample(writeRight);

        // 6: Fixed read positions (do not depend on diffusion amount)
        const float nominalReadMilliseconds = delayMilliseconds;
        const float earlyReadMilliseconds = std::max(1.0f, delayMilliseconds - staticDiffusionCompensationMilliseconds);

        // Read nominal and early taps
        const float nominalWetLeft = mainDelayLeft->ReadDelayMilliseconds(nominalReadMilliseconds, sampleRate);
        const float nominalWetRight = mainDelayRight->ReadDelayMilliseconds(nominalReadMilliseconds, sampleRate);

        const float earlyWetLeft = mainDelayLeft->ReadDelayMilliseconds(earlyReadMilliseconds, sampleRate);
        const float earlyWetRight = mainDelayRight->ReadDelayMilliseconds(earlyReadMilliseconds, sampleRate);

        // Diffuse early branch only
        const float diffusedEarlyLeft = delayDiffusionLeft->ProcessSample(earlyWetLeft);
        const float diffusedEarlyRight = delayDiffusionRight->ProcessSample(earlyWetRight);

        // Remap so midpoint already heavily suppresses clean tap.
        const float diffusionDrive = juce::jlimit(0.0f, 1.0f, diffusionAmountSmoothed * 2.0f);

        // Very aggressive clean suppression (clean ~= 0 at amount >= 0.5)
        const float cleanGain = std::pow(1.0f - diffusionDrive, 4.0f);

        // Keep diffuse energy stable
        const float diffusedGain = std::sin(diffusionDrive * juce::MathConstants<float>::halfPi);

        float wetLeft = nominalWetLeft * cleanGain + diffusedEarlyLeft * diffusedGain;
        float wetRight = nominalWetRight * cleanGain + diffusedEarlyRight * diffusedGain;

        // 7: Damping + feedback from composite wet (keeps loop behavior consistent with diffusion)
        const float dampedLeft = dampingLeft->ProcessSample(wetLeft, lowpass01);
        const float dampedRight = dampingRight->ProcessSample(wetRight, lowpass01);

        lastFeedbackL = dampedLeft * feedbackGain;
        lastFeedbackR = dampedRight * feedbackGain;

        // 8: Stereo spread
        float spreadWetLeft = wetLeft;
        float spreadWetRight = wetRight;

        const float spread = juce::jlimit(-1.0f, 1.0f, stereoSpreadMinus1To1);

        if (std::abs(spread) > 0.0001f)
        {
            const float widen = std::max(0.0f, spread);
            const float narrow = std::max(0.0f, -spread);

            if (widen > 0.0f)
            {
                const float cross = widen * 0.25f;
                const float newLeft = spreadWetLeft - cross * spreadWetRight;
                const float newRight = spreadWetRight - cross * spreadWetLeft;
                spreadWetLeft = newLeft;
                spreadWetRight = newRight;
            }
            else if (narrow > 0.0f)
            {
                const float mono = 0.5f * (spreadWetLeft + spreadWetRight);
                spreadWetLeft = spreadWetLeft * (1.0f - narrow) + mono * narrow;
                spreadWetRight = spreadWetRight * (1.0f - narrow) + mono * narrow;
            }
        }

        // 9: Dry/Wet
        float outputLeft = PMath::EqualPowerCrossfade(dryLeft, spreadWetLeft, dryWet01);
        float outputRight = PMath::EqualPowerCrossfade(dryRight, spreadWetRight, dryWet01);

        // 10: Optional post filtering
        if (hplpPrePost01 >= 0.5f)
        {
            outputLeft = highpassL.processSample(outputLeft);
            outputLeft = lowpassL.processSample(outputLeft);

            outputRight = highpassR.processSample(outputRight);
            outputRight = lowpassR.processSample(outputRight);
        }

        leftData[sampleIndex] = outputLeft;

        if (rightData != nullptr)
        {
            rightData[sampleIndex] = outputRight;
        }
    }
}

void NewDelayReverb::SetDelayTime(float newDelayTimeNormalized)
{
    delayTimeNormalized = clamp01(newDelayTimeNormalized);
    updateDelayMillisecondsFromNormalized();
}

void NewDelayReverb::SetFeedbackTime(float newFeedbackTimeSeconds)
{
    feedbackTimeSeconds = std::max(0.0f, newFeedbackTimeSeconds);
    updateFeedbackGainFromFeedbackTime();
}

void NewDelayReverb::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount01 = clamp01(newAmount01);
}

void NewDelayReverb::SetDiffusionSize(float newSize01)
{
    diffusionSize01 = clamp01(newSize01);
    diffusionRebuildPending.store(true, std::memory_order_release);
}

void NewDelayReverb::SetDiffusionQuality(int newQualityStages)
{
    diffusionQualityStages = clampInt(newQualityStages, 1, 8);
    diffusionRebuildPending.store(true, std::memory_order_release);
}

void NewDelayReverb::SetDryWetMix(float newDryWet01)
{
    dryWet01 = clamp01(newDryWet01);
}

void NewDelayReverb::SetLowpassCutoff(float newLowpass01)
{
    lowpass01 = clamp01(newLowpass01);
    filterRebuildPending.store(true, std::memory_order_release);
}

void NewDelayReverb::SetHighpassCutoff(float newHighpass01)
{
    highpass01 = clamp01(newHighpass01);
    filterRebuildPending.store(true, std::memory_order_release);
}

void NewDelayReverb::SetStereoSpread(float newSpreadMinus1To1)
{
    stereoSpreadMinus1To1 = juce::jlimit(-1.0f, 1.0f, newSpreadMinus1To1);
    updateStereoSpread();
}

void NewDelayReverb::SetHPLPPrePost(float prePost01)
{
    hplpPrePost01 = clamp01(prePost01);
}

void NewDelayReverb::SetPitchShift(float pitchShiftEnabled01)
{
    pitchShift01 = clamp01(pitchShiftEnabled01);
}

void NewDelayReverb::SetHostTempo(float bpm)
{
    if (bpm > 0.0f)
        hostTempoBpm = bpm;
}

// ---------------- Internal helpers ----------------
void NewDelayReverb::updateDelayMillisecondsFromNormalized()
{
    float baseMs = map01ToRange(delayTimeNormalized, 0.0f, 1000.0f);
    delayMilliseconds = std::max(1.0f, baseMs);
}

void NewDelayReverb::rebuildDiffusionIfNeeded()
{
    if (diffusionQualityStages == lastBuiltQualityStages
        && diffusionSize01 == lastBuiltSize01)
    {
        return;
    }

    lastBuiltQualityStages = diffusionQualityStages;
    lastBuiltSize01 = diffusionSize01;

    if (delayDiffusionLeft != nullptr)
    {
        delayDiffusionLeft->Configure(diffusionQualityStages, diffusionSize01);
    }

    if (delayDiffusionRight != nullptr)
    {
        delayDiffusionRight->Configure(diffusionQualityStages, diffusionSize01);
    }

    totalDelayDiffusionMilliseconds = 0.0f;

    if (delayDiffusionLeft != nullptr)
    {
        for (float stageDelayMilliseconds : delayDiffusionLeft->perStageDelayMs)
        {
            totalDelayDiffusionMilliseconds += stageDelayMilliseconds;
        }
    }

    const float baseCompensation = totalDelayDiffusionMilliseconds * centeredSwellRatio;
    staticDiffusionCompensationMilliseconds = baseCompensation * diffusionCompensationBias;
}

void NewDelayReverb::updateFeedbackGainFromFeedbackTime()
{
    // Basic mapping: feedbackTimeSeconds in [0..10] -> feedbackGain ~ [0..0.85]
    // Use a smooth curve to avoid jumps.
    const float normalized = juce::jlimit(0.0f, 1.0f, feedbackTimeSeconds / 10.0f);
    const float curved = std::sqrt(normalized); // emphasize mid/high
    feedbackGain = 0.85f * curved;

    // Keep a minimum to avoid tail immediately dying when not desired.
    feedbackGain = std::max(0.0f, std::min(feedbackGain, 0.95f));
}

void NewDelayReverb::updateFilters()
{
    // Map lowpass01 to cutoff in [500 .. 9000] Hz
    const float lpHz = map01ToRange(lowpass01, 500.0f, 9000.0f);
    const float hpHz = map01ToRange(highpass01, 10.0f, 2000.0f);

    auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, lpHz);
    auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, hpHz);

    *lowpassL.coefficients = *lpCoeffs;
    *lowpassR.coefficients = *lpCoeffs;
    *highpassL.coefficients = *hpCoeffs;
    *highpassR.coefficients = *hpCoeffs;
}

void NewDelayReverb::updateStereoSpread()
{
    // No static setup needed; spread is applied inline during process
}

float NewDelayReverb::map01ToRange(float value01, float minValue, float maxValue)
{
    const float v = clamp01(value01);
    return minValue + (maxValue - minValue) * v;
}

float NewDelayReverb::clamp01(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

int NewDelayReverb::clampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(maxValue, value));
}
