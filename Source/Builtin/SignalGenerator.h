#pragma once

#include "BuiltInProcessor.h"
#include "Prbs.h"

// Differential NRZ / PAM4 pattern source. R carries +v, L carries -v.
// Ignores its input.
//
// The waveform is a sum of raised-cosine steps, one per symbol change, evaluated
// analytically, so any (fractional) samples-per-UI works and each edge can be moved
// in time independently. That is how jitter is applied:
//   RJ  - random: Gaussian, per edge, in UI rms
//   SJ  - deterministic: sinusoidal, in UI peak-to-peak, at a frequency given in
//         real-world MHz (1 MHz -> 1 Hz on the audio side)
//   DCD - deterministic: duty-cycle distortion, rising edges late and falling early
//         by half of the setting each (UI peak-to-peak)
class SignalGenerator : public BuiltInProcessor
{
public:
    static constexpr const char* id = "lab.generator";

    // As a plugin in a DAW there is no host strip to set the line rate and modulation, so the generator
    // (the transmitter, where they belong) carries them and writes them to the shared link. Inside the
    // DAW_LAB host the strip does that and the generator has no such parameters.
    explicit SignalGenerator (std::shared_ptr<LinkSettings> linkSettings, bool drivesLinkSettings = false);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    // Total edge displacement never exceeds this, which keeps edges in order.
    static constexpr double maxEdgeShiftUI = 0.45;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (bool withLink);
    void pushLinkSettings();
    float nextSymbolLevel (bool pam4);
    void scheduleNextEdge (double sjPeakToPeakUI, double sjFrequencyHz, double rjUI, double dcdUI);
    float sampleWaveform (double uiTime, float rise);

    struct Edge { double start; float delta; };

    const bool drivesLink;
    float pushedRate = -1.0f;
    int pushedModulation = -1;

    PrbsGenerator prbs;
    juce::Random random { 12345 };
    double sampleRate = 48000.0;
    double sampleCount = 0.0;

    double uiTime = 0.0;             // absolute position, in UI
    long long edgeIndex = 0;         // index of the edge being waited for
    double nextEdgeTime = 0.0;
    float lastLevel = 0.0f;          // level before the pending edge
    float pendingLevel = 0.0f;       // level after it
    float baseLevel = 0.0f;          // level once every finished edge is folded in
    std::array<Edge, 4> active {};
    int numActive = 0;
    bool started = false;
};
