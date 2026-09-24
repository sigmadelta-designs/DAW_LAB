#include "EyeScope.h"
#include "EyeScopeEditor.h"

EyeScope::EyeScope (std::shared_ptr<LinkSettings> linkSettings)
    : BuiltInProcessor (id, "SerDes Eye Scope", std::move (linkSettings), createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout EyeScope::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "vscale", 1 },
                                                             "Vertical scale (diff FS)",
                                                             juce::NormalisableRange<float> (0.1f, 2.0f, 0.01f, 0.6f), 0.75f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "thru", 1 }, "Pass-through (off = terminate the signal here)", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "trainer", 1 }, "Feed the FFE trainer", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "view", 1 }, "View",
                                                              juce::StringArray { "Differential (odd)", "Common-mode (even)", "+ leg (R)", "- leg (L)" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "persistence", 1 },
                                                              "Persistence",
                                                              juce::StringArray { "Infinite", "Long", "Short" }, 1));
    return layout;
}

const char* EyeScope::getViewName (int view)
{
    static const char* names[] = { "Differential (odd mode, R - L)", "Common-mode (even mode, (R + L) / 2)", "+ leg (R)", "- leg (L)" };
    return names[juce::jlimit (0, 3, view)];
}

void EyeScope::prepareToPlay (double newSampleRate, int)
{
    sumOdd = sumEven = 0.0;
    oddMin = oddMax = evenMin = evenMax = 0.0f;
    measured = 0;
    measurements.valid = false;
    sampleRate = newSampleRate;
    sampleCounter = 0.0;
    fifo.reset();
    probe.reset();
    differential.reserve (16384);
}

void EyeScope::setCaptureActive (bool shouldCapture)
{
    capturing = shouldCapture;
}

void EyeScope::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() >= 2)
    {
        // The probe always runs: other stages (the FFE trainer) tune themselves against it.
        differential.resize ((size_t) numSamples);
        displayed.resize ((size_t) numSamples);
        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        const int view = (int) getParam ("view");

        for (int i = 0; i < numSamples; ++i)
        {
            const float odd = r[i] - l[i], even = 0.5f * (r[i] + l[i]);
            differential[(size_t) i] = odd;
            displayed[(size_t) i] = view == viewCommon ? even : (view == viewPlusLeg ? r[i] : (view == viewMinusLeg ? l[i] : odd));

            sumOdd += (double) odd * odd;
            sumEven += (double) even * even;
            oddMin = measured == 0 ? odd : juce::jmin (oddMin, odd);
            oddMax = measured == 0 ? odd : juce::jmax (oddMax, odd);
            evenMin = measured == 0 ? even : juce::jmin (evenMin, even);
            evenMax = measured == 0 ? even : juce::jmax (evenMax, even);
            ++measured;
        }

        if (measured >= (int) (0.25 * sampleRate.load()))
        {
            measurements.oddRms = (float) std::sqrt (sumOdd / measured);
            measurements.evenRms = (float) std::sqrt (sumEven / measured);
            measurements.oddPeakToPeak = oddMax - oddMin;
            measurements.evenPeakToPeak = evenMax - evenMin;
            measurements.valid = true;
            sumOdd = sumEven = 0.0;
            measured = 0;
        }

        if (getParam ("trainer") > 0.5f)
            probe.process (differential.data(), numSamples, link->samplesPerUI (sampleRate.load()),
                           link->isPam4(), sampleRate.load(), link->probe);
    }

    if (capturing.load() && buffer.getNumChannels() >= 2)
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

    sampleCounter += numSamples;

    // Pass-through: the signal goes on to the next stage exactly as it came in. With it off the scope is a
    // terminated input (like a scope on a 50 ohm load instead of a T): it still measures, but nothing is passed on.
    if (getParam ("thru") < 0.5f)
        buffer.clear();
}

int EyeScope::readCaptured (std::vector<float>& samples, std::vector<double>& indices)
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

juce::AudioProcessorEditor* EyeScope::createEditor()
{
    return new EyeScopeEditor (*this);
}
