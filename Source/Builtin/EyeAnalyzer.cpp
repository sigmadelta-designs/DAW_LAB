#include "EyeAnalyzer.h"

namespace
{
    constexpr int subSteps = 8;

    float catmullRom (float p0, float p1, float p2, float p3, float t)
    {
        return 0.5f * (2.0f * p1 + (p2 - p0) * t
                       + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                       + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t * t * t);
    }
}

void EyeAnalyzer::setup (double newSamplesPerUI, bool newPam4, float newVerticalScale)
{
    const bool timingChanged = std::abs (newSamplesPerUI - samplesPerUI) > 1.0e-9 || newPam4 != pam4;
    samplesPerUI = newSamplesPerUI;
    pam4 = newPam4;

    if (std::abs (newVerticalScale - verticalScale) > 1.0e-6f)
    {
        verticalScale = newVerticalScale;
        clearImage();
    }

    if (timingChanged)
        resetAll();
}

void EyeAnalyzer::clearImage()
{
    std::fill (histogram.begin(), histogram.end(), 0.0f);
    maxCount = 1.0f;
    havePlotPoint = false;
}

void EyeAnalyzer::relock()
{
    locked = false;
    lockStarted = false;
    crossSin = crossCos = 0.0;
    crossCount = 0;
    havePrevious = false;
    havePlotPoint = false;
    windowStarted = false;
    centreSamples.clear();
    jitterMin = 1.0e9;
    jitterMax = -1.0e9;
}

void EyeAnalyzer::resetAll()
{
    clearImage();
    relock();
    metrics = {};
    historyCount = 0;
    nextExpectedIndex = -1.0;
}

void EyeAnalyzer::applyDecay (float factor)
{
    if (factor >= 1.0f)
        return;

    for (auto& bin : histogram)
        bin *= factor;

    maxCount *= factor;
    if (maxCount < 1.0f)
        maxCount = 1.0f;
}

void EyeAnalyzer::push (const float* diff, int numSamples, double firstSampleIndex)
{
    if (std::abs (firstSampleIndex - nextExpectedIndex) > 0.5)
    {
        // A gap in the stream: the interpolation window is no longer valid.
        historyCount = 0;
        havePrevious = false;
        havePlotPoint = false;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        history[0] = history[1];
        history[1] = history[2];
        history[2] = history[3];
        history[3] = diff[i];
        historyCount = juce::jmin (4, historyCount + 1);

        if (historyCount < 4)
            continue;

        // Interpolate the segment between history[1] and history[2].
        const double segmentStart = firstSampleIndex + i - 2;

        for (int step = 0; step < subSteps; ++step)
        {
            const float t = (float) step / (float) subSteps;
            processSegment (segmentStart + t, catmullRom (history[0], history[1], history[2], history[3], t));
        }
    }

    nextExpectedIndex = firstSampleIndex + numSamples;
}

// One interpolated point of the waveform.
void EyeAnalyzer::processSegment (double time, float value)
{
    const double ui = time / samplesPerUI;

    if (havePrevious && ((prevValue < 0.0) != (value < 0.0)))
    {
        // Zero crossing between the previous and current point.
        const double frac = prevValue / (prevValue - value);
        const double crossTime = (prevTime + frac * (time - prevTime)) / samplesPerUI;

        if (! locked)
        {
            const double angle = juce::MathConstants<double>::twoPi * (crossTime - std::floor (crossTime));
            crossSin += std::sin (angle);
            crossCos += std::cos (angle);
            ++crossCount;
        }
        else
        {
            double offset = crossTime - phase;
            offset -= std::floor (offset + 0.5);   // wrap to [-0.5, 0.5)
            jitterMin = juce::jmin (jitterMin, offset);
            jitterMax = juce::jmax (jitterMax, offset);
        }
    }

    if (! locked)
    {
        if (! lockStarted)
        {
            lockStarted = true;
            lockStartTime = ui;
        }
        else if (ui - lockStartTime >= lockUI)
        {
            if (crossCount > 20)
            {
                phase = std::atan2 (crossSin, crossCos) / juce::MathConstants<double>::twoPi;
                locked = true;
                windowStarted = false;
            }
            else
            {
                lockStartTime = ui;   // no edges yet (silence?), keep listening
                crossSin = crossCos = 0.0;
                crossCount = 0;
            }
        }

        havePrevious = true;
        prevTime = time;
        prevValue = value;
        return;
    }

    if (! windowStarted)
    {
        windowStarted = true;
        windowStartTime = ui;
        centreSamples.clear();
        jitterMin = 1.0e9;
        jitterMax = -1.0e9;
    }

    // Position within the 2-UI display, crossings at 0, 1, 2.
    double x = ui - phase;
    x -= 2.0 * std::floor (x / 2.0);

    const double px = x / 2.0 * (width - 1);
    const double py = (1.0 - (value + verticalScale) / (2.0f * verticalScale)) * (height - 1);

    if (havePlotPoint && x >= prevX)
        plotLine (prevX / 2.0 * (width - 1), prevY, px, py);

    havePlotPoint = true;
    prevX = x;
    prevY = py;

    // Sampling instant: half a UI after the crossing.
    if (havePrevious)
    {
        const double a = prevTime / samplesPerUI - phase, b = ui - phase;
        const double centreA = a - std::floor (a), centreB = b - std::floor (b);

        if (centreA < 0.5 && centreB >= 0.5 && b - a < 0.5)
            if (centreSamples.size() < 60000)
                centreSamples.push_back (value);
    }

    havePrevious = true;
    prevTime = time;
    prevValue = value;

    if (ui - windowStartTime >= metricsWindowUI)
        finishMetricsWindow();
}

void EyeAnalyzer::plotLine (double x0, double y0, double x1, double y1)
{
    const int steps = juce::jmax (1, (int) std::ceil (juce::jmax (std::abs (x1 - x0), std::abs (y1 - y0))));

    for (int i = 0; i <= steps; ++i)
    {
        const double f = (double) i / steps;
        const int x = (int) std::lround (x0 + (x1 - x0) * f);
        const int y = (int) std::lround (y0 + (y1 - y0) * f);

        if (x >= 0 && x < width && y >= 0 && y < height)
        {
            auto& bin = histogram[(size_t) (y * width + x)];
            bin += 1.0f;
            maxCount = juce::jmax (maxCount, bin);
        }
    }
}

void EyeAnalyzer::finishMetricsWindow()
{
    Metrics result;
    windowStarted = false;

    if (centreSamples.size() < 50 || jitterMax < jitterMin)
    {
        centreSamples.clear();
        return;
    }

    float lo = centreSamples.front(), hi = lo, peak = 0.0f;
    for (auto v : centreSamples)
    {
        lo = juce::jmin (lo, v);
        hi = juce::jmax (hi, v);
        peak = juce::jmax (peak, std::abs (v));
    }

    result.swing = hi - lo;

    // Sort the sampling-instant values into their levels and find the tightest gap.
    const int levels = pam4 ? 4 : 2;
    float classMin[4], classMax[4];
    bool seen[4] {};
    for (int i = 0; i < 4; ++i) { classMin[i] = 1.0e9f; classMax[i] = -1.0e9f; }

    for (auto v : centreSamples)
    {
        int level;
        if (! pam4)
            level = v >= 0.0f ? 1 : 0;
        else
            level = v < -2.0f * peak / 3.0f ? 0 : (v < 0.0f ? 1 : (v < 2.0f * peak / 3.0f ? 2 : 3));

        seen[level] = true;
        classMin[level] = juce::jmin (classMin[level], v);
        classMax[level] = juce::jmax (classMax[level], v);
    }

    bool allSeen = true;
    float smallest = 1.0e9f;
    for (int level = 0; level < levels; ++level)
        allSeen = allSeen && seen[level];

    if (allSeen)
    {
        for (int level = 0; level + 1 < levels; ++level)
            smallest = juce::jmin (smallest, classMin[level + 1] - classMax[level]);

        result.valid = true;
        result.heightFS = smallest;
        const float ideal = result.swing / (float) (levels - 1);
        result.heightPct = ideal > 0.0f ? juce::jmax (0.0f, 100.0f * smallest / ideal) : 0.0f;
        result.closed = result.heightPct < 2.0f;

        const double spread = jitterMax - jitterMin;
        result.widthUI = (float) juce::jmax (0.0, 1.0 - spread);
        result.widthPct = 100.0f * result.widthUI;
    }

    metrics = result;
    centreSamples.clear();
    jitterMin = 1.0e9;
    jitterMax = -1.0e9;
}

void EyeAnalyzer::render (juce::Image& target) const
{
    if (! target.isValid() || target.getWidth() != width || target.getHeight() != height)
        target = juce::Image (juce::Image::ARGB, width, height, true);

    juce::Image::BitmapData data (target, juce::Image::BitmapData::writeOnly);

    // black -> blue -> cyan -> green -> yellow -> red
    static const juce::Colour stops[] = {
        juce::Colour (0xff000000), juce::Colour (0xff0a1a6e), juce::Colour (0xff0aa5d6),
        juce::Colour (0xff2ee05a), juce::Colour (0xfff5e12a), juce::Colour (0xffff3b1f) };
    constexpr int numStops = (int) (sizeof (stops) / sizeof (stops[0]));

    const float inverseMax = 1.0f / maxCount;

    for (int y = 0; y < height; ++y)
    {
        auto* row = (juce::PixelARGB*) data.getLinePointer (y);

        for (int x = 0; x < width; ++x)
        {
            const float count = histogram[(size_t) (y * width + x)];
            const float level = count <= 0.0f ? 0.0f : std::pow (count * inverseMax, 0.45f);
            const float position = level * (numStops - 1);
            const int index = juce::jmin (numStops - 2, (int) position);
            const auto colour = stops[index].interpolatedWith (stops[index + 1], position - (float) index);
            row[x] = colour.getPixelARGB();
        }
    }
}
