#pragma once

// Progressive octaves: starts at startOctave, steps by stepOctaves per echo,
// clamped to [lowerBound, upperBound].
class ProgressiveOctaveSequence : public IPitchSequence
{
public:
    ProgressiveOctaveSequence() {}

    void SetRange(int newLowerBound, int newUpperBound)
    {
        lowerBound = std::min(newLowerBound, newUpperBound);
        upperBound = std::max(newLowerBound, newUpperBound);
    }

    void SetStartOctave(int newStartOctave) { startOctave = newStartOctave; }
    void SetStepOctaves(int newStepOctaves) { stepOctaves = newStepOctaves; }

    void Reset() override
    {
        currentEchoIndex = 0;
        currentOctaves = startOctave;
    }

    void AdvanceToNextEcho() override
    {
        ++currentEchoIndex;

        const int rangeSize = upperBound - lowerBound + 1;

        if (rangeSize <= 0)
        {
            currentOctaves = lowerBound;
            return;
        }

        int wrappedIndex = (currentOctaves - lowerBound) + stepOctaves;
        wrappedIndex %= rangeSize;

        if (wrappedIndex < 0)
            wrappedIndex += rangeSize;

        currentOctaves = lowerBound + wrappedIndex;
    }

    float GetCurrentPitchRatio() const override
    {
        const int clamped = juce::jlimit(-4, 4, currentOctaves);
        return std::pow(2.0f, static_cast<float>(clamped));
    }

private:
    int stepOctaves = 1;
    int startOctave = 0;
    int lowerBound = -2;
    int upperBound = 2;
    int currentEchoIndex = 0;
    int currentOctaves = 0;
};