#pragma once

#include "HostEngine.h"
#include "Builtin/DawLabLookAndFeel.h"

// A separate small window for homework presets: the scenario (what's wrong, what you're told to find) is
// always visible, but the hint and the full explanation are only shown once you click for them - so just
// opening this window to check whether a preset has homework attached doesn't spoil the exercise.
// Refreshes itself (and re-hides any previously revealed hint/explanation) whenever the loaded chain
// changes, so it always matches whatever preset is actually loaded.
class HintWindow : public juce::DocumentWindow
{
public:
    HintWindow (HostEngine& engineToWatch, std::function<void()> onClose);
    ~HintWindow() override;

    void closeButtonPressed() override { closeCallback(); }

private:
    class Content;
    std::function<void()> closeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HintWindow)
};
