#include "DdrEyeScope.h"
#include "DdrEyeScopeEditor.h"

DdrEyeScope::DdrEyeScope (std::shared_ptr<DdrLinkSettings> linkSettings)
    : BuiltInProcessorBase (id, "DDR Eye Scope", std::move (linkSettings), createLayout(), DdrBus::numChannels)
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DdrEyeScope::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vscale", 1 },
                                                             "Vertical scale (FS)",
                                                             juce::NormalisableRange<float> (0.1f, 2.0f, 0.01f, 0.6f), 0.75f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "thru", 1 }, "Pass-through (off = terminate the signal here)", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "probe", 1 }, "Probe",
                                                              juce::StringArray { "DQ0", "DQ1", "DQ2", "DQ3", "DQ4", "DQ5", "DQ6", "DQ7", "DQS" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "persistence", 1 },
                                                              "Persistence",
                                                              juce::StringArray { "Infinite", "Long", "Short" }, 1));
    return layout;
}

const char* DdrEyeScope::getProbeName (int probe)
{
    static const char* names[] = { "DQ0", "DQ1", "DQ2", "DQ3", "DQ4", "DQ5", "DQ6", "DQ7", "DQS (dqsP - dqsN)" };
    return names[juce::jlimit (0, numProbes - 1, probe)];
}

void DdrEyeScope::prepareToPlay (double newSampleRate, int)
{
    sum = 0.0;
    minV = maxV = 0.0f;
    measured = 0;
    measurements.valid = false;
    sampleRate = newSampleRate;
    sampleCounter = 0.0;
    fifo.reset();
    probed.reserve (16384);
}

void DdrEyeScope::setCaptureActive (bool shouldCapture)
{
    capturing = shouldCapture;
}

void DdrEyeScope::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < DdrBus::numChannels)
        return;

    const int numSamples = buffer.getNumSamples();
    const int probe = juce::jlimit (0, numProbes - 1, (int) getParam ("probe"));
    const bool isDqs = probe == DdrBus::dqWidth;

    if (probe != lastProbeSeen)
    {
        lastProbeSeen = probe;
        sum = 0.0;
        measured = 0;
        measurements.valid = false;
    }

    const auto* a = buffer.getReadPointer (isDqs ? DdrBus::dqsP : DdrBus::dqChannel (probe));
    const auto* b = isDqs ? buffer.getReadPointer (DdrBus::dqsN) : nullptr;
    const auto* cmd = buffer.getReadPointer (DdrBus::cmd);

    // DQ/DQS are only actually driven during a write burst - the controller holds them flat at 0 the rest
    // of the time (reads don't drive DQ back out at all, see the Phase 1/Phase 2 note on DdrChip), which is
    // correct bus behaviour but not an eye diagram worth looking at: mixed in with real write traffic, long
    // idle stretches would dilute the measured height/RMS and confuse timing lock for no real reason. Only
    // write-command samples are measured or forwarded to the display FIFO; everything else is skipped
    // (sampleCounter still advances for them), so the index naturally jumps across an idle stretch and the
    // reader's existing gap-detection (any index step > 1.5) splits the eye analyzer's input into separate
    // write bursts automatically, the same mechanism already used for ordinary FIFO drops.
    probed.clear();
    indicesScratch.clear();

    for (int i = 0; i < numSamples; ++i)
    {
        if ((int) std::lround (cmd[i]) != DdrBus::write)
            continue;

        const float v = isDqs ? (a[i] - b[i]) : a[i];
        probed.push_back (v);
        indicesScratch.push_back (sampleCounter + i);

        sum += (double) v * v;
        minV = measured == 0 ? v : juce::jmin (minV, v);
        maxV = measured == 0 ? v : juce::jmax (maxV, v);
        ++measured;
    }

    if (measured >= (int) (0.25 * sampleRate.load()))
    {
        measurements.rms = (float) std::sqrt (sum / measured);
        measurements.peakToPeak = maxV - minV;
        measurements.valid = true;
        sum = 0.0;
        measured = 0;
    }

    if (capturing.load() && ! probed.empty())
    {
        const int toWrite = (int) probed.size();
        int start1, size1, start2, size2;
        fifo.prepareToWrite (toWrite, start1, size1, start2, size2);

        int written = 0;
        const auto copy = [&] (int start, int size)
        {
            for (int i = 0; i < size; ++i, ++written)
            {
                fifoSamples[(size_t) (start + i)] = probed[(size_t) written];
                fifoIndices[(size_t) (start + i)] = indicesScratch[(size_t) written];
            }
        };

        copy (start1, size1);
        copy (start2, size2);
        fifo.finishedWrite (size1 + size2);
    }

    sampleCounter += numSamples;

    // Pass-through: see EyeScope's identical note - off terminates the whole bus here instead of passing
    // it on to whatever's next (another DdrChip rank, say), the same "scope on a terminated load" idea.
    if (getParam ("thru") < 0.5f)
        buffer.clear();
}

int DdrEyeScope::readCaptured (std::vector<float>& samples, std::vector<double>& indices)
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

juce::AudioProcessorEditor* DdrEyeScope::createEditor()
{
    return new DdrEyeScopeEditor (*this);
}
