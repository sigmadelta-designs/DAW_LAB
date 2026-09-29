#include "HostEngine.h"
#include "Builtin/BuiltInFormat.h"

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

// Stable identity for a parameter: its ID where the format provides one, else its position.
static juce::String idOf (const juce::AudioProcessorParameter& parameter)
{
    if (auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*> (&parameter))
        return withId->paramID;

    return "#" + juce::String (parameter.getParameterIndex());
}

struct HostEngine::PendingChain
{
    struct PendingSlot
    {
        juce::PluginDescription description;
        SavedState state;
        bool hasState = false;
        bool bypassed = false;
    };

    juce::String name;
    std::vector<PendingSlot> slots;
    juce::StringArray failures;
    LoadChainCallback done;
};

HostEngine::HostEngine (bool startMuted, bool useSharedLink)
    : link (useSharedLink ? SharedLink::acquire() : std::make_shared<LinkSettings>())
{
    gate.muted = startMuted || juce::SystemStats::getEnvironmentVariable ("DAW_LAB_MUTE", {}).isNotEmpty();

    juce::PropertiesFile::Options options;
    options.applicationName     = "DAW_LAB";
    options.filenameSuffix      = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName          = "DAW_LAB";
    properties.setStorageParameters (options);

    formatManager.addDefaultFormats();
    formatManager.addFormat (new BuiltInFormat (link));

    link->lineRateGBd = (float) getSettings()->getDoubleValue ("lineRateGBd", 10.0);
    link->modulation = getSettings()->getIntValue ("modulation", LinkSettings::nrz);

    if (auto xml = juce::parseXML (getSettings()->getValue ("knownPlugins")))
        knownPlugins.recreateFromXml (*xml);
    knownPlugins.addChangeListener (this);

    audioIn  = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioInputNode));
    audioOut = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));
    midiIn   = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::midiInputNode));
    rebuildConnections();

    auto audioState = juce::parseXML (getSettings()->getValue ("audioDeviceState"));
    deviceManager.initialise (2, 2, audioState.get(), true);

    for (const auto& midi : juce::MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled (midi.identifier, true);

    player.setProcessor (&graph);
    deviceManager.addChangeListener (this);
    deviceManager.addAudioCallback (&gate);
    deviceManager.addMidiInputDeviceCallback ({}, &player);
}

HostEngine::~HostEngine()
{
    alive.reset();

    if (auto state = deviceManager.createStateXml())
        getSettings()->setValue ("audioDeviceState", state.get());
    getSettings()->setValue ("lineRateGBd", (double) link->lineRateGBd.load());
    getSettings()->setValue ("modulation", link->modulation.load());
    getSettings()->saveIfNeeded();

    deviceManager.removeMidiInputDeviceCallback ({}, &player);
    deviceManager.removeAudioCallback (&gate);
    deviceManager.removeChangeListener (this);
    knownPlugins.removeChangeListener (this);
    player.setProcessor (nullptr);
    graph.clear();
}

void HostEngine::addPlugin (const juce::PluginDescription& description, const SavedState* state,
                            bool bypassed, AddCallback callback)
{
    double sampleRate = 44100.0;
    int blockSize = 512;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize  = device->getCurrentBufferSizeSamples();
    }

    std::optional<SavedState> savedState;
    if (state != nullptr)
        savedState = *state;

    formatManager.createPluginInstanceAsync (
        description, sampleRate, blockSize,
        [this, weakAlive = std::weak_ptr<bool> (alive), description, savedState, bypassed,
         onDone = std::move (callback)]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (weakAlive.lock() == nullptr)
                return;

            if (instance == nullptr)
            {
                onDone (false, error);
                return;
            }

            if (savedState.has_value())
                applyState (*instance, *savedState);

            auto node = graph.addNode (std::move (instance));
            node->setBypassed (bypassed);
            slots.push_back ({ node->nodeID, description });

            rebuildConnections();
            chainChanged.sendChangeMessage();
            onDone (true, {});
        });
}

void HostEngine::removePlugin (size_t index)
{
    if (index >= slots.size())
        return;

    graph.removeNode (slots[index].nodeId);
    slots.erase (slots.begin() + (std::ptrdiff_t) index);
    rebuildConnections();
    chainChanged.sendChangeMessage();
}

void HostEngine::movePlugin (size_t from, size_t to)
{
    if (from >= slots.size() || to >= slots.size() || from == to)
        return;

    auto slot = slots[from];
    slots.erase (slots.begin() + (std::ptrdiff_t) from);
    slots.insert (slots.begin() + (std::ptrdiff_t) to, slot);
    rebuildConnections();
    chainChanged.sendChangeMessage();
}

void HostEngine::clearChain()
{
    for (const auto& slot : slots)
        graph.removeNode (slot.nodeId);

    slots.clear();
    homework = Homework{};
    rebuildConnections();
    chainChanged.sendChangeMessage();
}

void HostEngine::setBypassed (size_t index, bool shouldBypass)
{
    if (auto* node = getNode (index))
    {
        node->setBypassed (shouldBypass);
        chainChanged.sendChangeMessage();
    }
}

juce::AudioProcessorGraph::Node* HostEngine::getNode (size_t index) const
{
    return index < slots.size() ? graph.getNodeForId (slots[index].nodeId) : nullptr;
}

juce::AudioPluginInstance* HostEngine::getPlugin (size_t index) const
{
    if (auto* node = getNode (index))
        return dynamic_cast<juce::AudioPluginInstance*> (node->getProcessor());

    return nullptr;
}

bool HostEngine::isBypassed (size_t index) const
{
    auto* node = getNode (index);
    return node != nullptr && node->isBypassed();
}

// The chain is strictly serial: every plugin's stereo output feeds the next
// plugin's input. MIDI from the devices goes to every plugin that accepts it.
// Connections a plugin can't take (e.g. audio into an instrument) are refused
// by the graph and simply skipped.
void HostEngine::rebuildConnections()
{
    for (const auto& connection : graph.getConnections())
        graph.removeConnection (connection);

    auto previous = audioIn->nodeID;
    const int inputChannels = juce::jmax (1, graph.getMainBusNumInputChannels());
    bool firstStage = true;

    for (const auto& slot : slots)
    {
        // A mono input device feeds both channels of the first stage.
        for (int channel = 0; channel < 2; ++channel)
            graph.addConnection ({ { previous, firstStage ? juce::jmin (channel, inputChannels - 1) : channel },
                                   { slot.nodeId, channel } });
        firstStage = false;

        graph.addConnection ({ { midiIn->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                               { slot.nodeId,    juce::AudioProcessorGraph::midiChannelIndex } });
        previous = slot.nodeId;
    }

    for (int channel = 0; channel < 2; ++channel)
        graph.addConnection ({ { previous, firstStage ? juce::jmin (channel, inputChannels - 1) : channel },
                               { audioOut->nodeID, channel } });
}

HostEngine::SavedState HostEngine::captureState (juce::AudioProcessor& processor)
{
    SavedState state;
    processor.getStateInformation (state.blob);

    for (auto* parameter : processor.getParameters())
        state.parameters.emplace_back (idOf (*parameter), parameter->getValue());

    return state;
}

void HostEngine::applyState (juce::AudioProcessor& processor, const SavedState& state)
{
    if (state.blob.getSize() > 0)
        processor.setStateInformation (state.blob.getData(), (int) state.blob.getSize());

    for (auto* parameter : processor.getParameters())
    {
        const auto id = idOf (*parameter);

        for (const auto& [savedId, value] : state.parameters)
            if (savedId == id)
                parameter->setValue (value);
    }
}

std::unique_ptr<juce::XmlElement> HostEngine::createChainXml (const juce::String& chainName) const
{
    auto root = std::make_unique<juce::XmlElement> ("LABCHAIN");
    root->setAttribute ("version", 1);
    root->setAttribute ("name", chainName);
    root->setAttribute ("lineRateGBd", (double) link->lineRateGBd.load());
    root->setAttribute ("modulation", link->modulation.load());

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto* node = getNode (i);
        if (node == nullptr)
            continue;

        const auto state = captureState (*node->getProcessor());

        auto* element = root->createNewChildElement ("SLOT");
        element->setAttribute ("bypassed", node->isBypassed());
        element->setAttribute ("state", state.blob.toBase64Encoding());

        for (const auto& [id, value] : state.parameters)
        {
            auto* parameter = element->createNewChildElement ("PARAM");
            parameter->setAttribute ("id", id);
            parameter->setAttribute ("value", (double) value);
        }

        element->addChildElement (slots[i].description.createXml().release());
    }

    if (homework.present)
    {
        auto* hw = root->createNewChildElement ("HOMEWORK");
        hw->setAttribute ("title", homework.title);
        hw->setAttribute ("prompt", homework.prompt);
        hw->setAttribute ("hint", homework.hint);
        hw->setAttribute ("explanation", homework.explanation);
    }

    return root;
}

bool HostEngine::loadChain (const juce::XmlElement& xml, LoadChainCallback onDone)
{
    if (! xml.hasTagName ("LABCHAIN"))
        return false;

    auto chain = std::make_shared<PendingChain>();
    chain->name = xml.getStringAttribute ("name");
    chain->done = std::move (onDone);

    for (auto* slotXml : xml.getChildWithTagNameIterator ("SLOT"))
    {
        PendingChain::PendingSlot pending;
        auto* descriptionXml = slotXml->getChildByName ("PLUGIN");

        if (descriptionXml == nullptr || ! pending.description.loadFromXml (*descriptionXml))
        {
            chain->failures.add ("unreadable plugin entry");
            continue;
        }

        pending.state.blob.fromBase64Encoding (slotXml->getStringAttribute ("state"));

        for (auto* parameter : slotXml->getChildWithTagNameIterator ("PARAM"))
            pending.state.parameters.emplace_back (parameter->getStringAttribute ("id"),
                                                   (float) parameter->getDoubleAttribute ("value"));

        pending.hasState = true;
        pending.bypassed = slotXml->getBoolAttribute ("bypassed");
        chain->slots.push_back (std::move (pending));
    }

    if (xml.hasAttribute ("lineRateGBd"))
        link->lineRateGBd = (float) xml.getDoubleAttribute ("lineRateGBd");
    if (xml.hasAttribute ("modulation"))
        link->modulation = xml.getIntAttribute ("modulation");

    clearChain();   // also resets `homework`, so this has to run before reading HOMEWORK below

    if (auto* hw = xml.getChildByName ("HOMEWORK"))
    {
        homework.present = true;
        homework.title = hw->getStringAttribute ("title");
        homework.prompt = hw->getStringAttribute ("prompt");
        homework.hint = hw->getStringAttribute ("hint");
        homework.explanation = hw->getStringAttribute ("explanation");
    }

    loadPending (chain, 0);
    return true;
}

// Plugins are instantiated one after another so the chain order in the preset
// is the order in the graph, regardless of how long each plugin takes to load.
void HostEngine::loadPending (std::shared_ptr<PendingChain> chain, size_t index)
{
    if (index >= chain->slots.size())
    {
        chain->done (chain->name, chain->failures);
        return;
    }

    auto& pending = chain->slots[index];

    addPlugin (pending.description, pending.hasState ? &pending.state : nullptr, pending.bypassed,
               [this, chain, index] (bool ok, const juce::String& error)
               {
                   if (! ok)
                       chain->failures.add (chain->slots[index].description.name + ": " + error);

                   loadPending (chain, index + 1);
               });
}

juce::File HostEngine::getDeadMansPedalFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DAW_LAB/scan-crash.txt");
}

void HostEngine::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // The graph's channel counts follow the audio device, so the wiring must too.
    if (source == &deviceManager)
    {
        rebuildConnections();
        return;
    }

    if (auto xml = knownPlugins.createXml())
    {
        getSettings()->setValue ("knownPlugins", xml.get());
        getSettings()->saveIfNeeded();
    }
}
