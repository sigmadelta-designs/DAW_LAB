#include "TraceChannel.h"
#include <complex>
#include "LossyChannel.h"
#include "TraceEditor.h"

TraceChannel::TraceChannel (std::shared_ptr<LinkSettings> linkSettings)
    : BuiltInProcessor (id, "SerDes Trace Channel", std::move (linkSettings), createLayout())
{
    for (auto* r : { &ring[0], &ring[1], &oddRing, &evenRing, &latticeRing[0], &latticeRing[1] })
        r->assign ((size_t) ringSize, 0.0f);

    for (auto* parameter : { "velocity", "reflections", "connector" })
        apvts.addParameterListener (parameter, this);

    boardEdited();
}

void TraceChannel::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();   // rebuild the impedance profile on the message thread
}

juce::AudioProcessorValueTreeState::ParameterLayout TraceChannel::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "enabled", 1 }, "Channel enabled", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "velocity", 1 }, "Propagation delay (ps/inch)",
                                                             juce::NormalisableRange<float> (100.0f, 260.0f, 1.0f), 170.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "lossscale", 1 }, "Loss vs FR4 (x)",
                                                             juce::NormalisableRange<float> (0.0f, 3.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "coupling", 1 }, "Coupling strength (x)",
                                                             juce::NormalisableRange<float> (0.0f, 4.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "aggressors", 1 }, "Aggressor pickup (x)",
                                                             juce::NormalisableRange<float> (0.0f, 4.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reflections", 1 }, "Reflection strength (x)",
                                                             juce::NormalisableRange<float> (0.0f, 3.0f, 0.01f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "connector", 1 }, "Pad/connector mismatch (%)",
                                                             juce::NormalisableRange<float> (-30.0f, 30.0f, 0.5f), 8.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sourcer", 1 }, "Source resistance (ohm)",
                                                             juce::NormalisableRange<float> (10.0f, 500.0f, 0.5f, 0.5f), 50.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "loadr", 1 }, "Load resistance (ohm)",
                                                             juce::NormalisableRange<float> (10.0f, 500.0f, 0.5f, 0.5f), 50.0f));
    return layout;
}

double TraceChannel::lossDb (double lengthInches, double lineRateGBd, double scale) noexcept
{
    const double nyquistGHz = 0.5 * lineRateGBd;
    return lengthInches * scale * (0.25 * std::sqrt (nyquistGHz) + 0.12 * nyquistGHz);
}

void TraceChannel::boardEdited()
{
    for (int t = 0; t < 2; ++t)
        lengthInches[t] = board.effectiveLengthInches (t);

    weightedCouplingInches = board.weightedCouplingInches();

    // aggressor lanes -> what the audio thread needs
    const auto coupling = board.aggressorCoupling();
    numLanes = (int) coupling.size();
    for (size_t k = 0; k < coupling.size(); ++k)
    {
        laneInches[k][0] = coupling[k].plus;
        laneInches[k][1] = coupling[k].minus;
        laneRatio[k] = board.aggressors[k].rateRatio;
        laneSeed[k] = board.aggressors[k].seed;
    }
    ++laneVersion;

    rebuildProfile();
    boardChanged.sendChangeMessage();
}

// Cut each trace into one-sample sections and give each an impedance from the drawing.
void TraceChannel::rebuildProfile()
{
    LatticeProfile fresh;
    const double strength = getParam ("reflections"), connector = getParam ("connector");

    for (int t = 0; t < 2; ++t)
    {
        const double delay = getDelayPs (t) * 1.0e-6 * sampleRate;   // samples, real trace delay
        const int sections = (int) std::floor (delay) + 1;
        if (delay >= 1.0 && sections >= 2 && sections <= ReflectionLine::maxSections)
        {
            fresh.sections[t] = sections;
            fresh.fraction[t] = delay - (sections - 1);
            const auto z = board.impedanceProfile (t, sections, strength, connector);
            std::copy (z.begin(), z.end(), fresh.impedance[t]);
        }
    }

    {
        const juce::SpinLock::ScopedLockType lock (profileLock);
        published = fresh;
    }
    ++profileVersion;
}

void TraceChannel::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;

    for (auto* r : { &ring[0], &ring[1], &oddRing, &evenRing, &latticeRing[0], &latticeRing[1] })
        std::fill (r->begin(), r->end(), 0.0f);
    writeIndex = 0;
    for (auto& l : line)
        l.reset();
    wasLattice[0] = wasLattice[1] = false;
    appliedProfile = -1;
    appliedSourceR = appliedLoadR = -1.0f;
    rebuildProfile();   // section counts depend on the sample rate
    evenDelay.reset (sampleRate, 0.03);
    evenDelay.setCurrentAndTargetValue (1.0 + getEvenModeExtraDelayPs() * 1.0e-6 * sampleRate);
    std::memset (poleState, 0, sizeof (poleState));

    for (int t = 0; t < 2; ++t)
    {
        delaySamples[t].reset (sampleRate, 0.03);
        delaySamples[t].setCurrentAndTargetValue (1.0 + getDelayPs (t) * 1.0e-6 * sampleRate);
    }
}

// Real delay in ps -> audio seconds: x1e-12 for ps, x1e6 for the simulation scale.
void TraceChannel::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (getParam ("enabled") < 0.5f || buffer.getNumChannels() < 2)
    {
        link->reportChannelLoss (0.0f);
        return;
    }

    const double rate = link->lineRateGBd.load();
    const double scale = getParam ("lossscale");

    double lossPerLeg[2];
    float gain[2];
    for (int t = 0; t < 2; ++t)
    {
        delaySamples[t].setTargetValue (1.0 + getDelayPs (t) * 1.0e-6 * sampleRate);   // +1: the interpolator needs one sample of history

        lossPerLeg[t] = lossDb (getLengthInches (t), rate, scale);
        const double corner = juce::jmin (LossyChannel::cornerForLoss (lossPerLeg[t], 0.5 * link->baudHz()), 0.45 * sampleRate);
        const float g = (float) std::tan (juce::MathConstants<double>::pi * corner / sampleRate);
        gain[t] = lossPerLeg[t] < 0.05 ? -1.0f : g / (1.0f + g);
    }

    link->reportChannelLoss ((float) (0.5 * (lossPerLeg[0] + lossPerLeg[1])));
    evenDelay.setTargetValue (1.0 + getEvenModeExtraDelayPs() * 1.0e-6 * sampleRate);

    // aggressor lanes
    const int lanes = juce::jmin (numLanes.load(), TraceBoard::maxAggressors);
    const double unitInterval = link->samplesPerUI (sampleRate);
    if (appliedVersion != laneVersion.load() || std::abs (unitInterval - appliedUnitInterval) > 1.0e-9)
    {
        appliedVersion = laneVersion.load();
        appliedUnitInterval = unitInterval;
        for (int k = 0; k < lanes; ++k)
            laneSource[k].configure (unitInterval / laneRatio[k].load(), laneSeed[k].load());
    }

    float pickup[TraceBoard::maxAggressors][2];
    const double pickupScale = getParam ("aggressors") * pickupPerInch;
    for (int k = 0; k < lanes; ++k)
    {
        pickup[k][0] = (float) (pickupScale * laneInches[k][0].load());
        pickup[k][1] = (float) (pickupScale * laneInches[k][1].load());
    }

    // reflection lattice: pick up a new profile if there is one
    if (appliedProfile != profileVersion.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (profileLock);
        if (lock.isLocked())
        {
            active = published;
            appliedProfile = profileVersion.load();
            appliedSourceR = appliedLoadR = -1.0f;   // reconfigure the lines
        }
    }

    const float sourceR = getParam ("sourcer"), loadR = getParam ("loadr");
    const bool needLattice = getParam ("reflections") > 0.0f || std::abs (sourceR - lineImpedance) > 0.5f || std::abs (loadR - lineImpedance) > 0.5f;
    bool useLattice[2];
    for (int t = 0; t < 2; ++t)
    {
        useLattice[t] = needLattice && active.sections[t] >= 2;
        if (useLattice[t] && (! wasLattice[t] || ! juce::approximatelyEqual (appliedSourceR, sourceR) || ! juce::approximatelyEqual (appliedLoadR, loadR)))
        {
            if (! wasLattice[t])
                line[t].reset();
            line[t].configure (active.impedance[t], active.sections[t], sourceR, loadR);
        }
        wasLattice[t] = useLattice[t];
        line[t].setLossGain (gain[t]);
    }
    appliedSourceR = sourceR;
    appliedLoadR = loadR;

    // + trace is the R channel (1), - trace is L (0)
    const int channelOf[2] = { 1, 0 };
    float* data[2] = { buffer.getWritePointer (channelOf[0]), buffer.getWritePointer (channelOf[1]) };

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        writeIndex = (writeIndex + 1) & (ringSize - 1);

        for (int t = 0; t < 2; ++t)
        {
            ring[t][(size_t) writeIndex] = data[t][i];

            // The plain path's delay smoothing keeps running even while the lattice is in use.
            const double smoothedDelay = delaySamples[t].getNextValue();
            const double delay = useLattice[t] ? 1.0 + active.fraction[t] : smoothedDelay;

            const auto& source = useLattice[t] ? latticeRing[t] : ring[t];
            if (useLattice[t])
                latticeRing[t][(size_t) writeIndex] = line[t].process (data[t][i]);

            const int base = (int) delay;
            const float frac = (float) (delay - base);
            const auto at = [&] (int d) { return source[(size_t) ((writeIndex - d) & (ringSize - 1))]; };
            const float p0 = at (base - 1), p1 = at (base), p2 = at (base + 1), p3 = at (base + 2);

            float x = 0.5f * (2.0f * p1 + (p2 - p0) * frac
                              + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * frac * frac
                              + (3.0f * p1 - p0 - 3.0f * p2 + p3) * frac * frac * frac);

            if (! useLattice[t] && gain[t] >= 0.0f)   // (the lattice carries its own loss poles)
                for (auto& s : poleState[t])
                {
                    const float v = (x - s) * gain[t];
                    x = v + s;
                    s = x + v;
                }

            data[t][i] = x;
        }

        // Odd / even mode split: the even mode lags the odd one by the coupling-dependent extra delay.
        const float odd = 0.5f * (data[0][i] - data[1][i]);   // data[0] is the + leg (R), data[1] the - leg (L)
        const float even = 0.5f * (data[0][i] + data[1][i]);
        oddRing[(size_t) writeIndex] = odd;
        evenRing[(size_t) writeIndex] = even;

        const double delay = evenDelay.getNextValue();
        const int base = (int) delay;
        const float frac = (float) (delay - base);
        const auto at = [&] (int d) { return evenRing[(size_t) ((writeIndex - d) & (ringSize - 1))]; };
        const float p0 = at (base - 1), p1 = at (base), p2 = at (base + 1), p3 = at (base + 2);
        const float evenOut = 0.5f * (2.0f * p1 + (p2 - p0) * frac
                                      + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * frac * frac
                                      + (3.0f * p1 - p0 - 3.0f * p2 + p3) * frac * frac * frac);
        const float oddOut = oddRing[(size_t) ((writeIndex - 1) & (ringSize - 1))];

        data[0][i] = oddOut + evenOut;
        data[1][i] = evenOut - oddOut;

        // crosstalk from the aggressor lanes, added to each trace in proportion to its own coupling
        for (int k = 0; k < lanes; ++k)
        {
            const float n = laneSource[k].next();
            data[0][i] += pickup[k][0] * n;
            data[1][i] += pickup[k][1] * n;
        }
    }
}

TraceChannel::AggressorReport TraceChannel::getAggressorReport() const
{
    AggressorReport report;
    const double scale = getParam ("aggressors") * pickupPerInch;
    double diffSquared = 0.0, commonSquared = 0.0;

    for (const auto& c : board.aggressorCoupling())
    {
        report.lanes.push_back ({ c.plus, c.minus });
        const double a = scale * c.plus, b = scale * c.minus;
        diffSquared += (a - b) * (a - b);
        commonSquared += 0.25 * (a + b) * (a + b);
    }

    report.differentialRms = std::sqrt (diffSquared);
    report.commonRms = std::sqrt (commonSquared);
    report.sxrDb = report.differentialRms > 1.0e-9 ? 20.0 * std::log10 (0.5 / report.differentialRms) : 99.0;
    return report;
}

TraceChannel::ReflectionReport TraceChannel::getReflectionReport() const
{
    ReflectionReport report;
    report.sourceR = getParam ("sourcer");
    report.loadR = getParam ("loadr");

    const double strength = getParam ("reflections"), connector = getParam ("connector");
    const double nyquistAudioHz = 0.5 * link->baudHz();

    for (int t = 0; t < 2; ++t)
    {
        const double delay = getDelayPs (t) * 1.0e-6 * sampleRate;
        const int sections = (int) std::floor (delay) + 1;
        if (delay < 1.0 || sections < 2 || sections > ReflectionLine::maxSections)
            continue;

        report.active[t] = true;
        report.impedance[t] = board.impedanceProfile (t, sections, strength, connector);
        report.inchesPerSection[t] = getLengthInches (t) / sections;
        report.returnLossDb[t] = TraceReflections::returnLossDb (report.impedance[t], report.loadR, report.sourceR, nyquistAudioHz, sampleRate,
                                                                 getLossDb (t));
        report.worst[t] = TraceReflections::largestDiscontinuities (report.impedance[t], report.sourceR, report.loadR, report.inchesPerSection[t], 3);
    }

    return report;
}

TraceChannel::ModeReport TraceChannel::getModeReport() const
{
    using Complex = std::complex<double>;
    ModeReport report;

    const double rateGBd = link->lineRateGBd.load();
    const double nyquist = 0.5 * rateGBd * 1.0e9;                 // real-world Hz
    const double scale = getParam ("lossscale");
    const double twoPi = juce::MathConstants<double>::twoPi;

    Complex leg[2];
    double delayPs[2];
    for (int t = 0; t < 2; ++t)
    {
        const double loss = lossDb (getLengthInches (t), rateGBd, scale);
        const double corner = LossyChannel::cornerForLoss (loss, nyquist);
        const Complex pole = 1.0 / Complex (1.0, nyquist / corner);
        delayPs[t] = getDelayPs (t);
        leg[t] = std::polar (1.0, -twoPi * nyquist * delayPs[t] * 1.0e-12) * pole * pole;
    }

    const double extraPs = getEvenModeExtraDelayPs();
    const Complex even = std::polar (1.0, -twoPi * nyquist * extraPs * 1.0e-12);

    const auto db = [] (Complex v) { return 20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (v))); };
    report.sdd21Db = db (0.5 * (leg[0] + leg[1]));
    report.scc21Db = db (even * 0.5 * (leg[0] + leg[1]));
    report.sdc21Db = db (even * 0.5 * (leg[0] - leg[1]));
    report.oddDelayPs = 0.5 * (delayPs[0] + delayPs[1]);
    report.evenDelayPs = report.oddDelayPs + extraPs;

    // Drive one trace with an edge of 0.4 UI: each mode carries half of it, offset by extraPs.
    const double edgePs = 0.4 * getUnitIntervalPs();
    report.fextPercent = 50.0 * juce::jmin (1.0, extraPs / juce::jmax (1.0, edgePs)) * std::abs (0.5 * (leg[0] + leg[1]));
    report.coupledInches = board.coupledLengthInches();
    report.weightedInches = weightedCouplingInches.load();
    return report;
}

void TraceChannel::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("TRACECHANNEL");
    root.addChildElement (apvts.copyState().createXml().release());
    root.addChildElement (board.toXml().release());
    copyXmlToBinary (root, destData);
}

void TraceChannel::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("TRACECHANNEL"))
        {
            if (auto* parameters = xml->getChildByName ("STATE"))
                apvts.replaceState (juce::ValueTree::fromXml (*parameters));
            if (auto* boardXml = xml->getChildByName ("BOARD"))
                board.fromXml (*boardXml);

            boardEdited();
        }
    }
}

juce::AudioProcessorEditor* TraceChannel::createEditor()
{
    return new TraceEditor (*this);
}
