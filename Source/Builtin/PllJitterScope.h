#pragma once

#include "BuiltInProcessor.h"
#include "PllCore.h"
#include "PllLinkSettings.h"

// The PLL domain's counterpart to the SerDes/DDR jitter scopes - reuses the same generic JitterAnalyzer
// unchanged (a stream of samples, a unit-interval length, nothing else), but its own stage rather than
// stretching JitterScope (tied to LinkSettings) to cover a third domain, the same call DdrEyeScope made
// for DDR. Sits downstream of PllCore on its 4-channel bus and passes the whole bus through unchanged
// (Pass-through off terminates it), picking one of the three clock nodes to actually analyse: a PLL's
// "reference spurs" and "jitter peaking" bench measurements are exactly what this gives you once pointed
// at the VCO output, the same way DdrEyeScope let an existing analyzer answer a DDR-shaped question
// unchanged.
class PllJitterScope : public BuiltInProcessorBase<PllLinkSettings>
{
public:
    static constexpr const char* id = "lab.plljitter";

    explicit PllJitterScope (std::shared_ptr<PllLinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    double getSampleRate() const noexcept { return sampleRate.load(); }

    enum Probe { probeRef = 0, probeVco = 1, probeFb = 2 };

    void setCaptureActive (bool shouldCapture);
    int readCaptured (std::vector<float>& samples, std::vector<double>& indices);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int fifoSize = 1 << 17;

    std::vector<float> displayed;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> capturing { false };
    double sampleCounter = 0.0;

    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoSamples = std::vector<float> (fifoSize);
    std::vector<double> fifoIndices = std::vector<double> (fifoSize);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PllJitterScope)
};
