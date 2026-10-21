#pragma once

#include "Dsp.h"

#include <array>
#include <vector>

namespace gmosh
//==============================================================================
// Master effects: chorus into an 8 line feedback delay network reverb
// (design doc section 8).
//
//   fx_size  -> delay line lengths (mutually prime, scaled)
//   fx_len   -> RT60 -> per line feedback gain
//   fx_damp  -> one pole lowpass inside every line
//   fx_chors -> chorus depth and rate together
//   fx_mix   -> equal power wet/dry, scaled by the Effect macro
//==============================================================================
{
class MasterFx
{
public:
    void prepare (double sampleRate, int maxBlockSamples);
    void reset();

    /// Called at block rate from the audio thread; values are ramped inside so
    /// growing a branch never clicks the tail.
    void setParams (float size, float rt60, float damp, float chorus, float mix, float width);

    void process (float* left, float* right, int numSamples);

    double tailSeconds() const { return rt60Current; }

private:
    static constexpr std::size_t kFdnLines = 8;
    static constexpr int kCtrlPeriod = 32;

    void updateLineLengths();

    double sampleRate = 44100.0;

    std::array<VariableDelay, kFdnLines> lines;
    std::array<OnePole, kFdnLines> lineDamp;
    std::array<float, kFdnLines> baseLength{};   ///< samples at size 1, 48 kHz
    std::array<float, kFdnLines> lengthCur{};
    std::array<float, kFdnLines> lengthTo{};
    std::array<float, kFdnLines> feedback{};
    std::array<float, kFdnLines> lineModPhase{};
    std::array<float, kFdnLines> lineModInc{};

    std::array<float, kFdnLines> inGainL{}, inGainR{}, outGainL{}, outGainR{};

    VariableDelay chorusL, chorusR;
    Modulator chorusModL, chorusModR;
    Rng rng { 0 };

    // smoothed targets
    float sizeCurrent = 0.6f, sizeTarget = 0.6f;
    float rt60Current = 1.5f, rt60Target = 1.5f;
    float dampCurrent = 0.4f, dampTarget = 0.4f;
    float chorusCurrent = 0.0f, chorusTarget = 0.0f;
    float mixCurrent = 0.2f, mixTarget = 0.2f;
    float widthCurrent = 0.6f, widthTarget = 0.6f;

    float xIn[kFdnLines] {}, yOut[kFdnLines] {};
    int ctrlCountdown = 0;
};

} // namespace gmosh
