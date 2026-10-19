#include "JitterAnalyzer.h"

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

void JitterAnalyzer::setup (double newSamplesPerUI, double newBaudHzSim, bool newPam4)
{
    const bool timingChanged = std::abs (newSamplesPerUI - samplesPerUI) > 1.0e-9 || newPam4 != pam4
                                || std::abs (newBaudHzSim - baudHzSim) > 1.0e-6;
    samplesPerUI = newSamplesPerUI;
    baudHzSim = newBaudHzSim;
    pam4 = newPam4;
    binHzSim = baudHzSim / (double) fftSize;

    // Hann window, computed once - its shape never depends on the link rate, only fftSize does.
    if (! windowReady)
    {
        double sumSq = 0.0;
        for (int n = 0; n < fftSize; ++n)
        {
            const double w = 0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi * (double) n / (double) (fftSize - 1)));
            window[(size_t) n] = (float) w;
            sumSq += w * w;
        }
        windowPower = sumSq / (double) fftSize;
        windowReady = true;
    }

    if (timingChanged)
        resetAll();
}

void JitterAnalyzer::relock()
{
    locked = false;
    lockStarted = false;
    crossSin = crossCos = 0.0;
    crossCount = 0;
    havePrevious = false;
    refPhaseUI = 0.0;
    refPeriodUI = 1.0;
    lastHeldTie = 0.0f;

    std::fill (tieRing.begin(), tieRing.end(), 0.0f);
    tieWritePos = 0;
    tieTotalCount = 0;
    uiSeries.clear();
    std::fill (powerAccum.begin(), powerAccum.end(), 0.0f);
    segmentsDone = 0;
    metrics = {};
}

void JitterAnalyzer::resetAll()
{
    relock();
    historyCount = 0;
    nextExpectedIndex = -1.0;
    spectrumDb.clear();
}

void JitterAnalyzer::push (const float* diff, int numSamples, double firstSampleIndex)
{
    if (std::abs (firstSampleIndex - nextExpectedIndex) > 0.5)
    {
        historyCount = 0;
        havePrevious = false;
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

        const double segmentStart = firstSampleIndex + i - 2;

        for (int step = 0; step < subSteps; ++step)
        {
            const float t = (float) step / (float) subSteps;
            processSegment (segmentStart + t, catmullRom (history[0], history[1], history[2], history[3], t));
        }
    }

    nextExpectedIndex = firstSampleIndex + numSamples;
}

void JitterAnalyzer::processSegment (double time, float value)
{
    if (havePrevious && ((prevValue < 0.0) != (value < 0.0)))
    {
        const double frac = prevValue / (prevValue - value);
        const double crossTimeUI = (prevTime + frac * (time - prevTime)) / samplesPerUI;

        if (! locked)
        {
            const double angle = juce::MathConstants<double>::twoPi * (crossTimeUI - std::floor (crossTimeUI));
            crossSin += std::sin (angle);
            crossCos += std::cos (angle);
            ++crossCount;

            if (! lockStarted)
            {
                lockStarted = true;
                lockStartUi = crossTimeUI;
            }
            else if (crossTimeUI - lockStartUi >= lockUI && crossCount > 20)
            {
                // Anchor the reference on this very crossing (not the circular-mean phase estimate
                // itself) so the first TIE sample it ever reports is exactly zero, a clean starting point.
                refPhaseUI = crossTimeUI;
                refPeriodUI = 1.0;
                lastHeldTie = 0.0f;
                locked = true;
            }
        }
        else
            onCrossing (crossTimeUI);
    }

    havePrevious = true;
    prevTime = time;
    prevValue = value;
}

void JitterAnalyzer::onCrossing (double crossTimeUI)
{
    const double rawSteps = (crossTimeUI - refPhaseUI) / refPeriodUI;
    // At least one ideal UI must separate two real crossings; cap an absurd gap (a dropped stream
    // segment, or a freak run far longer than any realistic test pattern) instead of backfilling
    // thousands of held samples in one go.
    const int steps = (int) juce::jlimit (1.0, 4096.0, std::round (rawSteps));
    const double expected = refPhaseUI + (double) steps * refPeriodUI;
    const double tie = crossTimeUI - expected;

    // The reference's own loop: proportional phase correction plus an integral (frequency) term. A real
    // jitter analyzer's internal reference is built to be transparent to the jitter it's measuring, not
    // to track it like a receiver's CDR does - so this loop's bandwidth is set far below the lowest
    // frequency the spectrum plot can even show (a few Hz vs. tens of Hz for the lowest FFT bin at a
    // typical link rate), and over-damped (no peaking), so it only ever removes true DC-ish clock-rate
    // offset over many thousands of symbols, never any content this scope actually displays.
    constexpr double phaseKp = 0.0025, freqKi = 1.0e-6;
    refPeriodUI += freqKi * tie;
    refPhaseUI = expected + phaseKp * tie;

    for (int s = 1; s < steps; ++s)
        pushUiSample (lastHeldTie);
    pushUiSample ((float) tie);
    lastHeldTie = (float) tie;

    tieRing[(size_t) tieWritePos] = (float) tie;
    tieWritePos = (tieWritePos + 1) % tieHistoryLength;
    ++tieTotalCount;
}

void JitterAnalyzer::pushUiSample (float tieUI)
{
    uiSeries.push_back (tieUI);
    if ((int) uiSeries.size() >= fftSize)
        runSegmentFft();
}

void JitterAnalyzer::runSegmentFft()
{
    for (int n = 0; n < fftSize; ++n)
        fftBuffer[(size_t) n] = uiSeries[(size_t) n] * window[(size_t) n];
    std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    fft.performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    // Periodogram, one-sided PSD scaling (Welch): |X(k)|^2 / (Fs * N * mean(window^2)), doubled for
    // every bin except DC and Nyquist since their "negative frequency" twin has already been folded in.
    const double scale = baudHzSim * (double) fftSize * windowPower;
    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const double mag = fftBuffer[(size_t) k];
        double p = (mag * mag) / scale;
        if (k != 0 && k != fftSize / 2)
            p *= 2.0;
        powerAccum[(size_t) k] += (float) p;
    }

    uiSeries.erase (uiSeries.begin(), uiSeries.begin() + fftSize);
    if (++segmentsDone >= segmentsAveraged)
        finishAveraging();
}

void JitterAnalyzer::finishAveraging()
{
    spectrumDb.resize (powerAccum.size());
    double integratedVariance = 0.0;
    int peakBin = -1;
    double peakPower = 0.0, noiseFloorSum = 0.0;
    int noiseFloorCount = 0;

    for (size_t k = 0; k < powerAccum.size(); ++k)
    {
        const double p = (double) powerAccum[k] / (double) segmentsDone;
        spectrumDb[k] = (float) (10.0 * std::log10 (juce::jmax (p, 1.0e-20)));

        if (k >= 1)   // exclude DC - a locked reference should have none, any left over is just the lock's own residual
        {
            integratedVariance += p * binHzSim;
            if (p > peakPower) { peakPower = p; peakBin = (int) k; }
            noiseFloorSum += p;
            ++noiseFloorCount;
        }
    }

    Metrics result;
    result.valid = true;
    result.rmsFromSpectrumUI = std::sqrt (integratedVariance);

    if (peakBin > 0 && noiseFloorCount > 1)
    {
        const double meanFloor = (noiseFloorSum - peakPower) / (double) (noiseFloorCount - 1);

        // A real tone's power leaks into the bins either side of it (window leakage), so summing a
        // small neighbourhood around the peak recovers most of its true power instead of just the one
        // bin that happened to land closest to its exact frequency.
        double toneBinPower = 0.0;
        for (int k = juce::jmax (1, peakBin - 1); k <= juce::jmin ((int) powerAccum.size() - 1, peakBin + 1); ++k)
            toneBinPower += (double) powerAccum[(size_t) k] / (double) segmentsDone;

        result.toneUI = std::sqrt (toneBinPower * binHzSim);
        result.toneHzSim = (double) peakBin * binHzSim;
        result.toneAboveNoise = peakPower > 6.0 * juce::jmax (meanFloor, 1.0e-20);
    }

    // Direct time-domain stats over the same span the spectrum was just built from (one Welch average's
    // worth of UIs), for a Parseval cross-check against rmsFromSpectrumUI.
    const int span = (int) juce::jmin ((juce::int64) (fftSize * segmentsAveraged), tieTotalCount, (juce::int64) tieHistoryLength);
    if (span > 0)
    {
        double sum = 0.0, sumSq = 0.0;
        float lo = tieRing[0], hi = tieRing[0];
        for (int i = 0; i < span; ++i)
        {
            const int idx = (tieWritePos - 1 - i + tieHistoryLength * 4) % tieHistoryLength;
            const float v = tieRing[(size_t) idx];
            sum += v;
            sumSq += (double) v * v;
            lo = juce::jmin (lo, v);
            hi = juce::jmax (hi, v);
        }
        const double mean = sum / span;
        result.rmsUI = std::sqrt (juce::jmax (0.0, sumSq / span - mean * mean));
        result.ppUI = hi - lo;
    }

    metrics = result;
    std::fill (powerAccum.begin(), powerAccum.end(), 0.0f);
    segmentsDone = 0;
}
