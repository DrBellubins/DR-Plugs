#include "VoiceParams.h"
#include "../Genetic/Curves.h"
#include "Dsp.h"

namespace gmosh
{
namespace
{
    /// vol_atk / vol_dcy / mod_atk / mod_dcy are *curvature* controls in the
    /// Rev 3 table (4.2), while the ADSR that still runs until the shared
    /// envelope engine lands (M3) needs seconds. Neutral curvature therefore maps
    /// to a usable default time and the ends of the gene scale it by a factor.
    /// Provisional scaffolding on purpose: M3 deletes it together with Env.
    float provisionalSeconds (float curvature, float baseSeconds, float factor)
    {
        return baseSeconds * std::pow (factor, curvature); // curvature in -1..+1
    }
}

//==============================================================================
void resolveVoiceParams (const Genome& g, const BranchTraits& traits,
                         const MacroValues& macros, int midiNote, float velocity,
                         VoiceParams& p)
{
    p.tuneCents = macros.tuningCents;

    //------------------------------------------------------------------------
    // Envelope clock. envTime is the duration of the sound (4.2); until M3 the
    // old loop generator is what consumes it, hence the field names.
    p.envTime   = mapGene (g_env_time, g[g_env_time]);
    p.envRepeat = g[g_env_loop];              // stays the loop crossfade until M3
    p.envTilt   = mapGene (g_env_tilt, g[g_env_tilt]);
    p.envKeyFollow = mapGene (g_env_kf, g[g_env_kf]);   // octaves of time scaling, M3

    p.ampAttack  = provisionalSeconds (mapGene (g_vol_atk, g[g_vol_atk]), 0.02f, 8.0f);
    p.ampDecay   = provisionalSeconds (mapGene (g_vol_dcy, g[g_vol_dcy]), 0.35f, 8.0f);
    p.ampSustain = g[g_vol_sus];              // the < 0.5 gate is applied by M1
    p.ampRelease = kNoteOffReleaseSeconds;    // D-5: fixed note off release, no gene
    p.volFade    = g[g_vol_fade];             // read by M3, gated above 0.75

    p.modAttack  = provisionalSeconds (mapGene (g_mod_atk, g[g_mod_atk]), 0.006f, 8.0f);
    p.modDecay   = provisionalSeconds (mapGene (g_mod_dcy, g[g_mod_dcy]), 0.30f, 8.0f);
    // mod_sh is documented as a sample and hold rate (4.2); until M4 it stays
    // the mod envelope curve exponent it has always been, read from the raw
    // gene value so a branch sounds the way it did before the re-semantifying.
    p.modShape   = lerpf (0.3f, 3.0f, g[g_mod_sh]);
    p.modVelocity= g[g_mod_vel];

    p.lfoRate    = mapGene (g_lfo_rate, g[g_lfo_rate]);
    p.lfoAmount  = mapGene (g_lfo_amt,  g[g_lfo_amt]);   // bipolar, 0.5 = silent
    p.lfoBalance = g[g_lfo_bal];                         // 0 vibrato, .5 both, 1 tremolo
    p.lfoDelaySeconds = g[g_lfo_dly];                    // read by M4

    //------------------------------------------------------------------------
    // Oscillators. a_freq is the patch pitch and everything tonal follows it,
    // which is exactly why it is a stepped octave grid (5.7.5) and why the
    // filter cutoffs are ratios of it (5.7.2).
    p.aForm   = g[g_a_form];
    p.aColour = g[g_a_color];
    p.aNoise  = mapGene (g_a_noise, g[g_a_noise]);
    p.aRatio  = mapGene (g_a_freq, g[g_a_freq]);   // octaves, quantised by the table
    p.aMod    = mapGene (g_a_mod, g[g_a_mod]);

    p.bForm   = g[g_b_form];
    p.bNoise  = mapGene (g_b_noise, g[g_b_noise]);
    p.bRatio  = mapGene (g_b_freq, g[g_b_freq]);
    p.bMod    = mapGene (g_b_mod, g[g_b_mod]);
    p.bShape  = g[g_b_sh];

    // One shared colour source for both oscillators (design Q-7 as answered by
    // R1): the b_color extra is B's *offset* from it, silent at its neutral.
    p.bColour = clamp01 (p.aColour + 0.5f * mapGene (g_b_color, g[g_b_color]));

    p.balance   = g[g_osc_mix];               // the 0.79 equal power warp is M1
    p.fmMod     = mapGene (g_fm_mod, g[g_fm_mod]);
    // fm_amt is bipolar now (direction of the modulation); the direction itself
    // still comes from the structural trait, so only the depth is read here.
    p.fmAmount  = std::abs (mapGene (g_fm_amt, g[g_fm_amt]));
    p.mixMod    = mapGene (g_mix_mod, g[g_mix_mod]);   // read by M4
    p.subLevel  = g[g_sub_am];

    // Per voice life is not a gene any more (D-3): the amount is a named
    // constant, the per voice offsets stay random.
    p.detuneCents = kVoiceDetuneCents;
    p.driftAmount = kVoiceDriftAmount;

    //------------------------------------------------------------------------
    // Filter. The cutoff tracks A's pitch (4.2), which is the single thing that
    // keeps a branch's character across octaves.
    const float fA = noteToHz (midiNote, p.tuneCents)
                   * std::pow (2.0f, p.aRatio / 12.0f);
    const float cutoffOctaves = mapGene (g_flt_freq, g[g_flt_freq]);
    p.filterHz   = clampf (fA * std::pow (2.0f, cutoffOctaves), 20.0f, 20000.0f);
    p.filterQ    = 0.5f + 13.0f * g[g_flt_q];   // per topology caps arrive with M5
    p.filterTopology = g[g_flt_type];           // audible from M5 (R10)
    // Key follow: pre computed octaves of deviation for this note. The soft
    // rails of 5.7.4 belong to the gene and are applied by M5.
    p.keyFollow  = mapGene (g_flt_kf, g[g_flt_kf]) * static_cast<float> (midiNote - 60) / 12.0f;
    p.filterMod  = mapGene (g_flt_mod, g[g_flt_mod]);
    p.filterSep  = mapGene (g_flt_sep, g[g_flt_sep]);   // octaves around 0.5 = none

    p.drive = mapGene (g_saturate, g[g_saturate]);      // per voice stage after the filter: M6

    //------------------------------------------------------------------------
    // Reverb and output. The output adjusters are the player's, not the plant's
    // (kFlagNever), and nothing reads them until M7.
    p.fxSize   = lerpf (0.25f, 1.0f, g[g_rvb_size]);
    p.fxLength = mapGene (g_rvb_len, g[g_rvb_len]);
    p.fxDamp   = g[g_rvb_damp];
    p.fxChorus = g[g_rvb_chor];
    // The Effect macro scales the wet path (design doc 8).
    p.fxMix    = clamp01 (g[g_rvb_mix] * lerpf (0.25f, 1.25f, macros.effect));
    p.fxWidth  = g[g_fx_width];
    p.reverbBloomSeconds = g[g_rvb_atk];                // read by M7, gated above 0.66

    p.bassDb    = 24.0f * mapGene (g_adj_bass, g[g_adj_bass]);
    p.trebleDb  = 24.0f * mapGene (g_adj_treb, g[g_adj_treb]);
    p.panSpread = mapGene (g_adj_pan, g[g_adj_pan]);
    p.clipDrive = g[g_adj_clip];

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

    p.filterMode = traits.filterMode;
    p.fmDirection = traits.fmDirection;
    p.modSource = traits.modSource;
    p.oscBMode = traits.oscBMode;
}

} // namespace gmosh
