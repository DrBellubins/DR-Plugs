#include "PitchShifter.h"

PitchShifter::PitchShifter(DelayLine& newDelayLine)
    : delayLine(&newDelayLine)
{
    delayLine = &newDelayLine;
}

void PitchShifter::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    // Delay time
    delayTimeSegment.PepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();

    // Delay line
    //delayLine = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);

    //delayLine.Clear();
    //delayLine.SetSampleRate(sampleRate);

    // Pitch shifter
    echoWriteCounter = 0;

    pitchShifter.Prepare(sampleRate);
    pitchShifter.SetEnabled(true);

    pitchShifter.CommitPendingSequenceNow();

    // Various
    rebuildPitchSequences();
}

void PitchShifter::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (pitchSequenceRebuildPending.exchange(false, std::memory_order_acq_rel))
        rebuildPitchSequences();

    pitchShifterLatencyMs = pitchShifter.GetLatencyMilliseconds();
}

float PitchShifter::ProcessSample(float inputSample)
{
    if (delayLine == nullptr)
        return inputSample;

    // 0) Write sample to buffer
    //delayLine->PushSample(inputSample);

    // 1) Pre-read latency compensation.
    smoothedCenteredReadDelayMilliseconds += readDelaySlewCoefficient *
            (delayTimeSegment.DelayTimeMilliseconds - smoothedCenteredReadDelayMilliseconds);

    const float nominalReadMilliseconds = smoothedCenteredReadDelayMilliseconds;
    const float preReadMs = std::max(1.0f, nominalReadMilliseconds - pitchShifterLatencyMs);

    const float preReadWet  = delayLine->ReadFeedbackBuffer(preReadMs);

    // 2) Process pitch shifter
    float pitched = inputSample;

    if (pitchWetMix > 0.0001f)
    {
        pitched = pitchShifter.ProcessSample(preReadWet);

        // TODO: Implement pitch reverb line.

        /*float diffPitchedLeft = pitchDiffusion->ProcessSample(pitched);

        pitched = PMath::EqualPowerCrossfade(pitched, diffPitchedLeft, diffusionAmount);*/
    }

    // 3) Advance echo boundary counters (needed regardless of pitch enable state)
    {
        ++echoWriteCounter;
        if (echoWriteCounter >= writePeriodSamples)
        {
            echoWriteCounter = 0;
            pitchShifter.OnNewEchoBoundary();
        }
    }

    // TODO: Temporary, until reverb line implemented
    pitched = PMath::EqualPowerCrossfade(inputSample, pitched, pitchWetMix);

    return pitched;
}

// --- Parameters ---
void PitchShifter::SetHostTempo(float bpm)
{
    hostBPM = bpm;

    delayTimeSegment.SetHostTempo(bpm);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();
}

void PitchShifter::SetDelayTime(float newDelayTime)
{
    delayTimeNormalized = newDelayTime;

    delayTimeSegment.SetDelayTime(newDelayTime);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();
}

void PitchShifter::SetDelayMode(int newDelayMode)
{
    delayMode = newDelayMode;

    delayTimeSegment.SetDelayMode(newDelayMode);
    delayTimeSegment.UpdateDelayMillisecondsFromNormalized();
}

void PitchShifter::SetDiffusionAmount(float newDiffusionAmount)
{
    diffusionAmount = newDiffusionAmount;
}

void PitchShifter::SetDiffusionSize(float newDiffusionSize)
{
    diffusionSize = newDiffusionSize;
}

void PitchShifter::SetDiffusionQuality(int newDiffusionQuality)
{
    diffusionQualityStages = newDiffusionQuality;
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
    pitchMode = sequenceIndex;
    pitchSequenceRebuildPending.store(true, std::memory_order_release);
}

void PitchShifter::SetPitchWetMix(float newPitchWetMix)
{
    pitchWetMix = newPitchWetMix;
}

//region Update Functions

void PitchShifter::rebuildPitchSequences()
{
    int lowerOctave = semitonesToOctaveIndex(pitchRangeLower);
    int upperOctave = semitonesToOctaveIndex(pitchRangeUpper);

    if (lowerOctave > upperOctave)
        std::swap(lowerOctave, upperOctave);

    auto configureShifter = [&](OctaveEchoPitchShifter& shifter)
    {
        if (pitchMode == 3) // Up-Down
        {
            auto pingPongSequence = std::make_unique<PingPongOctaveSequence>();
            pingPongSequence->SetRange(lowerOctave, upperOctave);
            pingPongSequence->SetStartOctave(lowerOctave);
            pingPongSequence->SetInitialDirection(1);
            shifter.SetSequence(std::move(pingPongSequence));
        }
        else if (pitchMode == 2) // Random
        {
            auto randomSequence = std::make_unique<RandomOctaveSequence>();
            randomSequence->SetRange(lowerOctave, upperOctave);
            shifter.SetSequence(std::move(randomSequence));
        }
        else
        {
            auto progressiveSequence = std::make_unique<ProgressiveOctaveSequence>();
            progressiveSequence->SetRange(lowerOctave, upperOctave);

            if (pitchMode == 0) // Up
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

    configureShifter(pitchShifter);
}

//endregion