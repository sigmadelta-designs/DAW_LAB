#pragma once

#include "PllCore.h"
#include "DawLabLookAndFeel.h"
#include <deque>

// PllCore's own editor (a new domain gets its own editor conventions rather than being retrofitted into
// the SerDes-specific SimpleParamEditor, the same call the DDR domain's editors already made): six DUT/SIM
// sliders, a live status readout, and a lock-acquisition transient strip chart - frequency error vs. time
// since the last parameter change or cold start, which nothing else in this project shows (EyeScope/
// JitterScope are steady-state-only tools; this is what you'd actually watch on a bench while a PLL pulls
// in after a reference step).
class PllCoreEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit PllCoreEditor (PllCore& coreToEdit);
    ~PllCoreEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawTransient (juce::Graphics&, juce::Rectangle<float> area);

    PllCore& core;

    juce::Slider refFreqSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider fbDividerSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider vcoFreeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider loopKpSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider loopKiSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider refJitterSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label refFreqLabel { {}, "Reference freq (MHz)" }, fbDividerLabel { {}, "Feedback divider N" },
                vcoFreeLabel { {}, "VCO free-run (MHz)" }, loopKpLabel { {}, "Loop kp" },
                loopKiLabel { {}, "Loop ki" }, refJitterLabel { {}, "Ref jitter (UI rms)" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        refFreqAttachment, fbDividerAttachment, vcoFreeAttachment, loopKpAttachment, loopKiAttachment, refJitterAttachment;

    juce::Rectangle<int> statusArea, transientArea;

    // lock-acquisition transient: frequency error (instantaneous VCO Hz minus the currently-targeted
    // N*ref) sampled once per timer tick. A deque, not a fixed ring, because it also needs to reset
    // cleanly whenever the target itself changes (a new step), not just scroll through it.
    std::deque<float> transientHz;
    static constexpr int transientLength = 300;   // 15 s at the 20 Hz timer below
    double lastTargetHz = -1.0;
    float rangeHz = 10.0f;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PllCoreEditor)
};
