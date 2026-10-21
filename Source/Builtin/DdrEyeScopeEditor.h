#pragma once

#include "EyeAnalyzer.h"
#include "DdrEyeScope.h"
#include "DawLabLookAndFeel.h"

class DdrEyeScopeEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit DdrEyeScopeEditor (DdrEyeScope& scopeToEdit);
    ~DdrEyeScopeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawEye (juce::Graphics&, juce::Rectangle<float> area);

    DdrEyeScope& scope;
    EyeAnalyzer analyzer;
    juce::Image image;
    std::vector<float> pulledSamples;
    std::vector<double> pulledIndices;

    juce::Slider scaleSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox persistenceBox, probeBox;
    juce::TextButton clearButton { "Clear" }, relockButton { "Re-lock" };
    juce::ToggleButton thruButton { "Pass-through" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> thruAttachment;
    juce::Label scaleLabel { {}, "Vertical scale" }, persistenceLabel { {}, "Persistence" }, probeLabel { {}, "Probe" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> scaleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> persistenceAttachment, probeAttachment;
    int lastProbe = -1;

    juce::Rectangle<int> eyeArea, readoutArea;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DdrEyeScopeEditor)
};
