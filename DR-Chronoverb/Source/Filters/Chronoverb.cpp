#include "Chronoverb.h"

Chronoverb::Chronoverb()
{
    // Must happen at class construction
    DelayLeft = std::make_unique<Delay>();
    DelayRight = std::make_unique<Delay>();

    ReverbLeft = std::make_unique<Reverb>();
    ReverbRight = std::make_unique<Reverb>();
}

void Chronoverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    DelayLeft->PrepareToPlay(sampleRate);
    DelayRight->PrepareToPlay(sampleRate);

    ReverbLeft->PrepareToPlay(sampleRate);
    ReverbRight->PrepareToPlay(sampleRate);
}

void Chronoverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples  = audioBuffer.getNumSamples();

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    DelayLeft->ProcessBlock(audioBuffer);
    DelayRight->ProcessBlock(audioBuffer);

    ReverbLeft->ProcessBlock(audioBuffer);
    ReverbRight->ProcessBlock(audioBuffer);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float dryLeft = leftData[sampleIndex];
        const float dryRight = (rightData != nullptr ? rightData[sampleIndex] : dryLeft);

        float delayLeft = DelayLeft->ProcessSample(dryLeft);
        float delayRight = DelayRight->ProcessSample(dryRight);

        float reverbLeft = ReverbLeft->ProcessSample(dryLeft);
        float reverbRight = ReverbRight->ProcessSample(dryRight);

        // Blend delay -> reverb between diff amt 0.5 -> 1.0
        auto [delayGain, reverbGain] = GetDelayReverbGain(diffusionAmount);

        DBG("Delay gain: " << delayGain << " Reverb gain: " << reverbGain);

        const float wetLeft = (delayLeft * delayGain) + (reverbLeft * reverbGain);
        const float wetRight = (delayRight * delayGain) + (reverbRight * reverbGain);

        // Dry + wet volume
        //float outputLeft = (dryLeft * dryVolume) + (wetLeft * wetVolume);
        //float outputRight = (dryRight * dryVolume) + (wetRight * wetVolume);

        float outputLeft = delayLeft;
        float outputRight = delayRight;

        // Write to buffer
        leftData[sampleIndex] = outputLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = outputRight;
    }
}
