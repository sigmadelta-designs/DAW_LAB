#include "DdrChip.h"
#include "DdrChipEditor.h"

static const char* const dqSkewIds[DdrBus::dqWidth] =
    { "dqskew0", "dqskew1", "dqskew2", "dqskew3", "dqskew4", "dqskew5", "dqskew6", "dqskew7" };

DdrChip::DdrChip (std::shared_ptr<DdrLinkSettings> linkSettings)
    : BuiltInProcessorBase (id, "DDR Chip", std::move (linkSettings), createLayout(), DdrBus::numChannels)
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DdrChip::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using P = juce::ParameterID;

    layout.add (std::make_unique<juce::AudioParameterChoice> (P { "chipnumber", 1 }, "Chip number",
                                                              juce::StringArray { "Chip 0", "Chip 1", "Chip 2", "Chip 3" }, 0));
    // A real Vref training register: the slicing threshold for DQ, relative to the differential swing.
    // 0 is centred/ideal; a mis-trained Vref is a real, common DDR bring-up bug.
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "vref", 1 }, "Vref (DQ slice level)",
                                                             juce::NormalisableRange<float> (-0.9f, 0.9f, 0.01f), 0.0f));

    // On-die termination: the mode-register-programmed target (ohms) and how far ZQ calibration against
    // the external 240 ohm reference actually landed from it. 60 ohm matches DdrBus::nominalImpedanceOhms
    // (a trained, matched default); a wrong target or a bad calibration both shift the chip's *effective*
    // termination and so the reflection it sees - see the long comment in DdrChip.h.
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "odt", 1 }, "ODT target (ohm)",
                                                             juce::NormalisableRange<float> (20.0f, 300.0f, 1.0f), 60.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "zqerror", 1 }, "ZQ cal error (%)",
                                                             juce::NormalisableRange<float> (-50.0f, 50.0f, 0.5f), 0.0f));

    // CS/CA training: how many UI late this chip's own chip-select sampling lags the real signal. 0 is
    // trained.
    layout.add (std::make_unique<juce::AudioParameterFloat> (P { "cstiming", 1 }, "CS timing error (UI)",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

    // Per-bit DQ deskew (write levelling when all eight agree; per-bit write-DQ training when they don't).
    for (int b = 0; b < DdrBus::dqWidth; ++b)
        layout.add (std::make_unique<juce::AudioParameterFloat> (P { dqSkewIds[b], 1 }, "DQ" + juce::String (b) + " skew (UI)",
                                                                 juce::NormalisableRange<float> (0.0f, 0.5f, 0.01f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (P { "dfe", 1 }, "DFE enabled", false));
    return layout;
}

void DdrChip::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    prevDqsP = 0.0f;
    prevCmd = DdrBus::nop;
    prevSelected = false;
    ringIndex = 0;
    csRing.fill (0.0f);
    for (auto& ring : dqRing)
        ring.fill (0.0f);
    previousDecision.fill (-1.0f);
    status.wordsWritten = 0;
    status.wordsRead = 0;
    status.everSelected = false;
    std::fill (memory.begin(), memory.end(), (juce::uint8) 0);
}

void DdrChip::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumChannels() < DdrBus::numChannels)
        return;

    const int numSamples = buffer.getNumSamples();
    const int chipNumber = juce::jlimit (0, DdrBus::maxChips - 1, (int) getParam ("chipnumber"));
    const float vref = getParam ("vref");

    // ZQ calibration trims the on-die termination against an external 240 ohm reference; a calibration
    // error leaves the chip's *actual* termination off from whatever the mode register asked for. That
    // mismatch against the bus's nominal impedance is a genuine reflection coefficient, computed the same
    // way a real transmission line's would be (see DdrBus::nominalImpedanceOhms).
    const float odtOhms = getParam ("odt");
    const float zqErrorPct = getParam ("zqerror");
    const float effectiveOdt = odtOhms * (1.0f + zqErrorPct / 100.0f);
    const float gamma = (effectiveOdt - (float) DdrBus::nominalImpedanceOhms)
                       / (effectiveOdt + (float) DdrBus::nominalImpedanceOhms);

    const bool dfeOn = getParam ("dfe") > 0.5f;

    const double unitIntervalSamples = juce::jmax (1.0, link->samplesPerUI (sampleRate));
    const int reflectionLagSamples = (int) std::lround (unitIntervalSamples);

    // CS/CA training error: how many samples late this chip's own chip-select sampling lags the real
    // signal - untrained/mistrained CS timing always reads as *late* in this simplified, look-back-only
    // model.
    const int csTimingSamples = (int) std::lround (juce::jlimit (0.0f, 1.0f, getParam ("cstiming")) * unitIntervalSamples);

    int dqSkewSamples[DdrBus::dqWidth];
    for (int b = 0; b < DdrBus::dqWidth; ++b)
        dqSkewSamples[b] = (int) std::lround (juce::jlimit (0.0f, 0.5f, getParam (dqSkewIds[b])) * unitIntervalSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        ringIndex = (ringIndex + 1) & (ringSize - 1);
        csRing[(size_t) ringIndex] = buffer.getSample (DdrBus::csChannel (chipNumber), i);
        for (int b = 0; b < DdrBus::dqWidth; ++b)
            dqRing[(size_t) b][(size_t) ringIndex] = buffer.getSample (DdrBus::dqChannel (b), i);

        const bool selected = ringLookup (csRing, csTimingSamples) > 0.5f;
        const int cmdValue = (int) std::lround (buffer.getSample (DdrBus::cmd, i));
        const int addrValue = (int) std::lround (buffer.getSample (DdrBus::addr, i));
        const float dqsP = buffer.getSample (DdrBus::dqsP, i);
        const bool dqsRisingEdge = prevDqsP <= 0.0f && dqsP > 0.0f;

        if (selected)
            status.everSelected = true;

        // Write: a real signal-level capture, clocked by the DQS edge, each bit sliced against Vref -
        // the part of this stage that a channel/eye-scope/jitter-scope in front of it actually exercises.
        if (selected && cmdValue == DdrBus::write && dqsRisingEdge)
        {
            juce::uint8 captured = 0;
            for (int b = 0; b < DdrBus::dqWidth; ++b)
            {
                const float raw = ringLookup (dqRing[(size_t) b], dqSkewSamples[b]);

                // A mismatched termination reflects a scaled copy of the signal back after roughly one
                // unit interval's round trip - genuine inter-symbol interference, the same mechanism
                // TraceReflections models on the SerDes side, simplified here to a single tap.
                const float echo = ringLookup (dqRing[(size_t) b], dqSkewSamples[b] + reflectionLagSamples);
                float level = raw + gamma * echo;

                // A single shared-tap DFE: cancel the known reflection contribution using this chip's own
                // prior decision for this bit - the same sign-sign principle (if not the full 4-tap
                // reality) as DDR5's real DQ receiver DFE and this project's existing SerDes receiver.
                if (dfeOn)
                    level -= gamma * previousDecision[(size_t) b];

                const bool bit = level > vref;
                previousDecision[(size_t) b] = bit ? 1.0f : -1.0f;
                if (bit)
                    captured |= (juce::uint8) (1 << b);
            }

            memory[(size_t) (addrValue & (memoryWords - 1))] = captured;
            status.wordsWritten.fetch_add (1);
        }

        // Read: phase-1 protocol-level handoff (see DdrController.h) - fires once, on the transition into
        // a read command while selected, not every sample the command value is held.
        if (selected && cmdValue == DdrBus::read && ! (prevSelected && prevCmd == DdrBus::read))
        {
            auto& rb = link->readback[(size_t) chipNumber];
            rb.data = memory[(size_t) (addrValue & (memoryWords - 1))].load();
            rb.answeredAddr = addrValue;
            rb.sequence.fetch_add (1);
            rb.valid = true;
            status.wordsRead.fetch_add (1);
        }

        prevDqsP = dqsP;
        prevCmd = cmdValue;
        prevSelected = selected;
    }

    // Deliberately does not touch the buffer any further: a real multi-drop bus has every chip just
    // listen to the same CA/CS/CK/DQ signals and pass them straight on, not consume or modify them - this
    // stage only ever *reads* the bus (reads answer through the phase-1 shared-state handoff in
    // DdrLinkSettings, not by driving DQ/DQS back out - see DdrController.h). That's what lets a chain of
    // several DdrChip stages (one per rank, each with its own "chip number") sit one after another and
    // all see the identical bus, the way real chips on a shared bus actually work.
}

juce::AudioProcessorEditor* DdrChip::createEditor()
{
    return new DdrChipEditor (*this);
}
