#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// ThemedCheckbox: Custom checkbox using ThemePink and AccentGray colors, with a rounded square indicator.
class ThemedCheckbox : public juce::ToggleButton
{
public:
    ThemedCheckbox(const juce::String& checkboxText = juce::String())
        : juce::ToggleButton(checkboxText)
    {
    }

    void paintButton(
        juce::Graphics& graphics,
        bool shouldDrawButtonAsHighlighted,
        bool shouldDrawButtonAsDown
    ) override
    {
        auto bounds = getLocalBounds().toFloat();

        float outerSize = juce::jmin(bounds.getHeight(), 28.0f);
        float innerSize = outerSize * 0.6f;
        float outerRadius = outerSize / 4.0f;
        float innerRadius = innerSize / 4.0f;

        float boxX = bounds.getX();
        float boxY = bounds.getCentreY() - outerSize / 2.0f;

        juce::Rectangle<float> outerRect(boxX, boxY, outerSize, outerSize);
        graphics.setColour(AccentGray);
        graphics.fillRoundedRectangle(outerRect, outerRadius);

        if (getToggleState())
        {
            float innerX = boxX + (outerSize - innerSize) / 2.0f;
            float innerY = boxY + (outerSize - innerSize) / 2.0f;
            juce::Rectangle<float> innerRect(innerX, innerY, innerSize, innerSize);
            graphics.setColour(ThemePink);
            graphics.fillRoundedRectangle(innerRect, innerRadius);
        }

        if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
        {
            graphics.setColour(FocusedGray);
            graphics.drawRoundedRectangle(outerRect, outerRadius, 2.0f);
        }

        graphics.setColour(juce::Colours::white);
        graphics.setFont(15.0f);

        float textX = boxX + outerSize + 8.0f;
        float textY = bounds.getY();
        float textWidth = bounds.getWidth() - textX;
        float textHeight = bounds.getHeight();

        juce::Rectangle<float> textRect(textX, textY, textWidth, textHeight);
        graphics.drawFittedText(getButtonText(), textRect.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    class Attachment : public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        Attachment(
            juce::AudioProcessorValueTreeState& state,
            const juce::String& parameterID,
            ThemedCheckbox& checkboxToControl
        )
            : apvts(state),
              attachedParameterID(parameterID),
              checkbox(checkboxToControl)
        {
            parameter = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(attachedParameterID));
            jassert(parameter != nullptr && "ThemedCheckbox::Attachment: Parameter ID must refer to an AudioParameterBool.");

            checkbox.onClick = [this]()
            {
                if (ignoreCallbacks)
                {
                    return;
                }

                if (parameter != nullptr)
                {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost(checkbox.getToggleState() ? 1.0f : 0.0f);
                    parameter->endChangeGesture();
                }
            };

            apvts.addParameterListener(attachedParameterID, this);

            if (parameter != nullptr)
            {
                checkbox.setToggleState(parameter->get(), juce::dontSendNotification);
            }
        }

        ~Attachment() override
        {
            apvts.removeParameterListener(attachedParameterID, this);
            checkbox.onClick = nullptr;
        }

        void parameterChanged(const juce::String& changedParameterID, float newValue) override
        {
            if (changedParameterID != attachedParameterID)
            {
                return;
            }

            const bool newState = (newValue >= 0.5f);

            juce::MessageManager::callAsync([this, newState]()
            {
                ignoreCallbacks = true;
                checkbox.setToggleState(newState, juce::dontSendNotification);
                ignoreCallbacks = false;
            });
        }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        juce::String attachedParameterID;
        juce::AudioParameterBool* parameter = nullptr;
        ThemedCheckbox& checkbox;
        std::atomic<bool> ignoreCallbacks { false };
    };
};