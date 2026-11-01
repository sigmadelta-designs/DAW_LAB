#pragma once

#include "HostEngine.h"
#include "Builtin/DawLabLookAndFeel.h"

// A guided, linear walk through a fixed sequence of BRINGUP_*.labchain presets (scanned from the preset
// directory, sorted by filename - the zero-padded step number sorts them into order). Each step is an
// ordinary homework preset (same title/prompt/hint/explanation block the Homework Hint window reads) but
// the window itself tracks which step you're on and loads the next one for you with a single button press,
// instead of making you use Load Preset... and remember where you are. The current step persists (in the
// app's settings) across closing/reopening the window and across runs of the app.
class BringUpWindow : public juce::DocumentWindow
{
public:
    // `loadStepFile` actually replaces the chain (MainComponent::loadPresetFile) - this window only
    // decides WHICH file, never touches the engine's chain directly.
    BringUpWindow (HostEngine& engineToWatch, juce::File presetDirectory,
                  std::function<void (const juce::File&)> loadStepFile, std::function<void()> onClose);
    ~BringUpWindow() override;

    void closeButtonPressed() override { closeCallback(); }

private:
    class Content;
    std::function<void()> closeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BringUpWindow)
};
