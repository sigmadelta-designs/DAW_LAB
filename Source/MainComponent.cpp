#include "MainComponent.h"
#include "Builtin/BuiltInFormat.h"

class MainComponent::PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor& processor, std::function<void()> onClose)
        : DocumentWindow (processor.getName(), juce::Colours::darkgrey, DocumentWindow::closeButton),
          closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);

        auto* editor = processor.hasEditor() ? processor.createEditorIfNeeded()
                                             : new juce::GenericAudioProcessorEditor (processor);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { closeCallback(); }

private:
    std::function<void()> closeCallback;
};

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);

    for (auto* button : { &addButton, &scanButton, &saveButton, &loadButton, &clearButton, &audioButton, &hintButton })
        addAndMakeVisible (button);

    addAndMakeVisible (pluginBox);
    addAndMakeVisible (rateLabel);
    addAndMakeVisible (rateSlider);
    addAndMakeVisible (modulationBox);
    addAndMakeVisible (muteButton);
    muteButton.setTooltip ("The chain keeps running and measuring, but nothing is played through the speakers.");
    muteButton.setToggleState (engine.isOutputMuted() || engine.getSettings()->getBoolValue ("muteOutput", false), juce::dontSendNotification);
    engine.setOutputMuted (muteButton.getToggleState());
    muteButton.onClick = [this]
    {
        engine.setOutputMuted (muteButton.getToggleState());
        engine.getSettings()->setValue ("muteOutput", muteButton.getToggleState());
    };
    addAndMakeVisible (rateInfoLabel);

    pluginBox.setTooltip ("Choose a built-in stage or a scanned plugin, then Add to Chain to append it.");
    addButton.setTooltip ("Appends the chosen plugin to the end of the chain.");
    scanButton.setTooltip ("Scans your AU/VST3 plugin folders (or a folder you pick) and adds what it finds to the list above.");
    saveButton.setTooltip ("Writes the current chain - stage order, bypass flags, and every parameter - to a .labchain preset file.");
    loadButton.setTooltip ("Replaces the current chain with one loaded from a .labchain preset file.");
    clearButton.setTooltip ("Removes every stage from the chain.");
    audioButton.setTooltip ("Opens JUCE's audio/MIDI device picker for this app.");
    hintButton.setTooltip ("Opens the homework window: the scenario for a HOMEWORK_*.labchain preset, "
                           "with a hint and an explanation you can reveal if you get stuck.");
    hintButton.onClick = [this] { toggleHintWindow(); };

    // 1 GBd on the wire runs as 1 kBd here (simulation scale 1e6).
    rateSlider.setRange (LinkSettings::minGBd, LinkSettings::maxGBd, 0.01);
    rateSlider.setNumDecimalPlacesToDisplay (2);
    rateSlider.setTextValueSuffix (" GBd");
    rateSlider.setTooltip (ControlTip::make ("Line rate", ControlTip::Domain::dut,
        "The link's real symbol rate, in Gigabaud - a property of the transmitter and receiver you're testing, "
        "not of this simulation. Everything downstream (channel loss, CDR range, eye timing) scales with it."));
    rateSlider.onValueChange = [this] { engine.link->lineRateGBd = (float) rateSlider.getValue(); };
    modulationBox.addItemList ({ "NRZ", "PAM4" }, 1);
    modulationBox.setTooltip (ControlTip::make ("Modulation", ControlTip::Domain::dut,
        "NRZ (one bit per symbol) or PAM4 (two bits per symbol, four levels) - the real signalling format the "
        "transmitter and receiver are built for."));
    modulationBox.onChange = [this] { engine.link->modulation = modulationBox.getSelectedItemIndex(); };
    rateInfoLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    syncLinkControls();
    startTimerHz (5);
    addAndMakeVisible (chainLabel);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&chainView, false);
    viewport.setScrollBarsShown (true, false);

    chainLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    addButton.onClick   = [this] { addSelectedPlugin(); };
    scanButton.onClick  = [this] { showScanDialog(); };
    saveButton.onClick  = [this] { savePreset(); };
    loadButton.onClick  = [this] { loadPreset(); };
    clearButton.onClick = [this] { clearChain(); };
    audioButton.onClick = [this] { showAudioSettings(); };

    chainView.onEdit     = [this] (int i) { toggleEditor (i); };
    chainView.onRemove   = [this] (int i) { removeFromChain (i); };
    chainView.onMoveUp   = [this] (int i) { engine.movePlugin ((size_t) i, (size_t) i - 1); };
    chainView.onMoveDown = [this] (int i) { engine.movePlugin ((size_t) i, (size_t) i + 1); };
    chainView.onBypass   = [this] (int i, bool bypassed) { engine.setBypassed ((size_t) i, bypassed); };

    engine.knownPlugins.addChangeListener (this);
    engine.chainChanged.addChangeListener (this);
    refreshPluginList();
    setStatus ("Empty chain: input is passed straight to output");
    setSize (780, 600);
    triggerAsyncUpdate();
}

MainComponent::~MainComponent()
{
    stopTimer();
    cancelPendingUpdate();
    engine.knownPlugins.removeChangeListener (this);
    engine.chainChanged.removeChangeListener (this);
    closeAllEditors();
    engine.clearChain();
    setLookAndFeel (nullptr);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (28);
    muteButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (6);
    scanButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (6);
    addButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (6);
    pluginBox.setBounds (top);

    area.removeFromTop (6);
    auto linkRow = area.removeFromTop (28);
    rateLabel.setBounds (linkRow.removeFromLeft (70));
    rateSlider.setBounds (linkRow.removeFromLeft (260));
    linkRow.removeFromLeft (10);
    modulationBox.setBounds (linkRow.removeFromLeft (80));
    linkRow.removeFromLeft (12);
    rateInfoLabel.setBounds (linkRow);

    area.removeFromTop (8);
    chainLabel.setBounds (area.removeFromTop (24));

    auto status = area.removeFromBottom (24);
    statusLabel.setBounds (status);
    area.removeFromBottom (6);

    auto bottom = area.removeFromBottom (28);
    audioButton.setBounds (bottom.removeFromRight (140));
    bottom.removeFromRight (6);
    hintButton.setBounds (bottom.removeFromRight (130));
    bottom.removeFromRight (6);
    clearButton.setBounds (bottom.removeFromRight (100));
    bottom.removeFromRight (6);
    loadButton.setBounds (bottom.removeFromRight (120));
    bottom.removeFromRight (6);
    saveButton.setBounds (bottom.removeFromRight (120));
    area.removeFromBottom (6);

    viewport.setBounds (area);
    layoutChain();
}

void MainComponent::layoutChain()
{
    chainView.setSize (viewport.getMaximumVisibleWidth(), chainView.getRequiredHeight());
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.knownPlugins)
        refreshPluginList();
    else
        triggerAsyncUpdate();
}

// Rebuilds the chain view from the engine. Always deferred: view buttons call
// into us from their own click handlers, and rebuilding destroys those buttons.
void MainComponent::handleAsyncUpdate()
{
    std::vector<ChainView::Item> items;
    const auto& slots = engine.getSlots();

    for (size_t i = 0; i < slots.size(); ++i)
    {
        ChainView::Item item;
        item.name = slots[i].description.name;
        item.detail = slots[i].description.pluginFormatName + "  |  " + slots[i].description.manufacturerName;
        item.bypassed = engine.isBypassed (i);
        item.editorOpen = editors.count (slots[i].nodeId) > 0;
        items.push_back (std::move (item));
    }

    chainView.setItems (items);
    layoutChain();

    chainLabel.setText ("Signal chain: " + (chainName.isEmpty() ? juce::String ("(unsaved)") : chainName)
                          + "   [" + juce::String ((int) slots.size()) + " plugin"
                          + (slots.size() == 1 ? "" : "s") + "]",
                        juce::dontSendNotification);
    updateControls();
}

void MainComponent::refreshPluginList()
{
    const auto previous = pluginBox.getSelectedItemIndex() >= 0
                            ? pluginChoices[(size_t) pluginBox.getSelectedItemIndex()].createIdentifierString()
                            : juce::String();

    pluginChoices.clear();
    pluginBox.clear (juce::dontSendNotification);

    for (const auto& type : BuiltInFormat::getDescriptions())
    {
        pluginChoices.push_back (type);
        pluginBox.addItem ("[Built-in]  " + type.name, (int) pluginChoices.size());
    }

    for (const auto& type : engine.knownPlugins.getTypes())
    {
        pluginChoices.push_back (type);
        pluginBox.addItem (type.name + "  (" + type.pluginFormatName + ", " + type.manufacturerName + ")",
                           (int) pluginChoices.size());
    }

    int selected = 0;
    for (size_t i = 0; i < pluginChoices.size(); ++i)
        if (pluginChoices[i].createIdentifierString() == previous)
            selected = (int) i;

    if (! pluginChoices.empty())
        pluginBox.setSelectedItemIndex (selected, juce::dontSendNotification);
    else
        pluginBox.setTextWhenNothingSelected ("No plugins - click Scan...");

    updateControls();
}

void MainComponent::syncLinkControls()
{
    const double rate = engine.link->lineRateGBd.load();
    if (std::abs (rateSlider.getValue() - rate) > 1.0e-4)
        rateSlider.setValue (rate, juce::dontSendNotification);

    if (modulationBox.getSelectedItemIndex() != engine.link->modulation.load())
        modulationBox.setSelectedItemIndex (engine.link->modulation.load(), juce::dontSendNotification);

    double sampleRate = 48000.0;
    if (auto* device = engine.deviceManager.getCurrentAudioDevice())
        sampleRate = device->getCurrentSampleRate();

    const double fastest = LinkSettings::maxRateForSampleRate (sampleRate);
    const double baud = engine.link->baudHz();

    juce::String info = "= " + juce::String (baud / 1000.0, 2) + " kBd on the audio side, Nyquist "
                        + juce::String (baud / 2000.0, 2) + " kHz, "
                        + juce::String (engine.link->samplesPerUI (sampleRate), 1) + " samples/UI";

    const bool tooFast = rate > fastest + 1.0e-6;
    if (tooFast)
        info = "Too fast for " + juce::String (sampleRate / 1000.0, 1) + " kHz audio: running at "
               + juce::String (fastest, 2) + " GBd";

    rateInfoLabel.setText (info, juce::dontSendNotification);
    rateInfoLabel.setColour (juce::Label::textColourId, tooFast ? juce::Colours::orange : juce::Colours::lightgrey);
}

void MainComponent::timerCallback()
{
    syncLinkControls();   // also picks up a rate/modulation loaded from a preset
}

void MainComponent::updateControls()
{
    const bool hasChain = ! engine.getSlots().empty();

    addButton.setEnabled (! busy && pluginBox.getSelectedItemIndex() >= 0);
    loadButton.setEnabled (! busy);
    saveButton.setEnabled (! busy && hasChain);
    clearButton.setEnabled (! busy && hasChain);
}

void MainComponent::showScanDialog()
{
    auto* list = new juce::PluginListComponent (engine.formatManager, engine.knownPlugins,
                                                engine.getDeadMansPedalFile(), engine.getSettings(), true);
    list->setSize (700, 500);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Plugins";
    options.content.setOwned (list);
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colours::darkgrey;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.deviceManager, 0, 2, 0, 2, true, false, true, false);
    selector->setSize (500, 450);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio Settings";
    options.content.setOwned (selector);
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colours::darkgrey;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::addSelectedPlugin()
{
    const auto index = pluginBox.getSelectedItemIndex();
    if (busy || index < 0)
        return;

    const auto description = pluginChoices[(size_t) index];

    busy = true;
    updateControls();
    setStatus ("Loading " + description.name + "...");

    engine.addPlugin (description, nullptr, false,
        [safeThis = juce::Component::SafePointer<MainComponent> (this), description]
        (bool ok, const juce::String& error)
        {
            if (safeThis == nullptr)
                return;

            safeThis->busy = false;
            safeThis->updateControls();
            safeThis->setStatus (ok ? "Added " + description.name
                                    : "Failed to load " + description.name + ": " + error);
        });
}

void MainComponent::removeFromChain (int index)
{
    const auto& slots = engine.getSlots();
    if (index < 0 || (size_t) index >= slots.size())
        return;

    // The editor must go before the plugin it belongs to.
    editors.erase (slots[(size_t) index].nodeId);
    engine.removePlugin ((size_t) index);
}

void MainComponent::toggleEditor (int index)
{
    const auto& slots = engine.getSlots();
    if (index < 0 || (size_t) index >= slots.size())
        return;

    const auto nodeId = slots[(size_t) index].nodeId;

    if (editors.erase (nodeId) == 0)
    {
        if (auto* plugin = engine.getPlugin ((size_t) index))
        {
            editors[nodeId] = std::make_unique<PluginWindow> (*plugin,
                [safeThis = juce::Component::SafePointer<MainComponent> (this), nodeId]
                {
                    // Defer so the window isn't destroyed from inside its own callback.
                    juce::MessageManager::callAsync ([safeThis, nodeId]
                    {
                        if (safeThis != nullptr)
                        {
                            safeThis->editors.erase (nodeId);
                            safeThis->triggerAsyncUpdate();
                        }
                    });
                });
        }
    }

    triggerAsyncUpdate();
}

void MainComponent::closeAllEditors()
{
    editors.clear();
}

void MainComponent::toggleHintWindow()
{
    if (hintWindow != nullptr)
    {
        hintWindow.reset();
        return;
    }

    hintWindow = std::make_unique<HintWindow> (engine,
        [safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            juce::MessageManager::callAsync ([safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->hintWindow.reset();
            });
        });
}

void MainComponent::clearChain()
{
    closeAllEditors();
    engine.clearChain();
    chainName = {};
    setStatus ("Chain cleared");
}

juce::File MainComponent::getPresetDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         .getChildFile ("DAW_LAB/Presets");
    directory.createDirectory();
    return directory;
}

void MainComponent::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Save chain preset",
                                                   getPresetDirectory().getChildFile (chainName.isEmpty() ? "chain" : chainName)
                                                       .withFileExtension ("labchain"),
                                                   "*.labchain");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (safeThis == nullptr || file == juce::File())
                return;

            file = file.withFileExtension ("labchain");
            const auto name = file.getFileNameWithoutExtension();

            if (safeThis->engine.createChainXml (name)->writeTo (file))
            {
                safeThis->chainName = name;
                safeThis->setStatus ("Saved preset " + file.getFullPathName());
                safeThis->triggerAsyncUpdate();
            }
            else
            {
                safeThis->setStatus ("Could not write " + file.getFullPathName());
            }
        });
}

void MainComponent::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load chain preset", getPresetDirectory(), "*.labchain");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
        {
            if (safeThis != nullptr && fc.getResult() != juce::File())
                safeThis->loadPresetFile (fc.getResult());
        });
}

void MainComponent::loadPresetFile (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr)
    {
        setStatus ("Could not read " + file.getFullPathName());
        return;
    }

    closeAllEditors();
    busy = true;
    updateControls();
    setStatus ("Loading preset " + file.getFileNameWithoutExtension() + "...");

    const bool started = engine.loadChain (*xml,
        [safeThis = juce::Component::SafePointer<MainComponent> (this), file]
        (const juce::String&, const juce::StringArray& failures)
        {
            if (safeThis == nullptr)
                return;

            safeThis->busy = false;
            safeThis->chainName = file.getFileNameWithoutExtension();
            safeThis->updateControls();
            safeThis->triggerAsyncUpdate();

            if (failures.isEmpty())
                safeThis->setStatus ("Loaded preset " + safeThis->chainName);
            else
                safeThis->setStatus ("Loaded " + safeThis->chainName + " with problems - "
                                     + failures.joinIntoString ("; "));
        });

    if (! started)
    {
        busy = false;
        updateControls();
        setStatus (file.getFileName() + " is not a DAW_LAB chain preset");
    }
}

void MainComponent::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}
