#pragma once

#include <utility>

#include "Distortion/HardClipper.h"
#include "Distortion/Chebyshev.h"

class Distortion
{
public:
    void Prepare(float newSampleRate);

    std::tuple<float, float, float, float> ProcessSample(float dryL, float dryR, float wetL, float wetR);

private:
    HardClipper hardClipper;
    Chebyshev chebyshev;
};
