#pragma once

// Smoothers
// - Header-only static helpers for one-pole smoothing.

class Smoothers
{
public:
    // One-pole lag towards target: y += a * (target - y)
    static inline float OnePole(float CurrentValue, float TargetValue, float Coefficient)
    {
        return CurrentValue + Coefficient * (TargetValue - CurrentValue);
    }
};