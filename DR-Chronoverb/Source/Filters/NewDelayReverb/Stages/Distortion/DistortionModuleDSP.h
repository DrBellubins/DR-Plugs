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
        std::pair<float, float> outDry = { dryL, dryR };
        std::pair<float, float> outWet = { wetL, wetR };

        if (!enabled)
            return std::make_tuple(outDry.first, outDry.second, outWet.first, outWet.second);

        const float moduleMix = juce::jlimit(0.0f, 1.0f, mix);

        // Make drive clearly audible for now.
        hardClipper.SetDrive(drive);
        chebyshev.SetHarmonics(chebyHarmonics);
        chebyshev.SetMix(1.0f);

        auto blendPair = [moduleMix](std::pair<float, float> in,
                                     std::pair<float, float> processed)
        {
            return std::make_pair(
                in.first + (processed.first - in.first) * moduleMix,
                in.second + (processed.second - in.second) * moduleMix);
        };

        auto processByType = [this](float left, float right) -> std::pair<float, float>
        {
            switch (distortionType)
            {
                case 0: // Heat - temporary alias
                case 2: // Hard Clip
                    return hardClipper.ProcessSample(left, right);

                case 1: // Chebyshev
                    return chebyshev.ProcessSample(left, right);

                case 3: // Tube - temporary alias
                    return hardClipper.ProcessSample(left, right);

                default:
                    return { left, right };
            }
        };

        if (distortionTarget == 0 || distortionTarget == 2)
        {
            const auto processedDry = processByType(dryL, dryR);
            outDry = blendPair({ dryL, dryR }, processedDry);
        }

        if (distortionTarget == 1 || distortionTarget == 2)
        {
            const auto processedWet = processByType(wetL, wetR);
            outWet = blendPair({ wetL, wetR }, processedWet);
        }

        return std::make_tuple(outDry.first, outDry.second, outWet.first, outWet.second);
    }

    void Setup(int newDistortionType, int newDistortionTarget)
    {
        distortionType = newDistortionType;
        distortionTarget = newDistortionTarget;
    }

    void SetEnabled(bool newEnabled) { enabled = newEnabled; }
    bool GetEnabled() const { return enabled; }

    void SetType(int newType) { distortionType = newType;}
    void SetTarget(int newTarget){ distortionTarget = newTarget; }

    void SetDrive(float newDrive)
    {
        drive = newDrive * maxDrive;
        chebyHarmonics = newDrive * maxChebyshev;
    }

    void SetMix(float newMix) { mix = newMix; }

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
