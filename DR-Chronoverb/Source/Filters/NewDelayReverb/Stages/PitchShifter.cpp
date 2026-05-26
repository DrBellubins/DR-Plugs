#include "PitchShifter.h"

void PitchShifter::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    constexpr float MaxBeatMultiplier = 4.0f;
    constexpr float MaxDottedMultiplier = 1.5f;

    maxDelayMS = (60000.0f / MinimumBPM) * MaxBeatMultiplier * MaxDottedMultiplier;
}

void PitchShifter::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{

}

float PitchShifter::ProcessSample(float inputSample)
{

}

// --- Parameters ---
void PitchShifter::SetHostTempo(float bpm)
{
    hostBPM = bpm;
    updateDelayMillisecondsFromNormalized();
}

void PitchShifter::SetDelayTime(float newDelayTime)
{
    delayTimeNormalized = newDelayTime;
    updateDelayMillisecondsFromNormalized();
}

void PitchShifter::SetDelayMode(int newDelayMode)
{
    delayMode = newDelayMode;
    updateDelayMillisecondsFromNormalized();
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

    configureShifter(pitchShifterLeft);
    configureShifter(pitchShifterRight);
}

//endregion