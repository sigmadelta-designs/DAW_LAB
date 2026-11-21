#pragma once

#include "JitterAnalyzer.h"
#include "PllJitterScope.h"
#include "DawLabLookAndFeel.h"

// The PLL domain's counterpart to JitterScopeEditor - same strip chart / histogram / spectrum layout and
// the same JitterAnalyzer underneath, adapted for a Probe choice (which of the PLL's three clock nodes)
// instead of a differential View choice, and reading the expected clock rate for whichever node is
// selected from PllLinkSettings (published by PllCore - see its header) instead of LinkSettings::baudHz().
class PllJitterScopeEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit PllJitterScopeEditor (PllJitterScope& scopeToEdit);
    ~PllJitterScopeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawStrip (juce::Graphics&, juce::Rectangle<float> area);
    void drawHistogram (juce::Graphics&, juce::Rectangle<float> area);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> area);
    double expectedFreqHzForProbe (int probe) const;

    PllJitterScope& scope;
    JitterAnalyzer analyzer;
    std::vector<float> pulledSamples;
    std::vector<double> pulledIndices;

    juce::ComboBox probeBox;
    juce::Label probeLabel { {}, "Probe" };
    juce::ToggleButton thruButton { "Pass-through" };
    juce::TextButton relockButton { "Re-lock" }, resetButton { "Reset stats" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> thruAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> probeAttachment;
    int lastProbe = -1;

    juce::Rectangle<int> stripArea, histArea, spectrumArea, readoutArea;
    float rangePs = 20.0f;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PllJitterScopeEditor)
};
