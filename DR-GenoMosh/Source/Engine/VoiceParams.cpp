#include "VoiceParams.h"
#include "../Genetic/Curves.h"
#include "Dsp.h"

namespace gmosh
{
namespace
{
    /// The gene value at which env_time stops being a duration (decision R3:
    /// "Above 0.9, the Envelope will have infinite duration, stopped only by
    /// releasing the key or vol_fade fades out"). Read from the table rather
    /// than written as a literal, so the two cannot drift apart.
    constexpr float kInfiniteDurationGene = 0.9f;

    /// The shape time base for an infinite duration with no repeats: the
    /// duration that env_time = 0.9 maps to. A literal phase += dt/inf freezes
    /// the shape at its start and the note never opens, so "infinite duration"
    /// has to mean "no end to the note", never "no end to the shape".
    float infiniteShapeSeconds()
    {
        return mapGene (kGenes[g_env_time], kInfiniteDurationGene);
    }

    /// mapGene() through the gene's activation gate (5.7.3). Reading a gated
    /// gene without this is how a dead zone ends up "half on", which is the one
    /// thing 5.7.3 exists to prevent.
    float levelOf (std::size_t gene, float norm)
    {
        return mappedGated (kGenes[gene], norm);
    }

    /// A curvature gene into an exponent: 4.2 asks for 2^(2(v-0.5)), so the
    /// neutral value is exactly linear, the bottom of the range is front
    /// weighted and the top is back weighted, and the curve never inverts.
    float curvatureExponent (std::size_t gene, float norm)
    {
        const float m = mapGene (kGenes[gene], norm);   // Bipolar, -1..+1
        return std::exp2 (m);
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
    // The shared envelope clock (design 6.1.1, M3).
    //
    // env_time is the duration of the sound and env_kf scales it by key, so the
    // voice's whole notion of "how long" is decided here, once, in seconds.
    // Above 0.9 the *duration* is infinite while the shape still needs a finite
    // base, which is decision R3, and the whole of it lives in these lines:
    //
    //   env_loop < duration  -> the shape repeats, one whole envelope per
    //                           period. "Infinite" is then heard as an endless
    //                           sweep, exactly as the reference describes it.
    //   env_loop >= duration -> one pass, no repeats. For an infinite duration
    //                           that pass takes what env_time = 0.9 maps to
    //                           (infiniteShapeSeconds), then holds at sustain -
    //                           or closes to silence under vol_fade, which is
    //                           the only way such a note ends by itself.
    //
    // A repeat takes the whole shape rather than a slice of it: the design
    // sketch let a short period truncate the shape, which makes env_time and
    // env_loop fight over the same envelope and leaves a fast period near
    // silence. The note is still the shape env_time asks for; env_loop only
    // decides how often it is re sounded.
    const float envGene = g[g_env_time];
    const bool infinite = envGene >= kInfiniteDurationGene;

    const float kfOct = mapGene (g_env_kf, g[g_env_kf]);   // octaves of time
    const float keyScaleOct = clampf (-kfOct * static_cast<float> (midiNote - 60) / 12.0f,
                                      -6.0f, 6.0f);
    p.envKeyFollow = kfOct;
    p.envLoopSeconds = mapGene (g_env_loop, g[g_env_loop]);

    if (infinite)
    {
        p.envDuration = EnvShape::kInfiniteDuration;
        p.envRepeats  = p.envLoopSeconds < infiniteShapeSeconds();
        p.envShapeSeconds = p.envRepeats ? p.envLoopSeconds : infiniteShapeSeconds();
    }
    else
    {
        const float t = mapGene (g_env_time, envGene) * std::pow (2.0f, keyScaleOct);
        p.envDuration = clampf (t, 0.01f, EnvShape::kInfiniteDuration * 0.1f);
        p.envRepeats = p.envLoopSeconds < p.envDuration;
        p.envShapeSeconds = p.envRepeats ? p.envLoopSeconds : p.envDuration;
    }

    p.envTilt = mapGene (g_env_tilt, g[g_env_tilt]);   // -1..1 attack/decay skew

    // Curvature, never seconds (4.2: "0.5 is linear").
    p.ampAttackExp = curvatureExponent (g_vol_atk, g[g_vol_atk]);
    p.ampDecayExp  = curvatureExponent (g_vol_dcy, g[g_vol_dcy]);
    p.modAttackExp = curvatureExponent (g_mod_atk, g[g_mod_atk]);
    p.modDecayExp  = curvatureExponent (g_mod_dcy, g[g_mod_dcy]);

    // 5.7.3: "vol_sus < 0.5 = none". Read through the gate this is exactly the
    // documented max(0, 2(v-0.5)) of 6.1.1, and the neutral value is silence.
    p.ampSustain = levelOf (g_vol_sus, g[g_vol_sus]);
    p.ampRelease = kNoteOffReleaseSeconds;    // D-5: fixed note off release, no gene
    // No second fade below 0.75 (4.2); above it, the depth of the droop that
    // closes a note which has no duration of its own.
    p.volFade    = levelOf (g_vol_fade, g[g_vol_fade]);

    // mod_sh is a sample and hold rate (4.2), gated to the top quarter and
    // inverted: the gate opening gives the fastest rate and the top of the gene
    // the slowest. Nothing samples yet - M4 owns the destination.
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

    // 6.1.1: the mod envelope is what velocity scales - one place, which is
    // also what makes mix_mod/fm_mod/a_mod share the same velocity curve when
    // M4 wires them up. The destination amounts stay raw and bipolar.
    const float modSens = clamp01 (p.modVelocity);
    p.modScale = 1.0f - modSens * (1.0f - vel);

    // Guard rail 5.6: level compensation so branches sit near the seed.
    p.gain = loudnessCompensation (g) * p.velocity;

    p.fmDirection = traits.fmDirection;
    p.oscBMode = traits.oscBMode;
}

} // namespace gmosh
