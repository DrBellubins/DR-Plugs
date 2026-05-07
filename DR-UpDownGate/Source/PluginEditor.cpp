#include "PluginEditor.h"
#include "BinaryData.h"

static FlatRotaryLookAndFeel flatKnobLAF;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (300, 500);

    BGAndLogo = juce::ImageFileFormat::loadFrom(BinaryData::BGAndLogo_png, BinaryData::BGAndLogo_pngSize);

    // Range slider
    rangeSlider = std::make_unique<VerticalRangeSlider>(-60.0f, 0.0f);

    addAndMakeVisible(*rangeSlider);

    rangeSliderAttachment = std::make_unique<VerticalRangeSliderAttachment>(
    processor.parameters, "thresholdLow", "thresholdHigh", *rangeSlider);

    rangeSlider->setBounds(170, 50, 50, 400); // Position as needed
    rangeSlider->setRoundness(10.0f);

    // Attack knob
    attackKnob = std::make_unique<EnvelopeKnob>("Attack");
    attackKnob->setLookAndFeel(&flatKnobLAF);
    attackKnob->setSliderStyle(juce::Slider::RotaryVerticalDrag);
    attackKnob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    attackKnob->setRange(1.0, 1000.0, 1.0);

    attackKnob->getProperties().set(
        FlatRotaryLookAndFeel::KnobBackgroundColourPropertyName,
        static_cast<int>(AccentGray.brighter(0.15f).getARGB()));

    attackKnob->setBounds(25, 100, 100, 100);

    addAndMakeVisible(*attackKnob);

    attackKnobAttacthment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "attack", *attackKnob);

    // Release knob
    releaseKnob = std::make_unique<EnvelopeKnob>("Release");
    releaseKnob->setLookAndFeel(&flatKnobLAF);
    releaseKnob->setSliderStyle(juce::Slider::RotaryVerticalDrag);
    releaseKnob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    releaseKnob->setRange(1.0, 1000.0, 1.0);

    releaseKnob->getProperties().set(
        FlatRotaryLookAndFeel::KnobBackgroundColourPropertyName,
        static_cast<int>(AccentGray.getARGB()));

    releaseKnob->setBounds(25, 300, 100, 100);

    addAndMakeVisible(*releaseKnob);

    releaseKnobAttacthment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "release", *releaseKnob);

    // Level display
    gateLevelDisplay = std::make_unique<GateLevelDisplay>(processor, *rangeSlider);
    gateLevelDisplay->setBounds(275, 50, 15, 400);

    addAndMakeVisible(*gateLevelDisplay);

    // Tooltip Overlay
    tooltipOverlay = std::make_unique<TooltipOverlay>(*rangeSlider);
    tooltipOverlay->setBounds(getLocalBounds());

    addAndMakeVisible(*tooltipOverlay);

    tooltipOverlay->toFront(false);

    rangeSlider->OnTooltipStateChanged = [this]()
    {
        if (tooltipOverlay)
        {
            tooltipOverlay->repaint();
        }
    };
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() = default;

//==============================================================================c
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    graphics.fillAll(BGGray);

    if (BGAndLogo.isValid())
    {
        graphics.drawImage(BGAndLogo, getLocalBounds().toFloat());
    }
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor
    if (tooltipOverlay)
    {
        tooltipOverlay->setBounds(getLocalBounds());
        tooltipOverlay->toFront(false);
    }
}
