#include "DdrLossyChannelEditor.h"

DdrLossyChannelEditor::DdrLossyChannelEditor (DdrLossyChannel& channelToEdit)
    : DdrSimpleParamEditor (channelToEdit, 420)
{
    using D = ControlTip::Domain;

    addSlider ("swing", "DQ/DQS swing (% of ideal)", D::dut,
        "How much the real board trace or connector attenuates DQ/DQS before this chip's Vref slices it - "
        "the same kind of number as the SerDes side's Loss at Nyquist, just for a parallel bus's data/strobe "
        "lines rather than a serial link. 100% is a direct-attach loopback; anything lower is a real channel "
        "a trained Vref has to account for.");

    finishLayout();
}
