#pragma once

#include "BuiltInProcessor.h"

// A lossy backplane / cable: two cascaded one-pole low-passes whose corner is
// set so the channel loses a chosen number of dB at the Nyquist frequency of
// the link (half the line rate). Both legs get the same response.
class LossyChannel : public BuiltInProcessor
{
public:
    static constexpr const char* id = "lab.channel";

    explicit LossyChannel (std::shared_ptr<LinkSettings> linkSettings);
    ~LossyChannel() override;

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Corner frequency (Hz) of each of the two poles for the requested loss.
    static double cornerForLoss (double lossDb, double nyquistHz);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    double sampleRate = 48000.0;
    float state[2][2] {};   // [channel][pole]
};
