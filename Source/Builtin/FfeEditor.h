#pragma once

#include "DiffFfe.h"

// Tap sliders plus a picture of the taps actually applied and what they do to
// the spectrum (DC vs Nyquist), compared with the channel's loss.
class FfeEditor : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit FfeEditor (DiffFfe& ffeToEdit);
    ~FfeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    DiffFfe& ffe;
    std::array<juce::Slider, DiffFfe::numTaps> sliders;
    std::array<juce::Label, DiffFfe::numTaps> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, DiffFfe::numTaps> attachments;
    juce::ToggleButton txModeButton { "TX mode: main = 1 - sum|other taps| (constant swing)" };
    juce::ToggleButton reverseButton { "Reverse tap direction (pre <-> post)" };
    juce::ToggleButton adaptButton { "Auto-adapt taps (link training against the Eye Scope downstream)" };
    juce::TextButton restartButton { "Restart" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> txAttachment, reverseAttachment, adaptAttachment;

    juce::Rectangle<int> plotArea, textArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FfeEditor)
};
