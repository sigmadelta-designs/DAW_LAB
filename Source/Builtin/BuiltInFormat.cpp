#include "BuiltInFormat.h"
#include "DiffFfe.h"
#include "EyeScope.h"
#include "LossyChannel.h"
#include "NoiseInjector.h"
#include "SerdesReceiver.h"
#include "SignalGenerator.h"
#include "SParamChannel.h"
#include "TraceChannel.h"

namespace
{
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::String& identifier,
                                                       std::shared_ptr<LinkSettings> link)
    {
        if (identifier == SignalGenerator::id) return std::make_unique<SignalGenerator> (link);
        if (identifier == DiffFfe::id)         return std::make_unique<DiffFfe> (link);
        if (identifier == LossyChannel::id)    return std::make_unique<LossyChannel> (link);
        if (identifier == TraceChannel::id)    return std::make_unique<TraceChannel> (link);
        if (identifier == SParamChannel::id)   return std::make_unique<SParamChannel> (link);
        if (identifier == NoiseInjector::id)   return std::make_unique<NoiseInjector> (link);
        if (identifier == SerdesReceiver::id)  return std::make_unique<SerdesReceiver> (link);
        if (identifier == EyeScope::id)        return std::make_unique<EyeScope> (link);
        return nullptr;
    }
}

BuiltInFormat::BuiltInFormat (std::shared_ptr<LinkSettings> linkSettings)
    : link (std::move (linkSettings))
{
}

// In signal-flow order.
juce::Array<juce::PluginDescription> BuiltInFormat::getDescriptions()
{
    juce::Array<juce::PluginDescription> descriptions;
    auto scratch = std::make_shared<LinkSettings>();

    for (auto* identifier : { SignalGenerator::id, DiffFfe::id, LossyChannel::id, TraceChannel::id, SParamChannel::id, NoiseInjector::id, SerdesReceiver::id, EyeScope::id })
    {
        juce::PluginDescription description;
        create (identifier, scratch)->fillInPluginDescription (description);
        descriptions.add (description);
    }

    return descriptions;
}

void BuiltInFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& identifier)
{
    for (const auto& description : getDescriptions())
        if (description.fileOrIdentifier == identifier)
            results.add (new juce::PluginDescription (description));
}

bool BuiltInFormat::fileMightContainThisPluginType (const juce::String& identifier)
{
    return identifier.startsWith ("lab.");
}

juce::String BuiltInFormat::getNameOfPluginFromIdentifier (const juce::String& identifier)
{
    for (const auto& description : getDescriptions())
        if (description.fileOrIdentifier == identifier)
            return description.name;

    return {};
}

void BuiltInFormat::createPluginInstance (const juce::PluginDescription& description, double, int,
                                          PluginCreationCallback callback)
{
    if (auto instance = create (description.fileOrIdentifier, link))
        callback (std::move (instance), {});
    else
        callback (nullptr, "Unknown built-in stage: " + description.fileOrIdentifier);
}
