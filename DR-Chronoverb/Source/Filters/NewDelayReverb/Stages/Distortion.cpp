#include "Distortion.h"

#include <tuple>

void Distortion::PrepareToPlay(float newSampleRate)
{
    chebyshev.Prepare(newSampleRate);
}

// The master class the holds all of the different types of distortion.
std::tuple<float, float, float, float> Distortion::ProcessSample(float dryL, float dryR, float wetL, float wetR)
{
    std::pair<float, float> outDry = std::make_pair(dryL, dryR);
    std::pair<float, float> outWet = std::make_pair(wetL, wetR);

    hardClipper.SetDrive(drive);
    chebyshev.SetDrive(drive);
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

    return std::make_tuple(outDry.first, outDry.second,
    outWet.first, outWet.second);
}

void Distortion::Setup(int newDistortionType, int newDistortionTarget)
{
    distortionType = newDistortionType;
    distortionTarget = newDistortionTarget;
}

void Distortion::SetDrive(float newDrive)
{
    drive = std::clamp(newDrive, 1.0f, 999.0f);
}

void Distortion::SetChebyHarmonics(float newHarmonics)
{
    chebyHarmonics = newHarmonics;
}
