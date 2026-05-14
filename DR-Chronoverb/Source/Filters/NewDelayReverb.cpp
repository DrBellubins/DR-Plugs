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

std::vector<float> delayTunings =
{
    //7.0f, 13.0f, 19.0f, 29.0f, 53.0f, 79.0f, 113.0f, 149.0f   // Generated primes
    //5.0f, 11.0f, 17.0f, 19.0f, 23.0f, 29.0f, 31.0f, 37.0f     // Bad Deelay approx.
    //5.0f, 11.0f, 17.0f, 23.0f, 47.0f, 67.0f, 71.0f, 73.0f     // Also bad.
    10.0f, 15.0f, 22.5f, 33.75f, 50.6f, 75.9f, 113.9f, 170.8f   // Natural
};

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
        return;

    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();

    if (filterRebuildPending.exchange(false, std::memory_order_acq_rel))
        updateFilters();

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float dryLeft  = leftData[sampleIndex];
        const float dryRight = (rightData != nullptr ? rightData[sampleIndex] : dryLeft);

        // Smooth diffusion amount for automation safety
        smoothedDelayReverbDiffBlend += kBlendSlewCoeff * (diffusionAmount01 - smoothedDelayReverbDiffBlend);
        const float diffusionAmountSmoothed = juce::jlimit(0.0f, 1.0f, smoothedDelayReverbDiffBlend);

        // 1: Optional pre filtering on dry
        float filteredDryLeft  = dryLeft;
        float filteredDryRight = dryRight;

        if (hplpPrePost01 < 0.5f)
        {
            filteredDryLeft  = highpassL.processSample(filteredDryLeft);
            filteredDryLeft  = lowpassL.processSample(filteredDryLeft);

            filteredDryRight = highpassR.processSample(filteredDryRight);
            filteredDryRight = lowpassR.processSample(filteredDryRight);
        }

        // 2: Sum input + feedback (no pitch shift here)
        float preLeft  = filteredDryLeft  + lastFeedbackL;
        float preRight = filteredDryRight + lastFeedbackR;

        // 3: Diffuse before write (amount-controlled)
        float writeLeft  = preLeft;
        float writeRight = preRight;

        if (diffusionAmountSmoothed > 0.0001f)
        {
            const float diffusedWriteLeft  = delayDiffusionLeft->ProcessSample(preLeft);
            const float diffusedWriteRight = delayDiffusionRight->ProcessSample(preRight);

            const float cleanWriteGain    = std::cos(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);
            const float diffusedWriteGain = std::sin(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);

            writeLeft  = preLeft  * cleanWriteGain + diffusedWriteLeft  * diffusedWriteGain;
            writeRight = preRight * cleanWriteGain + diffusedWriteRight * diffusedWriteGain;
        }

        // 4: Fixed read positions
        const float nominalReadMilliseconds = delayMilliseconds;
        const float earlyReadMilliseconds   = std::max(1.0f, delayMilliseconds - staticDiffusionCompensationMilliseconds);

        // 5: Write to delay line
        mainDelayLeft->PushSample(writeLeft);
        mainDelayRight->PushSample(writeRight);

        // 6: Read taps
        const float nominalWetLeft  = mainDelayLeft->ReadDelayMilliseconds(nominalReadMilliseconds, sampleRate);
        const float nominalWetRight = mainDelayRight->ReadDelayMilliseconds(nominalReadMilliseconds, sampleRate);

        const float earlyWetLeft  = mainDelayLeft->ReadDelayMilliseconds(earlyReadMilliseconds, sampleRate);
        const float earlyWetRight = mainDelayRight->ReadDelayMilliseconds(earlyReadMilliseconds, sampleRate);

        // 7: Pitch shift ONLY the clean nominal tap — before any diffusion blend
        float pitchedNominalLeft  = nominalWetLeft;
        float pitchedNominalRight = nominalWetRight;

        if (pitchShiftEnabled >= 0.5f)
        {
            pitchedNominalLeft  = wetInputPitchShifterLeft.ProcessSample(nominalWetLeft);
            pitchedNominalRight = wetInputPitchShifterRight.ProcessSample(nominalWetRight);

            const int delaySamplesInt =
                static_cast<int>(std::round((delayMilliseconds * sampleRate) / 1000.0));

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

        // 8: Diffuse early tap and blend with pitched nominal
        const float diffusedEarlyLeft  = delayDiffusionLeft->ProcessSample(earlyWetLeft);
        const float diffusedEarlyRight = delayDiffusionRight->ProcessSample(earlyWetRight);

        const float diffusionDrive = juce::jlimit(0.0f, 1.0f, diffusionAmountSmoothed * 2.0f);
        const float cleanGain      = std::pow(1.0f - diffusionDrive, 4.0f);
        const float diffusedGain   = std::sin(diffusionDrive * juce::MathConstants<float>::halfPi);

        // Pitched nominal blends with un-pitched diffused tail
        float wetLeft  = pitchedNominalLeft  * cleanGain + diffusedEarlyLeft  * diffusedGain;
        float wetRight = pitchedNominalRight * cleanGain + diffusedEarlyRight * diffusedGain;

        // 9: Damp and feed back from composite wet — feedback remains un-pitched
        const float dampedLeft  = dampingLeft->ProcessSample(wetLeft,  lowpass01);
        const float dampedRight = dampingRight->ProcessSample(wetRight, lowpass01);

        lastFeedbackL = dampedLeft  * feedbackGain;
        lastFeedbackR = dampedRight * feedbackGain;

        // 9: Stereo spread (on pitched wet)
        float spreadWetLeft  = wetLeft;
        float spreadWetRight = wetRight;

        const float spread = juce::jlimit(-1.0f, 1.0f, stereoSpreadMinus1To1);

        if (std::abs(spread) > 0.0001f)
        {
            const float widen  = std::max(0.0f,  spread);
            const float narrow = std::max(0.0f, -spread);

            if (widen > 0.0f)
            {
                const float cross    = widen * 0.25f;
                const float newLeft  = spreadWetLeft  - cross * spreadWetRight;
                const float newRight = spreadWetRight - cross * spreadWetLeft;
                spreadWetLeft  = newLeft;
                spreadWetRight = newRight;
            }
            else if (narrow > 0.0f)
            {
                const float mono = 0.5f * (spreadWetLeft + spreadWetRight);
                spreadWetLeft  = spreadWetLeft  * (1.0f - narrow) + mono * narrow;
                spreadWetRight = spreadWetRight * (1.0f - narrow) + mono * narrow;
            }
        }

        // 10: Dry/Wet
        float outputLeft  = PMath::EqualPowerCrossfade(dryLeft,  spreadWetLeft,  dryWet01);
        float outputRight = PMath::EqualPowerCrossfade(dryRight, spreadWetRight, dryWet01);

        // 11: Optional post filtering
        if (hplpPrePost01 >= 0.5f)
        {
            outputLeft  = highpassL.processSample(outputLeft);
            outputLeft  = lowpassL.processSample(outputLeft);

            outputRight = highpassR.processSample(outputRight);
            outputRight = lowpassR.processSample(outputRight);
        }

        leftData[sampleIndex] = outputLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = outputRight;
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

void NewDelayReverb::SetPitchShiftEnabled(float pitchShiftEnabled01)
{
    pitchShiftEnabled = clamp01(pitchShiftEnabled01);
}

void NewDelayReverb::SetPitchShiftRangeLower(float pitchShiftRangeLower01)
{
    pitchShiftRangeLower = juce::jlimit(-48.0f, 48.0f, pitchShiftRangeLower01);
    rebuildPitchSequences();
}

void NewDelayReverb::SetPitchShiftRangeUpper(float pitchShiftRangeUpper01)
{
    pitchShiftRangeUpper = juce::jlimit(-48.0f, 48.0f, pitchShiftRangeUpper01);
    rebuildPitchSequences();
}

void NewDelayReverb::SetPitchShiftMode(int modeIndex)
{
    pitchShiftMode = juce::jlimit(0, 2, modeIndex);
    rebuildPitchSequences();
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
        delayDiffusionLeft->Configure(delayTunings, diffusionQualityStages, diffusionSize01,
            0.005f, 0.2f, 0.3f);
    }

    if (delayDiffusionRight != nullptr)
    {
        delayDiffusionRight->Configure(delayTunings, diffusionQualityStages, diffusionSize01,
            0.005f, 0.2f, 0.3f);
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

void NewDelayReverb::rebuildPitchSequences()
{
    wetInputPitchShifterLeft.RebuildSequence(pitchShiftMode, pitchShiftRangeLower, pitchShiftRangeUpper);
    wetInputPitchShifterRight.RebuildSequence(pitchShiftMode, pitchShiftRangeLower, pitchShiftRangeUpper);
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
