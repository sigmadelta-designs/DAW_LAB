#include "DdrChipEditor.h"

// The scrolled content: one row per address, one column per DQ bit. Only the rows that actually intersect
// the current clip (i.e. are on screen) are drawn, so scrolling stays cheap even at DdrChip::memoryWords
// rows - most of which, in practice, a BIST sweep never touches and so just read back as 0.
class DdrChipEditor::Grid : public juce::Component
{
public:
    explicit Grid (DdrChip& c) : chip (c)
    {
        setSize (gutterWidth + DdrBus::dqWidth * cellWidth, DdrChip::memoryWords * cellHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101418));

        const auto clip = g.getClipBounds();
        const int firstRow = juce::jlimit (0, DdrChip::memoryWords - 1, clip.getY() / cellHeight);
        const int lastRow = juce::jlimit (0, DdrChip::memoryWords - 1, clip.getBottom() / cellHeight);

        for (int row = firstRow; row <= lastRow; ++row)
        {
            const int y = row * cellHeight;
            const juce::uint8 byte = chip.getMemoryByte (row);

            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (row), 2, y, gutterWidth - 8, cellHeight, juce::Justification::centredRight);

            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            for (int b = 0; b < DdrBus::dqWidth; ++b)
            {
                const int x = gutterWidth + b * cellWidth;
                const bool bit = ((byte >> b) & 1) != 0;

                g.setColour (bit ? juce::Colour (0xff1c6e8c) : juce::Colour (0xff17191d));
                g.fillRect (x + 1, y + 1, cellWidth - 2, cellHeight - 2);
                g.setColour (bit ? juce::Colour (0xff35c8ff) : juce::Colours::white.withAlpha (0.35f));
                g.drawText (bit ? "1" : "0", x, y, cellWidth, cellHeight, juce::Justification::centred);
            }
        }

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        for (int b = 0; b <= DdrBus::dqWidth; ++b)
            g.drawVerticalLine (gutterWidth + b * cellWidth, (float) firstRow * cellHeight, (float) (lastRow + 1) * cellHeight);
    }

    static constexpr int gutterWidth = 50, cellWidth = 30, cellHeight = 20;

private:
    DdrChip& chip;
};

DdrChipEditor::DdrChipEditor (DdrChip& chipToEdit)
    : juce::AudioProcessorEditor (chipToEdit), chip (chipToEdit)
{
    setLookAndFeel (&lookAndFeel);

    grid = std::make_unique<Grid> (chip);
    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    for (auto* c : std::initializer_list<juce::Component*> { &chipNumberBox, &chipNumberLabel, &vrefSlider, &vrefLabel })
        addAndMakeVisible (c);

    chipNumberBox.addItemList ({ "Chip 0", "Chip 1", "Chip 2", "Chip 3" }, 1);
    chipNumberAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        chip.getParameterTree(), "chipnumber", chipNumberBox);
    vrefAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        chip.getParameterTree(), "vref", vrefSlider);

    using D = ControlTip::Domain;
    chipNumberBox.setTooltip (ControlTip::make ("Chip number", D::dut,
        "Literally which CS wire this chip is strapped to - the same kind of choice a real package/rank-select "
        "pin is. Only responds while the controller asserts that line; a mismatch with the controller's own "
        "Target chip is a real bring-up bug (see the chip-select homework scenarios), not a setting to just "
        "match by habit."));
    vrefSlider.setTooltip (ControlTip::make ("Vref (DQ slice level)", D::dut,
        "The slicing threshold for DQ, relative to the differential swing - a real Vref training register. "
        "0 is centred for an ideal, full swing; left at a value trained for a different (often larger) swing "
        "than this channel actually delivers, it reads every bit wrong, a real and common DDR mis-training bug."));

    chipNumberLabel.setJustificationType (juce::Justification::centredRight);
    vrefLabel.setJustificationType (juce::Justification::centredRight);

    setSize (360, 640);
    startTimerHz (12);
}

DdrChipEditor::~DdrChipEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DdrChipEditor::timerCallback()
{
    grid->repaint (viewport.getViewArea());
    repaint (statusArea);
}

void DdrChipEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto row1 = area.removeFromTop (26);
    chipNumberLabel.setBounds (row1.removeFromLeft (84));
    chipNumberBox.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (16);
    vrefLabel.setBounds (row1.removeFromLeft (40));
    vrefSlider.setBounds (row1);

    area.removeFromTop (6);
    statusArea = area.removeFromTop (20);
    area.removeFromTop (8);

    columnHeaderArea = area.removeFromTop (18);
    area.removeFromTop (2);

    viewport.setBounds (area);
}

void DdrChipEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    const bool selected = chip.status.everSelected.load();
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (selected ? juce::Colours::lightgreen : juce::Colours::orange);
    g.drawText (selected ? "SELECTED at least once since this chain loaded" : "Never selected - check Target chip vs. Chip number",
                statusArea.removeFromLeft (statusArea.getWidth() * 2 / 3), juce::Justification::centredLeft);
    g.setColour (juce::Colours::lightgrey);
    g.drawText ("W " + juce::String ((juce::int64) chip.status.wordsWritten.load())
                    + "  R " + juce::String ((juce::int64) chip.status.wordsRead.load()),
                statusArea, juce::Justification::centredRight);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("addr", columnHeaderArea.withWidth (Grid::gutterWidth), juce::Justification::centred);
    for (int b = 0; b < DdrBus::dqWidth; ++b)
        g.drawText ("DQ" + juce::String (b),
                    columnHeaderArea.getX() + Grid::gutterWidth + b * Grid::cellWidth, columnHeaderArea.getY(),
                    Grid::cellWidth, columnHeaderArea.getHeight(), juce::Justification::centred);
}
