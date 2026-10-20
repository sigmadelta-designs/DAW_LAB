#pragma once

#include <juce_core/juce_core.h>

// Shared channel layout for one DDR byte lane - a small, deliberately simplified command/address/data
// bus, not a literal JEDEC DDR4/DDR5 CA mapping. It models the concepts that matter for signal-integrity
// and protocol training (chip select, command type, target address, data, and the strobe/clock that
// times everything), not every real command's exact multi-cycle encoding.
//
// Chip select is modelled as one real wire per supported chip/rank (one-hot), the way a real DIMM
// actually wires CS - not as an encoded value - so a DdrChip's own "chip number" parameter is literally
// "which CS wire am I strapped to", the same kind of choice a real package/rank-select pin is.
//
// Clock and the data strobe are true differential pairs (CK+/-, DQS+/-), matching how real DDR4/DDR5
// wires them and matching this project's existing convention (R = +, L = -) of modelling the actual
// differential signal rather than abstracting it away.
namespace DdrBus
{
    constexpr int maxChips = 4;   // one DIMM's worth of ranks, for a first version

    constexpr int ckP = 0, ckN = 1;
    constexpr int cs0 = 2;                        // cs0 .. cs0 + maxChips - 1, one wire per chip/rank
    constexpr int cmd = cs0 + maxChips;            // 6
    constexpr int addr = cmd + 1;                  // 7
    constexpr int dq0 = addr + 1;                  // 8
    constexpr int dqWidth = 8;                     // one byte lane
    constexpr int dqsP = dq0 + dqWidth;            // 16
    constexpr int dqsN = dqsP + 1;                 // 17
    constexpr int numChannels = dqsN + 1;          // 18

    inline int csChannel (int chip) noexcept { return cs0 + chip; }
    inline int dqChannel (int bit) noexcept { return dq0 + bit; }

    // Carried as a value on the `cmd` channel, one command per bus cycle - a simplified stand-in for
    // DDR's real multi-cycle, multi-pin command bus, enough to drive a write/read/idle protocol for
    // training purposes without modelling every real DDR5 command and timing parameter.
    enum Command { nop = 0, activate = 1, write = 2, read = 3, precharge = 4, refresh = 5 };

    inline const char* commandName (int c) noexcept
    {
        static const char* names[] = { "NOP", "ACT", "WR", "RD", "PRE", "REF" };
        return names[juce::jlimit (0, 5, c)];
    }
}
