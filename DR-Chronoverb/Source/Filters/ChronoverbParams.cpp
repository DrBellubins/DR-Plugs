#include "Chronoverb.h"

void Chronoverb::SetHostTempo(float bpm) const
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

// Pitch
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

// Distortion
void Chronoverb::SetDistortionModuleEnabled(int moduleIndex, bool enabled)
{
    const int index = juce::jlimit(0, NumDistortionModules - 1, moduleIndex);
    DistortionLeftRight->SetEnabled(index, enabled);

    //DBG("Dist mod enabled: " << moduleIndex << ", " << static_cast<int>(enabled));
}

void Chronoverb::SetDistortionModuleType(int moduleIndex, int type)
{
    const int index = juce::jlimit(0, NumDistortionModules - 1, moduleIndex);
    DistortionLeftRight->SetType(index, type);

    //DBG("Dist mod type/target: " << moduleIndex << ", " << type << ", " << target);
}

void Chronoverb::SetDistortionModuleTarget(int moduleIndex, int target)
{
    const int index = juce::jlimit(0, NumDistortionModules - 1, moduleIndex);
    DistortionLeftRight->SetTarget(index, target);

    //DBG("Dist mod type/target: " << moduleIndex << ", " << type << ", " << target);
}


void Chronoverb::SetDistortionModuleDrive(int moduleIndex, float drive01)
{
    const int index = juce::jlimit(0, NumDistortionModules - 1, moduleIndex);
    DistortionLeftRight->SetDrive(index, drive01);

    //DBG("Dist mod drive: " << moduleIndex << ", " << drive01);
}

void Chronoverb::SetDistortionModuleMix(int moduleIndex, float mix01)
{
    const int index = juce::jlimit(0, NumDistortionModules - 1, moduleIndex);
    DistortionLeftRight->SetMix(index, mix01);

    //DBG("Dist mod mix: " << moduleIndex << ", " << mix01);
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

void Chronoverb::SetDuckAmount(float newDuckAmount)
{
    duckAmount = clamp01(newDuckAmount);
}