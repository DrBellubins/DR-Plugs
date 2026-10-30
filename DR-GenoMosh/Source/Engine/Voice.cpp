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
    lfo.setShape (rng.integer (3), rng);
    lfo.setPhase (rng.uniform());

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
    freqASlope = freqBSlope = cutoff1Slope = cutoff2Slope = fmIndexSlope = 0.0f;
    colourASlope = colourBSlope = 0.0f;
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
void Voice::updateControlTargets (float modEnvV, float lfoPitch, float lfoFilter,
                                 float driftP, float driftF)
{
    const float driftCents = driftP * p.driftAmount * 12.0f;

    // Pitch (5.3): vibrato from the LFO, split by lfo_bal, plus a few cents of
    // drift. The mod envelope is deliberately *not* in this sum: a_mod / b_mod
    // used to add modMix * 0.75 octaves, so every note dived or rose by up to
    // two thirds of an octave while the mod envelope attacked, in whichever
    // direction the sign of the gene happened to point. That is the "the note
    // changes pitch when I play it" bug. A pitch envelope, if it is ever wanted,
    // should be its own gene rather than a side effect of timbre modulation.
    const float pitchModA = lfoPitch * 0.5f;
    const float pitchModB = lfoPitch * 0.5f;

    // Timbre: a_mod / b_mod are the mod envelope's amount on each oscillator's
    // own spectrum (pulse width, added partials), which is the destination their
    // names suggest and leaves the played pitch alone. The envelope itself is
    // 0..1 (6.1.1); the bipolar gene is what gives the movement a sign, so a
    // note starts at its pressed colour and moves away from it as the envelope
    // opens. M4 moves these two to pitch, which is what the reference documents.
    constexpr float kModColourDepth = 0.35f;
    colourATo = clampf (p.aColour + p.aMod * modEnvV * kModColourDepth, 0.0f, 1.0f);
    colourBTo = clampf (p.bColour + p.bMod * modEnvV * kModColourDepth, 0.0f, 1.0f);

    freqATo = clampFreq (baseFreqA * centsToRatio (detuneA + driftCents)
                                    * std::pow (2.0f, pitchModA), sampleRate);
    freqBTo = clampFreq (baseFreqB * centsToRatio (detuneB + driftCents * 0.7f)
                                    * std::pow (2.0f, pitchModB), sampleRate);

    // Filter: the resolved tracking pair moves together (5.7.2). Key follow was
    // pre computed at note on through its soft rails (5.7.4); the mod sources
    // add up to three octaves of deviation.
    const float oct = p.keyFollow
                    + p.filterMod * modEnvV * 3.0f
                    + lfoFilter * 1.5f
                    + driftF * p.driftAmount * 0.12f;

    const float maxCut = static_cast<float> (sampleRate) * 0.45f;
    cutoff1To = clampf (p.filterHz * std::pow (2.0f, oct), 20.0f, maxCut);
    cutoff2To = clampf (cutoff1To * std::pow (2.0f, cutoffSepOct), 20.0f, maxCut);

    fmIndexTo = std::max (0.0f, p.fmAmount * (1.0f + p.fmMod * modEnvV) * 3.0f);

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
    // warp of 5.7.7 was applied by VoiceParams, so the law here stays symmetric
    // and mix_mod can share it when M4 adds to the same value.
    const float mixA = std::cos (p.balance * 0.5f * kPi);
    const float mixB = std::sin (p.balance * 0.5f * kPi);

    // lfo_bal is 0 = vibrato only, 0.5 = both, 1 = tremolo only (4.2), split at
    // equal power. The tremolo half still drives the filter here: the volume
    // path arrives with M4 and the fixed LFO to filter route goes away with it
    // (decision R6).
    const float lfoDepth = std::abs (p.lfoAmount);   // bipolar gene, 0.5 = silent
    const float lfoBalanceCos = std::cos (clamp01 (p.lfoBalance) * 0.5f * kPi);
    const float lfoBalanceSin = std::sin (clamp01 (p.lfoBalance) * 0.5f * kPi);

    const float sr = static_cast<float> (sampleRate);
    const bool syncOn = p.oscBMode == 0 && p.bShape > 0.02f;
    const bool ringOn = p.oscBMode == 1 && p.bShape > 0.02f;
    const bool holdOn = p.oscBMode == 2 && p.bShape > 0.02f;
    const bool hasNoiseA = p.aNoise > 0.001f;
    const bool hasNoiseB = p.bNoise > 0.001f;
    const bool hasSub = p.subLevel > 0.001f;

    for (int i = 0; i < numSamples; ++i)
    {
        //--------------------------------------------------------------------
        // The one clock (design 6.1.1): the amp is the shape through the volume
        // curvatures, the modulator is the same position through the mod
        // curvatures, scaled once by VelSens x mod_vel. Sample accurate, per
        // 6.4 - with duration as a gene, an envelope that lags its own shape is
        // an envelope that does not measure what the harness measures.
        const float ampEnvV = env.process();
        const float modMix = env.modValue() * p.modScale;

        const float lfoV = lfo.process();
        const float driftP = driftPitch.process (rng);
        const float driftF = driftFilter.process (rng);

        const float lfoPitch = lfoV * lfoDepth * lfoBalanceCos;
        const float lfoFilter = lfoV * lfoDepth * lfoBalanceSin;

        //--------------------------------------------------------------------
        // Control rate update + ramp (design doc 6.4).
        if (--ctrlCountdown <= 0)
        {
            ctrlCountdown = kCtrlPeriod;
            constexpr float kCtrlInv = 1.0f / static_cast<float> (kCtrlPeriod);
            const float fromA = freqA, fromB = freqB, fromC1 = cutoff1, fromC2 = cutoff2, fromF = fmIndex;
            const float fromCA = colourA, fromCB = colourB;
            updateControlTargets (modMix, lfoPitch, lfoFilter, driftP, driftF);
            freqASlope = (freqATo - fromA) * kCtrlInv;
            freqBSlope = (freqBTo - fromB) * kCtrlInv;
            cutoff1Slope = (cutoff1To - fromC1) * kCtrlInv;
            cutoff2Slope = (cutoff2To - fromC2) * kCtrlInv;
            fmIndexSlope = (fmIndexTo - fromF) * kCtrlInv;
            colourASlope = (colourATo - fromCA) * kCtrlInv;
            colourBSlope = (colourBTo - fromCB) * kCtrlInv;
        }

        freqA += freqASlope;   oscA.setFrequency (freqA, sr);
        freqB += freqBSlope;   oscB.setFrequency (freqB, sr);
        colourA += colourASlope;
        colourB += colourBSlope;
        cutoff1 += cutoff1Slope;
        cutoff2 += cutoff2Slope;
        fmIndex += fmIndexSlope;

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
                oscB.setPhase (p.bShape * 0.5f);
        }
        else
        {
            const float rawA = oscA.next (p.aForm, colourA);
            a = rawA;
            b = oscB.nextModulated (p.bForm, colourB, fmIndex * rawA);

            if (syncOn && oscB.phaseValue() < oscB.increment())
                oscA.setPhase (p.bShape * 0.5f);
        }

        if (holdOn) b = oscB.heldValue (p.bForm, colourB);
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
        // Dual filter, amp envelope, level.
        const float f1 = filter1.process (mix, lpMix, bpMix, hpMix);
        const float f2 = filter2.process (f1, lpMix, bpMix, hpMix);
        const float filtered = dcBlock.process (f2);

        const float amp = ampEnvV;
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
