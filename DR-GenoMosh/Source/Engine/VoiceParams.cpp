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
    /// Provisional scaffolding on purpose: M3 deletes it together with Env, when
    /// these four become exponents of a shape that has no seconds in it at all.
    float provisionalSeconds (float curvature, float baseSeconds, float factor)
    {
        return baseSeconds * std::pow (factor, curvature); // curvature in -1..+1
    }

    /// mapGene() through the gene's activation gate (5.7.3). Reading a gated
    /// gene without this is how a dead zone ends up "half on", which is the one
    /// thing 5.7.3 exists to prevent.
    float levelOf (std::size_t gene, float norm)
    {
        return mappedGated (kGenes[gene], norm);
    }
}

//==============================================================================
void resolveVoiceParams (const Genome& g, const BranchTraits& traits,
                         const MacroValues& macros, int midiNote, float velocity,
                         VoiceParams& p)
{
    p.tuneCents = macros.tuningCents;

    // The Effect macro is a link (5.7.2): one value scales the wet path and the
    // branch pan spread together, so a patch does not lose its width when it
    // loses its reverb. M7 gives it the limiter half of its job.
    const float effectScale = lerpf (0.25f, 1.25f, clamp01 (macros.effect));

    //------------------------------------------------------------------------
    // Envelope clock. envTime is the duration of the sound (4.2); until M3 the
    // old loop generator is what consumes it, hence the field names.
    p.envTime   = mapGene (g_env_time, g[g_env_time]);
    p.envRepeat = g[g_env_loop];              // stays the loop crossfade until M3
    p.envTilt   = mapGene (g_env_tilt, g[g_env_tilt]);
    p.envKeyFollow = mapGene (g_env_kf, g[g_env_kf]);   // octaves of time scaling, M3

    p.ampAttack  = provisionalSeconds (mapGene (g_vol_atk, g[g_vol_atk]), 0.02f, 8.0f);
    p.ampDecay   = provisionalSeconds (mapGene (g_vol_dcy, g[g_vol_dcy]), 0.35f, 8.0f);
    // 5.7.3: "vol_sus < 0.5 = none". Read through the gate this is the
    // documented max(0, 2(v-0.5)) of 6.1.1, and the neutral value is silence.
    p.ampSustain = levelOf (g_vol_sus, g[g_vol_sus]);
    p.ampRelease = kNoteOffReleaseSeconds;    // D-5: fixed note off release, no gene
    p.volFade    = levelOf (g_vol_fade, g[g_vol_fade]); // no second fade below 0.75; read by M3

    p.modAttack  = provisionalSeconds (mapGene (g_mod_atk, g[g_mod_atk]), 0.006f, 8.0f);
    p.modDecay   = provisionalSeconds (mapGene (g_mod_dcy, g[g_mod_dcy]), 0.30f, 8.0f);
    // mod_sh is a sample and hold rate (4.2), gated to the top quarter and
    // inverted: the gate opening gives the fastest rate and the top of the gene
    // the slowest. Nothing samples yet - M4 owns the destination - so the mod
    // envelope's curve exponent is left at the identity rather than being driven
    // by a gene that does not mean "curve" any more.
    p.modShape   = 1.0f;
    p.modSampleHoldHz = gateOpen (kGenes[g_mod_sh], g[g_mod_sh])
                      ? mapGene (g_mod_sh, gated (kGenes[g_mod_sh], g[g_mod_sh]))
                      : 0.0f;
    p.modVelocity = levelOf (g_mod_vel, g[g_mod_vel]);  // "below a tenth, none"

    p.lfoRate    = mapGene (g_lfo_rate, g[g_lfo_rate]);
    p.lfoAmount  = mapGene (g_lfo_amt,  g[g_lfo_amt]);   // bipolar, 0.5 = silent
    p.lfoBalance = g[g_lfo_bal];                         // 0 vibrato, .5 both, 1 tremolo
    p.lfoDelaySeconds = levelOf (g_lfo_dly, g[g_lfo_dly]); // no onset delay below 0.5

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
    p.bShape  = g[g_b_sh];                          // an S&H rate from M4

    // One shared colour source for both oscillators (design Q-7 as answered by
    // R1): the b_color extra is B's *offset* from it, silent at its neutral.
    p.bColour = clamp01 (p.aColour + 0.5f * mapGene (g_b_color, g[g_b_color]));

    // 5.7.7: the reference's 50/50 sits at 0.7937, not at the middle of the
    // gene. Warp into mix space here so the voice's equal power law is the
    // symmetric one and mix_mod can share it (M4).
    p.balance   = equalPowerMixSpace (g[g_osc_mix]);
    p.fmMod     = mapGene (g_fm_mod, g[g_fm_mod]);
    // fm_amt is bipolar now (direction of the modulation); the direction itself
    // still comes from the structural trait, so only the depth is read here.
    p.fmAmount  = std::abs (mapGene (g_fm_amt, g[g_fm_amt]));
    p.mixMod    = mapGene (g_mix_mod, g[g_mix_mod]);   // read by M4
    p.subLevel  = mapGene (g_sub_am, g[g_sub_am]);

    // Per voice life is not a gene any more (D-3): the amount is a named
    // constant, the per voice offsets stay random.
    p.detuneCents = kVoiceDetuneCents;
    p.driftAmount = kVoiceDriftAmount;

    //------------------------------------------------------------------------
    // Links (5.7.2), resolved once. A's pitch is the note plus the octave grid,
    // B is A plus its offset, and both filter cutoffs are ratios of A rather
    // than absolute hertz - that is what keeps a branch's character at every
    // octave. Everything downstream reads these numbers.
    p.freqAHz = noteToHz (midiNote, p.tuneCents) * std::pow (2.0f, p.aRatio / 12.0f);
    p.freqBHz = p.freqAHz * std::pow (2.0f, p.bRatio / 12.0f);

    const float cutoffOctaves = mapGene (g_flt_freq, g[g_flt_freq]);
    p.filterHz = clampf (p.freqAHz * std::pow (2.0f, cutoffOctaves), 20.0f, 20000.0f);
    // 5.7.2 again: the second cutoff is a ratio of the first. The voice keeps
    // them locked together while modulation moves the pair.
    p.filterSep = mapGene (g_flt_sep, g[g_flt_sep]);   // octaves, 0.5 = no separation
    p.cutoff2Hz = clampf (p.filterHz * std::pow (2.0f, p.filterSep), 20.0f, 20000.0f);

    p.filterQ    = 0.5f + 13.0f * mapGene (g_flt_q, g[g_flt_q]); // per topology caps: M5
    p.filterTopology = mapGene (g_flt_type, g[g_flt_type]);       // 4 steps, from the table
    // 5.7.4: "nothing below 0.25, exact above 0.75". The rails are a 0..1
    // strength multiplied into the signed amount, so both ends of the gene go
    // quiet smoothly instead of being clamped. Note 60 is the reference pitch,
    // so the deviation for this note is pre computed here rather than per
    // modulation tick in the voice.
    const float kfRails = softRails (g[g_flt_kf], kGenes[g_flt_kf].gateLow,
                                     kGenes[g_flt_kf].gateHigh);
    p.keyFollow  = mapGene (g_flt_kf, g[g_flt_kf]) * kfRails
                 * static_cast<float> (midiNote - 60) / 12.0f;
    p.filterMod  = mapGene (g_flt_mod, g[g_flt_mod]);

    p.drive = mapGene (g_saturate, g[g_saturate]);      // per voice stage after the filter: M6

    //------------------------------------------------------------------------
    // Reverb and output. The output adjusters are the player's, not the plant's
    // (kFlagNever), and nothing reads them until M7.
    p.fxSize   = lerpf (0.25f, 1.0f, g[g_rvb_size]);
    p.fxLength = mapGene (g_rvb_len, g[g_rvb_len]);
    p.fxDamp   = g[g_rvb_damp];
    p.fxChorus = mapGene (g_rvb_chor, g[g_rvb_chor]);
    p.fxMix    = clamp01 (mapGene (g_rvb_mix, g[g_rvb_mix]) * effectScale);
    p.fxWidth  = g[g_fx_width];
    p.reverbBloomSeconds = levelOf (g_rvb_atk, g[g_rvb_atk]); // no bloom below 0.66; read by M7

    p.bassDb    = 24.0f * mapGene (g_adj_bass, g[g_adj_bass]);
    p.trebleDb  = 24.0f * mapGene (g_adj_treb, g[g_adj_treb]);
    p.panSpread = mapGene (g_adj_pan, g[g_adj_pan]) * effectScale;
    p.clipDrive = mapGene (g_adj_clip, g[g_adj_clip]);

    //------------------------------------------------------------------------
    // Velocity (design doc 4.1 VelSens + 4.2 mod_vel). The product of the two
    // is the link of 5.7.2, and it is applied to the *modulation amounts*, never
    // to the genes, so a voice keeps its own copy of what it was played with.
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
