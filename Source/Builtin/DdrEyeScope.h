#pragma once

#include "BuiltInProcessor.h"
#include "DdrBus.h"
#include "DdrLinkSettings.h"
#include "SymbolProbe.h"

// The DDR-domain counterpart to the SerDes side's EyeScope - same pass-through-probe-into-a-FIFO idea
// (and reuses the same EyeAnalyzer unchanged, which was already generic: a stream of samples, a unit
// interval length, a PAM4 flag, nothing else), but a genuinely different stage rather than the existing
// EyeScope stretched to cover both domains. Three real differences made that the cleaner choice:
//   - DQ0-7 are single-ended (one real wire each), not a differential pair - there is no "- leg" to form a
//     R-L odd-mode signal from, the way the SerDes side's two-channel bus always has one.
//   - DQS *is* a true differential pair, so "Probe" can pick either: whichever DQ bit a BIST write/read is
//     actually exercising, or DQS itself (dqsP - dqsN), to check the strobe's own timing margin.
//   - There is no DDR-side equivalent of the SerDes EyeScope's "Feed FFE trainer" probe (DdrLinkSettings
//     carries no adaptive-equalizer feedback object the way LinkSettings::probe does, and there is nothing
//     on the DDR side training against this measurement) - so that whole mechanism is simply absent here,
//     not stubbed out.
class DdrEyeScope : public BuiltInProcessorBase<DdrLinkSettings>
{
public:
    static constexpr const char* id = "lab.ddreyescope";
    static constexpr int numProbes = DdrBus::dqWidth + 1;   // DQ0..DQ7, then DQS

    explicit DdrEyeScope (std::shared_ptr<DdrLinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    double getSampleRate() const noexcept { return sampleRate.load(); }

    static const char* getProbeName (int probe);

    struct Measurements
    {
        std::atomic<float> rms { 0.0f }, peakToPeak { 0.0f };
        std::atomic<bool> valid { false };
    } measurements;

    void setCaptureActive (bool shouldCapture);
    int readCaptured (std::vector<float>& samples, std::vector<double>& indices);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int fifoSize = 1 << 17;

    std::vector<float> probed;
    std::vector<double> indicesScratch;
    int lastProbeSeen = -1;   // resets the rms/pk-pk accumulator when Probe changes - it picks a genuinely
                              // different electrical node (DQ0 vs DQ3 vs DQS), not just a different view of
                              // the same signal the way EyeScope's "view" does, so mixing samples from
                              // before and after a switch would measure a meaningless blend of the two
    double sum = 0.0;
    float minV = 0.0f, maxV = 0.0f;
    int measured = 0;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> capturing { false };
    double sampleCounter = 0.0;

    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoSamples = std::vector<float> (fifoSize);
    std::vector<double> fifoIndices = std::vector<double> (fifoSize);
};
