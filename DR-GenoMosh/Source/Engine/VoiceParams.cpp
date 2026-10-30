#include "VoiceParams.h"
#include "../Genetic/Curves.h"
#include "Dsp.h"

namespace gmosh
{
namespace
{
    /// The gene value at which the outer volume decay stops existing (design
    /// 6.1.1 Rev 4: "Above 0.9, the Envelope will have infinite duration,
    /// stopped only by releasing the key or vol_fade fades out"). Read from the
    /// table rather than written as a literal, so the two cannot drift apart.
    constexpr float kInfiniteDurationGene = 0.9f;

    /// The note has no length of its own, but vol_fade was asked to close it:
    /// how long that fade takes. Named, because it is the only way a note with
    /// env_time at 0.9 can ever end without a key lift, and a test asserts it.
    constexpr float kFadeOutWithoutDurationSeconds = 8.0f;

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

    /// env_tilt (-1..1) -> the peak position of a pluck inside its period.
    float pluckPeak (float tilt)
    {
        return tilt < 0.0f ? lerpf (kPluckPeakFast, kPluckPeakNeutral, tilt + 1.0f)
                           : lerpf (kPluckPeakNeutral, kPluckPeakSwell, tilt);
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
    // Levels first, because the envelope needs them: vol_sus is the floor the
    // outer volume decay settles on and vol_fade pulls that floor toward
    // silence (design 6.1.1 Rev 4). Both through their gate (5.7.3):
    // "vol_sus < 0.5 = none" is exactly the documented max(0, 2(v-0.5)), and
    // there is no second fade at all below 0.75.
    p.ampSustain = levelOf (g_vol_sus, g[g_vol_sus]);
    p.volFade    = levelOf (g_vol_fade, g[g_vol_fade]);
    p.ampRelease = kNoteOffReleaseSeconds;    // D-5: fixed note off release, no gene

    //------------------------------------------------------------------------
    // The shared envelope (design 6.1.1 Rev 4, M4b).
    //
    // Two layers, one clock, and only one of them is a shape:
    //
    //   the pluck   - env_loop is how long it takes and how often it is re
    //                 sounded; env_tilt is where its peak sits.
    //   the note    - env_time (scaled by env_kf) is a volume decay applied to
    //                 the whole train of plucks, settling on vol_sus's floor and
    //                 pulled toward silence by vol_fade.
    //
    // Above env_time 0.9 there is no note-length decay at all and the plucks
    // keep going until the key comes up: that is the whole of what "infinite
    // duration" means now, and it needs no time base, because a pluck already
    // carries its own (decision R3 and its fallback base are retired). What can
    // still close such a note is vol_fade, exactly as the reference says, so the
    // gate opening turns the endless case into a fade to (1 - fade) over a named
    // kFadeOutWithoutDurationSeconds; with the gate shut the floor is 1.0 and
    // the curve has nowhere to go.
    const float envGene = g[g_env_time];
    const bool noDecay = envGene >= kInfiniteDurationGene;

    const float kfOct = mapGene (g_env_kf, g[g_env_kf]);   // octaves of time
    const float keyScaleOct = clampf (-kfOct * static_cast<float> (midiNote - 60) / 12.0f,
                                      -6.0f, 6.0f);
    p.envKeyFollow = kfOct;
    p.envLoopSeconds = mapGene (g_env_loop, g[g_env_loop]);

    if (noDecay)
    {
        p.envRepeats = true;                    // an endless train of plucks
        p.envPeriodSeconds = p.envLoopSeconds;
        if (p.volFade > 0.0f)
        {
            p.envDuration    = kFadeOutWithoutDurationSeconds;
            p.envOuterFloor  = std::max (0.0f, 1.0f - p.volFade);
        }
        else
        {
            p.envDuration    = EnvShape::kInfiniteDuration;
            p.envOuterFloor  = 1.0f;            // nothing decays: stopped by key-up
        }
    }
    else
    {
        const float t = mapGene (g_env_time, envGene) * std::pow (2.0f, keyScaleOct);
        p.envDuration = clampf (t, 0.01f, EnvShape::kInfiniteDuration * 0.1f);
        // 4.2: a period at or beyond the note's length gives one pluck spanning
        // the note and no repeats at all.
        p.envRepeats = p.envLoopSeconds < p.envDuration;
        p.envPeriodSeconds = p.envRepeats ? p.envLoopSeconds : p.envDuration;
        p.envOuterFloor = lerpf (p.ampSustain, 0.0f, p.volFade);
    }

    p.envTilt = mapGene (g_env_tilt, g[g_env_tilt]);   // -1..1, pluck -> swell
    p.envPeakPosition = pluckPeak (p.envTilt);

    // Curvature, never seconds (4.2: "0.5 is linear"). vol_atk bends the rise
    // into the peak; vol_dcy bends the outer decay of the repeats (deviation
    // D-8: it has no say in the pluck's own fall, which is a fixed curve).
    p.pluckRiseExp  = curvatureExponent (g_vol_atk, g[g_vol_atk]);
    p.outerDecayExp = curvatureExponent (g_vol_dcy, g[g_vol_dcy]);
    // The modulation envelope is the same pluck with its own timing (6.1.1):
    // mod_atk moves its peak within the period, mod_dcy scales how fast it
    // decays compared with the level. Both are nothing at their neutral value.
    p.modPeakOffset = mapGene (g_mod_atk, g[g_mod_atk]) * kModPeakOffsetRange;
    p.modTimeScale  = curvatureExponent (g_mod_dcy, g[g_mod_dcy]);

    // mod_sh is a sample and hold rate (4.2), gated to the top quarter and
    // inverted: the gate opening gives the fastest rate and the top of the gene
    // the slowest. The voice re-quantises the mod value with it before any
    // destination reads it (6.1.1), which is why it is resolved here and not
    // derived in the DSP.
    p.modSampleHoldHz = gateOpen (kGenes[g_mod_sh], g[g_mod_sh])
                      ? mapGene (g_mod_sh, gated (kGenes[g_mod_sh], g[g_mod_sh]))
                      : 0.0f;
    p.modVelocity = levelOf (g_mod_vel, g[g_mod_vel]);  // "below a tenth, none"

    p.lfoRate    = mapGene (g_lfo_rate, g[g_lfo_rate]);
    // lfo_amt is bipolar: its magnitude is the depth and its sign is the
    // documented polarity (4.2: "above 0.5 = A and B modulated together, below
    // = in opposite directions"). Both halves come from the table's curve, so
    // the neutral value has no depth and nothing to polarity.
    const float lfoSigned = mapGene (g_lfo_amt, g[g_lfo_amt]);
    p.lfoDepth   = std::abs (lfoSigned);
    p.lfoSignB   = lfoSigned >= 0.0f ? 1.0f : -1.0f;
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
    // a_mod / b_mod are pitch modulation from the mod envelope (decision R2),
    // resolved here in semitones at the top of that envelope. Reading them as a
    // timbre control was the Rev 1 behaviour; the history of that mistake and
    // the test that replaced it are in plan 6.1.
    p.aModSemis = mapGene (g_a_mod, g[g_a_mod]) * kPitchModSemitoneCeiling;

    p.bForm   = g[g_b_form];
    p.bNoise  = mapGene (g_b_noise, g[g_b_noise]);
    p.bRatio  = mapGene (g_b_freq, g[g_b_freq]);
    p.bModSemis = mapGene (g_b_mod, g[g_b_mod]) * kPitchModSemitoneCeiling;  // as a_mod

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
    p.mixMod    = mapGene (g_mix_mod, g[g_mix_mod]);   // adds to balance, in mix space (M4)
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

    // b_sh, the second sample and hold (4.2), is a consumer of that link rather
    // than an independent rate: "below the middle the rate follows B's
    // frequency". Below 0.5 it is B divided by 8..1, which is what makes it the
    // same *timbral* effect at every pitch (5.7.5: deliberate quantisation,
    // "continuity is not a virtue where a continuous value would sound
    // unstable"); at and above the middle the rate is free, 0.5..16 Hz.
    //
    // The reference gives b_sh no dead zone, but its neutral value is 0 and
    // 5.7.1 requires neutral to be inaudible, so the hold crossfades in over
    // the first eighth of the gene instead of switching on at the first non zero
    // value. That is the only way a seed at b_sh = 0.02 is the instrument at
    // rest rather than a broken oscillator.
    const float bsh = clamp01 (g[g_b_sh]);
    p.bSampleHoldHz  = bsh < 0.5f ? p.freqBHz / lerpf (8.0f, 1.0f, bsh * 2.0f)
                                  : lerpf (0.5f, 16.0f, (bsh - 0.5f) * 2.0f);
    p.bSampleHoldMix = smoothStep (clamp01 (bsh / 0.125f));

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
