#include "DistortionModule.h"

DistortionModule::DistortionModule()
{
    titleLabel.setText("Distortion", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);

    typeLabel.setText("Type", juce::dontSendNotification);
    typeLabel.setJustificationType(juce::Justification::centred);

    driveLabel.setText("Drive", juce::dontSendNotification);
    driveLabel.setJustificationType(juce::Justification::centred);

    mixLabel.setText("Mix", juce::dontSendNotification);
    mixLabel.setJustificationType(juce::Justification::centred);

    typeDropdown.addItem("Heat", 1);
    typeDropdown.addItem("Chebyshev", 2);
    typeDropdown.addItem("Hard Clip", 3);
    typeDropdown.addItem("Tube", 4);
    typeDropdown.setSelectedId(1, juce::dontSendNotification);

    driveKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    driveKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    driveKnob.setRange(0.0, 1.0, 0.0);
    driveKnob.setValue(0.5);

    mixKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mixKnob.setRange(0.0, 1.0, 0.0);
    mixKnob.setValue(1.0);

    addAndMakeVisible(titleLabel);
    addAndMakeVisible(typeDropdown);
    addAndMakeVisible(typeLabel);
    addAndMakeVisible(driveKnob);
    addAndMakeVisible(driveLabel);
    addAndMakeVisible(mixKnob);
    addAndMakeVisible(mixLabel);
}

void DistortionModule::CreateLayout(const RackTheme& newTheme)
{
    theme = newTheme;

    SetThemeColour(juce::Colour::fromRGB(230, 120, 70));
    ApplyThemeToBaseChrome();
    ApplyThemeToControls();
    LayoutBaseChrome();
    LayoutControls();
}

void DistortionModule::resized()
{
    Module::resized();
    LayoutControls();
}

juce::ComboBox& DistortionModule::GetTypeDropdown()
{
    return typeDropdown;
}

juce::Slider& DistortionModule::GetDriveKnob()
{
    return driveKnob;
}

juce::Slider& DistortionModule::GetMixKnob()
{
    return mixKnob;
}

juce::Label& DistortionModule::GetTitleLabel()
{
    return titleLabel;
}

juce::Label& DistortionModule::GetTypeLabel()
{
    return typeLabel;
}

juce::Label& DistortionModule::GetDriveLabel()
{
    return driveLabel;
}

juce::Label& DistortionModule::GetMixLabel()
{
    return mixLabel;
}

void DistortionModule::ApplyThemeToControls()
{
    const auto labelColour = GetModuleLabelColour();
    const auto secondary = GetModuleSecondaryColour();
    const auto controlFill = GetModuleControlFillColour();

    titleLabel.setColour(juce::Label::textColourId, themeColour);
    typeLabel.setColour(juce::Label::textColourId, labelColour);
    driveLabel.setColour(juce::Label::textColourId, labelColour);
    mixLabel.setColour(juce::Label::textColourId, labelColour);

    typeDropdown.setColour(juce::ComboBox::backgroundColourId, controlFill);
    typeDropdown.setColour(juce::ComboBox::outlineColourId, secondary);
    typeDropdown.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    typeDropdown.setColour(juce::ComboBox::arrowColourId, themeColour);

    driveKnob.setColour(juce::Slider::rotarySliderFillColourId, themeColour);
    driveKnob.setColour(juce::Slider::rotarySliderOutlineColourId, controlFill);
    driveKnob.setColour(juce::Slider::thumbColourId, juce::Colours::white);

    mixKnob.setColour(juce::Slider::rotarySliderFillColourId, themeColour);
    mixKnob.setColour(juce::Slider::rotarySliderOutlineColourId, controlFill);
    mixKnob.setColour(juce::Slider::thumbColourId, juce::Colours::white);
}

void DistortionModule::LayoutControls()
{
    const int contentX = theme.modulePadding;
    const int contentW = getWidth() - (theme.modulePadding * 2);

    const int titleY = theme.modulePadding + 2;
    titleLabel.setBounds(contentX + 28, titleY, juce::jmax(40, contentW - 28), 18);

    const int dropdownY = theme.modulePadding + theme.enableButtonHeight + 16 + theme.oscilloscopeHeight + 8;
    typeDropdown.setBounds(contentX, dropdownY, contentW, 24);
    typeLabel.setBounds(contentX, dropdownY + 24 + theme.labelOffsetBelow, contentW, 16);

    const int knobY = dropdownY + 24 + theme.labelOffsetBelow + 16 + 10;
    const int knobSize = theme.optionSize + 16;
    const int gap = theme.optionSpacing;
    const int totalKnobWidth = (knobSize * 2) + gap;
    const int knobStartX = contentX + juce::jmax(0, (contentW - totalKnobWidth) / 2);

    driveKnob.setBounds(knobStartX, knobY, knobSize, knobSize);
    mixKnob.setBounds(knobStartX + knobSize + gap, knobY, knobSize, knobSize);

    driveLabel.setBounds(driveKnob.getX() - 4,
                         driveKnob.getBottom() + theme.labelOffsetBelow,
                         knobSize + 8,
                         16);

    mixLabel.setBounds(mixKnob.getX() - 4,
                       mixKnob.getBottom() + theme.labelOffsetBelow,
                       knobSize + 8,
                       16);
}