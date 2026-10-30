#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

#include "../Genetic/GeneTable.h"

namespace gmosh
//==============================================================================
// Debug/seed editor page: every gene in the table as a slider, grouped by
// section. This is the plain "12 sliders plus wheel" style debug UI the design
// document asks for before the plant UI: it is what makes the gene table
// tunable by ear.
//==============================================================================
{
class GeneGrid : public juce::Component,
             private juce::Slider::Listener
{
public:
    GeneGrid();

    std::function<float (std::size_t)> getGeneValue;
    std::function<void (std::size_t, float)> setGeneValue;

    /// Push the current genome into the sliders.
    void refresh();

    void resized() override;

private:
    struct Row
    {
        std::size_t gene = 0;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
    };

    void sliderValueChanged (juce::Slider* slider);
    int rowsInGroup (int group) const;
    int placeColumn (const std::vector<int>& groups, std::size_t first, std::size_t last,
                     int x, int width);

    juce::Viewport viewport;
    juce::Component content;
    std::vector<Row> rows;
    std::array<std::unique_ptr<juce::Label>, GrpCount> groupHeaders;
    int contentHeight = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneGrid)
};

} // namespace gmosh
