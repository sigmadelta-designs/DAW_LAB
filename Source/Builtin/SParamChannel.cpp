#include "SParamChannel.h"
#include "SParamEditor.h"

namespace
{
    // 4-wide float vectors: what lets the compiler keep several partial sums in flight
    typedef float v4f __attribute__ ((vector_size (16), aligned (4)));

    inline float dot (const float* a, const float* b, int n) noexcept
    {
        v4f s0 {}, s1 {}, s2 {}, s3 {};
        for (int i = 0; i < n; i += 16)
        {
            s0 += *(const v4f*) (a + i)      * *(const v4f*) (b + i);
            s1 += *(const v4f*) (a + i + 4)  * *(const v4f*) (b + i + 4);
            s2 += *(const v4f*) (a + i + 8)  * *(const v4f*) (b + i + 8);
            s3 += *(const v4f*) (a + i + 12) * *(const v4f*) (b + i + 12);
        }
        const v4f s = s0 + s1 + s2 + s3;
        return s[0] + s[1] + s[2] + s[3];
    }
}

SParamChannel::SParamChannel (std::shared_ptr<LinkSettings> linkSettings)
    : BuiltInProcessor (id, "SerDes S-Parameter Channel", std::move (linkSettings), createLayout())
{
    for (auto& h : history)
        h.assign ((size_t) ring * 2, 0.0f);

    for (auto* parameter : { "ports", "taps" })
        apvts.addParameterListener (parameter, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout SParamChannel::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "enabled", 1 }, "Channel enabled", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "ports", 1 }, "Ports (4-port files)",
                                                              juce::StringArray { "P1,P2 in -> P3,P4 out", "P1,P3 in -> P2,P4 out" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "taps", 1 }, "Filter length",
                                                              juce::StringArray { "512 taps", "1024 taps", "2048 taps" }, 1));
    return layout;
}

SParamSynthesis::PortMap SParamChannel::getPortMap() const
{
    SParamSynthesis::PortMap map;
    if (getParam ("ports") > 0.5f)
    {
        map.in[0] = 0;  map.in[1] = 2;
        map.out[0] = 1; map.out[1] = 3;
    }
    return map;
}

// ---------------------------------------------------------------------------------
bool SParamChannel::loadFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found.";
        return false;
    }

    if (file.getSize() > 24 * 1024 * 1024)
    {
        error = "File is too large (over 24 MB).";
        return false;
    }

    return loadText (file.loadFileAsString(), file.getFileName(), TouchstoneData::portsFromFilename (file.getFileName()), error);
}

bool SParamChannel::loadText (const juce::String& text, const juce::String& name, int portsHint, juce::String& error)
{
    TouchstoneData parsed;
    if (! TouchstoneData::parse (text, portsHint, parsed, error))
        return false;

    if (parsed.ports < 2)
    {
        error = "A one-port file has no transmission to apply.";
        return false;
    }

    data = std::move (parsed);
    fileName = name;
    embeddedText = text;
    rebuild();
    return true;
}

void SParamChannel::clearData()
{
    data = TouchstoneData();
    fileName = {};
    embeddedText = {};
    rebuild();
}

void SParamChannel::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    for (auto& h : history)
        std::fill (h.begin(), h.end(), 0.0f);
    position = 0;
    appliedVersion = -1;
    rebuild();   // the filters depend on the sample rate
}

void SParamChannel::rebuild()
{
    Design fresh;

    if (hasData())
    {
        static const int tapChoices[] = { 512, 1024, 2048 };
        const int taps = tapChoices[juce::jlimit (0, 2, (int) getParam ("taps"))];
        firs = SParamSynthesis::design (data, getPortMap(), sampleRate, taps);

        if (firs.taps > 0 && firs.present[0][0])
        {
            fresh.valid = true;
            fresh.taps = firs.taps;
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                {
                    fresh.present[i][j] = firs.present[i][j];
                    if (firs.present[i][j])
                        std::reverse_copy (firs.h[i][j].begin(), firs.h[i][j].end(), fresh.reversed[i][j]);
                }

            // insertion loss across the audio band, for the loss the FFE editor compares against
            const auto map = getPortMap();
            for (int m = 0; m < lossTableSize; ++m)
            {
                const double hz = 1.0e6 * (0.5 * sampleRate) * m / (lossTableSize - 1);
                const double magnitude = 0.5 * (std::abs (SParamSynthesis::transfer (data, map, 0, 0, hz)) + std::abs (SParamSynthesis::transfer (data, map, 1, 1, hz)));
                fresh.lossDb[m] = (float) juce::jmax (0.0, -20.0 * std::log10 (juce::jmax (1.0e-4, magnitude)));
            }
        }
    }
    else
        firs = {};

    {
        const juce::SpinLock::ScopedLockType scoped (lock);
        published = fresh;
    }
    ++version;
    changed.sendChangeMessage();
}

SParamChannel::Summary SParamChannel::getSummary() const
{
    Summary s;
    if (! hasData())
        return s;

    s.valid = true;
    s.points = (int) data.numFrequencies();
    s.ports = data.ports;
    s.firstHz = data.frequencyHz.front();
    s.lastHz = data.frequencyHz.back();
    s.nyquistHz = 0.5 * link->baudHz() * 1.0e6;
    s.warning = firs.warning;

    const auto map = getPortMap();
    const auto t00 = SParamSynthesis::transfer (data, map, 0, 0, s.nyquistHz), t01 = SParamSynthesis::transfer (data, map, 0, 1, s.nyquistHz);
    const auto t10 = SParamSynthesis::transfer (data, map, 1, 0, s.nyquistHz), t11 = SParamSynthesis::transfer (data, map, 1, 1, s.nyquistHz);
    const auto db = [] (TouchstoneData::Complex v) { return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (v))); };
    s.sdd21Db = db (0.5 * (t00 - t01 - t10 + t11));
    s.scc21Db = db (0.5 * (t00 + t01 + t10 + t11));
    s.sdc21Db = db (0.5 * (t00 - t01 + t10 - t11));

    const int row0 = data.ports == 2 ? 1 : map.out[0], col0 = data.ports == 2 ? 0 : map.in[0];
    const int row1 = data.ports == 2 ? 1 : map.out[1], col1 = data.ports == 2 ? 0 : map.in[1];
    s.delayPlusPs = SParamSynthesis::bulkDelay (data, row0, col0) * 1.0e12;
    s.delayMinusPs = SParamSynthesis::bulkDelay (data, row1, col1) * 1.0e12;
    return s;
}

// ---------------------------------------------------------------------------------
void SParamChannel::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (appliedVersion != version.load())
    {
        const juce::SpinLock::ScopedTryLockType scoped (lock);
        if (scoped.isLocked())
        {
            running = published;
            appliedVersion = version.load();
        }
    }

    if (getParam ("enabled") < 0.5f || ! running.valid || buffer.getNumChannels() < 2)
    {
        link->reportChannelLoss (0.0f);
        return;
    }

    // loss at the link's Nyquist frequency, for whoever wants to compare against it
    {
        const double position01 = juce::jlimit (0.0, 1.0, 0.5 * link->baudHz() / (0.5 * sampleRate)) * (lossTableSize - 1);
        const int lower = juce::jmin (lossTableSize - 2, (int) position01);
        const float f = (float) (position01 - lower);
        link->reportChannelLoss (running.lossDb[lower] * (1.0f - f) + running.lossDb[lower + 1] * f);
    }

    const int taps = running.taps;
    float* leg[2] = { buffer.getWritePointer (1), buffer.getWritePointer (0) };   // + is R (channel 1), - is L (channel 0)

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        position = (position + 1) % ring;
        history[0][(size_t) position] = history[0][(size_t) (position + ring)] = leg[0][i];
        history[1][(size_t) position] = history[1][(size_t) (position + ring)] = leg[1][i];

        const int start = (position - taps + 1 + ring) % ring;   // the most recent `taps` samples are contiguous from here
        const float* window0 = &history[0][(size_t) start];
        const float* window1 = &history[1][(size_t) start];

        float outPlus = dot (running.reversed[0][0], window0, taps);
        float outMinus = dot (running.reversed[1][1], window1, taps);

        if (running.present[0][1]) outPlus  += dot (running.reversed[0][1], window1, taps);
        if (running.present[1][0]) outMinus += dot (running.reversed[1][0], window0, taps);

        leg[0][i] = outPlus;
        leg[1][i] = outMinus;
    }
}

// ---------------------------------------------------------------------------------
void SParamChannel::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("SPARAMCHANNEL");
    root.addChildElement (apvts.copyState().createXml().release());

    if (hasData() && embeddedText.length() < 24 * 1024 * 1024)
    {
        auto* file = root.createNewChildElement ("TOUCHSTONE");
        file->setAttribute ("name", fileName);
        file->setAttribute ("ports", data.ports);
        file->addTextElement (embeddedText);
    }

    copyXmlToBinary (root, destData);
}

void SParamChannel::setStateInformation (const void* bytes, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (bytes, sizeInBytes))
    {
        if (! xml->hasTagName ("SPARAMCHANNEL"))
            return;

        if (auto* parameters = xml->getChildByName ("STATE"))
            apvts.replaceState (juce::ValueTree::fromXml (*parameters));

        if (auto* file = xml->getChildByName ("TOUCHSTONE"))
        {
            juce::String error;
            loadText (file->getAllSubText(), file->getStringAttribute ("name"), file->getIntAttribute ("ports"), error);
        }
        else
            clearData();
    }
}

juce::AudioProcessorEditor* SParamChannel::createEditor()
{
    return new SParamEditor (*this);
}
