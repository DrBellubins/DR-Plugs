#pragma once

#include "../Genetic/Curves.h"
#include "../Genetic/Rng.h"

#include <cmath>
#include <vector>

namespace gmosh
{
//==============================================================================
// Small DSP building blocks used by the voice and the effects.
// All of them are allocation free after prepare() and sample rate aware
// (design doc section 10).
//==============================================================================

constexpr float kPi = 3.14159265358979323846f;

inline float softClip (float x)
{
    // Cheap odd symmetric saturator, unity slope at 0, bounded by +-1.
    return x / (1.0f + std::abs (x) * 0.65f);
}

inline float noteToHz (int midiNote, float centsOffset = 0.0f)
{
    return 440.0f * std::pow (2.0f, (static_cast<float> (midiNote) - 69.0f + centsOffset / 100.0f) / 12.0f);
}

//==============================================================================
class OnePole
{
public:
    void setHz (float hz, double sampleRate)
    {
        coef = 1.0f - std::exp (-2.0f * kPi * std::max (hz, 10.0f) / static_cast<float> (sampleRate));
    }

    float process (float x)
    {
        state += (x - state) * coef;
        return state;
    }

    void reset() { state = 0.0f; }
    float value() const { return state; }

private:
    float coef = 1.0f, state = 0.0f;
};

//==============================================================================
/// Trapezoidal (zero delay feedback) state variable filter.
///
/// Derived by digitising the classic coupled integrator SVF with the trapezoidal
/// rule and solving the resulting delay free loop:
///
///   low  = L + g*(band + B)
///   band = B + g*(hp + hpPrev)
///   hp   = x - band/Q - low
///
/// with g = tan(pi*fc/fs). Unlike the plain Chamberlin form this stays stable
/// and accurate right up to Nyquist, which matters because flt_freq goes to
/// 18 kHz.
class TrapezoidSVF
{
public:
    void setSampleRate (double sampleRate) { sr = static_cast<float> (sampleRate); reset(); }

    void setCutoffQ (float hz, float q)
    {
        const float nyq = sr * 0.5f;
        const float fc = std::min (std::max (hz, 10.0f), nyq * 0.96f);
        const float qq = std::max (q, 0.3f);

        g = std::tan (kPi * fc / sr);
        k = 1.0f / qq;

        const float gk = g * k;
        invGk = 1.0f / (1.0f + gk);
        invD  = 1.0f / (1.0f + gk + g * g);
        cL    = 1.0f + gk;
        cB    = g * (2.0f + gk);
        cX    = g * g;
    }

    void setDrive (float d) { drive = d; }

    void reset() { low = band = hpPrev = 0.0f; }

    /// modeMix: (lp, bp, hp) weights.
    float process (float x, float lpMix, float bpMix, float hpMix)
    {
        const float xh = x + hpPrev;

        float newLow  = (low * cL + band * cB + xh * cX) * invD;
        float newBand = (band + xh * g - newLow * g) * invGk;
        float newHp   = x - newBand * k - newLow;

        if (drive > 0.0f)
            newHp = newHp / (1.0f + drive * newHp * newHp * 0.5f);

        low = newLow;
        band = newBand;
        hpPrev = newHp;

        return newLow * lpMix + newBand * bpMix + newHp * hpMix;
    }

private:
    float sr = 44100.0f;
    float g = 0.0f, k = 0.0f, invGk = 1.0f, invD = 1.0f, cL = 1.0f, cB = 0.0f, cX = 0.0f;
    float drive = 0.0f;
    float low = 0.0f, band = 0.0f, hpPrev = 0.0f;
};

//==============================================================================
class DcBlocker
{
public:
    void setSampleRate (double sampleRate)
    {
        // One pole highpass at ~7 Hz: r = 1 - 2*pi*fc/fs.
        r = 1.0f - 2.0f * kPi * 7.0f / static_cast<float> (sampleRate);
    }

    float process (float x)
    {
        const float y = x - prevIn + r * prevOut;
        prevIn = x;
        prevOut = y;
        return y;
    }

    void reset() { prevIn = prevOut = 0.0f; }

private:
    float r = 0.995f, prevIn = 0.0f, prevOut = 0.0f;
};

//==============================================================================
/// Fractional delay line, linear interpolation, preallocated.
class VariableDelay
{
public:
    void prepare (int maxSamples)
    {
        buffer.assign (static_cast<std::size_t> (maxSamples) + 1, 0.0f);
        writePos = 0;
    }

    void reset() { for (float& s : buffer) s = 0.0f; writePos = 0; }

    void push (float x)
    {
        buffer[writePos] = x;
        if (++writePos >= buffer.size())
            writePos = 0;
    }

    /// delay in samples (>= 1).
    float read (float delay) const
    {
        const float n = static_cast<float> (size());
        float d = delay < 1.0f ? 1.0f : (delay > n - 2.0f ? n - 2.0f : delay);

        const float rp = static_cast<float> (writePos) - d;
        float i0f = std::floor (rp);
        const float frac = rp - i0f;
        auto i0 = static_cast<std::size_t> (wrap (i0f, n));
        auto i1 = static_cast<std::size_t> (wrap (i0f + 1.0f, n));

        return buffer[i0] * (1.0f - frac) + buffer[i1] * frac;
    }

    int size() const { return static_cast<int> (buffer.size()); }

private:
    static float wrap (float v, float n)
    {
        while (v < 0.0f) v += n;
        while (v >= n)   v -= n;
        return v;
    }

    std::vector<float> buffer;
    std::size_t writePos = 0;
};

//==============================================================================
// The shared envelope (design doc 6.1.1, plan milestone M3).
//
// One clock, feeding both the amplitude and the modulation envelope. This is
// the structure the reference describes; it replaces the Rev 1 ADSR, which had
// three unrelated time genes and no notion of "how long the note is".
//
//   * env_time *is* the duration: T = env_time x key follow, and env_time at
//     or above 0.9 means the *duration* is infinite (decision R3).
//   * The shape always needs a finite base (R3 again): a literal T = infinity
//     would freeze the clock at its start and the note would never open, which
//     is a bug rather than the reference's behaviour. The base is the loop
//     period when the envelope repeats, else T, else - only in the infinite,
//     non repeating case - the duration env_time = 0.9 maps to.
//   * env_loop repeats the shape; a period at or above the duration means no
//     repeats at all (4.2).
//   * vol_atk / vol_dcy / mod_atk / mod_dcy are curvature exponents of *this*
//     shape (1.0 = exactly linear), never seconds. env_tilt is where the shape
//     splits into attack and decay.
//   * vol_sus is the floor the decay aims at; vol_fade is the documented second
//     fade after the attack, and its destination is silence, which is how a note
//     with an infinite duration can still end by itself.
//   * Note off (D-5): stop looping, then a fixed release.
//
// Sample accurate, with no first order smoothing anywhere: the design asks for
// it (6.4), and with the duration being a gene, an envelope that lags its own
// shape is an envelope that does not measure what the harness measures.
//==============================================================================

/// Everything the clock needs, resolved once per note in VoiceParams. Nothing
/// here is a gene: gates, rails and ranges are already applied.
struct EnvShape
{
    /// "Infinite duration" as a number the sample loop can compare with. Kept
    /// finite on purpose: 1e9 s is far beyond any block, and no IEEE special
    /// case (inf, nan) is ever produced per sample.
    static constexpr float kInfiniteDuration = 1.0e9f;

    float duration = 1.0f;        ///< T in seconds, or kInfiniteDuration (R3)
    bool  repeats = false;        ///< env_loop shorter than the duration (4.2)
    float loopPeriod = 0.0f;      ///< re trigger interval, only when repeats
    float shapeSeconds = 1.0f;    ///< finite base of one pass of the shape (R3)
    float tilt = 0.0f;            ///< -1..1, fast attack at -1, slow attack at +1

    float attackExp = 1.0f;       ///< vol_atk curvature, 1 = linear
    float decayExp  = 1.0f;       ///< vol_dcy curvature, 1 = linear
    float modAttackExp = 1.0f;    ///< mod_atk curvature
    float modDecayExp  = 1.0f;    ///< mod_dcy curvature

    float sustain = 0.0f;         ///< max(0, 2(v-0.5)), the floor of the decay
    float fade    = 0.0f;         ///< vol_fade depth 0..1 (gate already applied)
    float releaseSeconds = 0.18f; ///< D-5: filled in from kNoteOffReleaseSeconds
};

class SharedEnv
{
public:
    enum Stage : unsigned char { Idle, Sounding, Release };

    void setSampleRate (double sampleRate) { sr = static_cast<float> (sampleRate); }

    /// Note on (and re articulation of a note that is already held).
    ///
    /// Every number here is resolved by VoiceParams, including the decision
    /// about whether the shape repeats and how long one pass of it is; the
    /// clock's job is only to run it. That is deliberate: the R3 reading of
    /// "env_time >= 0.9 means infinite duration" belongs to the mapping layer,
    /// where it can be tested against the gene table, and this class must not
    /// re-derive it a second way.
    void start (const EnvShape& s)
    {
        shape = s;

        repeat = shape.repeats && shape.loopPeriod > 1.0e-4f;

        // One pass of the shape is also one cycle of the clock: a repeat is a
        // whole envelope again, never a sliver of one (see VoiceParams).
        shapeSeconds = clampf (shape.shapeSeconds, 1.0e-4f, EnvShape::kInfiniteDuration);
        cycleSeconds = shapeSeconds;

        attackFraction = clampf (0.5f + shape.tilt * 0.45f, 0.02f, 0.98f);

        // vol_fade's second fade lands on silence, which is what lets an
        // "infinite" note close itself off without anyone lifting a key.
        fadeFloor = lerpf (shape.sustain, 0.0f, clamp01 (shape.fade));

        cycleInc = 1.0f / (cycleSeconds * sr);
        releaseInc = 1.0f / (std::max (shape.releaseSeconds, 1.0e-3f) * sr);

        pos = 0.0f;
        releasePos = 0.0f;
        amp = 0.0f;
        modVal = 0.0f;
        stage = Sounding;
    }

    /// Note off (D-5): stop looping and ramp the level to zero over the fixed
    /// release, from wherever it is right now (so this cannot click).
    void release()
    {
        if (stage == Idle)
            return;
        releaseFrom = amp;
        modAtRelease = modVal;
        releasePos = 0.0f;
        stage = Release;
    }

    float process()
    {
        switch (stage)
        {
            case Release:
            {
                releasePos += releaseInc;
                if (releasePos >= 1.0f) { releasePos = 1.0f; stage = Idle; }
                const float g = 1.0f - releasePos;
                amp = releaseFrom * g;
                modVal = modAtRelease * g;
                break;
            }

            case Sounding:
            {
                pos += cycleInc;
                if (pos >= 1.0f)
                {
                    if (repeat)
                        pos -= 1.0f;   // re trigger the same shape
                    else
                        pos = 1.0f;    // hold at sustain, or at the fade floor
                }
                amp = shapeAt (pos, shape.attackExp, shape.decayExp,
                               shape.sustain, fadeFloor);
                // The mod envelope shares the clock and the tilt and always
                // returns to nothing (6.1.1): it moves parameters, not level.
                modVal = shapeAt (pos, shape.modAttackExp, shape.modDecayExp, 0.0f, 0.0f);
                break;
            }

            case Idle:
            default:
                amp = 0.0f;
                modVal = 0.0f;
                break;
        }
        return amp;
    }

    float modValue() const { return modVal; }
    float ampValue() const { return amp; }
    bool  isRepeating() const { return repeat; }
    bool  sounding() const { return stage != Idle; }
    float shapeSecondsOf() const { return shapeSeconds; }
    float positionOf() const { return pos; }
    float attackFractionOf() const { return attackFraction; }
    float fadeFloorOf() const { return fadeFloor; }

    /// True when the envelope can never come back without a new note: the one
    /// shot ended at silence. This is what lets a note that ended itself (its
    /// duration is over with no sustain, or vol_fade closed it) free its voice
    /// while the key is still held.
    bool finished() const
    {
        return stage == Idle
            || (! repeat && stage == Sounding && pos >= 1.0f && fadeFloor <= 0.0f);
    }

    /// True when the shape completed while the key was still down.
    bool selfEnded() const { return stage == Sounding && ! repeat && pos >= 1.0f; }

    void reset() { stage = Idle; amp = 0.0f; modVal = 0.0f; pos = 0.0f; }

private:
    /// One pass of the shape: the attack power curve, then a decay aimed at
    /// `sus`, whose floor itself slides from sustain to fadeFloor across the
    /// decay (that slide is vol_fade). An exponent of 1 is exactly linear.
    float shapeAt (float p, float attackExp, float decayExp, float sus, float endFloor) const
    {
        if (p <= 0.0f)
            return 0.0f;
        if (p < attackFraction)
            return std::pow (p / attackFraction, attackExp);

        const float u = clamp01 ((p - attackFraction) / (1.0f - attackFraction));
        const float fl = lerpf (sus, endFloor, u);
        return fl + (1.0f - fl) * std::pow (1.0f - u, decayExp);
    }

    float sr = 44100.0f;
    EnvShape shape;
    float shapeSeconds = 1.0f, cycleSeconds = 1.0f;
    float cycleInc = 1.0f, releaseInc = 1.0f;
    float attackFraction = 0.5f, fadeFloor = 0.0f;
    float pos = 0.0f, releasePos = 0.0f;
    float amp = 0.0f, modVal = 0.0f, releaseFrom = 0.0f, modAtRelease = 0.0f;
    bool repeat = false;
    Stage stage = Idle;
};

//==============================================================================
/// Free running modulator: sine / triangle / sample and hold, chosen per voice.
class Modulator
{
public:
    void setRateHz (float hz, double sampleRate)
    {
        inc = std::min (hz, static_cast<float> (sampleRate) * 0.25f) / static_cast<float> (sampleRate);
    }

    void setShape (int shape, Rng& rng)
    {
        wave = static_cast<unsigned char> (shape % 3);
        holdValue = 0.0f;
        holdPhase = rng.uniform();
    }

    void setPhase (float p) { phase = p; }

    float process()
    {
        phase += inc;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            if (wave == 2) holdPhase = holdValue; // re-randomise per period
        }

        switch (wave)
        {
            case 1: // triangle
                return phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
            case 2: // sample and hold
                if (phase < inc) holdValue = holdPhase * 2.0f - 1.0f;
                return holdValue;
            case 0:
            default: // sine
                return std::sin (phase * 2.0f * kPi);
        }
    }

    void reset() { phase = 0.0f; }

private:
    float inc = 0.0f, phase = 0.0f, holdValue = 0.0f, holdPhase = 0.0f;
    unsigned char wave = 0;
};

//==============================================================================
/// Slow smoothed random walk: the "drift" that makes a patch feel alive
/// (design doc 7, voice level life).
class Drift
{
public:
    void prepare (double sampleRate, Rng& rng, float rateHz)
    {
        interval = static_cast<int> (sampleRate / std::max (rateHz, 0.02f));
        smooth = 1.0f - std::exp (-1.0f / (0.25f * static_cast<float> (sampleRate)));
        target = rng.range (-1.0f, 1.0f);
        counter = rng.integer (interval) + 1;
        value = target;
    }

    float process (Rng& rng)
    {
        if (--counter <= 0)
        {
            counter = interval;
            target = rng.gaussianClamped (1.0f);
        }
        value += (target - value) * smooth;
        return value;
    }

    float valueOf() const { return value; }

private:
    int interval = 1000, counter = 1000;
    float smooth = 0.001f, target = 0.0f, value = 0.0f;
};

} // namespace gmosh
