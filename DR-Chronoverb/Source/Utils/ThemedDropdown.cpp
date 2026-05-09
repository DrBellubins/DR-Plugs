#include "ThemedDropdown.h"

ThemedDropdown::ThemedDropdown()
{
    setEditableText(false);
    setJustificationType(textJustification);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

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
    juce::Rectangle<int> localBounds = getLocalBounds();
    juce::Rectangle<int> textBounds = localBounds.reduced(10, 0);
    juce::Rectangle<int> arrowAreaBounds = localBounds.removeFromRight(24);
    textBounds.removeFromRight(24);

    const juce::Colour adjustedAccentGray = ThemeContext::GetAdjustedColour(AccentGray, *this);
    const juce::Colour adjustedUnfocusedGray = ThemeContext::GetAdjustedColour(UnfocusedGray, *this);
    const juce::Colour adjustedFocusedGray = ThemeContext::GetAdjustedColour(FocusedGray, *this);

    GraphicsContext.setColour(adjustedAccentGray);
    GraphicsContext.fillRoundedRectangle(getLocalBounds().toFloat(), cornerRadius);

    GraphicsContext.setColour(
        hasKeyboardFocus(true)
            ? adjustedFocusedGray
            : adjustedUnfocusedGray.brighter(0.1f));

    GraphicsContext.drawRoundedRectangle(
        getLocalBounds().toFloat().reduced(outlineThickness * 0.5f),
        cornerRadius,
        outlineThickness
    );

    const juce::String displayedText = getText().isEmpty() ? "Select..." : getText();

    GraphicsContext.setColour(juce::Colours::white);
    GraphicsContext.setFont(juce::Font("Liberation Sans", 14.0f, juce::Font::bold));
    GraphicsContext.drawFittedText(
        displayedText,
        textBounds,
        textJustification,
        1
    );

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

void ThemedDropdown::resized()
{
    juce::ComboBox::resized();
}

void ThemedDropdown::mouseDown(const juce::MouseEvent& MouseEvent)
{
    juce::ignoreUnused(MouseEvent);

    if (!isEnabled())
    {
        return;
    }

    showPopup();
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