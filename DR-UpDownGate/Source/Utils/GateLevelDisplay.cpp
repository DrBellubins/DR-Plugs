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
    graphics.fillRoundedRectangle(bounds, 6.0f);

    float thresholdLowDecibels = rangeSliderRef.getLowerValue();
    float thresholdHighDecibels = rangeSliderRef.getUpperValue();

    float thresholdHighYPosition = decibelsToY(thresholdHighDecibels);
    float thresholdLowYPosition = decibelsToY(thresholdLowDecibels);

    juce::Rectangle<float> allowedRangeRectangle(
        bounds.getX(),
        thresholdHighYPosition,
        bounds.getWidth(),
        juce::jmax(1.0f, thresholdLowYPosition - thresholdHighYPosition));

    graphics.setColour(ThemePink.withAlpha(0.10f));
    graphics.fillRoundedRectangle(allowedRangeRectangle, 4.0f);

    graphics.setColour(ThemePink.withAlpha(0.35f));
    graphics.drawLine(bounds.getX(), thresholdLowYPosition, bounds.getRight(), thresholdLowYPosition, 1.0f);
    graphics.drawLine(bounds.getX(), thresholdHighYPosition, bounds.getRight(), thresholdHighYPosition, 1.0f);

    float levelYPosition = decibelsToY(currentLevelDecibels);

    juce::Rectangle<float> meterRectangle(
        bounds.getX() + 2.0f,
        levelYPosition,
        bounds.getWidth() - 4.0f,
        bounds.getBottom() - levelYPosition - 2.0f);

    bool isWithinAllowedRange =
        currentLevelDecibels >= thresholdLowDecibels
        && currentLevelDecibels <= thresholdHighDecibels;

    juce::Colour meterColour = isWithinAllowedRange
                               ? juce::Colours::white
                               : juce::Colours::white.withAlpha(0.45f);

    graphics.setColour(meterColour);

    if (meterRectangle.getHeight() > 0.0f)
    {
        graphics.fillRoundedRectangle(meterRectangle, 4.0f);
    }

    graphics.setColour(FocusedGray);
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

void GateLevelDisplay::resized()
{
}