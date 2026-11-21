#pragma once

#include "BuiltInProcessor.h"
#include "DdrLinkSettings.h"
#include "DawLabLookAndFeel.h"

// The DDR-domain counterpart to SimpleParamEditor: a minimal editor for a DDR stage whose controls are
// just a vertical list of labelled sliders, combo boxes and toggles (controller, lossy channel). A
// separate class, not a template specialisation of SimpleParamEditor, for the same reason the PLL domain
// got its own stages rather than retrofitting the SerDes-specific one - see the README's "Hover tips"
// section and the PLL Core section for the precedent.
class DdrSimpleParamEditor : public juce::AudioProcessorEditor
{
public:
    explicit DdrSimpleParamEditor (BuiltInProcessorBase<DdrLinkSettings>& processorToEdit, int editorWidth = 480);
    ~DdrSimpleParamEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

protected:
    juce::Slider& addSlider (const char* parameterId, const juce::String& label,
                             ControlTip::Domain domain, const juce::String& reason);
    juce::ComboBox& addCombo (const char* parameterId, const juce::String& label, const juce::StringArray& items,
                              ControlTip::Domain domain, const juce::String& reason);
    juce::ToggleButton& addToggle (const char* parameterId, const juce::String& label,
                                   ControlTip::Domain domain, const juce::String& reason);
    void addHeading (const juce::String& text);

    // Call once, after adding every row, at the end of the subclass constructor: sizes the window to fit
    // what was added (resized() only positions rows within whatever size that established).
    void finishLayout();

    BuiltInProcessorBase<DdrLinkSettings>& stage;

private:
    struct Row
    {
        juce::String heading;    // non-empty: this row is a section heading, `control`/`label` unused
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    std::vector<std::unique_ptr<Row>> rows;
    int editorWidth;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DdrSimpleParamEditor)
};
