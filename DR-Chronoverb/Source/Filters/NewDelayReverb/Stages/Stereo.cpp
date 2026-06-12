//
// Created by justin on 6/12/26.
//

#include "Stereo.h"

void Stereo::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    delayTimeSegment.PrepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMilliseconds();
}

std::pair<float, float> Stereo::ProcessSample(float inputL, float inputR)
{
    float spreadLeft = inputL;
    float spreadRight = inputR;

    const float spread = juce::jlimit(-1.0f, 1.0f, stereoSpread);

    if (std::abs(spread) > 0.0001f)
    {
        const float widen = std::max(0.0f,  spread);
        const float narrow = std::max(0.0f, -spread);

        if (widen > 0.0f)
        {
            const float cross = widen * 0.25f;
            const float newLeft = spreadLeft - cross * spreadRight;
            const float newRight = spreadRight - cross * spreadLeft;
            spreadLeft = newLeft;
            spreadRight = newRight;
        }
        else if (narrow > 0.0f)
        {
            const float mono = 0.5f * (spreadLeft + spreadRight);
            spreadLeft = spreadLeft * (1.0f - narrow) + mono * narrow;
            spreadRight = spreadRight * (1.0f - narrow) + mono * narrow;
        }
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

