#pragma once

#include "LinkSettings.h"

// Audio-thread signal-quality meter for a differential stream. It recovers the
// symbol clock from the zero crossings (circular mean of their phase within the
// UI), samples the waveform half a UI later, decides which level each sample
// belongs to, and reports the decision error normalised to the signal amplitude:
//
//     nmse = mean( (y - decision)^2 ) / A^2
//
// A closed eye gives a large value, a clean one a small one, independent of gain.
// One result is published per measurement window.
class SymbolProbe
{
public:
    void reset();
    void process (const float* diff, int numSamples, double samplesPerUI, bool pam4,
                  double sampleRate, LinkSettings::Probe& result);

    // Static helper so the metric can be computed offline from a list of samples.
    static float computeNmse (const std::vector<float>& samples, bool pam4, bool& valid);

private:
    void finishWindow (bool pam4, LinkSettings::Probe& result);

    float history[4] {};
    int historyCount = 0;
    double sampleIndex = 0.0;
    float previous = 0.0f;
    bool havePrevious = false;

    double lastUnitInterval = 0.0;
    bool lastPam4 = false;

    double crossSin = 0.0, crossCos = 0.0;
    int crossings = 0;

    std::vector<float> windowValues;
    double windowStart = 0.0;
};
