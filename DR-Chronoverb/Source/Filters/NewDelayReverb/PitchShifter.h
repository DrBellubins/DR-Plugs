#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdlib>

// ============================ Pitch ratio sequence API ============================

class IPitchSequence
{
public:
    virtual ~IPitchSequence() = default;

    virtual void Reset()
    {
    }

    virtual void AdvanceToNextEcho()
    {
    }

    // Pitch ratio for current echo (1.0 = unison, 2.0 = +1 octave, 0.5 = -1 octave).
    virtual float GetCurrentPitchRatio() const = 0;
};

// Progressive octaves: starts at 0 (unison), then steps by stepOctaves per echo.
class ProgressiveOctaveSequence : public IPitchSequence
{
public:
    ProgressiveOctaveSequence()
    {
    }

    void SetStepOctaves(int newStepOctaves)
    {
        stepOctaves = newStepOctaves;
    }

    void SetMaxAbsOctaves(int newMaxAbsOctaves)
    {
        maxAbsOctaves = std::max(0, newMaxAbsOctaves);
    }

    void Reset() override
    {
        currentEchoIndex = 0;
        // Fix: Start at 0 (unison) — first echo is dry pitch, second echo is +stepOctaves, etc.
        // This avoids immediately pitch-shifting echo #0.
        currentOctaves = 0;
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
        return std::pow(2.0f, static_cast<float>(currentOctaves));
    }

private:
    int stepOctaves    = 1;
    int maxAbsOctaves  = 0;
    int currentEchoIndex = 0;
    int currentOctaves = 0;
};

// ============================ Pitch shifter backend API ============================

class IPitchShifterBackend
{
public:
    virtual ~IPitchShifterBackend() = default;

    virtual void Prepare(double newSampleRate, int maximumBlockSize)
    {
        (void)newSampleRate;
        (void)maximumBlockSize;
    }

    virtual void Reset()
    {
    }

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
    float pitchRatio = 1.0f;
};

// DirectionalOctaveSequence
// Steps through octave values from lowerSemitones to upperSemitones (or reverse),
// wrapping around when the end is reached.
// Semitone values are snapped to the nearest multiple of 12.
class DirectionalOctaveSequence : public IPitchSequence
{
public:
    DirectionalOctaveSequence()
    {
    }

    void SetRange(float newLowerSemitones, float newUpperSemitones)
    {
        lowerSemitones = newLowerSemitones;
        upperSemitones = newUpperSemitones;
        BuildPool();
    }

    void SetDirectionUp(bool shouldGoUp)
    {
        directionUp = shouldGoUp;
    }

    void Reset() override
    {
        BuildPool();

        if (pool.empty())
        {
            currentIndex = 0;
            return;
        }

        currentIndex = directionUp ? 0 : static_cast<int>(pool.size()) - 1;
    }

    void AdvanceToNextEcho() override
    {
        if (pool.empty())
            return;

        if (directionUp)
        {
            ++currentIndex;

            if (currentIndex >= static_cast<int>(pool.size()))
                currentIndex = 0;
        }
        else
        {
            --currentIndex;

            if (currentIndex < 0)
                currentIndex = static_cast<int>(pool.size()) - 1;
        }
    }

    float GetCurrentPitchRatio() const override
    {
        if (pool.empty())
            return 1.0f;

        const float semitones = pool[static_cast<size_t>(currentIndex)];
        return std::pow(2.0f, semitones / 12.0f);
    }

private:
    void BuildPool()
    {
        pool.clear();

        // Snap lower/upper to nearest multiple of 12 and step through them
        const float snappedLower = std::round(lowerSemitones / 12.0f) * 12.0f;
        const float snappedUpper = std::round(upperSemitones / 12.0f) * 12.0f;
        const float low  = std::min(snappedLower, snappedUpper);
        const float high = std::max(snappedLower, snappedUpper);

        for (float semitoneValue = low; semitoneValue <= high + 0.01f; semitoneValue += 12.0f)
            pool.push_back(semitoneValue);
    }

    float lowerSemitones = -12.0f;
    float upperSemitones =  12.0f;
    bool  directionUp    = true;
    int   currentIndex   = 0;

    std::vector<float> pool;
};

// RandomOctaveSequence
// Picks a random octave value within the semitone range each echo.
// Avoids repeating the same value back-to-back when the pool has more than one entry.
class RandomOctaveSequence : public IPitchSequence
{
public:
    RandomOctaveSequence()
    {
    }

    void SetRange(float newLowerSemitones, float newUpperSemitones)
    {
        lowerSemitones = newLowerSemitones;
        upperSemitones = newUpperSemitones;
        BuildPool();
    }

    void Reset() override
    {
        BuildPool();

        if (pool.empty())
        {
            currentSemitones = 0.0f;
            return;
        }

        currentSemitones = pool[static_cast<size_t>(rand()) % pool.size()];
    }

    void AdvanceToNextEcho() override
    {
        if (pool.empty())
            return;

        if (pool.size() == 1)
        {
            currentSemitones = pool[0];
            return;
        }

        float chosen = currentSemitones;

        for (int attempt = 0; attempt < 20; ++attempt)
        {
            chosen = pool[static_cast<size_t>(rand()) % pool.size()];

            if (chosen != currentSemitones)
                break;
        }

        currentSemitones = chosen;
    }

    float GetCurrentPitchRatio() const override
    {
        return std::pow(2.0f, currentSemitones / 12.0f);
    }

private:
    void BuildPool()
    {
        pool.clear();

        const float snappedLower = std::round(lowerSemitones / 12.0f) * 12.0f;
        const float snappedUpper = std::round(upperSemitones / 12.0f) * 12.0f;
        const float low  = std::min(snappedLower, snappedUpper);
        const float high = std::max(snappedLower, snappedUpper);

        for (float semitoneValue = low; semitoneValue <= high + 0.01f; semitoneValue += 12.0f)
            pool.push_back(semitoneValue);
    }

    float lowerSemitones  = -12.0f;
    float upperSemitones  =  12.0f;
    float currentSemitones = 0.0f;

    std::vector<float> pool;
};

// ============================ Granular pitch backend ============================
// Dual read-head delay/grain pitch shifter.
//
// Key tuning parameters:
// - grainLengthMs: 20–50ms is the sweet spot for shimmer. Longer = more smear + latency.
//   150ms (old default) was far too long.
// - lookbackMultiplier: 3.0 is safe for ratios ≤ 2× (one octave up/down).
//   Increase to 4.0 if using ratios > 2×.
// - jitterPercent: 0.10–0.20 breaks up the periodic anchor pattern that causes
//   metallic buzz artifacts.
//
// Latency introduced = grainLengthSamples * lookbackMultiplier samples.
// At 35ms grain + 3× lookback + 48kHz: ~5040 samples / ~105ms latency.
// This latency is absorbed into the reverb feedback path — not directly perceptible.
class GranularPitchBackend : public IPitchShifterBackend
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

        // Buffer is 200ms — plenty for any lookback at our grain sizes.
        const int bufferMilliseconds = 200;
        const int bufferSizeSamples = std::max(
            1024,
            static_cast<int>(std::ceil((bufferMilliseconds * sampleRate) / 1000.0)));

        buffer.assign(static_cast<size_t>(bufferSizeSamples), 0.0f);

        // Default: 35ms grains. This is tunable via SetGrainLengthMilliseconds().
        SetGrainLengthMilliseconds(35.0f);
        Reset();
    }

    void Reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        writeIndex = 0;
        smoothedPitchRatio = 1.0f;

        // Start the two grains half a grain-length apart in phase so their
        // Hann windows sum to unity at all times: hann(t) + hann(t+0.5) = 1.0
        grainPhaseA = 0.0f;
        grainPhaseB = 0.5f;

        anchorHeadToWrite(headA, 0.0f);
        anchorHeadToWrite(headB, 0.0f);
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

        // Advance grain A phase; re-anchor when it wraps
        grainPhaseA += phaseIncrement;

        if (grainPhaseA >= 1.0f)
        {
            grainPhaseA -= 1.0f;
            // Jitter anchor position to break up metallic periodic artifact
            const float jitterSamples = generateJitter();
            anchorHeadToWrite(headA, jitterSamples);
        }

        // Advance grain B phase; re-anchor when it wraps
        grainPhaseB += phaseIncrement;

        if (grainPhaseB >= 1.0f)
        {
            grainPhaseB -= 1.0f;
            const float jitterSamples = generateJitter();
            anchorHeadToWrite(headB, jitterSamples);
        }

        // Read from each head using cubic interpolation
        const float sampleA = readCubic(headA.ReadIndex);
        const float sampleB = readCubic(headB.ReadIndex);

        // Advance read positions by the pitch ratio
        headA.ReadIndex = wrapReadIndex(headA.ReadIndex + smoothedPitchRatio);
        headB.ReadIndex = wrapReadIndex(headB.ReadIndex + smoothedPitchRatio);

        // Hann windows offset by half a period guarantee unity sum at all times
        const float windowA = hannWindow(grainPhaseA);
        const float windowB = hannWindow(grainPhaseB);

        return (sampleA * windowA) + (sampleB * windowB);
    }

    // Grain length in milliseconds. 20–50ms recommended for shimmer.
    // Shorter = crisper / more transient-friendly, but slightly more grainy.
    // Longer = smoother but more smear and latency.
    void SetGrainLengthMilliseconds(float newGrainLengthMilliseconds)
    {
        const float clamped = juce::jlimit(5.0f, 500.0f, newGrainLengthMilliseconds);
        grainLengthSamples = std::max(16, static_cast<int>(std::round((clamped * sampleRate) / 1000.0)));
    }

    // Jitter as a fraction of grain length. 0.10–0.20 recommended.
    // Reduces metallic/buzzing artifact from deterministic grain anchoring.
    void SetJitterPercent(float newJitterPercent)
    {
        jitterPercent = juce::jlimit(0.0f, 0.5f, newJitterPercent);
    }

    // Lookback multiplier. 3.0 is safe for ratios ≤ 2×.
    // Increase to 4.0–5.0 for ratios up to 4×.
    void SetLookbackMultiplier(float newLookbackMultiplier)
    {
        lookbackMultiplier = juce::jlimit(2.0f, 6.0f, newLookbackMultiplier);
    }

    float GetLatencyMilliseconds() const
    {
        const float lookbackSamples =
            static_cast<float>(grainLengthSamples) * lookbackMultiplier;

        return static_cast<float>((lookbackSamples * 1000.0) / sampleRate);
    }

private:
    // Exponential smoothing toward target pitch ratio.
    // Coefficient of 0.05 converges in ~20 samples (~0.4ms at 48kHz) — fast enough.
    void smoothPitchRatio(float targetPitchRatio)
    {
        const float smoothingCoefficient = 0.05f;
        smoothedPitchRatio += smoothingCoefficient * (targetPitchRatio - smoothedPitchRatio);
    }

    // Place the read head behind the write head by (lookbackMultiplier × grainLength) samples,
    // optionally offset by a jitter amount to break up periodic anchor patterns.
    void anchorHeadToWrite(ReadHead& readHead, float jitterOffsetSamples)
    {
        const float lookbackSamples =
            static_cast<float>(grainLengthSamples) * lookbackMultiplier;

        const float writeFloat = static_cast<float>(writeIndex);
        readHead.ReadIndex = wrapReadIndex(writeFloat - lookbackSamples + jitterOffsetSamples);
    }

    // Returns a random jitter offset in samples, bounded to ±(jitterPercent × grainLength).
    float generateJitter() const
    {
        // Simple uniform random in [-1, +1]
        const float unitRandom =
            (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;

        return unitRandom * jitterPercent * static_cast<float>(grainLengthSamples);
    }

    static float hannWindow(float phase01)
    {
        // hann(t) = 0.5 - 0.5 * cos(2π * t)
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

    float readCubic(float readIndexFloat) const
    {
        const int size = static_cast<int>(buffer.size());
        const float wrapped = wrapReadIndex(readIndexFloat);
        const int index1 = static_cast<int>(std::floor(wrapped));
        const float frac  = wrapped - static_cast<float>(index1);

        const int index0 = (index1 - 1 + size) % size;
        const int index2 = (index1 + 1)         % size;
        const int index3 = (index1 + 2)         % size;

        const float y0 = buffer[static_cast<size_t>(index0)];
        const float y1 = buffer[static_cast<size_t>(index1)];
        const float y2 = buffer[static_cast<size_t>(index2)];
        const float y3 = buffer[static_cast<size_t>(index3)];

        const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float a1 =         y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float a2 = -0.5f * y0              + 0.5f * y2;
        const float a3 =                    y1;

        return ((a0 * frac + a1) * frac + a2) * frac + a3;
    }

    double sampleRate = 48000.0;

    std::vector<float> buffer;
    int writeIndex = 0;

    int grainLengthSamples = 1680; // Default ~35ms at 48kHz

    ReadHead headA;
    ReadHead headB;

    float grainPhaseA = 0.0f;
    float grainPhaseB = 0.5f;

    float smoothedPitchRatio = 1.0f;

    // 3.0 is safe for max ratio of 2× (one octave up/down).
    // For max ratio 4×, increase to 5.0.
    float lookbackMultiplier = 3.0f;

    // Jitter fraction of grain length (0.15 = ±15% of grain length).
    float jitterPercent = 0.15f;
};

// Passthrough backend for testing/bypassing without touching architecture.
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
// Integrates into NewDelayReverb feedback path.
// Owns a sequence + backend; exposes OnNewEchoBoundary() so the delay can increment pitch.
class OctaveEchoPitchShifter
{
public:
    OctaveEchoPitchShifter()
    {
        auto granularBackend = std::make_unique<GranularPitchBackend>();
        granularBackend->SetGrainLengthMilliseconds(35.0f);  // Was 150ms — now 35ms
        granularBackend->SetJitterPercent(0.15f);             // Break up metallic artifacts
        granularBackend->SetLookbackMultiplier(3.0f);         // 3× is safe for ≤1 octave shift

        auto progressiveSequence = std::make_unique<ProgressiveOctaveSequence>();
        progressiveSequence->SetStepOctaves(1);    // +1 octave per echo
        progressiveSequence->SetMaxAbsOctaves(2);  // Clamp at ±2 octaves

        SetSequence(std::move(progressiveSequence));
        SetBackend(std::move(granularBackend));
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
    }

    void SetEnabled(bool shouldBeEnabled)
    {
        enabled.store(shouldBeEnabled, std::memory_order_release);
    }

    bool GetEnabled() const
    {
        return enabled.load(std::memory_order_acquire);
    }

    // Call this when your delay produces a new echo generation.
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

    ProgressiveOctaveSequence* GetProgressiveOctaveSequence()
    {
        return dynamic_cast<ProgressiveOctaveSequence*>(sequence.get());
    }

    GranularPitchBackend* GetGranularBackend()
    {
        return dynamic_cast<GranularPitchBackend*>(backend.get());
    }

    float GetLatencyMilliseconds() const
    {
        auto* granularBackend = dynamic_cast<GranularPitchBackend*>(backend.get());

        if (granularBackend != nullptr && GetEnabled())
            return granularBackend->GetLatencyMilliseconds();

        return 0.0f;
    }

    // Rebuild the active sequence based on mode index and semitone range.
    // modeIndex: 0 = Up, 1 = Down, 2 = Random
    void RebuildSequence(int modeIndex, float lowerSemitones, float upperSemitones)
    {
        if (modeIndex == 2)
        {
            auto randomSequence = std::make_unique<RandomOctaveSequence>();
            randomSequence->SetRange(lowerSemitones, upperSemitones);
            SetSequence(std::move(randomSequence));
        }
        else
        {
            auto directionalSequence = std::make_unique<DirectionalOctaveSequence>();
            directionalSequence->SetRange(lowerSemitones, upperSemitones);
            directionalSequence->SetDirectionUp(modeIndex == 0);
            SetSequence(std::move(directionalSequence));
        }
    }

private:
    double sampleRate = 48000.0;
    int maximumBlockSizeCached = 0;

    std::unique_ptr<IPitchSequence> sequence;
    std::unique_ptr<IPitchShifterBackend> backend;

    std::atomic<bool> enabled { false };
};