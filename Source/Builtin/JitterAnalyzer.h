#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

// Timing-jitter and phase-noise analysis from a stream of differential samples. UI-thread only,
// fed the same way EyeAnalyzer is: contiguous runs of samples tagged with their absolute index.
//
// Zero crossings are found the same way EyeAnalyzer finds them (Catmull-Rom sub-sampling, then linear
// interpolation of the sign change) - but instead of folding them into an eye image, each crossing's
// deviation from an ideal, evenly spaced clock becomes one time-interval-error (TIE) sample, in UI.
//
// That ideal clock ("the reference") isn't a fixed phase: it tracks phase and frequency with a very
// narrow, fixed loop bandwidth - the same idea as a real jitter analyzer's internal reference PLL,
// which is deliberately far too slow to follow the jitter being measured. Its only job is soaking up
// long-term wander and clock-rate offset so TIE stays a bounded, meaningful number instead of ramping
// forever - not removing any of the jitter itself.
//
// TIE samples also feed a per-UI (sample-and-held across runs with no transition) series that a Welch-
// averaged FFT turns into a jitter/phase-noise spectral density plot, so periodic jitter shows up as a
// line at its own frequency instead of just inflating the RMS number.
class JitterAnalyzer
{
public:
    static constexpr int lockUI = 1500;            // crossings averaged to find the reference's start phase
    static constexpr int tieHistoryLength = 2048;  // on-screen TIE strip-chart / histogram length
    static constexpr int fftOrder = 10;            // 2^10 = 1024-sample Welch segments
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int segmentsAveraged = 6;     // Welch averaging - fewer, noisier bins vs more, smoother ones

    struct Metrics
    {
        bool valid = false;
        double rmsUI = 0.0, ppUI = 0.0;
        double rmsFromSpectrumUI = 0.0;   // Parseval cross-check: integrated from the PSD instead of the raw samples
        double toneUI = 0.0;              // RMS-equivalent amplitude of the single strongest spectral bin
        double toneHzSim = 0.0;           // ...and its offset frequency (simulation Hz - multiply by 1e6 for real Hz)
        bool toneAboveNoise = false;      // whether that bin actually stands out, or the spectrum just looks flat
    };

    void setup (double samplesPerUI, double baudHzSim, bool pam4);
    void resetAll();
    void relock();

    void push (const float* diff, int numSamples, double firstSampleIndex);

    bool isLocked() const noexcept { return locked; }
    const Metrics& getMetrics() const noexcept { return metrics; }

    // Picoseconds of real-world jitter per 1.0 UI of simulated jitter, at the current link rate.
    double uiToRealPs() const noexcept { return baudHzSim > 0.0 ? 1.0e6 / baudHzSim : 0.0; }

    const std::vector<float>& getTieHistoryUI() const noexcept { return tieRing; }
    int getTieWritePos() const noexcept { return tieWritePos; }
    juce::int64 getTieTotalCount() const noexcept { return tieTotalCount; }

    // One-sided PSD of the TIE signal in dB (rad^2/Hz-style units - see the tooltip/README for what that
    // means for a baseband recovered clock vs a classic RF L(f) phase-noise plot). Index 0 is the lowest
    // non-DC bin.
    const std::vector<float>& getSpectrumDb() const noexcept { return spectrumDb; }
    double getSpectrumBinHzSim() const noexcept { return binHzSim; }

private:
    void processSegment (double time, float value);
    void onCrossing (double crossTimeUI);
    void pushUiSample (float tieUI);
    void runSegmentFft();
    void finishAveraging();

    double samplesPerUI = 5.0;
    double baudHzSim = 1000.0;
    bool pam4 = false;

    // interpolation history, same scheme as EyeAnalyzer
    float history[4] {};
    int historyCount = 0;
    double nextExpectedIndex = -1.0;
    bool havePrevious = false;
    double prevTime = 0.0, prevValue = 0.0;

    // reference-clock lock and slow tracking
    bool locked = false;
    bool lockStarted = false;
    double lockStartUi = 0.0;
    double crossSin = 0.0, crossCos = 0.0;
    int crossCount = 0;
    double refPhaseUI = 0.0;    // the next ideal crossing time, in UI
    double refPeriodUI = 1.0;   // ideal crossings should be an integer number of these apart

    std::vector<float> tieRing = std::vector<float> (tieHistoryLength, 0.0f);
    int tieWritePos = 0;
    juce::int64 tieTotalCount = 0;

    std::vector<float> uiSeries;   // accumulates one sample-and-held TIE value per ideal UI
    float lastHeldTie = 0.0f;

    juce::dsp::FFT fft { fftOrder };
    bool windowReady = false;
    std::vector<float> window = std::vector<float> (fftSize, 0.0f);
    double windowPower = 1.0;   // mean(window^2), for PSD normalisation
    std::vector<float> fftBuffer = std::vector<float> (2 * fftSize, 0.0f);
    std::vector<float> powerAccum = std::vector<float> (fftSize / 2 + 1, 0.0f);   // bins 0 .. Nyquist, inclusive
    int segmentsDone = 0;
    std::vector<float> spectrumDb;
    double binHzSim = 0.0;

    Metrics metrics;
};
