#include "JitterScope.h"
#include "JitterScopeEditor.h"

JitterScope::JitterScope (std::shared_ptr<LinkSettings> linkSettings)
    : BuiltInProcessor (id, "SerDes Jitter / Phase Noise Scope", std::move (linkSettings), createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout JitterScope::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "thru", 1 }, "Pass-through (off = terminate the signal here)", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "view", 1 }, "View",
                                                              juce::StringArray { "Differential (odd)", "Common-mode (even)", "+ leg (R)", "- leg (L)" }, 0));
    return layout;
}

void JitterScope::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    sampleCounter = 0.0;
    fifo.reset();
}

void JitterScope::setCaptureActive (bool shouldCapture)
{
    capturing = shouldCapture;
}

void JitterScope::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() >= 2)
    {
        displayed.resize ((size_t) numSamples);
        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        const int view = (int) getParam ("view");

        for (int i = 0; i < numSamples; ++i)
        {
            const float odd = r[i] - l[i], even = 0.5f * (r[i] + l[i]);
            displayed[(size_t) i] = view == viewCommon ? even : (view == viewPlusLeg ? r[i] : (view == viewMinusLeg ? l[i] : odd));
        }

        if (capturing.load())
        {
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
    }

    sampleCounter += numSamples;

    // Pass-through: same "terminated input vs a T-connector" idea as the Eye Scope.
    if (getParam ("thru") < 0.5f)
        buffer.clear();
}

int JitterScope::readCaptured (std::vector<float>& samples, std::vector<double>& indices)
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

juce::AudioProcessorEditor* JitterScope::createEditor()
{
    return new JitterScopeEditor (*this);
}
