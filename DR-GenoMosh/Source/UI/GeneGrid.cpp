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
        header->setColour (juce::Label::textColourId, juce::Colours::lightgoldenrodyellow);
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
        content.addAndMakeVisible (*row.label);

        row.slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                     juce::Slider::NoTextBox);
        row.slider->setRange (0.0, 1.0, 0.001);
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
int GeneGrid::placeColumn (int column, int firstGroup, int lastGroup, int x, int width)
{
    int y = kPadding;
    const int labelWidth = juce::jlimit (90, 150, width / 3);

    for (int group = firstGroup; group < lastGroup; ++group)
    {
        auto* header = groupHeaders[static_cast<std::size_t> (group)].get();
        if (header == nullptr)
            continue;

        bool any = false;
        for (const auto& row : rows)
        {
            if (kGenes[row.gene].group == group)
            {
                any = true;
                break;
            }
        }
        if (! any)
            continue;

        header->setBounds (x, y, width, kHeaderHeight - 8);
        y += kHeaderHeight;

        for (auto& row : rows)
        {
            if (kGenes[row.gene].group != group)
                continue;

            row.label->setBounds (x, y, labelWidth, kRowHeight - 8);
            row.slider->setBounds (x + labelWidth + 6, y,
                                   width - labelWidth - 6 - column * 0, kRowHeight - 8);
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

    const int half = GrpCount / 2;
    const int bottomLeft = placeColumn (0, 0, half, kPadding, columnWidth - kPadding);
    const int bottomRight = placeColumn (1, half, GrpCount, columnWidth + kPadding,
                                         columnWidth - kPadding);

    contentHeight = std::max (bottomLeft, bottomRight) + kPadding;
    content.setSize (totalWidth, contentHeight);
    content.setBounds (0, 0, totalWidth, contentHeight);
}

} // namespace gmosh
