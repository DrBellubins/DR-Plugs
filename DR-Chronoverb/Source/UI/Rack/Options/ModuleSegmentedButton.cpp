#include "ModuleSegmentedButton.h"

ModuleSegmentedButton::ModuleSegmentedButton()
{
    auto fontOptions = juce::FontOptions("Liberation Sans", 13.0f, juce::Font::bold);
    buttonFont = juce::Font(fontOptions);

    SetLabelVisible(true);
}

void ModuleSegmentedButton::SetOptions(const juce::StringArray& newOptions)
{
    options = newOptions;

    if (options.isEmpty())
    {
        selectedIndex = -1;
    }
    else if (selectedIndex < 0 || selectedIndex >= options.size())
    {
        selectedIndex = 0;
    }

    repaint();
}

const juce::StringArray& ModuleSegmentedButton::GetOptions() const
{
    return options;
}

void ModuleSegmentedButton::SetSelectedIndex(int newIndex, juce::NotificationType notification)
{
    const int clampedIndex = options.isEmpty()
        ? -1
        : juce::jlimit(0, options.size() - 1, newIndex);

    if (selectedIndex == clampedIndex)
        return;

    selectedIndex = clampedIndex;
    repaint();

    if ((notification == juce::sendNotification || notification == juce::sendNotificationAsync)
        && onSelectionChanged != nullptr)
    {
        onSelectionChanged(selectedIndex);
    }
}

void ModuleSegmentedButton::SetSelectedIndexSilently(int newIndex)
{
    const int clampedIndex = options.isEmpty()
        ? -1
        : juce::jlimit(0, options.size() - 1, newIndex);

    if (selectedIndex == clampedIndex)
        return;

    selectedIndex = clampedIndex;
    repaint();
}

int ModuleSegmentedButton::GetSelectedIndex() const
{
    return selectedIndex;
}

juce::String ModuleSegmentedButton::GetSelectedText() const
{
    if (selectedIndex >= 0 && selectedIndex < options.size())
        return options[selectedIndex];

    return {};
}

void ModuleSegmentedButton::AttachToParameter(juce::AudioProcessorValueTreeState& apvts,
                                              const juce::String& parameterID)
{
    attachment = std::make_unique<ChoiceAttachment>(apvts, parameterID, *this);
}

void ModuleSegmentedButton::SetCornerRadius(float newCornerRadius)
{
    cornerRadius = juce::jmax(0.0f, newCornerRadius);
    repaint();
}

void ModuleSegmentedButton::SetDividerThickness(float newDividerThickness)
{
    dividerThickness = juce::jmax(0.0f, newDividerThickness);
    repaint();
}

void ModuleSegmentedButton::SetFont(const juce::Font& newFont)
{
    buttonFont = newFont;
    repaint();
}

void ModuleSegmentedButton::SetControlBounds(const juce::Rectangle<int>& newBounds)
{
    controlBoundsOverride = newBounds;
    resized();
}

juce::Rectangle<int> ModuleSegmentedButton::GetControlBounds() const
{
    return GetResolvedControlBounds();
}

void ModuleSegmentedButton::SetLabelHeight(int newLabelHeight)
{
    labelHeight = juce::jmax(10, newLabelHeight);
    resized();
}

int ModuleSegmentedButton::GetLabelHeight() const
{
    return labelHeight;
}

void ModuleSegmentedButton::ApplyTheme(const RackTheme& rackTheme)
{
    ModuleOption::ApplyTheme(rackTheme);
    repaint();
}

void ModuleSegmentedButton::resized()
{
    const auto controlBounds = GetResolvedControlBounds();

    if (IsLabelVisible())
        label.setBounds(GetLabelBoundsBelow(controlBounds, labelHeight, currentTheme.labelOffsetBelow));
}

void ModuleSegmentedButton::paint(juce::Graphics& g)
{
    ModuleOption::paint(g);

    if (options.isEmpty())
        return;

    const auto accentColour = GetOptionAccentColour();
    const auto fillColour = GetOptionFillColour(currentTheme);
    const auto outlineColour = GetOptionOutlineColour(currentTheme);
    const auto textColour = juce::Colours::white;

    const auto bounds = GetResolvedControlBounds().toFloat();
    const int numOptions = options.size();
    const float segmentWidth = bounds.getWidth() / static_cast<float>(numOptions);

    for (int optionIndex = 0; optionIndex < numOptions; ++optionIndex)
    {
        const bool isFirst = (optionIndex == 0);
        const bool isLast = (optionIndex == numOptions - 1);
        const bool isSelected = (optionIndex == selectedIndex);
        const bool isHovered = (optionIndex == hoveredIndex);

        const float x = bounds.getX() + std::floor(segmentWidth * static_cast<float>(optionIndex));
        const float width = isLast
            ? (bounds.getRight() - x)
            : std::floor(segmentWidth);

        const juce::Rectangle<float> segmentBounds(x, bounds.getY(), width, bounds.getHeight());

        juce::Path segmentPath;
        segmentPath.addRoundedRectangle(segmentBounds.getX(),
                                        segmentBounds.getY(),
                                        segmentBounds.getWidth(),
                                        segmentBounds.getHeight(),
                                        cornerRadius,
                                        cornerRadius,
                                        isFirst,
                                        isLast,
                                        isFirst,
                                        isLast);

        g.setColour(isSelected ? accentColour : fillColour);
        g.fillPath(segmentPath);

        if (isHovered && !isSelected)
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillPath(segmentPath);
        }

        if (!isLast && dividerThickness > 0.0f)
        {
            g.setColour(outlineColour);
            g.fillRect(segmentBounds.getRight() - (dividerThickness * 0.5f),
                       segmentBounds.getY() + 2.0f,
                       dividerThickness,
                       segmentBounds.getHeight() - 4.0f);
        }

        g.setColour(textColour);
        g.setFont(buttonFont);
        g.drawFittedText(options[optionIndex],
                         segmentBounds.toNearestInt().reduced(4, 2),
                         juce::Justification::centred,
                         1);
    }

    g.setColour(outlineColour.brighter(0.15f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
}

void ModuleSegmentedButton::mouseMove(const juce::MouseEvent& e)
{
    const int newHoveredIndex = GetIndexFromPosition(static_cast<float>(e.x));

    if (hoveredIndex != newHoveredIndex)
    {
        hoveredIndex = newHoveredIndex;
        repaint();
    }
}

void ModuleSegmentedButton::mouseExit(const juce::MouseEvent& e)
{
    juce::ignoreUnused(e);
    hoveredIndex = -1;
    repaint();
}

void ModuleSegmentedButton::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled() || options.isEmpty())
        return;

    const int clickedIndex = GetIndexFromPosition(static_cast<float>(e.x));

    if (clickedIndex < 0 || clickedIndex >= options.size())
        return;

    if (onGestureBegin != nullptr)
        onGestureBegin();

    if (onGestureCommit != nullptr)
        onGestureCommit(clickedIndex);

    SetSelectedIndex(clickedIndex, juce::sendNotificationSync);

    if (onGestureEnd != nullptr)
        onGestureEnd();
}

int ModuleSegmentedButton::GetIndexFromPosition(float x) const
{
    const int numOptions = options.size();

    if (numOptions <= 0)
        return -1;

    const auto bounds = GetResolvedControlBounds();
    const float localX = x - static_cast<float>(bounds.getX());
    const float totalWidth = static_cast<float>(bounds.getWidth());

    if (totalWidth <= 0.0f)
        return -1;

    const float segmentWidth = totalWidth / static_cast<float>(numOptions);

    int index = static_cast<int>(std::floor(
        juce::jlimit(0.0f, totalWidth - 1.0f, localX) / segmentWidth));

    return juce::jlimit(0, numOptions - 1, index);
}

juce::Rectangle<int> ModuleSegmentedButton::GetResolvedControlBounds() const
{
    if (!controlBoundsOverride.isEmpty())
        return controlBoundsOverride;

    const auto area = getLocalBounds();
    const int labelSpace = IsLabelVisible() ? (labelHeight + currentTheme.labelOffsetBelow) : 0;
    return area.withTrimmedBottom(labelSpace);
}

// ============================ ChoiceAttachment ============================

ModuleSegmentedButton::ChoiceAttachment::ChoiceAttachment(
    juce::AudioProcessorValueTreeState& state,
    const juce::String& parameterID,
    ModuleSegmentedButton& controlToAttach)
    : apvts(state),
      attachedParameterID(parameterID),
      control(controlToAttach)
{
    parameter = apvts.getParameter(attachedParameterID);

    jassert(parameter != nullptr && "ModuleSegmentedButton::ChoiceAttachment: Parameter ID not found.");

    if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter))
    {
        if (control.GetOptions().isEmpty())
            control.SetOptions(choiceParameter->choices);
    }

    control.onGestureBegin = [this]()
    {
        if (parameter != nullptr)
            parameter->beginChangeGesture();
    };

    control.onGestureCommit = [this](int newIndex)
    {
        if (parameter != nullptr)
        {
            const float rawTargetValue = static_cast<float>(newIndex);
            const float normalised = parameter->convertTo0to1(rawTargetValue);
            parameter->setValueNotifyingHost(normalised);
        }
    };

    control.onGestureEnd = [this]()
    {
        if (parameter != nullptr)
            parameter->endChangeGesture();
    };

    apvts.addParameterListener(attachedParameterID, this);

    if (parameter != nullptr)
    {
        const float rawValue = parameter->convertFrom0to1(parameter->getValue());
        control.SetSelectedIndexSilently(static_cast<int>(std::round(rawValue)));
    }
}

ModuleSegmentedButton::ChoiceAttachment::~ChoiceAttachment()
{
    apvts.removeParameterListener(attachedParameterID, this);

    control.onGestureBegin = nullptr;
    control.onGestureCommit = nullptr;
    control.onGestureEnd = nullptr;
}

void ModuleSegmentedButton::ChoiceAttachment::parameterChanged(const juce::String& changedParameterID,
                                                               float newValue)
{
    if (changedParameterID != attachedParameterID)
        return;

    const int newIndex = static_cast<int>(std::round(newValue));

    juce::MessageManager::callAsync([this, newIndex]()
    {
        control.SetSelectedIndexSilently(newIndex);
        control.repaint();
    });
}