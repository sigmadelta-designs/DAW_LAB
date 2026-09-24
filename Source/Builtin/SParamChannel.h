#pragma once

#include "BuiltInProcessor.h"
#include "SParamSynthesis.h"

// A channel defined by a Touchstone file (measured with a VNA, or exported from a field solver).
//   * 2-port (.s2p): one trace; the same transfer (S21) is applied to both legs.
//   * 4-port (.s4p) and up: two coupled traces. Choose which ports are the + and - inputs and the
//     + and - outputs; all four ways through (including the crosstalk terms) are applied, so skew,
//     loss imbalance, mode conversion and crosstalk all come from the data.
// The data is embedded in the stage's state, so a chain preset carries its channel with it.
// The file is the channel between matched 50 ohm ports: transmitter and receiver reflections
// are not part of it (use the Trace Channel for those).
class SParamChannel : public BuiltInProcessor,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private juce::AsyncUpdater
{
public:
    static constexpr const char* id = "lab.sparam";

    explicit SParamChannel (std::shared_ptr<LinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- message thread
    bool loadFile (const juce::File& file, juce::String& error);
    bool loadText (const juce::String& text, const juce::String& name, int portsHint, juce::String& error);
    void clearData();

    bool hasData() const noexcept { return data.numFrequencies() > 1; }
    const TouchstoneData& getData() const noexcept { return data; }
    const juce::String& getFileName() const noexcept { return fileName; }
    const SParamSynthesis::FirSet& getFirs() const noexcept { return firs; }
    double getSampleRate() const noexcept { return sampleRate; }
    SParamSynthesis::PortMap getPortMap() const;
    juce::ChangeBroadcaster changed;

    // What the data amounts to at the link's Nyquist frequency (mixed-mode, unitary normalisation).
    struct Summary
    {
        bool valid = false;
        double nyquistHz = 0.0;                       // real-world
        double sdd21Db = 0, scc21Db = 0, sdc21Db = 0; // differential, common, diff-to-common
        double delayPlusPs = 0, delayMinusPs = 0;     // bulk delay of each through path
        double firstHz = 0, lastHz = 0;
        int points = 0, ports = 0;
        juce::String warning;
    };
    Summary getSummary() const;
    void rebuild();                                    // redesign the filters (automatic on load / setting changes)

    static constexpr int lossTableSize = 64;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { rebuild(); }

    TouchstoneData data;
    juce::String fileName, embeddedText;
    SParamSynthesis::FirSet firs;
    double sampleRate = 48000.0;

    // what the audio thread runs; copied over under a spin lock when a new design is ready
    struct Design
    {
        int taps = 0;
        bool present[2][2] {};
        bool valid = false;
        float lossDb[lossTableSize] {};                 // insertion loss across 0 .. Nyquist(sample rate)
        alignas(16) float reversed[2][2][SParamSynthesis::maxTaps] {};   // newest-sample-last order, ready for a dot product
    };
    juce::SpinLock lock;
    Design published, running;
    std::atomic<int> version { 0 };
    int appliedVersion = -1;

    static constexpr int ring = SParamSynthesis::maxTaps;
    std::vector<float> history[2];
    int position = 0;
};
