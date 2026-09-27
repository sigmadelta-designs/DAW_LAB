#include "SimpleParamEditor.h"

SimpleParamEditor::SimpleParamEditor (BuiltInProcessor& processorToEdit, int width)
    : juce::AudioProcessorEditor (processorToEdit), stage (processorToEdit), editorWidth (width)
{
    setLookAndFeel (&lookAndFeel);
}

SimpleParamEditor::~SimpleParamEditor()
{
    setLookAndFeel (nullptr);
}

void SimpleParamEditor::addHeading (const juce::String& text)
{
    auto row = std::make_unique<Row>();
    row->heading = text;
    rows.push_back (std::move (row));
}

juce::Slider& SimpleParamEditor::addSlider (const char* parameterId, const juce::String& label,
                                           ControlTip::Domain domain, const juce::String& reason)
{
    auto row = std::make_unique<Row>();
    row->label = std::make_unique<juce::Label> (juce::String(), label);
    row->label->setJustificationType (juce::Justification::centredRight);

    auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    slider->setTooltip (ControlTip::make (label, domain, reason));
    row->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        stage.getParameterTree(), parameterId, *slider);

    addAndMakeVisible (*row->label);
    addAndMakeVisible (*slider);
    auto& ref = *slider;
    row->control = std::move (slider);
    rows.push_back (std::move (row));
    return ref;
}

juce::ComboBox& SimpleParamEditor::addCombo (const char* parameterId, const juce::String& label, const juce::StringArray& items,
                                            ControlTip::Domain domain, const juce::String& reason)
{
    auto row = std::make_unique<Row>();
    row->label = std::make_unique<juce::Label> (juce::String(), label);
    row->label->setJustificationType (juce::Justification::centredRight);

    auto combo = std::make_unique<juce::ComboBox>();
    combo->addItemList (items, 1);
    combo->setTooltip (ControlTip::make (label, domain, reason));
    row->comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        stage.getParameterTree(), parameterId, *combo);

    addAndMakeVisible (*row->label);
    addAndMakeVisible (*combo);
    auto& ref = *combo;
    row->control = std::move (combo);
    rows.push_back (std::move (row));
    return ref;
}

juce::ToggleButton& SimpleParamEditor::addToggle (const char* parameterId, const juce::String& label,
                                                  ControlTip::Domain domain, const juce::String& reason)
{
    auto row = std::make_unique<Row>();

    auto toggle = std::make_unique<juce::ToggleButton> (label);
    toggle->setTooltip (ControlTip::make (label, domain, reason));
    row->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        stage.getParameterTree(), parameterId, *toggle);

    addAndMakeVisible (*toggle);
    auto& ref = *toggle;
    row->control = std::move (toggle);
    rows.push_back (std::move (row));
    return ref;
}

namespace { constexpr int rowHeight = 28, headingHeight = 30, rowGap = 4, margin = 12; }

void SimpleParamEditor::finishLayout()
{
    int height = margin * 2;
    for (auto& row : rows)
        height += (row->heading.isNotEmpty() ? headingHeight : rowHeight) + rowGap;

    setSize (editorWidth, juce::jmax (120, height));
}

void SimpleParamEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    for (auto& row : rows)
    {
        if (row->heading.isNotEmpty())
        {
            area.removeFromTop (headingHeight);
            area.removeFromTop (rowGap);
            continue;
        }

        auto line = area.removeFromTop (rowHeight);

        if (row->label != nullptr)
        {
            row->label->setBounds (line.removeFromLeft (150));
            line.removeFromLeft (10);
        }

        row->control->setBounds (line);
        area.removeFromTop (rowGap);
    }
}

void SimpleParamEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    auto area = getLocalBounds().reduced (margin);

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    for (auto& row : rows)
    {
        if (row->heading.isNotEmpty())
        {
            auto line = area.removeFromTop (headingHeight);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawText (row->heading, line, juce::Justification::centredLeft);
        }
        else
        {
            area.removeFromTop (rowHeight);
        }

        area.removeFromTop (rowGap);
    }
}
