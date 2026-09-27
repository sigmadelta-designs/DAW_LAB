#pragma once

#include "SParamChannel.h"
#include "DawLabLookAndFeel.h"

// Load a Touchstone file, choose how its ports map onto the + / - traces, and see what it does:
// insertion loss and mode conversion against frequency, the differential step response, and the
// numbers at the link's Nyquist frequency.
class SParamEditor : public juce::AudioProcessorEditor,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit SParamEditor (SParamChannel& channelToEdit);
    ~SParamEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override { repaint(); }
    void chooseFile();
    void drawResponse (juce::Graphics&, juce::Rectangle<int>);
    void drawStep (juce::Graphics&, juce::Rectangle<int>);

    SParamChannel& channel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;

    juce::TextButton loadButton { "Load .sNp file..." }, clearButton { "Clear" };
    juce::ComboBox portsBox, tapsBox;
    juce::Label portsLabel { {}, "Ports" }, tapsLabel { {}, "Filter" };
    juce::ToggleButton enabledButton { "Enabled" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> portsAttachment, tapsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;

    juce::Rectangle<int> responseArea, stepArea, textArea;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SParamEditor)
};
