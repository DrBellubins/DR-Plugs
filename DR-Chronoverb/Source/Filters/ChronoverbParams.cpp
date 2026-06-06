#include "Chronoverb.h"

void Chronoverb::SetHostTempo(float bpm)
{
    DelayLeftRight->SetHostTempo(bpm);
    ReverbLeftRight->SetHostTempo(bpm);
    PitchShifterLeftRight->SetHostTempo(bpm);
}

void Chronoverb::SetDelayTime(float newDelayTimeNormalized)
{
    delayTimeNormalized = clamp01(newDelayTimeNormalized);

    DelayLeftRight->SetDelayTime(delayTimeNormalized);
    ReverbLeftRight->SetDelayTime(delayTimeNormalized);
    PitchShifterLeftRight->SetDelayTime(delayTimeNormalized);
}

void Chronoverb::SetDelayMode(int newDelayMode)
{
    delayMode = juce::jlimit(0, 3, newDelayMode);

    DelayLeftRight->SetDelayMode(delayMode);
    ReverbLeftRight->SetDelayMode(delayMode);
    PitchShifterLeftRight->SetDelayMode(delayMode);
}

void Chronoverb::SetFeedbackTime(float newFeedbackTimeSeconds)
{
    feedbackTimeSeconds = std::max(0.0f, newFeedbackTimeSeconds);

    DelayLeftRight->SetFeedbackTime(feedbackTimeSeconds);
    ReverbLeftRight->SetFeedbackTime(feedbackTimeSeconds);
}

void Chronoverb::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = clamp01(newAmount01);

    DelayLeftRight->SetDiffusionAmount(diffusionAmount);
    ReverbLeftRight->SetDiffusionAmount(diffusionAmount);
    PitchShifterLeftRight->SetDiffusionAmount(diffusionAmount);
}

void Chronoverb::SetDiffusionSize(float newSize01)
{
    diffusionSize = clamp01(newSize01);

    DelayLeftRight->SetDiffusionSize(diffusionSize);
    ReverbLeftRight->SetDiffusionSize(diffusionSize);
    PitchShifterLeftRight->SetDiffusionSize(diffusionSize);
}

void Chronoverb::SetDiffusionQuality(int newQualityStages)
{
    diffusionQualityStages = clampInt(newQualityStages, 1, 8);

    DBG("Quality: " << diffusionQualityStages);

    DelayLeftRight->SetDiffusionQuality(diffusionQualityStages);
    ReverbLeftRight->SetDiffusionQuality(diffusionQualityStages);
    PitchShifterLeftRight->SetDiffusionQuality(diffusionQualityStages);
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

    PitchShifterLeftRight->SetPitchRangeLower(pitchRangeLower);
}

void Chronoverb::SetPitchRangeUpper(float pitchRangeUpperSemitones)
{
    pitchRangeUpper = juce::jlimit(-48.0f, 48.0f, pitchRangeUpperSemitones);

    PitchShifterLeftRight->SetPitchRangeUpper(pitchRangeUpper);
}

void Chronoverb::SetPitchSequence(int sequenceIndex)
{
    pitchSequence = juce::jlimit(0, 3, sequenceIndex);

    PitchShifterLeftRight->SetPitchSequence(pitchSequence);
}

void Chronoverb::SetpitchWetMix(float newPitchWetMix)
{
    pitchWetMix = clamp01(newPitchWetMix);

    PitchShifterLeftRight->SetPitchWetMix(pitchWetMix);
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