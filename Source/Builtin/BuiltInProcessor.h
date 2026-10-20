#pragma once

#include "LinkSettings.h"
#include <type_traits>

class DdrLinkSettings;   // forward declared only for the category-name trait check below

// Shared plumbing for the SerDes stages: a processor with a parameter tree, state save/restore, and the
// plugin-description boilerplate the host needs to treat it like any other plugin. Templated on which
// shared "link" object the stage carries (LinkSettings for the SerDes side, DdrLinkSettings for the DDR
// side - see DdrBus.h/DdrLinkSettings.h) and how many channels its bus needs: a differential pair is 2
// (stereo, R = +, L = -, the SerDes default), a DDR byte lane's bus is wider (DdrBus::numChannels).
template <typename LinkSettingsType>
class BuiltInProcessorBase : public juce::AudioPluginInstance
{
public:
    BuiltInProcessorBase (const juce::String& identifierToUse, const juce::String& nameToUse,
                          std::shared_ptr<LinkSettingsType> linkSettings,
                          juce::AudioProcessorValueTreeState::ParameterLayout layout,
                          int numChannelsToUse = 2)
        : juce::AudioPluginInstance (BusesProperties()
                                         .withInput ("Input", makeChannelSet (numChannelsToUse), true)
                                         .withOutput ("Output", makeChannelSet (numChannelsToUse), true)),
          link (std::move (linkSettings)),
          identifier (identifierToUse),
          name (nameToUse),
          numChannels (numChannelsToUse),
          apvts (*this, nullptr, "STATE", std::move (layout))
    {
    }

    const juce::String getName() const override { return name; }

    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = name;
        description.descriptiveName = name;
        description.pluginFormatName = formatName;
        description.category = std::is_same_v<LinkSettingsType, DdrLinkSettings> ? "DDR" : "SerDes";
        description.manufacturerName = "DAW_LAB";
        description.version = "1.0";
        description.fileOrIdentifier = identifier;
        description.lastFileModTime = {};
        description.lastInfoUpdateTime = {};
        description.uniqueId = description.deprecatedUid = identifier.hashCode();
        description.isInstrument = false;
        description.numInputChannels = numChannels;
        description.numOutputChannels = numChannels;
        description.hasSharedContainer = false;
    }

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
    LinkSettingsType& getLink() noexcept { return *link; }

    static constexpr const char* formatName = "DAW_LAB Built-in";

    // Identity used by the built-in "format" to recreate this stage from a preset.
    const juce::String& getIdentifier() const noexcept { return identifier; }

protected:
    float getParam (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    std::shared_ptr<LinkSettingsType> link;

private:
    static juce::AudioChannelSet makeChannelSet (int n)
    {
        return n == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::discreteChannels (n);
    }

    juce::String identifier, name;
    int numChannels;

protected:
    juce::AudioProcessorValueTreeState apvts;
};

using BuiltInProcessor = BuiltInProcessorBase<LinkSettings>;
