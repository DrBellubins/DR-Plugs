#include "Chronoverb.h"

Chronoverb::Chronoverb()
{
    // Must happen at class construction
    DeverbLeftRight = std::make_unique<Deverb>();

    PitchShifterLeftRight = std::make_unique<PitchShifter>();
    DistortionLeftRight = std::make_unique<Distortion>();
    StereoLeftRight = std::make_unique<Stereo>();
    DuckingLeftRight = std::make_unique<Ducking>();
    FilterLeftRight = std::make_unique<Filters>();
}

void Chronoverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    // Pre-allocate dry snapshot buffer — avoids heap allocation on the audio thread.
    // 2 channels, 4096 samples covers all typical DAW block sizes.
    drySnapshot.setSize(2, 4096, false, true, false);

    DeverbLeftRight->PrepareToPlay(newSampleRate, *FilterLeftRight);

    PitchShifterLeftRight->PrepareToPlay(sampleRate, *FilterLeftRight);
    DistortionLeftRight->PrepareToPlay(static_cast<float>(sampleRate));
    StereoLeftRight->PrepareToPlay(sampleRate);
    DuckingLeftRight->PrepareToPlay(sampleRate);
    FilterLeftRight->PrepareToPlay(sampleRate);
}

void Chronoverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples  = audioBuffer.getNumSamples();

    // Snapshot dry input before any writes — prevents re-processing own output.
    for (int ch = 0; ch < std::min(numChannels, 2); ++ch)
        drySnapshot.copyFrom(ch, 0, audioBuffer, ch, 0, numSamples);

    const float* drySnapL = drySnapshot.getReadPointer(0);
    const float* drySnapR = drySnapshot.getReadPointer(numChannels > 1 ? 1 : 0);

    float* leftData  = audioBuffer.getWritePointer(0);
    float* rightData = (numChannels > 1 ? audioBuffer.getWritePointer(1) : nullptr);

    DeverbLeftRight->ProcessBlock(audioBuffer);

    PitchShifterLeftRight->ProcessBlock(audioBuffer);
    FilterLeftRight->ProcessBlock(audioBuffer);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float dryLeft = drySnapL[sampleIndex];
        const float dryRight = drySnapR[sampleIndex];

        // 1) Delay/Reverb (Deverb)
        auto [deverbLeft, deverbRight] =
            DeverbLeftRight->ProcessSample(dryLeft, dryRight);

        // 2) Pitch shifter
        auto [pitchLeft, pitchRight] =
            PitchShifterLeftRight->ProcessSample(deverbLeft, deverbRight);

        // 3) Distortion
        auto [distortionDryLeft, distortionDryRight, distortionWetLeft, distortionWetRight] =
            DistortionLeftRight->ProcessSample(dryLeft, dryRight, pitchLeft, pitchRight);

        // 4) Post filters
        float postFilteredLeft = distortionWetLeft;
        float postFilteredRight = distortionWetRight;

        if (filtersOrder == 2)
        {
            auto [filteredL, filteredR] =
                FilterLeftRight->ProcessSample(distortionWetLeft, distortionWetRight);

            postFilteredLeft = filteredL;
            postFilteredRight = filteredR;
        }

        // 5) Ducking (shouldn't duck by distortion signal, since distortion crushes dynamics)
        auto [duckedWetLeft, duckedWetRight] =
            DuckingLeftRight->ProcessSample(dryLeft, dryRight,
                postFilteredLeft, postFilteredRight);

        // 6) Dry/wet volume gain + combine
        float gainedLeft = (distortionDryLeft * dryVolume) + (duckedWetLeft * wetVolume);
        float gainedRight = (distortionDryRight * dryVolume) + (duckedWetRight * wetVolume);

        // 7) Stereo
        auto [stereoLeft, stereoRight] =
            StereoLeftRight->ProcessSample(gainedLeft, gainedRight);

        // Write to buffer
        leftData[sampleIndex] = stereoLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = stereoRight;
    }
}
