#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cmath>
#include <utility>

class Ducking
{
public:
    void PrepareToPlay(double newSampleRate);
    void Reset();

    // Returns ducked wet L/R. Detector is driven from dry L/R.
    std::pair<float, float> ProcessSample(float dryL, float dryR, float wetL, float wetR);

    void SetDuckAmount(float newAmount01);
    void SetDuckAttack(float newAttackMs);
    void SetDuckRelease(float newReleaseMs);

private:
    float ComputeEnvelopeFromDry(float dryL, float dryR) const;
    void UpdateTimeCoefficients();

    double sampleRate = 48000.0;

    float duckAmount = 0.0f;     // 0..1
    float attackMs = 300.0f;
    float releaseMs = 300.0f;

    float attackCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;

    float detectorEnvelope = 0.0f;
};