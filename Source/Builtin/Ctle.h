#pragma once

#include <juce_core/juce_core.h>
#include <complex>

// Continuous-time linear equaliser: one zero and two poles,
//
//        H(s) = g (1 + s/wz) / ((1 + s/wp1)(1 + s/wp2))
//
// with wp1 = wz x 10^(boost/20), so `boost` is the peaking above the DC gain. The high-frequency pole
// wp2 rolls the peak off, the way the input stage of a real receiver does. Frequencies are real-world
// (GHz); the 1e6 simulation scale maps 1 GHz to 1 kHz. Realised as a biquad by the bilinear transform.
class Ctle
{
public:
    struct Settings
    {
        double dcGainDb = 0.0, zeroGHz = 2.0, boostDb = 6.0, pole2GHz = 15.0;
    };

    void configure (const Settings& s, double sampleRate)
    {
        constexpr double scale = 1.0e6;   // simulation scale
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double wz = twoPi * s.zeroGHz * 1.0e9 / scale;
        const double wp1 = wz * std::pow (10.0, s.boostDb / 20.0);
        const double wp2 = twoPi * juce::jmax (s.pole2GHz, s.zeroGHz * 1.5) * 1.0e9 / scale;
        const double g = std::pow (10.0, s.dcGainDb / 20.0);
        const double k = 2.0 * sampleRate, a = 1.0 / wz, b1 = 1.0 / wp1, b2 = 1.0 / wp2;

        // numerator ((1 + aK) + (1 - aK) z^-1)(1 + z^-1); denominator ((1 + b1K) + (1 - b1K) z^-1)((1 + b2K) + (1 - b2K) z^-1)
        const double n0 = 1.0 + a * k, n1 = 1.0 - a * k;
        const double d10 = 1.0 + b1 * k, d11 = 1.0 - b1 * k, d20 = 1.0 + b2 * k, d21 = 1.0 - b2 * k;

        const double num0 = g * n0, num1 = g * (n0 + n1), num2 = g * n1;
        const double den0 = d10 * d20, den1 = d10 * d21 + d11 * d20, den2 = d11 * d21;

        c[0] = num0 / den0; c[1] = num1 / den0; c[2] = num2 / den0;
        c[3] = den1 / den0; c[4] = den2 / den0;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    // transposed direct form II, in double: the corners are far below the sample rate
    double process (double x) noexcept
    {
        const double y = c[0] * x + z1;
        z1 = c[1] * x - c[3] * y + z2;
        z2 = c[2] * x - c[4] * y;
        return y;
    }

    // gain (dB) of the digital filter at an audio frequency
    double responseDb (double audioHz, double sampleRate) const
    {
        const std::complex<double> zi = std::polar (1.0, -juce::MathConstants<double>::twoPi * audioHz / sampleRate);
        const auto top = c[0] + c[1] * zi + c[2] * zi * zi;
        const auto bottom = 1.0 + c[3] * zi + c[4] * zi * zi;
        return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (top / bottom)));
    }

private:
    double c[5] { 1.0, 0.0, 0.0, 0.0, 0.0 };
    double z1 = 0.0, z2 = 0.0;
};
