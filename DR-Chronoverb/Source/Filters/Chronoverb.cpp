#include "Chronoverb.h"

Chronoverb::Chronoverb()
{
    // Must happen at class construction
    DelayLeft = std::make_unique<Delay>();
    DelayRight = std::make_unique<Delay>();

    ReverbLeftRight = std::make_unique<Reverb>();

    PitchShifterLeft = std::make_unique<PitchShifter>();
    PitchShifterRight = std::make_unique<PitchShifter>();
}

void Chronoverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    DelayLeft->PrepareToPlay(sampleRate);
    DelayRight->PrepareToPlay(sampleRate);

    ReverbLeftRight->PrepareToPlay(sampleRate);

    PitchShifterLeft->PrepareToPlay(sampleRate, DelayLeft->InternalDelayLine);
    PitchShifterRight->PrepareToPlay(sampleRate);
}

void Chronoverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples  = audioBuffer.getNumSamples();

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    DelayLeft->ProcessBlock(audioBuffer);
    DelayRight->ProcessBlock(audioBuffer);

    ReverbLeftRight->ProcessBlock(audioBuffer);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float dryLeft = leftData[sampleIndex];
        const float dryRight = (rightData != nullptr ? rightData[sampleIndex] : dryLeft);

        float delayLeft = DelayLeft->ProcessSample(dryLeft);
        float delayRight = DelayRight->ProcessSample(dryRight);

        auto [reverbLeft, reverbRight] =
            ReverbLeftRight->ProcessSample(dryLeft, dryRight);

        // Blend delay -> reverb between diff amt 0.5 -> 1.0
        auto [delayGain, reverbGain] = GetDelayReverbGain(diffusionAmount);

        const float wetLeft = (delayLeft * delayGain) + (reverbLeft * reverbGain);
        const float wetRight = (delayRight * delayGain) + (reverbRight * reverbGain);

        float pitchLeft = PitchShifterLeft->ProcessSample(wetLeft);
        float pitchRight = PitchShifterRight->ProcessSample(wetRight);

        // Dry + wet volume
        float outputLeft = (dryLeft * dryVolume) + (pitchLeft * wetVolume);
        float outputRight = (dryRight * dryVolume) + (pitchRight * wetVolume);

        // Write to buffer
        leftData[sampleIndex] = outputLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = outputRight;
    }
}
