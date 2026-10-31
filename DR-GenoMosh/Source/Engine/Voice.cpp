#include "Voice.h"
#include "../Genetic/Rng.h"

#include <algorithm>
#include <cmath>

namespace gmosh
{
namespace
{
float clampFrequency(float frequencyHz, float sampleRateHz)
{
    return clampf(frequencyHz, 0.5f, static_cast<float>(sampleRateHz) * 0.45f);
}

float centsToRatio(float cents) { return std::pow(2.0f, cents / 1200.0f); }
} // namespace

//==============================================================================
void Voice::prepare(double sampleRateHz, int /*maxBlockSamples*/)
{
    sampleRate = sampleRateHz;
    envelope.setSampleRate(sampleRateHz);
    modHold.setSampleRate(sampleRateHz);
    filterA.setSampleRate(sampleRateHz);
    filterB.setSampleRate(sampleRateHz);
    dcBlocker.setSampleRate(sampleRateHz);
}

//==============================================================================
void Voice::start(const VoiceParams& newParams, int midiNote, uint64_t plantHash)
{
    params = newParams;
    note = midiNote;
    active = true;
    startedAt = sampleCount;
    peak = 0.0f;

    // Per voice variation: a fresh, note and time dependent stream (design
    // doc 7 "voice level life").
    randomGenerator =
        Rng(Hash::combine(plantHash, static_cast<uint64_t>(midiNote) * uint64_t{ 0x9E3779B97F4A7C15 }
                                         + static_cast<uint64_t>(sampleCount)));

    oscillatorA.reset();
    oscillatorB.reset();
    envelope.reset();
    modHold.reset();
    lfo.reset();
    filterA.reset();
    filterB.reset();
    dcBlocker.reset();
    subPhase = 0.0f;
    noiseState = 0.0f;

    const float fundamentalFrequency = noteToHz(midiNote, params.tuneCents);
    baseFrequencyA = clampFrequency(fundamentalFrequency * std::pow(2.0f, params.aRatio / 12.0f), sampleRate);
    baseFrequencyB = clampFrequency(fundamentalFrequency * std::pow(2.0f, params.bRatio / 12.0f), sampleRate);
    baseFrequencySub = clampFrequency(fundamentalFrequency * 0.5f, sampleRate);
    subIncrement = baseFrequencySub / static_cast<float>(sampleRate);

    // Tiny random detune plus the curated detune gene.
    detuneA = randomGenerator.gaussianClamped(1.0f) * (2.0f + params.detuneCents);
    detuneB = -detuneA * randomGenerator.range(0.5f, 1.0f);

    oscillatorA.setFrequency(baseFrequencyA, static_cast<float>(sampleRate));
    oscillatorB.setFrequency(baseFrequencyB, static_cast<float>(sampleRate));
    oscillatorA.setPhase(randomGenerator.uniform());
    oscillatorB.setPhase(randomGenerator.uniform());

    // The envelope is resolved once, here, from the voice's own snapshot: a
    // branch growing under a held note cannot change the pluck that note is
    // playing (design doc 3).
    envelope.setShape(params.envShape);
    envelope.gateOn();
    modHold.setRateHz(params.modHoldHz);

    // The level meter used for voice stealing, with a slow release, so a voice
    // is never "quiet" in the gap between two of its own plucks.
    levelTrack = 0.0f;
    levelDecayCoefficient = std::exp(-1.0f / (0.35f * static_cast<float>(sampleRate)));

    lfo.setRateHz(params.lfoRate, sampleRate);
    lfo.setShape(randomGenerator.integer(3), randomGenerator);
    lfo.setPhase(randomGenerator.uniform());

    driftPitch.prepare(sampleRate, randomGenerator, 0.25f);
    driftFilter.prepare(sampleRate, randomGenerator, 0.11f);

    // Slight per voice stereo spread so chords breathe.
    const float panAmount = randomGenerator.gaussianClamped(1.0f) * 0.30f;
    const float angle = (panAmount * 0.5f + 0.5f) * 0.5f * kPi;
    panGainL = std::cos(angle);
    panGainR = std::sin(angle);

    const float secondaryResonance = 1.0f + (params.filterQ - 1.0f) * 0.5f;
    cutoffHzA = cutoffHzATarget = clampf(params.filterHz, 20.0f, static_cast<float>(sampleRate) * 0.45f);
    cutoffHzB = cutoffHzBTarget = clampf(params.filterHz * std::pow(2.0f, params.filterSep), 20.0f,
                                         static_cast<float>(sampleRate) * 0.45f);
    filterA.setCutoffQ(cutoffHzA, params.filterQ);
    filterB.setCutoffQ(cutoffHzB, secondaryResonance);
    filterA.setDrive(params.drive);
    filterB.setDrive(params.drive * 0.5f);

    frequencyA = frequencyATarget = baseFrequencyA;
    frequencyB = frequencyBTarget = baseFrequencyB;
    colourA = colourATarget = clamp01(params.aColour);
    colourB = colourBTarget = clamp01(params.bColour);
    fmIndex = fmIndexTarget = params.fmAmount * 3.0f;
    frequencyASlope = frequencyBSlope = cutoffHzASlope = cutoffHzBSlope = fmIndexSlope = 0.0f;
    colourASlope = colourBSlope = 0.0f;
    ctrlCountdown = 0;
}

//==============================================================================
void Voice::release()
{
    if (!active)
        return;

    envelope.gateOff();
}

//==============================================================================
float Voice::whiteNoise() { return randomGenerator.uniform() * 2.0f - 1.0f; }

//==============================================================================
void Voice::updateControlTargets(float modulationValue, float lfoValue, float driftPitchValue,
                                 float driftFilterValue)
{
    const float driftCents = driftPitchValue * params.driftAmount * 12.0f;

    // Pitch (5.3). a_mod / b_mod are the mod envelope's amount on each
    // oscillator's pitch: with the envelope being a pluck, this is the dive or
    // rise of each individual pluck rather than a sweep across the note. The
    // depth is bounded in the mapping layer (kPitchModSemitoneCeiling), so the
    // worst case is an octave, in the direction the gene points.
    // aModSemis / vibSemis are semitones, centsToRatio takes cents.
    const float centsA = (params.aModSemis * modulationValue + params.vibSemis * lfoValue) * 100.0f;
    const float centsB = (params.bModSemis * modulationValue + params.vibSemis * lfoValue * 0.7f) * 100.0f;

    frequencyATarget =
        clampFrequency(baseFrequencyA * centsToRatio(detuneA + driftCents + centsA), sampleRate);
    frequencyBTarget =
        clampFrequency(baseFrequencyB * centsToRatio(detuneB + driftCents * 0.7f + centsB), sampleRate);

    // Filter: key follow was pre computed at note on, the mod envelope adds up
    // to three octaves of deviation, and the drift adds a slow wander.
    const float octaveDeviation = params.keyFollow + params.filterMod * modulationValue * 3.0f
                                  + driftFilterValue * params.driftAmount * 0.12f;

    const float maxCutoffHz = static_cast<float>(sampleRate) * 0.45f;
    cutoffHzATarget = clampf(params.filterHz * std::pow(2.0f, octaveDeviation), 20.0f, maxCutoffHz);
    cutoffHzBTarget = clampf(cutoffHzATarget * std::pow(2.0f, params.filterSep), 20.0f, maxCutoffHz);

    fmIndexTarget = std::max(0.0f, params.fmAmount * (1.0f + params.fmMod * modulationValue) * 3.0f);
}

//==============================================================================
void Voice::render(float* outL, float* outR, int numSamples)
{
    if (!active)
        return;

    const float lowpassMix = params.filterMode == 0 ? 1.0f : 0.0f;
    const float bandpassMix = params.filterMode == 1 ? 1.0f : 0.0f;
    const float highpassMix = params.filterMode == 2 ? 1.0f : 0.0f;

    const float mixA = std::cos(params.balance * 0.5f * kPi);
    const float mixB = std::sin(params.balance * 0.5f * kPi);

    const float sampleRateHz = static_cast<float>(sampleRate);
    const bool syncOn = params.oscBMode == 0 && params.bShape > 0.02f;
    const bool ringOn = params.oscBMode == 1 && params.bShape > 0.02f;
    const bool holdOn = params.oscBMode == 2 && params.bShape > 0.02f;
    const bool hasNoiseA = params.aNoise > 0.001f;
    const bool hasNoiseB = params.bNoise > 0.001f;
    const bool hasSub = params.subLevel > 0.001f;

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        //--------------------------------------------------------------------
        // The one clock, then the modulators that are free (LFO, drift).
        envelope.process();
        const float lfoValue = lfo.process();
        const float driftPitchValue = driftPitch.process(randomGenerator);
        const float driftFilterValue = driftFilter.process(randomGenerator);

        // What drives the modulation destinations. Default is the envelope's own
        // pluck. A branch whose structural trait says otherwise gets a modulator
        // that follows the note as a whole, or the LFO, instead: that choice is
        // what makes two branches of the same plant modulate differently.
        float modulationBase;

        switch (params.modSource)
        {
            case 1:
                // A branch whose structural trait asks for this modulates from
                // the note as a whole instead of from each pluck.
                modulationBase = envelope.outerValue();
                break;

            case 2:
                // Or from the free running LFO.
                modulationBase = 0.5f + 0.5f * lfoValue;
                break;

            default:
                // The pluck envelope itself.
                modulationBase = envelope.modulationValue();
                break;
        }

        // Velocity scales the modulator once. mod_sh re quantises it: the step
        // is the sound, so nothing here is smoothed.
        float modulationValue = modulationBase * params.modScale;

        if (params.modHoldHz > 0.0f)
            modulationValue = modHold.process(modulationValue);

        //--------------------------------------------------------------------
        // Control rate update + ramp (design doc 6.4).
        if (--ctrlCountdown <= 0)
        {
            ctrlCountdown = kCtrlPeriod;
            constexpr float kCtrlPeriodInverse = 1.0f / static_cast<float>(kCtrlPeriod);
            const float previousFrequencyA = frequencyA;
            const float previousFrequencyB = frequencyB;
            const float previousCutoffA = cutoffHzA;
            const float previousCutoffB = cutoffHzB;
            const float previousFmIndex = fmIndex;
            const float previousColourA = colourA;
            const float previousColourB = colourB;
            updateControlTargets(modulationValue, lfoValue, driftPitchValue, driftFilterValue);
            frequencyASlope = (frequencyATarget - previousFrequencyA) * kCtrlPeriodInverse;
            frequencyBSlope = (frequencyBTarget - previousFrequencyB) * kCtrlPeriodInverse;
            cutoffHzASlope = (cutoffHzATarget - previousCutoffA) * kCtrlPeriodInverse;
            cutoffHzBSlope = (cutoffHzBTarget - previousCutoffB) * kCtrlPeriodInverse;
            fmIndexSlope = (fmIndexTarget - previousFmIndex) * kCtrlPeriodInverse;
            colourASlope = (colourATarget - previousColourA) * kCtrlPeriodInverse;
            colourBSlope = (colourBTarget - previousColourB) * kCtrlPeriodInverse;
        }

        frequencyA += frequencyASlope;
        oscillatorA.setFrequency(frequencyA, sampleRateHz);
        frequencyB += frequencyBSlope;
        oscillatorB.setFrequency(frequencyB, sampleRateHz);
        colourA += colourASlope;
        colourB += colourBSlope;
        cutoffHzA += cutoffHzASlope;
        cutoffHzB += cutoffHzBSlope;
        fmIndex += fmIndexSlope;

        const bool cutMoved = std::abs(cutoffHzASlope) > 1.0e-6f || std::abs(cutoffHzBSlope) > 1.0e-6f;

        if (cutMoved)
        {
            filterA.setCutoffQ(cutoffHzA, params.filterQ);
            filterB.setCutoffQ(cutoffHzB, 1.0f + (params.filterQ - 1.0f) * 0.5f);
        }

        //--------------------------------------------------------------------
        // Sources.
        float signalA = 0.0f;
        float signalB = 0.0f;

        if (params.fmDirection == 0)
        {
            const float rawSignalB = oscillatorB.next(params.bForm, colourB);
            signalB = rawSignalB;
            signalA = oscillatorA.nextModulated(params.aForm, colourA, fmIndex * rawSignalB);

            if (syncOn && oscillatorA.phaseValue() < oscillatorA.incrementValue())
                oscillatorB.setPhase(params.bShape * 0.5f);
        }
        else
        {
            const float rawSignalA = oscillatorA.next(params.aForm, colourA);
            signalA = rawSignalA;
            signalB = oscillatorB.nextModulated(params.bForm, colourB, fmIndex * rawSignalA);

            if (syncOn && oscillatorB.phaseValue() < oscillatorB.incrementValue())
                oscillatorA.setPhase(params.bShape * 0.5f);
        }

        if (holdOn)
            signalB = oscillatorB.heldValue(params.bForm, colourB);

        if (ringOn)
            signalB = signalA * signalB * 2.0f;

        if (hasNoiseA)
        {
            noiseState = whiteNoise();
            signalA = signalA * (1.0f - params.aNoise) + noiseState * params.aNoise;
        }

        if (hasNoiseB)
        {
            const float noiseSample = whiteNoise();
            signalB = signalB * (1.0f - params.bNoise) + noiseSample * params.bNoise;
        }

        float mixedSignal = signalA * mixA + signalB * mixB;

        if (hasSub)
        {
            subPhase += subIncrement;

            if (subPhase >= 1.0f)
                subPhase -= 1.0f;

            mixedSignal += std::sin(subPhase * 2.0f * kPi) * params.subLevel;
        }

        //--------------------------------------------------------------------
        // Dual filter, amp envelope, level.
        const float filteredA = filterA.process(mixedSignal, lowpassMix, bandpassMix, highpassMix);
        const float filteredB = filterB.process(filteredA, lowpassMix, bandpassMix, highpassMix);
        const float filtered = dcBlocker.process(filteredB);

        // The tremolo half of the LFO is free too: it rides on top of the
        // envelope instead of replacing it, so it is still there in the middle
        // of a long note.
        float amplitude = envelope.amplitudeValue();

        if (params.tremDepth > 0.0f)
            amplitude *= std::max(0.0f, 1.0f + params.tremDepth * lfoValue);

        levelTrack = std::max(amplitude, levelTrack * levelDecayCoefficient);

        float outputSample = filtered * amplitude * params.gain * params.velocity;

        if (envelope.isIdle() && amplitude <= 0.0f)
        {
            // Either the key is up and the release has run out, or the outer
            // curve settled on a floor of nothing and the note ended by itself.
            active = false;

            for (int remainingIndex = sampleIndex; remainingIndex < numSamples; ++remainingIndex)
            {
                outL[remainingIndex] += 0.0f;
                outR[remainingIndex] += 0.0f;
            }

            return;
        }

        peak = std::max(peak, std::abs(outputSample));

        outL[sampleIndex] += outputSample * panGainL;
        outR[sampleIndex] += outputSample * panGainR;

        ++sampleCount;
    }
}

} // namespace gmosh
