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
namespace hash
{
    /// splitmix64 finalizer.
    inline uint64_t mix (uint64_t z)
    {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /// Combine two 64 bit values into a well distributed third one.
    inline uint64_t combine (uint64_t a, uint64_t b)
    {
        return mix (a ^ mix (b + 0x9E3779B97F4A7C15ull));
    }

    /// Stable hash of a NUL terminated string (FNV-1a, then mixed).
    inline uint64_t fromString (const char* text)
    {
        uint64_t h = 0xCBF29CE484222325ull;
        while (*text != '\0')
        {
            h ^= static_cast<uint64_t> (static_cast<unsigned char> (*text));
            h *= 0x100000001B3ull;
            ++text;
        }
        return mix (h);
    }
} // namespace hash

//==============================================================================
/// splitmix64 generator.
class Rng
{
public:
    explicit Rng (uint64_t seed = 0) : state (seed) {}

    uint64_t nextUint64()
    {
        state += 0x9E3779B97F4A7C15ull;
        return hash::mix (state);
    }

    uint32_t nextUint32() { return static_cast<uint32_t> (nextUint64() >> 32); }

    /// Uniform in [0, 1).
    float uniform()
    {
        return static_cast<float> (nextUint64() >> 40) * (1.0f / 16777216.0f);
    }

    float range (float lo, float hi) { return lo + uniform() * (hi - lo); }

    int integer (int numOptions)
    {
        if (numOptions <= 1)
            return 0;
        return static_cast<int> (nextUint32() % static_cast<uint32_t> (numOptions));
    }

    /// Roughly gaussian, clamped to [-scale, +scale].
    ///
    /// Irwin-Hall (sum of three uniforms) rather than Box-Muller: no libm, so
    /// no platform dependent rounding, and the clamp keeps a branch from ever
    /// getting an absurdly large kick.
    float gaussianClamped (float scale = 1.0f)
    {
        const float sum = uniform() + uniform() + uniform() - 1.5f; // [-1.5, 1.5]
        float v = sum * scale * 0.6666667f;
        if (v > scale)  v = scale;
        if (v < -scale) v = -scale;
        return v;
    }

    /// True with probability p.
    bool chance (float p) { return p > 0.0f && uniform() < p; }

    uint64_t getState() const { return state; }

private:
    uint64_t state;
};

} // namespace gmosh
