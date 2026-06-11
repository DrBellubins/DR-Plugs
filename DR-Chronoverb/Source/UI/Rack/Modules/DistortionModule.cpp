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

void DistortionModule::CreateLayout(const RackTheme& newTheme,
                                    juce::AudioProcessorValueTreeState& apvts,
                                    const DistortionModuleParameterIDs& parameterIDs)
{
    theme = newTheme;

    AttachEnableButton(apvts, parameterIDs.enabled);
    typeDropdown.AttachToParameter(apvts, parameterIDs.type);
    driveKnob.AttachToParameter(apvts, parameterIDs.drive);
    mixKnob.AttachToParameter(apvts, parameterIDs.mix);

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
    titleLabel.setColour(juce::Label::textColourId, GetModuleLabelColour());

    typeDropdown.ApplyTheme(theme);
    driveKnob.ApplyTheme(theme);
    mixKnob.ApplyTheme(theme);
}

void DistortionModule::LayoutControls()
{
    const int contentX = theme.modulePadding;
    const int contentY = theme.modulePadding;
    const int contentW = getWidth() - (theme.modulePadding * 2);

    // ----- Header -----
    const int headerY = contentY + theme.titleTopOffset;
    const int titleX = contentX + theme.enableButtonWidth + 8;
    const int titleW = juce::jmax(30, contentW - (theme.enableButtonWidth + 8));

    titleLabel.setBounds(titleX,
                         headerY,
                         titleW,
                         theme.headerHeight);

    // ----- Left scope column -----
    const auto scopeBounds = GetOscilloscopePlaceholder().getBounds();

    const int rightColumnX = scopeBounds.getRight() + theme.moduleInnerGap;
    const int rightColumnW = getWidth() - theme.modulePadding - rightColumnX;

    if (rightColumnW <= 30)
        return;

    // ----- Dropdown at top-right -----
    const int dropdownY = scopeBounds.getY();
    const int dropdownLabelHeight = 14;
    const int dropdownTotalHeight =
        theme.dropdownHeight + theme.labelOffsetBelow + dropdownLabelHeight;

    typeDropdown.setBounds(rightColumnX,
                           dropdownY,
                           rightColumnW,
                           dropdownTotalHeight);

    // ----- Knobs below dropdown -----
    const int knobY = dropdownY + dropdownTotalHeight + 6;
    const int knobSize = theme.optionSize;
    const int knobLabelHeight = 14;
    const int knobTotalHeight =
        knobSize + theme.labelOffsetBelow + knobLabelHeight;

    const int totalKnobWidth = (knobSize * 2) + theme.optionSpacing;
    const int knobStartX = rightColumnX + juce::jmax(0, (rightColumnW - totalKnobWidth) / 2);

    driveKnob.setBounds(knobStartX,
                        knobY,
                        knobSize,
                        knobTotalHeight);

    mixKnob.setBounds(knobStartX + knobSize + theme.optionSpacing,
                      knobY,
                      knobSize,
                      knobTotalHeight);
}