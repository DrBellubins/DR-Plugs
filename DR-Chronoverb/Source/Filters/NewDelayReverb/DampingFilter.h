#pragma once

#include <algorithm>
#include <cmath>

class DampingFilter
{
public:
    void Prepare(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        Reset();
        UpdateCoefficient();
    }

    void Reset()
    {
        z1 = 0.0f;
    }

    void SetCutoffHz(float newCutoffHz)
    {
        const float clamped = std::clamp(newCutoffHz, 20.0f, 20000.0f);

        if (std::abs(clamped - cutoffHz) < 0.001f)
            return;

        cutoffHz = clamped;
        UpdateCoefficient();
    }

    float ProcessSample(float inputSample)
    {
        z1 = a0 * inputSample + b1 * z1;
        return z1;
    }

private:
    void UpdateCoefficient()
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float x = std::exp(-2.0f * kPi * cutoffHz / static_cast<float>(sampleRate));
        a0 = 1.0f - x;
        b1 = x;
    }

    double sampleRate = 48000.0;
    float cutoffHz = 7000.0f;

    float a0 = 0.0f;
    float b1 = 0.0f;
    float z1 = 0.0f;
};