#pragma once

#include <algorithm>
#include <utility>

class HardClipper
{
public:
    std::pair<float, float> ProcessSample(float inputL, float inputR)
    {
        float outL = std::clamp(inputL, -Threshold, Threshold);
        float outR = std::clamp(inputR, -Threshold, Threshold);

        return { outL, outR };
    }

    float Threshold = 1.0f; // TODO: Should be in dB
};
