#include "DdrController.h"

DdrController::DdrController (std::shared_ptr<DdrLinkSettings> linkSettings)
    : BuiltInProcessorBase (id, "DDR Controller", std::move (linkSettings), createLayout(), DdrBus::numChannels)
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DdrController::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using P = juce::ParameterID;

    layout.add (std::make_unique<juce::AudioParameterChoice> (P { "targetchip", 1 }, "Target chip",
                                                              juce::StringArray { "Chip 0", "Chip 1", "Chip 2", "Chip 3" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (P { "pattern", 1 }, "BIST pattern",
                                                              juce::StringArray { "All zero", "All one", "Address as data", "Checkerboard" }, 2));
    layout.add (std::make_unique<juce::AudioParameterInt> (P { "burstwords", 1 }, "Sweep length (words)", 4, 256, 64));
    layout.add (std::make_unique<juce::AudioParameterInt> (P { "caslatency", 1 }, "CAS latency (UI)", 1, 16, 4));
    layout.add (std::make_unique<juce::AudioParameterBool> (P { "running", 1 }, "BIST running", true));
    return layout;
}

juce::uint8 DdrController::patternByte (int pattern, int address) noexcept
{
    switch (pattern)
    {
        case allZero:      return 0x00;
        case allOne:        return 0xff;
        case checkerboard: return (address % 2 == 0) ? 0xaa : 0x55;
        case addressAsData:
        default:            return (juce::uint8) (address & 0xff);
    }
}

void DdrController::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    samplePos = 0.0;
    phase = writing;
    sweepIndex = 0;
    latencyCountdown = 0;
    pollCountdown = 0;
    currentCmd = DdrBus::nop;
    currentAddr = 0;
    currentWriteByte = 0;
    status.wordsChecked = 0;
    status.errors = 0;
}

void DdrController::advanceSequence()
{
    const int pattern = (int) getParam ("pattern");
    const int burstWords = juce::jlimit (4, 256, (int) getParam ("burstwords"));
    const int casLatency = juce::jlimit (1, 16, (int) getParam ("caslatency"));

    switch (phase)
    {
        case writing:
            // The target chip is only latched when a command actually issues, not re-read live every UI -
            // changing it mid-transaction (e.g. while readWait is still polling for a reply) must not
            // retarget an in-flight read to a different chip's readback slot than the one actually asked.
            currentChip = juce::jlimit (0, DdrBus::maxChips - 1, (int) getParam ("targetchip"));
            currentCmd = DdrBus::write;
            currentAddr = sweepIndex;
            currentWriteByte = patternByte (pattern, sweepIndex);
            if (++sweepIndex >= burstWords) { sweepIndex = 0; phase = readIssue; }
            break;

        case readIssue:
            currentChip = juce::jlimit (0, DdrBus::maxChips - 1, (int) getParam ("targetchip"));
            currentCmd = DdrBus::read;
            currentAddr = sweepIndex;
            latencyCountdown = casLatency;
            phase = readWait;
            break;

        case readWait:
        {
            currentCmd = DdrBus::nop;

            // The CAS latency is a minimum wait, not the actual signal for "the answer is ready" - a
            // chip's processBlock for the block this read lives in runs strictly after this stage's, so
            // even the minimum wait can elapse before the chip has processed it (see the long comment on
            // DdrLinkSettings::Readback). Keep polling for a reply tagged with *this* address instead of
            // trusting the countdown alone, bounded by maxPollUI so a chip that genuinely never answers
            // (wrong target, never selected) doesn't hang the sequencer forever.
            if (latencyCountdown > 0)
            {
                --latencyCountdown;
                break;
            }

            auto& rb = link->readback[(size_t) currentChip];
            const bool answered = rb.valid.load() && rb.answeredAddr.load() == sweepIndex;
            if (! answered && pollCountdown < maxPollUI)
            {
                ++pollCountdown;
                break;
            }

            const juce::uint8 want = patternByte (pattern, sweepIndex);
            // No matching reply within the poll budget reads back as "nothing came back" - a mismatch
            // against any expected pattern, the same as a real bus read timeout would be.
            const juce::uint8 got = answered ? rb.data.load() : (juce::uint8) ~want;

            status.wordsChecked.fetch_add (1);
            if (got != want)
                status.errors.fetch_add (1);

            pollCountdown = 0;
            if (++sweepIndex >= burstWords) { sweepIndex = 0; phase = writing; }
            else phase = readIssue;
            break;
        }
    }

    status.lastCommand = currentCmd;
    status.lastAddr = currentAddr;
}

void DdrController::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < DdrBus::numChannels)
        return;

    uiLength = juce::jmax (3.0, link->samplesPerUI (sampleRate.load()));
    const bool running = getParam ("running") > 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        if (samplePos <= 0.0)
        {
            if (running)
                advanceSequence();
            samplePos = uiLength;
        }

        const double uiFrac = 1.0 - samplePos / uiLength;   // 0 at the start of this UI, towards 1 at the end
        const bool firstHalf = uiFrac < 0.5;

        const float ck = firstHalf ? 1.0f : -1.0f;
        buffer.setSample (DdrBus::ckP, i, ck);
        buffer.setSample (DdrBus::ckN, i, -ck);

        for (int c = 0; c < DdrBus::maxChips; ++c)
            buffer.setSample (DdrBus::csChannel (c), i,
                              (currentCmd != DdrBus::nop && c == currentChip) ? 1.0f : 0.0f);

        buffer.setSample (DdrBus::cmd, i, (float) currentCmd);
        buffer.setSample (DdrBus::addr, i, (float) currentAddr);

        const bool isWrite = currentCmd == DdrBus::write;
        for (int b = 0; b < DdrBus::dqWidth; ++b)
            buffer.setSample (DdrBus::dqChannel (b), i, isWrite ? (((currentWriteByte >> b) & 1) ? 1.0f : -1.0f) : 0.0f);

        const float dqs = isWrite ? (firstHalf ? 1.0f : -1.0f) : 0.0f;
        buffer.setSample (DdrBus::dqsP, i, dqs);
        buffer.setSample (DdrBus::dqsN, i, -dqs);

        samplePos -= 1.0;
    }
}
