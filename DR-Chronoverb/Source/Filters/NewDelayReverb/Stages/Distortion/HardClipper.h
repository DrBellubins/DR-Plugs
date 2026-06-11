#pragma once

#include <algorithm>
#include <utility>

class HardClipper
{
public:
    std::pair<float, float> ProcessSample(float inputL, float inputR)
    {
        float wetDistL = std::clamp(inputL, -Threshold, Threshold);
        float wetDistR = std::clamp(inputR, -Threshold, Threshold);

        return { wetDistL, wetDistR };
    }

    float Threshold = 1.0f; // TODO: Should be in dB
};
