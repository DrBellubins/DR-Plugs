#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>

#include "../Utils/PMath.h"
#include "NewDelayReverb/DelayLine.h"
#include "NewDelayReverb/DampingFilter.h"
#include "NewDelayReverb/DiffusionChain.h"
#include "NewDelayReverb/PitchShiftingEngine.h"
#include "NewDelayReverb/DiffusionAllpass.h"
#include "ChronoverbUtils.h"

#include "NewDelayReverb/Stages/Delay.h"
#include "NewDelayReverb/Stages/Reverb.h"
#include "NewDelayReverb/Stages/PitchShifter.h"

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

    //region Parameter Sets
    void SetHostTempo(float bpm);
    void SetDelayTime(float newDelayTimeNormalized);      // 0..1 -> 0..1000 ms
    void SetDelayMode(int newDelayMode);

    void SetFeedbackTime(float newFeedbackTimeSeconds);   // 0..10 s
    void SetDiffusionAmount(float newAmount01);
    void SetDiffusionSize(float newSize01);
    void SetDiffusionQuality(int newQualityStages);       // 1..8

    void SetDryVolume(float newDryVolume);
    void SetWetVolume(float newWetVolume);

    void SetLowpassCutoff(float newLowpass01);            // 0..1 -> 500..9000 Hz
    void SetHighpassCutoff(float newHighpass01);          // 0..1 -> 10..2000 Hz
    void SetStereoSpread(float newSpreadMinus1To1);       // -1..1
    void SetHPLPPrePost(float prePost01);                 // 0 = Pre, 1 = Post

    void SetPitchRangeLower(float pitchRangeLowerSemitones);
    void SetPitchRangeUpper(float pitchRangeUpperSemitones);
    void SetPitchSequence(int sequenceIndex);                // 0=Up, 1=Down, 2=Random
    void SetPitchStereoEnabled(float enabled01);
    void SetpitchWetMix(float wetVolume);
    //endregion

private:
    //region Parameters
    double sampleRate = 48000.0;
    float hostTempoBpm = 120.0f;

    float delayTimeNormalized = 0.3f;
    float delayMilliseconds = 300.0f;
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
    float hplpPrePost01 = 1.0f;

    float pitchRangeLower = -12.0f;
    float pitchRangeUpper = 12.0f;
    int pitchMode = 0;
    float pitchStereoEnabled01 = 0.0f;
    float pitchWetMix = 0.0f;
    //endregion
};