#pragma once

#include <algorithm>
#include <utility>
#include <cmath>

class DCBlocker
{
public:
    void Prepare(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        UpdateCoefficient();
        Reset();
    }

    void Reset()
    {
        x1L = 0.0f;
        y1L = 0.0f;
        x1R = 0.0f;
        y1R = 0.0f;
    }

    void SetCutoffHz(float newCutoffHz)
    {
        cutoffHz = std::clamp(newCutoffHz, 1.0f, 40.0f);
        UpdateCoefficient();
    }

    std::pair<float, float> ProcessSample(float inputL, float inputR)
    {
        return { ProcessMono(inputL, x1L, y1L), ProcessMono(inputR, x1R, y1R) };
    }

private:
    float ProcessMono(float x, float& x1, float& y1)
    {
        const float y = x - x1 + coefficient * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void UpdateCoefficient()
    {
        const float omega = 2.0f * 3.14159265359f * cutoffHz / static_cast<float>(sampleRate);
        coefficient = std::exp(-omega);
    }

    double sampleRate = 48000.0;
    float cutoffHz = 10.0f;
    float coefficient = 0.998f;

    float x1L = 0.0f;
    float y1L = 0.0f;
    float x1R = 0.0f;
    float y1R = 0.0f;
};