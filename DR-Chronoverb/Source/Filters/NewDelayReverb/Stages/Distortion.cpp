#include "Distortion.h"

#include <tuple>

void Distortion::PrepareToPlay(float newSampleRate)
{
    distortionModule1.PrepareToPlay(newSampleRate);
    distortionModule2.PrepareToPlay(newSampleRate);
    distortionModule3.PrepareToPlay(newSampleRate);
}

// The master class the holds all of the different types of distortion.
std::tuple<float, float, float, float> Distortion::ProcessSample(float inputDryL, float inputDryR,
    float inputWetL, float inputWetR)
{
    auto [dry1L, dry1R, wet1L, wet1R] =
        distortionModule1.ProcessSample(inputDryL, inputDryR, inputWetL, inputWetR);

    auto [dry2L, dry2R, wet2L, wet2R] =
        distortionModule2.ProcessSample(dry1L, dry1R, wet1L, wet1R);

    auto [dry3L, dry3R, wet3L, wet3R] =
        distortionModule3.ProcessSample(dry2L, dry2R, wet2L, wet2R);

    return std::make_tuple(dry3L, dry3R, wet3L, wet3R);
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

void Distortion::SetDrive(int index, float newDrive)
{
    if (index == 0)
        distortionModule1.SetDrive(newDrive);
    else if (index == 1)
        distortionModule2.SetDrive(newDrive);
    else if (index == 2)
        distortionModule3.SetDrive(newDrive);
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

void Distortion::SetType(int index, int newDistortionType)
{
    if (index == 0)
        distortionModule1.SetType(newDistortionType);
    else if (index == 1)
        distortionModule2.SetType(newDistortionType);
    else if (index == 2)
        distortionModule3.SetType(newDistortionType);
    else
        DBG("Invalid distortion index: " << index);
}

void Distortion::SetTarget(int index, int newDistortionTarget)
{
    if (index == 0)
        distortionModule1.SetTarget(newDistortionTarget);
    else if (index == 1)
        distortionModule2.SetTarget(newDistortionTarget);
    else if (index == 2)
        distortionModule3.SetTarget(newDistortionTarget);
    else
        DBG("Invalid distortion index: " << index);
}