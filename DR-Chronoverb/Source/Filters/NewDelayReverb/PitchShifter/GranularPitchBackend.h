#pragma once

// ============================ Granular pitch backend (echo-quantized) ============================
// Ratio changes are driven externally by OnEchoBoundary(newRatio).
// ProcessSample's pitchRatio parameter is ignored — the granular backend uses
// only the ratio set at the last boundary call.
class GranularPitchBackend : public IPitchShifterBackend
{
    struct ReadHead { float readIndex = 0.0f; };

    struct GrainState
    {
        ReadHead headA, headB, headC, headD;
        float phaseA = 0.0f, phaseB = 0.25f, phaseC = 0.5f, phaseD = 0.75f;

        // Per-head target ratios — committed on next grain reset for each head
        float ratioA = 1.0f, ratioB = 1.0f, ratioC = 1.0f, ratioD = 1.0f;

        // The pending ratio waiting to be picked up at each head's next reset
        float pendingRatio = 1.0f;
        bool hasPending = false;
    };

public:
    GranularPitchBackend() = default;

    void Prepare(double newSampleRate) override
    {
        sampleRate = newSampleRate;

        const int bufferMs = 300;
        const int bufferSize = std::max(
            2048,
            static_cast<int>(std::ceil((bufferMs * sampleRate) / 1000.0)));

        buffer.assign(static_cast<size_t>(bufferSize), 0.0f);

        SetGrainLengthMilliseconds(50.0f);
        SetJitterPercent(0.12f);
        SetLookbackMultiplier(4.0f);
        SetBoundaryCrossfadeMilliseconds(4.0f);

        Reset();
    }

    void Reset() override
    {
        setPRNGSeed(0xC0FFEEu);

        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;

        stateA = {};
        stateB = {};

        stateA.phaseA = 0.0f;
        stateA.phaseB = 0.25f;
        stateA.phaseC = 0.5f;
        stateA.phaseD = 0.75f;
        stateA.ratio  = 1.0f;
        stateB = stateA;

        anchorStateToWrite(stateA);
        anchorStateToWrite(stateB);

        crossfadeRemainingSamples = 0;
        crossfadeTotalSamples = boundaryCrossfadeSamples;
        activeIsA = true;
        pendingFlipAfterFade = false;
    }

    // ------------------------------------------------------------------
    // Called by OctaveEchoPitchShifter AFTER advancing the sequence.
    // newRatio is the ratio the next echo should play at.
    // ------------------------------------------------------------------
    void OnEchoBoundary(float newRatio) override
    {
        if (std::abs(newRatio - grainState.ratioA) < 1.0e-6f)
            return;

        grainState.pendingRatio = newRatio;
        grainState.hasPending = true;
    }

    // ------------------------------------------------------------------
    // Sets the initial ratio with no crossfade. Call once at PrepareToPlay
    // after CommitPendingSequenceNow to avoid a silent "wrong-pitch" first echo.
    // ------------------------------------------------------------------
    void SetInitialRatio(float ratio) override
    {
        stateA.ratio = ratio;
        stateB.ratio = ratio;

        stateA.phaseA = 0.0f;
        stateA.phaseB = 0.25f;
        stateA.phaseC = 0.5f;
        stateA.phaseD = 0.75f;
        stateB.phaseA = 0.0f;
        stateB.phaseB = 0.25f;
        stateB.phaseC = 0.5f;
        stateB.phaseD = 0.75f;

        anchorStateToWrite(stateA);
        anchorStateToWrite(stateB);

        crossfadeRemainingSamples = 0;
        pendingFlipAfterFade = false;
        activeIsA = true;
    }

    // pitchRatio is intentionally unused — ratio is now managed via OnEchoBoundary.
    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        if (buffer.empty())
            return inputSample;

        buffer[static_cast<size_t>(writeIndex)] = inputSample;
        writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());

        GrainState& active   = (activeIsA ? stateA : stateB);
        GrainState& inactive = (activeIsA ? stateB : stateA);

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
                activeIsA = !activeIsA;
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

    void SetJitterPercent(float percent) { jitterPercent = juce::jlimit(0.0f, 0.5f, percent); }
    void SetLookbackMultiplier(float multiplier){ lookbackMultiplier = juce::jlimit(2.0f, 6.0f, multiplier); }

    float GetLatencyMilliseconds() const override
    {
        const GrainState& active = (activeIsA ? stateA : stateB);
        const float grainMs = static_cast<float>(grainLengthSamples) * 1000.0f
                              / static_cast<float>(sampleRate);

        // Effective latency varies with ratio. At phase 0.5 (steady-state average
        // across 4 interleaved heads), the read head has consumed:
        //   0.5 * grainLength * (ratio - 1)
        // samples of the lookback, reducing the effective age of the output.
        const float effectiveLatencyMs = grainMs
            * (lookbackMultiplier + 0.5f * (1.0f - active.ratio));

        return std::max(1.0f, effectiveLatencyMs);
    }

private:
    float processStateOneSample(GrainState& newGrainState)
    {
        const float sampleA = readCubic(newGrainState.headA.readIndex);
        const float sampleB = readCubic(newGrainState.headB.readIndex);
        const float sampleC = readCubic(newGrainState.headC.readIndex);
        const float sampleD = readCubic(newGrainState.headD.readIndex);

        grainState.headA.readIndex = wrapReadIndex(grainState.headA.readIndex + grainState.ratioA);
        grainState.headB.readIndex = wrapReadIndex(grainState.headB.readIndex + grainState.ratioB);
        grainState.headC.readIndex = wrapReadIndex(grainState.headC.readIndex + grainState.ratioC);
        grainState.headD.readIndex = wrapReadIndex(grainState.headD.readIndex + grainState.ratioD);

        const float phaseInc = 1.0f / static_cast<float>(grainLengthSamples);

        newGrainState.phaseA += phaseInc;
        if (newGrainState.phaseA >= 1.0f)
        {
            grainState.phaseA -= 1.0f;

            if (grainState.hasPending)
                grainState.ratioA = grainState.pendingRatio;

            anchorHeadToWrite(grainState.headA, generateJitterSamples());
        }

        newGrainState.phaseB += phaseInc;
        if (newGrainState.phaseB >= 1.0f)
        {
            grainState.phaseB -= 1.0f;

            if (grainState.hasPending)
                grainState.ratioB = grainState.pendingRatio;

            anchorHeadToWrite(grainState.headB, generateJitterSamples());
        }

        newGrainState.phaseC += phaseInc;
        if (newGrainState.phaseC >= 1.0f)
        {
            grainState.phaseC -= 1.0f;

            if (grainState.hasPending)
                grainState.ratioC = grainState.pendingRatio;

            anchorHeadToWrite(grainState.headC, generateJitterSamples());
        }

        newGrainState.phaseD += phaseInc;
        if (newGrainState.phaseD >= 1.0f)
        {
            grainState.phaseD -= 1.0f;

            if (grainState.hasPending)
                grainState.ratioD = grainState.pendingRatio;

            anchorHeadToWrite(grainState.headD, generateJitterSamples());
        }

        // Clear hasPending once all four heads have committed:
        if (grainState.hasPending
            && grainState.ratioA == grainState.pendingRatio
            && grainState.ratioB == grainState.pendingRatio
            && grainState.ratioC == grainState.pendingRatio
            && grainState.ratioD == grainState.pendingRatio)
        {
            grainState.hasPending = false;
        }

        const float wA = hannWindow(newGrainState.phaseA);
        const float wB = hannWindow(newGrainState.phaseB);
        const float wC = hannWindow(newGrainState.phaseC);
        const float wD = hannWindow(newGrainState.phaseD);

        // Divide by 2 to compensate: 4 sqrt-Hann heads summing to ~2.0 at any point.
        return (sampleA * wA + sampleB * wB + sampleC * wC + sampleD * wD) * 0.5f;
    }

    void anchorStateToWrite(GrainState& s)
    {
        anchorHeadToWrite(s.headA, 0.0f);
        anchorHeadToWrite(s.headB, 0.0f);
        anchorHeadToWrite(s.headC, 0.0f);
        anchorHeadToWrite(s.headD, 0.0f);
    }

    void anchorHeadToWrite(ReadHead& readHead, float jitterOffsetSamples)
    {
        const float lookback = static_cast<float>(grainLengthSamples) * lookbackMultiplier;
        const float index = static_cast<float>(writeIndex);
        readHead.readIndex = wrapReadIndex(index - lookback + jitterOffsetSamples);
    }

    float generateJitterSamples() const
    {
        const float u01 = nextUniform01();          // [0, 1)
        const float u = (u01 * 2.0f) - 1.0f;        // [-1, 1)
        return u * jitterPercent * static_cast<float>(grainLengthSamples);
    }

    static float hannWindow(float phase01)
    {
        return 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * phase01);
    }

    float wrapReadIndex(float idx) const
    {
        const float size = static_cast<float>(buffer.size());
        float out = idx;
        while (out < 0.0f)    out += size;
        while (out >= size)   out -= size;
        return out;
    }

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

    // Realtime-safe per-instance PRNG (xorshift32).
    // Deterministic as long as seed is set deterministically.
    mutable uint32_t prngState = 0x12345678u;

    void setPRNGSeed(uint32_t seed)
    {
        // Avoid the all-zero state (xorshift degenerates).
        prngState = (seed != 0u ? seed : 0x6D2B79F5u);
    }

    // Returns [0, 1).
    float nextUniform01() const
    {
        uint32_t x = prngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        prngState = x;

        // Use top 24 bits -> float in [0, 1)
        const uint32_t mantissa = (x >> 8) & 0x00FFFFFFu;
        return static_cast<float>(mantissa) * (1.0f / 16777216.0f);
    }

    double sampleRate = 48000.0;
    std::vector<float> buffer;
    int writeIndex = 0;

    int grainLengthSamples = 1680;
    float jitterPercent = 0.12f;
    float lookbackMultiplier = 3.0f;

    //GrainState stateA;
    //GrainState stateB;
    //bool activeIsA = true;

    GrainState grainState;

    //int boundaryCrossfadeSamples = 0;
    //int crossfadeTotalSamples = 0;
    //int crossfadeRemainingSamples = 0;
    //bool pendingFlipAfterFade = false;
};