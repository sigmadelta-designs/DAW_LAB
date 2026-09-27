#include "LossyEditor.h"

LossyEditor::LossyEditor (LossyChannel& channelToEdit)
    : SimpleParamEditor (channelToEdit, 420)
{
    using D = ControlTip::Domain;

    addToggle ("enabled", "Channel enabled", D::dut,
        "Puts the physical channel in or out of the path - like swapping in a direct-attach loopback "
        "instead of the real cable/backplane, a real bring-up step.");
    addSlider ("loss", "Loss at Nyquist (dB)", D::dut,
        "How much the real cable or backplane attenuates the signal at the link's Nyquist frequency (half "
        "the line rate) - the standard number on a real channel's datasheet or measured insertion-loss plot.");

    finishLayout();
}
