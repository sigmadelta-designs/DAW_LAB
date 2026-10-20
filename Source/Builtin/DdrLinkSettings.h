#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "DdrBus.h"

// The one "link" every DDR stage on a byte lane shares: data rate, and the chip readback channel used
// for the write/phase-1 BIST loop (see DdrChip.h for why reads hand back data this way instead of
// driving it through the simulated channel). Same 1e6 real-time-to-audio-rate scaling convention as the
// SerDes side's LinkSettings - DDR data rates (hundreds to low thousands of MT/s) are the same order of
// magnitude as the GBd rates already handled there.
class DdrLinkSettings
{
public:
    static constexpr double simulationScale = 1.0e6;
    static constexpr double minMTs = 400.0, maxMTs = 8800.0;   // roughly DDR3 to DDR5 territory

    std::atomic<float> dataRateMTs { 3200.0f };

    double busHz() const noexcept { return dataRateMTs.load() * 1.0e6 / simulationScale; }

    double samplesPerUI (double sampleRate) const noexcept
    {
        return juce::jmax (3.0, sampleRate / busHz());
    }

    // Each chip writes its own slot when it answers a read - the phase-1 stand-in for driving DQ back
    // through the simulated channel (see the "Phase 1 / Phase 2" note in DdrChip.h and the README).
    //
    // `answeredAddr` matters more than it looks: several bus cycles can happen inside one audio block
    // (a typical UI is a handful of samples; a block is tens to hundreds), and a chip's processBlock for
    // a given block runs strictly after the controller's in a linear chain - so a controller waiting on
    // a fixed "CAS latency" UI countdown alone can resolve its compare before the chip has even processed
    // that block's read yet, silently comparing against a stale answer from an earlier address. Tagging
    // the answer with which address it's actually for lets the controller confirm it got the response to
    // *this* read, not just "a" response, regardless of how UI-per-block happens to line up.
    struct Readback
    {
        std::atomic<juce::uint8> data { 0 };
        std::atomic<juce::uint32> sequence { 0 };   // bumped every time `data` is written, so a reader
                                                     // can tell a fresh answer from a stale one
        std::atomic<int> answeredAddr { -1 };
        std::atomic<bool> valid { false };          // true once any chip has ever answered
    };
    Readback readback[DdrBus::maxChips];
};
