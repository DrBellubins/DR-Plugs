#pragma once

#include <utility>

#include "Distortion/HardClipper.h"

class Distortion
{
public:
    std::pair<float, float> ProcessSample(float dryL, float dryR, float wetL, float wetR);

private:
    HardClipper hardClipper;
};
