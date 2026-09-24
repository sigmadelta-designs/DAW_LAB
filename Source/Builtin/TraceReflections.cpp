#include "TraceReflections.h"

// ---------------------------------------------------------------------------------
void ReflectionLine::reset()
{
    std::fill (std::begin (rightA), std::end (rightA), 0.0f);
    std::fill (std::begin (leftA), std::end (leftA), 0.0f);
    std::fill (std::begin (rightB), std::end (rightB), 0.0f);
    std::fill (std::begin (leftB), std::end (leftB), 0.0f);
    right = rightA; left = leftA; newRight = rightB; newLeft = leftB;
    std::fill (std::begin (poleRight), std::end (poleRight), 0.0f);
    std::fill (std::begin (poleLeft), std::end (poleLeft), 0.0f);
}

void ReflectionLine::configure (const float* impedance, int count, float sourceR, float loadR)
{
    const int previous = sections;
    sections = juce::jlimit (0, maxSections, count);

    // waves already in flight beyond a shortened line are dropped
    for (int k = sections; k < previous; ++k)
        right[k] = left[k] = 0.0f;

    if (sections < 2)
        return;

    for (int k = 0; k + 1 < sections; ++k)
        rho[k] = (impedance[k + 1] - impedance[k]) / (impedance[k + 1] + impedance[k]);

    gammaSource = (sourceR - impedance[0]) / (sourceR + impedance[0]);
    gammaLoad = (loadR - impedance[sections - 1]) / (loadR + impedance[sections - 1]);
    launch = 2.0f * impedance[0] / (sourceR + impedance[0]);   // 1 when the source matches the line
    poleAt[0] = juce::jmax (0, sections / 3 - 1);
    poleAt[1] = juce::jmax (poleAt[0] + 1, 2 * sections / 3 - 1);
    poleAt[1] = juce::jmin (poleAt[1], sections - 2);
}

float ReflectionLine::process (float input) noexcept
{
    const int n = sections;
    if (n < 2)
        return input;

    newRight[0] = input * launch + gammaSource * left[0];

    for (int k = 0; k + 1 < n; ++k)
    {
        const float a = right[k], b = left[k + 1], p = rho[k];
        newRight[k + 1] = (1.0f + p) * a - p * b;
        newLeft[k] = p * a + (1.0f - p) * b;
    }

    newLeft[n - 1] = gammaLoad * right[n - 1];

    if (lossGain >= 0.0f)
        for (int i = 0; i < 2; ++i)
        {
            const int j = poleAt[i];
            float v = (newRight[j + 1] - poleRight[i]) * lossGain;
            float y = v + poleRight[i];
            poleRight[i] = y + v;
            newRight[j + 1] = y;

            v = (newLeft[j] - poleLeft[i]) * lossGain;
            y = v + poleLeft[i];
            poleLeft[i] = y + v;
            newLeft[j] = y;
        }

    std::swap (right, newRight);
    std::swap (left, newLeft);
    return right[n - 1] * (1.0f + gammaLoad);   // voltage across the load
}

// ---------------------------------------------------------------------------------
namespace TraceReflections
{
    // A TDR launches a unit step from a reference-impedance (50 ohm) line into the trace and watches
    // what comes back at that plane: b(t) = rhoRef + (1 - rhoRef) * (wave returning from the trace),
    // and Z(t) = Z0 (1 + b) / (1 - b). The trace's own source resistance plays no part (the TDR is the source).
    Tdr computeTdr (const std::vector<float>& z, double /*sourceR*/, double loadR, double inchesPerSection)
    {
        Tdr result;
        const int n = (int) z.size();
        if (n < 2)
            return result;

        const double z0 = nominalImpedance;
        std::vector<double> rho ((size_t) n - 1);
        for (int k = 0; k + 1 < n; ++k)
            rho[(size_t) k] = (z[(size_t) k + 1] - z[(size_t) k]) / (z[(size_t) k + 1] + z[(size_t) k]);

        const double rhoRef = (z[0] - z0) / (z[0] + z0);
        const double gammaLoad = (loadR - z[(size_t) n - 1]) / (loadR + z[(size_t) n - 1]);

        std::vector<double> r ((size_t) n, 0.0), l ((size_t) n, 0.0), nr ((size_t) n), nl ((size_t) n);
        const auto record = [&] (double distance, double b)
        {
            b = juce::jlimit (-0.95, 0.95, b);
            result.distanceInches.push_back (distance);
            result.impedance.push_back (z0 * (1.0 + b) / (1.0 - b));
        };

        record (0.0, rhoRef);

        for (int t = 0; t < 2 * n + 4; ++t)
        {
            nr[0] = (1.0 + rhoRef) - rhoRef * l[0];
            for (int k = 0; k + 1 < n; ++k)
            {
                nr[(size_t) k + 1] = (1.0 + rho[(size_t) k]) * r[(size_t) k] - rho[(size_t) k] * l[(size_t) k + 1];
                nl[(size_t) k] = rho[(size_t) k] * r[(size_t) k] + (1.0 - rho[(size_t) k]) * l[(size_t) k + 1];
            }
            nl[(size_t) n - 1] = gammaLoad * r[(size_t) n - 1];
            r = nr;
            l = nl;

            // junction k (k + 1 sections from the source) shows up at step 2k + 1
            record (0.5 * (t + 1) * inchesPerSection, rhoRef + (1.0 - rhoRef) * l[0]);
        }

        return result;
    }

    std::vector<Discontinuity> largestDiscontinuities (const std::vector<float>& z, double sourceR, double loadR,
                                                       double inchesPerSection, int count)
    {
        std::vector<Discontinuity> steps;
        const int n = (int) z.size();
        if (n < 2)
            return steps;

        steps.push_back ({ 0.0, (z[0] - sourceR) / (z[0] + sourceR) });   // source into the line
        for (int k = 0; k + 1 < n; ++k)
        {
            const double rho = (z[(size_t) k + 1] - z[(size_t) k]) / (z[(size_t) k + 1] + z[(size_t) k]);
            if (std::abs (rho) > 0.004)
                steps.push_back ({ (k + 1) * inchesPerSection, rho });
        }
        steps.push_back ({ n * inchesPerSection, (loadR - z[(size_t) n - 1]) / (loadR + z[(size_t) n - 1]) });

        // merge neighbouring steps of the same sign (one feature spread over a few sections)
        std::vector<Discontinuity> merged;
        for (const auto& s : steps)
        {
            if (! merged.empty() && (s.distanceInches - merged.back().distanceInches) < 3.5 * inchesPerSection
                && (s.rho > 0) == (merged.back().rho > 0))
                merged.back().rho += s.rho;
            else
                merged.push_back (s);
        }

        std::sort (merged.begin(), merged.end(), [] (const Discontinuity& a, const Discontinuity& b) { return std::abs (a.rho) > std::abs (b.rho); });
        if ((int) merged.size() > count)
            merged.resize ((size_t) count);
        return merged;
    }

    double returnLossDb (const std::vector<float>& z, double loadR, double referenceR, double hz, double sampleRate, double lossDbTotal)
    {
        using Complex = std::complex<double>;
        const int n = (int) z.size();
        if (n < 1)
            return -99.0;

        const double theta = juce::MathConstants<double>::twoPi * hz / sampleRate;       // one section, in radians
        const Complex gamma (lossDbTotal / 8.685889638 / n, theta);                         // per section
        const Complex t = std::tanh (gamma);

        Complex zin (loadR, 0.0);
        for (int k = n - 1; k >= 0; --k)
        {
            const double zk = z[(size_t) k];
            zin = zk * (zin + zk * t) / (zk + zin * t);
        }

        const Complex s11 = (zin - referenceR) / (zin + referenceR);
        return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (s11)));
    }
}
