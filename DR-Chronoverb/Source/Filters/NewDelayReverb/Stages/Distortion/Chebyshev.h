#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

#include "../Utils/DCBlocker.h"

class Chebyshev
{
public:
    void PrepareToPlay(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        dcBlocker.Prepare(sampleRate);
        dcBlocker.SetCutoffHz(10.0f);

        smoothedHarmonics.reset(sampleRate, 0.02); // 20 ms
        smoothedHarmonics.setCurrentAndTargetValue(harmonics);

        Reset();
    }

    void Reset()
    {
        dcBlocker.Reset();
    }

    void SetHarmonics(float newHarmonics)
    {
        harmonics = std::clamp(newHarmonics, 0.0f, 32.0f);
        smoothedHarmonics.setTargetValue(harmonics);
    }

    void SetOrder(int newOrder)
    {
        order = std::clamp(newOrder, 1, 16);
        harmonics = ((static_cast<float>(order) - 1.0f)
            / static_cast<float>(maxPolynomialOrder - 1)) * 32.0f;
    }

    void SetDrive(float newDrive)
    {
        drive = std::max(0.0f, newDrive);
    }

    void SetOutputGain(float newOutputGain)
    {
        outputGain = std::max(0.0f, newOutputGain);
    }

    void SetMix(float newMix)
    {
        mix = std::clamp(newMix, 0.0f, 1.0f);
    }

    void SetInputTrim(float newInputTrim)
    {
        inputTrim = std::max(0.0f, newInputTrim);
    }

    void SetDCBlockEnabled(bool shouldEnable)
    {
        dcBlockEnabled = shouldEnable;
    }

    std::pair<float, float> ProcessSample(float inputL, float inputR)
    {
        const float dryL = inputL;
        const float dryR = inputR;

        float wetL = ProcessMono(inputL);
        float wetR = ProcessMono(inputR);

        if (dcBlockEnabled)
        {
            auto dcBlocked = dcBlocker.ProcessSample(wetL, wetR);
            wetL = dcBlocked.first;
            wetR = dcBlocked.second;
        }

        wetL *= outputGain;
        wetR *= outputGain;

        const float outL = dryL + (wetL - dryL) * mix;
        const float outR = dryR + (wetR - dryR) * mix;

        return { outL, outR };
    }

    // ------------------------------------------------------------------
    // Oversampling-ready hook:
    // This is the isolated nonlinear function you'd later run inside an
    // upsample/process/downsample wrapper.
    // ------------------------------------------------------------------
    float ProcessShaperOnly(float inputSample)
    {
        float x = inputSample * inputTrim * drive;
        x = std::clamp(x, -1.0f, 1.0f);

        const float currentHarmonics = smoothedHarmonics.getNextValue();
        const float mappedOrder = MapHarmonicsToOrder(currentHarmonics, maxPolynomialOrder);

        const int lowerOrder =
            std::clamp(static_cast<int>(std::floor(mappedOrder)), 1, maxPolynomialOrder);

        const int upperOrder =
            std::clamp(lowerOrder + 1, 1, maxPolynomialOrder);

        const float blend = mappedOrder - static_cast<float>(lowerOrder);

        const float yLower = EvaluateChebyshevPolynomial(x, lowerOrder);
        const float yUpper = EvaluateChebyshevPolynomial(x, upperOrder);

        float y = yLower + (yUpper - yLower) * blend;

        // Makes harmonics=0 behave closer to clean.
        const float shapeAmount = std::clamp(currentHarmonics, 0.0f, 1.0f);
        y = x + (y - x) * shapeAmount;

        y = SoftClip(y);
        return y;
    }

private:
    float ProcessMono(float inputSample)
    {
        return ProcessShaperOnly(inputSample);
    }

    static float EvaluateChebyshevPolynomial(float x, int polynomialOrder)
    {
        // Stable recurrence:
        // T0(x) = 1
        // T1(x) = x
        // Tn(x) = 2xT(n-1)(x) - T(n-2)(x)
        if (polynomialOrder <= 0)
            return 1.0f;

        if (polynomialOrder == 1)
            return x;

        float t0 = 1.0f;
        float t1 = x;
        float tn = x;

        for (int n = 2; n <= polynomialOrder; ++n)
        {
            tn = 2.0f * x * t1 - t0;
            t0 = t1;
            t1 = tn;
        }

        return tn;
    }

    static float MapHarmonicsToOrder(float harmonicsValue, int maxOrder)
    {
        const float normalized = std::clamp(harmonicsValue / 32.0f, 0.0f, 1.0f);
        const float curved = std::pow(normalized, 1.35f);
        return 1.0f + curved * static_cast<float>(maxOrder - 1);
    }

    static float SoftClip(float x)
    {
        // Simple tanh-style containment without needing std::tanh
        // You could swap this for std::tanh if preferred.
        return x / (1.0f + std::abs(x));
    }

    double sampleRate = 48000.0;

    int order = 3;
    float harmonics = 3.0f;
    int maxPolynomialOrder = 16;

    float drive = 1.0f;
    float outputGain = 1.0f;
    float mix = 1.0f;
    float inputTrim = 0.8f;
    bool dcBlockEnabled = true;

    juce::SmoothedValue<float> smoothedHarmonics;

    DCBlocker dcBlocker;
};