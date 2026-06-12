#pragma once

#include "../ChronoverbUtils.h"

class DelayTimeSegment
{
public:
    const float MinimumBPM = 20.0f; // Silently breaks below this

    // Runtime
    float MaxDelayMS = 1.0f;
    int MaxDelaySamples = 0;

    int WritePeriodSamples = 1;
    int EchoWriteCounter = 0;

    // Outputs
    float DelayTimeMilliseconds = 0.0f;
    float ReadDelaySlewCoefficient = 0.0f;

    void PrepareToPlay(double newSampleRate)
    {
        sampleRate = newSampleRate;

        constexpr float MaxBeatMultiplier = 4.0f;
        constexpr float MaxDottedMultiplier = 1.5f;

        MaxDelayMS = (60000.0f / MinimumBPM) * MaxBeatMultiplier * MaxDottedMultiplier;
        MaxDelayMS = std::max(1.0f, MaxDelayMS);

        MaxDelaySamples = static_cast<int>(std::ceil((MaxDelayMS / 1000.0f) * sampleRate));
    }

    void SetHostTempo(float bpm)
    {
        hostBPM = bpm;
        UpdateDelayMillisecondsFromNormalized();
    }

    void UpdateDelayMillisecondsFromNormalized()
    {
        if (delayMode == 0) // ms — free range 1..1000 ms
        {
            const float baseMs = map01ToRange(delayTimeNormalized, 1.0f, 1000.0f);
            DelayTimeMilliseconds = std::max(1.0f, baseMs);

            // ms mode: fixed glide
            ReadDelaySlewCoefficient =
                1.0f / (0.08f * static_cast<float>(sampleRate));
        }
        else
        {
            // Beat-synced modes. Knob snaps to:
            // 0, 0.25, 0.5, 0.75, 1.0 => 1/1, 1/2, 1/4, 1/8, 1/16
            static constexpr float snapPositions[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            static constexpr float beatMultipliers[5] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };

            int stepIndex = 0;
            float smallestDistance = std::abs(delayTimeNormalized - snapPositions[0]);

            for (int i = 1; i < 5; ++i)
            {
                const float distance = std::abs(delayTimeNormalized - snapPositions[i]);

                if (distance < smallestDistance)
                {
                    smallestDistance = distance;
                    stepIndex = i;
                }
            }

            const float quarterNoteMs = 60000.0f / hostBPM;
            float beatMs = beatMultipliers[stepIndex] * quarterNoteMs;

            if (delayMode == 2)      // triplet
                beatMs *= (2.0f / 3.0f);
            else if (delayMode == 3) // dotted
                beatMs *= 1.5f;

            DelayTimeMilliseconds = juce::jlimit(1.0f, MaxDelayMS, beatMs);

            const float slewSeconds = std::max(0.05f, DelayTimeMilliseconds / 1000.0f);
            ReadDelaySlewCoefficient = 1.0f / (slewSeconds * static_cast<float>(sampleRate));
        }

        // IMPORTANT: always update write period for all modes
        WritePeriodSamples = std::max(
            1, static_cast<int>(std::round((DelayTimeMilliseconds * static_cast<float>(sampleRate)) / 1000.0f)));

        // Keep counters in range after timing changes
        EchoWriteCounter = juce::jlimit(0, WritePeriodSamples - 1, EchoWriteCounter);
    }

    // Parameters
    void SetDelayTime(float newDelayTime)
    {
        delayTimeNormalized = newDelayTime;
        UpdateDelayMillisecondsFromNormalized();
    }

    void SetDelayMode(int newDelayMode)
    {
        delayMode = newDelayMode;
        UpdateDelayMillisecondsFromNormalized();
    }

private:
    double sampleRate = 0.0f;
    float hostBPM = 120.0f;

    // Parameters
    float delayTimeNormalized = 0.0f;
    int delayMode = 0;
};