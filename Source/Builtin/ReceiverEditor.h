#pragma once

#include "BitCompare.h"
#include "SerdesReceiver.h"
#include "DawLabLookAndFeel.h"

class ReceiverEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit ReceiverEditor (SerdesReceiver& receiverToEdit);
    ~ReceiverEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct SliderRow
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;   // last: destroyed first
    };

    void timerCallback() override;
    SliderRow& addSlider (const char* parameterId, const juce::String& text, ControlTip::Domain domain,
                         const juce::String& reason, bool vertical = false);
    void drawCtle (juce::Graphics&, juce::Rectangle<int>);
    void drawCompare (juce::Graphics&, juce::Rectangle<int>);

    SerdesReceiver& receiver;
    BitCompare compare;
    int ticks = 0;

    std::vector<std::unique_ptr<SliderRow>> rows;
    std::map<juce::String, SliderRow*> byId;
    juce::ToggleButton enabledButton { "Receiver enabled" }, ctleAdaptButton { "Auto-adapt boost" }, dfeAdaptButton { "Auto-adapt DFE taps" },
                       trainerButton { "Feed the FFE trainer" };
    juce::ComboBox outputBox;
    juce::Label outputLabel { {}, "Output" };
    juce::TextButton resetButton { "Reset counters" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment, ctleAdaptAttachment, dfeAdaptAttachment, trainerAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputAttachment;

    juce::Rectangle<int> ctleTitleArea, ctleArea, cdrTitleArea, cdrTextArea, dfeArea, dfeNoteArea, compareArea;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReceiverEditor)
};
