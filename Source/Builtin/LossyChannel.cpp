#include "LossyChannel.h"

LossyChannel::LossyChannel (std::shared_ptr<LinkSettings> linkSettings)
    : BuiltInProcessor (id, "SerDes Lossy Channel", std::move (linkSettings), createLayout())
{
}

LossyChannel::~LossyChannel()
{
    link->reportChannelLoss (0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout LossyChannel::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "enabled", 1 },
                                                            "Channel enabled", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "loss", 1 },
                                                             "Loss at Nyquist (dB)",
                                                             juce::NormalisableRange<float> (0.0f, 40.0f, 0.1f), 12.0f));
    return layout;
}

// |H| of two identical poles at Nyquist is 1 / (1 + (fN/fc)^2); solve for fc.
double LossyChannel::cornerForLoss (double lossDb, double nyquistHz)
{
    const double x2 = std::pow (10.0, lossDb / 20.0) - 1.0;
    return x2 <= 1.0e-9 ? 1.0e12 : nyquistHz / std::sqrt (x2);
}

void LossyChannel::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    std::memset (state, 0, sizeof (state));
}

void LossyChannel::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const bool enabled = getParam ("enabled") > 0.5f;
    const float lossDb = enabled ? getParam ("loss") : 0.0f;
    link->reportChannelLoss (lossDb);

    if (lossDb < 0.05f)
        return;   // disabled or lossless: the signal passes through untouched

    const double nyquist = 0.5 * link->baudHz();
    const double corner = juce::jmin (cornerForLoss (lossDb, nyquist), 0.45 * sampleRate);
    const float g = (float) std::tan (juce::MathConstants<double>::pi * corner / sampleRate);
    const float gain = g / (1.0f + g);

    for (int channel = 0; channel < juce::jmin (2, buffer.getNumChannels()); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float x = data[i];

            for (auto& s : state[channel])
            {
                const float v = (x - s) * gain;
                x = v + s;
                s = x + v;
            }

            data[i] = x;
        }
    }
}
