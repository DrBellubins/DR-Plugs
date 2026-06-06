#include "ModuleDropdown.h"

ModuleDropdown::ModuleDropdown()
{
    addAndMakeVisible(comboBox);
    SetLabelVisible(true);
}

void ModuleDropdown::AttachToParameter(juce::AudioProcessorValueTreeState& apvts,
                                       const juce::String& parameterID)
{
    attachment = std::make_unique<Attachment>(apvts, parameterID, comboBox);
}

juce::ComboBox& ModuleDropdown::GetComboBox()
{
    return comboBox;
}

void ModuleDropdown::SetControlBounds(const juce::Rectangle<int>& newBounds)
{
    controlBoundsOverride = newBounds;
    resized();
}

juce::Rectangle<int> ModuleDropdown::GetControlBounds() const
{
    return comboBox.getBounds();
}

void ModuleDropdown::SetLabelHeight(int newLabelHeight)
{
    labelHeight = juce::jmax(10, newLabelHeight);
    resized();
}

int ModuleDropdown::GetLabelHeight() const
{
    return labelHeight;
}

void ModuleDropdown::ApplyTheme(const RackTheme& rackTheme)
{
    ModuleOption::ApplyTheme(rackTheme);

    comboBox.setColour(juce::ComboBox::backgroundColourId, GetOptionFillColour(rackTheme));
    comboBox.setColour(juce::ComboBox::outlineColourId, GetOptionOutlineColour(rackTheme));
    comboBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    comboBox.setColour(juce::ComboBox::arrowColourId, GetOptionAccentColour());
}

void ModuleDropdown::resized()
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
    }

    comboBox.setBounds(controlBounds);

    if (IsLabelVisible())
        label.setBounds(GetLabelBoundsBelow(controlBounds, labelHeight, currentTheme.labelOffsetBelow));
}