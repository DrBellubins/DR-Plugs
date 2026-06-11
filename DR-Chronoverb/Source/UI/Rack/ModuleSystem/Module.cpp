#include "Module.h"

namespace
{
    class PlaceholderScopeComponent final : public juce::Component
    {
    public:
        void SetColours(juce::Colour newBackground, juce::Colour newOutline, juce::Colour newWave)
        {
            background = newBackground;
            outline = newOutline;
            wave = newWave;
            repaint();
        }

        void paint(juce::Graphics& graphics) override
        {
            const auto bounds = getLocalBounds().toFloat();

            graphics.setColour(background.darker(0.5));
            graphics.fillRoundedRectangle(bounds, 6.0f);

            graphics.setColour(outline);
            graphics.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

            const auto area = getLocalBounds().reduced(8);
            const int midY = area.getCentreY();

            juce::Path waveform;
            waveform.startNewSubPath(static_cast<float>(area.getX()),
                                     static_cast<float>(midY));

            for (int x = 0; x < area.getWidth(); ++x)
            {
                const float t = static_cast<float>(x)
                              / static_cast<float>(juce::jmax(1, area.getWidth() - 1));

                const float y =
                    std::sin(t * juce::MathConstants<float>::twoPi * 2.0f) * 0.35f
                  + std::sin(t * juce::MathConstants<float>::twoPi * 5.0f) * 0.12f;

                waveform.lineTo(static_cast<float>(area.getX() + x),
                                static_cast<float>(midY) - y * static_cast<float>(area.getHeight()) * 0.5f);
            }

            graphics.setColour(wave);
            graphics.strokePath(waveform, juce::PathStrokeType(1.5f));
        }

    private:
        juce::Colour background = juce::Colours::black;
        juce::Colour outline = juce::Colours::darkgrey;
        juce::Colour wave = juce::Colours::white;
    };
}

Module::Module()
{
    enableButton.setClickingTogglesState(true);
    enableButton.setToggleState(true, juce::dontSendNotification);
    enableButton.setButtonText({});

    enableButton.onStateChange = [this]()
    {
        SetModuleEnabled(enableButton.getToggleState());
    };

    addAndMakeVisible(enableButton);

    oscilloscopePlaceholder = std::make_unique<PlaceholderScopeComponent>();
    addAndMakeVisible(*oscilloscopePlaceholder);

    SetModuleEnabled(true);
}

void Module::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();

    graphics.setColour(GetModuleBackgroundColour());
    graphics.fillRoundedRectangle(bounds, theme.moduleCornerRadius);

    graphics.setColour(GetModuleOutlineColour());
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), theme.moduleCornerRadius, 1.0f);
}

void Module::resized()
{
    LayoutBaseChrome();
}

void Module::SetThemeColour(juce::Colour newThemeColour)
{
    themeColour = newThemeColour;
    ApplyThemeToBaseChrome();
    repaint();
}

juce::Colour Module::GetThemeColour() const
{
    return themeColour;
}

void Module::SetModuleEnabled(bool shouldBeEnabled)
{
    moduleEnabled = shouldBeEnabled;

    const float alpha = moduleEnabled ? 1.0f : theme.disabledAlpha;

    for (int i = 0; i < getNumChildComponents(); ++i)
    {
        if (auto* child = getChildComponent(i))
        {
            if (child == &enableButton)
                child->setAlpha(1.0f);
            else
                child->setAlpha(alpha);
        }
    }

    repaint();
}

bool Module::IsModuleEnabled() const
{
    return moduleEnabled;
}

juce::Component& Module::GetOscilloscopePlaceholder()
{
    jassert(oscilloscopePlaceholder != nullptr);
    return *oscilloscopePlaceholder;
}

juce::ToggleButton& Module::GetEnableButton()
{
    return enableButton;
}

void Module::ApplyThemeToBaseChrome()
{
    enableButton.setColour(juce::ToggleButton::tickColourId, themeColour);
    enableButton.setColour(juce::ToggleButton::textColourId, juce::Colours::transparentBlack);

    if (auto* scope = dynamic_cast<PlaceholderScopeComponent*>(oscilloscopePlaceholder.get()))
    {
        scope->SetColours(
            GetModuleControlFillColour().darker(0.35f),
            GetModuleOutlineColour(),
            themeColour);
    }
}

void Module::LayoutBaseChrome()
{
    const int buttonX = theme.enableButtonMargin;
    const int buttonY = theme.enableButtonMargin;

    enableButton.setBounds(buttonX,
                           buttonY,
                           theme.enableButtonWidth,
                           theme.enableButtonHeight);

    const int contentX = theme.modulePadding;
    const int contentY = theme.modulePadding;
    const int contentW = getWidth() - (theme.modulePadding * 2);
    const int contentH = getHeight() - (theme.modulePadding * 2);

    const int scopeY = contentY + theme.headerHeight + theme.moduleInnerGap;

    const int maxScopeHeight = juce::jmax(24,
        getHeight() - scopeY - theme.modulePadding);

    const int scopeSide = juce::jmin(theme.scopeSize, maxScopeHeight);

    if (oscilloscopePlaceholder != nullptr)
        oscilloscopePlaceholder->setBounds(contentX,
                                           scopeY,
                                           juce::jmax(10, scopeSide),
                                           juce::jmax(10, scopeSide));
}

juce::Colour Module::GetModuleBackgroundColour() const
{
    return themeColour.darker(theme.moduleBackgroundDarkenAmount);
}

juce::Colour Module::GetModuleOutlineColour() const
{
    return GetModuleBackgroundColour().brighter(theme.moduleOutlineBrightenAmount);
}

juce::Colour Module::GetModuleLabelColour() const
{
    return themeColour.darker(theme.moduleLabelDarkenAmount);
}

juce::Colour Module::GetModuleSecondaryColour() const
{
    return themeColour.darker(theme.moduleSecondaryDarkenAmount);
}

juce::Colour Module::GetModuleControlFillColour() const
{
    return themeColour.darker(theme.moduleControlDarkenAmount);
}

void Module::AttachEnableButton(juce::AudioProcessorValueTreeState& apvts,
                                const juce::String& parameterID)
{
    enableAttachment = std::make_unique<ButtonAttachment>(apvts, parameterID, enableButton);
    SetModuleEnabled(enableButton.getToggleState());
}