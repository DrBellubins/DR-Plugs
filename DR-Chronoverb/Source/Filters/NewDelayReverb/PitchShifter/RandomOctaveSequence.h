#pragma once

#include <cmath>

#include "PitchShiftingUtils.h"

// Random octave sequence: picks a random octave within [lowerBound, upperBound]
// each echo, avoiding an immediate back-to-back repeat when more than one choice exists.
class RandomOctaveSequence : public IPitchSequence
{
public:
    RandomOctaveSequence() { BuildOctaveList(); }

    void SetRange(int newLowerBound, int newUpperBound)
    {
        lowerBound = std::min(newLowerBound, newUpperBound);
        upperBound = std::max(newLowerBound, newUpperBound);
        BuildOctaveList();
    }

    void Reset() override
    {
        lastOctave = INT_MIN;
        currentOctave = PickRandom(lastOctave);
    }

    void AdvanceToNextEcho() override
    {
        lastOctave = currentOctave;
        currentOctave = PickRandom(lastOctave);
    }

    float GetCurrentPitchRatio() const override
    {
        const int clamped = juce::jlimit(-4, 4, currentOctave);

        return std::pow(2.0f, static_cast<float>(clamped));
    }

private:
    void BuildOctaveList()
    {
        octaves.clear();

        for (int o = lowerBound; o <= upperBound; ++o)
            octaves.push_back(o);
    }

    int PickRandom(int excludeOctave) const
    {
        if (octaves.empty())
            return 0;

        if (static_cast<int>(octaves.size()) == 1)
            return octaves[0];

        std::vector<int> candidates;
        candidates.reserve(octaves.size());

        for (int o : octaves)
        {
            if (o != excludeOctave)
                candidates.push_back(o);
        }

        if (candidates.empty())
            return octaves[0];

        return candidates[static_cast<size_t>(rand()) % candidates.size()];
    }

    int lowerBound = -2;
    int upperBound =  2;
    int currentOctave =  0;
    int lastOctave = INT_MIN;
    std::vector<int> octaves;
};