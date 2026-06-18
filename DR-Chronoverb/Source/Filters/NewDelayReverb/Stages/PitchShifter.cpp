#include "PitchShifter.h"

PitchShifter::PitchShifter()
{
    reverb = std::make_unique<Reverb>();
}

void PitchShifter::PrepareToPlay(double newSampleRate, Filters& filters)
{
    sampleRate = newSampleRate;
    filtersInput = &filters;

    // Delay time
    delayTimeSegment.PrepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMilliseconds();

    // Delay line
    delayLineLeft = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);
    delayLineRight = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);

    // Pitch shifter
    echoWriteCounter = 0;

    pitchShifterLeft.Prepare(sampleRate);
    pitchShifterRight.Prepare(sampleRate);

    pitchShifterLeft.SetEnabled(true);
    pitchShifterRight.SetEnabled(true);

    rebuildPitchSequences();
    pitchShifterLeft.CommitPendingSequenceNow();
    pitchShifterRight.CommitPendingSequenceNow();

    // Reverb line
    reverb->PrepareToPlay(sampleRate, *filtersInput);

    // Various
    smoothedCenteredReadDelayMilliseconds = delayTimeSegment.DelayTimeMilliseconds;
    readDelaySlewCoefficient = delayTimeSegment.ReadDelaySlewCoefficient;
    writePeriodSamples = delayTimeSegment.WritePeriodSamples;
}

void PitchShifter::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (pitchSequenceRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildPitchSequences();

    reverb->ProcessBlock(audioBuffer);

    pitchShifterLatencyMs = pitchShifterLeft.GetLatencyMilliseconds();

    readDelaySlewCoefficient = delayTimeSegment.ReadDelaySlewCoefficient;
    writePeriodSamples = delayTimeSegment.WritePeriodSamples;
}

std::pair<float, float> PitchShifter::ProcessSample(float inputSampleL, float inputSampleR)
{
    if (pitchWetMix <= 0.0001f)
        return std::make_pair(inputSampleL, inputSampleR);

    delayLineLeft->PushSample(inputSampleL);
    delayLineRight->PushSample(inputSampleR);

    // 1) Pre-read latency compensation.
    smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
            (delayTimeSegment.DelayTimeMilliseconds - smoothedCenteredReadDelayMilliseconds);

    const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;
    const float preReadMs = std::max(1.0f, nominalReadMilliseconds - pitchShifterLatencyMs);

    const float preReadWetLeft = delayLineLeft->ReadFeedbackBuffer(preReadMs);
    const float preReadWetRight = delayLineRight->ReadFeedbackBuffer(preReadMs);

    float pitchedLeft = pitchShifterLeft.ProcessSample(preReadWetLeft);
    float pitchedRight = pitchShifterRight.ProcessSample(preReadWetRight);

    // Advance echo boundary
    ++echoWriteCounter;
    if (echoWriteCounter >= writePeriodSamples)
    {
        echoWriteCounter = 0;
        pitchShifterLeft.OnNewEchoBoundary();
        pitchShifterRight.OnNewEchoBoundary();
    }

    // 2) Diffuse pitch tap through reverb
    auto [diffPitchedLeft, diffPitchedRight] =
        reverb->ProcessSample(pitchedLeft, pitchedRight);

    const float lowerHalf01 = std::clamp(diffusionAmount * 2.0f, 0.0f, 1.0f);
    const float cleanGain = std::pow(1.0f - lowerHalf01, 3.0f);
    const float diffusedGain = std::sin(lowerHalf01 * juce::MathConstants<float>::halfPi) * 0.75f;
    const float makeupGain =
        1.0f + (0.12f * std::sin(lowerHalf01 * juce::MathConstants<float>::pi));

    pitchedLeft =
        ((pitchedLeft * cleanGain) + (diffPitchedLeft * diffusedGain)) * makeupGain;

    pitchedRight =
        ((pitchedRight * cleanGain) + (diffPitchedRight * diffusedGain)) * makeupGain;

    // 3) Blend input wet with pitched signal
    const float dryWetPitch = std::clamp(pitchWetMix, 0.0f, 1.0f);

    const float outLeft =
        (inputSampleL * (1.0f - dryWetPitch)) + (pitchedLeft * dryWetPitch);

    const float outRight =
        (inputSampleR * (1.0f - dryWetPitch)) + (pitchedRight * dryWetPitch);

    return std::make_pair(outLeft, outRight);
}

//region Parameters

void PitchShifter::SetHostTempo(float bpm)
{
    hostBPM = bpm;

    delayTimeSegment.SetHostTempo(hostBPM);
    delayTimeSegment.UpdateDelayMilliseconds();

    reverb->SetHostTempo(hostBPM);
}

void PitchShifter::SetDelayTime(float newDelayTime)
{
    delayTimeMs = newDelayTime;
    delayTimeSegment.SetDelayTime(newDelayTime);

    reverb->SetDelayTime(delayTimeMs);
}

void PitchShifter::SetDelayMode(int newDelayMode)
{
    delayMode = newDelayMode;
    delayTimeSegment.SetDelayMode(newDelayMode);

    reverb->SetDelayMode(delayMode);
}

void PitchShifter::SetFeebackTime(float newFeedbackTime)
{
    reverb->SetFeedbackTime(newFeedbackTime);
}

void PitchShifter::SetDiffusionAmount(float newDiffusionAmount)
{
    diffusionAmount = newDiffusionAmount;
    reverb->SetDiffusionAmount(diffusionAmount);
}

void PitchShifter::SetDiffusionSize(float newDiffusionSize)
{
    diffusionSize = newDiffusionSize;
    reverb->SetDiffusionSize(diffusionSize);
}

void PitchShifter::SetDiffusionQuality(int newDiffusionQuality)
{
    // Limit quality to save on CPU usage.
    diffusionQualityStages = std::clamp(newDiffusionQuality, 1, 3);
    reverb->SetDiffusionQuality(diffusionQualityStages);
}

void PitchShifter::SetFiltersOrder(int newFiltersOrder)
{
    filtersOrder = newFiltersOrder;
    reverb->SetFiltersOrder(filtersOrder);
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
            // TODO: Random isn't synced between L/R channels
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