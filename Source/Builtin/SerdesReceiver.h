#pragma once

#include "BuiltInProcessor.h"
#include "Ctle.h"

// A receiver: CTLE -> gain control -> clock recovery -> decision feedback equaliser -> slicer.
//
//   in (R - L) -> CTLE (peaking filter) -> AGC -> [ sampled at the recovered clock ] -> slicer -> bits
//                                                        ^                 |
//                                     CDR (Alexander phase detector,       DFE: subtracts h1..h5 x the last
//                                     proportional-integral loop)          decisions before the slicer
//
// * CTLE: zero + two poles, boost set in dB; can adapt its boost against the slicer error.
// * AGC: brings the signal to a fixed target level so slicer levels are fixed (+-T; PAM4 +-T, +-T/3).
// * CDR: an NCO produces the sampling instants (a data sample, and an edge sample half a UI earlier).
//   A bang-bang Alexander detector compares the edge sample with the neighbouring decisions and nudges the
//   clock early or late; an integral term tracks frequency offset. Works on the sign bit, so it does PAM4 too.
// * DFE: five post-cursor taps, sign-sign LMS adapted against the slicer error.
// * Slicer: NRZ or PAM4 (Gray), the recovered bits are logged for comparison with the transmitted ones.
//
// Output (choose): the CTLE/AGC analog signal, the slicer input (analog with the DFE feedback applied, so a scope
// shows the post-DFE eye), or the regenerated data (retimed, clean edges at the recovered clock).
class SerdesReceiver : public BuiltInProcessor,
                       private juce::Timer
{
public:
    static constexpr const char* id = "lab.rx";
    static constexpr int dfeTaps = 5;
    enum Output { outputCtle, outputSlicerInput, outputRecovered };

    explicit SerdesReceiver (std::shared_ptr<LinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    // What the receiver is doing, for the editor and for tests.
    struct Status
    {
        std::atomic<bool> locked { false };
        std::atomic<float> frequencyPpm { 0.0f };       // what the CDR's integral path has learned
        std::atomic<float> nmse { 1.0f };               // slicer error, normalised to the target level (last 512 symbols)
        std::atomic<float> agcGain { 1.0f };
        std::atomic<float> edgeLevel { 1.0f };          // mean |edge sample| / T at transitions: small when the clock is on the edges
        std::atomic<float> dfe[dfeTaps];
        std::atomic<uint32_t> window { 0 };             // finished 512-symbol windows
        std::atomic<uint64_t> symbols { 0 };
        std::atomic<int> ctleState { 0 };               // 0 off, 1 starting, 2 adapting, 3 converged
        Status() { for (auto& d : dfe) d = 0.0f; }
    } status;

    double getSampleRateForDisplay() const noexcept { return sampleRate; }
    Ctle::Settings getCtleSettings() const;
    double getCtleResponseDb (double audioHz) const;
    void restartCtleTraining() { ctleRestart = true; }
    void serviceForTesting() { timerCallback(); }   // what the message-thread timer does (DFE tap persistence, CTLE training)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void handleEvent();
    double interpolate (double time) const noexcept;
    void ctleTrainerTick();

    double sampleRate = 48000.0;

    // --- audio thread state
    Ctle ctle;
    Ctle::Settings appliedCtle;
    bool ctleConfigured = false;
    std::vector<float> history;
    int64_t latest = -1;                     // index of the newest input sample
    double nextEvent = 0.0;
    double unitInterval = 4.8;               // samples, nominal
    double agc = 1.0, integral = 0.0;
    double taps[dfeTaps] {};
    bool adapting = false;
    double decisions[dfeTaps] {};            // most recent first, in level units (+-1, PAM4 also +-1/3)
    double feedbackNext = 0.0, heldFeedback = 0.0;
    struct Switch { double time, value; };
    Switch switches[8] {};
    int switchCount = 0;
    int previousSign = 1;
    bool havePrevious = false;
    double edgeAverage = 1.0, balanceAverage = 0.0;
    int transitionsSeen = 0;
    double errorSum = 0.0;
    int errorCount = 0;
    double regenBase = 0.0, regenLast = 0.0;
    struct Edge { double start, delta; };
    Edge edges[6] {};
    int edgeCount = 0;
    // per-block settings
    double target = 0.3, kp = 0.003, ki = 3.0e-5, step = 0.001, clockOffset = 0.0;
    bool pam4 = false;

    // --- message thread: DFE taps are kept in the parameters, and the CTLE can train itself
    std::atomic<bool> ctleRestart { false };
    float trialBoost = 0.0f, bestBoost = 0.0f, bestNmse = 1.0f, ctleStep = 3.0f;
    int ctleDirection = 1;
    bool ctleTriedOpposite = false;
    uint32_t ctleWaitUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerdesReceiver)
};
