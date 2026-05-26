#pragma once

#include "../PitchShiftingEngine.h"
#include "Reverb.h"
#include "../DelayTimeSegment.h"
#include "../DelayLine.h"
#include "../../../Utils/PMath.h"

class PitchShifter
{
public:
    PitchShifter(const DelayLine& newDelayLine);

    void PrepareToPlay(double newSampleRate);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    float ProcessSample(float inputSample);

    void SetHostTempo(float bpm);

    void SetDelayTime(float newDelayTime);
    void SetDelayMode(int newDelaymode);

    void SetDiffusionAmount(float newDiffusionAmount);
    void SetDiffusionSize(float newDiffusionSize);
    void SetDiffusionQuality(int newDiffusionQuality);

    void SetPitchRangeLower(float pitchRangeLowerSemitones);
    void SetPitchRangeUpper(float pitchRangeUpperSemitones);
    void SetPitchSequence(int sequenceIndex);
    void SetPitchWetMix(float newPitchWetMix);

    void SetWetMix(float newWetMix);

private:
    void rebuildPitchSequences();

    // Runtime
    double sampleRate = 48000.0;
    float hostBPM = 120.0f;

    float maxDelayMS = 0.0f;

    float lastFeedback = 0.0f;
    float feedbackGain = 0.5f;

    int lastBuiltQualityStages = -1;
    float lastBuiltSize01 = -1.0f;

    float smoothedCenteredReadDelayMilliseconds = 1.0f;
    float readDelaySlewCoefficient = 0.0f;

    int writePeriodSamples = 1;
    int echoWriteCounter = 0;

    // Settings
    const float MinimumBPM = 20.0f;

    // Latency
    float cachedPitchCompensationMs = 0.0f;
    float pitchShifterLatencyMs = 0.0f;

    // Parameters
    float delayTimeNormalized = 0.3f;
    float delayMilliseconds = 300.0f;

    int delayMode = 0;

    float feedbackTimeSeconds = 3.0f;

    float diffusionAmount = 0.0f;
    float diffusionSize = 0.0f;
    int diffusionQualityStages = 8;

    float pitchRangeLower = -12.0f;
    float pitchRangeUpper = 12.0f;
    int pitchMode = 0;
    float pitchStereoEnabled = 0.0f;
    float pitchWetMix = 0.0f;

    // Data
    OctaveEchoPitchShifter pitchShifter;

    //DelayLine delayLine;
    DelayLine delayLine;

    DelayTimeSegment delayTimeSegment;
    Reverb reverb;

    std::atomic<bool> pitchSequenceRebuildPending { false };
};
