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
    float dry1L = inputDryL, dry1R = inputDryR, wet1L = inputWetL, wet1R = inputWetR;

    if (!distortionModule1.GetEnabled())
    {
        auto dist1 =
        distortionModule1.ProcessSample(inputDryL, inputDryR, inputWetL, inputWetR);

        dry1L = std::get<0>(dist1);
        dry1R = std::get<1>(dist1);
        wet1L = std::get<2>(dist1);
        wet1R = std::get<3>(dist1);
    }

    float dry2L = dry1L, dry2R = dry1R, wet2L = wet1L, wet2R = wet1R;

    if (!distortionModule2.GetEnabled())
    {
        auto dist2 =
            distortionModule2.ProcessSample(dry1L, dry1R, wet1L, wet1R);

        dry2L = std::get<0>(dist2);
        dry2R = std::get<1>(dist2);
        wet2L = std::get<2>(dist2);
        wet2R = std::get<3>(dist2);
    }

    float dry3L = dry2L, dry3R = dry2R, wet3L = wet2L, wet3R = wet2R;

    if (!distortionModule3.GetEnabled())
    {
        auto dist3 =
            distortionModule3.ProcessSample(dry1L, dry1R, wet1L, wet1R);

        dry3L = std::get<0>(dist3);
        dry3R = std::get<1>(dist3);
        wet3L = std::get<2>(dist3);
        wet3R = std::get<3>(dist3);
    }

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