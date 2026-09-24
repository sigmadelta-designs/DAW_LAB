#pragma once

#include "BuiltInProcessor.h"

// Lets the SerDes stages be created through the ordinary plugin-format machinery,
// so chains, presets and the host treat them exactly like AU/VST3 plugins.
// They are not file based, so they are listed directly rather than scanned.
class BuiltInFormat : public juce::AudioPluginFormat
{
public:
    explicit BuiltInFormat (std::shared_ptr<LinkSettings> linkSettings);

    static juce::Array<juce::PluginDescription> getDescriptions();

    juce::String getName() const override { return BuiltInProcessor::formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& identifier) override;
    bool fileMightContainThisPluginType (const juce::String& identifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& identifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription&, double, int, PluginCreationCallback) override;

    std::shared_ptr<LinkSettings> link;
};
