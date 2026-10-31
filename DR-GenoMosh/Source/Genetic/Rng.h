#pragma once

#include <cstdint>

namespace gmosh
{
//==============================================================================
/// Deterministic, platform independent hashing and pseudo random numbers.
///
/// Design doc section 10: the PRNG must not depend on the standard library
/// distributions or on libm, otherwise presets would sound different on another
/// machine. Everything here is integer arithmetic plus exact power-of-two
/// scaling, so a given seed gives bit identical results everywhere.
namespace Hash
{
/// splitmix64 finalizer.
inline uint64_t mix(uint64_t value)
{
    uint64_t mixed = value;

    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBull;

    return mixed ^ (mixed >> 31);
}

/// Combine two 64 bit values into a well distributed third one.
inline uint64_t combine(uint64_t first, uint64_t second)
{
    return mix(first ^ mix(second + 0x9E3779B97F4A7C15ull));
}

/// Stable hash of a NUL terminated string (FNV-1a, then mixed).
inline uint64_t fromString(const char* text)
{
    uint64_t hashValue = 0xCBF29CE484222325ull;

    while (*text != '\0')
    {
        hashValue ^= static_cast<uint64_t>(static_cast<unsigned char>(*text));
        hashValue *= 0x100000001B3ull;
        ++text;
    }

    return mix(hashValue);
}
} // namespace Hash

//==============================================================================
/// splitmix64 generator.
class Rng
{
  public:
    explicit Rng(uint64_t seed = 0) : state(seed) {}

    uint64_t nextUint64()
    {
        state += 0x9E3779B97F4A7C15ull;
        return Hash::mix(state);
    }

    uint32_t nextUint32() { return static_cast<uint32_t>(nextUint64() >> 32); }

    /// Uniform in [0, 1).
    float uniform() { return static_cast<float>(nextUint64() >> 40) * (1.0f / 16777216.0f); }

    float range(float lowValue, float highValue) { return lowValue + uniform() * (highValue - lowValue); }

    int integer(int numOptions)
    {
        if (numOptions <= 1)
            return 0;

        return static_cast<int>(nextUint32() % static_cast<uint32_t>(numOptions));
    }

    /// Roughly gaussian, clamped to [-scale, +scale].
    ///
    /// Irwin-Hall (sum of three uniforms) rather than Box-Muller: no libm, so
    /// no platform dependent rounding, and the clamp keeps a branch from ever
    /// getting an absurdly large kick.
    float gaussianClamped(float scale = 1.0f)
    {
        // Irwin-Hall of three uniforms is centred on zero and spans +-1.5.
        const float sum = uniform() + uniform() + uniform() - 1.5f;
        float value = sum * scale * 0.6666667f;

        if (value > scale)
            value = scale;

        if (value < -scale)
            value = -scale;

        return value;
    }

    /// True with the given probability.
    bool chance(float probability) { return probability > 0.0f && uniform() < probability; }

    uint64_t getState() const { return state; }

  private:
    uint64_t state;
};

} // namespace gmosh
