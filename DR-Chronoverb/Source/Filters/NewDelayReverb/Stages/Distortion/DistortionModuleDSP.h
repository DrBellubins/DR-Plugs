#pragma once

#include <tuple>

#include "Chebyshev.h"
#include "HardClipper.h"

class DistortionModuleDSP
{
public:
    void PrepareToPlay(float newSampleRate)
    {
        chebyshev.PrepareToPlay(newSampleRate);
    }

    std::tuple<float, float, float, float> ProcessSample(float dryL, float dryR, float wetL, float wetR)
    {
        std::pair<float, float> outDry = std::make_pair(dryL, dryR);
        std::pair<float, float> outWet = std::make_pair(wetL, wetR);

        if (!enabled)
            return std::make_tuple(outDry.first, outDry.second, outWet.first, outWet.second);

        hardClipper.SetDrive(drive);
        chebyshev.SetHarmonics(chebyHarmonics);
        chebyshev.SetMix(1.0f); // let this class own the module mix consistently

        auto blendPair = [this](std::pair<float, float> in, std::pair<float, float> processed)
        {
            const float outL = in.first  + (processed.first  - in.first)  * mix;
            const float outR = in.second + (processed.second - in.second) * mix;
            return std::make_pair(outL, outR);
        };

        if (distortionType == 0)
        {
            if (distortionTarget == 0 || distortionTarget == 2)
            {
                const auto processed = hardClipper.ProcessSample(dryL, dryR);
                outDry = blendPair({ dryL, dryR }, processed);
            }

            if (distortionTarget == 1 || distortionTarget == 2)
            {
                const auto processed = hardClipper.ProcessSample(wetL, wetR);
                outWet = blendPair({ wetL, wetR }, processed);
            }
        }
        else if (distortionType == 1)
        {
            if (distortionTarget == 0 || distortionTarget == 2)
            {
                const auto processed = chebyshev.ProcessSample(dryL, dryR);
                outDry = blendPair({ dryL, dryR }, processed);
            }

            if (distortionTarget == 1 || distortionTarget == 2)
            {
                const auto processed = chebyshev.ProcessSample(wetL, wetR);
                outWet = blendPair({ wetL, wetR }, processed);
            }
        }

        return std::make_tuple(outDry.first, outDry.second, outWet.first, outWet.second);
    }

    void Setup(int newDistortionType, int newDistortionTarget)
    {
        distortionType = newDistortionType;
        distortionTarget = newDistortionTarget;
    }

    void SetEnabled(bool newEnabled)
    {
        enabled = newEnabled;
    }

    void SetType(int newType)
    {
        distortionType = newType;
    }

    void SetTarget(int newTarget)
    {
        distortionTarget = newTarget;
    }

    void SetDrive(float newDrive)
    {
        drive = newDrive * maxDrive;
        chebyHarmonics = newDrive * maxChebyshev;
    }

    void SetMix(float newMix)
    {
        mix = newMix;
    }

private:
    HardClipper hardClipper;
    Chebyshev chebyshev;

    const float maxDrive = 32.0f;
    const float maxChebyshev = 32.0f;

    bool enabled = false;
    int distortionType = 0 ; // 0 = hard clipper, 1 = chebysehv
    int distortionTarget = 1; // 0 = dry, 1 = wet, 2 = both

    float drive = 0.0f; // Pre gain
    float chebyHarmonics = 3.0f; // Chebyshev harmonics 0..32
    float mix = 0.0f;
};
