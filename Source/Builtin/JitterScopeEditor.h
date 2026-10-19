#pragma once

#include "JitterAnalyzer.h"
#include "JitterScope.h"
#include "DawLabLookAndFeel.h"

class JitterScopeEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit JitterScopeEditor (JitterScope& scopeToEdit);
    ~JitterScopeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawStrip (juce::Graphics&, juce::Rectangle<float> area);
    void drawHistogram (juce::Graphics&, juce::Rectangle<float> area);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> area);

    JitterScope& scope;
    JitterAnalyzer analyzer;
    std::vector<float> pulledSamples;
    std::vector<double> pulledIndices;

    juce::ComboBox viewBox;
    juce::Label viewLabel { {}, "View" };
    juce::ToggleButton thruButton { "Pass-through" };
    juce::TextButton relockButton { "Re-lock" }, resetButton { "Reset stats" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> thruAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> viewAttachment;
    int lastView = -1;

    juce::Rectangle<int> stripArea, histArea, spectrumArea, readoutArea;
    // auto-ranged y-scale for the strip chart / histogram, in ps - grows to fit, never shrinks below
    // a sane floor, so a quiet link isn't shown pinned against the full scale of a noisy one.
    float rangePs = 20.0f;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JitterScopeEditor)
};
