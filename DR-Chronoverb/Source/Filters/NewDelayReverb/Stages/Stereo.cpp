//
// Created by justin on 6/12/26.
//

#include "Stereo.h"

void Stereo::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    delayTimeSegment.PrepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMilliseconds();

    delayLine = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);
}

std::pair<float, float> Stereo::ProcessSample(float inputL, float inputR)
{
    float spreadLeft = inputL;
    float spreadRight = inputR;

    const float spread = juce::jlimit(-1.0f, 1.0f, stereoSpread);

    if (spread < -0.0001f)
    {
        const float narrow = -spread;
        const float mono = 0.5f * (spreadLeft + spreadRight);
        spreadLeft = spreadLeft * (1.0f - narrow) + mono * narrow;
        spreadRight = spreadRight * (1.0f - narrow) + mono * narrow;
    }
    else if (spread > 0.0001f) // TODO: Stereo widening is not good at all...
    {
        const float widen = spread;

        const float haasDelayMs = juce::jmap(widen, 0.0f,
            1.0f, 0.0f, 12.0f);

        // Only delay the right channel
        const float mid = 0.5f * (inputL + inputR);
        const float delayedMid = delayLine->ReadFeedbackBuffer(haasDelayMs);

        delayLine->PushSample(mid);

        spreadLeft = inputL;
        spreadRight = inputR * (1.0f - widen) + delayedMid * widen;
    }

    return std::make_pair(spreadLeft, spreadRight);
}

void Stereo::SetHostTempo(float newHostTempo)
{
    hostBpm = newHostTempo;
    delayTimeSegment.SetHostTempo(hostBpm);
}

void Stereo::SetDelayTime(float newDelayTime)
{
    delayTimeSegment.SetDelayTime(newDelayTime);
}

void Stereo::SetDelayMode(int newDelayMode)
{
    delayTimeSegment.SetDelayMode(newDelayMode);
}

void Stereo::SetStereoSpread(float newStereoSpread)
{
    stereoSpread = newStereoSpread;
}

void Stereo::SetDiffusionAmount(float newAmount)
{
    diffusionAmount = newAmount;
}

