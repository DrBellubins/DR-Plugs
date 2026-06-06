#include "DistortionModule.h"

DistortionModule::DistortionModule()
{
    titleLabel.setText("Distortion", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setInterceptsMouseClicks(false, false);

    typeDropdown.SetLabelText("Type");
    driveKnob.SetLabelText("Drive");
    mixKnob.SetLabelText("Mix");

    typeDropdown.GetComboBox().addItem("Heat", 1);
    typeDropdown.GetComboBox().addItem("Chebyshev", 2);
    typeDropdown.GetComboBox().addItem("Hard Clip", 3);
    typeDropdown.GetComboBox().addItem("Tube", 4);
    typeDropdown.GetComboBox().setSelectedId(1, juce::dontSendNotification);

    driveKnob.GetSlider().setRange(0.0, 1.0, 0.0);
    driveKnob.GetSlider().setValue(0.5, juce::dontSendNotification);

    mixKnob.GetSlider().setRange(0.0, 1.0, 0.0);
    mixKnob.GetSlider().setValue(1.0, juce::dontSendNotification);

    addAndMakeVisible(titleLabel);
    addAndMakeVisible(typeDropdown);
    addAndMakeVisible(driveKnob);
    addAndMakeVisible(mixKnob);
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

ModuleDropdown& DistortionModule::GetTypeDropdown()
{
    return typeDropdown;
}

ModuleKnob& DistortionModule::GetDriveKnob()
{
    return driveKnob;
}

ModuleKnob& DistortionModule::GetMixKnob()
{
    return mixKnob;
}

juce::Label& DistortionModule::GetTitleLabel()
{
    return titleLabel;
}

void DistortionModule::ApplyThemeToControls()
{
    titleLabel.setColour(juce::Label::textColourId, GetThemeColour());

    typeDropdown.ApplyTheme(theme);
    driveKnob.ApplyTheme(theme);
    mixKnob.ApplyTheme(theme);
}

void DistortionModule::LayoutControls()
{
    const int contentX = theme.modulePadding;
    const int contentW = getWidth() - (theme.modulePadding * 2);

    const int titleY = theme.modulePadding + 2;
    titleLabel.setBounds(contentX + 28, titleY, juce::jmax(40, contentW - 28), 18);

    const int dropdownY =
        theme.modulePadding
        + theme.enableButtonHeight
        + 16
        + theme.oscilloscopeHeight
        + 8;

    const int dropdownHeight = 24;
    const int dropdownTotalHeight = dropdownHeight + theme.labelOffsetBelow + 16;

    typeDropdown.setBounds(contentX,
                           dropdownY,
                           contentW,
                           dropdownTotalHeight);

    const int knobY = dropdownY + dropdownTotalHeight + 10;
    const int knobSize = theme.optionSize + 16;
    const int knobTotalHeight = knobSize + theme.labelOffsetBelow + 16;
    const int gap = theme.optionSpacing;

    const int totalKnobWidth = (knobSize * 2) + gap;
    const int knobStartX = contentX + juce::jmax(0, (contentW - totalKnobWidth) / 2);

    driveKnob.setBounds(knobStartX,
                        knobY,
                        knobSize,
                        knobTotalHeight);

    mixKnob.setBounds(knobStartX + knobSize + gap,
                      knobY,
                      knobSize,
                      knobTotalHeight);
}