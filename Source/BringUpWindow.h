#pragma once

#include "HostEngine.h"
#include "Builtin/DawLabLookAndFeel.h"

// A guided, linear walk through a fixed sequence of <filePattern> presets (scanned from the preset
// directory, sorted by filename - the zero-padded step number sorts them into order). Each step is an
// ordinary homework preset (same title/prompt/hint/explanation block the Homework Hint window reads) but
// the window itself tracks which step you're on and loads the next one for you with a single button press,
// instead of making you use Load Preset... and remember where you are. The current step persists (in the
// app's settings, under `settingsKey`) across closing/reopening the window and across runs of the app.
//
// Generic over which plan it's walking (filePattern/windowTitle/settingsKey) rather than hardcoded to the
// SerDes bring-up plan, so a second, independent plan (e.g. the DDR bring-up plan) can use a second
// instance of this same window with its own non-overlapping filename prefix, its own title, and its own
// persisted step - without the two plans' presets ever being scanned into one mixed sequence.
class BringUpWindow : public juce::DocumentWindow
{
public:
    // `loadStepFile` actually replaces the chain (MainComponent::loadPresetFile) - this window only
    // decides WHICH file, never touches the engine's chain directly.
    BringUpWindow (HostEngine& engineToWatch, juce::File presetDirectory, juce::String filePattern,
                  juce::String windowTitle, juce::String settingsKey,
                  std::function<void (const juce::File&)> loadStepFile, std::function<void()> onClose);
    ~BringUpWindow() override;

    void closeButtonPressed() override { closeCallback(); }

private:
    class Content;
    std::function<void()> closeCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BringUpWindow)
};
