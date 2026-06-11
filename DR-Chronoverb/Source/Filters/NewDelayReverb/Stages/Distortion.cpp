#include "Distortion.h"

#include <tuple>

void Distortion::Prepare(float newSampleRate)
{
    chebyshev.Prepare(newSampleRate);
}

// The master class the holds all of the different types of distortion.
std::tuple<float, float, float, float> Distortion::ProcessSample(float dryL, float dryR, float wetL, float wetR)
{
    auto hardClipperDry = hardClipper.ProcessSample(dryL, dryR);
    auto hardClipperWet = hardClipper.ProcessSample(wetL, wetR);

    //auto chebyshevOut = chebyshev.ProcessSample(wetL, wetR);

    return std::make_tuple(hardClipperDry.first, hardClipperDry.second,
        hardClipperWet.first, hardClipperWet.second);
}