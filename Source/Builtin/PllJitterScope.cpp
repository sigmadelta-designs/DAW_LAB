#include "PllJitterScope.h"
#include "PllJitterScopeEditor.h"

PllJitterScope::PllJitterScope (std::shared_ptr<PllLinkSettings> linkSettings)
    : BuiltInProcessorBase (id, "PLL Jitter / Phase Noise Scope", std::move (linkSettings), createLayout(), PllCore::numChannels)
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PllJitterScope::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "thru", 1 }, "Pass-through (off = terminate the signal here)", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "probe", 1 }, "Probe",
                                                              juce::StringArray { "Reference", "VCO output", "Feedback" }, 1));
    return layout;
}

void PllJitterScope::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    sampleCounter = 0.0;
    fifo.reset();
}

void PllJitterScope::setCaptureActive (bool shouldCapture)
{
    capturing = shouldCapture;
}

void PllJitterScope::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < PllCore::numChannels)
        return;

    const int probe = juce::jlimit ((int) probeRef, (int) probeFb, (int) getParam ("probe"));
    const int channel = probe == probeRef ? PllCore::refCh : (probe == probeVco ? PllCore::vcoCh : PllCore::fbCh);
    const auto* in = buffer.getReadPointer (channel);

    if (capturing.load())
    {
        displayed.assign (in, in + numSamples);
        int start1, size1, start2, size2;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        int written = 0;
        const auto copy = [&] (int start, int size)
        {
            for (int i = 0; i < size; ++i, ++written)
            {
                fifoSamples[(size_t) (start + i)] = displayed[(size_t) written];
                fifoIndices[(size_t) (start + i)] = sampleCounter + written;
            }
        };

        copy (start1, size1);
        copy (start2, size2);
        fifo.finishedWrite (size1 + size2);
    }

    sampleCounter += numSamples;

    // Pass-through: same "terminated input vs a T-connector" idea as the other scopes.
    if (getParam ("thru") < 0.5f)
        buffer.clear();
}

int PllJitterScope::readCaptured (std::vector<float>& samples, std::vector<double>& indices)
{
    const int available = fifo.getNumReady();
    int start1, size1, start2, size2;
    fifo.prepareToRead (available, start1, size1, start2, size2);

    for (auto [start, size] : { std::pair { start1, size1 }, std::pair { start2, size2 } })
        for (int i = 0; i < size; ++i)
        {
            samples.push_back (fifoSamples[(size_t) (start + i)]);
            indices.push_back (fifoIndices[(size_t) (start + i)]);
        }

    fifo.finishedRead (size1 + size2);
    return size1 + size2;
}

juce::AudioProcessorEditor* PllJitterScope::createEditor()
{
    return new PllJitterScopeEditor (*this);
}
