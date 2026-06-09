#include "Distortion.h"

void Distortion::Prepare(float newSampleRate)
{
    chebyshev.Prepare(newSampleRate);
}

// The master class the holds all of the different types of distortion.
std::pair<float, float> Distortion::ProcessSample(float dryL, float dryR, float wetL, float wetR)
{
    auto hardClipperOut = hardClipper.ProcessSample(wetL, wetR);
    auto chebyshevOut = chebyshev.ProcessSample(wetL, wetR);

    return hardClipperOut;
}