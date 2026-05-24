#include "Chronoverb.h"

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

        auto [cleanLeft, diffusedLeft] = DelayLeft->ProcessSample(dryLeft);
        auto [cleanRight, diffusedRight] = DelayRight->ProcessSample(dryRight);

        // Diffusion amount 0 - 0.5: Cross-fade clean -> diffused
        const float gainCos =
            std::cos(diffusionAmount * juce::MathConstants<float>::halfPi);

        const float gainSin =
            std::sin(diffusionAmount * juce::MathConstants<float>::halfPi);

        const float wetLeft = (cleanLeft * gainCos) + (diffusedLeft  * gainSin);
        const float wetRight = (cleanRight * gainCos) + (diffusedRight * gainSin);

        leftData[sampleIndex] = wetLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = wetRight;
    }
}
