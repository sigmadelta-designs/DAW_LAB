#pragma once

#include "LinkSettings.h"

// Shared plumbing for the SerDes stages: a stereo (R = +, L = -) processor with
// a parameter tree, state save/restore, and the plugin-description boilerplate
// the host needs to treat it like any other plugin.
class BuiltInProcessor : public juce::AudioPluginInstance
{
public:
    BuiltInProcessor (const juce::String& identifierToUse, const juce::String& nameToUse,
                      std::shared_ptr<LinkSettings> linkSettings,
                      juce::AudioProcessorValueTreeState::ParameterLayout layout)
        : juce::AudioPluginInstance (BusesProperties()
                                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          link (std::move (linkSettings)),
          identifier (identifierToUse),
          name (nameToUse),
          apvts (*this, nullptr, "STATE", std::move (layout))
    {
    }

    const juce::String getName() const override { return name; }
    void fillInPluginDescription (juce::PluginDescription& description) const override;

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    void releaseResources() override {}

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    juce::AudioProcessorValueTreeState& getParameterTree() noexcept { return apvts; }
    LinkSettings& getLink() noexcept { return *link; }

    static constexpr const char* formatName = "DAW_LAB Built-in";

    // Identity used by the built-in "format" to recreate this stage from a preset.
    const juce::String& getIdentifier() const noexcept { return identifier; }

protected:
    float getParam (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    std::shared_ptr<LinkSettings> link;

private:
    juce::String identifier, name;

protected:
    juce::AudioProcessorValueTreeState apvts;
};

inline void BuiltInProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    description.name = name;
    description.descriptiveName = name;
    description.pluginFormatName = formatName;
    description.category = "SerDes";
    description.manufacturerName = "DAW_LAB";
    description.version = "1.0";
    description.fileOrIdentifier = identifier;
    description.lastFileModTime = {};
    description.lastInfoUpdateTime = {};
    description.uniqueId = description.deprecatedUid = identifier.hashCode();
    description.isInstrument = false;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
    description.hasSharedContainer = false;
}
