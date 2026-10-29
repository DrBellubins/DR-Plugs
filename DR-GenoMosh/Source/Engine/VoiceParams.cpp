#include "VoiceParams.h"
#include "../Genetic/Curves.h"

namespace gmosh
{
//==============================================================================
void resolveVoiceParams (const Genome& g, const BranchTraits& traits,
                         const MacroValues& macros, int midiNote, float velocity,
                         VoiceParams& p)
{
    p.ampAttack  = mapGene (g_vol_atk,  g[g_vol_atk]);
    p.ampDecay   = mapGene (g_vol_dcy,  g[g_vol_dcy]);
    p.ampSustain = g[g_vol_sus];
    p.ampRelease = mapGene (g_vol_rel,  g[g_vol_rel]);

    p.modAttack  = mapGene (g_mod_atk,  g[g_mod_atk]);
    p.modDecay   = mapGene (g_mod_dcy,  g[g_mod_dcy]);
    // mod_sh is the shape of the mod envelope curve: <1 front weighted
    // (percussive), >1 back weighted (swelling).
    p.modShape   = lerpf (0.3f, 3.0f, g[g_mod_sh]);
    p.modVelocity= g[g_mod_vel];

    p.loopTime   = mapGene (g_env_time, g[g_env_time]);
    p.loopAmount = g[g_env_loop];
    p.loopTilt   = mapGene (g_env_tilt, g[g_env_tilt]);

    p.lfoRate    = mapGene (g_lfo_rate, g[g_lfo_rate]);
    p.lfoAmount  = mapGene (g_lfo_amt,  g[g_lfo_amt]);
    p.lfoBalance = mapGene (g_lfo_bal,  g[g_lfo_bal]);

    p.aForm   = g[g_a_form];
    p.aColour = g[g_a_color];
    p.aNoise  = mapGene (g_a_noise, g[g_a_noise]);
    // The oscillator offsets are already inside the atonality ceiling and on the
    // ratio ladder: that gate is part of generating the plant, not of playing
    // it (see Genetic/Mutation.h, applyAtonality).
    p.aRatio  = mapGene (g_a_freq, g[g_a_freq]);
    p.aMod    = mapGene (g_a_mod, g[g_a_mod]);

    p.bForm   = g[g_b_form];
    p.bColour = g[g_b_color];
    p.bNoise  = mapGene (g_b_noise, g[g_b_noise]);
    p.bRatio  = mapGene (g_b_freq, g[g_b_freq]);
    p.bMod    = mapGene (g_b_mod, g[g_b_mod]);
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

    const float modSens = clamp01 (p.modVelocity);
    const float modScale = 1.0f - modSens * (1.0f - vel);
    p.aMod *= modScale;
    p.bMod *= modScale;
    p.fmMod *= modScale;
    p.filterMod *= modScale;

    // Guard rail 5.6: level compensation so branches sit near the seed.
    p.gain = loudnessCompensation (g) * p.velocity;
}

} // namespace gmosh
