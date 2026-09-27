#include "NoiseInjector.h"
#include "NoiseEditor.h"

juce::AudioProcessorEditor* NoiseInjector::createEditor() { return new NoiseEditor (*this); }

namespace
{
    constexpr int grayTaps = 512;

    // RMS of each coloured generator, measured once so every type comes out at unit power.
    const float* colourNormalisation()
    {
        static const auto table = []
        {
            static float values[NoiseInjector::numTypes] {};
            for (int type : { (int) NoiseInjector::white, (int) NoiseInjector::pink, (int) NoiseInjector::brown,
                              (int) NoiseInjector::blue, (int) NoiseInjector::violet })
            {
                NoiseInjector::Coloured generator (99);
                double sum = 0.0;
                constexpr int count = 1 << 19;
                for (int i = 0; i < 4096; ++i) generator.nextRaw (type);   // settle
                for (int i = 0; i < count; ++i) { const double v = generator.nextRaw (type); sum += v * v; }
                values[type] = (float) (1.0 / std::sqrt (sum / count));
            }
            return values;
        }();

        return table;
    }
}

const char* NoiseInjector::getTypeName (int type)
{
    static const char* names[] = { "White", "Pink (1/f)", "Brown (1/f^2)", "Blue", "Violet", "Gray (equal loudness)",
                                   "DC offset", "Sine interference", "Crosstalk (PRBS aggressor)" };
    return names[type];
}

NoiseInjector::NoiseInjector (std::shared_ptr<LinkSettings> linkSettings)
    : BuiltInProcessor (id, "SerDes Noise Injector", std::move (linkSettings), createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout NoiseInjector::createLayout()
{
    juce::StringArray types;
    for (int i = 0; i < numTypes; ++i)
        types.add (getTypeName (i));

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "type", 1 }, "Noise type", types, white));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "level", 1 }, "Level (RMS, FS; DC = offset)",
                                                             juce::NormalisableRange<float> (0.0f, 0.5f, 0.0005f, 0.5f), 0.02f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "injection", 1 }, "Injection",
                                                              juce::StringArray { "Differential", "Common-mode" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "invert", 1 }, "Invert polarity (DC / sine / crosstalk)", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "freq", 1 }, "Sine frequency (MHz, real-world)",
                                                             juce::NormalisableRange<float> (0.01f, 500.0f, 0.01f, 0.3f), 10.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "aggoffset", 1 }, "Aggressor rate offset (%)",
                                                             juce::NormalisableRange<float> (-25.0f, 25.0f, 0.1f), 1.3f));
    return layout;
}

// ---------------------------------------------------------------------------------
float NoiseInjector::Coloured::gaussian()
{
    const double u1 = juce::jmax (1.0e-12, (double) random.nextFloat()), u2 = random.nextDouble();
    return (float) (std::sqrt (-2.0 * std::log (u1)) * std::cos (juce::MathConstants<double>::twoPi * u2));
}

float NoiseInjector::Coloured::next (int type)
{
    return nextRaw (type) * colourNormalisation()[type];
}

float NoiseInjector::Coloured::nextRaw (int type)
{
    const float w = gaussian();
    float out = w;

    switch (type)
    {
        case pink:
        case blue:
        {
            // Paul Kellet's refined pink filter (about -3 dB/octave)
            pinkState[0] = 0.99886f * pinkState[0] + w * 0.0555179f;
            pinkState[1] = 0.99332f * pinkState[1] + w * 0.0750759f;
            pinkState[2] = 0.96900f * pinkState[2] + w * 0.1538520f;
            pinkState[3] = 0.86650f * pinkState[3] + w * 0.3104856f;
            pinkState[4] = 0.55000f * pinkState[4] + w * 0.5329522f;
            pinkState[5] = -0.7616f * pinkState[5] - w * 0.0168980f;
            const float pinkOut = pinkState[0] + pinkState[1] + pinkState[2] + pinkState[3] + pinkState[4]
                                  + pinkState[5] + pinkState[6] + w * 0.5362f;
            pinkState[6] = w * 0.115926f;

            out = type == pink ? pinkOut : pinkOut - lastPink;   // blue = differentiated pink (+3 dB/oct)
            lastPink = pinkOut;
            break;
        }
        case brown:
            brownState = 0.997f * brownState + w;   // leaky integrator (-6 dB/octave)
            out = brownState;
            break;
        case violet:
            out = w - lastWhite;                     // differentiated white (+6 dB/octave)
            lastWhite = w;
            break;
        default:
            break;
    }

    return out;
}

// Inverse of the A-weighting curve (capped at +30 dB), so the result sounds equally
// loud at every frequency. Zero-phase design by direct inverse DFT, Hann windowed.
std::vector<float> NoiseInjector::designGrayKernel (double sampleRate, int taps)
{
    const auto weighting = [] (double f)
    {
        const double f2 = f * f;
        const double ra = (12194.0 * 12194.0 * f2 * f2)
                          / ((f2 + 20.6 * 20.6) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
        return ra * 1.2589;   // +2 dB so the curve is 0 dB at 1 kHz
    };

    const int bins = taps / 2 + 1;
    std::vector<double> gain ((size_t) bins);
    for (int k = 0; k < bins; ++k)
        gain[(size_t) k] = juce::jmin (31.6, 1.0 / juce::jmax (1.0e-9, weighting (k * sampleRate / taps)));

    std::vector<float> kernel ((size_t) taps);
    double power = 0.0;

    for (int n = 0; n < taps; ++n)
    {
        const int m = n - taps / 2;
        double sum = gain[0];
        for (int k = 1; k < bins - 1; ++k)
            sum += 2.0 * gain[(size_t) k] * std::cos (juce::MathConstants<double>::twoPi * k * m / taps);
        sum += gain[(size_t) bins - 1] * ((m & 1) ? -1.0 : 1.0);

        const double window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / taps);
        const double h = sum / taps * window;
        kernel[(size_t) n] = (float) h;
        power += h * h;
    }

    const float scale = (float) (1.0 / std::sqrt (power));   // unit-variance white in -> unit RMS out
    for (auto& h : kernel)
        h *= scale;

    return kernel;
}

// ---------------------------------------------------------------------------------
void NoiseInjector::Aggressor::reset (double unitInterval, double ratio)
{
    prbs.setPattern (-1);
    prbs.setPattern (PrbsGenerator::prbs23);
    step = ratio / unitInterval;
    phase = 0.0;
    previousLevel = currentLevel = 0.0f;
    lowpass = 0.0f;

    // coupling corner at a fifth of the line rate (one-pole, trapezoidal)
    const double g = std::tan (juce::MathConstants<double>::pi * 0.2 * ratio / unitInterval);
    cornerGain = (float) (g / (1.0 + g));
}

float NoiseInjector::Aggressor::next()
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
    return x - lowpass;   // high-pass = what is left after removing the low-passed part
}

void NoiseInjector::calibrateCrosstalk (double unitInterval, double ratio)
{
    Aggressor probe;
    probe.reset (unitInterval, ratio);

    double sum = 0.0;
    constexpr int count = 60000;
    for (int i = 0; i < 2000; ++i) probe.next();
    for (int i = 0; i < count; ++i) { const double v = probe.next(); sum += v * v; }

    crosstalkNorm = (float) (1.0 / std::sqrt (juce::jmax (1.0e-12, sum / count)));
    calibratedUnitInterval = unitInterval;
    calibratedRatio = ratio;
    aggressor.reset (unitInterval, ratio);
}

void NoiseInjector::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    grayKernel = designGrayKernel (sampleRate, grayTaps);
    grayHistory.assign ((size_t) grayTaps * 2, 0.0f);
    grayPosition = 0;
    sinePhase = 0.0;
    calibratedUnitInterval = 0.0;   // recalibrate on first use
}

float NoiseInjector::nextGray()
{
    const float w = coloured.next (white);

    grayPosition = (grayPosition + 1) % grayTaps;
    grayHistory[(size_t) grayPosition] = grayHistory[(size_t) (grayPosition + grayTaps)] = w;

    // newest sample is at grayPosition; kernel[0] pairs with the oldest of the window
    const float* window = &grayHistory[(size_t) (grayPosition + 1)];
    float sum = 0.0f;
    for (int i = 0; i < grayTaps; ++i)
        sum += grayKernel[(size_t) i] * window[i];
    return sum;
}

void NoiseInjector::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int type = (int) getParam ("type");
    const float level = getParam ("level");
    const bool commonMode = getParam ("injection") > 0.5f;
    const float polarity = getParam ("invert") > 0.5f ? -1.0f : 1.0f;
    const double sineStep = juce::MathConstants<double>::twoPi * getParam ("freq") * 1.0e6 / LinkSettings::simulationScale / sampleRate;

    if (type == crosstalk)
    {
        const double unitInterval = link->samplesPerUI (sampleRate);
        const double ratio = 1.0 + getParam ("aggoffset") / 100.0;
        if (std::abs (unitInterval - calibratedUnitInterval) > 1.0e-9 || std::abs (ratio - calibratedRatio) > 1.0e-9)
            calibrateCrosstalk (unitInterval, ratio);
    }

    if (buffer.getNumChannels() < 2 || level <= 0.0f)
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float n;

        switch (type)
        {
            case gray:      n = nextGray(); break;
            case dc:        n = polarity; break;
            case sine:      n = polarity * 1.41421356f * (float) std::sin (sinePhase); sinePhase += sineStep;
                            if (sinePhase > juce::MathConstants<double>::twoPi) sinePhase -= juce::MathConstants<double>::twoPi;
                            break;
            case crosstalk: n = polarity * crosstalkNorm * aggressor.next(); break;
            default:        n = coloured.next (type); break;
        }

        if (commonMode)
        {
            left[i]  += level * n;
            right[i] += level * n;
        }
        else
        {
            left[i]  -= 0.5f * level * n;
            right[i] += 0.5f * level * n;
        }
    }
}
