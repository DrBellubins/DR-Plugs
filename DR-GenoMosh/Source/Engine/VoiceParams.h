#pragma once

#include "Dsp.h"
#include "../Genetic/GeneTable.h"
#include "../Genetic/Mutation.h"

namespace gmosh
//==============================================================================
// Genome -> real world voice settings.
//
// Design doc section 3: this happens at note on, never per sample. The genome
// stays normalized; all the ranges and curves live in the gene table.
//
// The played key is looked at once, here. The voice's pitch and the filter's
// anchor are both resolved from it (see the pitch block below), so no other
// layer in the engine needs the note number.
//==============================================================================
{
/// Host automatable macros (design doc 4.1). These are *not* genes: they never
/// mutate, they scale what the player is already holding.
struct MacroValues
{
    float volume = 0.8f;
    float velSens = 0.6f;
    float tuningCents = 0.0f;
    float effect = 0.6f;
    float wheelScale = 0.7f;
    float wheel = 0.0f;
};

struct VoiceParams
{
    // The one envelope clock: a pluck, optionally floored above silence, faded
    // out after a counted number of repeats. All the decisions are made here;
    // SharedEnv derives none of them.
    SharedEnv::Shape envShape{};

    // mod_sh: sample and hold on the modulator, 0 = off
    float modHoldHz = 0.0f;

    // velocity scaling of the modulator, applied once (VelSens x mod_vel)
    float modScale = 1.0f;

    // lfo
    float lfoRate = 1.0f, lfoAmount = 0.0f, lfoBalance = 0.0f;
    // The LFO is free: vibrato and tremolo at one rate, split equal power by
    // lfo_bal, never scaled by the envelope.
    float vibSemis = 0.0f, tremDepth = 0.0f;

    // oscillators
    float aForm = 0.0f, aColour = 0.5f, aNoise = 0.0f, aRatio = 0.0f;
    float bForm = 0.0f, bColour = 0.5f, bNoise = 0.0f, bRatio = 0.0f, bShape = 0.0f;

    // The pitch the voice is built on, resolved once at note on: the key with
    // the tuning macro on it, then the two oscillator offset genes on top. The
    // voice uses these instead of recomputing them, and the filter cutoff is
    // anchored to oscillator A's. Unclamped Hz: this layer has no sample rate,
    // the voice clamps for audio.
    float noteFrequencyHz = 261.63f;
    float oscillatorAFrequencyHz = 261.63f;
    float oscillatorBFrequencyHz = 261.63f;

    // a_mod / b_mod resolved in semitones at their ceiling (the ceiling is a
    // hard bound of the mapping layer, not something the voice may exceed)
    float aModSemis = 0.0f, bModSemis = 0.0f;

    // mix / cross modulation
    float balance = 0.5f, fmAmount = 0.0f, fmMod = 0.0f, subLevel = 0.0f, detuneCents = 0.0f;

    // filter. filterHz is already anchored to the pitch being played: it is the
    // cutoff at oscillatorAFrequencyHz. keyFollow and filterMod are octaves of
    // deviation from a filter that tracks the keyboard.
    float filterHz = 4000.0f, filterQ = 1.0f, keyFollow = 0.0f, filterMod = 0.0f, filterSep = 0.0f;

    // effects
    float fxSize = 0.6f, fxLength = 1.5f, fxDamp = 0.5f, fxChorus = 0.0f, fxMix = 0.2f, fxWidth = 0.5f;

    // movement / life
    float driftAmount = 0.0f, drive = 0.0f;

    // derived
    float gain = 1.0f;
    float velocity = 1.0f;
    float tuneCents = 0.0f;

    unsigned char filterMode = 0;
    unsigned char fmDirection = 0;
    unsigned char modSource = 0;
    unsigned char oscBMode = 0;
};

/// Resolve a (already distance displaced) genome into playable settings.
void resolveVoiceParams(const Genome& genome, const BranchTraits& traits, const MacroValues& macros,
                        int midiNote, float velocity, VoiceParams& out);

} // namespace gmosh
