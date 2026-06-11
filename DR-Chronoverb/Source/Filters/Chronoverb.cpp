#include "Chronoverb.h"

Chronoverb::Chronoverb()
{
    // Must happen at class construction
    DelayLeftRight = std::make_unique<Delay>();
    ReverbLeftRight = std::make_unique<Reverb>();
    PitchShifterLeftRight = std::make_unique<PitchShifter>();
    DistortionLeftRight = std::make_unique<Distortion>();
}

void Chronoverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    DelayLeftRight->PrepareToPlay(sampleRate);
    ReverbLeftRight->PrepareToPlay(sampleRate);

    PitchShifterLeftRight->SetDelayLines(*DelayLeftRight->InternalDelayLineLeft,
        *DelayLeftRight->InternalDelayLineRight);

    PitchShifterLeftRight->PrepareToPlay(sampleRate);

    DistortionLeftRight->PrepareToPlay(static_cast<float>(sampleRate));
    DistortionLeftRight->Setup(0, 1);
}

void Chronoverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer) const
{
    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples  = audioBuffer.getNumSamples();

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    DelayLeftRight->ProcessBlock(audioBuffer);
    ReverbLeftRight->ProcessBlock(audioBuffer);
    PitchShifterLeftRight->ProcessBlock(audioBuffer);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float dryLeft = leftData[sampleIndex];
        const float dryRight = (rightData != nullptr ? rightData[sampleIndex] : dryLeft);

        // 1) Delay
        auto [delayLeft, delayRight] =
            DelayLeftRight->ProcessSample(dryLeft, dryRight);

        // 2) Reverb
        auto [reverbLeft, reverbRight] =
            ReverbLeftRight->ProcessSample(dryLeft, dryRight);

        // 3) Blend delay -> reverb between diff amt 0.5 -> 1.0
        auto [delayGain, reverbGain] = GetDelayReverbGain(diffusionAmount);

        const float wetLeft = (delayLeft * delayGain) + (reverbLeft * reverbGain);
        const float wetRight = (delayRight * delayGain) + (reverbRight * reverbGain);

        // 4) Pitch shifter
        auto [pitchLeft, pitchRight] =
            PitchShifterLeftRight->ProcessSample(wetLeft, wetRight);

        // 5) Distortion
        // TEST
        DistortionLeftRight->SetDrive(stereoSpread * 10.0f);

        auto [distortionDryLeft, distortionDryRight, distortionWetLeft, distortionWetRight] =
            DistortionLeftRight->ProcessSample(dryLeft, dryRight, pitchLeft, pitchRight);

        // 6) Dry + wet volume gain
        float outputLeft = (distortionDryLeft * dryVolume) + (distortionWetLeft * wetVolume);
        float outputRight = (distortionDryRight * dryVolume) + (distortionWetRight * wetVolume);

        // Write to buffer
        leftData[sampleIndex] = outputLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = outputRight;
    }
}
