#include "NewDelayReverb.h"

#include <cmath>
#include <algorithm>

NewDelayReverb::NewDelayReverb()
{
}

NewDelayReverb::~NewDelayReverb()
{
}

void NewDelayReverb::PrepareToPlay(double newSampleRate, float initialHostTempoBpm)
{
    sampleRate    = newSampleRate;
    hostTempoBpm  = initialHostTempoBpm;

    // Prepare IIR filters
    juce::dsp::ProcessSpec filterSpec;
    filterSpec.sampleRate = sampleRate;
    filterSpec.maximumBlockSize = 4096;
    filterSpec.numChannels = 1;

    lowpassL.prepare(filterSpec);
    lowpassR.prepare(filterSpec);
    highpassL.prepare(filterSpec);
    highpassR.prepare(filterSpec);

    filterRebuildPending.store(true, std::memory_order_release);
    updateFilters();
    filterRebuildPending.store(false, std::memory_order_release);

    // Main delay lines (1000 ms max)
    const int maxDelaySamples = static_cast<int>(std::ceil(1.0 * sampleRate));
    mainDelayLeft.reset(new DelayLine(maxDelaySamples));
    mainDelayRight.reset(new DelayLine(maxDelaySamples));

    // Delay-quality diffusion chains
    delayDiffusionLeft.reset(new DiffusionChain());
    delayDiffusionRight.reset(new DiffusionChain());
    delayDiffusionLeft->Prepare(sampleRate);
    delayDiffusionRight->Prepare(sampleRate);

    // Reverb-quality diffusion chains
    reverbDiffusionLeft.reset(new DiffusionChain());
    reverbDiffusionRight.reset(new DiffusionChain());
    reverbDiffusionLeft->Prepare(sampleRate);
    reverbDiffusionRight->Prepare(sampleRate);

    // Force full rebuild
    lastBuiltQualityStages = -1;
    lastBuiltSize01 = -1.0f;
    rebuildDiffusionIfNeeded();

    if (delayDiffusionLeft) delayDiffusionLeft->ClearState();
    if (delayDiffusionRight) delayDiffusionRight->ClearState();
    if (reverbDiffusionLeft) reverbDiffusionLeft->ClearState();
    if (reverbDiffusionRight) reverbDiffusionRight->ClearState();

    smoothedDelayReverbDiffBlend = 0.0f;
    diffusionRebuildPending.store(false, std::memory_order_release);

    // Damping filters
    dampingLeft.reset(new DampingFilter());
    dampingRight.reset(new DampingFilter());
    dampingLeft->Prepare(sampleRate);
    dampingRight->Prepare(sampleRate);

    updateDelayMillisecondsFromNormalized();
    updateFeedbackGainFromFeedbackTime();
    updateStereoSpread();

    // Pitch shifters
    wetInputPitchShifterLeft.Prepare(sampleRate, 512);
    wetInputPitchShifterRight.Prepare(sampleRate, 512);
    wetInputPitchShifterLeft.SetEnabled(true);
    wetInputPitchShifterRight.SetEnabled(true);

    rebuildPitchSequences();

    echoSampleCounterL = 0;
    echoSampleCounterR = 0;

    lastFeedbackL = 0.0f;
    lastFeedbackR = 0.0f;

    kBlendSlewCoeff = 1.0f / (0.01f * static_cast<float>(sampleRate));

    smoothedCenteredReadDelayMilliseconds = delayMilliseconds;
    readDelaySlewCoefficient = 1.0f / (0.02f * static_cast<float>(sampleRate));

    mainDelayLeft->Clear();
    mainDelayRight->Clear();
}

void NewDelayReverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples  = audioBuffer.getNumSamples();

    if (numChannels < 1 || numSamples <= 0)
        return;

    if (diffusionRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildDiffusionIfNeeded();

    if (filterRebuildPending.exchange(false, std::memory_order_acq_rel))
        updateFilters();

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    pitchShifterLatencyMs = wetInputPitchShifterLeft.GetLatencyMilliseconds();

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        delayDiffusionLeft->UpdateSize(diffusionSize01);
        delayDiffusionRight->UpdateSize(diffusionSize01);
        reverbDiffusionLeft->UpdateSize(diffusionSize01);
        reverbDiffusionRight->UpdateSize(diffusionSize01);

        const float dryLeft = leftData[sampleIndex];
        const float dryRight = (rightData != nullptr ? rightData[sampleIndex] : dryLeft);

        // Smooth diffusion amount
        smoothedDelayReverbDiffBlend +=
            kBlendSlewCoeff * (diffusionAmount01 - smoothedDelayReverbDiffBlend);

        const float diffusionAmountSmoothed =
            juce::jlimit(0.0f, 1.0f, smoothedDelayReverbDiffBlend);

        // ---- 1: Optional pre-filtering ----
        float filteredDryLeft = dryLeft;
        float filteredDryRight = dryRight;

        if (hplpPrePost01 < 0.5f)
        {
            filteredDryLeft  = highpassL.processSample(filteredDryLeft);
            filteredDryLeft  = lowpassL.processSample(filteredDryLeft);
            filteredDryRight = highpassR.processSample(filteredDryRight);
            filteredDryRight = lowpassR.processSample(filteredDryRight);
        }

        // ---- 2: Sum input + feedback ----
        const float preLeft = filteredDryLeft  + lastFeedbackL;
        const float preRight = filteredDryRight + lastFeedbackR;

        // ---- 3: Optional pitch shift ----
        float pitchedLeft = preLeft;
        float pitchedRight = preRight;

        if (pitchShiftEnabled >= 0.5f)
        {
            pitchedLeft = wetInputPitchShifterLeft.ProcessSample(preLeft);
            pitchedRight = wetInputPitchShifterRight.ProcessSample(preRight);
        }

        // Advance echo boundary counters (needed regardless of pitch enable state)
        {
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

        // ---- 4: Pre-write diffusion (amount-controlled, dual-chain) ----
        //  0.0 .. 0.5 : only delayDiffusion chain (discrete tap blur)
        //  0.5 .. 1.0 : crossfade delayDiffusion -> reverbDiffusion (lush tail)
        float writeLeft = pitchedLeft;
        float writeRight = pitchedRight;

        if (diffusionAmountSmoothed > 0.001f)
        {
            float diffLeft;
            float diffRight;

            if (diffusionAmountSmoothed <= 0.5f)
            {
                // Lower half: delay-quality diffusion only
                diffLeft = delayDiffusionLeft->ProcessSample(pitchedLeft);
                diffRight = delayDiffusionRight->ProcessSample(pitchedRight);
            }
            else
            {
                // Upper half: crossfade between delay and reverb chains
                const float reverbBlend =
                    (diffusionAmountSmoothed - 0.5f) * 2.0f; // 0..1

                const float delayDiffLeft = delayDiffusionLeft->ProcessSample(pitchedLeft);
                const float delayDiffRight = delayDiffusionRight->ProcessSample(pitchedRight);
                const float reverbDiffLeft = reverbDiffusionLeft->ProcessSample(pitchedLeft);
                const float reverbDiffRight = reverbDiffusionRight->ProcessSample(pitchedRight);

                const float delayGain =
                    std::cos(reverbBlend * juce::MathConstants<float>::halfPi);

                const float reverbGain =
                    std::sin(reverbBlend * juce::MathConstants<float>::halfPi);

                diffLeft = delayDiffLeft  * delayGain + reverbDiffLeft  * reverbGain;
                diffRight = delayDiffRight * delayGain + reverbDiffRight * reverbGain;
            }

            // Equal-power blend between clean pitched and diffused
            const float cleanGain =
                std::cos(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);

            const float diffusedGain =
                std::sin(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);

            writeLeft = pitchedLeft  * cleanGain + diffLeft  * diffusedGain;
            writeRight = pitchedRight * cleanGain + diffRight * diffusedGain;
        }

        // ---- 5: Write to delay line ----
        mainDelayLeft->PushSample(writeLeft);
        mainDelayRight->PushSample(writeRight);

        // ---- 6: Read nominal tap and early tap ----
        smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
            (delayMilliseconds - smoothedCenteredReadDelayMilliseconds);

        const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;

        const float earlyReadMilliseconds =
            std::max(1.0f, delayMilliseconds - staticDiffusionCompensationMilliseconds);

        const float nominalWetLeft  =
            mainDelayLeft->ReadDelayMilliseconds(nominalReadMilliseconds, sampleRate);

        const float nominalWetRight =
            mainDelayRight->ReadDelayMilliseconds(nominalReadMilliseconds, sampleRate);

        const float earlyWetLeft  =
            mainDelayLeft->ReadDelayMilliseconds(earlyReadMilliseconds, sampleRate);

        const float earlyWetRight =
            mainDelayRight->ReadDelayMilliseconds(earlyReadMilliseconds, sampleRate);

        // ---- 7: Diffuse the early tap (second pass through delay chain) ----
        // This gives the characteristic "blur around each tap" and creates
        // audible content before the nominal tap on each feedback recirculation.
        const float diffusedEarlyLeft = delayDiffusionLeft->ProcessSample(earlyWetLeft);
        const float diffusedEarlyRight = delayDiffusionRight->ProcessSample(earlyWetRight);

        // Remap amount so clean tap suppression is aggressive (gone by amount=0.5)
        const float diffusionDrive =
            juce::jlimit(0.0f, 1.0f, diffusionAmountSmoothed * 2.0f);

        const float cleanTapGain =
            std::pow(1.0f - diffusionDrive, 4.0f); // collapses to 0 at drive >= 1

        const float diffusedTapGain =
            std::sin(diffusionDrive * juce::MathConstants<float>::halfPi);

        float wetLeft = nominalWetLeft  * cleanTapGain + diffusedEarlyLeft  * diffusedTapGain;
        float wetRight = nominalWetRight * cleanTapGain + diffusedEarlyRight * diffusedTapGain;

        // ---- 8: Damping + feedback recirculation ----
        const float dampedLeft = dampingLeft->ProcessSample(wetLeft,  lowpass01);
        const float dampedRight = dampingRight->ProcessSample(wetRight, lowpass01);

        lastFeedbackL = dampedLeft * feedbackGain;
        lastFeedbackR = dampedRight * feedbackGain;

        // ---- 9: Stereo spread ----
        float spreadWetLeft = wetLeft;
        float spreadWetRight = wetRight;

        const float spread = juce::jlimit(-1.0f, 1.0f, stereoSpreadMinus1To1);

        if (std::abs(spread) > 0.0001f)
        {
            const float widen = std::max(0.0f,  spread);
            const float narrow = std::max(0.0f, -spread);

            if (widen > 0.0f)
            {
                const float cross = widen * 0.25f;
                const float newLeft = spreadWetLeft  - cross * spreadWetRight;
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

        // ---- 10: Dry/wet mix ----
        float outputLeft = (dryLeft * dryVolume) + (spreadWetLeft * wetVolume);
        float outputRight = (dryRight * dryVolume) + (spreadWetRight * wetVolume);

        // ---- 11: Optional post-filtering ----
        if (hplpPrePost01 >= 0.5f)
        {
            outputLeft = highpassL.processSample(outputLeft);
            outputLeft = lowpassL.processSample(outputLeft);
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

void NewDelayReverb::SetDelayMode(int newDelayMode)
{
    delayMode = juce::jlimit(0, 3, newDelayMode);
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
}

void NewDelayReverb::SetDiffusionQuality(int newQualityStages)
{
    diffusionQualityStages = clampInt(newQualityStages, 1, 8);
    diffusionRebuildPending.store(true, std::memory_order_release);
}

void NewDelayReverb::SetDryVolume(float newDry01)
{
    dryVolume = clamp01(newDry01);
}

void NewDelayReverb::SetWetVolume(float newWet01)
{
    wetVolume = clamp01(newWet01);
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

void NewDelayReverb::SetPitchShiftRangeLower(float pitchShiftRangeLowerSemitones)
{
    pitchShiftRangeLower = juce::jlimit(-48.0f, 48.0f, pitchShiftRangeLowerSemitones);
    rebuildPitchSequences();
}

void NewDelayReverb::SetPitchShiftRangeUpper(float pitchShiftRangeUpperSemitones)
{
    pitchShiftRangeUpper = juce::jlimit(-48.0f, 48.0f, pitchShiftRangeUpperSemitones);
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
    {
        hostTempoBpm = bpm;

        if (delayMode != 0)
            updateDelayMillisecondsFromNormalized();
    }
}

// ---------------- Internal helpers ----------------

void NewDelayReverb::updateDelayMillisecondsFromNormalized()
{
    if (delayMode == 0) // ms — free range 1..1000 ms
    {
        const float BaseMs = map01ToRange(delayTimeNormalized, 1.0f, 1000.0f);
        delayMilliseconds = std::max(1.0f, BaseMs);
        return;
    }

    // Beat-synced modes. The knob snaps to 5 positions: 0, 0.25, 0.5, 0.75, 1.0
    // mapping to: 1/1 (slowest) → 1/16 (fastest).
    static constexpr float SnapPositions[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    // Beat multipliers in quarter notes: 1/1=4, 1/2=2, 1/4=1, 1/8=0.5, 1/16=0.25
    static constexpr float BeatMultipliers[5] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };

    // Find nearest snap index
    int StepIndex = 0;
    float SmallestDistance = std::abs(delayTimeNormalized - SnapPositions[0]);

    for (int i = 1; i < 5; ++i)
    {
        const float Distance = std::abs(delayTimeNormalized - SnapPositions[i]);

        if (Distance < SmallestDistance)
        {
            SmallestDistance = Distance;
            StepIndex = i;
        }
    }

    // Quarter-note duration in ms
    const float QuarterNoteMs = 60000.0f / hostTempoBpm;
    float BeatMs = BeatMultipliers[StepIndex] * QuarterNoteMs;

    if (delayMode == 2)      // Triplet: 2/3 of straight value
        BeatMs *= (2.0f / 3.0f);
    else if (delayMode == 3) // Dotted: 3/2 of straight value
        BeatMs *= 1.5f;

    delayMilliseconds = juce::jlimit(1.0f, 1000.0f, BeatMs);

    // After computing the new delayMilliseconds:
    // In beat-sync modes, slew over ~one echo cycle so the transition
    // sounds like a single pitched echo rather than a continuous sweep.
    // In ms mode, slew over a fixed short time for a musical glide.
    if (delayMode == 0)
    {
        // ms mode: fixed 80ms glide feels musical
        readDelaySlewCoefficient = 1.0f / (0.08f * static_cast<float>(sampleRate));
    }
    else
    {
        // Beat-sync: slew over one echo period so the pitch artifact
        // resolves in one repeat, matching Deelay's character.
        const float slewSeconds = std::max(0.05f, delayMilliseconds / 1000.0f);
        readDelaySlewCoefficient = 1.0f / (slewSeconds * static_cast<float>(sampleRate));
    }
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

    if (delayDiffusionLeft  != nullptr)
        delayDiffusionLeft->Configure(diffusionQualityStages, diffusionSize01, DelayTunings);

    if (delayDiffusionRight != nullptr)
        delayDiffusionRight->Configure(diffusionQualityStages, diffusionSize01, DelayTunings);

    if (reverbDiffusionLeft  != nullptr)
        reverbDiffusionLeft->ConfigureAsReverb(diffusionQualityStages, diffusionSize01, DeverbTunings);

    if (reverbDiffusionRight != nullptr)
        reverbDiffusionRight->ConfigureAsReverb(diffusionQualityStages, diffusionSize01, DeverbTunings);

    totalDelayDiffusionMilliseconds = 0.0f;

    if (delayDiffusionLeft != nullptr)
    {
        for (float stageDelayMilliseconds : delayDiffusionLeft->perStageDelayMs)
            totalDelayDiffusionMilliseconds += stageDelayMilliseconds;
    }

    const float baseCompensation = totalDelayDiffusionMilliseconds * centeredSwellRatio;
    staticDiffusionCompensationMilliseconds = baseCompensation * diffusionCompensationBias;
}

void NewDelayReverb::updateFeedbackGainFromFeedbackTime()
{
    const float normalized = juce::jlimit(0.0f, 1.0f, feedbackTimeSeconds / 10.0f);
    const float curved = std::sqrt(normalized);
    feedbackGain = std::max(0.0f, std::min(0.85f * curved, 0.95f));
}

void NewDelayReverb::updateFilters()
{
    const float lpHz = map01ToRange(lowpass01,  500.0f, 9000.0f);
    const float hpHz = map01ToRange(highpass01,  10.0f, 2000.0f);

    auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate,  lpHz);
    auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, hpHz);

    *lowpassL.coefficients = *lpCoeffs;
    *lowpassR.coefficients = *lpCoeffs;
    *highpassL.coefficients = *hpCoeffs;
    *highpassR.coefficients = *hpCoeffs;
}

void NewDelayReverb::updateStereoSpread()
{
    // Applied inline during ProcessBlock
}

void NewDelayReverb::rebuildPitchSequences()
{
    const int lowerOctave = static_cast<int>(std::round(pitchShiftRangeLower / 12.0f));
    const int upperOctave = static_cast<int>(std::round(pitchShiftRangeUpper / 12.0f));

    auto configureShifter = [&](OctaveEchoPitchShifter& shifter)
    {
        if (pitchShiftMode == 2) // Random
        {
            auto randomSequence = std::make_unique<RandomOctaveSequence>();
            randomSequence->SetRange(lowerOctave, upperOctave);

            shifter.SetSequence(std::move(randomSequence));
        }
        else // Up (0) or Down (1)
        {
            auto progressiveSequence = std::make_unique<ProgressiveOctaveSequence>();
            progressiveSequence->SetRange(lowerOctave, upperOctave);

            if (pitchShiftMode == 0) // Up
            {
                progressiveSequence->SetStartOctave(lowerOctave);
                progressiveSequence->SetStepOctaves(1);
            }
            else // Down
            {
                progressiveSequence->SetStartOctave(upperOctave);
                progressiveSequence->SetStepOctaves(-1);
            }

            shifter.SetSequence(std::move(progressiveSequence));
        }
    };

    configureShifter(wetInputPitchShifterLeft);
    configureShifter(wetInputPitchShifterRight);
}

float NewDelayReverb::map01ToRange(float value01, float minValue, float maxValue)
{
    return minValue + (maxValue - minValue) * clamp01(value01);
}

float NewDelayReverb::clamp01(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

int NewDelayReverb::clampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(maxValue, value));
}