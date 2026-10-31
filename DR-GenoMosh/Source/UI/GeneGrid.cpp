#include "GeneGrid.h"

#include <algorithm>

namespace gmosh
{
namespace
{
constexpr int kRowHeight = 28;
constexpr int kHeaderHeight = 26;
constexpr int kPadding = 10;
} // namespace

//==============================================================================
GeneGrid::GeneGrid()
{
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);

    for (int groupIndex = 0; groupIndex < GrpCount; ++groupIndex)
    {
        auto header = std::make_unique<juce::Label>();
        header->setText(kGroupNames[static_cast<std::size_t>(groupIndex)], juce::dontSendNotification);
        header->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        header->setColour(juce::Label::textColourId, juce::Colours::lightgoldenrodyellow);
        content.addAndMakeVisible(*header);
        groupHeaders[static_cast<std::size_t>(groupIndex)] = std::move(header);
    }

    for (std::size_t geneIndex = 0; geneIndex < kNumGenes; ++geneIndex)
    {
        const GeneInfo& geneInfo = kGenes[geneIndex];

        Row row;
        row.geneIndex = geneIndex;

        row.label = std::make_unique<juce::Label>();
        row.label->setText(geneInfo.label, juce::dontSendNotification);
        row.label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        content.addAndMakeVisible(*row.label);

        row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        row.slider->setRange(0.0, 1.0, 0.001);
        row.slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff3a4a3a));
        row.slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xff7fd08a));
        row.slider->addListener(this);
        content.addAndMakeVisible(*row.slider);

        rows.push_back(std::move(row));
    }
}

//==============================================================================
void GeneGrid::refresh()
{
    if (getGeneValue == nullptr)
        return;

    for (auto& row : rows)
    {
        const float value = getGeneValue(row.geneIndex);
        row.slider->setValue(value, juce::sendNotificationAsync);
    }
}

//==============================================================================
void GeneGrid::sliderValueChanged(juce::Slider* slider)
{
    for (auto& row : rows)
    {
        if (row.slider.get() == slider && setGeneValue != nullptr)
        {
            setGeneValue(row.geneIndex, static_cast<float>(slider->getValue()));
            return;
        }
    }
}

//==============================================================================
// Lays out one column of groups from top to bottom and returns the height it
// used, which resized() needs to size the scrollable content.
int GeneGrid::placeColumn(int columnIndex, int firstGroupIndex, int lastGroupIndex, int columnX,
                          int columnWidth)
{
    int yCursor = kPadding;
    const int labelWidth = juce::jlimit(90, 150, columnWidth / 3);

    for (int groupIndex = firstGroupIndex; groupIndex < lastGroupIndex; ++groupIndex)
    {
        auto* header = groupHeaders[static_cast<std::size_t>(groupIndex)].get();

        if (header == nullptr)
            continue;

        bool hasRows = false;

        for (const auto& row : rows)
        {
            if (kGenes[row.geneIndex].group == groupIndex)
            {
                hasRows = true;
                break;
            }
        }

        if (!hasRows)
            continue;

        header->setBounds(columnX, yCursor, columnWidth, kHeaderHeight - 8);
        yCursor += kHeaderHeight;

        for (auto& row : rows)
        {
            if (kGenes[row.geneIndex].group != groupIndex)
                continue;

            row.label->setBounds(columnX, yCursor, labelWidth, kRowHeight - 8);
            row.slider->setBounds(columnX + labelWidth + 6, yCursor, columnWidth - labelWidth - 6,
                                  kRowHeight - 8);
            yCursor += kRowHeight;
        }

        yCursor += 6;
    }

    return yCursor;
}

//==============================================================================
void GeneGrid::resized()
{
    viewport.setBounds(getLocalBounds());

    const int totalWidth = viewport.getMaximumVisibleWidth();
    const int columnWidth = totalWidth / 2;

    const int halfGroupCount = GrpCount / 2;
    const int bottomLeft = placeColumn(0, 0, halfGroupCount, kPadding, columnWidth - kPadding);
    const int bottomRight =
        placeColumn(1, halfGroupCount, GrpCount, columnWidth + kPadding, columnWidth - kPadding);

    contentHeight = std::max(bottomLeft, bottomRight) + kPadding;
    content.setSize(totalWidth, contentHeight);
    content.setBounds(0, 0, totalWidth, contentHeight);
}

} // namespace gmosh
