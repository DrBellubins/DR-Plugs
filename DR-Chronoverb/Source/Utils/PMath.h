#pragma once

class PMath
{
    public:

    static float Lerp(float StartValue, float EndValue, float Amount01)
    {
        return StartValue + (EndValue - StartValue) * Amount01;
    }

    // Add this utility equal-power crossfade helper.
    // Amount01 = 0 -> outputSample = drySample
    // Amount01 = 1 -> outputSample = wetSample
    // Uses sin/cos mapping to maintain perceived loudness across the crossfade.
    static void EqualPowerCrossfade(float amount01,
                                    float drySample,
                                    float wetSample,
                                    float& outputSample)
    {
        const float clampedAmount = juce::jlimit(0.0f, 1.0f, amount01);

        const float dryWeight = std::cos(clampedAmount * juce::MathConstants<float>::halfPi);
        const float wetWeight = std::sin(clampedAmount * juce::MathConstants<float>::halfPi);

        outputSample = dryWeight * drySample + wetWeight * wetSample;
    }
};