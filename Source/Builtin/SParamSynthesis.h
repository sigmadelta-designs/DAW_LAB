#pragma once

#include "Touchstone.h"

// Turning measured (or simulated) S-parameters into something the audio path can run:
// four FIR filters, one for each way a signal can get from an input trace to an output trace.
//
//   out+ = h[0][0] * in+ + h[0][1] * in-
//   out- = h[1][0] * in+ + h[1][1] * in-
//
// Frequencies in the file are real-world; the 1e6 simulation scale maps 1 GHz to 1 kHz.
//
// Method: find the bulk delay of the through paths, remove it, interpolate the smooth remainder on
// the FIR's frequency grid (DC extrapolated from the lowest point, tapered to zero above the highest),
// put the delay back with a small design margin, and inverse-transform (a direct DFT, so no FFT
// library is needed). The S-parameters describe the channel between matched 50 ohm ports, so
// reflections from the transmitter and receiver are not part of this model.
namespace SParamSynthesis
{
    // Which ports are the two inputs (+, -) and the two outputs (+, -). Zero-based.
    struct PortMap
    {
        int in[2] { 0, 1 };
        int out[2] { 2, 3 };
    };

    constexpr int maxTaps = 2048;
    constexpr int designMargin = 24;   // samples of latency added so the pre-cursor part of the response fits

    struct FirSet
    {
        int taps = 0;
        bool present[2][2] {};
        std::vector<float> h[2][2];    // chronological: h[0] is the first tap
        double delaySamples = 0.0;     // bulk delay found in the data, not counting the design margin
        double delayReal = 0.0;        // seconds, real-world
        juce::String warning;
    };

    // Two-port files describe one trace: the same channel is applied to both legs.
    FirSet design (const TouchstoneData& data, const PortMap& map, double sampleRate, int taps);

    // The through-path transfer T[i][j](f) the design is built from, at a real-world frequency.
    TouchstoneData::Complex transfer (const TouchstoneData& data, const PortMap& map, int i, int j, double hz);

    // Mean group delay of a path (seconds), the bulk delay used by the design.
    double bulkDelay (const TouchstoneData& data, int row, int column);
}
