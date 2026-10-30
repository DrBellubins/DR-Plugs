#include "VoiceParams.h"
#include "Dsp.h"
#include "../Genetic/Curves.h"

namespace gmosh
{
//==============================================================================
// Envelope mapping.
//
// The gene table is left exactly as it is (its sensitivities, soft ranges and
// groups are what the genetic engine works on). What changes is only what the
// envelope genes *mean*: the four vol_*/env_* genes no longer describe an
// attack/decay/sustain trapezoid, they describe a pluck and a level decay over
// the repeats of that pluck.
//
// The direction of every gene is preserved, so a plant that used to be punchy
// is still punchy:
//
//   env_time  longer overall decay            (unchanged: it is a time)
//   env_loop  longer individual pluck         (was "loop amount": it is now the
//                                             period, and a period longer than
//                                             the note is one slow swell)
//   env_tilt  peak later in the cycle         (unchanged direction)
//   vol_atk   softer, more delayed rise       (was "attack time": in a pluck
//                                             there is no attack time, only how
//                                             the rise bends into the peak)
//   vol_dcy   later collapse of the repeats   (was "decay time": the length of
//                                             the decay is env_time, this only
//                                             bends it)
//   vol_sus   where the repeats settle        (unchanged: it is the level)
//   vol_rel   note off fade                 (unchanged)
//==============================================================================
namespace
{
    // At or above this env_time the note has no outer decay at all: the pluck
    // train runs at full level until the key is released.
    constexpr float kInfiniteDurationGene = 0.9f;

    // Where the pluck peaks in its cycle for the three ends of env_tilt.
    // Neutral must be a plain pluck, so only the top of the range gives the
    // "rise slowly, then fall off a cliff" shape.
    constexpr float kPluckPeakFast = 0.010f;
    constexpr float kPluckPeakNeutral = 0.060f;
    constexpr float kPluckPeakSwell = 0.950f;

    // The shortest and longest individual pluck the instrument will make.
    constexpr float kPluckPeriodFast = 0.030f;
    constexpr float kPluckPeriodSlow = 16.0f;

    // mod_sh is off until here, then a sample and hold on the modulator: fast
    // just past the gate, slower as the gene climbs.
    constexpr float kModHoldGate = 0.75f;
    constexpr float kModHoldFastHz = 8.0f;
    constexpr float kModHoldSlowHz = 0.1f;

    // Hard bounds of the mapping layer. The voice may never exceed these, which
    // is what keeps a wildly modulated branch playable rather than out of tune.
    constexpr float kPitchModSemitoneCeiling = 12.0f;
    constexpr float kVibratoSemitoneCeiling = 12.0f;
    constexpr float kTremoloDepthCeiling = 0.6f;

    float expRange (float lo, float hi, float v)
    {
        return lo * std::pow (hi / lo, clamp01 (v));
    }

    float peakPositionFor (float tilt /* -1 .. +1 */)
    {
        const float u = clamp01 (0.5f * (tilt + 1.0f));
        return u < 0.5f ? expRange (kPluckPeakFast,   kPluckPeakNeutral, u * 2.0f)
                        : expRange (kPluckPeakNeutral, kPluckPeakSwell,  (u - 0.5f) * 2.0f);
    }

    // 0.5 is a straight line, below 0.5 is front weighted (punchy), above is
    // soft and delayed.
    float curvatureExponent (float gene)
    {
        return std::exp2 (2.0f * (clamp01 (gene) - 0.5f));
    }
}

//==============================================================================
void resolveVoiceParams (const Genome& g, const BranchTraits& traits,
                         const MacroValues& macros, int midiNote, float velocity,
                         VoiceParams& p)
{
    //------------------------------------------------------------------------
    // The one envelope clock.
    const float envTimeGene = g[g_env_time];
    p.envShape.duration = envTimeGene >= kInfiniteDurationGene
                        ? 0.0f : mapGene (g_env_time, envTimeGene);
    p.envShape.period = expRange (kPluckPeriodFast, kPluckPeriodSlow, g[g_env_loop]);
    // A pluck longer than the note is one pluck spanning the note: without this
    // a slow env_loop would leave the voice rising for longer than the note
    // lasts, and the branch would never arrive. It keeps repeating at that
    // length afterwards, at the level the outer curve has settled to.
    if (p.envShape.duration > 0.0f && p.envShape.period > p.envShape.duration)
        p.envShape.period = p.envShape.duration;
    p.envShape.peakPos = peakPositionFor (mapGene (g_env_tilt, g[g_env_tilt]));
    p.envShape.riseExp = curvatureExponent (g[g_vol_atk]);
    // The outer curve is counted as "how much is left": 1 - q, bent by an
    // exponent. A high exponent spends the level early, a low one holds and then
    // collapses, which is the direction vol_dcy had as a decay time.
    p.envShape.outerExp = std::exp2 (2.0f * (0.5f - clamp01 (g[g_vol_dcy])));
    p.envShape.floorLevel = clamp01 (g[g_vol_sus]);
    p.envShape.release = mapGene (g_vol_rel, g[g_vol_rel]);

    // The modulator is the same curve on the same clock: mod_atk moves where it
    // peaks relative to the level, mod_dcy is how much of the level's fall
    // window it uses (a short window means the modulation is gone before the
    // pluck is). Both keep their old direction: a gene near zero was a fast
    // attack / fast decay, and still is.
    const float modPeakShift = (clamp01 (g[g_mod_atk]) * 2.0f - 1.0f) * 0.4f;
    p.envShape.modPeak = clampf (p.envShape.peakPos + modPeakShift, 0.005f, 0.95f);
    p.envShape.modFallScale = lerpf (0.15f, 1.0f, clamp01 (g[g_mod_dcy]));

    p.modHoldHz = g[g_mod_sh] > kModHoldGate
                ? expRange (kModHoldFastHz, kModHoldSlowHz,
                             (g[g_mod_sh] - kModHoldGate) / (1.0f - kModHoldGate))
                : 0.0f;

    p.lfoRate    = mapGene (g_lfo_rate, g[g_lfo_rate]);
    p.lfoAmount  = mapGene (g_lfo_amt,  g[g_lfo_amt]);
    p.lfoBalance = mapGene (g_lfo_bal,  g[g_lfo_bal]);

    // Vibrato and tremolo at one rate, split equal power. These are *free*:
    // nothing in the voice scales them by the envelope, which is what makes a
    // tremolo still audible in the middle of a slow note.
    const float lfoSplit = clamp01 (0.5f * (p.lfoBalance + 1.0f)); // 0 vib .. 1 trem
    p.vibSemis  = p.lfoAmount * std::cos (lfoSplit * 0.5f * kPi) * kVibratoSemitoneCeiling;
    p.tremDepth = p.lfoAmount * std::sin (lfoSplit * 0.5f * kPi) * kTremoloDepthCeiling;

    p.aForm   = g[g_a_form];
    p.aColour = g[g_a_color];
    p.aNoise  = mapGene (g_a_noise, g[g_a_noise]);
    // The oscillator offsets are already inside the atonality ceiling and on the
    // ratio ladder: that gate is part of generating the plant, not of playing
    // it (see Genetic/Mutation.h, applyAtonality).
    p.aRatio  = mapGene (g_a_freq, g[g_a_freq]);
    p.aModSemis = mapGene (g_a_mod, g[g_a_mod]) * kPitchModSemitoneCeiling;

    p.bForm   = g[g_b_form];
    p.bColour = g[g_b_color];
    p.bNoise  = mapGene (g_b_noise, g[g_b_noise]);
    p.bRatio  = mapGene (g_b_freq, g[g_b_freq]);
    p.bModSemis = mapGene (g_b_mod, g[g_b_mod]) * kPitchModSemitoneCeiling;
    p.bShape  = g[g_b_sh];

    p.balance   = g[g_balance];
    p.fmAmount  = mapGene (g_fm_amt, g[g_fm_amt]);
    p.fmMod     = mapGene (g_fm_mod, g[g_fm_mod]);
    p.subLevel  = g[g_sub_am];
    p.detuneCents = g[g_det_spread] * 30.0f;

    p.filterHz   = mapGene (g_flt_freq, g[g_flt_freq]);
    p.filterQ    = 0.5f + 13.0f * g[g_flt_q];
    // Pre computed: key follow in octaves of deviation for this note.
    p.keyFollow  = mapGene (g_flt_kf, g[g_flt_kf]) * static_cast<float> (midiNote - 60) / 12.0f;
    p.filterMod  = mapGene (g_flt_mod, g[g_flt_mod]);
    p.filterSep  = g[g_flt_sep] * 2.0f; // octaves between the two stages

    p.fxSize   = lerpf (0.25f, 1.0f, g[g_fx_size]);
    p.fxLength = mapGene (g_fx_len, g[g_fx_len]);
    p.fxDamp   = g[g_fx_damp];
    p.fxChorus = g[g_fx_chors];
    // The Effect macro scales the wet path (design doc 8).
    p.fxMix    = clamp01 (g[g_fx_mix] * lerpf (0.25f, 1.25f, macros.effect));
    p.fxWidth  = g[g_fx_width];

    p.driftAmount = g[g_drift_amt];
    p.drive       = mapGene (g_sat_drive, g[g_sat_drive]);

    p.tuneCents = macros.tuningCents;

    p.filterMode = traits.filterMode;
    p.fmDirection = traits.fmDirection;
    p.modSource = traits.modSource;
    p.oscBMode = traits.oscBMode;

    //------------------------------------------------------------------------
    // Velocity (design doc 4.1 VelSens + 4.2 mod_vel).
    const float vel = clamp01 (velocity);
    const float ampSens = clamp01 (macros.velSens);
    p.velocity = 1.0f - ampSens * (1.0f - vel);

    const float modSens = clamp01 (g[g_mod_vel]);
    p.modScale = 1.0f - modSens * (1.0f - vel);

    // Guard rail 5.6: level compensation so branches sit near the seed.
    p.gain = loudnessCompensation (g) * p.velocity;
}

} // namespace gmosh
