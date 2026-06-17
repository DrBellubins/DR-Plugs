#include "Chronoverb.h"

void Chronoverb::SetHostTempo(float bpm) const
{
    DeverbLeftRight->SetHostTempo(bpm);
    //DelayLeftRight->SetHostTempo(bpm);
    //ReverbLeftRight->SetHostTempo(bpm);
    PitchShifterLeftRight->SetHostTempo(bpm);
    StereoLeftRight->SetHostTempo(bpm);
}

void Chronoverb::SetDelayTime(float newDelayTime)
{
    delayMilliseconds = newDelayTime;

    DeverbLeftRight->SetDelayTime(delayMilliseconds);
    //DelayLeftRight->SetDelayTime(delayMilliseconds);
    //ReverbLeftRight->SetDelayTime(delayMilliseconds);
    PitchShifterLeftRight->SetDelayTime(delayMilliseconds);
    StereoLeftRight->SetDelayTime(delayMilliseconds);
}

void Chronoverb::SetDelayMode(int newDelayMode)
{
    delayMode = std::clamp(newDelayMode, 0, 3);

    DeverbLeftRight->SetDelayMode(delayMode);
    //DelayLeftRight->SetDelayMode(delayMode);
    //ReverbLeftRight->SetDelayMode(delayMode);
    PitchShifterLeftRight->SetDelayMode(delayMode);
    StereoLeftRight->SetDelayMode(delayMode);
}

void Chronoverb::SetFeedbackTime(float newFeedbackTimeSeconds)
{
    feedbackTimeSeconds = std::max(0.0f, newFeedbackTimeSeconds);

    DeverbLeftRight->SetFeedbackTime(feedbackTimeSeconds);
    //DelayLeftRight->SetFeedbackTime(feedbackTimeSeconds);
    //ReverbLeftRight->SetFeedbackTime(feedbackTimeSeconds);
}

void Chronoverb::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = clamp01(newAmount01);

    DeverbLeftRight->SetDiffusionAmount(diffusionAmount);
    //DelayLeftRight->SetDiffusionAmount(diffusionAmount);
    //ReverbLeftRight->SetDiffusionAmount(diffusionAmount);
    PitchShifterLeftRight->SetDiffusionAmount(diffusionAmount);
    StereoLeftRight->SetDiffusionAmount(diffusionAmount);
}

void Chronoverb::SetDiffusionSize(float newSize01)
{
    diffusionSize = clamp01(newSize01);

    DeverbLeftRight->SetDiffusionSize(diffusionSize);
    //DelayLeftRight->SetDiffusionSize(diffusionSize);
    //ReverbLeftRight->SetDiffusionSize(diffusionSize);
    PitchShifterLeftRight->SetDiffusionSize(diffusionSize);
}

void Chronoverb::SetDiffusionQuality(int newQualityStages)
{
    diffusionQualityStages = clampInt(newQualityStages, 1, 8);

    DeverbLeftRight->SetDiffusionQuality(diffusionQualityStages);
    //DelayLeftRight->SetDiffusionQuality(diffusionQualityStages);
    //ReverbLeftRight->SetDiffusionQuality(diffusionQualityStages);
    PitchShifterLeftRight->SetDiffusionQuality(diffusionQualityStages);
}

void Chronoverb::SetDryVolume(float newDry01)
{
    dryVolume = clamp01(newDry01);
}

void Chronoverb::SetWetVolume(float newWet01)
{
    wetVolume = clamp01(newWet01);
}

void Chronoverb::SetStereoSpread(float newSpreadMinus1To1)
{
    stereoSpread = std::clamp(newSpreadMinus1To1, -1.0f, 1.0f);
    StereoLeftRight->SetStereoSpread(stereoSpread);
}

// Pitch
void Chronoverb::SetPitchRangeLower(float pitchRangeLowerSemitones)
{
    pitchRangeLower = std::clamp(pitchRangeLowerSemitones, -48.0f, 48.0f);
    PitchShifterLeftRight->SetPitchRangeLower(pitchRangeLower);
}

void Chronoverb::SetPitchRangeUpper(float pitchRangeUpperSemitones)
{
    pitchRangeUpper = std::clamp(pitchRangeUpperSemitones, -48.0f, 48.0f);
    PitchShifterLeftRight->SetPitchRangeUpper(pitchRangeUpper);
}

void Chronoverb::SetPitchSequence(int sequenceIndex)
{
    pitchSequence = std::clamp(sequenceIndex, 0, 3);
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
    const int index = std::clamp(moduleIndex, 0, NumDistortionModules - 1);
    DistortionLeftRight->SetEnabled(index, enabled);

    //DBG("Dist mod enabled: " << moduleIndex << ", " << static_cast<int>(enabled));
}

void Chronoverb::SetDistortionModuleType(int moduleIndex, int type)
{
    const int index = std::clamp(moduleIndex, 0, NumDistortionModules - 1);
    DistortionLeftRight->SetType(index, type);

    //DBG("Dist mod type/target: " << moduleIndex << ", " << type << ", " << target);
}

void Chronoverb::SetDistortionModuleTarget(int moduleIndex, int target)
{
    const int index = std::clamp(moduleIndex, 0, NumDistortionModules - 1);
    DistortionLeftRight->SetTarget(index, target);

    //DBG("Dist mod type/target: " << moduleIndex << ", " << type << ", " << target);
}


void Chronoverb::SetDistortionModuleDrive(int moduleIndex, float drive01)
{
    const int index = std::clamp(moduleIndex, 0, NumDistortionModules - 1);
    DistortionLeftRight->SetDrive(index, drive01);

    //DBG("Dist mod drive: " << moduleIndex << ", " << drive01);
}

void Chronoverb::SetDistortionModuleMix(int moduleIndex, float mix01)
{
    const int index = std::clamp(moduleIndex, 0, NumDistortionModules - 1);
    DistortionLeftRight->SetMix(index, mix01);

    //DBG("Dist mod mix: " << moduleIndex << ", " << mix01);
}

// Ducking
void Chronoverb::SetDuckAmount(float newDuckAmount)
{
    duckAmount = newDuckAmount;
    DuckingLeftRight->SetDuckAmount(duckAmount);
}

void Chronoverb::SetDuckAttack(float newDuckAttack)
{
    duckAttack = newDuckAttack;
    DuckingLeftRight->SetDuckAttack(duckAttack);
}

void Chronoverb::SetDuckRelease(float newDuckRelease)
{
    duckRelease = newDuckRelease;
    DuckingLeftRight->SetDuckRelease(duckRelease);
}

// Filters
void Chronoverb::SetFiltersOrder(int newOrder)
{
    filtersOrder = std::clamp(newOrder, 0, 2);
    //DeverbLeftRight->SetFiltersOrder(filtersOrder);
    //DelayLeftRight->SetFiltersOrder(filtersOrder);
}

void Chronoverb::SetLowPassCutoff(float newLowpass)
{
    lowpassCutoff = newLowpass;
    FilterLeftRight->SetLowPassCutoff(lowpassCutoff);
}

void Chronoverb::SetHighPassCutoff(float newHighpass)
{
    highpassCutoff = newHighpass;
    FilterLeftRight->SetHighPassCutoff(highpassCutoff);
}

// TODO

/*void Chronoverb::SetHPLPPrePost(float prePost01)
{
    hplpPrePost01 = clamp01(prePost01);
}*/