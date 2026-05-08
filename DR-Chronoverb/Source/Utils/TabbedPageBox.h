#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

class TabbedPageBox : public juce::Component
{
public:
    struct Tab
    {
        juce::String Title;
        juce::Component* PageComponent = nullptr;
    };

    TabbedPageBox()
    {
        setInterceptsMouseClicks(true, true);
    }

    ~TabbedPageBox() override = default;

    void AddTab(const juce::String& tabTitle, juce::Component* pageComponent)
    {
        jassert(pageComponent != nullptr);

        Tab newTab;
        newTab.Title = tabTitle;
        newTab.PageComponent = pageComponent;

        tabs.push_back(newTab);

        addAndMakeVisible(pageComponent);
        pageComponent->setVisible(false);

        if (selectedTabIndex < 0)
        {
            selectedTabIndex = 0;
            updatePageVisibility();
        }

        resized();
        repaint();
    }

    void ClearTabs()
    {
        for (Tab& tab : tabs)
        {
            if (tab.PageComponent != nullptr)
            {
                removeChildComponent(tab.PageComponent);
            }
        }

        tabs.clear();
        selectedTabIndex = -1;

        repaint();
    }

    int GetSelectedTabIndex() const
    {
        return selectedTabIndex;
    }

    void SetSelectedTabIndex(int newSelectedTabIndex, juce::NotificationType notificationType = juce::sendNotificationAsync)
    {
        const int clampedIndex = juce::jlimit(0, static_cast<int>(tabs.size()) - 1, newSelectedTabIndex);

        if (clampedIndex == selectedTabIndex || tabs.empty())
        {
            return;
        }

        selectedTabIndex = clampedIndex;
        updatePageVisibility();
        repaint();

        if (notificationType == juce::sendNotification || notificationType == juce::sendNotificationAsync)
        {
            if (onTabChanged != nullptr)
            {
                onTabChanged(selectedTabIndex);
            }
        }
    }

    juce::String GetSelectedTabTitle() const
    {
        if (selectedTabIndex >= 0 && selectedTabIndex < static_cast<int>(tabs.size()))
        {
            return tabs[static_cast<size_t>(selectedTabIndex)].Title;
        }

        return {};
    }

    void SetCornerRadius(float newCornerRadius)
    {
        cornerRadius = juce::jmax(0.0f, newCornerRadius);
        repaint();
    }

    void SetTabHeight(int newTabHeight)
    {
        tabHeight = juce::jmax(20, newTabHeight);
        resized();
        repaint();
    }

    void SetTabWidth(int newTabWidth)
    {
        tabWidth = juce::jmax(40, newTabWidth);
        resized();
        repaint();
    }

    void SetInnerPadding(int newInnerPadding)
    {
        innerPadding = juce::jmax(0, newInnerPadding);
        resized();
        repaint();
    }

    std::function<void(int)> onTabChanged;

    void paint(juce::Graphics& graphics) override
    {
        const juce::Rectangle<int> localBounds = getLocalBounds();
        const juce::Rectangle<float> panelBounds = localBounds.toFloat();

        graphics.setColour(AccentGray.darker(0.25f));
        graphics.fillRoundedRectangle(panelBounds, cornerRadius);

        if (tabs.empty())
        {
            return;
        }

        juce::Font tabFont("Liberation Sans", 14.0f, juce::Font::bold);
        tabFont.setExtraKerningFactor(0.05f);
        graphics.setFont(tabFont);

        for (int tabIndex = 0; tabIndex < static_cast<int>(tabs.size()); ++tabIndex)
        {
            const juce::Rectangle<int> tabBounds = getTabBounds(tabIndex);
            const bool isSelected = (tabIndex == selectedTabIndex);

            juce::Colour tabColour = isSelected ? ThemePink : AccentGray.brighter(0.1f);
            juce::Colour textColour = juce::Colours::white;

            graphics.setColour(tabColour);
            graphics.fillRoundedRectangle(tabBounds.toFloat(), tabCornerRadius);

            graphics.setColour(textColour);
            graphics.drawFittedText(
                tabs[static_cast<size_t>(tabIndex)].Title,
                tabBounds.reduced(8, 2),
                juce::Justification::centred,
                1
            );
        }
    }

    void resized() override
    {
        const juce::Rectangle<int> contentBounds = getContentBounds();

        for (int tabIndex = 0; tabIndex < static_cast<int>(tabs.size()); ++tabIndex)
        {
            if (tabs[static_cast<size_t>(tabIndex)].PageComponent != nullptr)
            {
                tabs[static_cast<size_t>(tabIndex)].PageComponent->setBounds(contentBounds);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& mouseEvent) override
    {
        for (int tabIndex = 0; tabIndex < static_cast<int>(tabs.size()); ++tabIndex)
        {
            if (getTabBounds(tabIndex).contains(mouseEvent.getPosition()))
            {
                SetSelectedTabIndex(tabIndex, juce::sendNotificationAsync);
                return;
            }
        }
    }

private:
    std::vector<Tab> tabs;

    int selectedTabIndex = -1;

    float cornerRadius = 25.0f;
    float tabCornerRadius = 5.0f;
    int headerHeight = 34;
    int tabHeight = 22;
    int tabWidth = 100;
    int innerPadding = 10;
    int tabLeftPadding = 12;
    int tabGap = 6;
    int tabVerticalOffset = -11;

    // Extends the page area upward so pages can occupy space behind / just under the tabs.
    // Increase this if you want more usable page area above the normal content start.
    int pageBoundsExtendUpwards = 14;

    juce::Rectangle<int> getTabBounds(int tabIndex) const
    {
        const int x = tabLeftPadding + (tabIndex * (tabWidth + tabGap));
        const int y = ((headerHeight - tabHeight) / 2) + tabVerticalOffset;

        return { x, y, tabWidth, tabHeight };
    }

    juce::Rectangle<int> getContentBounds() const
    {
        juce::Rectangle<int> bounds = getLocalBounds();

        bounds.removeFromTop(headerHeight);
        bounds.reduce(innerPadding, 0);
        bounds.removeFromTop(2);
        bounds.removeFromBottom(innerPadding);

        // Extend page area upward so components can live closer to / slightly behind tabs.
        bounds.setY(bounds.getY() - pageBoundsExtendUpwards);
        bounds.setHeight(bounds.getHeight() + pageBoundsExtendUpwards);

        return bounds;
    }

    void updatePageVisibility()
    {
        for (int tabIndex = 0; tabIndex < static_cast<int>(tabs.size()); ++tabIndex)
        {
            if (tabs[static_cast<size_t>(tabIndex)].PageComponent != nullptr)
            {
                tabs[static_cast<size_t>(tabIndex)].PageComponent->setVisible(tabIndex == selectedTabIndex);
            }
        }
    }
};
