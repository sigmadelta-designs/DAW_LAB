#pragma once

#include <juce_graphics/juce_graphics.h>
#include <vector>

// Builds a persistence eye diagram from a stream of differential samples and
// measures it. UI-thread only; the caller feeds contiguous samples with their
// absolute sample indices.
//
// Timing recovery: the first stretch of samples is used only to find where the
// zero crossings fall within the unit interval (circular mean); the display is
// then aligned so crossings sit at 0, 1 and 2 UI and eye centres at 0.5 and 1.5.
class EyeAnalyzer
{
public:
    static constexpr int width = 512, height = 320;

    struct Metrics
    {
        bool valid = false;
        bool closed = false;
        float heightFS = 0.0f;      // smallest eye opening, in differential full-scale units
        float heightPct = 0.0f;     // as % of the ideal opening for the observed swing
        float swing = 0.0f;         // outer swing at the sampling instant
        float widthUI = 0.0f;       // 1 UI minus the zero-crossing spread
        float widthPct = 0.0f;
    };

    void setup (double samplesPerUI, bool pam4, float verticalScale);
    void resetAll();                        // clear image, metrics and timing lock
    void clearImage();
    void relock();

    void push (const float* diff, int numSamples, double firstSampleIndex);
    void applyDecay (float factor);

    bool isLocked() const noexcept { return locked; }
    const Metrics& getMetrics() const noexcept { return metrics; }
    void render (juce::Image& target) const;   // width x height ARGB

    // Length of a metrics window, in unit intervals.
    static constexpr int metricsWindowUI = 3000;
    static constexpr int lockUI = 1500;

private:
    void processSegment (double time, float value);
    void finishMetricsWindow();
    void plotLine (double x0, double y0, double x1, double y1);

    double samplesPerUI = 5.0;
    bool pam4 = false;
    float verticalScale = 0.75f;

    std::vector<float> histogram = std::vector<float> ((size_t) (width * height), 0.0f);
    float maxCount = 1.0f;

    // sliding window over the input
    float history[4] {};
    int historyCount = 0;
    double nextExpectedIndex = -1.0;

    // timing lock
    bool locked = false;
    double lockStartTime = 0.0;
    bool lockStarted = false;
    double crossSin = 0.0, crossCos = 0.0;
    int crossCount = 0;
    double phase = 0.0;   // UI

    // per-sub-sample state
    bool havePrevious = false;
    double prevTime = 0.0, prevValue = 0.0, prevX = 0.0, prevY = 0.0;
    bool havePlotPoint = false;

    // metrics window
    std::vector<float> centreSamples;
    double jitterMin = 1.0e9, jitterMax = -1.0e9;
    double windowStartTime = 0.0;
    bool windowStarted = false;
    Metrics metrics;
};
