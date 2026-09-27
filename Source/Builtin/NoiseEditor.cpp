#include "NoiseEditor.h"

NoiseEditor::NoiseEditor (NoiseInjector& injectorToEdit)
    : SimpleParamEditor (injectorToEdit, 460)
{
    using D = ControlTip::Domain;

    juce::StringArray types;
    for (int i = 0; i < NoiseInjector::numTypes; ++i)
        types.add (NoiseInjector::getTypeName (i));
    addCombo ("type", "Noise type", types, D::simulation,
        "Which kind of interference to add. The whole point of this stage is to inject an artificial "
        "impairment so you can see how the rest of the chain handles it - it isn't a property of any real "
        "hardware in the link.");
    addSlider ("level", "Level (RMS, FS; DC = offset)", D::simulation,
        "How much of it to add (RMS, relative to full scale; for DC noise, the offset itself) - how hard "
        "you're stressing the link with this injected impairment.");
    addCombo ("injection", "Injection", { "Differential", "Common-mode" }, D::simulation,
        "Differential adds it to the signal an ideal receiver actually sees; common-mode adds the same "
        "amount to both legs, which an ideal differential receiver rejects entirely. A choice of which "
        "coupling path you're testing, not a property of the interference itself.");
    addToggle ("invert", "Invert polarity (DC / sine / crosstalk)", D::simulation,
        "Flips the polarity of the injected DC offset, sine tone, or crosstalk. Lets you check the link "
        "responds the same either way - a test-setup choice.");
    addSlider ("freq", "Sine frequency (MHz, real-world)", D::simulation,
        "The injected sine tone's frequency - part of the same artificial stress condition as the Sine noise type.");
    addSlider ("aggoffset", "Aggressor rate offset (%)", D::simulation,
        "Offsets the crosstalk aggressor's own bit rate from the link's, so it's uncorrelated with the "
        "wanted signal - a property of this injected interference, not of any real hardware.");

    finishLayout();
}
