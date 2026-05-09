#include "UIHelpers.h"

UIHelpers::UIHelpers(
    juce::AudioProcessorValueTreeState& newValueTreeState,
    FlatRotaryLookAndFeel& newRotaryLookAndFeel)
    : valueTreeState(newValueTreeState),
      rotaryLookAndFeel(newRotaryLookAndFeel)
{
}

void UIHelpers::CreateToggle(
    juce::Component& parentComponent,
    std::unique_ptr<RoundedToggle>& toggle,
    std::unique_ptr<RoundedToggle::Attachment>& attachment,
    RoundedToggle::Orientation orientation,
    const juce::String& parameterID,
    int width,
    int height,
    int x,
    int y)
{
    if (toggle == nullptr)
    {
        toggle = std::make_unique<RoundedToggle>();
    }

    if (attachment == nullptr)
    {
        attachment = std::make_unique<RoundedToggle::Attachment>(valueTreeState, parameterID, *toggle);
    }

    parentComponent.addAndMakeVisible(*toggle);
    toggle->setOrientation(orientation);

    int toggleX = x - (width / 2);
    int toggleY = y - (height / 2);

    toggle->setBounds(toggleX, toggleY, width, height);
}

void UIHelpers::CreateCheckbox(
    juce::Component& parentComponent,
    std::unique_ptr<ThemedCheckbox>& checkbox,
    std::unique_ptr<ThemedCheckbox::Attachment>& attachment,
    const juce::String& parameterID,
    int width,
    int height,
    int x,
    int y)
{
    if (checkbox == nullptr)
    {
        checkbox = std::make_unique<ThemedCheckbox>();
    }

    if (attachment == nullptr)
    {
        attachment = std::make_unique<ThemedCheckbox::Attachment>(valueTreeState, parameterID, *checkbox);
    }

    parentComponent.addAndMakeVisible(*checkbox);

    int checkboxX = x - (width / 2);
    int checkboxY = y - (height / 2);

    checkbox->setBounds(checkboxX, checkboxY, width, height);
}

void UIHelpers::CreateCheckboxLabel(
    juce::Component& parentComponent,
    std::unique_ptr<juce::Label>& label,
    ThemedCheckbox& toggle,
    const juce::String& text,
    float fontSize,
    int offsetX)
{
    label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);

    juce::Font mainFont("Liberation Sans", fontSize, juce::Font::bold);
    mainFont.setExtraKerningFactor(0.05f);

    label->setFont(mainFont);
    label->setJustificationType(juce::Justification::centredLeft);

    parentComponent.addAndMakeVisible(*label);

    int labelWidth = GetLabelWidth(label);

    juce::Rectangle<int> sliderBounds = toggle.getBounds();
    int labelX = sliderBounds.getCentreX() - (labelWidth / 2) - offsetX;
    int labelY = sliderBounds.getCentreY() - (sliderBounds.getHeight() / 2);

    label->setBounds(labelX, labelY, labelWidth, 20);
}

void UIHelpers::CreateSlider(
    juce::Component& parentComponent,
    std::unique_ptr<ThemedSlider>& slider,
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
    const juce::String& parameterID,
    int width,
    int height,
    int x,
    int y)
{
    slider = std::make_unique<ThemedSlider>(
        "",
        nullptr,
        nullptr,
        "",
        juce::Slider::NoTextBox);

    slider->setLookAndFeel(&rotaryLookAndFeel);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        valueTreeState,
        parameterID,
        *slider);

    parentComponent.addAndMakeVisible(*slider);

    int sliderX = x - (width / 2);
    int sliderY = y - (height / 2);

    slider->setBounds(sliderX, sliderY, width, height);
}

void UIHelpers::CreateSliderLabel(
    juce::Component& parentComponent,
    std::unique_ptr<juce::Label>& label,
    ThemedSlider& slider,
    const juce::String& text,
    float fontSize,
    int offsetX)
{
    label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);

    juce::Font mainFont("Liberation Sans", fontSize, juce::Font::bold);
    mainFont.setExtraKerningFactor(0.05f);

    label->setFont(mainFont);
    label->setJustificationType(juce::Justification::centred);

    parentComponent.addAndMakeVisible(*label);

    int labelWidth = GetLabelWidth(label);

    juce::Rectangle<int> sliderBounds = slider.getBounds();
    int labelX = sliderBounds.getCentreX() - (labelWidth / 2) - offsetX;
    int labelY = sliderBounds.getCentreY() - (sliderBounds.getHeight() / 2);

    label->setBounds(labelX, labelY, labelWidth, 20);
}

void UIHelpers::CreateLabel(
    juce::Component& parentComponent,
    std::unique_ptr<juce::Label>& label,
    const juce::String& text,
    float fontSize,
    int x,
    int y)
{
    label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);

    juce::Font mainFont("Liberation Sans", fontSize, juce::Font::bold);
    mainFont.setExtraKerningFactor(0.05f);

    label->setFont(mainFont);
    label->setJustificationType(juce::Justification::centred);

    parentComponent.addAndMakeVisible(*label);

    int labelWidth = GetLabelWidth(label);
    int fontHeight = static_cast<int>(fontSize);

    int labelX = x - (labelWidth / 2);
    int labelY = y - (fontHeight / 2);

    label->setBounds(labelX, labelY, labelWidth, 20);
}

void UIHelpers::CreateKnob(
    juce::Component& parentComponent,
    std::unique_ptr<ThemedKnob>& knob,
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
    const juce::String& parameterID,
    const juce::String& suffix,
    int widthHeight,
    int x,
    int y)
{
    knob = std::make_unique<ThemedKnob>(
        "",
        nullptr,
        nullptr,
        suffix,
        juce::Slider::NoTextBox);

    knob->setLookAndFeel(&rotaryLookAndFeel);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        valueTreeState,
        parameterID,
        *knob);

    parentComponent.addAndMakeVisible(*knob);

    int knobX = x - (widthHeight / 2);
    int knobY = y - (widthHeight / 2);

    knob->setBounds(knobX, knobY, widthHeight, widthHeight);
    knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 22);
}

void UIHelpers::CreateKnobLabel(
    juce::Component& parentComponent,
    std::unique_ptr<juce::Label>& label,
    ThemedKnob& knob,
    const juce::String& text,
    float fontSize,
    int offsetY)
{
    label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);

    juce::Font mainFont("Liberation Sans", fontSize, juce::Font::bold);
    mainFont.setExtraKerningFactor(0.05f);

    label->setFont(mainFont);
    label->setJustificationType(juce::Justification::centred);

    parentComponent.addAndMakeVisible(*label);

    CenterKnobLabel(label, knob, offsetY);
}

void UIHelpers::CenterKnobLabel(
    std::unique_ptr<juce::Label>& label,
    ThemedKnob& knob,
    int offsetY)
{
    int labelWidth = GetLabelWidth(label);
    int labelHeight = static_cast<int>(label->getFont().getHeight());

    juce::Rectangle<int> knobBounds = knob.getBounds();
    int labelX = knobBounds.getCentreX() - (labelWidth / 2);
    int labelY = (knobBounds.getCentreY() - (labelHeight / 2)) - offsetY;

    label->setBounds(labelX, labelY, labelWidth, labelHeight);
}

int UIHelpers::GetLabelWidth(const std::unique_ptr<juce::Label>& label) const
{
    return label->getFont().getStringWidth(label->getText());
}