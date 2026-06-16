#pragma once


#include <memory>

#include "NewDelayReverb/Stages/Delay.h"
#include "NewDelayReverb/Stages/Reverb.h"
#include "NewDelayReverb/Stages/PitchShifter.h"
#include "NewDelayReverb/Stages/Distortion.h"
#include "NewDelayReverb/Stages/Ducking.h"
#include "NewDelayReverb/Stages/Stereo.h"
#include "NewDelayReverb/Stages/Filters.h"

class DelayLine;
class DampingFilter;
class DiffusionChain;

class Chronoverb
{
public:
    Chronoverb();

    void PrepareToPlay(double sampleRate);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    std::unique_ptr<Delay> DelayLeftRight;
    std::unique_ptr<Reverb> ReverbLeftRight;
    std::unique_ptr<PitchShifter> PitchShifterLeftRight;
    std::unique_ptr<Distortion> DistortionLeftRight;
    std::unique_ptr<Stereo> StereoLeftRight;
    std::unique_ptr<Ducking> DuckingLeftRight;
    std::unique_ptr<Filters> FilterLeftRight;

    //region Parameter Sets
    void SetHostTempo(float bpm) const;
    void SetDelayTime(float newDelayTime);      // 0..1 -> 0..1000 ms
    void SetDelayMode(int newDelayMode);

    void SetFeedbackTime(float newFeedbackTimeSeconds);   // 0..10 s
    void SetDiffusionAmount(float newAmount01);
    void SetDiffusionSize(float newSize01);
    void SetDiffusionQuality(int newQualityStages);       // 1..8

    void SetDryVolume(float newDryVolume);
    void SetWetVolume(float newWetVolume);

    void SetStereoSpread(float newSpreadMinus1To1);       // -1..1

    // Pitch
    void SetPitchRate(float newPitchRate);
    void SetPitchRangeLower(float pitchRangeLowerSemitones);
    void SetPitchRangeUpper(float pitchRangeUpperSemitones);
    void SetPitchSequence(int sequenceIndex);                // 0=Up, 1=Down, 2=Random
    void SetpitchWetMix(float wetVolume);

    // Distortion
    void SetDistortionModuleEnabled(int moduleIndex, bool enabled);
    void SetDistortionModuleType(int moduleIndex, int type);
    void SetDistortionModuleTarget(int moduleIndex, int target);
    void SetDistortionModuleDrive(int moduleIndex, float drive01);
    void SetDistortionModuleMix(int moduleIndex, float mix01);

    // Ducking
    void SetDuckAmount(float newDuckAmount);
    void SetDuckAttack(float newDuckAttack);
    void SetDuckRelease(float newDuckRelease);

    // Filters
    void SetFiltersOrder(int newOrder);                 // 0 = off, 1 = pre, 2 = post
    void SetLowPassCutoff(float newLowpass);            // 500..9000 Hz
    void SetHighPassCutoff(float newHighpass);          // 10..2000 Hz
    //endregion

private:

    double sampleRate = 48000.0;
    float hostTempoBpm = 120.0f;

    juce::AudioBuffer<float> drySnapshot;

    //float delayTimeNormalized = 0.3f;
    float delayMilliseconds = 300.0f;

    static constexpr int NumDistortionModules = 3;

    //region Parameters
    int delayMode = 0;

    float feedbackTimeSeconds = 3.0f;
    float feedbackGain = 0.5f;

    float diffusionAmount = 0.0f;
    float diffusionSize = 0.0f;
    int diffusionQualityStages = 8;

    float dryVolume = 1.0f;
    float wetVolume = 1.0f;

    float lowpassCutoff = 0.0f;
    float highpassCutoff = 0.0f;
    float stereoSpread = 0.0f; // -1 - 1 range
    int filtersOrder = 0;

    float pitchRateMs = 300.0f;
    float pitchRangeLower = -12.0f;
    float pitchRangeUpper = 12.0f;
    int pitchSequence = 0;
    float pitchStereoEnabled01 = 0.0f;
    float pitchWetMix = 0.0f;

    float duckAmount = 0.0f;
    float duckAttack = 0.0f;
    float duckRelease = 0.0f;
    //endregion
};