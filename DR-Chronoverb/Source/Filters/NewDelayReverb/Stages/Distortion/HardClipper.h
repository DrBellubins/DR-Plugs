#pragma once

#include <algorithm>
#include <utility>

class HardClipper
{
public:
    std::pair<float, float> ProcessSample(float inputL, float inputR)
    {
        inputL = inputL * drive;
        inputR = inputR * drive;

        float wetDistL = std::clamp(inputL, -threshold, threshold);
        float wetDistR = std::clamp(inputR, -threshold, threshold);

        return { wetDistL, wetDistR };
    }

    void SetDrive(float newDrive)
    {
        drive = newDrive;
    }

private:
    float drive = 1.0f;
    float threshold = 1.0f; // TODO: Should be in dB
};
