#include "SymbolProbe.h"

namespace
{
    float catmullRom (float p0, float p1, float p2, float p3, float t)
    {
        return 0.5f * (2.0f * p1 + (p2 - p0) * t
                       + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                       + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t * t * t);
    }
}

void SymbolProbe::reset()
{
    historyCount = 0;
    havePrevious = false;
    crossSin = crossCos = 0.0;
    crossings = 0;
    windowValues.clear();
    windowStart = sampleIndex;
}

float SymbolProbe::computeNmse (const std::vector<float>& samples, bool pam4, bool& valid)
{
    valid = false;
    if (samples.size() < 40)
        return 1.0f;

    double meanAbs = 0.0;
    for (auto v : samples)
        meanAbs += std::abs (v);
    meanAbs /= (double) samples.size();

    // Outer level A: NRZ sits at +-A; PAM4 levels are +-A and +-A/3, whose mean |y| is 2A/3.
    const double outer = pam4 ? 1.5 * meanAbs : meanAbs;
    if (outer < 1.0e-6)
        return 1.0f;

    double sum = 0.0;
    for (auto v : samples)
    {
        double level;
        if (! pam4)
            level = v >= 0.0f ? outer : -outer;
        else
        {
            const double a = std::abs (v);
            const double magnitude = a < 2.0 * outer / 3.0 ? outer / 3.0 : outer;
            level = v >= 0.0f ? magnitude : -magnitude;
        }

        const double error = v - level;
        sum += error * error;
    }

    valid = true;
    return (float) (sum / (double) samples.size() / (outer * outer));
}

void SymbolProbe::finishWindow (bool pam4, LinkSettings::Probe& result)
{
    bool valid = false;
    const float nmse = computeNmse (windowValues, pam4, valid);
    result.nmse = nmse;
    result.valid = valid;
    result.window.fetch_add (1);
    windowValues.clear();
}

void SymbolProbe::process (const float* diff, int numSamples, double samplesPerUI, bool pam4,
                           double sampleRate, LinkSettings::Probe& result)
{
    if (std::abs (samplesPerUI - lastUnitInterval) > 1.0e-9 || pam4 != lastPam4)
    {
        lastUnitInterval = samplesPerUI;
        lastPam4 = pam4;
        reset();
    }

    const double windowSamples = juce::jmax (0.05 * sampleRate, 300.0 * samplesPerUI);

    for (int i = 0; i < numSamples; ++i)
    {
        const float value = diff[i];
        const double n = sampleIndex++;

        // Clock recovery: circular mean of zero-crossing phase, exponentially forgotten.
        if (havePrevious && ((previous < 0.0f) != (value < 0.0f)))
        {
            const double crossTime = (n - 1.0) + previous / (previous - value);
            const double angle = juce::MathConstants<double>::twoPi * (crossTime / samplesPerUI - std::floor (crossTime / samplesPerUI));
            crossSin = crossSin * 0.99 + std::sin (angle);
            crossCos = crossCos * 0.99 + std::cos (angle);
            crossings = juce::jmin (crossings + 1, 1000000);
        }

        previous = value;
        havePrevious = true;

        history[0] = history[1];
        history[1] = history[2];
        history[2] = history[3];
        history[3] = value;
        historyCount = juce::jmin (4, historyCount + 1);

        if (historyCount == 4 && crossings > 40)
        {
            // Is a sampling instant (half a UI after the mean crossing) inside [n-2, n-1)?
            const double phase = std::atan2 (crossSin, crossCos) / juce::MathConstants<double>::twoPi;
            const double a = (n - 2.0) / samplesPerUI - phase - 0.5;
            const double instant = (phase + 0.5 + std::ceil (a)) * samplesPerUI;

            if (instant >= n - 2.0 && instant < n - 1.0)
                windowValues.push_back (catmullRom (history[0], history[1], history[2], history[3], (float) (instant - (n - 2.0))));
        }

        if (n - windowStart >= windowSamples)
        {
            windowStart = n;
            if (crossings > 40)
                finishWindow (pam4, result);
            else
                windowValues.clear();
        }
    }
}
