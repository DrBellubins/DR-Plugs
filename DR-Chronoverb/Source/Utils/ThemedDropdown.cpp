#include "ThemedDropdown.h"

ThemedDropdown::ThemedDropdown()
{
    setEditableText(false);
    setJustificationType(textJustification);
    setColour(juce::ComboBox::backgroundColourId, AccentGray);
    setColour(juce::ComboBox::outlineColourId, UnfocusedGray.brighter(0.1f));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::arrowColourId, ThemePink);
}

ThemedDropdown::~ThemedDropdown()
{
}

void ThemedDropdown::paint(juce::Graphics& GraphicsContext)
{
    const juce::Rectangle<int> localBounds = getLocalBounds();

    const juce::Colour adjustedAccentGray = ThemeContext::GetAdjustedColour(AccentGray, *this);
    const juce::Colour adjustedUnfocusedGray = ThemeContext::GetAdjustedColour(UnfocusedGray, *this);
    const juce::Colour adjustedFocusedGray = ThemeContext::GetAdjustedColour(FocusedGray, *this);

    GraphicsContext.setColour(adjustedAccentGray);
    GraphicsContext.fillRoundedRectangle(localBounds.toFloat(), cornerRadius);

    GraphicsContext.setColour(hasKeyboardFocus(true) ? adjustedFocusedGray : adjustedUnfocusedGray.brighter(0.1f));
    GraphicsContext.drawRoundedRectangle(
        localBounds.toFloat().reduced(outlineThickness * 0.5f),
        cornerRadius,
        outlineThickness
    );

    juce::Rectangle<int> textBounds = localBounds.reduced(10, 0);
    textBounds.removeFromRight(24);

    GraphicsContext.setColour(juce::Colours::white);
    GraphicsContext.setFont(juce::Font("Liberation Sans", 14.0f, juce::Font::bold));
    GraphicsContext.drawFittedText(
        getText(),
        textBounds,
        textJustification,
        1
    );

    const int arrowAreaWidth = 24;
    const juce::Rectangle<float> arrowBounds = localBounds.removeFromRight(arrowAreaWidth).toFloat();

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

    if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter))
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

void ThemedDropdown::Attachment::parameterChanged(const juce::String& changedParameterID, float newValue)
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
    if (parameter == nullptr)
    {
        return;
    }

    const float rawValue = parameter->convertFrom0to1(parameter->getValue());
    const int selectedIndex = static_cast<int>(std::round(rawValue));

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

    if (parameter == nullptr)
    {
        return;
    }

    const int selectedIndex = dropdown.getSelectedItemIndex();

    if (selectedIndex < 0)
    {
        return;
    }

    parameter->beginChangeGesture();

    const float normalizedValue = parameter->convertTo0to1(static_cast<float>(selectedIndex));
    parameter->setValueNotifyingHost(normalizedValue);

    parameter->endChangeGesture();
}