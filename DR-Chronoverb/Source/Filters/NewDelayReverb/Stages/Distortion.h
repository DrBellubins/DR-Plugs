#pragma once

#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include "Distortion/HardClipper.h"
#include "Distortion/Chebyshev.h"
#include "Distortion/DistortionModuleDSP.h"

class DistortionModuleDSP;

class Distortion
{
public:
    void PrepareToPlay(float newSampleRate);

    std::tuple<float, float, float, float> ProcessSample(float inputDryL, float inputDryR, float inputWetL, float inputWetR);

    void SetEnabled(int index, bool newEnabled);
    void SetTypeTarget(int index, int newDistortionType, int newDistortionTarget);
    void SetDrive(int index, float newDrive);
    void SetMix(int index, float newMix);

private:
    DistortionModuleDSP distortionModule1;
    DistortionModuleDSP distortionModule2;
    DistortionModuleDSP distortionModule3;
};