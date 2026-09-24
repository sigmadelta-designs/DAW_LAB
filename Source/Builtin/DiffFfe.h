#pragma once

#include "BuiltInProcessor.h"

// Symbol-spaced feed-forward equaliser, 3 pre-cursor + main + 3 post-cursor taps.
//
//   y[m] = c(-3) x[m+3] + c(-2) x[m+2] + c(-1) x[m+1] + c(0) x[m]
//        + c(+1) x[m-1] + c(+2) x[m-2] + c(+3) x[m-3]
//
// where the spacing is one unit interval of the link (fractional-sample delays,
// cubic interpolated). Pre-cursor taps look at the *upcoming* symbol, post-cursor
// taps at the *previous* ones. Applied identically to both legs, so the pair
// stays differential.
//
// Auto-adapt ("link training"): with the `adapt` switch on, the FFE nudges its own taps
// and keeps a change only when the signal quality measured *downstream* (the Eye Scope's
// decision-error probe) improves. Like a real TX FFE trained through a back-channel, it
// needs no knowledge of the channel, and it works wherever the FFE sits in the chain,
// as long as an Eye Scope follows it.
class DiffFfe : public BuiltInProcessor,
                private juce::Timer
{
public:
    static constexpr const char* id = "lab.ffe";
    static constexpr int numTaps = 7;
    static constexpr int mainTap = 3;

    explicit DiffFfe (std::shared_ptr<LinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    static const char* getTapParamId (int tap);
    static juce::String getTapName (int tap);   // "pre3" ... "main" ... "post3"

    using Taps = std::array<float, numTaps>;

    // Turns the slider values into the taps actually applied.
    //  txMode:  main tap is forced to 1 - sum|others| (a transmitter's constant-swing FFE),
    //           otherwise the main slider is used as-is (a receiver-style FFE).
    //  reverse: mirror the taps about the main tap, i.e. pre <-> post.
    static Taps computeTaps (const Taps& sliders, bool txMode, bool reverse);

    // Gain (dB) at DC and at Nyquist for a set of taps.
    static float gainDbAtDc (const Taps&);
    static float gainDbAtNyquist (const Taps&);

    Taps getSliderValues() const;
    Taps getEffectiveTaps() const;

    // Trainer. Driven by an internal message-thread timer; tests may call trainerTick() directly.
    enum TrainerState { trainerOff, trainerStarting, trainerAdapting, trainerConverged };
    void trainerTick();
    void restartTraining() { restartRequested = true; }

    struct TrainerStatus
    {
        std::atomic<int> state { trainerOff };
        std::atomic<float> step { 0.0f }, nmse { 1.0f };
        std::atomic<int> evaluations { 0 };
        std::atomic<bool> probeAlive { false };
    } trainerStatus;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float readDelayed (int channel, double delay) const;

    void timerCallback() override { trainerTick(); }
    void setSlider (int tap, float value);
    void beginTap();
    void applyTrial();
    void advanceTap();

    static constexpr int ringSize = 1 << 15;

    double sampleRate = 48000.0;
    std::vector<float> ring[2];
    int writeIndex = 0;
    std::array<juce::SmoothedValue<float>, numTaps> smoothedTaps;

    // trainer state (message thread only)
    std::atomic<bool> restartRequested { false };
    Taps trainerBase {};
    float trainerStep = 0.06f, best = 1.0f, trialValue = 0.0f;
    float smoothedNmse = 1.0f, convergedNmse = 1.0f;
    int orderPosition = 0, direction = -1;
    bool triedOpposite = false, movedThisTap = false, sweepImproved = false;
    uint32_t waitUntilWindow = 0, lastWindowSeen = 0;
    int ticksWithoutWindow = 0;
};
