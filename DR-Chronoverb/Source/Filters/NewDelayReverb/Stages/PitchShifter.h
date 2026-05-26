#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

#include "../PitchShifter.h"
#include "Reverb.h"
#include "../../ChronoverbUtils.h"

class DiffusionChain;

class PitchShifter
{
public:
    void PrepareToPlay(double newSampleRate);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    float ProcessSample(float inputSample);

    void SetHostTempo(float bpm);

    void SetDelayTime(float newDelayTime);
    void SetDelayMode(int newDelaymode);
    void SetFeedbackTime(float newFeedbackTimeSeconds);

    void SetDiffusionAmount(float newDiffusionAmount);
    void SetDiffusionSize(float newDiffusionSize);
    void SetDiffusionQuality(int newDiffusionQuality);

private:
    void updateDelayMillisecondsFromNormalized();
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

    // Settings
    const float MinimumBPM = 20.0f;

    // Latency
    float cachedPitchCompensationMs = 0.0f;
    float pitchShifterLatencyMs = 0.0f;

    // Probably not needed
    int writePeriodSamples = 1;
    int echoWriteCounter = 0;

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
    Reverb reverb;

    std::atomic<bool> pitchSequenceRebuildPending { false };
};
