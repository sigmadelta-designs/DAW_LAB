#pragma once

#include "BuiltInProcessor.h"
#include "Prbs.h"

// Adds noise or interference to the pair.
//
// All types are scaled to a common yardstick: `level` is the RMS of what is added
// (for DC it is the offset). "Differential" adds +n/2 to R and -n/2 to L, so the
// differential signal R - L sees exactly `level`; "Common-mode" adds n to both legs,
// which a differential probe does not see at all.
//
// Coloured types are defined on the audio frequency axis (the 1e6 scale applies:
// 20 Hz .. 20 kHz here is 20 MHz .. 20 GHz on the wire).
class NoiseInjector : public BuiltInProcessor
{
public:
    static constexpr const char* id = "lab.noise";

    enum Type { white, pink, brown, blue, violet, gray, dc, sine, crosstalk, numTypes };
    static const char* getTypeName (int type);

    explicit NoiseInjector (std::shared_ptr<LinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Unit-RMS generators, exposed so they can be measured.
    class Coloured
    {
    public:
        explicit Coloured (int seed = 1) : random (seed) {}
        float next (int type);      // white, pink, brown, blue, violet only; unit RMS
        float nextRaw (int type);   // before normalisation

    private:
        float gaussian();

        juce::Random random;
        float pinkState[7] {};
        float brownState = 0.0f, lastPink = 0.0f, lastWhite = 0.0f;
    };

    // Inverse A-weighting (equal-loudness "gray") FIR, unit-power.
    static std::vector<float> designGrayKernel (double sampleRate, int taps);

private:
    // PRBS aggressor coupled through a differentiator: a crosstalk stand-in.
    struct Aggressor
    {
        void reset (double unitInterval, double ratio);
        float next();

        PrbsGenerator prbs;
        double phase = 0.0, step = 0.2;
        float previousLevel = 0.0f, currentLevel = 0.0f, lowpass = 0.0f, cornerGain = 0.1f;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float nextGray();
    void calibrateCrosstalk (double unitInterval, double ratio);

    Coloured coloured { 7 };
    double sampleRate = 48000.0;

    std::vector<float> grayKernel, grayHistory;
    int grayPosition = 0;

    double sinePhase = 0.0;

    Aggressor aggressor;
    float crosstalkNorm = 1.0f;
    double calibratedUnitInterval = 0.0, calibratedRatio = 0.0;
};
