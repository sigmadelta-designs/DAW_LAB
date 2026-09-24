#pragma once

#include <juce_core/juce_core.h>
#include <complex>
#include <vector>

// Reflections on a drawn trace: a Kelly-Lochbaum lattice. The trace is cut into sections that
// each delay the signal by one sample in each direction; every junction between sections has
// a reflection coefficient rho = (Z2 - Z1) / (Z2 + Z1) taken from the impedance profile, and
// the two ends are closed with the source and load resistances. Reflections then come out
// on their own, including the multiple bounces between a source and a load that don't match.
//
// Loss: two one-pole low-passes (the same two poles the plain trace model uses) sit inside the
// line at 1/3 and 2/3 of its length, in both directions. The direct signal passes both once,
// exactly as before; an echo from further along the trace passes them again on the way back,
// so distant reflections are attenuated more, which is what a lossy line does.
class ReflectionLine
{
public:
    static constexpr int maxSections = 1024;

    void reset();

    // Audio thread safe (no allocation). `impedance` has `sections` entries.
    void configure (const float* impedance, int sections, float sourceR, float loadR);
    // one-pole TPT gain (g / (1 + g)); a negative value means "no loss"
    void setLossGain (float gain) noexcept { lossGain = gain; }

    int getSections() const noexcept { return sections; }
    float process (float input) noexcept;

private:
    int sections = 0;
    float rho[maxSections] {};
    float rightA[maxSections] {}, leftA[maxSections] {}, rightB[maxSections] {}, leftB[maxSections] {};
    float* right = rightA; float* left = leftA; float* newRight = rightB; float* newLeft = leftB;
    float gammaSource = 0.0f, gammaLoad = 0.0f, launch = 1.0f, lossGain = -1.0f;
    int poleAt[2] { 0, 0 };
    float poleRight[2] {}, poleLeft[2] {};
};

namespace TraceReflections
{
    constexpr double nominalImpedance = 50.0;   // per leg (100 ohm differential)

    struct Tdr
    {
        std::vector<double> distanceInches, impedance;   // what a TDR would show looking into the source end
    };

    // Step response at the source end of a lossless copy of the line, converted to impedance vs distance.
    Tdr computeTdr (const std::vector<float>& impedance, double sourceR, double loadR, double inchesPerSection);

    struct Discontinuity { double distanceInches = 0.0, rho = 0.0; };
    // The largest reflection-producing steps along the profile, biggest first.
    std::vector<Discontinuity> largestDiscontinuities (const std::vector<float>& impedance, double sourceR, double loadR,
                                                       double inchesPerSection, int count);

    // Input reflection (S11, dB) looking into the source end at audio frequency `hz`, from the profile,
    // the load, and a flat loss of `lossDbTotal` dB over the whole line at that frequency.
    double returnLossDb (const std::vector<float>& impedance, double loadR, double referenceR,
                         double hz, double sampleRate, double lossDbTotal);
}
