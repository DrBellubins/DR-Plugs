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
    currentLevelDecibels = processorRef.getVisualInputLevelDb();
    repaint();
}

float GateLevelDisplay::decibelsToY(float decibelValue) const
{
    juce::Rectangle<int> localBounds = getLocalBounds();

    float clampedDecibelValue = juce::jlimit(-60.0f, 0.0f, decibelValue);

    return juce::jmap(clampedDecibelValue,
                      -60.0f,
                      0.0f,
                      static_cast<float>(localBounds.getBottom()),
                      static_cast<float>(localBounds.getY()));
}

void GateLevelDisplay::paint(juce::Graphics& graphics)
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();

    graphics.setColour(AccentGray);
    graphics.fillRect(bounds);

    float thresholdLowDecibels = rangeSliderRef.getLowerValue();
    float thresholdHighDecibels = rangeSliderRef.getUpperValue();

    float thresholdHighYPosition = decibelsToY(thresholdHighDecibels);
    float thresholdLowYPosition = decibelsToY(thresholdLowDecibels);

    juce::Rectangle<float> allowedRangeRectangle(
        bounds.getX() + 2.0f,
        thresholdHighYPosition,
        bounds.getWidth() - 4.0f,
        juce::jmax(1.0f, thresholdLowYPosition - thresholdHighYPosition));

    graphics.setColour(ThemePink.withAlpha(0.10f));
    graphics.fillRect(allowedRangeRectangle);

    graphics.setColour(ThemePink.withAlpha(0.30f));
    graphics.drawLine(bounds.getX(), thresholdLowYPosition, bounds.getRight(), thresholdLowYPosition, 1.0f);
    graphics.drawLine(bounds.getX(), thresholdHighYPosition, bounds.getRight(), thresholdHighYPosition, 1.0f);

    float levelYPosition = decibelsToY(currentLevelDecibels);

    juce::Rectangle<float> meterRectangle(
        bounds.getX() + 2.0f,
        levelYPosition,
        bounds.getWidth() - 4.0f,
        juce::jmax(0.0f, bounds.getBottom() - levelYPosition - 2.0f));

    if (meterRectangle.getHeight() > 0.0f)
    {
        juce::Colour outOfRangeMeterColour = FocusedGray.brighter(0.35f);

        graphics.setColour(outOfRangeMeterColour);
        graphics.fillRect(meterRectangle);

        juce::Rectangle<float> inRangeMeterRectangle = meterRectangle.getIntersection(allowedRangeRectangle);

        if (inRangeMeterRectangle.getHeight() > 0.0f && inRangeMeterRectangle.getWidth() > 0.0f)
        {
            graphics.setColour(ThemePink.withAlpha(0.30f));
            graphics.fillRect(inRangeMeterRectangle);
        }
    }
}

void GateLevelDisplay::resized()
{
}