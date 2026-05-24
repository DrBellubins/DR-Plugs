#include "Chronoverb.h"

Chronoverb::Chronoverb()
{
    // Must happen at class construction
    DelayLeft = std::make_unique<Delay>();
    DelayRight = std::make_unique<Delay>();
}

void Chronoverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    DelayLeft->PrepareToPlay(sampleRate);
    DelayRight->PrepareToPlay(sampleRate);
}

void Chronoverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples  = audioBuffer.getNumSamples();

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    DelayLeft->ProcessBlock(audioBuffer);
    DelayRight->ProcessBlock(audioBuffer);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float dryLeft = leftData[sampleIndex];
        const float dryRight = (rightData != nullptr ? rightData[sampleIndex] : dryLeft);

        float delayOutputLeft = DelayLeft->ProcessSample(dryLeft);
        float delayOutputRight = DelayRight->ProcessSample(dryRight);

        const float wetLeft = delayOutputLeft;
        const float wetRight = delayOutputRight;

        leftData[sampleIndex] = wetLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = wetRight;
    }
}
