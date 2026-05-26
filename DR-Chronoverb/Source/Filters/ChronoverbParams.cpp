#include "Chronoverb.h"

void Chronoverb::SetHostTempo(float bpm)
{
    DelayLeft->SetHostTempo(bpm);
    DelayRight->SetHostTempo(bpm);

    ReverbLeftRight->SetHostTempo(bpm);
}

void Chronoverb::SetDelayTime(float newDelayTimeNormalized)
{
    delayTimeNormalized = clamp01(newDelayTimeNormalized);

    DelayLeft->SetDelayTime(delayTimeNormalized);
    DelayRight->SetDelayTime(delayTimeNormalized);

    ReverbLeftRight->SetDelayTime(delayTimeNormalized);
}

void Chronoverb::SetDelayMode(int newDelayMode)
{
    delayMode = juce::jlimit(0, 3, newDelayMode);

    DelayLeft->SetDelayMode(delayMode);
    DelayRight->SetDelayMode(delayMode);

    ReverbLeftRight->SetDelayMode(delayMode);
}

void Chronoverb::SetFeedbackTime(float newFeedbackTimeSeconds)
{
    feedbackTimeSeconds = std::max(0.0f, newFeedbackTimeSeconds);

    DelayLeft->SetFeedbackTime(feedbackTimeSeconds);
    DelayRight->SetFeedbackTime(feedbackTimeSeconds);

    ReverbLeftRight->SetFeedbackTime(feedbackTimeSeconds);
}

void Chronoverb::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = clamp01(newAmount01);

    DelayLeft->SetDiffusionAmount(diffusionAmount);
    DelayRight->SetDiffusionAmount(diffusionAmount);

    ReverbLeftRight->SetDiffusionAmount(diffusionAmount);
}

void Chronoverb::SetDiffusionSize(float newSize01)
{
    diffusionSize = clamp01(newSize01);

    DelayLeft->SetDiffusionSize(diffusionSize);
    DelayRight->SetDiffusionSize(diffusionSize);

    ReverbLeftRight->SetDiffusionSize(diffusionSize);
}

void Chronoverb::SetDiffusionQuality(int newQualityStages)
{
    diffusionQualityStages = clampInt(newQualityStages, 1, 8);

    DelayLeft->SetDiffusionQuality(diffusionQualityStages);
    DelayRight->SetDiffusionQuality(diffusionQualityStages);

    ReverbLeftRight->SetDiffusionQuality(diffusionQualityStages);
}

void Chronoverb::SetLowpassCutoff(float newLowpass01)
{
    lowpassCutoff = clamp01(newLowpass01);

    //DelayLeft->SetLowpassCutoff(lowpassCutoff);
    //DelayRight->SetLowpassCutoff(lowpassCutoff);
}

void Chronoverb::SetHighpassCutoff(float newHighpass01)
{
    highpassCutoff = clamp01(newHighpass01);

    //DelayLeft->SetHighpassCutoff(highpassCutoff);
    //DelayRight->SetHighpassCutoff(highpassCutoff);
}

void Chronoverb::SetDryVolume(float newDry01)
{
    dryVolume = clamp01(newDry01);
}

void Chronoverb::SetWetVolume(float newWet01)
{
    wetVolume = clamp01(newWet01);
}

// TODO

void Chronoverb::SetStereoSpread(float newSpreadMinus1To1)
{
    stereoSpread = juce::jlimit(-1.0f, 1.0f, newSpreadMinus1To1);
}

void Chronoverb::SetHPLPPrePost(float prePost01)
{
    hplpPrePost01 = clamp01(prePost01);
}

void Chronoverb::SetPitchRangeLower(float pitchRangeLowerSemitones)
{
    pitchRangeLower = juce::jlimit(-48.0f, 48.0f, pitchRangeLowerSemitones);

    PitchShifterLeft->SetPitchRangeLower(pitchRangeLower);
    PitchShifterRight->SetPitchRangeLower(pitchRangeLower);
}

void Chronoverb::SetPitchRangeUpper(float pitchRangeUpperSemitones)
{
    pitchRangeUpper = juce::jlimit(-48.0f, 48.0f, pitchRangeUpperSemitones);

    PitchShifterLeft->SetPitchRangeUpper(pitchRangeUpper);
    PitchShifterRight->SetPitchRangeUpper(pitchRangeUpper);
}

void Chronoverb::SetPitchSequence(int sequenceIndex)
{
    pitchMode = juce::jlimit(0, 3, sequenceIndex);

    PitchShifterLeft->SetPitchSequence(pitchMode);
    PitchShifterRight->SetPitchSequence(pitchMode);
}

void Chronoverb::SetpitchWetMix(float newPitchWetMix)
{
    pitchWetMix = clamp01(newPitchWetMix);

    PitchShifterLeft->SetPitchWetMix(pitchWetMix);
    PitchShifterRight->SetPitchWetMix(pitchWetMix);
}

// Pitch shifting

void Chronoverb::SetPitchStereoEnabled(float enabled01)
{
    /*const float newValue = clamp01(enabled01);
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
    }*/
}