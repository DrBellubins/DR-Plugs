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

/*void NewDelayReverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
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
        const float dryLeft  = leftData[sampleIndex];
        const float dryRight = (rightData != nullptr ? rightData[sampleIndex] : dryLeft);

        // --- Compute target blends ---
        float targetPreWriteBlend;

        if (diffusionAmount01 <= 0.5f)
            targetPreWriteBlend = diffusionAmount01 * 2.0f;
        else
            targetPreWriteBlend = 1.0f - (diffusionAmount01 - 0.5f) * 2.0f;

        const float targetFeedbackDiffBlend = std::max(0.0f, (diffusionAmount01 - 0.5f) * 2.0f);

        // Slew blend toward target — fast enough for responsiveness, but
        // it only affects write content, never the read position, so there
        // is zero pitch artifact regardless of how fast it moves.
        smoothedDelayReverbDiffBlend += kBlendSlewCoeff * (diffusionAmount01 - smoothedDelayReverbDiffBlend);

        // 1: Pre HP/LP on dry signal only
        float filteredDryLeft  = dryLeft;
        float filteredDryRight = dryRight;

        if (hplpPrePost01 < 0.5f)
        {
            filteredDryLeft  = highpassL.processSample(filteredDryLeft);
            filteredDryLeft  = lowpassL.processSample(filteredDryLeft);
            filteredDryRight = highpassR.processSample(filteredDryRight);
            filteredDryRight = lowpassR.processSample(filteredDryRight);
        }

        // 2: Sum input + feedback (Deelay architecture: diffusion sees the full sum)
        float preLeft  = filteredDryLeft  + lastFeedbackL;
        float preRight = filteredDryRight + lastFeedbackR;

        // 3: Diffuse the summed signal before writing.
        //    Lower half (0..0.5): delay-quality diffusion chain.
        //    Upper half (0.5..1.0): crossfade toward reverb-quality chain.
        //    Read position is NEVER touched — no pitch artifact possible.
        float writeLeft  = preLeft;
        float writeRight = preRight;

        if (smoothedDelayReverbDiffBlend > 0.001f)
        {
            const float blend = smoothedDelayReverbDiffBlend;

            // Select which chain dominates based on which half of the range we're in
            float diffLeft, diffRight;

            if (blend <= 0.5f)
            {
                // Delay diffusion only
                diffLeft  = delayDiffusionLeft->ProcessSample(preLeft);
                diffRight = delayDiffusionRight->ProcessSample(preRight);
            }
            else
            {
                // Crossfade between delay and reverb chains
                const float reverbBlend = (blend - 0.5f) * 2.0f; // 0..1 over upper half

                const float delayDiffLeft  = delayDiffusionLeft->ProcessSample(preLeft);
                const float delayDiffRight = delayDiffusionRight->ProcessSample(preRight);
                const float reverbDiffLeft  = reverbDiffusionLeft->ProcessSample(preLeft);
                const float reverbDiffRight = reverbDiffusionRight->ProcessSample(preRight);

                const float delayGain  = std::cos(reverbBlend * juce::MathConstants<float>::halfPi);
                const float reverbGain = std::sin(reverbBlend * juce::MathConstants<float>::halfPi);

                diffLeft  = delayDiffLeft  * delayGain + reverbDiffLeft  * reverbGain;
                diffRight = delayDiffRight * delayGain + reverbDiffRight * reverbGain;
            }

            // Equal-power crossfade between clean pre and diffused pre
            const float cleanGain    = std::cos(blend * juce::MathConstants<float>::halfPi);
            const float diffusedGain = std::sin(blend * juce::MathConstants<float>::halfPi);

            writeLeft  = preLeft  * cleanGain + diffLeft  * diffusedGain;
            writeRight = preRight * cleanGain + diffRight * diffusedGain;
        }

        // 4: Write diffused signal to delay line
        mainDelayLeft->PushSample(writeLeft);
        mainDelayRight->PushSample(writeRight);

        // 5: Read at FIXED delay — this never moves, so automation is artifact-free
        float outputLeft  = mainDelayLeft->ReadDelayMilliseconds(delayMilliseconds, sampleRate);
        float outputRight = mainDelayRight->ReadDelayMilliseconds(delayMilliseconds, sampleRate);

        // 6: Damping + feedback
        const float dampedLeft  = dampingLeft->ProcessSample(outputLeft,  lowpass01);
        const float dampedRight = dampingRight->ProcessSample(outputRight, lowpass01);

        lastFeedbackL = dampedLeft  * feedbackGain;
        lastFeedbackR = dampedRight * feedbackGain;

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
        float outLeft = PMath::EqualPowerCrossfade(dryLeft, spreadWetLeft, dryWet01);
        float outRight = PMath::EqualPowerCrossfade(dryRight, spreadWetRight, dryWet01);

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
}*/

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

    pitchShifterLatencyMs = wetInputPitchShifterLeft.GetLatencyMilliseconds();

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

        // 2: Input + feedback
        const float preLeft = filteredDryLeft + lastFeedbackL;
        const float preRight = filteredDryRight + lastFeedbackR;

        // 3: Delay-path diffusion before write only
        float writeLeft = preLeft;
        float writeRight = preRight;

        if (diffusionAmountSmoothed > 0.0001f)
        {
            const float delayDiffusedLeft = delayDiffusionLeft->ProcessSample(preLeft);
            const float delayDiffusedRight = delayDiffusionRight->ProcessSample(preRight);

            const float cleanWriteGain = std::cos(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);
            const float diffusedWriteGain = std::sin(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);

            writeLeft = preLeft * cleanWriteGain + delayDiffusedLeft * diffusedWriteGain;
            writeRight = preRight * cleanWriteGain + delayDiffusedRight * diffusedWriteGain;
        }

        // 4: Write
        mainDelayLeft->PushSample(writeLeft);
        mainDelayRight->PushSample(writeRight);

        // 5: Center smear around nominal tap with smoothed, compensated read
        const float halfDiffusionMilliseconds = 0.5f * totalDelayDiffusionMilliseconds * diffusionAmountSmoothed;
        const float targetCenteredReadDelayMilliseconds = std::max(1.0f, delayMilliseconds - halfDiffusionMilliseconds);

        smoothedCenteredReadDelayMilliseconds +=
            readDelaySlewCoefficient * (targetCenteredReadDelayMilliseconds - smoothedCenteredReadDelayMilliseconds);

        const float centeredReadDelayMilliseconds = std::max(1.0f, smoothedCenteredReadDelayMilliseconds);

        // Read only once, no post-read diffusion branch
        float wetLeft = mainDelayLeft->ReadDelayMilliseconds(centeredReadDelayMilliseconds, sampleRate);
        float wetRight = mainDelayRight->ReadDelayMilliseconds(centeredReadDelayMilliseconds, sampleRate);

        // 7: Damping + feedback from composite wet (keeps loop behavior consistent with diffusion)
        const float dampedLeft = dampingLeft->ProcessSample(wetLeft, lowpass01);
        const float dampedRight = dampingRight->ProcessSample(wetRight, lowpass01);

        lastFeedbackL = dampedLeft * feedbackGain;
        lastFeedbackR = dampedRight * feedbackGain;

        // Echo boundary counters (unchanged)
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

    if (reverbDiffusionLeft != nullptr)
    {
        reverbDiffusionLeft->ConfigureAsReverb(diffusionQualityStages, diffusionSize01);
    }

    if (reverbDiffusionRight != nullptr)
    {
        reverbDiffusionRight->ConfigureAsReverb(diffusionQualityStages, diffusionSize01);
    }

    totalDelayDiffusionMilliseconds = 0.0f;

    if (delayDiffusionLeft != nullptr)
    {
        for (float stageDelayMilliseconds : delayDiffusionLeft->perStageDelayMs)
        {
            totalDelayDiffusionMilliseconds += stageDelayMilliseconds;
        }
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
