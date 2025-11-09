#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/HorizontalRangeSlider.h"
#include "Utils/Theme.h"
#include "Utils/ThemedCheckbox.h"

static FlatRotaryLookAndFeel flatKnobLAF;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (700, 300);

    startTimerHz(20); // Update 20 times per second

    // Rate Knob
    arpRateKnob = std::make_unique<ThemedKnob>(
        "Arp Rate", nullptr, nullptr, " Rate", juce::Slider::NoTextBox);

    arpRateKnob->setTextValueSuffix(" Rate");
    arpRateKnob->setLookAndFeel(&flatKnobLAF);

    arpRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "arpRate", *arpRateKnob);

    addAndMakeVisible(*arpRateKnob);

	int arpRateWidthHeight = 150;
	int arpRateX = (getWidth() / 2) - (arpRateWidthHeight / 2);
	int arpRateY = (getHeight() / 2) - (arpRateWidthHeight / 2);

    arpRateKnob->setBounds(arpRateX, arpRateY - 50, arpRateWidthHeight, arpRateWidthHeight);

	// Octave range slider
	octaveRangeSlider = std::make_unique<HorizontalRangeSlider>(-48.0f, 48.0f);

	addAndMakeVisible(*octaveRangeSlider);

	octaveRangeSliderAttachment = std::make_unique<HorizontalRangeSliderAttachment>(
	processor.parameters, "thresholdLow", "thresholdHigh", *octaveRangeSlider);

	int octaveSliderWidth = 400;
	int octaveSliderHeight = 25;
	int octaveSliderX = (getWidth() / 2) - (octaveSliderWidth / 2);
	int octaveSliderY = (getHeight() / 2) - (octaveSliderHeight / 2);

	octaveRangeSlider->setBounds(octaveSliderX,  octaveSliderY + 100, octaveSliderWidth, octaveSliderHeight);
	octaveRangeSlider->setRoundness(10.0f);

	// Octave range labels
	octaveRangeLowLabel = std::make_unique<juce::Label>();
	octaveRangeHighLabel = std::make_unique<juce::Label>();

	octaveRangeLowLabel->setJustificationType(juce::Justification::centredLeft);
	octaveRangeHighLabel->setJustificationType(juce::Justification::centredRight);

	addAndMakeVisible(*octaveRangeLowLabel);
	addAndMakeVisible(*octaveRangeHighLabel);

	octaveRangeLowLabel->setText(juce::String(octaveRangeSlider->getLowerValue(), 1), juce::dontSendNotification);
	octaveRangeHighLabel->setText(juce::String(octaveRangeSlider->getUpperValue(), 1), juce::dontSendNotification);

	// Low
	int lowLabelWidth = octaveRangeLowLabel->getFont().getStringWidth(octaveRangeLowLabel->getText());
	int lowLabelHeight = octaveRangeLowLabel->getFont().getHeight();
	int lowLabelX = 100 - (lowLabelWidth / 2);
	int lowLabelY = (getHeight() - 53) - (lowLabelHeight / 2);

	octaveRangeLowLabel->setBounds(lowLabelX, lowLabelY, lowLabelWidth, lowLabelHeight);

	octaveRangeSlider->OnLowerValueChanged = [this](float NewValue)
	{
		octaveRangeLowLabel->setText(juce::String(NewValue, 1), juce::dontSendNotification);
	};

	// High
	int highLabelWidth = octaveRangeLowLabel->getFont().getStringWidth(octaveRangeLowLabel->getText());
	int highLabelHeight = octaveRangeLowLabel->getFont().getHeight();
	int highLabelX = (getWidth() - (highLabelWidth / 2)) - 100;
	int highLabelY = (getHeight() - 53) - (highLabelHeight / 2);

	octaveRangeHighLabel->setBounds(highLabelX, highLabelY, highLabelWidth, highLabelHeight);

	octaveRangeSlider->OnUpperValueChanged = [this](float NewValue)
	{
		octaveRangeHighLabel->setText(juce::String(NewValue, 1), juce::dontSendNotification);
	};

    // Free mode checkbox
    freeModeCheckbox = std::make_unique<ThemedCheckbox>("Free mode");

    freeModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.parameters, "isFreeMode", *freeModeCheckbox);

    addAndMakeVisible(*freeModeCheckbox);

    freeModeCheckbox->setBounds(50, 50, 150, 32); // x, y, width, height
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    graphics.fillAll(BGGray);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor.
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    float arpRate = processorRef.parameters.getRawParameterValue("arpRate")->load();
    bool isFreeMode = processorRef.parameters.getRawParameterValue("isFreeMode")->load() > 0.5f;

    static constexpr float beatFractionS[] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f };
    static constexpr int numBeatFractions = 6;

	float maxFraction = beatFractionS[0]; // 1/1
	float minFraction = beatFractionS[numBeatFractions - 1]; // 1/32

    // Track last mode to detect switching
    static bool lastIsFreeMode = isFreeMode;

    // Only snap and update parameter when switching into fractional mode
    if (!isFreeMode && lastIsFreeMode)
    {
        // Just switched from free to fractional mode
        float snappedArpRate = std::round(arpRate * 5.0f) / 5.0f;
        arpRateKnob->setValue(snappedArpRate, juce::sendNotification); // This updates parameter!
    }

    lastIsFreeMode = isFreeMode;

    if (isFreeMode) // Free mode
    {
    	arpRateKnob->setRange(0.0, 1.0, 0.001);

        // Calculate fraction with log interpolation
    	float fraction = maxFraction * std::pow(minFraction / maxFraction, arpRate);
        float hzValue = processorRef.BPM * fraction * 0.0166666666667;

        arpRateKnob->setLabelText("Arp Rate\n\n\n" + juce::String(hzValue, 2) + " Hz");
        arpRateKnob->setValueToTextFunction(nullptr);
        arpRateKnob->setTextToValueFunction(nullptr);

    	//DBG("free mode arpRate: " << arpRate << " Knob value: " << arpRateKnob->getValue());
    }
    else // Fractional
    {
    	arpRateKnob->setRange(0.0, 1.0, 1.0 / 5.0);

        float snappedArpRate = std::round(arpRate * 5.0f) / 5.0f;

        int arpRateIndex = static_cast<int>(std::round(snappedArpRate * 5.0f));
        arpRateIndex = juce::jlimit(0, 5, arpRateIndex);

        static const juce::StringArray BeatFractions { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
        arpRateKnob->setLabelText("Arp Rate\n\n\n" + BeatFractions[arpRateIndex]);

        arpRateKnob->setValueToTextFunction([=](double Value)
        {
            int Index = static_cast<int>(std::round(Value * 5.0));
            Index = juce::jlimit(0, 5, Index);
            return BeatFractions[Index];
        });

        arpRateKnob->setTextToValueFunction([=](const juce::String& Text)
        {
            int Index = BeatFractions.indexOf(Text.trim());
            if (Index < 0) Index = 0;
            return (double)Index / 5.0;
        });

    	//DBG("fractional mode arpRate: " << arpRate << " Knob value: " << arpRateKnob->getValue());
    }
}
