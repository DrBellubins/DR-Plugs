#pragma once

#include "../Genetic/GeneTable.h"
#include "../Genetic/Mutation.h"

namespace gmosh
//==============================================================================
// Genome -> real world voice settings.
//
// Design doc section 3: this happens at note on, never per sample. The genome
// stays normalized; all the ranges and curves live in the gene table.
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
    // amplitude envelope
    float ampAttack = 0.01f, ampDecay = 1.0f, ampSustain = 0.6f, ampRelease = 0.4f;

    // modulation envelope
    float modAttack = 0.01f, modDecay = 0.5f, modShape = 1.0f, modVelocity = 0.5f;

    // looping / tilting envelope
    float loopTime = 1.0f, loopAmount = 0.0f, loopTilt = 0.0f;

    // lfo
    float lfoRate = 1.0f, lfoAmount = 0.0f, lfoBalance = 0.0f;

    // oscillators
    float aForm = 0.0f, aColour = 0.5f, aNoise = 0.0f, aRatio = 0.0f, aMod = 0.0f;
    float bForm = 0.0f, bColour = 0.5f, bNoise = 0.0f, bRatio = 0.0f, bMod = 0.0f, bShape = 0.0f;

    // mix / cross modulation
    float balance = 0.5f, fmAmount = 0.0f, fmMod = 0.0f, subLevel = 0.0f, detuneCents = 0.0f;

    // filter
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
void resolveVoiceParams (const Genome& genome, const BranchTraits& traits,
                         const MacroValues& macros, int midiNote, float velocity,
                         VoiceParams& out);

} // namespace gmosh
