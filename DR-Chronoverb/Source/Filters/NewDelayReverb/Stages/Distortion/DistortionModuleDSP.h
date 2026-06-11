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

        if (enabled)
        {
            hardClipper.SetDrive(drive);
            chebyshev.SetHarmonics(chebyHarmonics);

            if (distortionType == 0)
            {
                if (distortionTarget == 0 || distortionTarget == 2)
                    outDry = hardClipper.ProcessSample(dryL, dryR);

                if (distortionTarget == 1 || distortionTarget == 2)
                    outWet = hardClipper.ProcessSample(wetL, wetR);
            }
            else if (distortionType == 1)
            {
                if (distortionTarget == 0 || distortionTarget == 2)
                    outDry = chebyshev.ProcessSample(dryL, dryR);

                if (distortionTarget == 1 || distortionTarget == 2)
                    outWet = chebyshev.ProcessSample(wetL, wetR);
            }
        }

        return std::make_tuple(outDry.first, outDry.second,
        outWet.first, outWet.second);
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
