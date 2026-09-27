#pragma once

#include "BuiltInProcessor.h"
#include "DawLabLookAndFeel.h"

// A minimal editor for a stage whose controls are just a vertical list of labelled sliders, combo boxes
// and toggles - used by stages simple enough not to need a bespoke layout (generator, lossy channel,
// noise injector). A subclass builds its rows in its own constructor with addSlider()/addCombo()/
// addToggle()/addHeading(), in the order they should appear; resized() then stacks them top to bottom.
class SimpleParamEditor : public juce::AudioProcessorEditor
{
public:
    explicit SimpleParamEditor (BuiltInProcessor& processorToEdit, int editorWidth = 480);
    ~SimpleParamEditor() override;

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

    BuiltInProcessor& stage;   // AudioProcessorEditor already has a `processor` member; avoid shadowing it

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleParamEditor)
};
