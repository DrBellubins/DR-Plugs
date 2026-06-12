#pragma once

#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include "Distortion/HardClipper.h"
#include "Distortion/Chebyshev.h"
#include "Distortion/DistortionModuleDSP.h"

// TODO: Implement pre-post (applies to wet signal only)
// TODO: For example, if pre and wet, the signal into wet will be distorted.
// TODO: If post and wet, the wet signal is distorted, same applies if both (only to wet)
class DistortionModuleDSP;

class Distortion
{
public:
    void PrepareToPlay(float newSampleRate);

    std::tuple<float, float, float, float> ProcessSample(float inputDryL, float inputDryR, float inputWetL, float inputWetR);

    void SetEnabled(int index, bool newEnabled);
    void SetDrive(int index, float newDrive);
    void SetMix(int index, float newMix);

    void SetType(int index, int newType);
    void SetTarget(int index, int newTarget);

private:
    DistortionModuleDSP distortionModule1;
    DistortionModuleDSP distortionModule2;
    DistortionModuleDSP distortionModule3;
};