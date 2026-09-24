#include "DiffFfe.h"
#include "FfeEditor.h"

static const char* const tapIds[DiffFfe::numTaps] = { "pre3", "pre2", "pre1", "main", "post1", "post2", "post3" };

const char* DiffFfe::getTapParamId (int tap) { return tapIds[tap]; }

juce::String DiffFfe::getTapName (int tap)
{
    if (tap == mainTap) return "main";
    return tap < mainTap ? "pre" + juce::String (mainTap - tap) : "post" + juce::String (tap - mainTap);
}

DiffFfe::DiffFfe (std::shared_ptr<LinkSettings> linkSettings)
    : BuiltInProcessor (id, "SerDes FFE", std::move (linkSettings), createLayout())
{
    for (auto& r : ring)
        r.assign ((size_t) ringSize, 0.0f);

    startTimerHz (30);
}

juce::AudioProcessorValueTreeState::ParameterLayout DiffFfe::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int tap = 0; tap < numTaps; ++tap)
    {
        const bool isMain = tap == mainTap;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { tapIds[tap], 1 }, getTapName (tap),
            isMain ? juce::NormalisableRange<float> (0.0f, 1.5f, 0.005f)
                   : juce::NormalisableRange<float> (-0.6f, 0.6f, 0.005f),
            isMain ? 1.0f : 0.0f));
    }

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "txmode", 1 },
                                                            "TX mode (main = 1 - sum|taps|)", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "reverse", 1 },
                                                            "Reverse tap direction", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "adapt", 1 },
                                                            "Auto-adapt taps (needs an Eye Scope downstream)", false));
    return layout;
}

DiffFfe::Taps DiffFfe::getSliderValues() const
{
    Taps values {};
    for (int tap = 0; tap < numTaps; ++tap)
        values[(size_t) tap] = getParam (tapIds[tap]);
    return values;
}

DiffFfe::Taps DiffFfe::getEffectiveTaps() const
{
    return computeTaps (getSliderValues(), getParam ("txmode") > 0.5f, getParam ("reverse") > 0.5f);
}

DiffFfe::Taps DiffFfe::computeTaps (const Taps& sliders, bool txMode, bool reverse)
{
    Taps taps = sliders;

    if (reverse)
        for (int k = 1; k <= mainTap; ++k)
            std::swap (taps[(size_t) (mainTap - k)], taps[(size_t) (mainTap + k)]);

    if (txMode)
    {
        float others = 0.0f;
        for (int tap = 0; tap < numTaps; ++tap)
            if (tap != mainTap)
                others += std::abs (taps[(size_t) tap]);

        taps[mainTap] = juce::jmax (0.0f, 1.0f - others);
    }

    return taps;
}

float DiffFfe::gainDbAtDc (const Taps& taps)
{
    float sum = 0.0f;
    for (auto c : taps) sum += c;
    return juce::Decibels::gainToDecibels (std::abs (sum), -60.0f);
}

float DiffFfe::gainDbAtNyquist (const Taps& taps)
{
    float sum = 0.0f;
    for (int tap = 0; tap < numTaps; ++tap)
        sum += taps[(size_t) tap] * (((tap - mainTap) & 1) ? -1.0f : 1.0f);
    return juce::Decibels::gainToDecibels (std::abs (sum), -60.0f);
}

void DiffFfe::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;

    for (auto& r : ring)
        std::fill (r.begin(), r.end(), 0.0f);
    writeIndex = 0;

    const auto taps = getEffectiveTaps();
    for (int tap = 0; tap < numTaps; ++tap)
    {
        smoothedTaps[(size_t) tap].reset (sampleRate, 0.02);
        smoothedTaps[(size_t) tap].setCurrentAndTargetValue (taps[(size_t) tap]);
    }
}

// Value of the input `delay` samples ago (delay >= 1), cubic (Catmull-Rom) interpolated.
float DiffFfe::readDelayed (int channel, double delay) const
{
    const auto& r = ring[channel];
    const int base = (int) delay;
    const float t = (float) (delay - base);

    const auto at = [&] (int d) { return r[(size_t) ((writeIndex - d) & (ringSize - 1))]; };
    const float p0 = at (base - 1), p1 = at (base), p2 = at (base + 1), p3 = at (base + 2);

    return 0.5f * (2.0f * p1 + (p2 - p0) * t
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                   + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t * t * t);
}

void DiffFfe::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto targets = getEffectiveTaps();
    for (int tap = 0; tap < numTaps; ++tap)
        smoothedTaps[(size_t) tap].setTargetValue (targets[(size_t) tap]);

    const double unitInterval = link->samplesPerUI (sampleRate);
    double delays[numTaps];
    for (int tap = 0; tap < numTaps; ++tap)
        delays[tap] = 1.0 + tap * unitInterval;   // pre3 is the newest sample, post3 the oldest

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    float* data[2] = { buffer.getWritePointer (0), numChannels > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float taps[numTaps];
        for (int tap = 0; tap < numTaps; ++tap)
            taps[tap] = smoothedTaps[(size_t) tap].getNextValue();

        writeIndex = (writeIndex + 1) & (ringSize - 1);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            ring[channel][(size_t) writeIndex] = data[channel][i];

            float y = 0.0f;
            for (int tap = 0; tap < numTaps; ++tap)
                y += taps[tap] * readDelayed (channel, delays[tap]);

            data[channel][i] = y;
        }
    }
}

juce::AudioProcessorEditor* DiffFfe::createEditor()
{
    return new FfeEditor (*this);
}

// ---------------------------------------------------------------------------------
// Link training: coordinate search on the six non-main taps, judged by the downstream
// probe. Each trial waits for two full probe windows so the measurement is not
// contaminated by the change itself.

namespace
{
    constexpr int trainOrder[6] = { 4, 2, 5, 1, 6, 0 };   // post1 pre1 post2 pre2 post3 pre3

    // The probe's clock recovery needs a few tens of milliseconds to follow a change in the
    // edges, so a trial is judged on the third window after it, not the first.
    constexpr uint32_t settleWindows = 3;
}

void DiffFfe::setSlider (int tap, float value)
{
    if (auto* parameter = apvts.getParameter (tapIds[tap]))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void DiffFfe::applyTrial()
{
    setSlider (trainOrder[orderPosition], trialValue);
    waitUntilWindow = link->probe.window.load() + settleWindows;
}

void DiffFfe::beginTap()
{
    const int tap = trainOrder[orderPosition];
    direction = -1;   // de-emphasis (negative) is the usual direction, so try it first
    triedOpposite = movedThisTap = false;
    trialValue = juce::jlimit (-0.6f, 0.6f, trainerBase[(size_t) tap] + (float) direction * trainerStep);
    applyTrial();
}

void DiffFfe::advanceTap()
{
    if (++orderPosition == 6)
    {
        orderPosition = 0;

        if (! sweepImproved)
        {
            trainerStep *= 0.5f;
            if (trainerStep < 0.004f)
            {
                trainerStatus.state = trainerConverged;
                trainerStatus.step = 0.0f;
                convergedNmse = smoothedNmse;
                waitUntilWindow = link->probe.window.load() + settleWindows;   // don't judge the last revert's transient
                return;
            }
        }

        sweepImproved = false;
    }

    beginTap();
}

void DiffFfe::trainerTick()
{
    const bool adaptOn = getParam ("adapt") > 0.5f;

    if (! adaptOn)
    {
        trainerStatus.state = trainerOff;
        return;
    }

    // Is anything downstream producing measurements?
    const uint32_t window = link->probe.window.load();
    ticksWithoutWindow = window != lastWindowSeen ? 0 : ticksWithoutWindow + 1;
    lastWindowSeen = window;
    trainerStatus.probeAlive = ticksWithoutWindow < 45;   // about 1.5 s at 30 Hz
    trainerStatus.nmse = link->probe.nmse.load();

    if (restartRequested.exchange (false) || trainerStatus.state.load() == trainerOff)
    {
        trainerBase = getSliderValues();
        trainerStep = 0.06f;
        orderPosition = 0;
        sweepImproved = false;
        trainerStatus.evaluations = 0;
        trainerStatus.state = trainerStarting;
        trainerStatus.step = trainerStep;
        smoothedNmse = link->probe.nmse.load();
        waitUntilWindow = window + settleWindows;   // baseline: the current settings, settled
        return;
    }

    if (window < waitUntilWindow || ! link->probe.valid.load())
        return;

    const float nmse = link->probe.nmse.load();
    trainerStatus.evaluations.fetch_add (1);
    trainerStatus.nmse = nmse;
    smoothedNmse += 0.15f * (nmse - smoothedNmse);   // single windows can spike, so judge drift on a smoothed value

    switch (trainerStatus.state.load())
    {
        case trainerStarting:
            best = nmse;
            trainerStatus.state = trainerAdapting;
            trainerStatus.step = trainerStep;
            beginTap();
            break;

        case trainerAdapting:
        {
            const int tap = trainOrder[orderPosition];
            const bool improved = nmse < best * 0.995f - 1.0e-5f;

            if (improved)
            {
                best = nmse;
                trainerBase[(size_t) tap] = trialValue;
                movedThisTap = sweepImproved = true;

                const float next = juce::jlimit (-0.6f, 0.6f, trialValue + (float) direction * trainerStep);
                if (juce::approximatelyEqual (next, trialValue))
                    advanceTap();
                else
                {
                    trialValue = next;   // keep going the same way while it helps
                    applyTrial();
                }
            }
            else if (! movedThisTap && ! triedOpposite)
            {
                triedOpposite = true;
                direction = -direction;
                trialValue = juce::jlimit (-0.6f, 0.6f, trainerBase[(size_t) tap] + (float) direction * trainerStep);
                applyTrial();
            }
            else
            {
                setSlider (tap, trainerBase[(size_t) tap]);   // put back the last good value
                advanceTap();
            }

            trainerStatus.step = trainerStep;
            break;
        }

        case trainerConverged:
            // The link changed under us (channel, rate, noise...): train again.
            if (smoothedNmse > convergedNmse * 4.0f + 0.008f)
                restartRequested = true;
            break;

        default:
            break;
    }
}
