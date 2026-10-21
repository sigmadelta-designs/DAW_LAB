#pragma once

#include "BuiltInProcessor.h"
#include "DdrBus.h"
#include "DdrLinkSettings.h"
#include <array>

// The memory chip side of a DDR byte lane. "Chip number" is literally which CS wire this instance is
// strapped to (DdrBus::csChannel(chipNumber)) - the same kind of choice a real package/rank-select pin
// is, not a logical ID the simulation invents. Only responds while that wire is asserted; everything else
// on the bus (other chips' CS lines, the rest of the command) is ignored, the same as real hardware.
//
// Write capture is genuine signal-level simulation: a DQS edge is the capture clock, and each DQ bit is
// sliced against a trainable Vref, so a channel stage (loss, reflections, crosstalk) and an eye/jitter
// scope placed on this bus between the controller and this stage mean something real. See DdrController.h
// for why reads don't yet drive DQ/DQS back out the same way (phase 1 / phase 2 split).
class DdrChip : public BuiltInProcessorBase<DdrLinkSettings>
{
public:
    static constexpr const char* id = "lab.ddrchip";
    static constexpr int memoryWords = 4096;   // plenty for any BIST sweep DdrController can run

    explicit DdrChip (std::shared_ptr<DdrLinkSettings> linkSettings);

    void prepareToPlay (double sampleRate, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    // The array's contents, for a GUI to show live - address x DQ-bit-position, the most direct "rows and
    // columns" reading of what is actually stored (not a model of a real DRAM's row/column/bank addressing,
    // which this simulation doesn't represent - see DdrChipEditor.h). Thread-safe: the audio thread writes
    // with ordinary stores through std::atomic, the message thread reads the same way.
    juce::uint8 getMemoryByte (int address) const noexcept { return memory[(size_t) (address & (memoryWords - 1))].load(); }

    struct Status
    {
        std::atomic<juce::uint64> wordsWritten { 0 }, wordsRead { 0 };
        std::atomic<bool> everSelected { false };
    } status;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    float prevDqsP = 0.0f;
    int prevCmd = DdrBus::nop;
    bool prevSelected = false;

    std::array<std::atomic<juce::uint8>, memoryWords> memory {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DdrChip)
};
