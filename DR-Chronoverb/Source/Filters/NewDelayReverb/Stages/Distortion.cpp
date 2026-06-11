#include "Distortion.h"

#include <tuple>

void Distortion::PrepareToPlay(float newSampleRate)
{
    distortionModule1.PrepareToPlay(newSampleRate);
    distortionModule2.PrepareToPlay(newSampleRate);
    distortionModule3.PrepareToPlay(newSampleRate);
}

// The master class the holds all of the different types of distortion.
std::tuple<float, float, float, float> Distortion::ProcessSample(float dryL, float dryR, float wetL, float wetR)
{
    std::pair<float, float> outDry = std::make_pair(dryL, dryR);
    std::pair<float, float> outWet = std::make_pair(wetL, wetR);

    // Module 1
    auto [distMod1DryL, distMod1DryR, distMod1WetL, distMod1WetR] =
        distortionModule1.ProcessSample(dryL, dryR, wetL, wetR);

    outDry.first += distMod1DryL;
    outDry.second += distMod1DryR;
    outWet.first += distMod1WetL;
    outWet.second += distMod1WetR;

    // Module 2
    auto [distMod2DryL, distMod2DryR, distMod2WetL, distMod2WetR] =
        distortionModule1.ProcessSample(dryL, dryR, wetL, wetR);

    outDry.first += distMod2DryL;
    outDry.second += distMod2DryR;
    outWet.first += distMod2WetL;
    outWet.second += distMod2WetR;

    // Module 3
    auto [distMod3DryL, distMod3DryR, distMod3WetL, distMod3WetR] =
        distortionModule1.ProcessSample(dryL, dryR, wetL, wetR);

    outDry.first += distMod3DryL;
    outDry.second += distMod3DryR;
    outWet.first += distMod3WetL;
    outWet.second += distMod3WetR;

    return std::make_tuple(outDry.first, outDry.second,
    outWet.first, outWet.second);
}

void Distortion::SetEnabled(int index, bool newEnabled)
{
    if (index == 0)
        distortionModule1.SetEnabled(newEnabled);
    else if (index == 1)
        distortionModule2.SetEnabled(newEnabled);
    else if (index == 2)
        distortionModule3.SetEnabled(newEnabled);
    else
        DBG("Invalid distortion index: " << index);
}

void Distortion::SetTypeTarget(int index, int newDistortionType, int newDistortionTarget)
{
    if (index == 0)
    {
        distortionModule1.SetType(newDistortionType);
        distortionModule1.SetTarget(newDistortionTarget);
    }
    else if (index == 1)
    {
        distortionModule2.SetType(newDistortionType);
        distortionModule2.SetTarget(newDistortionTarget);
    }
    else if (index == 2)
    {
        distortionModule3.SetType(newDistortionType);
        distortionModule3.SetTarget(newDistortionTarget);
    }
    else
        DBG("Invalid distortion index: " << index);
}

void Distortion::SetDrive(int index, float newDrive)
{
    float drive = std::clamp(newDrive, 1.0f, 999.0f);

    if (index == 0)
        distortionModule1.SetDrive(drive);
    else if (index == 1)
        distortionModule2.SetDrive(drive);
    else if (index == 2)
        distortionModule3.SetDrive(drive);
    else
        DBG("Invalid distortion index: " << index);
}

void Distortion::SetMix(int index, float newMix)
{
    if (index == 0)
        distortionModule1.SetMix(newMix);
    else if (index == 1)
        distortionModule2.SetMix(newMix);
    else if (index == 2)
        distortionModule3.SetMix(newMix);
    else
        DBG("Invalid distortion index: " << index);
}