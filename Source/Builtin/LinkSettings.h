#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "BitLog.h"

// The one "link" every built-in stage shares: line rate and modulation.
// Real-world time is scaled down by a factor of one million so it can run at
// audio rate: 1 GBd on the wire is 1 kBd here, a 5 GHz Nyquist tone is 5 kHz.
class LinkSettings
{
public:
    static constexpr double simulationScale = 1.0e6;
    static constexpr double minGBd = 0.25, maxGBd = 24.0;
    static constexpr double minSamplesPerUI = 3.0;   // below this the waveform is under-sampled

    enum Modulation { nrz = 0, pam4 = 1 };

    std::atomic<float> lineRateGBd { 10.0f };
    std::atomic<int> modulation { nrz };

    // Written by any channel stage (once per audio block) so the FFE editor can compare
    // its boost against it. Reads as 0 when no channel has reported recently, so a
    // disabled, bypassed or removed channel doesn't leave a stale value behind.
    void reportChannelLoss (float lossDb) noexcept
    {
        channelLossDb = lossDb;
        channelReportedAtMs = juce::Time::getMillisecondCounter();
    }

    float getChannelLossDb() const noexcept
    {
        return juce::Time::getMillisecondCounter() - channelReportedAtMs.load() < 750 ? channelLossDb.load() : 0.0f;
    }

    // Signal-quality probe, written by the Eye Scope on the audio thread, read by
    // anything that wants to tune itself against the link (the FFE trainer).
    // `nmse` is the decision error at the recovered sampling instant, normalised to
    // the signal amplitude; `window` counts finished measurement windows.
    struct Probe
    {
        std::atomic<float> nmse { 1.0f };
        std::atomic<uint32_t> window { 0 };
        std::atomic<bool> valid { false };
    } probe;

private:
    std::atomic<float> channelLossDb { 0.0f };
    std::atomic<uint32_t> channelReportedAtMs { 0 };

public:
    // NMSE is a power ratio.
    static float nmseToDb (float nmse) noexcept { return 10.0f * std::log10 (juce::jmax (nmse, 1.0e-9f)); }

    // The bits the transmitter sent and the bits the receiver recovered, for comparing the two.
    BitLog txBits, rxBits;

    bool isPam4() const noexcept { return modulation.load() == pam4; }

    double baudHz() const noexcept   { return (double) lineRateGBd.load() * 1.0e9 / simulationScale; }

    // Samples per unit interval, never below what the sample rate can represent.
    double samplesPerUI (double sampleRate) const noexcept
    {
        return juce::jmax (minSamplesPerUI, sampleRate / baudHz());
    }

    // The fastest line rate this sample rate can show.
    static double maxRateForSampleRate (double sampleRate) noexcept
    {
        return juce::jmin (maxGBd, sampleRate / minSamplesPerUI * simulationScale / 1.0e9);
    }
};
