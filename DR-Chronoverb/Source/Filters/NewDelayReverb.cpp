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

    // End Pitch Shift
    wetInputPitchShifterLeft.Prepare(sampleRate, 512);
    wetInputPitchShifterRight.Prepare(sampleRate, 512);
    wetInputPitchShifterLeft.SetEnabled(true);
    wetInputPitchShifterRight.SetEnabled(true);

    rebuildPitchSequences();

    wetInputPitchShifterLeft.CommitPendingSequenceNow();
    wetInputPitchShifterRight.CommitPendingSequenceNow();

    echoWriteCounterL = 0;
    echoWriteCounterR = 0;

    lastFeedbackL = 0.0f;
    lastFeedbackR = 0.0f;

    postPitchAllpassLeft.Prepare(sampleRate);
    postPitchAllpassLeft.Configure(pitchShiftAllpassDelay, 0.6f); // 15 ms delay, gain 0.6

    postPitchAllpassRight.Prepare(sampleRate);
    postPitchAllpassRight.Configure(pitchShiftAllpassDelay, 0.6f);

    // ---- Pitch latency compensation delay lines ----
    // Must be sized after the backend is prepared so GetLatencyMilliseconds() is valid.
    cachedPitchCompensationMs = wetInputPitchShifterLeft.GetLatencyMilliseconds();

    // Allocate for up to 300 ms (well above any granular lookback we'd ever use).
    const int maxCompSamples = std::max(
        1,
        static_cast<int>(std::ceil((300.0 * sampleRate) / 1000.0)));

    pitchCompDelayLeft = std::make_unique<DelayLine>(maxCompSamples);
    pitchCompDelayRight = std::make_unique<DelayLine>(maxCompSamples);

    // End Pitch Shift

    kBlendSlewCoeff = 1.0f / (0.01f * static_cast<float>(sampleRate));

    smoothedCenteredReadDelayMilliseconds = delayMilliseconds;
    readDelaySlewCoefficient = 1.0f / (0.02f * static_cast<float>(sampleRate));

    mainDelayLeft->Clear();
    mainDelayRight->Clear();
}

/*void NewDelayReverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples  = audioBuffer.getNumSamples();

    if (numChannels < 1 || numSamples <= 0)
        return;

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float inputLeft = leftData[sampleIndex];
        const float inputRight = (rightData != nullptr ? rightData[sampleIndex] : inputLeft);

        float outputLeft = inputLeft;
        float outputRight = inputRight;

        if (pitchEnabled >= 0.5f)
        {
            outputLeft = wetInputPitchShifterLeft.ProcessSample(inputLeft);
            outputRight = wetInputPitchShifterRight.ProcessSample(inputRight);

            ++echoWriteCounterL;
            if (echoWriteCounterL >= writePeriodSamples)
            {
                echoWriteCounterL = 0;
                wetInputPitchShifterLeft.OnNewEchoBoundary();

                const bool stereoEnabled = (pitchStereoEnabled01 >= 0.5f);

                if (stereoEnabled)
                {
                    ++echoWriteCounterR;
                    if (echoWriteCounterR >= writePeriodSamples)
                    {
                        echoWriteCounterR = 0;
                        wetInputPitchShifterRight.OnNewEchoBoundary();
                    }
                }
                else
                {
                    const float leftNewRatio = wetInputPitchShifterLeft.GetCurrentPitchRatio();
                    wetInputPitchShifterRight.OnNewEchoBoundaryMirrored(leftNewRatio);
                    echoWriteCounterR = 0;
                }
            }
        }

        if (!std::isfinite(outputLeft))
            outputLeft = 0.0f;

        if (!std::isfinite(outputRight))
            outputRight = 0.0f;

        leftData[sampleIndex] = juce::jlimit(-1.0f, 1.0f, outputLeft);

        if (rightData != nullptr)
            rightData[sampleIndex] = juce::jlimit(-1.0f, 1.0f, outputRight);
    }
}*/

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

    if (pitchSequenceRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildPitchSequences();

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
        const float preLeft = filteredDryLeft + lastFeedbackL;
        const float preRight = filteredDryRight + lastFeedbackR;

        // ---- 4: Pre-write diffusion (amount-controlled, dual-chain) ----
        //  0.0 .. 0.5 : only delayDiffusion chain (discrete tap blur)
        //  0.5 .. 1.0 : crossfade delayDiffusion -> reverbDiffusion (lush tail)
        float writeLeft = preLeft;
        float writeRight = preRight;

        // Equal-power blend between clean gainOne and gainTwo
        const float diffusionGainOne =
            std::cos(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);

        const float diffusionGainTwo =
            std::sin(diffusionAmountSmoothed * juce::MathConstants<float>::halfPi);

        if (diffusionAmountSmoothed > 0.001f)
        {
            float diffLeft;
            float diffRight;

            if (diffusionAmountSmoothed <= 0.5f)
            {
                // Lower half: delay-quality diffusion only
                diffLeft = delayDiffusionLeft->ProcessSample(preLeft);
                diffRight = delayDiffusionRight->ProcessSample(preRight);
            }
            else
            {
                // Upper half: crossfade between delay and reverb chains
                const float reverbBlend =
                    (diffusionAmountSmoothed - 0.5f) * 2.0f; // 0..1

                const float delayDiffLeft = delayDiffusionLeft->ProcessSample(preLeft);
                const float delayDiffRight = delayDiffusionRight->ProcessSample(preRight);

                const float reverbDiffLeft = reverbDiffusionLeft->ProcessSample(preLeft);
                const float reverbDiffRight = reverbDiffusionRight->ProcessSample(preRight);

                const float delayGain =
                    std::cos(reverbBlend * juce::MathConstants<float>::halfPi);

                const float reverbGain =
                    std::sin(reverbBlend * juce::MathConstants<float>::halfPi);

                diffLeft = delayDiffLeft  * delayGain + reverbDiffLeft  * reverbGain;
                diffRight = delayDiffRight * delayGain + reverbDiffRight * reverbGain;
            }

            writeLeft = (preLeft * diffusionGainOne) + (diffLeft  * diffusionGainTwo);
            writeRight = (preRight * diffusionGainOne) + (diffRight * diffusionGainTwo);
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
        const float dampedLeft  = dampingLeft->ProcessSample(wetLeft,  lowpass01);
        const float dampedRight = dampingRight->ProcessSample(wetRight, lowpass01);

        lastFeedbackL = dampedLeft  * feedbackGain;
        lastFeedbackR = dampedRight * feedbackGain;

        // ---- 8b: Pitch latency compensation ----
        // Delay the non-pitched wet path to match the granular lookback latency,
        // so both sides of the EqualPowerCrossfade below are time-aligned.
        pitchCompDelayLeft->PushSample(dampedLeft);
        pitchCompDelayRight->PushSample(dampedRight);

        const float compDampedLeft  = (cachedPitchCompensationMs > 0.0f)
            ? pitchCompDelayLeft->ReadDelayMilliseconds(cachedPitchCompensationMs, sampleRate)
            : dampedLeft;

        const float compDampedRight = (cachedPitchCompensationMs > 0.0f)
            ? pitchCompDelayRight->ReadDelayMilliseconds(cachedPitchCompensationMs, sampleRate)
            : dampedRight;

        // ---- 9: Optional pitch shift ----
        float pitchedLeft = wetLeft;
        float pitchedRight = wetRight;

        if (pitchWetMix > 0.0001f)
        {
            pitchedLeft = wetInputPitchShifterLeft.ProcessSample(wetLeft);
            pitchedRight = wetInputPitchShifterRight.ProcessSample(wetRight);

            // ---- 9b: Post-pitch allpass smoothing (only when pitch and diffusion are active) ----
            if (diffusionAmountSmoothed > 0.001f)
            {
                float diffPitchedLeft  = postPitchAllpassLeft.ProcessSample(pitchedLeft);
                float diffPitchedRight = postPitchAllpassRight.ProcessSample(pitchedRight);

                pitchedLeft = (pitchedLeft * diffusionGainOne) + (diffPitchedLeft * diffusionGainTwo);
                pitchedRight = (pitchedRight * diffusionGainOne) + (diffPitchedRight * diffusionGainTwo);
            }
        }

        // Advance echo boundary counters (needed regardless of pitch enable state)
        {
            const bool stereoEnabled = (pitchStereoEnabled01 >= 0.5f);

            ++echoWriteCounterL;
            if (echoWriteCounterL >= writePeriodSamples)
            {
                echoWriteCounterL = 0;
                wetInputPitchShifterLeft.OnNewEchoBoundary();

                if (!stereoEnabled)
                {
                    // Mirror left's new ratio to right channel so both channels
                    // cross-fade simultaneously — no extra stereo information added.
                    const float leftNewRatio = wetInputPitchShifterLeft.GetCurrentPitchRatio();
                    wetInputPitchShifterRight.OnNewEchoBoundaryMirrored(leftNewRatio);
                    echoWriteCounterR = 0; // keep counters in lockstep
                }
            }

            if (stereoEnabled)
            {
                ++echoWriteCounterR;
                if (echoWriteCounterR >= writePeriodSamples)
                {
                    echoWriteCounterR = 0;
                    wetInputPitchShifterRight.OnNewEchoBoundary();
                }
            }
        }

        pitchedLeft = PMath::EqualPowerCrossfade(compDampedLeft, pitchedLeft, pitchWetMix);
        pitchedRight = PMath::EqualPowerCrossfade(compDampedRight, pitchedRight, pitchWetMix);

        // ---- 10: Stereo spread ----
        float spreadWetLeft = pitchedLeft;
        float spreadWetRight = pitchedRight;

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

        // ---- 11: Dry/wet mix ----
        float outputLeft = (dryLeft * dryVolume) + (spreadWetLeft * wetVolume);
        float outputRight = (dryRight * dryVolume) + (spreadWetRight * wetVolume);

        // ---- 12: Optional post-filtering ----
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

//void NewDelayReverb::SetPitchEnabled(float pitchEnabled01)
//{
//    pitchEnabled = clamp01(pitchEnabled01);
//}

void NewDelayReverb::SetPitchRangeLower(float pitchRangeLowerSemitones)
{
    pitchRangeLower = juce::jlimit(-48.0f, 48.0f, pitchRangeLowerSemitones);
    pitchSequenceRebuildPending.store(true, std::memory_order_release);
}

void NewDelayReverb::SetPitchRangeUpper(float pitchRangeUpperSemitones)
{
    pitchRangeUpper = juce::jlimit(-48.0f, 48.0f, pitchRangeUpperSemitones);
    pitchSequenceRebuildPending.store(true, std::memory_order_release);
}

void NewDelayReverb::SetPitchMode(int modeIndex)
{
    pitchMode = juce::jlimit(0, 3, modeIndex);
    pitchSequenceRebuildPending.store(true, std::memory_order_release);
}

void NewDelayReverb::SetpitchWetMix(float newPitchWetMix)
{
    pitchWetMix = clamp01(newPitchWetMix);
}

void NewDelayReverb::SetPitchStereoEnabled(float enabled01)
{
    const float newValue = clamp01(enabled01);
    const bool oldStereoEnabled = (pitchStereoEnabled01 >= 0.5f);
    const bool newStereoEnabled = (newValue >= 0.5f);

    pitchStereoEnabled01 = newValue;

    if (oldStereoEnabled != newStereoEnabled)
    {
        echoWriteCounterL = 0;
        echoWriteCounterR = 0;

        wetInputPitchShifterLeft.ResetSequenceAndBackendToCurrentState();
        wetInputPitchShifterRight.ResetSequenceAndBackendToCurrentState();

        if (!newStereoEnabled)
        {
            const float leftRatio = wetInputPitchShifterLeft.GetCurrentPitchRatio();
            wetInputPitchShifterRight.OnNewEchoBoundaryMirrored(leftRatio);
        }
    }
}

void NewDelayReverb::SetPitchAlgorithm(OctaveEchoPitchShifter::BackendType backendType)
{
    wetInputPitchShifterLeft.SetBackendType(backendType);
    wetInputPitchShifterRight.SetBackendType(backendType);
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

float NewDelayReverb::GetPitchShifterLatencyMilliseconds() const
{
    if (pitchWetMix <= 0.0001f)
        return 0.0f;

    return wetInputPitchShifterLeft.GetLatencyMilliseconds();
}
// ---------------- Internal helpers ----------------

void NewDelayReverb::updateDelayMillisecondsFromNormalized()
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

        const float quarterNoteMs = 60000.0f / hostTempoBpm;
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

/*void NewDelayReverb::rebuildPitchSequences()
{
    auto configureShifter = [&](OctaveEchoPitchShifter& shifter)
    {
        auto constantSequence = std::make_unique<ConstantRatioSequence>();

        // Debug fixed-ratio test:
        // 2.0f  = +1 octave
        // 0.5f  = -1 octave
        // 1.0f  = unison
        constantSequence->SetPitchRatio(2.0f);

        shifter.SetSequence(std::move(constantSequence));
    };

    configureShifter(wetInputPitchShifterLeft);
    configureShifter(wetInputPitchShifterRight);
}*/

void NewDelayReverb::rebuildPitchSequences()
{
    int lowerOctave = semitonesToOctaveIndex(pitchRangeLower);
    int upperOctave = semitonesToOctaveIndex(pitchRangeUpper);

    if (lowerOctave > upperOctave)
        std::swap(lowerOctave, upperOctave);

    auto configureShifter = [&](OctaveEchoPitchShifter& shifter)
    {
        if (pitchMode == 3) // Up-Down
        {
            auto pingPongSequence = std::make_unique<PingPongOctaveSequence>();
            pingPongSequence->SetRange(lowerOctave, upperOctave);
            pingPongSequence->SetStartOctave(lowerOctave);
            pingPongSequence->SetInitialDirection(1);
            shifter.SetSequence(std::move(pingPongSequence));
        }
        else if (pitchMode == 2) // Random
        {
            auto randomSequence = std::make_unique<RandomOctaveSequence>();
            randomSequence->SetRange(lowerOctave, upperOctave);
            shifter.SetSequence(std::move(randomSequence));
        }
        else
        {
            auto progressiveSequence = std::make_unique<ProgressiveOctaveSequence>();
            progressiveSequence->SetRange(lowerOctave, upperOctave);

            if (pitchMode == 0) // Up
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

int NewDelayReverb::semitonesToOctaveIndex(float semitones)
{
    return static_cast<int>(std::round(semitones / 12.0f));
}