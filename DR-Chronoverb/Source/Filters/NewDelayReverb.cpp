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

    // Apply any pending diffusion reconfiguration at block boundary (safe point)
    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();

    // Ensure filters are up to date
    updateFilters();

    // Per-sample processing based on Deelay estimate:
    // 1) input + previous feedback
    // 2) diffusion chain (amount crossfade)
    // 3) write to main delay; read delayed sample at delayMilliseconds
    // 4) damping in feedback path, scale by feedbackGain, store for next iteration
    // 5) dry/wet mix
    // 6) optional pre/post HP/LP

    float* leftData = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float inputL = leftData[sampleIndex];
        const float inputR = (rightData != nullptr ? rightData[sampleIndex] : inputL);

        // Optional pre HP/LP if hplpPrePost01 == 0 (Pre)
        float preL = inputL;
        float preR = inputR;

        if (hplpPrePost01 < 0.5f)
        {
            preL = highpassL.processSample(preL);
            preL = lowpassL.processSample(preL);

            preR = highpassR.processSample(preR);
            preR = lowpassR.processSample(preR);
        }

        // 1) Sum input + feedback
        float summedL = preL + lastFeedbackL;
        float summedR = preR + lastFeedbackR;

        // 2) Diffusion
        const float diffusedL = diffusionLeft->ProcessSample(summedL, diffusionAmount01);
        const float diffusedR = diffusionRight->ProcessSample(summedR, diffusionAmount01);

        // 3) Main delay write/read
        mainDelayLeft->PushSample(diffusedL);
        mainDelayRight->PushSample(diffusedR);

        // Compensate for diffusion group delay (align cluster to nominal tap)
        const float effectiveDelayMs = std::max(0.0f, delayMilliseconds - diffusionGroupDelayMilliseconds);

        const float delayedL = mainDelayLeft->ReadDelayMilliseconds(effectiveDelayMs, sampleRate);
        const float delayedR = mainDelayRight->ReadDelayMilliseconds(effectiveDelayMs, sampleRate);

        // 4) Damping in feedback path
        const float dampedL = dampingLeft->ProcessSample(delayedL, lowpass01);
        const float dampedR = dampingRight->ProcessSample(delayedR, lowpass01);

        // Feedback gain
        lastFeedbackL = dampedL * feedbackGain;
        lastFeedbackR = dampedR * feedbackGain;

        // 5) Dry/Wet mix
        float wetL = delayedL;
        float wetR = delayedR;

        // Simple stereo spread: crossmix based on stereoSpreadMinus1To1.
        // Positive -> widen (add anti-phase-ish crossfeed), Negative -> narrow (crossmix toward mono).
        const float spread = juce::jlimit(-1.0f, 1.0f, stereoSpreadMinus1To1);
        if (std::abs(spread) > 0.0001f)
        {
            const float widenAmount = std::max(0.0f, spread);
            const float narrowAmount = std::max(0.0f, -spread);

            if (widenAmount > 0.0f)
            {
                const float cross = widenAmount * 0.25f;
                const float newL = wetL - cross * wetR;
                const float newR = wetR - cross * wetL;
                wetL = newL;
                wetR = newR;
            }
            else if (narrowAmount > 0.0f)
            {
                const float mono = 0.5f * (wetL + wetR);
                wetL = wetL * (1.0f - narrowAmount) + mono * narrowAmount;
                wetR = wetR * (1.0f - narrowAmount) + mono * narrowAmount;
            }
        }

        const float dryGain = 1.0f - dryWet01;
        const float wetGain = dryWet01;

        float outL = dryGain * inputL + wetGain * wetL;
        float outR = dryGain * inputR + wetGain * wetR;

        // 6) Optional post HP/LP if hplpPrePost01 == 1 (Post)
        if (hplpPrePost01 >= 0.5f)
        {
            outL = highpassL.processSample(outL);
            outL = lowpassL.processSample(outL);

            outR = highpassR.processSample(outR);
            outR = lowpassR.processSample(outR);
        }

        leftData[sampleIndex] = outL;

        if (rightData != nullptr)
            rightData[sampleIndex] = outR;
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
    }

    if (diffusionRight)
    {
        diffusionRight->Configure(diffusionQualityStages, diffusionSize01);
        diffusionGroupDelayMilliseconds = diffusionLeft->GetEstimatedGroupDelayMilliseconds();
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