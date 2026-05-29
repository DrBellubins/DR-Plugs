#pragma once

#include <cmath>
#include <juce_dsp/juce_dsp.h>

#include "../DelayLine.h"

// ============================ Granular pitch backend (echo-quantized) ============================
// Ratio changes are driven externally by OnEchoBoundary(newRatio).
// ProcessSample's pitchRatio parameter is ignored — the granular backend uses
// only the ratio set at the last boundary call.
class GranularPitchBackend : public IPitchShifterBackend
{
    struct ReadHead { float samplesBehindWriteHead = 0.0f; };

    struct GrainState
    {
        ReadHead headA;
        ReadHead headB;
        ReadHead headC;
        ReadHead headD;
        float phaseA = 0.0f;
        float phaseB = 0.25f;
        float phaseC = 0.5f;
        float phaseD = 0.75f;
        float ratio  = 1.0f;
    };

public:
    GranularPitchBackend() = default;

    void Prepare(double newSampleRate) override
    {
        sampleRate = newSampleRate;

        /*SetGrainLengthMilliseconds(50.0f);
        SetJitterPercent(0.12f);
        SetLookbackMultiplier(4.0f);
        SetBoundaryCrossfadeMilliseconds(4.0f);*/

        Reset();
    }

    void Reset() override
    {
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
        GrainState& active = (activeIsA ? stateA : stateB);
        GrainState& inactive = (activeIsA ? stateB : stateA);

        if (std::abs(newRatio - active.ratio) < 1.0e-6f)
            return;

        inactive = active;
        inactive.ratio = newRatio;

        if (boundaryCrossfadeSamples <= 0)
        {
            // No crossfade configured — swap instantly.
            // Grain phases and read heads are identical to the old state;
            // only the advancement rate changes from here, which is smooth.
            activeIsA = !activeIsA;
            pendingFlipAfterFade = false;
            crossfadeRemainingSamples = 0;
            crossfadeTotalSamples = 0;
        }
        else
        {
            crossfadeTotalSamples = boundaryCrossfadeSamples;
            crossfadeRemainingSamples = crossfadeTotalSamples;
            pendingFlipAfterFade = true;
        }
    }

    // ------------------------------------------------------------------
    // Sets the initial ratio with no crossfade. Call once at PrepareToPlay
    // after CommitPendingSequenceNow to avoid a silent "wrong-pitch" first echo.
    // ------------------------------------------------------------------
    void SetInitialRatio(float ratio) override
    {
        const float m_ratio = juce::jlimit(0.25f, 4.0f, ratio);
        stateA.ratio = m_ratio;
        stateB.ratio = m_ratio;

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
        juce::ignoreUnused(inputSample);

        if (sourceDelayLine == nullptr)
            return inputSample;

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

    void SetBoundaryCrossfadeMilliseconds(float ms)
    {
        const float clamped = juce::jlimit(0.0f, 30.0f, ms);

        boundaryCrossfadeSamples = std::max(0, static_cast<int>(
            std::round((clamped * sampleRate) / 1000.0)));
    }

    void SetSourceDelayLine(DelayLine* newDelayLine) override
    {
        sourceDelayLine = newDelayLine;
    }

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
    float processStateOneSample(GrainState& grainState)
    {
        const float sampleA = readFromDelayLine(grainState.headA.samplesBehindWriteHead);
        const float sampleB = readFromDelayLine(grainState.headB.samplesBehindWriteHead);
        const float sampleC = readFromDelayLine(grainState.headC.samplesBehindWriteHead);
        const float sampleD = readFromDelayLine(grainState.headD.samplesBehindWriteHead);

        grainState.headA.samplesBehindWriteHead -= grainState.ratio;
        grainState.headB.samplesBehindWriteHead -= grainState.ratio;
        grainState.headC.samplesBehindWriteHead -= grainState.ratio;
        grainState.headD.samplesBehindWriteHead -= grainState.ratio;

        const float phaseInc = 1.0f / static_cast<float>(grainLengthSamples);

        grainState.phaseA += phaseInc;
        if (grainState.phaseA >= 1.0f)
        {
            grainState.phaseA -= 1.0f;
            anchorHeadToWrite(grainState.headA, generateJitterSamples());
        }

        grainState.phaseB += phaseInc;
        if (grainState.phaseB >= 1.0f)
        {
            grainState.phaseB -= 1.0f;
            anchorHeadToWrite(grainState.headB, generateJitterSamples());
        }

        grainState.phaseC += phaseInc;
        if (grainState.phaseC >= 1.0f)
        {
            grainState.phaseC -= 1.0f;
            anchorHeadToWrite(grainState.headC, generateJitterSamples());
        }

        grainState.phaseD += phaseInc;
        if (grainState.phaseD >= 1.0f)
        {
            grainState.phaseD -= 1.0f;
            anchorHeadToWrite(grainState.headD, generateJitterSamples());
        }

        const float wA = hannWindow(grainState.phaseA);
        const float wB = hannWindow(grainState.phaseB);
        const float wC = hannWindow(grainState.phaseC);
        const float wD = hannWindow(grainState.phaseD);

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
        const float baseLookback = static_cast<float>(grainLengthSamples) * lookbackMultiplier;
        const float safetyLookback = static_cast<float>(grainLengthSamples) * std::max(1.0f, stateA.ratio);
        const float lookback = std::max(baseLookback, safetyLookback);

        readHead.samplesBehindWriteHead = std::max(1.0f, lookback + jitterOffsetSamples);
    }

    float generateJitterSamples() const
    {
        const float u = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
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

    float readFromDelayLine(float samplesBehindWriteHead) const
    {
        if (sourceDelayLine == nullptr)
            return 0.0f;

        return sourceDelayLine->ReadSamplesBehindWrite(samplesBehindWriteHead);
    }

    double sampleRate = 48000.0;

    DelayLine* sourceDelayLine = nullptr;
    std::vector<float> buffer;
    int writeIndex = 0;

    int grainLengthSamples = 1680;
    float jitterPercent = 0.12f;
    float lookbackMultiplier = 3.0f;

    GrainState stateA;
    GrainState stateB;
    bool activeIsA = true;

    int boundaryCrossfadeSamples = 0;
    int crossfadeTotalSamples = 0;
    int crossfadeRemainingSamples = 0;
    bool pendingFlipAfterFade = false;
};