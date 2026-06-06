#include "ModuleOption.h"

ModuleOption::ModuleOption()
{
    label.setJustificationType(juce::Justification::centred);
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
}

void ModuleOption::SetLabelText(const juce::String& newText)
{
    label.setText(newText, juce::dontSendNotification);
}

const juce::String ModuleOption::GetLabelText() const
{
    return label.getText();
}

void ModuleOption::SetLabelVisible(bool shouldBeVisible)
{
    label.setVisible(shouldBeVisible);
}

bool ModuleOption::IsLabelVisible() const
{
    return label.isVisible();
}

juce::Label& ModuleOption::GetLabel()
{
    return label;
}

void ModuleOption::ApplyTheme(const RackTheme& rackTheme)
{
    currentTheme = rackTheme;
    label.setColour(juce::Label::textColourId, GetOptionLabelColour(rackTheme));
    repaint();
}

void ModuleOption::ApplyEnabledState(bool shouldBeEnabled, float disabledAlpha)
{
    setAlpha(shouldBeEnabled ? 1.0f : disabledAlpha);
}

juce::Colour ModuleOption::FindThemeColour() const
{
    const juce::Component* current = getParentComponent();

    while (current != nullptr)
    {
        if (const auto* provider = dynamic_cast<const ModuleThemeProvider*>(current))
            return provider->GetThemeColour();

        current = current->getParentComponent();
    }

    return juce::Colours::orange;
}

juce::Colour ModuleOption::GetOptionLabelColour(const RackTheme& rackTheme) const
{
    return FindThemeColour().darker(rackTheme.moduleLabelDarkenAmount);
}

juce::Colour ModuleOption::GetOptionOutlineColour(const RackTheme& rackTheme) const
{
    return FindThemeColour().darker(rackTheme.moduleSecondaryDarkenAmount);
}

juce::Colour ModuleOption::GetOptionFillColour(const RackTheme& rackTheme) const
{
    return FindThemeColour().darker(rackTheme.moduleControlDarkenAmount);
}

juce::Colour ModuleOption::GetOptionAccentColour() const
{
    return FindThemeColour();
}

juce::Rectangle<int> ModuleOption::GetLabelBoundsBelow(const juce::Rectangle<int>& controlBounds,
                                                       int labelHeight,
                                                       int offset) const
{
    return juce::Rectangle<int>(
        controlBounds.getX() - 4,
        controlBounds.getBottom() + offset,
        controlBounds.getWidth() + 8,
        labelHeight);
}