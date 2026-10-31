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

/// An exponential ramp between two positive ends, driven by a normalized value.
float expRange(float lowValue, float highValue, float normalized)
{
    return lowValue * std::pow(highValue / lowValue, clamp01(normalized));
}

/// Maps env_tilt (-1 .. +1) onto where the pluck peaks in its cycle.
float peakPositionFor(float tilt)
{
    const float normalizedTilt = clamp01(0.5f * (tilt + 1.0f));

    return normalizedTilt < 0.5f
               ? expRange(kPluckPeakFast, kPluckPeakNeutral, normalizedTilt * 2.0f)
               : expRange(kPluckPeakNeutral, kPluckPeakSwell, (normalizedTilt - 0.5f) * 2.0f);
}

// 0.5 is a straight line, below 0.5 is front weighted (punchy), above is
// soft and delayed.
float curvatureExponent(float gene) { return std::exp2(2.0f * (clamp01(gene) - 0.5f)); }
} // namespace

//==============================================================================
void resolveVoiceParams(const Genome& genome, const BranchTraits& traits, const MacroValues& macros,
                        int midiNote, float velocity, VoiceParams& params)
{
    //------------------------------------------------------------------------
    // The one envelope clock.
    const float envTimeGene = genome[g_env_time];

    params.envShape.Duration = envTimeGene >= kInfiniteDurationGene ? 0.0f : mapGene(g_env_time, envTimeGene);
    params.envShape.Period = expRange(kPluckPeriodFast, kPluckPeriodSlow, genome[g_env_loop]);

    // A pluck longer than the note is one pluck spanning the note: without this
    // a slow env_loop would leave the voice rising for longer than the note
    // lasts, and the branch would never arrive. It keeps repeating at that
    // length afterwards, at the level the outer curve has settled to.
    if (params.envShape.Duration > 0.0f && params.envShape.Period > params.envShape.Duration)
        params.envShape.Period = params.envShape.Duration;

    params.envShape.PeakPos = peakPositionFor(mapGene(g_env_tilt, genome[g_env_tilt]));
    params.envShape.RiseExp = curvatureExponent(genome[g_vol_atk]);

    // The outer curve is counted as "how much is left": 1 - q, bent by an
    // exponent. A high exponent spends the level early, a low one holds and then
    // collapses, which is the direction vol_dcy had as a decay time.
    params.envShape.OuterExp = std::exp2(2.0f * (0.5f - clamp01(genome[g_vol_dcy])));
    params.envShape.FloorLevel = clamp01(genome[g_vol_sus]);
    params.envShape.Release = mapGene(g_vol_rel, genome[g_vol_rel]);

    // The modulator is the same curve on the same clock: mod_atk moves where it
    // peaks relative to the level, mod_dcy is how much of the level's fall
    // window it uses (a short window means the modulation is gone before the
    // pluck is). Both keep their old direction: a gene near zero was a fast
    // attack / fast decay, and still is.
    const float modPeakShift = (clamp01(genome[g_mod_atk]) * 2.0f - 1.0f) * 0.4f;
    params.envShape.ModPeak = clampf(params.envShape.PeakPos + modPeakShift, 0.005f, 0.95f);
    params.envShape.ModFallScale = lerpf(0.15f, 1.0f, clamp01(genome[g_mod_dcy]));

    params.modHoldHz = genome[g_mod_sh] > kModHoldGate
                           ? expRange(kModHoldFastHz, kModHoldSlowHz,
                                      (genome[g_mod_sh] - kModHoldGate) / (1.0f - kModHoldGate))
                           : 0.0f;

    params.lfoRate = mapGene(g_lfo_rate, genome[g_lfo_rate]);
    params.lfoAmount = mapGene(g_lfo_amt, genome[g_lfo_amt]);
    params.lfoBalance = mapGene(g_lfo_bal, genome[g_lfo_bal]);

    // Vibrato and tremolo at one rate, split equal power. These are *free*:
    // nothing in the voice scales them by the envelope, which is what makes a
    // tremolo still audible in the middle of a slow note.
    // 0 is all vibrato, 1 is all tremolo.
    const float lfoSplit = clamp01(0.5f * (params.lfoBalance + 1.0f));
    params.vibSemis = params.lfoAmount * std::cos(lfoSplit * 0.5f * kPi) * kVibratoSemitoneCeiling;
    params.tremDepth = params.lfoAmount * std::sin(lfoSplit * 0.5f * kPi) * kTremoloDepthCeiling;

    params.aForm = genome[g_a_form];
    params.aColour = genome[g_a_color];
    params.aNoise = mapGene(g_a_noise, genome[g_a_noise]);

    // The oscillator offsets are already inside the atonality ceiling and on the
    // ratio ladder: that gate is part of generating the plant, not of playing
    // it (see Genetic/Mutation.h, applyAtonality).
    params.aRatio = mapGene(g_a_freq, genome[g_a_freq]);
    params.aModSemis = mapGene(g_a_mod, genome[g_a_mod]) * kPitchModSemitoneCeiling;

    params.bForm = genome[g_b_form];
    params.bColour = genome[g_b_color];
    params.bNoise = mapGene(g_b_noise, genome[g_b_noise]);
    params.bRatio = mapGene(g_b_freq, genome[g_b_freq]);
    params.bModSemis = mapGene(g_b_mod, genome[g_b_mod]) * kPitchModSemitoneCeiling;
    params.bShape = genome[g_b_sh];

    params.balance = genome[g_balance];
    params.fmAmount = mapGene(g_fm_amt, genome[g_fm_amt]);
    params.fmMod = mapGene(g_fm_mod, genome[g_fm_mod]);
    params.subLevel = genome[g_sub_am];
    params.detuneCents = genome[g_det_spread] * 30.0f;

    params.filterHz = mapGene(g_flt_freq, genome[g_flt_freq]);
    params.filterQ = 0.5f + 13.0f * genome[g_flt_q];

    // Pre computed: key follow in octaves of deviation for this note.
    params.keyFollow = mapGene(g_flt_kf, genome[g_flt_kf]) * static_cast<float>(midiNote - 60) / 12.0f;
    params.filterMod = mapGene(g_flt_mod, genome[g_flt_mod]);
    params.filterSep = genome[g_flt_sep] * 2.0f; // octaves between the two stages

    params.fxSize = lerpf(0.25f, 1.0f, genome[g_fx_size]);
    params.fxLength = mapGene(g_fx_len, genome[g_fx_len]);
    params.fxDamp = genome[g_fx_damp];
    params.fxChorus = genome[g_fx_chors];

    // The Effect macro scales the wet path (design doc 8).
    params.fxMix = clamp01(genome[g_fx_mix] * lerpf(0.25f, 1.25f, macros.effect));
    params.fxWidth = genome[g_fx_width];

    params.driftAmount = genome[g_drift_amt];
    params.drive = mapGene(g_sat_drive, genome[g_sat_drive]);

    params.tuneCents = macros.tuningCents;

    params.filterMode = traits.filterMode;
    params.fmDirection = traits.fmDirection;
    params.modSource = traits.modSource;
    params.oscBMode = traits.oscBMode;

    //------------------------------------------------------------------------
    // Velocity (design doc 4.1 VelSens + 4.2 mod_vel).
    const float velocityValue = clamp01(velocity);
    const float amplitudeSensitivity = clamp01(macros.velSens);
    params.velocity = 1.0f - amplitudeSensitivity * (1.0f - velocityValue);

    const float modulationSensitivity = clamp01(genome[g_mod_vel]);
    params.modScale = 1.0f - modulationSensitivity * (1.0f - velocityValue);

    // Guard rail 5.6: level compensation so branches sit near the seed.
    params.gain = loudnessCompensation(genome) * params.velocity;
}

} // namespace gmosh
