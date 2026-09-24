#include "SerdesReceiver.h"
#include "ReceiverEditor.h"

namespace
{
    constexpr int mask = 8191;
    constexpr int windowSymbols = 512;
    inline int sgn (double v) noexcept { return v >= 0.0 ? 1 : -1; }
}

SerdesReceiver::SerdesReceiver (std::shared_ptr<LinkSettings> linkSettings)
    : BuiltInProcessor (id, "SerDes Receiver", std::move (linkSettings), createLayout())
{
    history.assign (mask + 1, 0.0f);
    startTimerHz (20);
}

juce::AudioProcessorValueTreeState::ParameterLayout SerdesReceiver::createLayout()
{
    using P = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto flt = [&] (const char* paramId, const char* name, float lo, float hi, float interval, float def, float skew = 1.0f)
    { layout.add (std::make_unique<juce::AudioParameterFloat> (P { paramId, 1 }, name, juce::NormalisableRange<float> (lo, hi, interval, skew), def)); };

    layout.add (std::make_unique<juce::AudioParameterBool> (P { "enabled", 1 }, "Receiver enabled", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { "output", 1 }, "Output",
                                                              juce::StringArray { "CTLE output (analog)", "Slicer input (after DFE)", "Recovered data (retimed)" }, 1));
    flt ("target", "Target level (diff FS)", 0.05f, 1.0f, 0.005f, 0.3f);

    flt ("ctleboost", "CTLE boost (dB)", 0.0f, 18.0f, 0.1f, 6.0f);
    flt ("ctlezero", "CTLE zero (GHz)", 0.5f, 8.0f, 0.05f, 2.0f);
    flt ("ctlepole2", "CTLE high pole (GHz)", 4.0f, 30.0f, 0.1f, 16.0f);
    flt ("ctledc", "CTLE DC gain (dB)", -12.0f, 6.0f, 0.1f, 0.0f);
    layout.add (std::make_unique<juce::AudioParameterBool> (P { "ctleadapt", 1 }, "Auto-adapt CTLE boost", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (P { "dfeadapt", 1 }, "Auto-adapt DFE taps", true));
    flt ("dfestep", "DFE adaptation step", 0.0001f, 0.01f, 0.0001f, 0.001f, 0.5f);
    for (int k = 0; k < dfeTaps; ++k)
        flt (("dfe" + juce::String (k + 1)).toRawUTF8(), ("DFE tap " + juce::String (k + 1)).toRawUTF8(), -1.0f, 1.0f, 0.001f, 0.0f);

    flt ("cdrkp", "CDR proportional step (mUI)", 0.2f, 20.0f, 0.1f, 3.0f, 0.5f);
    flt ("cdrki", "CDR integral step (uUI)", 0.0f, 500.0f, 1.0f, 30.0f, 0.5f);
    flt ("clockppm", "Reference clock offset (ppm)", -5000.0f, 5000.0f, 1.0f, 0.0f);

    layout.add (std::make_unique<juce::AudioParameterBool> (P { "feedtrainer", 1 }, "Feed the FFE trainer with the slicer error", false));
    return layout;
}

Ctle::Settings SerdesReceiver::getCtleSettings() const
{
    Ctle::Settings s;
    s.boostDb = getParam ("ctleboost");
    s.zeroGHz = getParam ("ctlezero");
    s.pole2GHz = getParam ("ctlepole2");
    s.dcGainDb = getParam ("ctledc");
    return s;
}

double SerdesReceiver::getCtleResponseDb (double audioHz) const
{
    Ctle c;
    c.configure (getCtleSettings(), sampleRate);
    return c.responseDb (audioHz, sampleRate);
}

void SerdesReceiver::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    std::fill (history.begin(), history.end(), 0.0f);
    latest = -1;
    nextEvent = 6.0;
    agc = 1.0;
    integral = 0.0;
    adapting = false;
    for (auto& d : decisions) d = 0.0;
    for (auto& t : taps) t = 0.0;
    feedbackNext = heldFeedback = 0.0;
    switchCount = 0;
    havePrevious = false;
    previousSign = 1;
    edgeAverage = 1.0;
    balanceAverage = 0.0;
    transitionsSeen = 0;
    errorSum = 0.0;
    errorCount = 0;
    regenBase = regenLast = 0.0;
    edgeCount = 0;
    ctle.reset();
    ctleConfigured = false;
    link->rxBits.reset();
    status.locked = false;
    status.window = 0;
    status.symbols = 0;
}

double SerdesReceiver::interpolate (double time) const noexcept
{
    const int64_t base = (int64_t) std::floor (time);
    const float t = (float) (time - (double) base);
    const auto at = [&] (int64_t i) { return history[(size_t) (i & mask)]; };
    const float p0 = at (base - 1), p1 = at (base), p2 = at (base + 1), p3 = at (base + 2);
    return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t * t * t);
}

// One symbol: sample the data and the edge, apply the DFE, slice, adapt, and steer the clock.
void SerdesReceiver::handleEvent()
{
    const double te = nextEvent;
    const double period = unitInterval / juce::jlimit (0.9, 1.1, 1.0 + clockOffset + integral);

    const double sample = interpolate (te);
    const double edge = interpolate (te - 0.5 * period);
    const double equalised = sample - feedbackNext;

    // slicer: NRZ +-1, PAM4 +-1 and +-1/3 (thresholds at 0 and +-2/3 of the outer level)
    double level;
    if (! pam4)
        level = equalised >= 0.0 ? 1.0 : -1.0;
    else
    {
        const double u = equalised / target;
        level = u < -2.0 / 3.0 ? -1.0 : (u < 0.0 ? -1.0 / 3.0 : (u < 2.0 / 3.0 ? 1.0 / 3.0 : 1.0));
    }

    const double error = equalised - target * level;
    errorSum += (error / target) * (error / target);

    // DFE: sign-sign LMS
    if (adapting)
    {
        const int e = sgn (error);
        for (int k = 0; k < dfeTaps; ++k)
            if (decisions[k] != 0.0)
                taps[k] = juce::jlimit (-1.0, 1.0, taps[k] + step * e * sgn (decisions[k]));
    }

    // AGC: sign-sign on the same error
    agc = juce::jlimit (0.02, 100.0, agc * (1.0 - 0.0015 * sgn (error) * sgn (sample)));

    // CDR: Alexander phase detector on the sign, only where the data changes sign
    double phaseShift = 0.0;
    const int currentSign = sgn (level);
    if (havePrevious && currentSign != previousSign)
    {
        const double pe = sgn (edge) == previousSign ? -1.0 : 1.0;    // edge still on the old side: sampling early (-1)
        integral = juce::jlimit (-0.03, 0.03, integral + ki * pe);
        phaseShift = kp * pe;
        edgeAverage += 0.02 * (std::abs (edge) / target - edgeAverage);
        balanceAverage += 0.02 * (pe - balanceAverage);
        ++transitionsSeen;
    }
    previousSign = currentSign;
    havePrevious = true;

    // bits out
    if (! pam4)
        link->rxBits.push (level > 0.0);
    else
    {
        const int index = level < -0.5 ? 0 : (level < 0.0 ? 1 : (level < 0.5 ? 2 : 3));   // -1, -1/3, +1/3, +1
        static const int high[4] = { 0, 0, 1, 1 }, low[4] = { 0, 1, 1, 0 };             // Gray: -3 00, -1 01, +1 11, +3 10
        link->rxBits.push (high[index]);
        link->rxBits.push (low[index]);
    }

    // history of decisions and the feedback for the next symbol
    for (int k = dfeTaps - 1; k > 0; --k)
        decisions[k] = decisions[k - 1];
    decisions[0] = level;

    feedbackNext = 0.0;
    for (int k = 0; k < dfeTaps; ++k)
        feedbackNext += taps[k] * decisions[k];
    feedbackNext *= target;

    // the analog feedback takes effect at the start of the next symbol's window
    if (switchCount < 8)
        switches[switchCount++] = { te + 0.5 * period, feedbackNext };

    // regenerated data: this symbol's level arrives a UI later, with a 0.4 UI edge
    if (edgeCount < 6)
        edges[edgeCount++] = { te + 0.3 * period, target * level - regenLast };
    regenLast = target * level;

    ++errorCount;
    status.symbols.fetch_add (1);

    if (errorCount >= windowSymbols)
    {
        status.nmse = (float) (errorSum / errorCount);
        status.window.fetch_add (1);
        errorSum = 0.0;
        errorCount = 0;

        status.locked = transitionsSeen > 150 && edgeAverage < 0.4 && std::abs (balanceAverage) < 0.3 && status.nmse.load() < 0.6f;
        status.frequencyPpm = (float) (integral * 1.0e6);
        status.agcGain = (float) agc;
        status.edgeLevel = (float) edgeAverage;
        for (int k = 0; k < dfeTaps; ++k)
            status.dfe[k] = (float) taps[k];

        if (getParam ("feedtrainer") > 0.5f)
        {
            link->probe.nmse = status.nmse.load();
            link->probe.valid = status.locked.load();
            link->probe.window.fetch_add (1);
        }
    }

    nextEvent = te + period - phaseShift * period;
}

void SerdesReceiver::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (getParam ("enabled") < 0.5f || buffer.getNumChannels() < 2)
        return;

    // per-block settings
    pam4 = link->isPam4();
    unitInterval = link->samplesPerUI (sampleRate);
    target = getParam ("target");
    kp = getParam ("cdrkp") * 1.0e-3;
    ki = getParam ("cdrki") * 1.0e-6;
    step = getParam ("dfestep");
    clockOffset = getParam ("clockppm") * 1.0e-6;

    const auto settings = getCtleSettings();
    const auto same = [] (double a, double b) { return juce::approximatelyEqual (a, b); };
    if (! ctleConfigured || ! same (settings.boostDb, appliedCtle.boostDb) || ! same (settings.zeroGHz, appliedCtle.zeroGHz)
        || ! same (settings.pole2GHz, appliedCtle.pole2GHz) || ! same (settings.dcGainDb, appliedCtle.dcGainDb))
    {
        ctle.configure (settings, sampleRate);
        appliedCtle = settings;
        ctleConfigured = true;
    }

    const bool wantAdapt = getParam ("dfeadapt") > 0.5f;
    if (wantAdapt != adapting || ! wantAdapt)
    {
        for (int k = 0; k < dfeTaps; ++k)
            taps[k] = getParam (("dfe" + juce::String (k + 1)).toRawUTF8());   // start from (or follow) the parameters
        adapting = wantAdapt;
    }

    const int mode = (int) getParam ("output");
    float* plus = buffer.getWritePointer (1);
    float* minus = buffer.getWritePointer (0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const double differential = (double) plus[i] - (double) minus[i];
        const float y = (float) (agc * ctle.process (differential));
        history[(size_t) ((++latest) & mask)] = y;

        double out = 0.0;
        const int64_t m = latest - 2;            // the clock runs two samples behind so interpolation has lookahead
        if (m >= 4)
        {
            while (nextEvent <= (double) m)
                handleEvent();

            // apply feedback switches that have come due
            int kept = 0;
            for (int s = 0; s < switchCount; ++s)
            {
                if (switches[s].time <= (double) m) heldFeedback = switches[s].value;
                else                                switches[kept++] = switches[s];
            }
            switchCount = kept;

            const double analog = history[(size_t) (m & mask)];
            if (mode == outputCtle)
                out = analog;
            else if (mode == outputSlicerInput)
                out = analog - heldFeedback;
            else
            {
                const double rise = 0.4 * unitInterval;
                double value = regenBase;
                int keptEdges = 0;
                for (int e = 0; e < edgeCount; ++e)
                {
                    const double z = ((double) m - edges[e].start) / rise;
                    if (z >= 1.0) { regenBase += edges[e].delta; value += edges[e].delta; continue; }
                    if (z > 0.0)  value += edges[e].delta * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * z));
                    edges[keptEdges++] = edges[e];
                }
                edgeCount = keptEdges;
                out = value;
            }
        }

        plus[i] = (float) (0.5 * out);
        minus[i] = (float) (-0.5 * out);
    }
}

// ---------------------------------------------------------------------------------
void SerdesReceiver::timerCallback()
{
    // keep the adapted DFE taps in the parameters so they survive a preset
    if (getParam ("dfeadapt") > 0.5f)
        for (int k = 0; k < dfeTaps; ++k)
            if (auto* p = apvts.getParameter ("dfe" + juce::String (k + 1)))
            {
                const float value = status.dfe[k].load();
                if (std::abs (value - p->convertFrom0to1 (p->getValue())) > 0.004f)
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
            }

    ctleTrainerTick();
}

// CTLE boost search against the slicer error: like the FFE trainer, but one knob.
void SerdesReceiver::ctleTrainerTick()
{
    const bool on = getParam ("ctleadapt") > 0.5f;
    if (! on)
    {
        status.ctleState = 0;
        return;
    }

    const uint32_t window = status.window.load();
    auto* boost = apvts.getParameter ("ctleboost");
    const auto setBoost = [&] (float v) { v = juce::jlimit (0.0f, 18.0f, v); boost->setValueNotifyingHost (boost->convertTo0to1 (v)); return v; };

    if (ctleRestart.exchange (false) || status.ctleState.load() == 0)
    {
        bestBoost = trialBoost = getParam ("ctleboost");
        ctleStep = 3.0f;
        ctleDirection = 1;
        ctleTriedOpposite = false;
        status.ctleState = 1;
        ctleWaitUntil = window + 4;
        return;
    }

    if (window < ctleWaitUntil || ! status.locked.load())
        return;

    const float nmse = status.nmse.load();
    const auto trial = [&] (float v) { trialBoost = setBoost (v); ctleWaitUntil = window + 4; };

    switch (status.ctleState.load())
    {
        case 1:
            bestNmse = nmse;
            status.ctleState = 2;
            trial (bestBoost + (float) ctleDirection * ctleStep);
            break;

        case 2:
            if (nmse < bestNmse * 0.98f)
            {
                bestNmse = nmse;
                bestBoost = trialBoost;
                trial (bestBoost + (float) ctleDirection * ctleStep);
            }
            else if (! ctleTriedOpposite)
            {
                ctleTriedOpposite = true;
                ctleDirection = -ctleDirection;
                trial (bestBoost + (float) ctleDirection * ctleStep);
            }
            else
            {
                setBoost (bestBoost);
                ctleStep *= 0.5f;
                if (ctleStep < 0.4f)
                {
                    status.ctleState = 3;
                    ctleWaitUntil = window + 4;
                }
                else
                {
                    ctleDirection = 1;
                    ctleTriedOpposite = false;
                    trial (bestBoost + ctleStep);
                }
            }
            break;

        case 3:
            if (nmse > bestNmse * 3.0f + 0.01f)   // the link changed: train again
                ctleRestart = true;
            break;

        default:
            break;
    }
}

juce::AudioProcessorEditor* SerdesReceiver::createEditor()
{
    return new ReceiverEditor (*this);
}
