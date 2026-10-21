#pragma once

#include "BuiltInProcessor.h"
#include "DdrBus.h"
#include "DdrLinkSettings.h"

// A minimal placeholder channel for the DDR bus - the DDR-side counterpart to the SerDes side's
// LossyChannel, and just as deliberately a placeholder (see its header comment). Attenuates only DQ and
// DQS: those are the signals an eye actually forms on and a Vref is trained against, so this is enough to
// make "a real channel has loss, and Vref has to be trained for the swing it actually sees, not assumed"
// a genuine, measurable scenario. CK/CS/CMD/ADDR pass through unattenuated - modelling their own loss and
// a fly-by/multi-drop topology is future work, not forgotten.
class DdrLossyChannel : public BuiltInProcessorBase<DdrLinkSettings>
{
public:
    static constexpr const char* id = "lab.ddrlossy";

    explicit DdrLossyChannel (std::shared_ptr<DdrLinkSettings> linkSettings);

    void prepareToPlay (double, int) override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DdrLossyChannel)
};
