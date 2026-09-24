#pragma once

#include "Prbs.h"
#include <juce_core/juce_core.h>

// A neighbouring lane's data as it looks when it couples into a victim trace: an NRZ PRBS
// stream at its own rate, raised-cosine edges, passed through a differentiator (capacitive/
// inductive coupling responds to dV/dt), scaled to unit RMS.
// Unit RMS is kept by measuring a stretch of the stream when the source is (re)configured,
// so a pattern or rate change comes out at the right level straight away.
class AggressorSource
{
public:
    // unitInterval: samples per UI of the aggressor; seed decorrelates lanes.
    void configure (double unitInterval, int seed)
    {
        step = 1.0 / unitInterval;
        cornerGain = (float) (std::tan (juce::MathConstants<double>::pi * 0.2 / unitInterval) / (1.0 + std::tan (juce::MathConstants<double>::pi * 0.2 / unitInterval)));
        restart (seed);

        // measure, then start over from the same place
        double sum = 0.0;
        constexpr int count = 24000;
        for (int i = 0; i < 1500; ++i) raw();
        for (int i = 0; i < count; ++i) { const double v = raw(); sum += v * v; }
        norm = (float) (1.0 / std::sqrt (juce::jmax (1.0e-12, sum / count)));
        restart (seed);
    }

    float next() { return raw() * norm; }

private:
    void restart (int seed)
    {
        prbs.setPattern (-1);
        prbs.setPattern (PrbsGenerator::prbs23);
        for (int i = 0; i < 977 * (seed + 1); ++i)
            prbs.nextBit();

        phase = 0.0;
        previousLevel = currentLevel = 0.0f;
        lowpass = 0.0f;
    }

    float raw()
    {
        phase += step;

        while (phase >= 1.0)
        {
            phase -= 1.0;
            previousLevel = currentLevel;
            currentLevel = prbs.nextBit() ? 1.0f : -1.0f;
        }

        const float z = juce::jmin (1.0f, (float) phase / 0.4f);
        const float x = previousLevel + (currentLevel - previousLevel) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * z));

        lowpass += cornerGain * (x - lowpass);
        return x - lowpass;
    }

    PrbsGenerator prbs;
    double phase = 0.0, step = 0.2;
    float previousLevel = 0.0f, currentLevel = 0.0f, lowpass = 0.0f, cornerGain = 0.1f, norm = 1.0f;
};
