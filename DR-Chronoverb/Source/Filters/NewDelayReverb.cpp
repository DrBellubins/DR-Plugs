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

    // Create reverb diffusion chains
    reverbDiffusionLeft.reset(new DiffusionChain());
    reverbDiffusionRight.reset(new DiffusionChain());

    reverbDiffusionLeft->Prepare(sampleRate);
    reverbDiffusionRight->Prepare(sampleRate);

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

    if (reverbDiffusionLeft)  reverbDiffusionLeft->ClearState();
    if (reverbDiffusionRight) reverbDiffusionRight->ClearState();

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

    float* leftData = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    pitchShifterLatencyMs = wetInputPitchShifterLeft.GetLatencyMilliseconds();

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float inputLeft = leftData[sampleIndex];
        const float inputRight = (rightData != nullptr ? rightData[sampleIndex] : inputLeft);

        // 1: Add feedback to input (no diffusion yet)
        float preLeft  = inputLeft  + lastFeedbackL;
        float preRight = inputRight + lastFeedbackR;

        // Pre HP/LP
        if (hplpPrePost01 < 0.5f)
        {
            preLeft = highpassL.processSample(preLeft);
            preLeft = lowpassL.processSample(preLeft);

            preRight = highpassR.processSample(preRight);
            preRight = lowpassR.processSample(preRight);
        }

        // Compute blend amounts for each diffusion path.
        // Pre-write: rises 0→1 over amount 0→0.5, then falls 1→0 over 0.5→1.0
        // Feedback:  stays 0 for amount 0→0.5, then rises 0→1 over 0.5→1.0
        float preWriteBlend;

        if (diffusionAmount01 <= 0.5f)
            preWriteBlend = diffusionAmount01 * 2.0f;
        else
            preWriteBlend = 1.0f - (diffusionAmount01 - 0.5f) * 2.0f;

        const float delayReverbDiffBlend = std::max(0.0f, (diffusionAmount01 - 0.5f) * 2.0f);

        // 2: Pre-write diffusion (only active in lower half of amount range)
        float writeLeft  = preLeft;
        float writeRight = preRight;

        if (preWriteBlend > 0.001f)
        {
            float diffusedLeft  = delayDiffusionLeft->ProcessSample(preLeft);
            float diffusedRight = delayDiffusionRight->ProcessSample(preRight);

            const float cleanGain    = std::cos(preWriteBlend * juce::MathConstants<float>::halfPi);
            const float diffusedGain = std::sin(preWriteBlend * juce::MathConstants<float>::halfPi);

            writeLeft  = preLeft  * cleanGain + diffusedLeft  * diffusedGain;
            writeRight = preRight * cleanGain + diffusedRight * diffusedGain;
        }

        // 3: Write to delay line
        mainDelayLeft->PushSample(writeLeft);
        mainDelayRight->PushSample(writeRight);

        // 4: Read compensation only applies while pre-write diffusion is active
        const float halfDiffusionMs     = totalDiffusionMs * 0.5f * preWriteBlend;
        const float centeredReadDelayMs = std::max(1.0f, delayMilliseconds - halfDiffusionMs);

        // 5: Read tap
        float outputLeft  = mainDelayLeft->ReadDelayMilliseconds(centeredReadDelayMs, sampleRate);
        float outputRight = mainDelayRight->ReadDelayMilliseconds(centeredReadDelayMs, sampleRate);

        // 6: Damping always applies in feedback path
        const float dampedLeft  = dampingLeft->ProcessSample(outputLeft,  lowpass01);
        const float dampedRight = dampingRight->ProcessSample(outputRight, lowpass01);

        // 7: Feedback diffusion (only active in upper half of amount range)
        // This is what creates the reverb tail at high amounts — each feedback
        // pass through the diffusion chain adds density until discrete echoes
        // merge into a continuous reverb.
        float feedbackLeft  = dampedLeft;
        float feedbackRight = dampedRight;

        if (delayReverbDiffBlend > 0.001f)
        {
            float diffusedFBLeft  = reverbDiffusionLeft->ProcessSample(dampedLeft);
            float diffusedFBRight = reverbDiffusionRight->ProcessSample(dampedRight);

            const float cleanGain    = std::cos(delayReverbDiffBlend * juce::MathConstants<float>::halfPi);
            const float diffusedGain = std::sin(delayReverbDiffBlend * juce::MathConstants<float>::halfPi);

            feedbackLeft  = dampedLeft  * cleanGain + diffusedFBLeft  * diffusedGain;
            feedbackRight = dampedRight * cleanGain + diffusedFBRight * diffusedGain;
        }

        lastFeedbackL = feedbackLeft  * feedbackGain;
        lastFeedbackR = feedbackRight * feedbackGain;

        // Wet output is the tap directly
        float wetLeft  = outputLeft;
        float wetRight = outputRight;

        // 8: Progressive pitch shift (shimmer)
        //const float pitchedFeedbackLeft = wetInputPitchShifterLeft.ProcessSample(dampedLeft);
        //const float pitchedFeedbackRight = wetInputPitchShifterRight.ProcessSample(dampedRight);

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

        // 9: Output crossfade: morph between raw delay and diffused
        //const float fade = diffusionAmount01 * 0.5f * juce::MathConstants<float>::pi;
        //const float wetLeft = PMath::EqualPowerCrossfade(outputLeft, dampedLeft, fade);
        //const float wetRight = PMath::EqualPowerCrossfade(outputRight, dampedRight, fade);

        // 10: Stereo spread on wet
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

        // 11: Dry/Wet mix
        float outLeft = PMath::EqualPowerCrossfade(inputLeft, spreadWetLeft, dryWet01);
        float outRight = PMath::EqualPowerCrossfade(inputRight, spreadWetRight, dryWet01);

        // 12: Post HP/LP
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
    // Only rebuild if something actually changed
    if (diffusionQualityStages == lastBuiltQualityStages
        && diffusionSize01 == lastBuiltSize01)
    {
        return;
    }

    lastBuiltQualityStages = diffusionQualityStages;
    lastBuiltSize01 = diffusionSize01;

    if (delayDiffusionLeft)
        delayDiffusionLeft->Configure(diffusionQualityStages, diffusionSize01);

    if (delayDiffusionRight)
        delayDiffusionRight->Configure(diffusionQualityStages, diffusionSize01);

    if (reverbDiffusionLeft)
        reverbDiffusionLeft->Configure(diffusionQualityStages, diffusionSize01);

    if (reverbDiffusionRight)
        reverbDiffusionRight->Configure(diffusionQualityStages, diffusionSize01);

    totalDiffusionMs = 0.0f;

    for (float ms : delayDiffusionLeft->perStageDelayMs)
        totalDiffusionMs += ms;
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
