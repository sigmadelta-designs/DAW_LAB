#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Vertical, top-to-bottom picture of the signal path:
//   INPUT -> [1] plugin -> [2] plugin -> ... -> OUTPUT
// with per-row bypass / edit / reorder / remove controls.
class ChainView : public juce::Component
{
public:
    struct Item
    {
        juce::String name, detail;
        bool bypassed = false;
        bool editorOpen = false;
    };

    ChainView();
    ~ChainView() override;

    void setItems (const std::vector<Item>& newItems);
    int getRequiredHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (int)> onEdit, onRemove, onMoveUp, onMoveDown;
    std::function<void (int, bool)> onBypass;

private:
    class Row;

    static constexpr int rowHeight = 40;
    static constexpr int endHeight = 26;

    juce::OwnedArray<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainView)
};
