#include "ThemedDropdown.h"

ThemedDropdown::DropdownLookAndFeel::DropdownLookAndFeel(ThemedDropdown& ownerDropdown)
    : owner(ownerDropdown)
{
}

void ThemedDropdown::DropdownLookAndFeel::drawComboBox(
    juce::Graphics& GraphicsContext,
    int width,
    int height,
    bool isButtonDown,
    int buttonX,
    int buttonY,
    int buttonW,
    int buttonH,
    juce::ComboBox& comboBox)
{
    juce::ignoreUnused(isButtonDown, buttonX, buttonY, buttonW, buttonH);

    juce::Rectangle<int> localBounds(0, 0, width, height);

    const juce::Colour adjustedAccentGray =
        ThemeContext::GetAdjustedColour(AccentGray, comboBox);

    const juce::Colour adjustedUnfocusedGray =
        ThemeContext::GetAdjustedColour(UnfocusedGray, comboBox);

    const juce::Colour adjustedFocusedGray =
        ThemeContext::GetAdjustedColour(FocusedGray, comboBox);

    GraphicsContext.setColour(adjustedAccentGray);
    GraphicsContext.fillRoundedRectangle(localBounds.toFloat(), owner.cornerRadius);

    GraphicsContext.setColour(
        comboBox.hasKeyboardFocus(true)
            ? adjustedFocusedGray
            : adjustedUnfocusedGray.brighter(0.1f));

    GraphicsContext.drawRoundedRectangle(
        localBounds.toFloat().reduced(owner.outlineThickness * 0.5f),
        owner.cornerRadius,
        owner.outlineThickness
    );

    juce::Rectangle<int> arrowAreaBounds = localBounds.removeFromRight(24);
    const juce::Rectangle<float> arrowBounds = arrowAreaBounds.toFloat();

    juce::Path arrowPath;
    const float centreX = arrowBounds.getCentreX();
    const float centreY = arrowBounds.getCentreY();
    const float arrowHalfWidth = 5.0f;
    const float arrowHeight = 3.5f;

    arrowPath.startNewSubPath(centreX - arrowHalfWidth, centreY - arrowHeight);
    arrowPath.lineTo(centreX, centreY + arrowHeight);
    arrowPath.lineTo(centreX + arrowHalfWidth, centreY - arrowHeight);

    GraphicsContext.setColour(ThemePink);
    GraphicsContext.strokePath(arrowPath, juce::PathStrokeType(2.0f));
}

juce::Font ThemedDropdown::DropdownLookAndFeel::getComboBoxFont(juce::ComboBox& comboBox)
{
    juce::ignoreUnused(comboBox);
    return juce::Font("Liberation Sans", 14.0f, juce::Font::bold);
}

juce::Label* ThemedDropdown::DropdownLookAndFeel::createComboBoxTextBox(juce::ComboBox& comboBox)
{
    juce::Label* comboLabel = juce::LookAndFeel_V4::createComboBoxTextBox(comboBox);

    comboLabel->setFont(getComboBoxFont(comboBox));
    comboLabel->setJustificationType(owner.textJustification);
    comboLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    comboLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    comboLabel->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    comboLabel->setInterceptsMouseClicks(false, false);

    return comboLabel;
}

void ThemedDropdown::DropdownLookAndFeel::positionComboBoxText(
    juce::ComboBox& comboBox,
    juce::Label& label)
{
    juce::Rectangle<int> textBounds = comboBox.getLocalBounds().reduced(10, 0);
    textBounds.removeFromRight(24);

    label.setBounds(textBounds);
    label.setFont(getComboBoxFont(comboBox));
    label.setJustificationType(owner.textJustification);
}

ThemedDropdown::ThemedDropdown()
    : dropdownLookAndFeel(*this)
{
    setEditableText(false);
    setJustificationType(textJustification);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setLookAndFeel(&dropdownLookAndFeel);

    UpdateColours();
}

ThemedDropdown::~ThemedDropdown()
{
    setLookAndFeel(nullptr);
}

void ThemedDropdown::resized()
{
    juce::ComboBox::resized();
}

void ThemedDropdown::SetJustification(juce::Justification justificationType)
{
    textJustification = justificationType;
    setJustificationType(justificationType);
    repaint();
}

void ThemedDropdown::SetCornerRadius(float newCornerRadius)
{
    cornerRadius = juce::jmax(0.0f, newCornerRadius);
    repaint();
}

void ThemedDropdown::SetOutlineThickness(float newOutlineThickness)
{
    outlineThickness = juce::jmax(0.0f, newOutlineThickness);
    repaint();
}

void ThemedDropdown::UpdateColours()
{
    setColour(juce::ComboBox::backgroundColourId, AccentGray);
    setColour(juce::ComboBox::outlineColourId, UnfocusedGray.brighter(0.1f));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::arrowColourId, ThemePink);

    setColour(juce::PopupMenu::backgroundColourId, AccentGray);
    setColour(juce::PopupMenu::textColourId, juce::Colours::white);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ThemePink);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

ThemedDropdown::Attachment::Attachment(
    juce::AudioProcessorValueTreeState& state,
    const juce::String& parameterID,
    ThemedDropdown& dropdownToControl
)
    : apvts(state),
      attachedParameterID(parameterID),
      dropdown(dropdownToControl)
{
    parameter = apvts.getParameter(attachedParameterID);

    jassert(parameter != nullptr && "ThemedDropdown::Attachment: Parameter ID not found.");

    choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);

    jassert(choiceParameter != nullptr
        && "ThemedDropdown::Attachment: Parameter must be an AudioParameterChoice.");

    if (choiceParameter != nullptr)
    {
        dropdown.clear(juce::dontSendNotification);

        for (int choiceIndex = 0; choiceIndex < choiceParameter->choices.size(); ++choiceIndex)
        {
            dropdown.addItem(choiceParameter->choices[choiceIndex], choiceIndex + 1);
        }
    }

    dropdown.onChange = [this]()
    {
        SyncDropdownToParameter();
    };

    apvts.addParameterListener(attachedParameterID, this);
    SyncParameterToDropdown();
}

ThemedDropdown::Attachment::~Attachment()
{
    apvts.removeParameterListener(attachedParameterID, this);
    dropdown.onChange = nullptr;
}

void ThemedDropdown::Attachment::parameterChanged(
    const juce::String& changedParameterID,
    float newValue)
{
    juce::ignoreUnused(newValue);

    if (changedParameterID != attachedParameterID)
    {
        return;
    }

    juce::MessageManager::callAsync([this]()
    {
        SyncParameterToDropdown();
    });
}

void ThemedDropdown::Attachment::SyncParameterToDropdown()
{
    if (choiceParameter == nullptr)
    {
        return;
    }

    const int selectedIndex = choiceParameter->getIndex();

    ignoreCallbacks.store(true);
    dropdown.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
    ignoreCallbacks.store(false);
}

void ThemedDropdown::Attachment::SyncDropdownToParameter()
{
    if (ignoreCallbacks.load())
    {
        return;
    }

    if (parameter == nullptr || choiceParameter == nullptr)
    {
        return;
    }

    const int selectedIndex = dropdown.getSelectedItemIndex();

    if (selectedIndex < 0)
    {
        return;
    }

    const int numberOfChoices = choiceParameter->choices.size();

    if (numberOfChoices <= 1)
    {
        return;
    }

    const float normalizedValue =
        static_cast<float>(selectedIndex)
        / static_cast<float>(numberOfChoices - 1);

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalizedValue);
    parameter->endChangeGesture();
}