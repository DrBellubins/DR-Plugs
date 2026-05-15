#pragma once

class PMath
{
public:

    static float Lerp(float StartValue, float EndValue, float Amount01)
    {
        return StartValue + (EndValue - StartValue) * Amount01;
    }

    // Equal-power crossfade.
    // fade = 0.0 -> output is startValue only
    // fade = 1.0 -> output is endValue only
    // fade is in [0, 1]; mapped to [0, π/2] internally.
    static float EqualPowerCrossfade(float startValue, float endValue, float fade)
    {
        const float angle = juce::jlimit(0.0f, 1.0f, fade)
                            * juce::MathConstants<float>::halfPi;

        return std::cos(angle) * startValue + std::sin(angle) * endValue;
    }
};