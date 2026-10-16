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

    // The step between adjacent register codes over [lo, hi] - pass this as a NormalisableRange's
    // interval so the parameter (and every Slider attached to it) only ever lands on a legal code,
    // instead of just quantizing the value after the fact at the point of use.
    inline float stepFor (float lo, float hi, int bits) noexcept
    {
        return (hi - lo) / (float) ((1 << bits) - 1);
    }

    // Snaps to the same grid a NormalisableRange{lo, hi, stepFor(lo, hi, bits)} would. This has to
    // match juce::NormalisableRange<float>::snapToLegalValue()'s formula exactly (not just produce an
    // equivalent result) - right at a tie, e.g. quantizing 0 on a symmetric range, floating-point
    // rounding can break a "same maths, different order of operations" version the other way from
    // the Slider showing it, leaving the UI a code off from what's actually applied.
    inline float quantize (float value, float lo, float hi, int bits) noexcept
    {
        if (bits <= 0)
            return value;

        const float interval = stepFor (lo, hi, bits);
        const float v = lo + interval * std::floor ((value - lo) / interval + 0.5f);
        return (v <= lo || hi <= lo) ? lo : (v >= hi ? hi : v);
    }
}
