#pragma once

// ============================ WSOLA Pitch Backend v3 (Echo-Boundary-Aware Reset) ============================
//
// Header-only WSOLA pitch shifter backend for Chronoverb's echo pitch system.
//
// Design overview:
//   - Two voices at 50% phase offset, Hann windowed (sum to unity at all times).
//   - Voices read from a circular input buffer at `ratio` speed.
//   - When a voice completes its grain, WSOLA cross-correlation search finds the
//     optimal re-anchor position near writeIndex - lookback, matching against the
//     other voice's current audio for splice continuity.
//   - Echo-boundary-aware reset: ratio changes are applied via OnEchoBoundary()
//     with a short crossfade between two complete states (A/B swap pattern),
//     same pattern as GranularPitchBackend.
//
// Why two voices (not four):
//   Two Hann-windowed voices at 50% phase offset sum to exactly 1.0 at every sample.
//   This eliminates the need for gain compensation and simplifies the overlap logic
//   while still providing continuous, click-free output.
//
// Advantages over granular:
//   Cross-correlation matching at splice points preserves waveform continuity,
//   reducing artifacts on transient-heavy material where blind re-anchoring
//   can cause audible discontinuities.

// ---- Voice: a single read head in the input buffer, advancing at ratio speed ----
// ---- WSOLAState: full pitch-shifting state for one ratio (two overlapping voices) ----
// ---- Two WSOLAStates exist for echo-boundary crossfading (active/inactive A/B swap) ----

class WSOLAPitchBackend_v3 : public IPitchShifterBackend
{
    struct Voice { float readIndex = 0.0f; };

    struct WSOLAState
    {
        Voice voiceA;
        Voice voiceB;
        float phaseA = 0.0f;
        float phaseB = 0.5f;
        float ratio  = 1.0f;
    };

public:
    WSOLAPitchBackend_v3() = default;

    // Allocates the circular input buffer and sets default grain/search parameters.
    void Prepare(double newSampleRate) override
    {
        sampleRate = newSampleRate;

        const int bufferMs = 300;
        const int bufferSize = std::max(
            2048,
            static_cast<int>(std::ceil((bufferMs * sampleRate) / 1000.0)));

        buffer.assign(static_cast<size_t>(bufferSize), 0.0f);

        SetGrainLengthMilliseconds(40.0f);
        SetSearchRadiusMilliseconds(4.0f);
        SetLookbackMultiplier(3.0f);
        SetBoundaryCrossfadeMilliseconds(4.0f);

        Reset();
    }

    // Clears the buffer and re-initialises both states to default positions.
    void Reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;

        stateA = {};
        stateB = {};

        stateA.phaseA = 0.0f;
        stateA.phaseB = 0.5f;
        stateA.ratio  = 1.0f;
        stateB = stateA;

        anchorStateToWrite(stateA);
        anchorStateToWrite(stateB);

        crossfadeRemainingSamples = 0;
        crossfadeTotalSamples     = boundaryCrossfadeSamples;
        activeIsA                 = true;
        pendingFlipAfterFade      = false;
    }

    // Called at each echo boundary to change the pitch ratio.
    // Copies the active state into the inactive state with the new ratio,
    // re-anchors it, and initiates a short crossfade.
    void OnEchoBoundary(float newRatio) override
    {
        WSOLAState& active   = (activeIsA ? stateA : stateB);
        WSOLAState& inactive = (activeIsA ? stateB : stateA);

        if (std::abs(newRatio - active.ratio) < 1.0e-6f)
            return;

        inactive       = active;
        inactive.ratio = newRatio;

        anchorStateToWrite(inactive);
        inactive.phaseA = 0.0f;
        inactive.phaseB = 0.5f;

        if (boundaryCrossfadeSamples <= 0)
        {
            activeIsA            = !activeIsA;
            pendingFlipAfterFade = false;
            crossfadeRemainingSamples = 0;
            crossfadeTotalSamples     = 0;
        }
        else
        {
            crossfadeTotalSamples     = boundaryCrossfadeSamples;
            crossfadeRemainingSamples = crossfadeTotalSamples;
            pendingFlipAfterFade      = true;
        }
    }

    // Sets the ratio with no crossfade — safe to call at PrepareToPlay
    // after CommitPendingSequenceNow to seed the correct starting pitch.
    void SetInitialRatio(float ratio) override
    {
        const float r = juce::jlimit(0.25f, 4.0f, ratio);
        stateA.ratio  = r;
        stateB.ratio  = r;

        stateA.phaseA = 0.0f;
        stateA.phaseB = 0.5f;
        stateB.phaseA = 0.0f;
        stateB.phaseB = 0.5f;

        anchorStateToWrite(stateA);
        anchorStateToWrite(stateB);

        crossfadeRemainingSamples = 0;
        pendingFlipAfterFade      = false;
        activeIsA                 = true;
    }

    // Writes one input sample, produces one pitch-shifted output sample.
    // pitchRatio parameter is unused — ratio is managed via OnEchoBoundary.
    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        if (buffer.empty())
            return inputSample;

        buffer[static_cast<size_t>(writeIndex)] = inputSample;
        writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());

        WSOLAState& active   = (activeIsA ? stateA : stateB);
        WSOLAState& inactive = (activeIsA ? stateB : stateA);

        const float outActive = processStateOneSample(active);

        if (crossfadeRemainingSamples > 0)
        {
            const float outInactive = processStateOneSample(inactive);
            const int done = crossfadeTotalSamples - crossfadeRemainingSamples;
            const float t  = static_cast<float>(done)
                           / static_cast<float>(std::max(1, crossfadeTotalSamples));

            const float gOld = std::cos(t * juce::MathConstants<float>::halfPi);
            const float gNew = std::sin(t * juce::MathConstants<float>::halfPi);

            --crossfadeRemainingSamples;

            if (crossfadeRemainingSamples == 0 && pendingFlipAfterFade)
            {
                activeIsA            = !activeIsA;
                pendingFlipAfterFade = false;
            }

            return outActive * gOld + outInactive * gNew;
        }

        return outActive;
    }

    void SetGrainLengthMilliseconds(float ms)
    {
        const float clamped = juce::jlimit(5.0f, 120.0f, ms);
        grainLengthSamples  = std::max(16, static_cast<int>(
            std::round((clamped * sampleRate) / 1000.0)));
    }

    void SetSearchRadiusMilliseconds(float ms)
    {
        const float clamped = juce::jlimit(0.5f, 16.0f, ms);
        searchRadiusSamples = std::max(4, static_cast<int>(
            std::round((clamped * sampleRate) / 1000.0)));
    }

    void SetLookbackMultiplier(float multiplier)
    {
        lookbackMultiplier = juce::jlimit(2.0f, 6.0f, multiplier);
    }

    void SetBoundaryCrossfadeMilliseconds(float ms)
    {
        const float clamped = juce::jlimit(0.0f, 30.0f, ms);
        boundaryCrossfadeSamples = std::max(0, static_cast<int>(
            std::round((clamped * sampleRate) / 1000.0)));
    }

    // Reports estimated latency based on grain length and lookback.
    float GetLatencyMilliseconds() const override
    {
        const WSOLAState& active = (activeIsA ? stateA : stateB);
        const float grainMs = static_cast<float>(grainLengthSamples) * 1000.0f
                            / static_cast<float>(sampleRate);

        const float effectiveLatencyMs = grainMs
            * (lookbackMultiplier + 0.5f * (1.0f - active.ratio));

        return std::max(1.0f, effectiveLatencyMs);
    }

private:
    // Produces one output sample from a WSOLAState by reading both voices,
    // advancing their positions and phases, recycling with WSOLA search
    // when a grain completes, and Hann-windowing the result.
    float processStateOneSample(WSOLAState& state)
    {
        const float sampleA = readCubic(state.voiceA.readIndex);
        const float sampleB = readCubic(state.voiceB.readIndex);

        state.voiceA.readIndex = wrapReadIndex(state.voiceA.readIndex + state.ratio);
        state.voiceB.readIndex = wrapReadIndex(state.voiceB.readIndex + state.ratio);

        const float phaseInc = 1.0f / static_cast<float>(grainLengthSamples);

        state.phaseA += phaseInc;
        if (state.phaseA >= 1.0f)
        {
            state.phaseA -= 1.0f;
            recycleVoiceWSOLA(state.voiceA, state.voiceB);
        }

        state.phaseB += phaseInc;
        if (state.phaseB >= 1.0f)
        {
            state.phaseB -= 1.0f;
            recycleVoiceWSOLA(state.voiceB, state.voiceA);
        }

        const float wA = hannWindow(state.phaseA);
        const float wB = hannWindow(state.phaseB);

        return sampleA * wA + sampleB * wB;
    }

    // When a voice finishes its grain, finds the best re-anchor position
    // near writeIndex - lookback using normalised cross-correlation (NCC).
    //
    // The reference signal is the input audio at the OTHER voice's current
    // read position (what is currently audible at high weight). Matching
    // against this ensures the new grain blends smoothly during the overlap.
    //
    // matchLength: number of input-buffer samples compared per candidate.
    // distancePenalty: small bias toward the expected position to prevent drift.
    void recycleVoiceWSOLA(Voice& recyclingVoice, const Voice& otherVoice)
    {
        const float lookback    = static_cast<float>(grainLengthSamples) * lookbackMultiplier;
        const float expectedPos = wrapReadIndex(static_cast<float>(writeIndex) - lookback);

        const int matchLength = std::max(32, grainLengthSamples / 4);

        float bestScore = -2.0f;
        float bestPos   = expectedPos;

        for (int delta = -searchRadiusSamples; delta <= searchRadiusSamples; ++delta)
        {
            const float candidatePos = wrapReadIndex(expectedPos + static_cast<float>(delta));

            float dot        = 0.0f;
            float energyRef  = 0.0f;
            float energyCand = 0.0f;

            for (int i = 0; i < matchLength; ++i)
            {
                const float ref  = readNearest(wrapReadIndex(
                    otherVoice.readIndex + static_cast<float>(i)));
                const float cand = readNearest(wrapReadIndex(
                    candidatePos + static_cast<float>(i)));

                dot        += ref * cand;
                energyRef  += ref * ref;
                energyCand += cand * cand;
            }

            const float denom = std::sqrt(
                std::max(energyRef * energyCand, 1.0e-12f));

            float score = dot / denom;
            score -= 0.001f * std::abs(static_cast<float>(delta));

            if (score > bestScore)
            {
                bestScore = score;
                bestPos   = candidatePos;
            }
        }

        recyclingVoice.readIndex = bestPos;
    }

    // Anchors both voices of a state to the current write-head lookback position.
    void anchorStateToWrite(WSOLAState& s)
    {
        anchorVoiceToWrite(s.voiceA);
        anchorVoiceToWrite(s.voiceB);
    }

    // Anchors a single voice to writeIndex - lookback (no WSOLA search).
    void anchorVoiceToWrite(Voice& voice)
    {
        const float lookback = static_cast<float>(grainLengthSamples) * lookbackMultiplier;
        voice.readIndex = wrapReadIndex(static_cast<float>(writeIndex) - lookback);
    }

    // Hann window: 0.5 - 0.5 * cos(2π * phase).
    // Two voices at 50% phase offset sum to exactly 1.0 at every sample.
    static float hannWindow(float phase01)
    {
        return 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * phase01);
    }

    // Wraps an index into the circular buffer range [0, bufferSize).
    float wrapReadIndex(float idx) const
    {
        const float size = static_cast<float>(buffer.size());
        float out = idx;
        while (out < 0.0f)  out += size;
        while (out >= size)  out -= size;
        return out;
    }

    // Nearest-sample read from the circular buffer (used for NCC search).
    float readNearest(float readIndexFloat) const
    {
        const int size = static_cast<int>(buffer.size());
        const int i = static_cast<int>(std::round(wrapReadIndex(readIndexFloat))) % size;
        return buffer[static_cast<size_t>(i)];
    }

    // Cubic Hermite interpolation read from the circular buffer (used for audio output).
    float readCubic(float readIndexFloat) const
    {
        const int size = static_cast<int>(buffer.size());
        const float wrapped = wrapReadIndex(readIndexFloat);
        const int i1 = static_cast<int>(std::floor(wrapped));
        const float frac = wrapped - static_cast<float>(i1);

        const int i0 = (i1 - 1 + size) % size;
        const int i2 = (i1 + 1) % size;
        const int i3 = (i1 + 2) % size;

        const float y0 = buffer[static_cast<size_t>(i0)];
        const float y1 = buffer[static_cast<size_t>(i1)];
        const float y2 = buffer[static_cast<size_t>(i2)];
        const float y3 = buffer[static_cast<size_t>(i3)];

        const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float a1 =  y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float a2 = -0.5f * y0 + 0.5f * y2;
        const float a3 =  y1;

        return ((a0 * frac + a1) * frac + a2) * frac + a3;
    }

    double sampleRate = 48000.0;
    std::vector<float> buffer;
    int writeIndex = 0;

    int   grainLengthSamples     = 1920;   // ~40 ms at 48 kHz
    int   searchRadiusSamples    = 192;    // ~4 ms at 48 kHz
    float lookbackMultiplier     = 3.0f;
    int   boundaryCrossfadeSamples = 192;  // ~4 ms at 48 kHz

    WSOLAState stateA;
    WSOLAState stateB;
    bool activeIsA = true;

    int  crossfadeTotalSamples     = 0;
    int  crossfadeRemainingSamples = 0;
    bool pendingFlipAfterFade      = false;
};
