#include "PitchShifter.h"

PitchShifter::PitchShifter()
{
    delay = std::make_unique<Delay>();
}

void PitchShifter::PrepareToPlay(double newSampleRate, Filters& filters)
{
    sampleRate = newSampleRate;
    filtersInput = &filters;

    // Delay time
    delayTimeSegment.PrepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMilliseconds();

    // Pitch shifter
    echoWriteCounter = 0;

    pitchShifterLeft.Prepare(sampleRate);
    pitchShifterRight.Prepare(sampleRate);

    pitchShifterLeft.SetEnabled(true);
    pitchShifterRight.SetEnabled(true);

    pitchShifterLeft.CommitPendingSequenceNow();
    pitchShifterRight.CommitPendingSequenceNow();

    // Reverb line
    delay->PrepareToPlay(sampleRate, *filtersInput);

    // Various
    smoothedCenteredReadDelayMilliseconds = delayTimeSegment.DelayTimeMilliseconds;
    readDelaySlewCoefficient = delayTimeSegment.ReadDelaySlewCoefficient;
    writePeriodSamples = delayTimeSegment.WritePeriodSamples;

    rebuildPitchSequences();
}

void PitchShifter::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (pitchSequenceRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildPitchSequences();

    delay->ProcessBlock(audioBuffer);

    pitchShifterLatencyMs = pitchShifterLeft.GetLatencyMilliseconds();

    readDelaySlewCoefficient = delayTimeSegment.ReadDelaySlewCoefficient;
    writePeriodSamples = delayTimeSegment.WritePeriodSamples;
}

std::pair<float, float> PitchShifter::ProcessSample(float inputSampleL, float inputSampleR)
{
    if (delayLineLeft == nullptr || delayLineRight == nullptr)
        return std::make_pair(inputSampleL, inputSampleR);

    // 1) Pre-read latency compensation.
    smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
            (delayTimeSegment.DelayTimeMilliseconds - smoothedCenteredReadDelayMilliseconds);

    const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;
    const float preReadMs = std::max(1.0f, nominalReadMilliseconds - pitchShifterLatencyMs);

    const float preReadWetLeft = delayLineLeft->ReadFeedbackBuffer(preReadMs);
    const float preReadWetRight = delayLineRight->ReadFeedbackBuffer(preReadMs);

    // 2) Process pitch shifter
    float pitchedLeft = inputSampleL;
    float pitchedRight = inputSampleR;

    if (pitchWetMix > 0.0001f)
    {
        pitchedLeft = pitchShifterLeft.ProcessSample(preReadWetLeft);
        pitchedRight = pitchShifterRight.ProcessSample(preReadWetRight);

        auto [diffPitchedLeft, diffPitchedRight] =
            delay->ProcessSample(pitchedLeft, pitchedRight);

        // Blend clean and diffused  diffusion amount to 0 -> 0.5
        const float lowerHalf01 = juce::jlimit(0.0f, 1.0f, diffusionAmount * 2.0f);

        const float cleanGain = std::pow(1.0f - lowerHalf01, 3.0f);
        const float diffusedGain = std::sin(lowerHalf01 * juce::MathConstants<float>::halfPi) * 0.75f;

        const float makeupGain =
            1.0f + (0.12f * std::sin(lowerHalf01 * juce::MathConstants<float>::pi));

        // Blend
        pitchedLeft =
            ((pitchedLeft * cleanGain) + (diffPitchedLeft * diffusedGain)) * makeupGain;

        pitchedRight =
            ((pitchedRight * cleanGain) + (diffPitchedRight * diffusedGain)) * makeupGain;
    }

    // 3) Advance echo boundary counters (needed regardless of pitch enable state)
    {
        ++echoWriteCounter;
        if (echoWriteCounter >= writePeriodSamples)
        {
            echoWriteCounter = 0;

            pitchShifterLeft.OnNewEchoBoundary();
            pitchShifterRight.OnNewEchoBoundary();
        }
    }

    pitchedLeft = PMath::EqualPowerCrossfade(inputSampleL, pitchedLeft, pitchWetMix);
    pitchedRight = PMath::EqualPowerCrossfade(inputSampleR, pitchedRight, pitchWetMix);

    return std::make_pair(pitchedLeft, pitchedRight);
}

//region Parameters

void PitchShifter::SetHostTempo(float bpm)
{
    hostBPM = bpm;

    delayTimeSegment.SetHostTempo(hostBPM);
    delayTimeSegment.UpdateDelayMilliseconds();

    delay->SetHostTempo(hostBPM);
}

/*void PitchShifter::SetDelayTime(float newDelayTime)
{
    delayTimeNormalized = newDelayTime;
    delayTimeSegment.SetDelayTime(newDelayTime);

    delay->SetDelayTime(delayTimeNormalized);
}*/

void PitchShifter::SetDelayMode(int newDelayMode)
{
    delayMode = newDelayMode;
    delayTimeSegment.SetDelayMode(newDelayMode);

    delay->SetDelayMode(delayMode);
}

void PitchShifter::SetFeebackTime(float newFeedbackTime)
{
    delay->SetFeedbackTime(newFeedbackTime);
}

void PitchShifter::SetDiffusionAmount(float newDiffusionAmount)
{
    diffusionAmount = newDiffusionAmount;
    delay->SetDiffusionAmount(diffusionAmount);
}

void PitchShifter::SetDiffusionSize(float newDiffusionSize)
{
    diffusionSize = newDiffusionSize;
    delay->SetDiffusionSize(diffusionSize);
}

void PitchShifter::SetDiffusionQuality(int newDiffusionQuality)
{
    diffusionQualityStages = newDiffusionQuality;
    delay->SetDiffusionQuality(diffusionQualityStages);
}

void PitchShifter::SetFiltersOrder(int newFiltersOrder)
{
    filtersOrder = newFiltersOrder;
    delay->SetFiltersOrder(filtersOrder);
}

void PitchShifter::SetPitchRate(float newPitchRate)
{
    pitchRateMs = newPitchRate;
    delayTimeSegment.SetDelayTime(pitchRateMs);
    delayTimeSegment.UpdateDelayMilliseconds();

    delay->SetDelayTime(pitchRateMs);
}

void PitchShifter::SetPitchRangeLower(float pitchRangeLowerSemitones)
{
    pitchRangeLower = pitchRangeLowerSemitones;
    pitchSequenceRebuildPending.store(true, std::memory_order_release);
}

void PitchShifter::SetPitchRangeUpper(float pitchRangeUpperSemitones)
{
    pitchRangeUpper = pitchRangeUpperSemitones;
    pitchSequenceRebuildPending.store(true, std::memory_order_release);
}

void PitchShifter::SetPitchSequence(int sequenceIndex)
{
    pitchSequence = sequenceIndex;
    pitchSequenceRebuildPending.store(true, std::memory_order_release);
}

void PitchShifter::SetPitchWetMix(float newPitchWetMix)
{
    pitchWetMix = newPitchWetMix;
}

//endregion

//region Update Functions

void PitchShifter::SetDelayLines(DelayLine& newDelayLineLeft, DelayLine& newDelayLineRight)
{
    delayLineLeft = &newDelayLineLeft;
    delayLineRight = &newDelayLineRight;
}

void PitchShifter::rebuildPitchSequences()
{
    int lowerOctave = semitonesToOctaveIndex(pitchRangeLower);
    int upperOctave = semitonesToOctaveIndex(pitchRangeUpper);

    if (lowerOctave > upperOctave)
        std::swap(lowerOctave, upperOctave);

    auto configureShifter = [&](OctaveEchoPitchShifter& shifter)
    {
        if (pitchSequence == 3) // Up-Down
        {
            auto pingPongSequence = std::make_unique<PingPongOctaveSequence>();
            pingPongSequence->SetRange(lowerOctave, upperOctave);
            pingPongSequence->SetStartOctave(lowerOctave);
            pingPongSequence->SetInitialDirection(1);
            shifter.SetSequence(std::move(pingPongSequence));
        }
        else if (pitchSequence == 2) // Random
        {
            auto randomSequence = std::make_unique<RandomOctaveSequence>();
            randomSequence->SetRange(lowerOctave, upperOctave);
            shifter.SetSequence(std::move(randomSequence));
        }
        else
        {
            auto progressiveSequence = std::make_unique<ProgressiveOctaveSequence>();
            progressiveSequence->SetRange(lowerOctave, upperOctave);

            if (pitchSequence == 0) // Up
            {
                progressiveSequence->SetStartOctave(lowerOctave);
                progressiveSequence->SetStepOctaves(1);
            }
            else // Down
            {
                progressiveSequence->SetStartOctave(upperOctave);
                progressiveSequence->SetStepOctaves(-1);
            }

            shifter.SetSequence(std::move(progressiveSequence));
        }
    };

    configureShifter(pitchShifterLeft);
    configureShifter(pitchShifterRight);
}

//endregion