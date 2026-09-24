#pragma once

#include "BuiltInProcessor.h"
#include "TraceBoard.h"
#include "AggressorSource.h"
#include "TraceReflections.h"

// Two drawn PCB traces between the differential inputs and outputs.
//   R (+) passes through the + trace, L (-) through the - trace, each with its own
//   delay and its own loss, both set by the drawn length:
//     delay = length x propagation delay (ps/inch, scaled 1e6 like everything else)
//     loss  = length x (0.25 sqrt(f) + 0.12 f) dB/inch, f = Nyquist in GHz (skin + dielectric, FR4-like)
//   Equal lengths mean no skew; a length difference is skew (and an amplitude imbalance).
//
// Coupling: where the traces run close and parallel they couple, which is modelled the way
// coupled lines behave: as two modes with different propagation. The odd mode (differential)
// travels at the base speed, the even mode (common) is slower by an amount that grows with
// how far and how close the traces run together. In (R, L) terms that is crosstalk:
// drive one trace and a copy of the odd/even difference appears on the other (FEXT), and a
// skewed or unequal pair converts some differential signal into common-mode.
//
// Reflections: each trace is a lattice (see TraceReflections.h) whose impedance profile comes from the
// drawing (proximity to the other trace, corners, connector ends), closed with a source and a load
// resistance. Matched, it is the plain delay-and-loss line; mismatched, echoes appear at the round-trip
// delay of every discontinuity, with the loss of that longer path.
//
// The board (obstacles, traces, game state) is drawn in the editor and stored with the stage.
class TraceChannel : public BuiltInProcessor,
                     private juce::AudioProcessorValueTreeState::Listener,
                     private juce::AsyncUpdater
{
public:
    static constexpr const char* id = "lab.trace";

    explicit TraceChannel (std::shared_ptr<LinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- board access (message thread)
    TraceBoard& getBoard() noexcept { return board; }
    void boardEdited();                       // call after changing the board
    juce::ChangeBroadcaster boardChanged;

    // --- what the traces amount to, for the current settings
    double getLengthInches (int trace) const noexcept { return lengthInches[trace].load(); }
    double getVelocityPsPerInch() const { return getParam ("velocity"); }
    double getDelayPs (int trace) const { return getLengthInches (trace) * getVelocityPsPerInch(); }
    double getSkewPs() const { return getDelayPs (TraceBoard::plus) - getDelayPs (TraceBoard::minus); }
    double getLossDb (int trace) const { return lossDb (getLengthInches (trace), link->lineRateGBd.load(), getParam ("lossscale")); }

    static double lossDb (double lengthInches, double lineRateGBd, double scale) noexcept;
    static constexpr double lineImpedance = TraceReflections::nominalImpedance;
    double getUnitIntervalPs() const noexcept { return 1000.0 / juce::jmax (0.01, (double) link->lineRateGBd.load()); }

    // Even-mode delay beyond the odd mode: 2% of the coupled (spacing-weighted) delay, times the coupling setting.
    static constexpr double evenModeFraction = 0.02;
    double getEvenModeExtraDelayPs() const
    {
        return evenModeFraction * getParam ("coupling") * weightedCouplingInches.load() * getVelocityPsPerInch();
    }

    // What a network analyser would report at the link's Nyquist frequency (mixed-mode S-parameters,
    // unitary normalisation), computed from the model. Message thread.
    struct ModeReport
    {
        double oddDelayPs = 0, evenDelayPs = 0;         // mean delay of each mode
        double sdd21Db = 0, scc21Db = 0;                // differential / common insertion loss
        double sdc21Db = 0;                             // differential-to-common conversion
        double fextPercent = 0;                         // drive one trace: peak leaking onto the other
        double coupledInches = 0, weightedInches = 0;
    };
    ModeReport getModeReport() const;

    // Crosstalk from the board's aggressor lanes. Per lane, each trace picks up
    //   amplitude = 0.03 x setting x (spacing-weighted parallel inches)   (FS rms)
    // of that lane's unit-RMS coupled signal. The differential probe (R - L) sees the difference of the
    // two traces' pickup, the common-mode probe ((R + L) / 2) sees half the sum.
    static constexpr double pickupPerInch = 0.03;
    struct LaneReport { double plusInches = 0, minusInches = 0; };
    struct AggressorReport
    {
        std::vector<LaneReport> lanes;
        double differentialRms = 0, commonRms = 0;     // FS
        double sxrDb = 0;                              // 0.5 FS differential signal over differential pickup; large when quiet
    };
    AggressorReport getAggressorReport() const;

    // Reflection side, message thread. Cheap enough to call from a paint, but callers should cache
    // on getProfileVersion().
    struct ReflectionReport
    {
        std::vector<float> impedance[2];                       // per section, per leg
        double inchesPerSection[2] { 0.0, 0.0 };
        double returnLossDb[2] { -99.0, -99.0 };               // S11 at the link's Nyquist frequency
        std::vector<TraceReflections::Discontinuity> worst[2];
        bool active[2] { false, false };                       // lattice in use (else plain delay-and-loss)
        double sourceR = 50.0, loadR = 50.0;
    };
    ReflectionReport getReflectionReport() const;
    int getProfileVersion() const noexcept { return profileVersion.load(); }
    void rebuildProfile();     // normally automatic; public for tests and offline use

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& parameterID, float) override;
    void handleAsyncUpdate() override { rebuildProfile(); }

    TraceBoard board;
    std::atomic<double> lengthInches[2];
    std::atomic<double> weightedCouplingInches { 0.0 };

    static constexpr int ringSize = 1 << 14;

    double sampleRate = 48000.0;
    std::vector<float> ring[2], oddRing, evenRing;
    int writeIndex = 0;
    juce::SmoothedValue<double> delaySamples[2];
    juce::SmoothedValue<double> evenDelay;
    float poleState[2][2] {};

    // aggressor lanes: geometry is published from the message thread, the audio thread follows the version
    std::atomic<int> laneVersion { 0 };
    std::atomic<int> numLanes { 0 };
    std::atomic<double> laneInches[TraceBoard::maxAggressors][2];
    std::atomic<double> laneRatio[TraceBoard::maxAggressors];
    std::atomic<int> laneSeed[TraceBoard::maxAggressors];
    AggressorSource laneSource[TraceBoard::maxAggressors];
    int appliedVersion = -1;
    double appliedUnitInterval = 0.0;

    // reflection lattice: the profile is built on the message thread and copied to the audio thread when it changes
    struct LatticeProfile
    {
        int sections[2] { 0, 0 };
        double fraction[2] { 0.0, 0.0 };
        float impedance[2][ReflectionLine::maxSections] {};
    };
    juce::SpinLock profileLock;
    LatticeProfile published, active;
    std::atomic<int> profileVersion { 0 };
    int appliedProfile = -1;
    ReflectionLine line[2];
    std::vector<float> latticeRing[2];
    bool wasLattice[2] { false, false };
    float appliedSourceR = -1.0f, appliedLoadR = -1.0f;
};
