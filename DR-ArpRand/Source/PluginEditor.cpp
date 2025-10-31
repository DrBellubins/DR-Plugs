#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Utils/FlatRotaryLookAndFeel.h"
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

    arpRateKnob->setBounds((getWidth() / 2) - 100, (getHeight() / 2) - 100, 200, 200);

    // Free mode checkbox
    freeRateCheckbox = std::make_unique<ThemedCheckbox>("Free mode");

    freeRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.parameters, "isFreeMode", *freeRateCheckbox);

    addAndMakeVisible(*freeRateCheckbox);

    freeRateCheckbox->setBounds(50, 50, 150, 32); // x, y, width, height
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

    float minFraction = beatFractionS[numBeatFractions - 1];
    float maxFraction = beatFractionS[0];

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
        // Calculate fraction with log interpolation
        float fraction = minFraction * std::pow(maxFraction / minFraction, arpRate);
        float hzValue = processorRef.BPM * fraction * 0.0166666666667;

        arpRateKnob->setLabelText("Arp Rate\n\n\n" + juce::String(hzValue, 2) + " Hz");
        arpRateKnob->setValueToTextFunction(nullptr);
        arpRateKnob->setTextToValueFunction(nullptr);

    	//DBG("free mode arpRate: " << arpRate << " Knob value: " << arpRateKnob->getValue());
    }
    else // Fractional
    {
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
