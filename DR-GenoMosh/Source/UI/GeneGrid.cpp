#include "GeneGrid.h"

#include <algorithm>

namespace gmosh
{
namespace
{
    constexpr int kRowHeight = 28;
    constexpr int kHeaderHeight = 26;
    constexpr int kPadding = 10;
}

//==============================================================================
GeneGrid::GeneGrid()
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    for (int group = 0; group < GrpCount; ++group)
    {
        auto header = std::make_unique<juce::Label>();
        header->setText (kGroupNames[static_cast<std::size_t> (group)], juce::dontSendNotification);
        header->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        // Group colours come from the table (4.3), so the page layout, the plant
        // and the DAW list cannot disagree about which colour is which group.
        header->setColour (juce::Label::textColourId, juce::Colour (groupColour (static_cast<Group> (group))));
        content.addAndMakeVisible (*header);
        groupHeaders[static_cast<std::size_t> (group)] = std::move (header);
    }

    for (std::size_t gene = 0; gene < kNumGenes; ++gene)
    {
        const GeneInfo& info = kGenes[gene];

        Row row;
        row.gene = gene;

        row.label = std::make_unique<juce::Label>();
        row.label->setText (info.label, juce::dontSendNotification);
        row.label->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        row.label->setTooltip (info.helpText);   // one sentence per gene (4.3, 12)
        content.addAndMakeVisible (*row.label);

        row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                     juce::Slider::NoTextBox);
        row.slider->setRange (0.0, 1.0, 0.001);
        row.slider->setTooltip (info.helpText);
        row.slider->setColour (juce::Slider::trackColourId, juce::Colour (0xff3a4a3a));
        row.slider->setColour (juce::Slider::thumbColourId, juce::Colour (0xff7fd08a));
        row.slider->addListener (this);
        content.addAndMakeVisible (*row.slider);

        rows.push_back (std::move (row));
    }
}

//==============================================================================
void GeneGrid::refresh()
{
    if (getGeneValue == nullptr)
        return;

    for (auto& row : rows)
    {
        const float value = getGeneValue (row.gene);
        row.slider->setValue (value, juce::sendNotificationAsync);
    }
}

//==============================================================================
void GeneGrid::sliderValueChanged (juce::Slider* slider)
{
    for (auto& row : rows)
    {
        if (row.slider.get() == slider && setGeneValue != nullptr)
        {
            setGeneValue (row.gene, static_cast<float> (slider->getValue()));
            return;
        }
    }
}

//==============================================================================
int GeneGrid::rowsInGroup (int group) const
{
    int count = 0;
    for (const auto& row : rows)
        if (kGenes[row.gene].group == group)
            ++count;
    return count;
}

//==============================================================================
int GeneGrid::placeColumn (const std::vector<int>& groups, std::size_t first,
                           std::size_t last, int x, int width)
{
    int y = kPadding;
    const int labelWidth = juce::jlimit (90, 150, width / 3);

    for (std::size_t i = first; i < last; ++i)
    {
        const int group = groups[i];
        auto* header = groupHeaders[static_cast<std::size_t> (group)].get();
        if (header == nullptr)
            continue;

        header->setBounds (x, y, width, kHeaderHeight - 8);
        y += kHeaderHeight;

        for (auto& row : rows)
        {
            if (kGenes[row.gene].group != group)
                continue;

            row.label->setBounds (x, y, labelWidth, kRowHeight - 8);
            row.slider->setBounds (x + labelWidth + 6, y, width - labelWidth - 6, kRowHeight - 8);
            y += kRowHeight;
        }

        y += 6;
    }

    return y;
}

//==============================================================================
void GeneGrid::resized()
{
    viewport.setBounds (getLocalBounds());

    const int totalWidth = viewport.getMaximumVisibleWidth();
    const int columnWidth = totalWidth / 2;

    // Two columns of *rows*, split at a group boundary. The old code cut at
    // GrpCount / 2, which only worked while every group happened to be the same
    // size; the documented grouping is 4/4/4/4, 5/4/6 and 6/1/6/4, so it has to
    // be counted out here.
    std::vector<int> groups;
    for (int group = 0; group < GrpCount; ++group)
        if (rowsInGroup (group) > 0)
            groups.push_back (group);

    int totalRows = 0;
    for (int group : groups)
        totalRows += rowsInGroup (group);

    std::size_t split = groups.size();
    int counted = 0;
    for (std::size_t i = 0; i < groups.size(); ++i)
    {
        counted += rowsInGroup (groups[i]);
        if (totalRows > 0 && counted * 2 >= totalRows)
        {
            split = i + 1;
            break;
        }
    }

    const int bottomLeft  = placeColumn (groups, 0, split, kPadding, columnWidth - kPadding);
    const int bottomRight = placeColumn (groups, split, groups.size(),
                                         columnWidth + kPadding, columnWidth - kPadding);

    contentHeight = std::max (bottomLeft, bottomRight) + kPadding;
    content.setSize (totalWidth, contentHeight);
    content.setBounds (0, 0, totalWidth, contentHeight);
}

} // namespace gmosh
