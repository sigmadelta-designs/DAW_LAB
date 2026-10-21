#include "DdrLossyChannel.h"

DdrLossyChannel::DdrLossyChannel (std::shared_ptr<DdrLinkSettings> linkSettings)
    : BuiltInProcessorBase (id, "DDR Lossy Channel", std::move (linkSettings), createLayout(), DdrBus::numChannels)
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DdrLossyChannel::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "swing", 1 },
                                                             "DQ/DQS swing (% of ideal)",
                                                             juce::NormalisableRange<float> (5.0f, 100.0f, 0.1f), 100.0f));
    return layout;
}

void DdrLossyChannel::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < DdrBus::numChannels)
        return;

    const float gain = getParam ("swing") * 0.01f;
    if (gain >= 0.999f)
        return;

    const int numSamples = buffer.getNumSamples();
    for (int b = 0; b < DdrBus::dqWidth; ++b)
        buffer.applyGain (DdrBus::dqChannel (b), 0, numSamples, gain);
    buffer.applyGain (DdrBus::dqsP, 0, numSamples, gain);
    buffer.applyGain (DdrBus::dqsN, 0, numSamples, gain);
}
