#pragma once

#include <utility>

#include "Distortion/HardClipper.h"
#include "Distortion/Chebyshev.h"

class Distortion
{
public:
    void PrepareToPlay(float newSampleRate);

    std::tuple<float, float, float, float> ProcessSample(float dryL, float dryR, float wetL, float wetR);

    void Setup(int newDistortionType, int newDistortionTarget);

    void SetDrive(float newDrive);

private:
    HardClipper hardClipper;
    Chebyshev chebyshev;

    float drive = 0.0f; // Pre gain

    int distortionType = 0 ; // 0 = hard clipper, 1 = chebysehv
    int distortionTarget = 1; // 0 = dry, 1 = wet, 2 = both
};
