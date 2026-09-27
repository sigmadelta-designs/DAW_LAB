#include "GeneratorEditor.h"

GeneratorEditor::GeneratorEditor (SignalGenerator& generatorToEdit)
    : SimpleParamEditor (generatorToEdit)
{
    using D = ControlTip::Domain;

    // Only present when this is the standalone plugin (it drives the shared link); the built-in stage
    // inside DAW_LAB has no such parameters - the app's own rate strip does that instead.
    if (generatorToEdit.getParameterTree().getParameter ("linerate") != nullptr)
    {
        addHeading ("Link (this plugin sets it)");
        addSlider ("linerate", "Line rate (GBd)", D::dut,
            "The link's real symbol rate, in Gigabaud - a property of the transmitter and receiver you're "
            "testing, not of this simulation. There is no DAW_LAB rate strip in a plugin host, so the "
            "generator carries it instead and shares it with the other DAW_LAB stages in this chain.");
        addCombo ("modulation", "Modulation", { "NRZ", "PAM4" }, D::dut,
            "NRZ (one bit per symbol) or PAM4 (two bits per symbol, four levels) - the real signalling "
            "format the transmitter and receiver are built for.");
    }

    addHeading ("Transmitter");
    addSlider ("amplitude", "Amplitude (per leg)", D::dut,
        "The transmitter's real output swing on each leg, before any equalization - a TX driver's swing register.");
    addSlider ("rise", "Edge time (UI)", D::dut,
        "The transmitter's real edge rate, in unit intervals - a TX driver's rise/fall time. A faster "
        "(smaller) edge pushes more energy to high frequency, the way a real high-speed driver's "
        "bandwidth would.");

    addHeading ("Test pattern & stress conditions");
    {
        juce::StringArray patterns;
        for (int i = 0; i < PrbsGenerator::numPatterns; ++i)
            patterns.add (PrbsGenerator::getName (i));
        addCombo ("pattern", "Pattern", patterns, D::simulation,
            "Which test pattern this run transmits (a PRBS order, or a plain clock) - a stimulus choice "
            "for this test, the same as picking a pattern on a real BERT/pattern generator. Not a property "
            "of the transmitter itself.");
    }
    addCombo ("drive", "Drive", { "Differential (R = +, L = -)", "+ leg only (L quiet)", "- leg only (R quiet)", "Common-mode (R = L)" },
        D::simulation,
        "Which leg(s) actually carry signal - normally both, differentially. Forcing +-only, --only or "
        "common-mode is an artificial test condition, useful for checking how a receiver responds to "
        "something other than a clean differential signal.");
    addSlider ("rj", "Random jitter, RJ (UI rms)", D::simulation,
        "Random jitter added to each edge (Gaussian, UI rms) - an impairment you inject to stress the "
        "receiver's clock recovery, not a transmitter property.");
    addSlider ("sjamp", "Sinusoidal jitter, SJ (UI pk-pk)", D::simulation,
        "Amplitude of a sinusoidal jitter tone added to every edge - an injected stress condition. Sweep "
        "this against SJ frequency to trace out a jitter-tolerance curve, the standard test methodology "
        "for characterising a CDR's tracking range.");
    addSlider ("sjfreq", "SJ frequency (MHz, real-world)", D::simulation,
        "Frequency of that sinusoidal jitter tone - part of the same injected stress condition as SJ amplitude.");
    addSlider ("dcd", "Duty-cycle distortion, DCD (UI pk-pk)", D::simulation,
        "Duty-cycle distortion added to the pattern (rising edges late, falling edges early) - an injected "
        "impairment, not a transmitter property.");

    finishLayout();
}
