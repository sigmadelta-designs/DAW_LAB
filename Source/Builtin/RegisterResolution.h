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
//   - CTLE zero/second-pole: usually just a small bank of selectable RC corners, not a fine DAC - 4 bit.
//   - CTLE DC gain: same coarse analog-gain family as boost - 5 bit.
//   - AGC target level: the loop's own reference, usually driven by a somewhat finer DAC - 7 bit.
//   - CDR loop-filter gains (Kp/Ki): real bang-bang CDRs typically gear-shift between a handful of
//     coarse/fine loop-gain settings rather than a continuous gain - 4 bit.
//   - DFE adaptation step (mu): sign-sign LMS step sizes are commonly a small power-of-two "mu select"
//     field - approximated here as a coarse 4-bit linear register.
namespace RegisterResolution
{
    constexpr int ffeTapBits = 6;     // 64 codes
    constexpr int dfeTapBits = 6;     // 64 codes
    constexpr int ctleBoostBits = 5;  // 32 codes
    constexpr int ctleZeroBits = 4;   // 16 codes
    constexpr int ctlePole2Bits = 4;  // 16 codes
    constexpr int ctleDcGainBits = 5; // 32 codes
    constexpr int agcTargetBits = 7;  // 128 codes
    constexpr int cdrGainBits = 4;    // 16 codes
    constexpr int dfeStepBits = 4;    // 16 codes

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

    // The integer register code (0 .. 2^bits - 1) that `value` snaps to.
    inline int codeOf (float value, float lo, float hi, int bits) noexcept
    {
        if (bits <= 0)
            return 0;

        const float interval = stepFor (lo, hi, bits);
        const int levels = (1 << bits) - 1;
        return juce::jlimit (0, levels, (int) std::floor ((value - lo) / interval + 0.5f));
    }

    // A control's actual register contents, for display next to it: the raw bit pattern plus the
    // code/max-code in decimal, e.g. "101010 (42/63)" - so it's visible that the slider is really
    // setting a fixed-width digital field, not reading back a continuous number.
    inline juce::String codeText (float value, float lo, float hi, int bits) noexcept
    {
        if (bits <= 0)
            return {};

        const int code = codeOf (value, lo, hi, bits);
        juce::String bin;
        for (int b = bits - 1; b >= 0; --b)
            bin << ((code >> b) & 1);
        return bin + " (" + juce::String (code) + "/" + juce::String ((1 << bits) - 1) + ")";
    }
}
