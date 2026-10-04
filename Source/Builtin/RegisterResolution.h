#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// Real transmit/receive equalizer taps and CTLE boost are register fields driving a DAC: a fixed number
// of discrete codes over a fixed range, not a continuous value. quantize() snaps a value to the nearest
// of 2^bits evenly spaced codes between lo and hi, the way a real PHY's control actually behaves.
//
// Bit depths are fixed per control, not user-selectable - they're a property of the (simulated) silicon,
// not a setting to dial. Values are representative of real SerDes PHYs rather than any one spec:
//   - TX FFE / RX DFE taps: commonly ~6 bit per tap (PCIe's transmitter equalization is specified as
//     integer counts against a reported "Full Swing" value, typically in a similar range).
//   - CTLE boost: usually coarser, ~5 bit - it's a broad analog shape, so fine steps buy little.
namespace RegisterResolution
{
    constexpr int ffeTapBits = 6;    // 64 codes
    constexpr int dfeTapBits = 6;    // 64 codes
    constexpr int ctleBoostBits = 5; // 32 codes

    inline float quantize (float value, float lo, float hi, int bits) noexcept
    {
        if (bits <= 0)
            return value;

        const int levels = (1 << bits) - 1;
        const float t = juce::jlimit (0.0f, 1.0f, (value - lo) / (hi - lo));
        return lo + std::round (t * (float) levels) / (float) levels * (hi - lo);
    }
}
