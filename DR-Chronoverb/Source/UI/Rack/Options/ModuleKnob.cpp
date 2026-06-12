#include "ModuleKnob.h"

ModuleKnob::ModuleKnob()
    : slider(juce::Slider::RotaryHorizontalVerticalDrag,
             juce::Slider::NoTextBox)
{
    slider.setLookAndFeel(&knobLookAndFeel);
    addAndMakeVisible(slider);
    SetLabelVisible(true);
}

ModuleKnob::~ModuleKnob()
{
    slider.setLookAndFeel(nullptr);
}

void ModuleKnob::AttachToParameter(juce::AudioProcessorValueTreeState& apvts,
                                   const juce::String& parameterID)
{
    attachment = std::make_unique<Attachment>(apvts, parameterID, slider);
}

juce::Slider& ModuleKnob::GetSlider()
{
    return slider;
}

void ModuleKnob::SetSliderBounds(const juce::Rectangle<int>& newBounds)
{
    sliderBoundsOverride = newBounds;
    resized();
}

juce::Rectangle<int> ModuleKnob::GetSliderBounds() const
{
    return slider.getBounds();
}

void ModuleKnob::SetLabelHeight(int newLabelHeight)
{
    labelHeight = juce::jmax(10, newLabelHeight);
    resized();
}

int ModuleKnob::GetLabelHeight() const
{
    return labelHeight;
}

void ModuleKnob::ApplyTheme(const RackTheme& rackTheme)
{
    ModuleOption::ApplyTheme(rackTheme);

    slider.setColour(juce::Slider::rotarySliderFillColourId, GetOptionAccentColour());
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, GetOptionFillColour(rackTheme));
    //slider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
}

void ModuleKnob::resized()
{
    const auto area = getLocalBounds();

    juce::Rectangle<int> controlBounds;

    if (!sliderBoundsOverride.isEmpty())
        controlBounds = sliderBoundsOverride;
    else
    {
        const int labelSpace = IsLabelVisible() ? (labelHeight + currentTheme.labelOffsetBelow) : 0;
        controlBounds = area.withTrimmedBottom(labelSpace);
    }

    slider.setBounds(controlBounds);

    if (IsLabelVisible())
        label.setBounds(GetLabelBoundsBelow(controlBounds, labelHeight, currentTheme.labelOffsetBelow));
}