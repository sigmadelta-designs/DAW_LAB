#pragma once

#include "BuiltInProcessor.h"
#include "SymbolProbe.h"

// Pass-through probe. Watches the differential signal (R - L) and hands it to
// the eye-diagram editor through a lock-free FIFO.
class EyeScope : public BuiltInProcessor
{
public:
    static constexpr const char* id = "lab.eyescope";

    explicit EyeScope (std::shared_ptr<LinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    double getSampleRate() const noexcept { return sampleRate.load(); }

    enum View { viewDifferential, viewCommon, viewPlusLeg, viewMinusLeg };
    static const char* getViewName (int view);

    // Odd-mode (differential, R - L) and even-mode (common, (R + L) / 2) measurements over the
    // last quarter second, whatever the view is set to.
    struct Measurements
    {
        std::atomic<float> oddRms { 0.0f }, evenRms { 0.0f }, oddPeakToPeak { 0.0f }, evenPeakToPeak { 0.0f };
        std::atomic<bool> valid { false };
    } measurements;

    // Editor side. Capture only runs while an editor is open.
    void setCaptureActive (bool shouldCapture);

    // Copies out everything captured since the last call. Returns the absolute
    // index of the first returned sample (indices of dropped data are skipped).
    int readCaptured (std::vector<float>& samples, std::vector<double>& indices);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int fifoSize = 1 << 17;

    SymbolProbe probe;
    std::vector<float> differential, displayed;
    double sumOdd = 0.0, sumEven = 0.0;
    float oddMin = 0.0f, oddMax = 0.0f, evenMin = 0.0f, evenMax = 0.0f;
    int measured = 0;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> capturing { false };
    double sampleCounter = 0.0;

    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoSamples = std::vector<float> (fifoSize);
    std::vector<double> fifoIndices = std::vector<double> (fifoSize);
};
