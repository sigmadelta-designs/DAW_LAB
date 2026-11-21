#pragma once

#include "BuiltInProcessor.h"
#include "PllLinkSettings.h"

// A standalone frequency-synthesis PLL: reference clock -> phase/frequency detector -> charge-pump-style
// loop filter -> VCO -> feedback divider (/N) back to the detector. This is a different loop from the
// Alexander bang-bang clock-recovery CDR buried in SerdesReceiver (which tracks an already-present data
// stream's edges); this one generates a clean multiplied clock from its own reference, which is what "PLL
// validation" means on a bench (lock time, lock/capture range, loop bandwidth & damping, reference spurs,
// holdover).
//
//   ref ---> [2-flip-flop PFD] ---> charge pump (+-Icp pulses) ---> loop filter (R1+C1, PI) ---> VCO ---> out
//              ^ qa, qb edge latches                                                              |
//              |                                                                                   v
//              +------------------------------------------- /N divider <--------------------------+
//
// The PFD is modelled as the real circuit (two edge-triggered latches + an AND-gate reset), not a plain
// linear phase-error subtraction - this is what gives it genuine, not-assumed frequency-acquisition
// behaviour (a real tri-state PFD's average output is linear in phase error only within +-2pi; outside
// that it pumps one-sided, which is exactly how a real PFD pulls in frequency, not just phase). The loop
// filter's R1/C1 pair is folded into two direct gains, "loopkp" (the R1/proportional path - fires only in
// the same instants the charge pump does, since R1 sits in series with the same current) and "loopki" (the
// C1/integral path - the loop's frequency memory), the same simplification-and-disclosure the SerDes CDR
// already makes (its own kp/ki are not a literal transconductance/capacitance either - see SerdesReceiver.h).
//
// Channels (own small bus, BuiltInProcessorBase<PllLinkSettings>, numChannels below): not a literal
// differential pair like the SerDes/DDR buses - a PLL's clocks are normally single-ended internal nodes,
// so each gets one channel, exposed as real +-1 square waves so a future scope could probe any of them.
class PllCore : public BuiltInProcessorBase<PllLinkSettings>
{
public:
    static constexpr const char* id = "lab.pllcore";

    // channel layout
    static constexpr int refCh = 0;     // reference clock, as received (after injected jitter)
    static constexpr int vcoCh = 1;     // VCO / synthesized output clock = N x locked reference
    static constexpr int fbCh = 2;      // feedback clock (VCO / N) - should match refCh once locked
    static constexpr int ctrlCh = 3;    // loop filter control signal (integral + proportional), analog
    static constexpr int numChannels = 4;

    explicit PllCore (std::shared_ptr<PllLinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    struct Status
    {
        std::atomic<bool> locked { false };
        std::atomic<float> phaseErrorRad { 0.0f };    // ref vs. feedback, wrapped to +-pi, for display/lock-detect only
        std::atomic<float> vcoFreqHz { 0.0f };         // instantaneous VCO frequency, in the same "sim Hz" the params use
        std::atomic<float> controlSignal { 0.0f };     // integral + proportional, same units as processBlock uses internally
        std::atomic<uint64_t> refCycles { 0 };
    } status;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    double sampleRate = 48000.0;

    double refPhase = 0.0, vcoPhase = 0.0, fbPhase = 0.0;   // 0..1 cycles
    bool qa = false, qb = false;                             // PFD's two edge latches
    double integral = 0.0;                                   // loop filter's C1/integral path state
    double freqOffset = 0.0;                                 // applied to the VCO's free-run frequency, updated once per sample

    // phase-error lock detector: locked once |phaseError| has stayed under lockThreshold for lockWindow
    // consecutive reference cycles in a row; dropped the moment it exceeds unlockThreshold once.
    static constexpr double lockThresholdRad = 0.1, unlockThresholdRad = 0.5;
    static constexpr int lockWindowCycles = 64;
    int cyclesInLock = 0;

    // reference jitter injection (SIM): a simple per-sample Gaussian phase perturbation, in UI (ref cycles)
    // rms - same unit convention as the SerDes generator's "rj" param, simpler mechanism (no edge
    // scheduler) since a PLL's reference has no data pattern to carry.
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PllCore)
};
