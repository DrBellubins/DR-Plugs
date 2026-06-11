#include "Rack.h"

void Rack::CreateRackLayout(juce::Component& parent,
                            AudioPluginAudioProcessor& processorRef,
                            const RackTheme& newTheme,
                            int x, int y, int width, int height)
{
    processor = &processorRef;
    apvts = &processorRef.parameters;

    theme = newTheme;

    parent.addAndMakeVisible(*this);
    setBounds(x, y, width, height);

    LayoutModules();
    repaint();
}

void Rack::RegisterModule(Module& module)
{
    modules.push_back(&module);

    if (module.getParentComponent() != this)
        addAndMakeVisible(module);

    LayoutModules();
}

void Rack::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();

    graphics.setColour(theme.rackBackgroundColour);
    graphics.fillRoundedRectangle(bounds, theme.rackCornerRadius);

    graphics.setColour(theme.rackBackgroundColour.brighter(theme.rackOutlineBrightenAmount));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), theme.rackCornerRadius, theme.rackOutlineThickness);
}

void Rack::resized()
{
    LayoutModules();
}

void Rack::LayoutModules()
{
    const int contentX = theme.rackPadding;
    const int contentY = theme.rackPadding;
    const int moduleHeight = getHeight() - (theme.rackPadding * 2);

    int currentX = contentX;

    for (auto* module : modules)
    {
        if (module == nullptr)
            continue;

        module->setBounds(currentX,
                          contentY,
                          theme.moduleWidth,
                          juce::jmax(40, moduleHeight));

        currentX += theme.moduleWidth + theme.moduleGap;
    }
}