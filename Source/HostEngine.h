#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Builtin/SharedLink.h"

// Owns everything needed to host a serial chain of plugins: audio/MIDI devices,
// the format manager, the persisted known-plugin list, and the processing graph
// (device input -> plugin 1 -> ... -> plugin N -> device output).
class HostEngine : private juce::ChangeListener
{
public:
    // `startMuted`: nothing reaches the speakers from the first sample (tests use this). Setting the
    // environment variable DAW_LAB_MUTE has the same effect.
    // `useSharedLink`: use the process-wide link that the DAW_LAB plugins share, so VST3/AU stages loaded
    // into this host follow the host's line rate. Tests leave it off and get a private link.
    explicit HostEngine (bool startMuted = false, bool useSharedLink = false);
    ~HostEngine() override;

    struct Slot
    {
        juce::AudioProcessorGraph::NodeID nodeId;
        juce::PluginDescription description;
    };

    // Everything needed to bring a plugin instance back to how it was. Plugins
    // with a real getStateInformation() are restored from `blob`; the parameter
    // values are applied on top so plugins whose state methods are stubs
    // (or that don't persist every parameter) still come back correctly.
    struct SavedState
    {
        juce::MemoryBlock blob;
        std::vector<std::pair<juce::String, float>> parameters;   // paramID (or index) -> normalised value
    };

    using AddCallback       = std::function<void (bool ok, const juce::String& error)>;
    using LoadChainCallback = std::function<void (const juce::String& chainName,
                                                  const juce::StringArray& failures)>;

    // Appends a plugin to the end of the chain. `state` (optional) is applied
    // before the plugin enters the audio path.
    void addPlugin (const juce::PluginDescription& description, const SavedState* state,
                    bool bypassed, AddCallback callback);
    void removePlugin (size_t index);
    void movePlugin (size_t from, size_t to);
    void clearChain();
    void setBypassed (size_t index, bool shouldBypass);

    const std::vector<Slot>& getSlots() const noexcept { return slots; }
    juce::AudioPluginInstance* getPlugin (size_t index) const;
    bool isBypassed (size_t index) const;

    // Chain presets: order, plugin identity, bypass flag and full state of every instance.
    std::unique_ptr<juce::XmlElement> createChainXml (const juce::String& chainName) const;
    // Returns false (and does nothing) if the XML isn't a chain preset.
    bool loadChain (const juce::XmlElement& xml, LoadChainCallback onDone);

    // "Homework": a preset can carry a scenario with something deliberately wrong, a nudge (hint) and the
    // root cause (explanation), for hands-on validation/debug practice. Empty (`present` false) for an
    // ordinary preset, or after Clear Chain. Round-trips through createChainXml/loadChain automatically -
    // there is no UI to author one, these are written by hand into a preset's XML.
    struct Homework
    {
        bool present = false;
        juce::String title, prompt, hint, explanation;
    };
    Homework homework;

    void setOutputMuted (bool shouldMute) noexcept { gate.muted = shouldMute; }
    bool isOutputMuted() const noexcept { return gate.muted.load(); }

    juce::File getDeadMansPedalFile() const;
    juce::PropertiesFile* getSettings() { return properties.getUserSettings(); }

    // Line rate / modulation shared by all built-in SerDes stages.
    std::shared_ptr<LinkSettings> link;

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::ChangeBroadcaster chainChanged;

private:
    struct PendingChain;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildConnections();
    void loadPending (std::shared_ptr<PendingChain> chain, size_t index);
    juce::AudioProcessorGraph::Node* getNode (size_t index) const;
    static SavedState captureState (juce::AudioProcessor&);
    static void applyState (juce::AudioProcessor&, const SavedState&);

    juce::ApplicationProperties properties;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorGraph::Node::Ptr audioIn, audioOut, midiIn;
    // Sits between the device and the player so the output can be muted without stopping the chain:
    // the signals still run (and are measured), they just are not played.
    class OutputGate : public juce::AudioIODeviceCallback
    {
    public:
        explicit OutputGate (juce::AudioProcessorPlayer& p) : player (p) {}
        void audioDeviceAboutToStart (juce::AudioIODevice* d) override { player.audioDeviceAboutToStart (d); }
        void audioDeviceStopped() override { player.audioDeviceStopped(); }
        void audioDeviceError (const juce::String& message) override { player.audioDeviceError (message); }
        void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                               const juce::AudioIODeviceCallbackContext& context) override
        {
            player.audioDeviceIOCallbackWithContext (in, numIn, out, numOut, numSamples, context);
            if (muted.load())
                for (int c = 0; c < numOut; ++c)
                    if (out[c] != nullptr)
                        juce::FloatVectorOperations::clear (out[c], numSamples);
        }
        std::atomic<bool> muted { false };
    private:
        juce::AudioProcessorPlayer& player;
    };

    juce::AudioProcessorPlayer player;
    OutputGate gate { player };
    std::vector<Slot> slots;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HostEngine)
};
