#include "DdrChip.h"
#include "DdrChipEditor.h"

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
    return layout;
}

void DdrChip::prepareToPlay (double, int)
{
    prevDqsP = 0.0f;
    prevCmd = DdrBus::nop;
    prevSelected = false;
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

    for (int i = 0; i < numSamples; ++i)
    {
        const bool selected = buffer.getSample (DdrBus::csChannel (chipNumber), i) > 0.5f;
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
                if (buffer.getSample (DdrBus::dqChannel (b), i) > vref)
                    captured |= (juce::uint8) (1 << b);

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
