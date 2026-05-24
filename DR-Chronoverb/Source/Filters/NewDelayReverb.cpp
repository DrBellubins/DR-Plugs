#include "NewDelayReverb.h"

#include <cmath>
#include <algorithm>
#include <memory>

NewDelayReverb::NewDelayReverb() = default;
NewDelayReverb::~NewDelayReverb() = default;

void NewDelayReverb::SetHostTempo(float bpm)
{
    if (bpm > 0.0f)
    {
        hostTempoBpm = bpm;

        if (delayMode != 0)
            updateDelayMillisecondsFromNormalized();
    }
}

void NewDelayReverb::PrepareToPlay(double newSampleRate, float initialHostTempoBpm)
{
    sampleRate = newSampleRate;
    hostTempoBpm = initialHostTempoBpm;

    // Prepare IIR filters
    juce::dsp::ProcessSpec filterSpec {};
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
    mainDelayLeft = std::make_unique<DelayLine>(maxDelaySamples);
    mainDelayRight = std::make_unique<DelayLine>(maxDelaySamples);

    // Delay-quality diffusion chains

    // Read
    delayDiffusionReadLeft = std::make_unique<DiffusionChain>();
    delayDiffusionReadRight = std::make_unique<DiffusionChain>();

    delayDiffusionReadLeft->Prepare(sampleRate);
    delayDiffusionReadRight->Prepare(sampleRate);

    if (delayDiffusionReadLeft) delayDiffusionReadLeft->ClearState();
    if (delayDiffusionReadRight) delayDiffusionReadRight->ClearState();

    // Write
    delayDiffusionWriteLeft = std::make_unique<DiffusionChain>();
    delayDiffusionWriteRight = std::make_unique<DiffusionChain>();

    delayDiffusionWriteLeft->Prepare(sampleRate);
    delayDiffusionWriteRight->Prepare(sampleRate);

    if (delayDiffusionWriteLeft) delayDiffusionWriteLeft->ClearState();
    if (delayDiffusionWriteRight) delayDiffusionWriteRight->ClearState();

    // Reverb-quality diffusion chains
    reverbDiffusionLeft = std::make_unique<DiffusionChain>();
    reverbDiffusionRight = std::make_unique<DiffusionChain>();
    reverbDiffusionLeft->Prepare(sampleRate);
    reverbDiffusionRight->Prepare(sampleRate);

    // Force full rebuild
    lastBuiltQualityStages = -1;
    lastBuiltSize01 = -1.0f;
    rebuildDiffusionIfNeeded();

    if (reverbDiffusionLeft) reverbDiffusionLeft->ClearState();
    if (reverbDiffusionRight) reverbDiffusionRight->ClearState();

    smoothedDelayReverbDiffBlend = 0.0f;
    diffusionRebuildPending.store(false, std::memory_order_release);

    // Damping filters
    dampingLeft = std::make_unique<DampingFilter>();
    dampingRight = std::make_unique<DampingFilter>();
    dampingLeft->Prepare(sampleRate);
    dampingRight->Prepare(sampleRate);

    updateDelayMillisecondsFromNormalized();
    updateFeedbackGainFromFeedbackTime();

    // Start Pitch Shift
    pitchShifterLeft.Prepare(sampleRate, 512);
    pitchShifterRight.Prepare(sampleRate, 512);
    pitchShifterLeft.SetEnabled(true);
    pitchShifterRight.SetEnabled(true);

    rebuildPitchSequences();

    pitchShifterLeft.CommitPendingSequenceNow();
    pitchShifterRight.CommitPendingSequenceNow();

    echoWriteCounterL = 0;
    echoWriteCounterR = 0;

    lastFeedbackL = 0.0f;
    lastFeedbackR = 0.0f;

    pitchDiffusionLeft = std::make_unique<DiffusionChain>();
    pitchDiffusionRight = std::make_unique<DiffusionChain>();

    pitchDiffusionLeft->Prepare(sampleRate);
    pitchDiffusionRight->Prepare(sampleRate);

    lastPitchDiffFeedbackL = 0.0f;
    lastPitchDiffFeedbackR = 0.0f;

    // End Pitch Shift

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

    if (pitchSequenceRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildPitchSequences();

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    pitchShifterLatencyMs = pitchShifterLeft.GetLatencyMilliseconds();

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        delayDiffusionReadLeft->UpdateSize(diffusionAmount01);
        delayDiffusionReadRight->UpdateSize(diffusionAmount01);

        delayDiffusionWriteLeft->UpdateSize(diffusionAmount01);
        delayDiffusionWriteRight->UpdateSize(diffusionAmount01);

        reverbDiffusionLeft->UpdateSize(diffusionSize01);
        reverbDiffusionRight->UpdateSize(diffusionSize01);

        //pitchDiffusionLeft->UpdateSize(diffusionSize01);
        //pitchDiffusionRight->UpdateSize(diffusionSize01);

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
            const float reverbBlend =
                    (diffusionAmountSmoothed - 0.5f) * 2.0f; // 0..1

            const float delayDiffLeft = delayDiffusionWriteLeft->ProcessSample(preLeft);
            const float delayDiffRight = delayDiffusionWriteRight->ProcessSample(preRight);

            const float reverbDiffLeft = reverbDiffusionLeft->ProcessSample(preLeft);
            const float reverbDiffRight = reverbDiffusionRight->ProcessSample(preRight);

            const float delayGain =
                std::cos(reverbBlend * juce::MathConstants<float>::halfPi);

            const float reverbGain =
                std::sin(reverbBlend * juce::MathConstants<float>::halfPi);

            float diffLeft = delayDiffLeft * delayGain + reverbDiffLeft  * reverbGain;
            float diffRight = delayDiffRight * delayGain + reverbDiffRight * reverbGain;

            writeLeft = (preLeft * diffusionGainOne) + (diffLeft * diffusionGainTwo);
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
        const float diffusedEarlyLeft = delayDiffusionReadLeft->ProcessSample(nominalWetLeft);
        const float diffusedEarlyRight = delayDiffusionReadRight->ProcessSample(nominalWetRight);

        // Remap amount so clean tap suppression is aggressive (gone by amount=0.5)
        const float diffusionDrive =
            juce::jlimit(0.0f, 1.0f, diffusionAmountSmoothed * 2.0f);

        const float cleanTapGain = std::pow(1.0f - diffusionDrive, 4.0f);

        const float diffusedTapGain =
            std::sin(diffusionDrive * juce::MathConstants<float>::halfPi);

        float wetLeft = nominalWetLeft * cleanTapGain + diffusedEarlyLeft * diffusedTapGain;
        float wetRight = nominalWetRight * cleanTapGain + diffusedEarlyRight * diffusedTapGain;

        // ---- 8: Damping + feedback recirculation ----
        const float dampedLeft = dampingLeft->ProcessSample(wetLeft, lowpass01);
        const float dampedRight = dampingRight->ProcessSample(wetRight, lowpass01);

        lastFeedbackL = dampedLeft * feedbackGain;
        lastFeedbackR = dampedRight * feedbackGain;

        // ---- 8b: Pre-read tap for pitch shifting (reads earlier so output lands on time) ----
        const float preReadMs = std::max(1.0f, nominalReadMilliseconds - pitchShifterLatencyMs);

        const float preReadWetLeft  = mainDelayLeft->ReadDelayMilliseconds(preReadMs, sampleRate);
        const float preReadWetRight = mainDelayRight->ReadDelayMilliseconds(preReadMs, sampleRate);

        // ---- 9: Pitch shift ----
        float pitchedLeft = pitchShifterLeft.ProcessSample(preReadWetLeft);
        float pitchedRight = pitchShifterRight.ProcessSample(preReadWetRight);

        float diffPitchedLeft = pitchDiffusionLeft->ProcessSample(pitchedLeft);
        float diffPitchedRight = pitchDiffusionRight->ProcessSample(pitchedRight);

        pitchedLeft = PMath::EqualPowerCrossfade(pitchedLeft, diffPitchedLeft, diffusionAmountSmoothed);
        pitchedRight = PMath::EqualPowerCrossfade(pitchedRight, diffPitchedRight, diffusionAmountSmoothed);

        /*if (pitchWetMix > 0.0001f)
        {
            pitchedLeft = pitchShifterLeft.ProcessSample(preReadWetLeft);
            pitchedRight = pitchShifterRight.ProcessSample(preReadWetRight);

            const float pitchDiffInputL = pitchedLeft + lastPitchDiffFeedbackL;
            const float pitchDiffInputR = pitchedRight + lastPitchDiffFeedbackR;

            const float diffPitchedLeft = pitchDiffusionLeft->ProcessSample(pitchDiffInputL);
            const float diffPitchedRight = pitchDiffusionRight->ProcessSample(pitchDiffInputR);

            lastPitchDiffFeedbackL = diffPitchedLeft * pitchDiffFeedbackGain;
            lastPitchDiffFeedbackR = diffPitchedRight * pitchDiffFeedbackGain;

            //pitchedLeft = (pitchedLeft * diffusionGainOne) + (diffPitchedLeft * diffusionGainTwo);
            //pitchedRight = (pitchedRight * diffusionGainOne) + (diffPitchedRight * diffusionGainTwo);

            pitchedLeft  = diffPitchedLeft;
            pitchedRight = diffPitchedRight;
        }*/

        // Advance echo boundary counters (needed regardless of pitch enable state)
        {
            const bool stereoEnabled = (pitchStereoEnabled01 >= 0.5f);

            ++echoWriteCounterL;
            if (echoWriteCounterL >= writePeriodSamples)
            {
                echoWriteCounterL = 0;
                pitchShifterLeft.OnNewEchoBoundary();

                if (!stereoEnabled)
                {
                    // Mirror left's new ratio to right channel so both channels
                    // cross-fade simultaneously — no extra stereo information added.
                    const float leftNewRatio = pitchShifterLeft.GetCurrentPitchRatio();
                    pitchShifterRight.OnNewEchoBoundaryMirrored(leftNewRatio);
                    echoWriteCounterR = 0; // keep counters in lockstep
                }
            }

            if (stereoEnabled)
            {
                ++echoWriteCounterR;
                if (echoWriteCounterR >= writePeriodSamples)
                {
                    echoWriteCounterR = 0;
                    pitchShifterRight.OnNewEchoBoundary();
                }
            }
        }

        pitchedLeft = PMath::EqualPowerCrossfade(dampedLeft, pitchedLeft, pitchWetMix);
        pitchedRight = PMath::EqualPowerCrossfade(dampedRight, pitchedRight, pitchWetMix);

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
}

void NewDelayReverb::SetHPLPPrePost(float prePost01)
{
    hplpPrePost01 = clamp01(prePost01);
}

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

void NewDelayReverb::SetPitchSequence(int sequenceIndex)
{
    pitchMode = juce::jlimit(0, 3, sequenceIndex);
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

        pitchShifterLeft.ResetSequenceAndBackendToCurrentState();
        pitchShifterRight.ResetSequenceAndBackendToCurrentState();

        if (!newStereoEnabled)
        {
            const float leftRatio = pitchShifterLeft.GetCurrentPitchRatio();
            pitchShifterRight.OnNewEchoBoundaryMirrored(leftRatio);
        }
    }
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

    // Delay
    if (delayDiffusionReadLeft != nullptr)
    {
        delayDiffusionReadLeft->Configure(diffusionQualityStages,
            diffusionSize01, 0.005f, 0.5f, DelayTunings);
    }

    if (delayDiffusionReadRight != nullptr)
    {
        delayDiffusionReadRight->Configure(diffusionQualityStages,
            diffusionSize01, 0.005f, 0.5f, DelayTunings);
    }

    if (delayDiffusionWriteLeft != nullptr)
    {
        delayDiffusionWriteLeft->Configure(diffusionQualityStages,
            diffusionSize01, 0.005f, 0.5f, DelayTunings);
    }

    if (delayDiffusionWriteRight != nullptr)
    {
        delayDiffusionWriteRight->Configure(diffusionQualityStages,
            diffusionSize01, 0.005f, 0.5f, DelayTunings);
    }

    // Reverb
    if (reverbDiffusionLeft  != nullptr)
    {
        reverbDiffusionLeft->Configure(diffusionQualityStages,
            diffusionSize01, 0.003f, 0.3f, ReverbTunings);
    }

    if (reverbDiffusionRight != nullptr)
    {
        reverbDiffusionRight->Configure(diffusionQualityStages,
            diffusionSize01, 0.003f, 0.3f, ReverbTunings);
    }

    // Pitch shifting
    // Always configure pitch diffusion at full size — smearing is independent of the size knob
    if (pitchDiffusionLeft != nullptr)
    {
        pitchDiffusionLeft->Configure(diffusionQualityStages,
            diffusionSize01, 0.001f, 0.1f, ReverbTunings);
    }

    if (pitchDiffusionRight != nullptr)
    {
        pitchDiffusionRight->Configure(diffusionQualityStages,
            diffusionSize01, 0.001f, 0.1f, ReverbTunings);
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

void NewDelayReverb::updateFeedbackGainFromFeedbackTime()
{
    const float normalized = juce::jlimit(0.0f, 1.0f, feedbackTimeSeconds / 10.0f);
    const float curved = std::sqrt(normalized);
    feedbackGain = std::max(0.0f, std::min(0.85f * curved, 0.95f));
}

void NewDelayReverb::updateFilters() const
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

    configureShifter(pitchShifterLeft);
    configureShifter(pitchShifterRight);
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