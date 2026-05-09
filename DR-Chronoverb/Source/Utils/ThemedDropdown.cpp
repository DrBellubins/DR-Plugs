#include "ThemedDropdown.h"

ThemedDropdown::PopupList::PopupList(ThemedDropdown& ownerDropdown)
    : owner(ownerDropdown)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setInterceptsMouseClicks(true, true);
    setVisible(false);
}

void ThemedDropdown::PopupList::paint(juce::Graphics& GraphicsContext)
{
    const juce::Rectangle<int> localBounds = getLocalBounds();

    const juce::Colour adjustedAccentGray =
        ThemeContext::GetAdjustedColour(AccentGray, owner);

    const juce::Colour adjustedUnfocusedGray =
        ThemeContext::GetAdjustedColour(UnfocusedGray, owner);

    GraphicsContext.setColour(adjustedAccentGray.darker(0.1f));
    GraphicsContext.fillRoundedRectangle(localBounds.toFloat(), owner.cornerRadius);

    GraphicsContext.setColour(adjustedUnfocusedGray.brighter(0.1f));
    GraphicsContext.drawRoundedRectangle(
        localBounds.toFloat().reduced(0.5f),
        owner.cornerRadius,
        1.0f
    );

    GraphicsContext.setFont(juce::Font("Liberation Sans", 14.0f, juce::Font::bold));

    for (int itemIndex = 0; itemIndex < owner.items.size(); ++itemIndex)
    {
        const juce::Rectangle<int> itemBounds = GetItemBounds(itemIndex);

        if (itemIndex == hoveredItemIndex)
        {
            GraphicsContext.setColour(ThemePink);
            GraphicsContext.fillRoundedRectangle(itemBounds.reduced(4, 2).toFloat(), 6.0f);
        }

        GraphicsContext.setColour(juce::Colours::white);
        GraphicsContext.drawFittedText(
            owner.items.getReference(itemIndex).text,
            itemBounds.reduced(12, 0),
            juce::Justification::centredLeft,
            1
        );
    }
}

void ThemedDropdown::PopupList::mouseMove(const juce::MouseEvent& MouseEvent)
{
    hoveredItemIndex = GetItemIndexAtPosition(MouseEvent.getPosition());
    repaint();
}

void ThemedDropdown::PopupList::mouseExit(const juce::MouseEvent& MouseEvent)
{
    juce::ignoreUnused(MouseEvent);

    hoveredItemIndex = -1;
    repaint();
}

void ThemedDropdown::PopupList::mouseDown(const juce::MouseEvent& MouseEvent)
{
    const int clickedItemIndex = GetItemIndexAtPosition(MouseEvent.getPosition());

    if (clickedItemIndex >= 0 && clickedItemIndex < owner.items.size())
    {
        owner.setSelectedItemIndex(clickedItemIndex, juce::sendNotificationAsync);
    }

    owner.SetExpanded(false);
}

void ThemedDropdown::PopupList::UpdateBounds()
{
    setBounds(
        0,
        owner.getHeight() + 4,
        owner.getWidth(),
        owner.items.size() * owner.itemHeight
    );
}

int ThemedDropdown::PopupList::GetItemIndexAtPosition(juce::Point<int> mousePosition) const
{
    for (int itemIndex = 0; itemIndex < owner.items.size(); ++itemIndex)
    {
        if (GetItemBounds(itemIndex).contains(mousePosition))
        {
            return itemIndex;
        }
    }

    return -1;
}

juce::Rectangle<int> ThemedDropdown::PopupList::GetItemBounds(int itemIndex) const
{
    return juce::Rectangle<int>(
        0,
        itemIndex * owner.itemHeight,
        getWidth(),
        owner.itemHeight
    );
}

ThemedDropdown::ThemedDropdown()
    : popupList(*this)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setInterceptsMouseClicks(true, true);

    addChildComponent(popupList);
}

ThemedDropdown::~ThemedDropdown()
{
}

void ThemedDropdown::paint(juce::Graphics& GraphicsContext)
{
    const juce::Rectangle<int> closedBounds = GetClosedBounds();

    const juce::Colour adjustedAccentGray =
        ThemeContext::GetAdjustedColour(AccentGray, *this);

    const juce::Colour adjustedUnfocusedGray =
        ThemeContext::GetAdjustedColour(UnfocusedGray, *this);

    const juce::Colour adjustedFocusedGray =
        ThemeContext::GetAdjustedColour(FocusedGray, *this);

    GraphicsContext.setColour(adjustedAccentGray);
    GraphicsContext.fillRoundedRectangle(closedBounds.toFloat(), cornerRadius);

    GraphicsContext.setColour(
        hasKeyboardFocus(true)
            ? adjustedFocusedGray
            : adjustedUnfocusedGray.brighter(0.1f));

    GraphicsContext.drawRoundedRectangle(
        closedBounds.toFloat().reduced(outlineThickness * 0.5f),
        cornerRadius,
        outlineThickness
    );

    juce::Rectangle<int> textBounds = closedBounds.reduced(10, 0);
    textBounds.removeFromRight(24);

    GraphicsContext.setColour(juce::Colours::white);
    GraphicsContext.setFont(juce::Font("Liberation Sans", 14.0f, juce::Font::bold));

    const juce::String displayedText = getText().isNotEmpty() ? getText() : "Select...";
    GraphicsContext.drawFittedText(
        displayedText,
        textBounds,
        textJustification,
        1
    );

    const juce::Rectangle<float> arrowBounds = GetArrowBounds().toFloat();

    juce::Path arrowPath;
    const float centreX = arrowBounds.getCentreX();
    const float centreY = arrowBounds.getCentreY();
    const float arrowHalfWidth = 5.0f;
    const float arrowHeight = 3.5f;

    if (!isExpanded)
    {
        arrowPath.startNewSubPath(centreX - arrowHalfWidth, centreY - arrowHeight);
        arrowPath.lineTo(centreX, centreY + arrowHeight);
        arrowPath.lineTo(centreX + arrowHalfWidth, centreY - arrowHeight);
    }
    else
    {
        arrowPath.startNewSubPath(centreX - arrowHalfWidth, centreY + arrowHeight);
        arrowPath.lineTo(centreX, centreY - arrowHeight);
        arrowPath.lineTo(centreX + arrowHalfWidth, centreY + arrowHeight);
    }

    GraphicsContext.setColour(ThemePink);
    GraphicsContext.strokePath(arrowPath, juce::PathStrokeType(2.0f));
}

void ThemedDropdown::resized()
{
    popupList.UpdateBounds();
}

void ThemedDropdown::mouseDown(const juce::MouseEvent& MouseEvent)
{
    if (GetClosedBounds().contains(MouseEvent.getPosition()))
    {
        SetExpanded(!isExpanded);
        return;
    }

    if (isExpanded)
    {
        SetExpanded(false);
    }
}

void ThemedDropdown::SetJustification(juce::Justification justificationType)
{
    textJustification = justificationType;
    repaint();
}

void ThemedDropdown::SetCornerRadius(float newCornerRadius)
{
    cornerRadius = juce::jmax(0.0f, newCornerRadius);
    repaint();
    popupList.repaint();
}

void ThemedDropdown::SetOutlineThickness(float newOutlineThickness)
{
    outlineThickness = juce::jmax(0.0f, newOutlineThickness);
    repaint();
}

void ThemedDropdown::SetItemHeight(int newItemHeight)
{
    itemHeight = juce::jmax(20, newItemHeight);
    popupList.UpdateBounds();
    popupList.repaint();
}

void ThemedDropdown::addItem(const juce::String& itemText, int itemID)
{
    Item newItem;
    newItem.text = itemText;
    newItem.itemID = itemID;

    items.add(newItem);

    if (selectedItemIndex < 0)
    {
        selectedItemIndex = 0;
    }

    popupList.UpdateBounds();
    repaint();
}

void ThemedDropdown::clear(juce::NotificationType notificationType)
{
    items.clear();
    selectedItemIndex = -1;
    isExpanded = false;

    popupList.setVisible(false);
    popupList.UpdateBounds();

    if (notificationType == juce::sendNotification || notificationType == juce::sendNotificationAsync)
    {
        if (onChange != nullptr)
        {
            onChange();
        }
    }

    repaint();
}

int ThemedDropdown::getNumItems() const
{
    return items.size();
}

int ThemedDropdown::getSelectedItemIndex() const
{
    return selectedItemIndex;
}

void ThemedDropdown::setSelectedItemIndex(int newSelectedItemIndex, juce::NotificationType notificationType)
{
    const int clampedItemIndex = juce::jlimit(-1, items.size() - 1, newSelectedItemIndex);

    if (selectedItemIndex == clampedItemIndex)
    {
        return;
    }

    selectedItemIndex = clampedItemIndex;
    repaint();

    if (notificationType == juce::sendNotification || notificationType == juce::sendNotificationAsync)
    {
        if (onChange != nullptr)
        {
            onChange();
        }
    }
}

juce::String ThemedDropdown::getText() const
{
    if (selectedItemIndex >= 0 && selectedItemIndex < items.size())
    {
        return items.getReference(selectedItemIndex).text;
    }

    return {};
}

juce::Rectangle<int> ThemedDropdown::GetClosedBounds() const
{
    return juce::Rectangle<int>(0, 0, getWidth(), getHeight());
}

juce::Rectangle<int> ThemedDropdown::GetArrowBounds() const
{
    return juce::Rectangle<int>(getWidth() - 24, 0, 24, getHeight());
}

void ThemedDropdown::SetExpanded(bool shouldBeExpanded)
{
    if (isExpanded == shouldBeExpanded)
    {
        return;
    }

    isExpanded = shouldBeExpanded;
    popupList.setVisible(isExpanded);

    if (isExpanded)
    {
        popupList.UpdateBounds();
        popupList.toFront(false);
        toFront(false);
    }

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

    const int selectedItemIndex = choiceParameter->getIndex();

    ignoreCallbacks.store(true);
    dropdown.setSelectedItemIndex(selectedItemIndex, juce::dontSendNotification);
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

    const int selectedItemIndex = dropdown.getSelectedItemIndex();

    if (selectedItemIndex < 0)
    {
        return;
    }

    const int numberOfChoices = choiceParameter->choices.size();

    if (numberOfChoices <= 1)
    {
        return;
    }

    const float normalizedValue =
        static_cast<float>(selectedItemIndex)
        / static_cast<float>(numberOfChoices - 1);

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalizedValue);
    parameter->endChangeGesture();
}