#pragma once

class PingPongOctaveSequence : public IPitchSequence
{
public:
    PingPongOctaveSequence() = default;

    void SetRange(int newLowerBound, int newUpperBound)
    {
        lowerBound = std::min(newLowerBound, newUpperBound);
        upperBound = std::max(newLowerBound, newUpperBound);
    }

    void SetStartOctave(int newStartOctave)
    {
        startOctave = juce::jlimit(lowerBound, upperBound, newStartOctave);
    }

    void SetInitialDirection(int newDirection)
    {
        direction = (newDirection >= 0 ? 1 : -1);
    }

    void Reset() override
    {
        currentOctave = juce::jlimit(lowerBound, upperBound, startOctave);

        if (lowerBound == upperBound)
            direction = 1;
    }

    void AdvanceToNextEcho() override
    {
        if (lowerBound == upperBound)
            return;

        int next = currentOctave + direction;

        if (next > upperBound)
        {
            direction = -1;
            next = upperBound - 1;
        }
        else if (next < lowerBound)
        {
            direction = 1;
            next = lowerBound + 1;
        }

        currentOctave = juce::jlimit(lowerBound, upperBound, next);
    }

    float GetCurrentPitchRatio() const override
    {
        const int clamped = juce::jlimit(-4, 4, currentOctave);
        return std::pow(2.0f, static_cast<float>(clamped));
    }

private:
    int lowerBound = -2;
    int upperBound = 2;
    int startOctave = 0;
    int currentOctave = 0;
    int direction = 1;
};
