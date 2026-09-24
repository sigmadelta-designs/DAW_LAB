#include "ChainView.h"

class ChainView::Row : public juce::Component
{
public:
    Row (ChainView& ownerView, int rowIndex, const Item& rowItem, bool isFirst, bool isLast)
        : owner (ownerView), index (rowIndex), item (rowItem)
    {
        for (auto* button : { &editButton, &upButton, &downButton, &removeButton })
            addAndMakeVisible (button);
        addAndMakeVisible (bypassButton);

        editButton.setButtonText (item.editorOpen ? "Hide" : "Edit");
        bypassButton.setToggleState (item.bypassed, juce::dontSendNotification);
        upButton.setEnabled (! isFirst);
        downButton.setEnabled (! isLast);

        // These may cause the owner to rebuild its rows, so ChainView's clients
        // must defer that rebuild (MainComponent does, via AsyncUpdater).
        editButton.onClick   = [this] { if (owner.onEdit)     owner.onEdit (index); };
        upButton.onClick     = [this] { if (owner.onMoveUp)   owner.onMoveUp (index); };
        downButton.onClick   = [this] { if (owner.onMoveDown) owner.onMoveDown (index); };
        removeButton.onClick = [this] { if (owner.onRemove)   owner.onRemove (index); };
        bypassButton.onClick = [this] { if (owner.onBypass)   owner.onBypass (index, bypassButton.getToggleState()); };
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colours::white.withAlpha (item.bypassed ? 0.04f : 0.10f));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawRoundedRectangle (area, 5.0f, 1.0f);

        auto badge = juce::Rectangle<float> (26.0f, 26.0f).withCentre ({ 24.0f, (float) getHeight() * 0.5f });
        g.setColour (item.bypassed ? juce::Colours::grey : juce::Colours::orange);
        g.fillEllipse (badge);
        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1), badge.toNearestInt(), juce::Justification::centred);

        auto text = getLocalBounds().withTrimmedLeft (48).withTrimmedRight (buttonsWidth);
        g.setColour (juce::Colours::white.withAlpha (item.bypassed ? 0.45f : 1.0f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (item.name + (item.bypassed ? "  (bypassed)" : ""), text.removeFromTop (getHeight() / 2 + 2),
                    juce::Justification::bottomLeft, true);
        g.setColour (juce::Colours::lightgrey.withAlpha (0.7f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (item.detail, text, juce::Justification::topLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 7);
        removeButton.setBounds (area.removeFromRight (64));
        area.removeFromRight (4);
        downButton.setBounds (area.removeFromRight (52));
        area.removeFromRight (4);
        upButton.setBounds (area.removeFromRight (40));
        area.removeFromRight (4);
        editButton.setBounds (area.removeFromRight (52));
        area.removeFromRight (4);
        bypassButton.setBounds (area.removeFromRight (76));
    }

private:
    static constexpr int buttonsWidth = 6 + 64 + 4 + 52 + 4 + 40 + 4 + 52 + 4 + 76 + 6;

    ChainView& owner;
    int index;
    Item item;
    juce::TextButton editButton { "Edit" }, upButton { "Up" }, downButton { "Down" }, removeButton { "Remove" };
    juce::ToggleButton bypassButton { "Bypass" };
};

ChainView::ChainView() = default;
ChainView::~ChainView() = default;

void ChainView::setItems (const std::vector<Item>& newItems)
{
    rows.clear();

    for (size_t i = 0; i < newItems.size(); ++i)
        addAndMakeVisible (rows.add (new Row (*this, (int) i, newItems[i], i == 0, i + 1 == newItems.size())));

    setSize (getWidth(), getRequiredHeight());
    resized();
    repaint();
}

int ChainView::getRequiredHeight() const
{
    return endHeight * 2 + rows.size() * rowHeight + 4;
}

void ChainView::resized()
{
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, endHeight + i * rowHeight + 2, getWidth(), rowHeight - 4);
}

void ChainView::paint (juce::Graphics& g)
{
    const auto drawEnd = [&g, this] (int y, const juce::String& label)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, (float) y + 2.0f, (float) getWidth(), endHeight - 4.0f), 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (label, 12, y, getWidth() - 24, endHeight, juce::Justification::centredLeft);
    };

    // Signal-flow line running down the badge column.
    g.setColour (juce::Colours::orange.withAlpha (0.6f));
    g.drawLine (24.0f, (float) endHeight, 24.0f, (float) (endHeight + rows.size() * rowHeight + 2), 2.0f);

    drawEnd (0, "INPUT  (audio device)");
    drawEnd (endHeight + rows.size() * rowHeight + 2, "OUTPUT  (audio device)");
}
