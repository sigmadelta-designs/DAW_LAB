#pragma once

#include "EyeAnalyzer.h"
#include "EyeScope.h"

class EyeScopeEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit EyeScopeEditor (EyeScope& scopeToEdit);
    ~EyeScopeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawEye (juce::Graphics&, juce::Rectangle<float> area);

    EyeScope& scope;
    EyeAnalyzer analyzer;
    juce::Image image;
    std::vector<float> pulledSamples;
    std::vector<double> pulledIndices;

    juce::Slider scaleSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox persistenceBox, viewBox;
    juce::TextButton clearButton { "Clear" }, relockButton { "Re-lock" };
    juce::ToggleButton thruButton { "Pass-through" }, trainerButton { "Feed FFE trainer" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> thruAttachment, trainerAttachment;
    juce::Label scaleLabel { {}, "Vertical scale" }, persistenceLabel { {}, "Persistence" }, viewLabel { {}, "View" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> scaleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> persistenceAttachment, viewAttachment;
    int lastView = -1;

    juce::Rectangle<int> eyeArea, readoutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EyeScopeEditor)
};
