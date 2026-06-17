#include "Chronoverb.h"

Chronoverb::Chronoverb()
{
    // Must happen at class construction
    DeverbLeftRight = std::make_unique<Deverb>();

    //DelayLeftRight = std::make_unique<Delay>();
    //ReverbLeftRight = std::make_unique<Reverb>();
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

    DeverbLeftRight->PrepareToPlay(newSampleRate);

    //DelayLeftRight->PrepareToPlay(sampleRate, *FilterLeftRight);
    //ReverbLeftRight->PrepareToPlay(sampleRate, *FilterLeftRight);

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

    //DelayLeftRight->ProcessBlock(audioBuffer);
    //ReverbLeftRight->ProcessBlock(audioBuffer);

    PitchShifterLeftRight->ProcessBlock(audioBuffer);
    FilterLeftRight->ProcessBlock(audioBuffer);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const float dryLeft = drySnapL[sampleIndex];
        const float dryRight = drySnapR[sampleIndex];

        // 0) Pre filters
        // TODO: (breaks if removed) Don't know why this is needed when it exists in delay/reverb
        float preFilteredLeft = dryLeft;
        float preFilteredRight = dryRight;

        if (filtersOrder == 1)
        {
            auto [filteredL, filteredR] =
                FilterLeftRight->ProcessSample(dryLeft, dryRight);

            preFilteredLeft = filteredL;
            preFilteredRight = filteredR;
        }

        // 1) Deverb test
        auto [deverbLeft, deverbRight] =
            DeverbLeftRight->ProcessSample(preFilteredLeft, preFilteredRight);

        /*auto [delayGain, reverbGain] = GetDelayReverbGain(diffusionAmount);

        // 1) Delay
        float delayLeft = 0, delayRight = 0;

        if (delayGain > 0.0001f)
        {
            auto delayPair =
                DelayLeftRight->ProcessSample(preFilteredLeft, preFilteredRight);

            delayLeft = delayPair.first;
            delayRight = delayPair.second;
        }

        // 2) Reverb
        float reverbLeft = 0, reverbRight = 0;

        if (reverbGain > 0.0001f)
        {
            auto reverbPair = ReverbLeftRight->ProcessSample(preFilteredLeft, preFilteredRight);

            reverbLeft = reverbPair.first;
            reverbRight = reverbPair.second;
        }

        // 3) Blend delay -> reverb between diff amt 0.5 -> 1.0
        const float wetLeft = (delayLeft * delayGain) + (reverbLeft * reverbGain);
        const float wetRight = (delayRight * delayGain) + (reverbRight * reverbGain);*/

        // 4) Pitch shifter
        auto [pitchLeft, pitchRight] =
            PitchShifterLeftRight->ProcessSample(deverbLeft, deverbRight);

        // 5) Distortion
        auto [distortionDryLeft, distortionDryRight, distortionWetLeft, distortionWetRight] =
            DistortionLeftRight->ProcessSample(dryLeft, dryRight, pitchLeft, pitchRight);

        // 6) Post filters
        float postFilteredLeft = distortionWetLeft;
        float postFilteredRight = distortionWetRight;

        if (filtersOrder == 2)
        {
            auto [filteredL, filteredR] =
                FilterLeftRight->ProcessSample(distortionWetLeft, distortionWetRight);

            postFilteredLeft = filteredL;
            postFilteredRight = filteredR;
        }

        // 7) Ducking (shouldn't duck by distortion signal, since distortion crushes dynamics)
        auto [duckedWetLeft, duckedWetRight] =
            DuckingLeftRight->ProcessSample(dryLeft, dryRight,
                postFilteredLeft, postFilteredRight);

        // 8) Dry/wet volume gain + combine
        float gainedLeft = (distortionDryLeft * dryVolume) + (duckedWetLeft * wetVolume);
        float gainedRight = (distortionDryRight * dryVolume) + (duckedWetRight * wetVolume);

        // 9) Stereo
        auto [stereoLeft, stereoRight] =
            StereoLeftRight->ProcessSample(gainedLeft, gainedRight);

        // Write to buffer
        leftData[sampleIndex] = stereoLeft;

        if (rightData != nullptr)
            rightData[sampleIndex] = stereoRight;
    }
}
