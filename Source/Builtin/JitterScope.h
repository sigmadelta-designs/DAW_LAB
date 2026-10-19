#pragma once

#include "BuiltInProcessor.h"

// Pass-through probe, the jitter/phase-noise counterpart to the Eye Scope. Watches the differential
// signal (or one leg/common-mode, same View choice as the Eye Scope) and hands it to the jitter editor
// through a lock-free FIFO; all the actual timing analysis happens there (JitterAnalyzer), UI-thread only,
// same split as EyeScope/EyeAnalyzer.
class JitterScope : public BuiltInProcessor
{
public:
    static constexpr const char* id = "lab.jitter";

    explicit JitterScope (std::shared_ptr<LinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    double getSampleRate() const noexcept { return sampleRate.load(); }

    enum View { viewDifferential, viewCommon, viewPlusLeg, viewMinusLeg };

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
};
