#include "NewDelayReverb.h"

#include <cmath>
#include <algorithm>
#include <utility>

void NewDelayReverb::SetHostTempo(float bpm)
{
    if (bpm > 0.0f)
    {
        hostTempoBpm = bpm;

        if (delayMode != 0)
            updateDelayMillisecondsFromNormalized();
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