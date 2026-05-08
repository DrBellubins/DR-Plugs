#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "ThemeContext.h"

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
    bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused(shouldDrawButtonAsDown);

        const juce::Colour adjustedAccentGray = ThemeContext::GetAdjustedColour(AccentGray, *this);
        const juce::Colour adjustedFocusedGray = ThemeContext::GetAdjustedColour(FocusedGray, *this);

        const juce::Rectangle<float> bounds = getLocalBounds().toFloat();
        const float outerRadius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.2f;

        graphics.setColour(adjustedAccentGray);
        graphics.fillRoundedRectangle(bounds, outerRadius);

        if (getToggleState())
        {
            const juce::Rectangle<float> innerBounds = bounds.reduced(
                bounds.getWidth() * 0.2f,
                bounds.getHeight() * 0.2f
            );

            const float innerRadius = juce::jmin(innerBounds.getWidth(), innerBounds.getHeight()) * 0.2f;

            graphics.setColour(ThemePink);
            graphics.fillRoundedRectangle(innerBounds, innerRadius);
        }

        if (shouldDrawButtonAsHighlighted)
        {
            graphics.setColour(adjustedFocusedGray);
            graphics.drawRoundedRectangle(bounds.reduced(1.0f), outerRadius, 2.0f);
        }
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