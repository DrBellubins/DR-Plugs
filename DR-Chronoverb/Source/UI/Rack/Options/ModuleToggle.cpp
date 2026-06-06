#include "ModuleToggle.h"

ModuleToggle::ModuleToggle()
{
    button.setButtonText({});
    addAndMakeVisible(button);
    SetLabelVisible(true);
}

void ModuleToggle::AttachToParameter(juce::AudioProcessorValueTreeState& apvts,
                                     const juce::String& parameterID)
{
    attachment = std::make_unique<Attachment>(apvts, parameterID, button);
}

juce::ToggleButton& ModuleToggle::GetButton()
{
    return button;
}

void ModuleToggle::SetControlBounds(const juce::Rectangle<int>& newBounds)
{
    controlBoundsOverride = newBounds;
    resized();
}

juce::Rectangle<int> ModuleToggle::GetControlBounds() const
{
    return button.getBounds();
}

void ModuleToggle::SetLabelHeight(int newLabelHeight)
{
    labelHeight = juce::jmax(10, newLabelHeight);
    resized();
}

int ModuleToggle::GetLabelHeight() const
{
    return labelHeight;
}

void ModuleToggle::ApplyTheme(const RackTheme& rackTheme)
{
    ModuleOption::ApplyTheme(rackTheme);

    button.setColour(juce::ToggleButton::tickColourId, juce::Colours::transparentBlack);
    button.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colours::transparentBlack);
    button.setColour(juce::ToggleButton::textColourId, juce::Colours::transparentBlack);
}

void ModuleToggle::resized()
{
    const auto area = getLocalBounds();

    juce::Rectangle<int> controlBounds;

    if (!controlBoundsOverride.isEmpty())
    {
        controlBounds = controlBoundsOverride;
    }
    else
    {
        const int labelSpace = IsLabelVisible() ? (labelHeight + currentTheme.labelOffsetBelow) : 0;
        controlBounds = area.withTrimmedBottom(labelSpace);

        const int side = juce::jmin(controlBounds.getWidth(), controlBounds.getHeight());
        controlBounds = juce::Rectangle<int>(side, side).withCentre(controlBounds.getCentre());
    }

    button.setBounds(controlBounds);

    if (IsLabelVisible())
        label.setBounds(GetLabelBoundsBelow(controlBounds, labelHeight, currentTheme.labelOffsetBelow));
}

void ModuleToggle::paint(juce::Graphics& g)
{
    ModuleOption::paint(g);

    const auto bounds = button.getBounds().toFloat();
    const bool isOn = button.getToggleState();

    const auto fill = isOn
        ? GetOptionAccentColour()
        : GetOptionFillColour(currentTheme);

    const auto outline = GetOptionOutlineColour(currentTheme);
    const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());

    const auto circle = juce::Rectangle<float>(diameter, diameter).withCentre(bounds.getCentre());

    g.setColour(fill);
    g.fillEllipse(circle);

    g.setColour(outline);
    g.drawEllipse(circle.reduced(0.75f), 1.5f);
}