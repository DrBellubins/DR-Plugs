#include "NewDelayReverb.h"

#include <cmath>
#include <algorithm>
#include <memory>

void NewDelayReverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

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
    delayLineLeft = std::make_unique<DelayLine>(maxDelaySamples);
    delayLineRight = std::make_unique<DelayLine>(maxDelaySamples);

    delayLineLeft->Clear();
    delayLineRight->Clear();

    delayLineLeft->SetSampleRate(newSampleRate);
    delayLineRight->SetSampleRate(newSampleRate);

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
        delayDiffusionReadLeft->UpdateSize(diffusionSize01);
        delayDiffusionReadRight->UpdateSize(diffusionSize01);

        delayDiffusionWriteLeft->UpdateSize(diffusionSize01);
        delayDiffusionWriteRight->UpdateSize(diffusionSize01);

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
            float diffLeft = 0.0f;
            float diffRight = 0.0f;

            if (diffusionAmountSmoothed <= 0.5f)
            {
                // Lower half: delay-quality diffusion only
                diffLeft = delayDiffusionWriteLeft->ProcessSample(preLeft);
                diffRight = delayDiffusionWriteRight->ProcessSample(preRight);
            }
            else
            {
                // Upper half: crossfade between delay and reverb chains
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

                diffLeft = delayDiffLeft * delayGain + reverbDiffLeft  * reverbGain;
                diffRight = delayDiffRight * delayGain + reverbDiffRight * reverbGain;
            }

            writeLeft = (preLeft * diffusionGainOne) + (diffLeft  * diffusionGainTwo);
            writeRight = (preRight * diffusionGainOne) + (diffRight * diffusionGainTwo);
        }

        // ---- 5: Write to delay line ----
        delayLineLeft->PushSample(writeLeft);
        delayLineRight->PushSample(writeRight);

        // ---- 6: Read nominal tap and early tap ----
        smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
            (delayMilliseconds - smoothedCenteredReadDelayMilliseconds);

        const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;

        const float earlyReadMilliseconds =
            std::max(1.0f, delayMilliseconds - staticDiffusionCompensationMilliseconds);

        const float nominalWetLeft  =
            delayLineLeft->ReadFeedbackBuffer(nominalReadMilliseconds);

        const float nominalWetRight =
            delayLineRight->ReadFeedbackBuffer(nominalReadMilliseconds);

        const float earlyWetLeft  =
            delayLineLeft->ReadFeedbackBuffer(earlyReadMilliseconds);

        const float earlyWetRight =
            delayLineRight->ReadFeedbackBuffer(earlyReadMilliseconds);

        // ---- 7: Diffuse the early tap (second pass through delay chain) ----
        const float diffusedEarlyLeft = delayDiffusionReadLeft->ProcessSample(earlyWetLeft);
        const float diffusedEarlyRight = delayDiffusionReadRight->ProcessSample(earlyWetRight);

        // Diffusion amount < 0.5 - Fade between clean, and diffused delay tap

        // Remap amount so clean tap suppression is aggressive (gone by amount=0.5)
        const float diffusionDrive =
            juce::jlimit(0.0f, 1.0f, diffusionAmountSmoothed * 2.0f);

        const float cleanTapGain =
            std::pow(1.0f - diffusionDrive, 4.0f); // collapses to 0 at drive >= 1

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

        const float preReadWetLeft  = delayLineLeft->ReadFeedbackBuffer(preReadMs);
        const float preReadWetRight = delayLineRight->ReadFeedbackBuffer(preReadMs);

        // ---- 9: Pitch shift ----
        float pitchedLeft = dampedLeft;
        float pitchedRight = dampedRight;

        if (pitchWetMix > 0.0001f)
        {
            pitchedLeft = pitchShifterLeft.ProcessSample(preReadWetLeft);
            pitchedRight = pitchShifterRight.ProcessSample(preReadWetRight);

            float diffPitchedLeft = pitchDiffusionLeft->ProcessSample(pitchedLeft);
            float diffPitchedRight = pitchDiffusionRight->ProcessSample(pitchedRight);

            pitchedLeft = PMath::EqualPowerCrossfade(pitchedLeft,
                diffPitchedLeft, diffusionAmountSmoothed);

            pitchedRight = PMath::EqualPowerCrossfade(pitchedRight,
                diffPitchedRight, diffusionAmountSmoothed);
        }

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
