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
//
// This stage also owns the chip's own trainable physical properties, each with a genuine signal-level
// consequence on that same write-capture path - the same "a mis-trained setting is a measurable bug, not
// just a wrong number" discipline as the rest of DAW_LAB:
//   - odt / zqerror: the chip's on-die termination and how far ZQ calibration (against the external 240
//     ohm reference) actually landed from the target the mode register asked for. The mismatch between
//     the resulting *effective* termination and the bus's nominal impedance becomes a real reflection
//     coefficient (Gamma, see DdrBus::nominalImpedanceOhms), fed back into the chip's own capture as a
//     single-tap, ~1-UI-delayed echo - a simplified stand-in for a real multi-bounce transmission line.
//   - cstiming: CS/CA training error, modelled as how many samples late this chip's own CS sampling lags
//     the real signal (untrained/mistrained CS timing is always a *late* fix in this simplified model, to
//     avoid needing to simulate the future).
//   - dqskew0..7: per-bit DQ deskew (also look-back only). Set all eight equally and it models write
//     levelling left undone (one shared DQS-to-CK/byte-lane skew); set only some of them and it models
//     per-bit write-DQ deskew training left incomplete - a classic "always wrong on this one pin" bug.
//   - dfe: a single shared-tap decision-feedback equalizer that cancels the *known* reflection using the
//     chip's own prior decision, same sign-sign-DFE idea (if not the full 4-tap reality) as DDR5's real DQ
//     receiver DFE and this project's existing SerDes receiver.
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

    // Generous history depth: needs to cover roughly 1.5 UI (per-bit deskew look-back plus the ~1-UI
    // reflection echo) even at the slowest supported data rate and a high audio sample rate (e.g. 400
    // MT/s at 192 kHz is 480 samples/UI) with headroom to spare.
    static constexpr int ringSize = 2048;

    float ringLookup (const std::array<float, ringSize>& ring, int samplesBack) const noexcept
    {
        return ring[(size_t) ((ringIndex - samplesBack) & (ringSize - 1))];
    }

    double sampleRate = 48000.0;
    float prevDqsP = 0.0f;
    int prevCmd = DdrBus::nop;
    bool prevSelected = false;

    int ringIndex = 0;
    std::array<float, ringSize> csRing {};
    std::array<std::array<float, ringSize>, DdrBus::dqWidth> dqRing {};
    std::array<float, DdrBus::dqWidth> previousDecision {};   // for the DFE, the last sliced +-1 decision

    std::array<std::atomic<juce::uint8>, memoryWords> memory {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DdrChip)
};
