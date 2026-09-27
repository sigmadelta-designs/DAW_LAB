#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A hover tip for one control says three things: its name, whether it stands in for a real knob or
// register on the transmitter/channel/receiver you'd be testing (a "DUT control") or a test-bench setting
// - what stimulus you chose, an impairment you dialled in to stress the link, or something about this
// simulation's own fidelity/display, none of which exist on the physical hardware (a "Simulation
// control") - and why you'd actually reach for it. DawLabLookAndFeel renders the two kinds in different
// colours so they're distinguishable at a glance, not just by the text.
//
// Rule of thumb used throughout this project: if a real SerDes chip, cable or board has this as a
// property or register, it's DUT (line rate, FFE taps, CTLE boost, trace length, termination resistors).
// If it only exists because we're running a simulation - which test pattern to run, an injected
// impairment (noise, jitter), or a knob on the eye-scope/measurement display itself - it's Simulation.
namespace ControlTip
{
    enum class Domain { dut, simulation };

    // Tooltips are plain strings in JUCE, so the domain and title are packed into the string with a
    // control character that will not appear in ordinary text, and unpacked again by parse().
    inline constexpr juce::juce_wchar separator = 0x1F;   // ASCII unit separator

    inline juce::String make (const juce::String& title, Domain domain, const juce::String& reason)
    {
        return juce::String::charToString (domain == Domain::dut ? 'D' : 'S')
                 + juce::String::charToString (separator) + title
                 + juce::String::charToString (separator) + reason;
    }

    struct Parts { Domain domain; juce::String title, reason; };

    // False for a tooltip that wasn't built with make() (a plain, uncoded string) - the caller then
    // falls back to showing it as ordinary single-block text.
    inline bool parse (const juce::String& tooltip, Parts& out)
    {
        if (tooltip.length() < 2 || tooltip[1] != separator)
            return false;

        out.domain = tooltip[0] == 'D' ? Domain::dut : Domain::simulation;
        const auto rest = tooltip.substring (2);
        out.title = rest.upToFirstOccurrenceOf (juce::String::charToString (separator), false, false);
        out.reason = rest.fromFirstOccurrenceOf (juce::String::charToString (separator), false, false);
        return true;
    }

    inline juce::Colour colourFor (Domain domain) noexcept
    {
        // DUT: a cool blue, for "a real device's own control". Simulation: an amber, for "a test-bench
        // condition or a measurement/display setting" - not something you'd find on the physical part.
        return domain == Domain::dut ? juce::Colour (0xff5aa9e6) : juce::Colour (0xffe6a13a);
    }

    inline const char* tagFor (Domain domain) noexcept { return domain == Domain::dut ? "DUT" : "SIM"; }
}
