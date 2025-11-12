#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class LabelAttachment : public juce::Component
{
public:
    enum class Position
    {
        Above,
        Below
    };

    LabelAttachment(const juce::String& LabelText,
                   juce::Component* TargetComponent,
                   Position LabelPosition = Position::Above)
        : targetComponent(TargetComponent),
          labelPosition(LabelPosition)
    {
        jassert(targetComponent != nullptr); // TargetComponent must not be null

        addAndMakeVisible(label);
        label.setText(LabelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);

        addAndMakeVisible(targetComponent); // or setVisible, if it's already owned by parent

        setInterceptsMouseClicks(false, true); // Let mouse events pass through to the knob if needed
    }

    void setLabelText(const juce::String& NewText)
    {
        label.setText(NewText, juce::dontSendNotification);
    }

    void setLabelPosition(Position NewPosition)
    {
        labelPosition = NewPosition;
        resized();
    }

    void setLabelHeight(int NewLabelHeight)
    {
        labelHeight = NewLabelHeight;
        resized();
    }

    juce::Label& getLabel() { return label; }

    void resized() override
    {
        auto area = getLocalBounds();
        if (labelPosition == Position::Above)
        {
            label.setBounds(area.removeFromTop(labelHeight));
            if (targetComponent)
                targetComponent->setBounds(area);
        }
        else // Below
        {
            if (targetComponent)
            {
                targetComponent->setBounds(area.removeFromTop(area.getHeight() - labelHeight));
            }
            label.setBounds(area);
        }
    }

private:
    juce::Label label;
    juce::Component* targetComponent = nullptr; // Not owned here!
    Position labelPosition = Position::Above;
    int labelHeight = 18; // Default, can be set

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabelAttachment)
};