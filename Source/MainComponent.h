#pragma once

#include "ChainView.h"
#include "HostEngine.h"
#include "Builtin/DawLabLookAndFeel.h"

// Top-level UI: pick a scanned plugin, append it to the chain, reorder/bypass/
// edit the chain, and save/load whole chains (with plugin state) as presets.
class MainComponent : public juce::Component,
                      private juce::ChangeListener,
                      private juce::AsyncUpdater,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    class PluginWindow;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void refreshPluginList();
    void showScanDialog();
    void showAudioSettings();
    void addSelectedPlugin();
    void removeFromChain (int index);
    void toggleEditor (int index);
    void closeAllEditors();
    void clearChain();
    void savePreset();
    void loadPreset();
    void loadPresetFile (const juce::File& file);
    void syncLinkControls();
    void updateControls();
    void layoutChain();
    void setStatus (const juce::String& text);

    static juce::File getPresetDirectory();

    // Declared first so it is destroyed last, after the editor windows.
    HostEngine engine { false, true };
    std::vector<juce::PluginDescription> pluginChoices;
    std::map<juce::AudioProcessorGraph::NodeID, std::unique_ptr<PluginWindow>> editors;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String chainName;
    bool busy = false;

    juce::ComboBox pluginBox;
    juce::TextButton addButton        { "Add to Chain" },
                     scanButton       { "Scan..." },
                     saveButton       { "Save Preset..." },
                     loadButton       { "Load Preset..." },
                     clearButton      { "Clear Chain" },
                     audioButton      { "Audio Settings..." };
    juce::Label chainLabel, statusLabel, rateLabel { {}, "Line rate" }, rateInfoLabel;
    juce::Slider rateSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox modulationBox;
    juce::ToggleButton muteButton { "Mute output" };
    ChainView chainView;
    juce::Viewport viewport;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
