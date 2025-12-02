#pragma once

#include <cmath>

// DampingFilter
// One-pole lowpass for spectral decay in the feedback path.
// input: wet sample (delayed)
// control: lowpass01 (0..1) mapped to cutoff range externally
//
// Implementation uses simple one-pole accumulator form with alpha derived from cutoff.
// We rely on external mapping to cutoff; here, we compute alpha from cutoff to give
// reasonable smoothing.
class DampingFilter
{
public:
    DampingFilter()
    {
    }

    void Prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        z1 = 0.0f;
    }

    float ProcessSample(float inputSample, float lowpass01)
    {
        // Map lowpass01 to cutoff; if you prefer alternative mapping, adjust in NewDelayReverb::updateFilters.
        // Here, we keep it simple and derive alpha from an approximate RC filter coefficient.
        const float cutoffHz = 500.0f + lowpass01 * (9000.0f - 500.0f);
        const float x = std::exp(-2.0f * static_cast<float>(M_PI) * cutoffHz / static_cast<float>(sampleRate));
        const float alpha = 1.0f - x;

        const float y = alpha * inputSample + (1.0f - alpha) * z1;
        z1 = y;
        return y;
    }

private:
    double sampleRate = 48000.0;
    float z1 = 0.0f;
};