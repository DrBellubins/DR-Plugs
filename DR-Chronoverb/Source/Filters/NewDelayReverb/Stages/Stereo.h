#pragma once

#include <utility>

#include "../DelayLine.h"
#include "../DelayTimeSegment.h"

// TODO: Doesn't sound like haas filter (spread > 0) does anything.
class Stereo
{
public:
    void PrepareToPlay(double newSampleRate);

    std::pair<float, float> ProcessSample(float inputL, float inputR);

    void SetHostTempo(float newHostTempo);

    void SetDelayTime(float newDelayTime);
    void SetDelayMode(int newDelayMode);
    void SetStereoSpread(float newStereoSpread);
    void SetDiffusionAmount(float newAmount); // Used for ping-pong smoothing

private:
    double sampleRate = 0.0f;
    float hostBpm = 120.0f;

    float stereoSpread = 0.0f;
    float diffusionAmount = 0.0f;

    DelayTimeSegment delayTimeSegment; // Used for ping-pong
    std::unique_ptr<DelayLine> delayLine;
};
