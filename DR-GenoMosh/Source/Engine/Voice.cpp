#include <cstdio>
#include "Voice.h"
#include "../Genetic/Rng.h"

#include <algorithm>

namespace gmosh
{
namespace
{
    float clampFreq (float f, float sampleRate)
    {
        return clampf (f, 0.5f, static_cast<float> (sampleRate) * 0.45f);
    }

    float centsToRatio (float cents) { return std::pow (2.0f, cents / 1200.0f); }

    // The vibrato ceiling at full lfo_amt. 4.2 gives that gene no documented
    // bound, unlike a_mod / b_mod, so this number is ours: deliberately well
    // below kPitchModSemitoneCeiling, because a whole tone of LFO vibrato is a
    // special effect while an octave of envelope pitch sweep is the documented
    // "wide" one. Atonality bounds the gene first (it is kFlagPitch); this
    // bounds what a fully open gene can do.
    constexpr float kVibratoSemitoneCeiling = 2.0f;
}

//==============================================================================
void Voice::prepare (double sampleRateHz, int /*maxBlockSamples*/)
{
    sampleRate = sampleRateHz;
    env.setSampleRate (sampleRateHz);
    filter1.setSampleRate (sampleRateHz);
    filter2.setSampleRate (sampleRateHz);
    dcBlock.setSampleRate (sampleRateHz);
}

//==============================================================================
void Voice::start (const VoiceParams& params, int midiNote, uint64_t plantHash)
{
    p = params;
    note = midiNote;
    active = true;
    startedAt = sampleCount;
    peak = 0.0f;

    // Per voice variation: a fresh, note and time dependent stream (design
    // doc 7 "voice level life").
    rng = Rng (hash::combine (plantHash,
                              static_cast<uint64_t> (midiNote) * uint64_t{0x9E3779B97F4A7C15}
                            + static_cast<uint64_t> (sampleCount)));

    oscA.reset();
    oscB.reset();
    env.reset();
    lfo.reset();
    filter1.reset();
    filter2.reset();
    dcBlock.reset();
    subPhase = 0.0f;
    noiseState = 0.0f;

    // The pitches are resolved links (5.7.2): VoiceParams has already applied
    // the played note, the tuning macro and the a_freq / b_freq offsets. All
    // that is left here is keeping them inside Nyquist. The sub is still a sine
    // at A/2 (M6 turns it into the documented AM of B at B/2).
    baseFreqA     = clampFreq (p.freqAHz, sampleRate);
    baseFreqB     = clampFreq (p.freqBHz, sampleRate);
    baseFreqSub   = clampFreq (p.freqAHz * 0.5f, sampleRate);
    subInc = baseFreqSub / static_cast<float> (sampleRate);

    // Tiny random detune plus the curated detune gene.
    detuneA = rng.gaussianClamped (1.0f) * (2.0f + p.detuneCents);
    detuneB = -detuneA * rng.range (0.5f, 1.0f);

    oscA.setFrequency (baseFreqA, static_cast<float> (sampleRate));
    oscB.setFrequency (baseFreqB, static_cast<float> (sampleRate));
    oscA.setPhase (rng.uniform());
    oscB.setPhase (rng.uniform());

    // The one clock (design 6.1.1). Everything in the shape - duration, key
    // follow, loop period, curvatures, sustain, the second fade, the note off
    // release - is resolved by VoiceParams; the voice only runs it.
    env.start (p.envShape());

    lfo.setRateHz (p.lfoRate, sampleRate);
    // One LFO, one shape, one rate (design 6.2 / plan M4). The shape used to be
    // re-rolled per voice, which made the same patch sound like a different
    // instrument on every key press; the phase offset stays, that is the part of
    // "per voice life" that does not change what a gene means.
    lfo.setShape (kLfoSine, rng);
    lfo.setPhase (rng.uniform());

    // The two sample and holds (4.2). Both rates come from the mapping layer,
    // including the fact that b_sh's rate is a division of B's resolved
    // frequency; a rate of 0 means the gate is shut and the class bypasses.
    modHold.setRateHz (p.modSampleHoldHz, sampleRate);
    modHold.reset();
    bHold.setRateHz (p.bSampleHoldHz, sampleRate);
    bHold.reset();
    tremoloDepth = 0.0f;
    vibSemitones = 0.0f;

    driftPitch.prepare (sampleRate, rng, 0.25f);
    driftFilter.prepare (sampleRate, rng, 0.11f);

    // Slight per voice stereo spread so chords breathe.
    const float pan = rng.gaussianClamped (1.0f) * 0.30f;
    const float angle = (pan * 0.5f + 0.5f) * 0.5f * kPi;
    panGainL = std::cos (angle);
    panGainR = std::sin (angle);

    // The tracking cutoff pair arrives resolved (5.7.2). Both are clamped to
    // Nyquist here, and the octave offset between them is kept as a member so
    // modulation can move the pair together instead of re-deriving the link.
    const float maxCut = static_cast<float> (sampleRate) * 0.45f;
    cutoffSepOct = p.filterSep;
    cutoff1 = cutoff1To = clampf (p.filterHz, 20.0f, maxCut);
    cutoff2 = cutoff2To = clampf (p.cutoff2Hz, 20.0f, maxCut);

    const float q2 = 1.0f + (p.filterQ - 1.0f) * 0.5f;
    filter1.setCutoffQ (cutoff1, p.filterQ);
    filter2.setCutoffQ (cutoff2, q2);
    filter1.setDrive (p.drive);
    filter2.setDrive (p.drive * 0.5f);

    freqA = freqATo = baseFreqA;
    freqB = freqBTo = baseFreqB;
    colourA = colourATo = clamp01 (p.aColour);
    colourB = colourBTo = clamp01 (p.bColour);
    fmIndex = fmIndexTo = p.fmAmount * 3.0f;
    // The mix starts where osc_mix put it: p.balance is already in equal power
    // mix space (5.7.7), and mix_mod only moves it from the control block on.
    mixA = mixATo = std::cos (p.balance * 0.5f * kPi);
    mixB = mixBTo = std::sin (p.balance * 0.5f * kPi);
    freqASlope = freqBSlope = cutoff1Slope = cutoff2Slope = fmIndexSlope = 0.0f;
    colourASlope = colourBSlope = mixASlope = mixBSlope = 0.0f;
    ctrlCountdown = 0;
}

//==============================================================================
void Voice::release()
{
    if (! active)
        return;

    // The clock owns the note off behaviour now (D-5): stop repeating, ramp to
    // silence over the fixed release from wherever the shape currently is.
    env.release();
}

//==============================================================================
float Voice::whiteNoise()
{
    return rng.uniform() * 2.0f - 1.0f;
}

//==============================================================================
// The modulation budget of design 6.2, in one function: mod envelope -> pitch
// (a_mod / b_mod), FM amount (fm_mod), A/B mix (mix_mod) and cutoff (flt_mod);
// LFO -> pitch (vibrato) and level (tremolo, applied per sample in render).
//
// The amounts are resolved and signed, the envelope value is not: it has already
// been scaled once by VelSens x mod_vel and re-quantised by mod_sh, whichever of
// the two the genes asked for. Nothing here looks at a gene.
//==============================================================================
void Voice::updateControlTargets (float modEnv, float lfoVibSemis,
                                 float driftP, float driftF)
{
    const float driftCents = driftP * p.driftAmount * 12.0f;

    // Pitch (5.3 + 6.2, decision R2). Two bounded modulators reach it:
    //
    //   * the mod envelope, through p.aModSemis / p.bModSemis. This is the
    //     behaviour the 2026-10-21 "the note changes pitch when I play it" fix
    //     removed, and it is what the reference documents; the difference is that
    //     it is now bounded (VoiceParams::kPitchModSemitoneCeiling) and pulled
    //     to neutral at generation time by Atonality (5.5, R7). Plan 6.1 has the
    //     history, and testPitchModulation is the gate that replaced the old one.
    //   * the LFO's vibrato, with lfo_amt's documented polarity: below the
    //     middle of that gene, B moves the other way (4.2).
    //
    // With duration as a gene (M3) a short envelope makes this a dive and a long
    // one a swell. That is the reference's behaviour and the harness measures it.
    const float semisA = p.aModSemis * modEnv + lfoVibSemis;
    const float semisB = p.bModSemis * modEnv + lfoVibSemis * p.lfoSignB;

    freqATo = clampFreq (baseFreqA * centsToRatio (detuneA + driftCents + semisA * 100.0f),
                         sampleRate);
    freqBTo = clampFreq (baseFreqB * centsToRatio (detuneB + driftCents * 0.7f + semisB * 100.0f),
                         sampleRate);

    // Filter: the resolved tracking pair moves together (5.7.2). Key follow was
    // pre computed at note on through its soft rails (5.7.4); the mod envelope
    // adds up to three octaves of deviation.
    //
    // The LFO's fixed path to the cutoff is gone (decision R6): 4.2's lfo_bal
    // splits one LFO between vibrato and *tremolo*, and a hidden third
    // destination made that balance control mean two unrelated things at once.
    const float oct = p.keyFollow
                    + p.filterMod * modEnv * 3.0f
                    + driftF * p.driftAmount * 0.12f;

    const float maxCut = static_cast<float> (sampleRate) * 0.45f;
    cutoff1To = clampf (p.filterHz * std::pow (2.0f, oct), 20.0f, maxCut);
    cutoff2To = clampf (cutoff1To * std::pow (2.0f, cutoffSepOct), 20.0f, maxCut);

    // Timbre: a_mod / b_mod no longer sit here (they are pitch now, R2), so the
    // oscillator spectra follow the resolved noise colour only, which stays
    // smoothed because nothing is modulating it yet (M6's noise shaper will).
    colourATo = clamp01 (p.aColour);
    colourBTo = clamp01 (p.bColour);

    // Mix (6.2): mix_mod moves the A/B balance in *mix space*. That is the only
    // way it can share osc_mix's equal power law instead of having a second,
    // disagreeing one next to it (5.7.7).
    const float balance = clamp01 (p.balance + p.mixMod * modEnv);
    mixATo = std::cos (balance * 0.5f * kPi);
    mixBTo = std::sin (balance * 0.5f * kPi);

    fmIndexTo = std::max (0.0f, p.fmAmount * (1.0f + p.fmMod * modEnv) * 3.0f);
}

//==============================================================================
void Voice::applyTargetsImmediately()
{
    freqA = freqATo;      freqASlope = 0.0f;
    freqB = freqBTo;      freqBSlope = 0.0f;
    cutoff1 = cutoff1To;  cutoff1Slope = 0.0f;
    cutoff2 = cutoff2To;  cutoff2Slope = 0.0f;
    fmIndex = fmIndexTo;  fmIndexSlope = 0.0f;
    colourA = colourATo;  colourASlope = 0.0f;
    colourB = colourBTo;  colourBSlope = 0.0f;
    mixA = mixATo;        mixASlope = 0.0f;
    mixB = mixBTo;        mixBSlope = 0.0f;

    filter1.setCutoffQ (cutoff1, p.filterQ);
    filter2.setCutoffQ (cutoff2, 1.0f + (p.filterQ - 1.0f) * 0.5f);
}

//==============================================================================
void Voice::render (float* outL, float* outR, int numSamples)
{
    if (! active)
        return;

    // Lowpass, always, until M5: flt_type is the topology gene (4.2), the voice
    // has no topology of its own any more, and the structural trait that used to
    // pick one was removed by M3 (decision R10, see Genetic/Mutation.h). The
    // parallel sum, the notch mix and the band/high taps all belong to M5.
    const float lpMix = 1.0f, bpMix = 0.0f, hpMix = 0.0f;

    // p.balance is already in equal power mix space (0.5 = 50/50): the osc_mix
    // warp of 5.7.7 was applied by VoiceParams, so the law here stays symmetric.
    // mix_mod (6.2) moves the balance *in that space*, which is the only way it
    // can share one equal power curve with osc_mix instead of having a second,
    // disagreeing one next to it - so the mix gains are a modulation target like
    // any other, ramped between control updates unless an S&H step asks now.
    const float sr = static_cast<float> (sampleRate);

    // lfo_bal is 0 = vibrato only, 0.5 = both, 1 = tremolo only (4.2), split at
    // equal power between the pitch destination and the level destination. The
    // fixed LFO to filter route is gone (decision R6): with tremolo as the
    // documented second destination, that balance control means exactly one
    // thing at a time.
    const float lfoBalanceCos = std::cos (clamp01 (p.lfoBalance) * 0.5f * kPi);
    const float lfoBalanceSin = std::sin (clamp01 (p.lfoBalance) * 0.5f * kPi);
    const bool lfoOn = p.lfoDepth > 1.0e-4f;
    // The LFO is free (design 6.1.1 Rev 4): nothing here multiplies its depth by
    // the envelope. Vibrato and tremolo are the only modulators in the instrument
    // that do not follow the note's shape, which is what keeps them usable on a
    // slow sound; lfo_dly is the only thing that gates them, and only at the
    // onset. testModulationSources holds that line.

    // Structural traits, not b_sh semantics (plan M4, R10): a trait may swap what
    // B does, and mode 0 is "no trait". The sample and hold on B is a gene
    // (b_sh), which is why the old "sample & hold" trait mode is gone.
    const bool syncOn = p.oscBMode == 1;
    const bool ringOn = p.oscBMode == 2;
    const bool bIsHeld = p.bSampleHoldMix > 0.001f;

    const bool hasNoiseA = p.aNoise > 0.001f;
    const bool hasNoiseB = p.bNoise > 0.001f;
    const bool hasSub = p.subLevel > 0.001f;

    for (int i = 0; i < numSamples; ++i)
    {
        //--------------------------------------------------------------------
        // The one clock (design 6.1.1 Rev 4): a pluck repeated at the resolved
        // period, under an outer volume decay whose length is env_time. Sample
        // accurate, per 6.4 - an envelope that lags its own shape is an envelope
        // that does not measure what the harness measures.
        const float ampEnvV = env.process();

        // The modulator is the *same pluck* with its own timing (6.1.1: the
        // modulation envelope has no shape of its own), scaled once by VelSens x
        // mod_vel and then re-quantised by mod_sh if that gate is open - which is
        // what turns it from a pluck into a squared envelope that still follows
        // the note. Scale, then hold, in the document's order: the two commute,
        // but writing them the other way round is how a velocity curve ends up
        // inside the hold.
        const float modRaw = env.modValue() * p.modScale;
        const bool modStepped = modHold.process (modRaw);
        const float modMix = modHold.active() ? modHold.value() : modRaw;

        const float lfoV = lfoOn ? lfo.process() : 0.0f;
        const float driftP = driftPitch.process (rng);
        const float driftF = driftFilter.process (rng);

        //--------------------------------------------------------------------
        // Control rate update + ramp (design doc 6.4).
        if (--ctrlCountdown <= 0)
        {
            ctrlCountdown = kCtrlPeriod;
            constexpr float kCtrlInv = 1.0f / static_cast<float> (kCtrlPeriod);

            // lfo_dly (4.2: "delay before the LFO fades in"). The LFO runs from
            // the first sample of the note; its *depth* fades in over the
            // resolved seconds. That is the reading the gate measures: a silent
            // onset of the modulation, not a silent note.
            float lfoGate = 1.0f;
            if (p.lfoDelaySeconds > 0.0f)
                lfoGate = clamp01 (static_cast<float> (sampleCount - startedAt + i)
                                   / (p.lfoDelaySeconds * sr));

            // Vibrato depth, including lfo_amt's magnitude and the onset gate.
            // Its sign for B is lfoSignB, applied in updateControlTargets.
            vibSemitones = lfoV * p.lfoDepth * lfoBalanceCos * lfoGate
                         * kVibratoSemitoneCeiling;
            tremoloDepth = p.lfoDepth * lfoBalanceSin * lfoGate;

            const float fromA = freqA, fromB = freqB, fromC1 = cutoff1, fromC2 = cutoff2, fromF = fmIndex;
            const float fromCA = colourA, fromCB = colourB, fromMA = mixA, fromMB = mixB;
            updateControlTargets (modMix, vibSemitones, driftP, driftF);
            freqASlope = (freqATo - fromA) * kCtrlInv;
            freqBSlope = (freqBTo - fromB) * kCtrlInv;
            cutoff1Slope = (cutoff1To - fromC1) * kCtrlInv;
            cutoff2Slope = (cutoff2To - fromC2) * kCtrlInv;
            fmIndexSlope = (fmIndexTo - fromF) * kCtrlInv;
            colourASlope = (colourATo - fromCA) * kCtrlInv;
            colourBSlope = (colourBTo - fromCB) * kCtrlInv;
            mixASlope = (mixATo - fromMA) * kCtrlInv;
            mixBSlope = (mixBTo - fromMB) * kCtrlInv;
        }

        // 6.4's one exception: "S&H outputs must not be smoothed through their
        // step - the step is the sound". A mod_sh step lands on the sample it
        // lands on, with no ramp, for every destination that reads the mod value.
        if (modStepped)
        {
            updateControlTargets (modMix, vibSemitones, driftP, driftF);
            applyTargetsImmediately();
        }

        freqA += freqASlope;   oscA.setFrequency (freqA, sr);
        freqB += freqBSlope;   oscB.setFrequency (freqB, sr);
        colourA += colourASlope;
        colourB += colourBSlope;
        cutoff1 += cutoff1Slope;
        cutoff2 += cutoff2Slope;
        fmIndex += fmIndexSlope;
        mixA += mixASlope;
        mixB += mixBSlope;

        const bool cutMoved = std::abs (cutoff1Slope) > 1.0e-6f || std::abs (cutoff2Slope) > 1.0e-6f;
        if (cutMoved)
        {
            filter1.setCutoffQ (cutoff1, p.filterQ);
            filter2.setCutoffQ (cutoff2, 1.0f + (p.filterQ - 1.0f) * 0.5f);
        }

        //--------------------------------------------------------------------
        // Sources.
        float a = 0.0f, b = 0.0f;

        if (p.fmDirection == 0)
        {
            const float rawB = oscB.next (p.bForm, colourB);
            b = rawB;
            a = oscA.nextModulated (p.aForm, colourA, fmIndex * rawB);

            if (syncOn && oscA.phaseValue() < oscA.increment())
                oscB.setPhase (0.5f);
        }
        else
        {
            const float rawA = oscA.next (p.aForm, colourA);
            a = rawA;
            b = oscB.nextModulated (p.bForm, colourB, fmIndex * rawA);

            if (syncOn && oscB.phaseValue() < oscB.increment())
                oscA.setPhase (0.5f);
        }

        // b_sh (4.2): B's own sample and hold, at a rate the mapping layer took
        // from B's frequency below the middle of the gene, and from the free
        // 0.5..16 Hz range above it. The mix is the crossfade in from the
        // neutral value, so at b_sh = 0 the signal is the same one, sample for
        // sample (5.7.1).
        if (bIsHeld)
        {
            bHold.process (b);
            b += (bHold.value() - b) * p.bSampleHoldMix;
        }

        if (ringOn) b = a * b * 2.0f;

        if (hasNoiseA) { noiseState = whiteNoise(); a = a * (1.0f - p.aNoise) + noiseState * p.aNoise; }
        if (hasNoiseB) { const float n = whiteNoise(); b = b * (1.0f - p.bNoise) + n * p.bNoise; }

        float mix = a * mixA + b * mixB;

        if (hasSub)
        {
            subPhase += subInc;
            if (subPhase >= 1.0f) subPhase -= 1.0f;
            mix += std::sin (subPhase * 2.0f * kPi) * p.subLevel;
        }

        //--------------------------------------------------------------------
        // Dual filter, amp envelope, tremolo, level.
        const float f1 = filter1.process (mix, lpMix, bpMix, hpMix);
        const float f2 = filter2.process (f1, lpMix, bpMix, hpMix);
        const float filtered = dcBlock.process (f2);

        // Tremolo: lfo_bal's other destination (6.2). Applied per sample rather
        // than through the control ramp, because it *is* the modulation, and a
        // tremolo resampled every 16 samples is a tremolo with a staircase on it.
        // Depth is at most 1, so the gain stays in [0,2] and the mean level is
        // unchanged; the RMS rise it does cause is what loudnessCompensation now
        // knows about (Genetic/Mutation.cpp, 5.6).
        const float amp = ampEnvV * (1.0f + tremoloDepth * lfoV);
        float out = filtered * amp * p.gain * p.velocity;

        // Done in both directions: the key came up and the release finished, or
        // the note ended itself (its duration is over with nothing to sustain,
        // or vol_fade closed it) while the key is still down. The second one is
        // what keeps a field of one shot notes from holding voices open.
        if (env.finished() && amp <= 0.0f)
        {
            active = false;
            for (int j = i; j < numSamples; ++j)
            {
                outL[j] += 0.0f;
                outR[j] += 0.0f;
            }
            return;
        }

        peak = std::max (peak, std::abs (out));

        outL[i] += out * panGainL;
        outR[i] += out * panGainR;

        ++sampleCount;
    }
}

} // namespace gmosh
