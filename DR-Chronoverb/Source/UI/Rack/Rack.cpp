#include "Rack.h"

void Rack::CreateRackLayout(juce::Component& parent,
                            AudioPluginAudioProcessor& processorRef,
                            //const RackTheme& newTheme,
                            int x, int y, int width, int height)
{
    processor = &processorRef;
    theme = RackTheme();

    //theme = newTheme;

    parent.addAndMakeVisible(*this);
    setBounds(x, y, width, height);

    if (distortionModule.getParentComponent() != this)
        addAndMakeVisible(distortionModule);

    distortionModule.CreateLayout(theme);

    LayoutModules();
    repaint();
}

void Rack::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour(theme.rackBackgroundColour);
    g.fillRoundedRectangle(bounds, theme.rackCornerRadius);

    g.setColour(theme.rackBackgroundColour.brighter(theme.rackOutlineBrightenAmount));
    g.drawRoundedRectangle(bounds.reduced(0.5f),
                           theme.rackCornerRadius,
                           theme.rackOutlineThickness);
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

    distortionModule.setBounds(contentX,
                               contentY,
                               theme.moduleWidth,
                               juce::jmax(40, moduleHeight));
}