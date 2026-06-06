#pragma once

#include "../PluginEditor.h"
#include "PitchPageLayout.h"

class TabbedPageBoxLayout
{
public:
    void CreatePageBoxLayout(juce::Component& parent, UIHelpers uiHelpers, AudioPluginAudioProcessor& processorRef,
        int x, int y, int width, int height)
    {
        TabbedPageBoxMain = std::make_unique<TabbedPageBox>();

        parent.addAndMakeVisible(*TabbedPageBoxMain);

        TabbedPageBoxMain->setBounds(x, y, width, height);

        PitchPage = std::make_unique<juce::Component>();
        DistortionPage = std::make_unique<juce::Component>();
        TapePage = std::make_unique<juce::Component>();
        GranularPage = std::make_unique<juce::Component>();

        TabbedPageBoxMain->AddTab("Pitch", PitchPage.get());
        TabbedPageBoxMain->AddTab("Distortion", DistortionPage.get());
        TabbedPageBoxMain->AddTab("Tape", TapePage.get());
        TabbedPageBoxMain->AddTab("Granular", GranularPage.get());

        PitchLayout.CreatePitchPageLayout(*PitchPage, uiHelpers, processorRef);
    }

    std::unique_ptr<TabbedPageBox> TabbedPageBoxMain;

    // Pages
    std::unique_ptr<juce::Component> PitchPage;
    std::unique_ptr<juce::Component> DistortionPage;
    std::unique_ptr<juce::Component> TapePage;
    std::unique_ptr<juce::Component> GranularPage;

    // Layouts
    PitchPageLayout PitchLayout;
};
