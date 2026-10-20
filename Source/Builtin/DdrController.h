#pragma once

#include "BuiltInProcessor.h"
#include "DdrBus.h"
#include "DdrLinkSettings.h"

// The memory controller side of a DDR byte lane: drives CK/CS/CMD/ADDR always, and DQ/DQS during write
// beats (one data beat per bus cycle for now - DDR's real double-pumped-per-clock timing is a later
// refinement, noted in the README). Runs a small, continuously-repeating built-in self-test (BIST): write
// a pattern across an address sweep, then read it back and compare, counting mismatches live - the same
// "run forever, show a live error count" idea as the SerDes side's BitCompare, just for memory words
// instead of a serial bitstream.
//
// Phase 1 / Phase 2 (see the README and DdrChip.h for the full explanation): this stage drives the write
// path for real - CMD/ADDR/CS/DQ/DQS are genuine simulated signals a channel stage and an eye/jitter scope
// can sit on - but a read's answer is fetched directly from DdrLinkSettings::Readback (a same-process
// handoff) rather than driven back out through DQ/DQS and the simulated channel. That electrical loopback
// is a deliberate next step, not an oversight: it needs a feedback edge in the host's plugin graph, which
// doesn't exist yet.
class DdrController : public BuiltInProcessorBase<DdrLinkSettings>
{
public:
    static constexpr const char* id = "lab.ddrcontroller";

    explicit DdrController (std::shared_ptr<DdrLinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // No editor yet - this pass is about getting the protocol/chip-select/BIST logic right and verified;
    // a GUI (and hooking the Eye/Jitter Scopes up to this bus) is the next step, not an oversight.

    double getSampleRate() const noexcept { return sampleRate.load(); }

    // Simple, repeatable test patterns - not real March-test fault coverage, just enough variety to
    // shake out stuck bits vs. address-dependent ones.
    enum Pattern { allZero = 0, allOne = 1, addressAsData = 2, checkerboard = 3 };

    struct Status
    {
        std::atomic<juce::uint64> wordsChecked { 0 }, errors { 0 };
        std::atomic<int> lastCommand { DdrBus::nop };
        std::atomic<int> lastAddr { 0 };
        std::atomic<int> targetChip { 0 };
    } status;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static juce::uint8 patternByte (int pattern, int address) noexcept;

    void advanceSequence();   // called once per bus cycle: decide the next command

    std::atomic<double> sampleRate { 48000.0 };
    double samplePos = 0.0;      // position within the current UI, in samples
    double uiLength = 10.0;      // samplesPerUI, refreshed each block

    // BIST sequencer state: write the whole sweep, then read-verify the whole sweep, forever.
    enum Phase { writing, readIssue, readWait } phase = writing;
    int sweepIndex = 0;
    int latencyCountdown = 0;     // the CAS latency's minimum wait, in UI, before even checking for a reply
    int pollCountdown = 0;        // UI spent polling for a reply tagged with this address, after the minimum
    static constexpr int maxPollUI = 500;   // gives up and records "no response" past this - something is
                                             // genuinely wrong (wrong chip targeted, chip never answering),
                                             // not just a slightly-too-short CAS latency

    int currentCmd = DdrBus::nop, currentAddr = 0, currentChip = 0;
    juce::uint8 currentWriteByte = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DdrController)
};
