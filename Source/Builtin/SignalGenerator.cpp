#include "SignalGenerator.h"
#include "GeneratorEditor.h"

juce::AudioProcessorEditor* SignalGenerator::createEditor() { return new GeneratorEditor (*this); }

SignalGenerator::SignalGenerator (std::shared_ptr<LinkSettings> linkSettings, bool drivesLinkSettings)
    : BuiltInProcessor (id, "SerDes Signal Generator", std::move (linkSettings), createLayout (drivesLinkSettings)),
      drivesLink (drivesLinkSettings)
{
    pushLinkSettings();
}

// Only when a value has changed since the last look, so a control elsewhere (the host strip) isn't overruled every block.
void SignalGenerator::pushLinkSettings()
{
    if (! drivesLink)
        return;

    const float rate = getParam ("linerate");
    const int modulation = (int) getParam ("modulation");

    if (! juce::exactlyEqual (rate, pushedRate))
    {
        pushedRate = rate;
        link->lineRateGBd = rate;
    }

    if (modulation != pushedModulation)
    {
        pushedModulation = modulation;
        link->modulation = modulation;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SignalGenerator::createLayout (bool withLink)
{
    juce::StringArray patterns;
    for (int i = 0; i < PrbsGenerator::numPatterns; ++i)
        patterns.add (PrbsGenerator::getName (i));

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    if (withLink)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "linerate", 1 },
                                                                 "Line rate (GBd)",
                                                                 juce::NormalisableRange<float> ((float) LinkSettings::minGBd, (float) LinkSettings::maxGBd, 0.05f, 0.5f), 10.0f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "modulation", 1 }, "Modulation",
                                                                  juce::StringArray { "NRZ", "PAM4" }, LinkSettings::nrz));
    }

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "pattern", 1 },
                                                              "Pattern", patterns, PrbsGenerator::prbs7));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "drive", 1 }, "Drive",
                                                              juce::StringArray { "Differential (R = +, L = -)", "+ leg only (L quiet)",
                                                                                  "- leg only (R quiet)", "Common-mode (R = L)" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "amplitude", 1 },
                                                             "Amplitude (per leg)",
                                                             juce::NormalisableRange<float> (0.02f, 0.5f), 0.25f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "rise", 1 },
                                                             "Edge time (UI)",
                                                             juce::NormalisableRange<float> (0.05f, 1.0f), 0.4f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "rj", 1 },
                                                             "Random jitter, RJ (UI rms)",
                                                             juce::NormalisableRange<float> (0.0f, 0.15f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sjamp", 1 },
                                                             "Sinusoidal jitter, SJ (UI pk-pk)",
                                                             juce::NormalisableRange<float> (0.0f, 0.6f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sjfreq", 1 },
                                                             "SJ frequency (MHz, real-world)",
                                                             juce::NormalisableRange<float> (0.01f, 100.0f, 0.01f, 0.35f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "dcd", 1 },
                                                             "Duty-cycle distortion, DCD (UI pk-pk)",
                                                             juce::NormalisableRange<float> (0.0f, 0.4f, 0.001f), 0.0f));
    return layout;
}

void SignalGenerator::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    sampleCount = 0.0;
    prbs.setPattern (-1);   // restart the pattern on every prepare
    link->txBits.reset();
    random.setSeed (12345); // and the jitter, so a run is repeatable

    uiTime = 0.0;
    edgeIndex = 0;
    nextEdgeTime = 0.0;
    lastLevel = baseLevel = pendingLevel = 0.0f;
    numActive = 0;
    started = false;
}

// Level in -1..+1. NRZ takes one bit per symbol; PAM4 takes two, Gray coded
// (00 -> -3, 01 -> -1, 11 -> +1, 10 -> +3). Clock patterns are symbol-level, so
// in PAM4 they toggle between the outer levels.
float SignalGenerator::nextSymbolLevel (bool pam4)
{
    const bool isClock = prbs.getPattern() >= PrbsGenerator::clockUI;

    if (! pam4 || isClock)
    {
        const int bit = prbs.nextBit();
        link->txBits.push (bit);
        return bit ? 1.0f : -1.0f;
    }

    const int high = prbs.nextBit();
    const int low = prbs.nextBit();
    link->txBits.push (high);
    link->txBits.push (low);
    static const float gray[4] = { -3.0f, -1.0f, 3.0f, 1.0f };   // index = (high << 1) | low
    return gray[(high << 1) | low] / 3.0f;
}

// Time of the pending edge = its ideal position plus the jitter for it. An edge that
// changes nothing (same level twice) has no timing to disturb.
void SignalGenerator::scheduleNextEdge (double sjPeakToPeakUI, double sjFrequencyHz, double rjUI, double dcdUI)
{
    const float delta = pendingLevel - lastLevel;
    double shift = 0.0;

    if (delta != 0.0f)
    {
        if (rjUI > 0.0)
        {
            // Box-Muller
            const double u1 = juce::jmax (1.0e-12, (double) random.nextFloat()), u2 = random.nextDouble();
            shift += rjUI * std::sqrt (-2.0 * std::log (u1)) * std::cos (juce::MathConstants<double>::twoPi * u2);
        }

        if (sjPeakToPeakUI > 0.0)
            shift += 0.5 * sjPeakToPeakUI
                     * std::sin (juce::MathConstants<double>::twoPi * sjFrequencyHz * sampleCount / sampleRate);

        shift += (delta > 0.0f ? 0.5 : -0.5) * dcdUI;
        shift = juce::jlimit (-maxEdgeShiftUI, maxEdgeShiftUI, shift);
    }

    nextEdgeTime = (double) edgeIndex + shift;
}

float SignalGenerator::sampleWaveform (double time, float rise)
{
    float value = baseLevel;

    for (int i = 0; i < numActive; ++i)
    {
        const float z = juce::jlimit (0.0f, 1.0f, (float) (time - active[(size_t) i].start) / rise);
        value += active[(size_t) i].delta * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * z));
    }

    return value;
}

void SignalGenerator::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    pushLinkSettings();
    prbs.setPattern ((int) getParam ("pattern"));

    const bool pam4 = link->isPam4();
    const double step = 1.0 / link->samplesPerUI (sampleRate);
    const float amplitude = getParam ("amplitude");
    const float rise = getParam ("rise");
    const int drive = (int) getParam ("drive");
    const double rj = getParam ("rj"), sjAmp = getParam ("sjamp"), dcd = getParam ("dcd");
    const double sjHz = getParam ("sjfreq") * 1.0e6 / LinkSettings::simulationScale;

    if (! started)
    {
        started = true;
        pendingLevel = nextSymbolLevel (pam4);
        scheduleNextEdge (sjAmp, sjHz, rj, dcd);
    }

    const int numChannels = buffer.getNumChannels();
    auto* left  = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        uiTime += step;
        sampleCount += 1.0;

        while (uiTime >= nextEdgeTime)
        {
            const float delta = pendingLevel - lastLevel;

            if (delta != 0.0f)
            {
                if (numActive == (int) active.size())
                {
                    baseLevel += active[0].delta;   // out of room: the oldest edge is long finished
                    for (int k = 1; k < numActive; ++k)
                        active[(size_t) k - 1] = active[(size_t) k];
                    --numActive;
                }

                active[(size_t) numActive++] = { nextEdgeTime, delta };
            }

            lastLevel = pendingLevel;
            pendingLevel = nextSymbolLevel (pam4);
            ++edgeIndex;
            scheduleNextEdge (sjAmp, sjHz, rj, dcd);
        }

        // Fold finished edges into the base level.
        while (numActive > 0 && uiTime - active[0].start >= rise)
        {
            baseLevel += active[0].delta;
            for (int k = 1; k < numActive; ++k)
                active[(size_t) k - 1] = active[(size_t) k];
            --numActive;
        }

        const float value = amplitude * sampleWaveform (uiTime, rise);

        // Differential is the normal mode; the others exist to look at coupling (crosstalk) and the even/odd modes.
        switch (drive)
        {
            case 1:  left[i] = 0.0f;   if (right != nullptr) right[i] = value;  break;   // + leg only
            case 2:  left[i] = -value; if (right != nullptr) right[i] = 0.0f;   break;   // - leg only
            case 3:  left[i] = value;  if (right != nullptr) right[i] = value;  break;   // common-mode
            default: left[i] = -value; if (right != nullptr) right[i] = value;  break;   // differential
        }
    }

    for (int channel = 2; channel < numChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}
