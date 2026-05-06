#include "GateLevelDisplay.h"
#include "Theme.h"

GateLevelDisplay::GateLevelDisplay(AudioPluginAudioProcessor& audioProcessor,
                                   VerticalRangeSlider& thresholdSlider)
    : processorRef(audioProcessor),
      rangeSliderRef(thresholdSlider)
{
    startTimerHz(30);
}

void GateLevelDisplay::timerCallback()
{
    float currentLevelDb = processorRef.getVisualInputLevelDb();

    levelHistory.add(currentLevelDb);

    while (levelHistory.size() > maximumHistorySize)
    {
        levelHistory.remove(0);
    }

    repaint();
}

float GateLevelDisplay::decibelsToY(float decibelValue) const
{
    juce::Rectangle<int> bounds = getLocalBounds();

    return juce::jmap(decibelValue,
                      -60.0f, 0.0f,
                      static_cast<float>(bounds.getBottom()),
                      static_cast<float>(bounds.getY()));
}

void GateLevelDisplay::paint(juce::Graphics& graphics)
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();

    graphics.setColour(AccentGray);
    graphics.fillRoundedRectangle(bounds, 8.0f);

    float thresholdLow = rangeSliderRef.getLowerValue();
    float thresholdHigh = rangeSliderRef.getUpperValue();

    float thresholdHighY = decibelsToY(thresholdHigh);
    float thresholdLowY = decibelsToY(thresholdLow);

    juce::Rectangle<float> activeRangeRectangle(
        bounds.getX(),
        thresholdHighY,
        bounds.getWidth(),
        thresholdLowY - thresholdHighY);

    graphics.setColour(ThemePink.withAlpha(0.15f));
    graphics.fillRoundedRectangle(activeRangeRectangle, 6.0f);

    graphics.setColour(ThemePink.withAlpha(0.35f));
    graphics.drawLine(bounds.getX(), thresholdLowY, bounds.getRight(), thresholdLowY, 1.5f);
    graphics.drawLine(bounds.getX(), thresholdHighY, bounds.getRight(), thresholdHighY, 1.5f);

    if (levelHistory.size() > 1)
    {
        juce::Path levelPath;

        float xStep = bounds.getWidth() / static_cast<float>(juce::jmax(1, maximumHistorySize - 1));

        for (int historyIndex = 0; historyIndex < levelHistory.size(); ++historyIndex)
        {
            float xPosition = bounds.getX() + xStep * static_cast<float>(historyIndex);
            float yPosition = decibelsToY(levelHistory.getUnchecked(historyIndex));

            if (historyIndex == 0)
            {
                levelPath.startNewSubPath(xPosition, yPosition);
            }
            else
            {
                levelPath.lineTo(xPosition, yPosition);
            }
        }

        graphics.setColour(juce::Colours::white);
        graphics.strokePath(levelPath, juce::PathStrokeType(2.0f));
    }
}

void GateLevelDisplay::resized()
{
}