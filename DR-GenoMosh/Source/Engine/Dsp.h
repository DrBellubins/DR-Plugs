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
// The shared envelope (design doc 6.1.1, Rev 4 / plan milestone M4b).
//
// One clock, feeding both the amplitude and the modulation envelope. The shape
// is **a pluck with a volume decay over the repeats**, which is what the
// reference actually does; Rev 3's reading (one attack/window/decay trapezoid
// repeated at env_loop) is what made every plant pulse and click.
//
//   * The inner curve is a pluck: it rises to one peak and falls back to zero.
//     It never plateaus, and its fall has no gene - that fixed concave curve is
//     what makes the shape a pluck (deviation D-8).
//   * env_loop is how long that pluck takes, and how often it is re sounded.
//   * env_tilt is the *tilt of the pluck*: low is a regular pluck with the peak
//     at the start of the period, high rises slowly and then falls off a cliff.
//   * vol_atk bends the rise into the peak. Nothing bends the fall.
//   * The outer curve is a volume decay applied to the whole train of plucks:
//     env_time (scaled by env_kf) is how long that decay takes, vol_dcy is its
//     shape, vol_sus is the floor it settles at and vol_fade pulls that floor to
//     silence. env_time at or above 0.9 means no outer decay at all - the
//     plucks continue until key-up or until vol_fade closes them. Decision R3
//     and its kInfiniteShapeSeconds special case are retired: separating the
//     note's length from the curve's shape is what made that hack necessary.
//   * The modulation value is the *same pluck* (mod_atk / mod_dcy move its
//     peak position and its decay time relative to the level), scaled once by
//     VelSens x mod_vel in the mapping layer and optionally re-quantised by
//     mod_sh into a squared envelope that still follows the note.
//   * Note off (D-5): stop, then a fixed release from wherever the level is.
//
// Sample accurate, with no first order smoothing anywhere: the design asks for
// it (6.4), and an envelope that lags its own shape is an envelope that does
// not measure what the harness measures.
//==============================================================================

/// Everything the clock needs, resolved once per note in VoiceParams. Nothing
/// here is a gene: gates, rails and ranges are already applied.
struct EnvShape
{
    /// "No outer decay" as a number the sample loop can compare with. Kept
    /// finite on purpose: 1e9 s is far beyond any block, and no IEEE special
    /// case (inf, nan) is ever produced per sample.
    static constexpr float kInfiniteDuration = 1.0e9f;

    /// The pluck's fall is a fixed curve (deviation D-8): exponent < 1 is the
    /// concave "drops fast, long tail" of a plucked string. A gene here is what
    /// turned Rev 3's pluck into a linear window.
    static constexpr float kPluckFallExponent = 0.6f;

    /// How long a spent one-shot shape takes to fade in the level it settles on.
    static constexpr float kHoldFadeSeconds = 0.05f;

    /// T: how long the outer volume decay takes. "It never decays" is expressed
    /// as an outer floor of 1.0 (the curve cannot fall below it) and/or this
    /// number, which no block ever reaches - never an IEEE infinity, so the
    /// sample loop contains no special case.
    float noteSeconds = 1.0f;
    float periodSeconds = 1.0f;   ///< one pluck: env_loop, or T when there is no repeat
    bool  repeats = false;        ///< env_loop shorter than the note (4.2)

    float peak = 0.06f;           ///< env_tilt: where the pluck peaks in the period
    float riseExp = 1.0f;         ///< vol_atk curvature of the rise, 1 = linear

    float outerExp = 1.0f;        ///< vol_dcy curvature of the outer decay
    float outerFloor = 0.0f;      ///< vol_sus, pulled toward 0 by vol_fade

    float modPeak = 0.06f;        ///< the mod pluck's peak position (mod_atk offset)
    float modTimeScale = 1.0f;    ///< mod_dcy: mod pluck time relative to the level's

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
    /// about whether the note repeats and how long one pluck is; the clock's
    /// job is only to run it. That is deliberate: "env_time >= 0.9 means no
    /// outer decay" belongs to the mapping layer, where it can be tested against
    /// the gene table, and this class must not re-derive it a second way.
    void start (const EnvShape& s)
    {
        shape = s;

        period = clampf (shape.periodSeconds, 1.0e-4f, EnvShape::kInfiniteDuration);
        repeat = shape.repeats;

        periodInc = 1.0f / (period * sr);

        // The outer curve is counted in samples, not accumulated as a float:
        // "the note is over" has to be exact, and a sum of 24000 copies of
        // 1/24000 is not 1.0. kInfiniteDuration makes the count unreachable
        // (a soak's worth of samples is 1e8), which is exactly what it means.
        outerTotal = std::llround (std::max (shape.noteSeconds, 1.0e-4f) * sr);
        if (outerTotal < 1)
            outerTotal = 1;
        outerCount = 0;
        releaseInc = 1.0f / (std::max (shape.releaseSeconds, 1.0e-3f) * sr);

        // The mod pluck's own peak, kept inside the same period it shares.
        modPeak = clampf (shape.modPeak, 1.0e-3f, 0.999f);
        modScale = std::max (0.05f, shape.modTimeScale);

        pos = 0.0f;
        holdPos = 0.0f;
        holdInc = 1.0f / (EnvShape::kHoldFadeSeconds * sr);
        releasePos = 0.0f;
        amp = 0.0f;
        modVal = 0.0f;
        stage = Sounding;
    }

    /// Note off (D-5): stop repeating and ramp the level to zero over the fixed
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
                if (repeat)
                {
                    pos += periodInc;
                    if (pos >= 1.0f)
                        pos -= 1.0f;      // sound the pluck again
                }
                else
                {
                    if (pos < 1.0f)
                        pos = std::min (1.0f, pos + periodInc);
                    else
                        holdPos = std::min (1.0f, holdPos + holdInc);
                }

                if (outerCount < outerTotal)
                    ++outerCount;

                const float level = outerValue();
                // A note that was only ever going to pluck once has to end up
                // somewhere: with a floor under the outer curve it settles there,
                // faded in over a short window so the join cannot click. With no
                // floor there is nothing to settle on and the note is over.
                const float shapeValue = pos < 1.0f ? pluckAt (pos, shape.peak, shape.riseExp)
                                                   : holdPos;
                amp = shapeValue * level;
                // The modulator is the same curve on the same clock (6.1.1): it
                // has no shape of its own, only its own timing, and it follows
                // the note's level down so modulation cannot outlive its sound.
                modVal = (pos < 1.0f ? pluckAt (std::min (1.0f, pos * modScale), modPeak,
                                                shape.riseExp)
                                     : holdPos) * level;
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
    float periodOf() const { return period; }
    float positionOf() const { return pos; }
    float outerPositionOf() const { return outerPosition(); }
    float peakOf() const { return shape.peak; }
    float outerFloorOf() const { return shape.outerFloor; }

    /// True when the envelope can never come back without a new note: the outer
    /// decay is over and there is no floor to settle on. This is what lets a
    /// note that ended itself free its voice while the key is still held - and,
    /// unlike Rev 3, it is the *outer* curve that ends a note, never a pluck.
    bool finished() const
    {
        return stage == Idle
            || (stage == Sounding && outerDone() && shape.outerFloor <= 0.0f);
    }

    /// True when the note's length ran out while the key was still down.
    bool selfEnded() const { return stage == Sounding && outerDone(); }

    void reset() { stage = Idle; amp = 0.0f; modVal = 0.0f; pos = 0.0f;
                   holdPos = 0.0f; outerCount = 0; }

private:
    /// One pluck: rise to the peak with vol_atk's curvature, then fall to zero
    /// on the fixed curve. Both ends are exactly 0, so a train of them is a
    /// train of plucks and never a sustained tone.
    static float pluckAt (float p, float peak, float riseExp)
    {
        if (p <= 0.0f)
            return 0.0f;
        if (p >= 1.0f)
            return 0.0f;
        if (p < peak)
            return std::pow (p / peak, riseExp);
        return std::pow (1.0f - (p - peak) / (1.0f - peak), EnvShape::kPluckFallExponent);
    }

    bool outerDone() const { return outerCount >= outerTotal; }

    float outerPosition() const
    {
        // Clamped at 1.0 by construction, so pow() never sees a negative base.
        return std::min (1.0f, static_cast<float> (outerCount)
                                / static_cast<float> (outerTotal));
    }

    /// The outer curve: a level applied to the repeating pluck, decaying over
    /// env_time toward the (gated, fade-pulled) floor, with vol_dcy's
    /// curvature. 1.0 at the start of the note, floor at the end.
    float outerValue() const
    {
        const float f = clamp01 (shape.outerFloor);
        if (f >= 1.0f)
            return 1.0f;   // nothing to decay toward
        return f + (1.0f - f) * std::pow (1.0f - outerPosition (), shape.outerExp);
    }

    float sr = 44100.0f;
    EnvShape shape;
    float period = 1.0f, periodInc = 1.0f, releaseInc = 1.0f;
    long long outerTotal = 1, outerCount = 0;
    float modPeak = 0.06f, modScale = 1.0f;
    float pos = 0.0f, holdPos = 0.0f, holdInc = 1.0f, releasePos = 0.0f;
    float amp = 0.0f, modVal = 0.0f, releaseFrom = 0.0f, modAtRelease = 0.0f;
    bool repeat = false;
    Stage stage = Idle;
};

//==============================================================================
/// The three LFO shapes, named, for Modulator::setShape().
enum LfoShape : unsigned char { kLfoSine = 0, kLfoTriangle = 1, kLfoSampleHold = 2 };

/// Free running modulator: sine / triangle / sample and hold.
///
/// Since M4 the voice asks for the sine and keeps only a random *phase*: the LFO
/// is one documented source with one rate and two destinations (pitch and
/// tremolo), and a shape that changed per voice made the same patch sound like a
/// different synth on every key press (plan M4: "stop randomising the LFO shape
/// per voice (keep a phase offset)"). The other shapes stay, because the
/// 5.7.5 "swap which source drives a destination" trait is the one possible
/// future consumer, and an S&H LFO is exactly what it would want.
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
/// Sample and hold re-quantiser (design doc 4.2 mod_sh / b_sh, 6.1.1, 6.4).
///
/// Two documented destinations use one: mod_sh re-quantises the *modulation
/// envelope's output* ("optionally re-quantised by an S&H at rate mod_sh"), and
/// b_sh holds oscillator B. They are different objects of this class with
/// different rates, which is why it is a class rather than a branch in the voice.
///
/// 6.4 is the reason it exists on its own clock: "S&H outputs must not be
/// smoothed through their step - the step is the sound." So the step boundary is
/// reported to the caller (process() returns true) and the voice applies the
/// targets that depend on it with no ramp. Nothing here smooths, filters or
/// blends: a held value is a discontinuity by construction.
///
/// rate <= 0 means "off", which is how the mapping layer switches it (the
/// mod_sh gate is shut below its 0.75 rail, and b_sh is at its neutral 0).
class SampleHold
{
public:
    void setRateHz (float hz, double sampleRate)
    {
        inc = hz > 0.0f ? std::min (hz, static_cast<float> (sampleRate) * 0.45f)
                        / static_cast<float> (sampleRate)
                        : 0.0f;
    }

    /// A note on: the next sample primes the hold, so a new note never starts
    /// from a value left over from the last one.
    void reset() { phase = 0.0f; primed = false; held = 0.0f; }

    bool active() const { return inc > 0.0f; }

    /// Returns true when this sample began a new step.
    bool process (float in)
    {
        if (inc <= 0.0f)
            return false;

        phase += inc;
        if (! primed || phase >= 1.0f)
        {
            primed = true;
            phase -= std::floor (phase);
            held = in;
            return true;
        }
        return false;
    }

    float value() const { return held; }

private:
    float inc = 0.0f, phase = 0.0f, held = 0.0f;
    bool primed = false;
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
