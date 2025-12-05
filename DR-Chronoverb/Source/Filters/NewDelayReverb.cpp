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

    // Create main delay lines with 1000 ms max buffer
    const int maxDelaySamples = static_cast<int>(std::ceil(1.0 * sampleRate)); // 1000 ms
    mainDelayLeft.reset(new DelayLine(maxDelaySamples));
    mainDelayRight.reset(new DelayLine(maxDelaySamples));

    // Create diffusion chains
    diffusionLeft.reset(new DiffusionChain());
    diffusionRight.reset(new DiffusionChain());

    diffusionLeft->Prepare(sampleRate);
    diffusionRight->Prepare(sampleRate);

    // Initial diffusion config (safe here; not concurrently processing)
    rebuildDiffusionIfNeeded();
    diffusionRebuildPending.store(false, std::memory_order_release);

    // Damping filters (feedback path)
    dampingLeft.reset(new DampingFilter());
    dampingRight.reset(new DampingFilter());
    dampingLeft->Prepare(sampleRate);
    dampingRight->Prepare(sampleRate);

    // Simple FDN placeholders
    fdnLeft.reset(new SimpleFDN());
    fdnRight.reset(new SimpleFDN());
    fdnLeft->Prepare(sampleRate);
    fdnRight->Prepare(sampleRate);

    updateDelayMillisecondsFromNormalized();
    updateFeedbackGainFromFeedbackTime();

    // Initialize HP/LP filters
    updateFilters();

    // Spread setup
    updateStereoSpread();

    lastFeedbackL = 0.0f;
    lastFeedbackR = 0.0f;

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

    updateFilters();

    float* leftData = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float inputLeft = leftData[sampleIndex];
        const float inputRight = (rightData != nullptr ? rightData[sampleIndex] : inputLeft);

        float preLeft = inputLeft;
        float preRight = inputRight;

        if (hplpPrePost01 < 0.5f)
        {
            preLeft = highpassL.processSample(preLeft);
            preLeft = lowpassL.processSample(preLeft);

            preRight = highpassR.processSample(preRight);
            preRight = lowpassR.processSample(preRight);
        }

        // 1) Input + feedback (undiffused sum)
        const float sumLeft = preLeft + lastFeedbackL;
        const float sumRight = preRight + lastFeedbackR;

        // 2) Compute fully diffused versions (internal diffuser always at 1.0)
        const float diffusedLeft = diffusionLeft->ProcessSample(sumLeft, 1.0f);
        const float diffusedRight = diffusionRight->ProcessSample(sumRight, 1.0f);

        // 3) Write-path diffusion controlled by diffusionAmount01:
        //    - amount = 0 -> write undiffused sum (hard taps)
        //    - amount = 1 -> write fully diffused sum (lush)
        const float writeMix = diffusionAmount01;
        const float writeLeft = sumLeft * (1.0f - writeMix) + diffusedLeft * writeMix;
        const float writeRight = sumRight * (1.0f - writeMix) + diffusedRight * writeMix;

        mainDelayLeft->PushSample(writeLeft);
        mainDelayRight->PushSample(writeRight);

        // 4) Base tap (for both wet and feedback reference)
        const float baseMs = delayMilliseconds;
        const float baseTapLeft = mainDelayLeft->ReadDelayMilliseconds(baseMs, sampleRate);
        const float baseTapRight = mainDelayRight->ReadDelayMilliseconds(baseMs, sampleRate);

        // 5) Reverb-offset tap (earlier by half the cluster width, clamped)
        const float groupClusterMS = std::max(0.0f, diffusionClusterWidthMilliseconds) * 0.5f;
        const float offsetMs = std::max(0.0f, baseMs - groupClusterMS);

        const float swellTapLeft = mainDelayLeft->ReadDelayMilliseconds(offsetMs, sampleRate);
        const float swellTapRight = mainDelayRight->ReadDelayMilliseconds(offsetMs, sampleRate);

        // 6) Wet output “swell” crossfade:
        //    Use equal-power crossfade to avoid the 0.5 level dip/artifact.
        const float swellCrosfadeA = std::sqrt(1.0f - diffusionAmount01);
        const float swellCrosfadeB = std::sqrt(diffusionAmount01);

        float wetLeft = baseTapLeft * swellCrosfadeA + swellTapLeft * swellCrosfadeB;
        float wetRight = baseTapRight * swellCrosfadeA + swellTapRight * swellCrosfadeB;

        // 7) Blend amount drives FDN input (0: bypass FDN for clean delay, 1: full FDN for reverb)
        const float fdnInputLeft = wetLeft * diffusionAmount01;
        const float fdnInputRight = wetRight * diffusionAmount01;

        wetLeft = fdnLeft->ProcessSample(fdnInputLeft) + wetLeft * (1.0f - diffusionAmount01);
        wetRight = fdnRight->ProcessSample(fdnInputRight) + wetRight * (1.0f - diffusionAmount01);

        // 8) Feedback: drive from base tap only (stable energy, no comb cancellation in-loop)
        const float dampedLeft = dampingLeft->ProcessSample(baseTapLeft, lowpass01);
        const float dampedRight = dampingRight->ProcessSample(baseTapRight, lowpass01);

        lastFeedbackL = dampedLeft * feedbackGain;
        lastFeedbackR = dampedRight * feedbackGain;

        // 9) Stereo spread on wet
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
                const float newL = spreadWetLeft - cross * spreadWetRight;
                const float newR = spreadWetRight - cross * spreadWetLeft;
                spreadWetLeft = newL;
                spreadWetRight = newR;
            }
            else if (narrow > 0.0f)
            {
                const float mono = 0.5f * (spreadWetLeft + spreadWetRight);
                spreadWetLeft = spreadWetLeft * (1.0f - narrow) + mono * narrow;
                spreadWetRight = spreadWetRight * (1.0f - narrow) + mono * narrow;
            }
        }

        // 10) Dry/Wet mix
        const float dryGain = 1.0f - dryWet01;
        const float wetGain = dryWet01;

        float outLeft = dryGain * inputLeft + wetGain * spreadWetLeft;
        float outRight = dryGain * inputRight + wetGain * spreadWetRight;

        if (hplpPrePost01 >= 0.5f)
        {
            outLeft = highpassL.processSample(outLeft);
            outLeft = lowpassL.processSample(outLeft);

            outRight = highpassR.processSample(outRight);
            outRight = lowpassR.processSample(outRight);
        }

        leftData[sampleIndex] = outLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = outRight;
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
    updateFilters();
}

void NewDelayReverb::SetHighpassCutoff(float newHighpass01)
{
    highpass01 = clamp01(newHighpass01);
    updateFilters();
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

void NewDelayReverb::SetHostTempo(float bpm)
{
    if (bpm > 0.0f)
        hostTempoBpm = bpm;
}

// ---------------- Internal helpers ----------------

void NewDelayReverb::updateDelayMillisecondsFromNormalized()
{
    delayMilliseconds = map01ToRange(delayTimeNormalized, 0.0f, 1000.0f);
}

void NewDelayReverb::rebuildDiffusionIfNeeded()
{
    if (diffusionLeft)
    {
        diffusionLeft->Configure(diffusionQualityStages, diffusionSize01);
        diffusionGroupDelayMilliseconds = diffusionLeft->GetEstimatedGroupDelayMilliseconds();
        diffusionClusterWidthMilliseconds = diffusionLeft->GetEstimatedClusterWidthMilliseconds();
    }

    if (diffusionRight)
    {
        diffusionRight->Configure(diffusionQualityStages, diffusionSize01);
        // Left and right chains are identical; reuse left’s estimates
        diffusionGroupDelayMilliseconds = diffusionLeft->GetEstimatedGroupDelayMilliseconds();
        diffusionClusterWidthMilliseconds = diffusionLeft->GetEstimatedClusterWidthMilliseconds();
    }
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