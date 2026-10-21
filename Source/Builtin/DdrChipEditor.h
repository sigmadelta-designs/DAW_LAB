#pragma once

#include "DdrChip.h"
#include "DawLabLookAndFeel.h"

// Shows the chip's memory array "physically": one row per address, one column per DQ bit, a literal 0/1
// digit per cell - the most direct reading of what this simulation actually stores, live as the BIST
// writes and reads it. "Rows and columns" here means address x bit-position within the stored word, not
// a model of a real DRAM's row/column/bank addressing (this simulation doesn't have one - see DdrChip.h).
class DdrChipEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit DdrChipEditor (DdrChip& chipToEdit);
    ~DdrChipEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Grid;

    void timerCallback() override;

    DdrChip& chip;
    std::unique_ptr<Grid> grid;
    juce::Viewport viewport;

    juce::ComboBox chipNumberBox;
    juce::Slider vrefSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label chipNumberLabel { {}, "Chip number" }, vrefLabel { {}, "Vref" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chipNumberAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> vrefAttachment;

    juce::Rectangle<int> statusArea, columnHeaderArea;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DdrChipEditor)
};
