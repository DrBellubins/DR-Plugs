#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include <atomic>

// ============================ Pitch ratio sequence API ============================
// The sequence decides which pitch ratio applies to the current echo iteration.
// Later you can add Random, PingPong, CustomPattern, MIDI-scale quantized, etc.

class IPitchSequence
{
public:
    virtual ~IPitchSequence() = default;

    virtual void Reset()
    {
    }

    // Called when a new echo iteration begins (typically at the delay wrap / tap event).
    virtual void AdvanceToNextEcho()
    {
    }

    // Pitch ratio for current echo (1.0 = unison, 2.0 = +1 octave, 0.5 = -1 octave).
    virtual float GetCurrentPitchRatio() const = 0;
};

// Progressive octaves: 0, +1, +2, +3... (or negative if stepOctaves is negative).
class ProgressiveOctaveSequence : public IPitchSequence
{
public:
    ProgressiveOctaveSequence()
    {
    }

    void SetStepOctaves(int stepOctavesIn)
    {
        stepOctaves = stepOctavesIn;
    }

    void SetMaxAbsOctaves(int maxAbsOctavesIn)
    {
        maxAbsOctaves = std::max(0, maxAbsOctavesIn);
    }

    void Reset() override
    {
        currentEchoIndex = 0;
        currentOctaves = stepOctaves;
    }

    void AdvanceToNextEcho() override
    {
        ++currentEchoIndex;

        const int nextOctaves = currentOctaves + stepOctaves;

        if (maxAbsOctaves > 0)
        {
            currentOctaves = std::max(-maxAbsOctaves, std::min(maxAbsOctaves, nextOctaves));
        }
        else
        {
            currentOctaves = nextOctaves;
        }
    }

    float GetCurrentPitchRatio() const override
    {
        // ratio = 2^(octaves)
        return std::pow(2.0f, static_cast<float>(currentOctaves));
    }

private:
    int stepOctaves = 1;
    int maxAbsOctaves = 0; // 0 = unlimited

    int currentEchoIndex = 0;
    int currentOctaves = 0;
};

// ============================ Pitch shifter backend API ============================
// Backend is responsible for actually shifting pitch by ratio.
// Keep it interface-based so you can replace implementation without touching the sequencing.

class IPitchShifterBackend
{
public:
    virtual ~IPitchShifterBackend() = default;

    virtual void Prepare(double sampleRate, int maximumBlockSize)
    {
        (void)sampleRate;
        (void)maximumBlockSize;
    }

    virtual void Reset()
    {
    }

    // Process a single sample for now. If you later want a better algorithm, switch to block processing.
    virtual float ProcessSample(float inputSample, float pitchRatio) = 0;
};

class ConstantRatioSequence : public IPitchSequence
{
public:
    ConstantRatioSequence()
    {
    }

    void SetPitchRatio(float newPitchRatio)
    {
        pitchRatio = newPitchRatio;
    }

    void Reset() override
    {
    }

    void AdvanceToNextEcho() override
    {
    }

    float GetCurrentPitchRatio() const override
    {
        return pitchRatio;
    }

private:
    float pitchRatio = 2.0f; // default: +1 octave so it's obvious
};

// ============================ Granular pitch backend ============================
// Dual read-head delay/grain pitch shifter.
//
// Notes:
// - This introduces a small algorithmic latency roughly equal to grainLengthSamples.
// - For shimmer-style echo stepping, call OctaveEchoPitchShifter::OnNewEchoBoundary()
//   to change pitch ratio per echo, but this backend itself is continuous and will adapt.
/*class GranularPitchBackend : public IPitchShifterBackend
{
    struct ReadHead
    {
        float ReadIndex = 0.0f;
    };

public:
    GranularPitchBackend()
    {
    }

    void Prepare(double newSampleRate, int maximumBlockSize) override
    {
        juce::ignoreUnused(maximumBlockSize);

        sampleRate = newSampleRate;

        const int bufferMilliseconds = 200;
        const int bufferSizeSamples = std::max(1024, static_cast<int>(std::ceil((bufferMilliseconds * sampleRate) / 1000.0)));

        buffer.assign(static_cast<size_t>(bufferSizeSamples), 0.0f);

        SetGrainLengthMilliseconds(30.0f);
        Reset();
    }

    void Reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        writeIndex = 0;
        smoothedPitchRatio = 1.0f;

        // Start the two grains half a grain-length apart in phase so their
        // crossfade windows always sum to unity.
        grainPhaseA = 0.0f;
        grainPhaseB = 0.5f;

        anchorHeadToWrite(headA);
        anchorHeadToWrite(headB);
    }

    float ProcessSample(float inputSample, float pitchRatio) override
    {
        if (buffer.empty())
            return inputSample;

        const float clampedPitchRatio = juce::jlimit(0.25f, 4.0f, pitchRatio);
        smoothPitchRatio(clampedPitchRatio);

        // Write incoming sample
        buffer[static_cast<size_t>(writeIndex)] = inputSample;
        writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());

        const float phaseIncrement = 1.0f / static_cast<float>(grainLengthSamples);

        // Advance grain A
        const float prevPhaseA = grainPhaseA;
        grainPhaseA += phaseIncrement;

        if (grainPhaseA >= 1.0f)
        {
            grainPhaseA -= 1.0f;
            anchorHeadToWrite(headA);
        }

        // Advance grain B
        const float prevPhaseB = grainPhaseB;
        grainPhaseB += phaseIncrement;

        if (grainPhaseB >= 1.0f)
        {
            grainPhaseB -= 1.0f;
            anchorHeadToWrite(headB);
        }

        // Read from each head
        const float sampleA = readLinear(headA.ReadIndex);
        const float sampleB = readLinear(headB.ReadIndex);

        // Advance read positions by the pitch ratio
        headA.ReadIndex = wrapReadIndex(headA.ReadIndex + smoothedPitchRatio);
        headB.ReadIndex = wrapReadIndex(headB.ReadIndex + smoothedPitchRatio);

        // Hann windows offset by half a period guarantee unity sum:
        // hann(phase) + hann(phase + 0.5) = 1.0
        const float windowA = hannWindow(grainPhaseA);
        const float windowB = hannWindow(grainPhaseB);

        return (sampleA * windowA) + (sampleB * windowB);
    }

    void SetGrainLengthMilliseconds(float newGrainLengthMilliseconds)
    {
        const float clamped = juce::jlimit(5.0f, 80.0f, newGrainLengthMilliseconds);
        grainLengthSamples = std::max(16, static_cast<int>(std::round((clamped * sampleRate) / 1000.0)));
    }

    float GetLatencyMilliseconds() const
    {
        const float lookbackSamples = static_cast<float>(grainLengthSamples) * 5.0f;
        return static_cast<float>((lookbackSamples * 1000.0) / sampleRate);
    }

private:
    void smoothPitchRatio(float targetPitchRatio)
    {
        // Much faster coefficient — converges in ~100 samples rather than thousands.
        const float smoothingCoefficient = 0.05f;
        smoothedPitchRatio += smoothingCoefficient * (targetPitchRatio - smoothedPitchRatio);
    }

    void anchorHeadToWrite(ReadHead& readHead)
    {
        // Place the read head a safe distance behind the write head.
        // For pitch ratios up to 4x, the read head advances at most 4x per sample,
        // so over one grain it travels grainLength * 4 samples.
        // We need the head to never catch the write pointer during a grain's lifetime.
        // A lookback of grainLength * (maxRatio + 1) is safe.
        const float lookbackSamples = static_cast<float>(grainLengthSamples) * 5.0f;
        const float writeFloat = static_cast<float>(writeIndex);
        readHead.ReadIndex = wrapReadIndex(writeFloat - lookbackSamples);
    }

    static float hannWindow(float phase01)
    {
        // Hann window: w(t) = 0.5 - 0.5 * cos(2 * pi * t)
        // Key property: hann(t) + hann(t + 0.5) = 1.0 for all t.
        const float twoPi = 2.0f * juce::MathConstants<float>::pi;
        return 0.5f - 0.5f * std::cos(twoPi * phase01);
    }

    float wrapReadIndex(float index) const
    {
        const float size = static_cast<float>(buffer.size());
        float out = index;

        while (out < 0.0f)
            out += size;

        while (out >= size)
            out -= size;

        return out;
    }

    float readLinear(float readIndexFloat) const
    {
        const int size = static_cast<int>(buffer.size());
        const float wrapped = wrapReadIndex(readIndexFloat);

        const int index0 = static_cast<int>(std::floor(wrapped));
        const int index1 = (index0 + 1) % size;
        const float fraction = wrapped - static_cast<float>(index0);

        return buffer[static_cast<size_t>(index0)] * (1.0f - fraction)
             + buffer[static_cast<size_t>(index1)] * fraction;
    }

    double sampleRate = 48000.0;

    std::vector<float> buffer;
    int writeIndex = 0;

    int grainLengthSamples = 1440;

    ReadHead headA;
    ReadHead headB;

    float grainPhaseA = 0.0f;
    float grainPhaseB = 0.5f;

    float smoothedPitchRatio = 1.0f;
};*/

class GranularPitchBackend : public IPitchShifterBackend
{
    struct ReadHead
    {
        float ReadIndex    = 0.0f;
        float BlendGain    = 1.0f; // 1.0 = fully active, 0.0 = silent (fading out after teleport)
        bool  IsFading     = false;
        float FadeProgress = 0.0f; // 0..1 over CrossfadeSamples
    };

public:
    GranularPitchBackend()
    {
    }

    void Prepare(double newSampleRate, int maximumBlockSize) override
    {
        juce::ignoreUnused(maximumBlockSize);

        sampleRate = newSampleRate;

        const int bufferMilliseconds = 500;
        const int bufferSizeSamples = std::max(
            1024,
            static_cast<int>(std::ceil((bufferMilliseconds * sampleRate) / 1000.0))
        );

        buffer.assign(static_cast<size_t>(bufferSizeSamples), 0.0f);

        // Crossfade zone: ~6 ms — only active when a head needs to teleport
        crossfadeSamples = std::max(
            16,
            static_cast<int>(std::round((6.0f * sampleRate) / 1000.0))
        );

        // Safe lookback so the faster head never catches the write pointer within one crossfade window.
        // pitchRatio can be up to 4x, so head travels crossfadeSamples * 4 ahead in the worst case.
        lookbackSamples = static_cast<int>(crossfadeSamples * 6);

        Reset();
    }

    void Reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        writeIndex      = 0;
        smoothedPitchRatio = 1.0f;

        // Place heads offset by half the lookback so only one is ever near the wrap point at a time.
        anchorHeadToWrite(headA);
        headB.ReadIndex = wrapReadIndex(headA.ReadIndex - static_cast<float>(lookbackSamples) * 0.5f);
        headB.BlendGain    = 1.0f;
        headB.IsFading     = false;
        headB.FadeProgress = 0.0f;
    }

    float ProcessSample(float inputSample, float pitchRatio) override
    {
        if (buffer.empty())
            return inputSample;

        const float clampedPitchRatio = juce::jlimit(0.25f, 4.0f, pitchRatio);
        smoothPitchRatio(clampedPitchRatio);

        // Write
        buffer[static_cast<size_t>(writeIndex)] = inputSample;
        writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());

        // Advance both heads by the pitch ratio
        headA.ReadIndex = wrapReadIndex(headA.ReadIndex + smoothedPitchRatio);
        headB.ReadIndex = wrapReadIndex(headB.ReadIndex + smoothedPitchRatio);

        // Check if either head is about to catch the write pointer; if so, start a crossfade
        checkAndTriggerTeleport(headA, headB);
        checkAndTriggerTeleport(headB, headA);

        // Tick fade progress on fading heads
        tickFade(headA);
        tickFade(headB);

        // Read and mix
        const float sampleA = readCubic(headA.ReadIndex) * headA.BlendGain;
        const float sampleB = readCubic(headB.ReadIndex) * headB.BlendGain;

        // Gains sum to 1.0 outside fade events (one head at 1, other at 0),
        // and blend through 0..1 during the brief crossfade.
        return sampleA + sampleB;
    }

    float GetLatencyMilliseconds() const
    {
        return static_cast<float>((lookbackSamples * 1000.0) / sampleRate);
    }

private:
    // Only smooth on ratio changes to avoid zipper; converges in ~200 samples
    void smoothPitchRatio(float targetPitchRatio)
    {
        const float smoothingCoefficient = 0.02f;
        smoothedPitchRatio += smoothingCoefficient * (targetPitchRatio - smoothedPitchRatio);
    }

    void anchorHeadToWrite(ReadHead& readHead)
    {
        readHead.ReadIndex    = wrapReadIndex(static_cast<float>(writeIndex) - static_cast<float>(lookbackSamples));
        readHead.BlendGain    = 1.0f;
        readHead.IsFading     = false;
        readHead.FadeProgress = 0.0f;
    }

    // If thisHead is within the danger zone ahead of the write pointer, teleport it and
    // hand its blend duty to otherHead for the crossfade duration.
    void checkAndTriggerTeleport(ReadHead& thisHead, ReadHead& otherHead)
    {
        if (thisHead.IsFading)
            return; // already being faded out; don't re-trigger

        const int bufferSize = static_cast<int>(buffer.size());
        const float distanceAhead = wrapReadIndex(
            static_cast<float>(writeIndex) - thisHead.ReadIndex
        );

        // distanceAhead < danger threshold => head is about to lap the write pointer
        const float dangerSamples = static_cast<float>(crossfadeSamples) * 2.0f;

        if (distanceAhead < dangerSamples)
        {
            juce::ignoreUnused(bufferSize);

            // Start fading this head out over crossfadeSamples
            thisHead.IsFading     = true;
            thisHead.FadeProgress = 0.0f;

            // Ensure the other head is active and placed safely
            if (otherHead.IsFading)
            {
                // Edge case: both heads somehow fading at once — hard-reset other
                anchorHeadToWrite(otherHead);
            }
            else
            {
                // Make sure it's at full gain
                otherHead.BlendGain = 1.0f;
            }
        }
    }

    void tickFade(ReadHead& readHead)
    {
        if (!readHead.IsFading)
            return;

        readHead.FadeProgress += 1.0f / static_cast<float>(crossfadeSamples);

        if (readHead.FadeProgress >= 1.0f)
        {
            // Fade complete — teleport the head to a safe position and bring it back to standby
            anchorHeadToWrite(readHead);
            readHead.BlendGain    = 0.0f; // stays silent until it's needed again
            readHead.IsFading     = false;
            readHead.FadeProgress = 0.0f;
        }
        else
        {
            // Ease-out fade: cosine so the amplitude ramp is perceptually smooth
            const float cosPhase = readHead.FadeProgress * juce::MathConstants<float>::pi;
            readHead.BlendGain = 0.5f + 0.5f * std::cos(cosPhase); // 1 -> 0
        }
    }

    // 4-point cubic (Catmull-Rom) interpolation — much lower aliasing than linear under resampling
    float readCubic(float readIndexFloat) const
    {
        const int size = static_cast<int>(buffer.size());

        const float wrapped = wrapReadIndex(readIndexFloat);
        const int   index1  = static_cast<int>(std::floor(wrapped));
        const float frac    = wrapped - static_cast<float>(index1);

        const int index0 = (index1 - 1 + size) % size;
        const int index2 = (index1 + 1)         % size;
        const int index3 = (index1 + 2)         % size;

        const float y0 = buffer[static_cast<size_t>(index0)];
        const float y1 = buffer[static_cast<size_t>(index1)];
        const float y2 = buffer[static_cast<size_t>(index2)];
        const float y3 = buffer[static_cast<size_t>(index3)];

        // Catmull-Rom spline
        const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float a1 =        y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float a2 = -0.5f * y0             + 0.5f * y2;
        const float a3 =                   y1;

        return ((a0 * frac + a1) * frac + a2) * frac + a3;
    }

    float wrapReadIndex(float index) const
    {
        const float size = static_cast<float>(buffer.size());
        float out = index;

        while (out < 0.0f)
            out += size;

        while (out >= size)
            out -= size;

        return out;
    }

    double sampleRate = 48000.0;

    std::vector<float> buffer;
    int writeIndex = 0;

    int crossfadeSamples = 288;  // ~6 ms @ 48 kHz
    int lookbackSamples  = 1728; // crossfadeSamples * 6

    ReadHead headA;
    ReadHead headB;

    float smoothedPitchRatio = 1.0f;
};

// Placeholder backend: currently does NO pitch shifting (passes through).
// This lets you wire the architecture now and drop in a real algorithm later.
class PassthroughPitchBackend : public IPitchShifterBackend
{
public:
    PassthroughPitchBackend()
    {
    }

    float ProcessSample(float inputSample, float pitchRatio) override
    {
        (void)pitchRatio;
        return inputSample;
    }
};

// ============================ Octave-per-echo pitch extension ============================
// This is the class you integrate into NewDelayReverb feedback path.
// It owns a sequence + backend and exposes "OnNewEchoBoundary" so your delay can increment.

class OctaveEchoPitchShifter
{
public:
    OctaveEchoPitchShifter()
    {
        auto Backend = std::make_unique<GranularPitchBackend>();
        //Backend->SetGrainLengthMilliseconds(15.0f);

        //auto ConstantSequence = std::make_unique<ConstantRatioSequence>();
        //ConstantSequence->SetPitchRatio(2.0f); // very obvious

        auto Progressive = std::make_unique<ProgressiveOctaveSequence>();
        Progressive->SetStepOctaves(1);      // +1 octave per echo (use -1 for downward)
        Progressive->SetMaxAbsOctaves(4);    // clamp at ±4 octaves (48 semitones)

        SetSequence(std::move(Progressive));
        SetBackend(std::move(Backend));
    }

    void Prepare(double newSampleRate, int maximumBlockSize)
    {
        sampleRate = newSampleRate;
        maximumBlockSizeCached = maximumBlockSize;

        if (backend != nullptr)
            backend->Prepare(sampleRate, maximumBlockSize);

        Reset();
    }

    void Reset()
    {
        if (sequence != nullptr)
            sequence->Reset();

        if (backend != nullptr)
            backend->Reset();

        //enabled.store(false, std::memory_order_release);
    }

    void SetEnabled(bool shouldBeEnabled)
    {
        enabled.store(shouldBeEnabled, std::memory_order_release);
    }

    bool GetEnabled() const
    {
        return enabled.load(std::memory_order_acquire);
    }

    // Call this at the moment your delay produces a "new echo generation".
    // Practical trigger options:
    // - When your delay write index wraps (fixed delay = easier)
    // - When sampleCounter reaches delaySamples (if you track it)
    // - When you detect the tap boundary you consider "next echo"
    void OnNewEchoBoundary()
    {
        if (sequence != nullptr)
            sequence->AdvanceToNextEcho();
    }

    float ProcessSample(float inputSample)
    {
        if (!GetEnabled())
            return inputSample;

        if (sequence == nullptr || backend == nullptr)
            return inputSample;

        const float pitchRatio = sequence->GetCurrentPitchRatio();
        return backend->ProcessSample(inputSample, pitchRatio);
    }

    // ---- Extension points ----
    void SetSequence(std::unique_ptr<IPitchSequence> newSequence)
    {
        sequence = std::move(newSequence);

        if (sequence != nullptr)
            sequence->Reset();
    }

    void SetBackend(std::unique_ptr<IPitchShifterBackend>&& newBackend)
    {
        backend = std::move(newBackend);

        if (backend != nullptr)
        {
            backend->Prepare(sampleRate, maximumBlockSizeCached);
            backend->Reset();
        }
    }

    // Convenience access for configuring the default progressive sequence without downcasting elsewhere.
    ProgressiveOctaveSequence* GetProgressiveOctaveSequence()
    {
        return dynamic_cast<ProgressiveOctaveSequence*>(sequence.get());
    }

    float GetLatencyMilliseconds() const
    {
        auto* granularBackend = dynamic_cast<GranularPitchBackend*>(backend.get());

        if (granularBackend != nullptr && GetEnabled())
            return granularBackend->GetLatencyMilliseconds();

        return 0.0f;
    }

private:
    double sampleRate = 48000.0;
    int maximumBlockSizeCached = 0;

    std::unique_ptr<IPitchSequence> sequence;
    std::unique_ptr<IPitchShifterBackend> backend;

    std::atomic<bool> enabled { false };
};