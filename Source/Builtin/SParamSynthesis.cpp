#include "SParamSynthesis.h"

namespace
{
    using Complex = TouchstoneData::Complex;
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
}

namespace SParamSynthesis
{
    double bulkDelay (const TouchstoneData& data, int row, int column)
    {
        std::vector<double> delays;
        for (size_t i = 1; i < data.numFrequencies(); ++i)
        {
            const auto a = data.s (i - 1, row, column), b = data.s (i, row, column);
            if (std::abs (a) < 0.02 || std::abs (b) < 0.02)
                continue;

            double dphi = std::arg (b) - std::arg (a);
            while (dphi > juce::MathConstants<double>::pi)   dphi -= twoPi;
            while (dphi < -juce::MathConstants<double>::pi)  dphi += twoPi;
            delays.push_back (-dphi / (twoPi * (data.frequencyHz[i] - data.frequencyHz[i - 1])));
        }

        if (delays.empty())
            return 0.0;

        std::nth_element (delays.begin(), delays.begin() + (std::ptrdiff_t) delays.size() / 2, delays.end());
        return juce::jmax (0.0, delays[delays.size() / 2]);
    }

    Complex transfer (const TouchstoneData& data, const PortMap& map, int i, int j, double hz)
    {
        if (data.ports == 2)
            return i == j ? data.interpolate (1, 0, hz) : Complex();   // S21

        return data.interpolate (map.out[i], map.in[j], hz);
    }

    FirSet design (const TouchstoneData& data, const PortMap& map, double sampleRate, int taps)
    {
        FirSet result;
        taps = juce::jlimit (16, maxTaps, taps & ~1);
        result.taps = taps;

        if (data.ports < 2 || data.numFrequencies() < 2)
        {
            result.warning = "Not enough data.";
            return result;
        }

        if (data.ports > 2)
            for (int k = 0; k < 2; ++k)
                if (map.in[k] >= data.ports || map.out[k] >= data.ports)
                {
                    result.warning = "The port assignment doesn't fit this file.";
                    return result;
                }

        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                result.present[i][j] = data.ports > 2 || i == j;

        // bulk delay of the through paths
        const auto pathRow = [&] (int leg) { return data.ports == 2 ? 1 : map.out[leg]; };
        const auto pathColumn = [&] (int leg) { return data.ports == 2 ? 0 : map.in[leg]; };
        const double tau = 0.5 * (bulkDelay (data, pathRow (0), pathColumn (0)) + bulkDelay (data, pathRow (1), pathColumn (1)));
        result.delayReal = tau;
        result.delaySamples = tau * 1.0e6 * sampleRate;   // real seconds -> audio seconds -> samples

        if (result.delaySamples + designMargin > 0.85 * taps)
            result.warning = "The channel's delay (" + juce::String (result.delaySamples, 0) + " samples) is too long for a "
                             + juce::String (taps) + "-tap filter - choose more taps.";

        const double fMax = data.frequencyHz.back(), fMin = data.frequencyHz.front();
        const double totalDelayAudio = tau * 1.0e6 + (double) designMargin / sampleRate;

        // twiddle table for the inverse DFT
        std::vector<double> cosTable ((size_t) taps), sinTable ((size_t) taps);
        for (int m = 0; m < taps; ++m)
        {
            cosTable[(size_t) m] = std::cos (twoPi * m / taps);
            sinTable[(size_t) m] = std::sin (twoPi * m / taps);
        }

        const int bins = taps / 2 + 1;
        std::vector<Complex> spectrum ((size_t) bins);

        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
            {
                if (! result.present[i][j])
                    continue;

                // What is left once the bulk delay is taken out is smooth, so it can be interpolated between the file's
                // points. (Interpolating the raw values would not do: a phasor rotating between two points shrinks
                // by cos(dphi/2) when averaged.)
                std::vector<Complex> smooth (data.numFrequencies());
                for (size_t p = 0; p < smooth.size(); ++p)
                {
                    const double hz = data.frequencyHz[p];
                    smooth[p] = transfer (data, map, i, j, hz) * std::polar (1.0, twoPi * hz * tau);
                }

                const auto remainder = [&] (double hz)
                {
                    const auto upper = (size_t) (std::upper_bound (data.frequencyHz.begin(), data.frequencyHz.end(), hz) - data.frequencyHz.begin());
                    if (upper == 0) return smooth.front();
                    if (upper >= smooth.size()) return smooth.back();
                    const double span = data.frequencyHz[upper] - data.frequencyHz[upper - 1];
                    const double f = span > 0.0 ? (hz - data.frequencyHz[upper - 1]) / span : 0.0;
                    return smooth[upper - 1] * (1.0 - f) + smooth[upper] * f;
                };

                const Complex atMin = remainder (fMin), atMax = remainder (fMax);

                for (int k = 0; k < bins; ++k)
                {
                    const double audioHz = (double) k * sampleRate / taps;
                    const double realHz = audioHz * 1.0e6;
                    Complex h;

                    if (realHz < fMin)
                    {
                        // DC from the lowest point's real part, blending up to the first measured value
                        const double f = fMin > 0.0 ? realHz / fMin : 1.0;
                        h = Complex (atMin.real(), 0.0) * (1.0 - f) + atMin * f;
                    }
                    else if (realHz <= fMax)
                        h = remainder (realHz);
                    else
                    {
                        const double x = (realHz - fMax) / (0.25 * fMax);
                        h = x >= 1.0 ? Complex() : atMax * (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * x));
                    }

                    spectrum[(size_t) k] = h * std::polar (1.0, -twoPi * audioHz * totalDelayAudio);
                }

                auto& impulse = result.h[i][j];
                impulse.assign ((size_t) taps, 0.0f);

                for (int n = 0; n < taps; ++n)
                {
                    double sum = spectrum[0].real();
                    for (int k = 1; k < bins - 1; ++k)
                    {
                        const size_t m = (size_t) (((long long) k * n) % taps);
                        sum += 2.0 * (spectrum[(size_t) k].real() * cosTable[m] - spectrum[(size_t) k].imag() * sinTable[m]);
                    }
                    sum += spectrum[(size_t) bins - 1].real() * ((n & 1) ? -1.0 : 1.0);
                    impulse[(size_t) n] = (float) (sum / taps);
                }

                // ease the tail out so truncation doesn't ring
                for (int n = (int) (0.75 * taps); n < taps; ++n)
                    impulse[(size_t) n] *= (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (n - 0.75 * taps) / (0.25 * taps)));
            }

        return result;
    }
}
