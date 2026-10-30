#pragma once

#include "../Genetic/GeneTable.h"
#include "../Genetic/Mutation.h"

namespace gmosh
//==============================================================================
// Genome -> real world voice settings.
//
// Design doc section 3: this happens at note on, never per sample. The genome
// stays normalized; all the ranges and curves live in the gene table.
//
// This is also the only place where the links between genes are resolved
// (5.7.2): a_freq is the patch pitch and everything tonal follows it.
//==============================================================================
{
/// Non-gene engine behaviour, named instead of mutated (design deviations D-3
/// and D-5). The reference documents these as behaviour, not controls, so they
/// are constants here rather than genes; M10 may revisit the amounts with
/// measurements from the harness.
inline constexpr float kNoteOffReleaseSeconds = 0.18f; ///< D-5: fixed note off release
inline constexpr float kVoiceDetuneCents     = 12.0f;  ///< D-3: per voice spread of the detune
inline constexpr float kVoiceDriftAmount     = 0.35f;  ///< D-3: per voice random walk depth

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
    // envelope clock (4.2 group 1). env_time *is* the duration of the sound;
    // the shared envelope engine (M3) is what consumes it as one.
    float envTime = 1.0f, envRepeat = 0.0f, envTilt = 0.0f, envKeyFollow = 0.0f;

    // amplitude envelope
    float ampAttack = 0.02f, ampDecay = 0.6f, ampSustain = 0.6f,
          ampRelease = kNoteOffReleaseSeconds;
    float volFade = 0.0f;

    // modulation envelope
    float modAttack = 0.01f, modDecay = 0.5f, modShape = 1.0f, modVelocity = 0.5f;

    // lfo
    float lfoRate = 1.0f, lfoAmount = 0.0f, lfoBalance = 0.5f, lfoDelaySeconds = 0.0f;

    // oscillators
    float aForm = 0.0f, aColour = 0.5f, aNoise = 0.0f, aRatio = 0.0f, aMod = 0.0f;
    float bForm = 0.0f, bColour = 0.5f, bNoise = 0.0f, bRatio = 0.0f, bMod = 0.0f, bShape = 0.0f;

    // mix / cross modulation
    float balance = 0.79f, fmAmount = 0.0f, fmMod = 0.0f, mixMod = 0.0f, subLevel = 0.0f;

    // filter
    float filterHz = 4000.0f, filterQ = 1.0f, keyFollow = 0.0f, filterMod = 0.0f, filterSep = 0.0f;
    /// flt_type as a stepped value in [0,1] (0 bandpass, .33 serial LP, .66
    /// parallel LP, 1 notch). Resolved here so nothing else has to re-read the
    /// gene; the audible topology stays the trait until M5 (decision R10).
    float filterTopology = 0.6667f;

    // saturation
    float drive = 0.0f;

    // effects (per voice wet path, M7)
    float fxSize = 0.6f, fxLength = 1.5f, fxDamp = 0.5f, fxChorus = 0.0f, fxMix = 0.2f, fxWidth = 0.5f;
    float reverbBloomSeconds = 0.0f;

    // output stage (M7)
    float bassDb = 0.0f, trebleDb = 0.0f, panSpread = 0.0f, clipDrive = 0.0f;

    // per voice "life" (not genes any more, D-3)
    float driftAmount = kVoiceDriftAmount, detuneCents = kVoiceDetuneCents;

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
void resolveVoiceParams (const Genome& genome, const BranchTraits& traits,
                         const MacroValues& macros, int midiNote, float velocity,
                         VoiceParams& out);

} // namespace gmosh
