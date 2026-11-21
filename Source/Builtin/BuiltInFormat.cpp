#include "BuiltInFormat.h"
#include "DdrChip.h"
#include "DdrController.h"
#include "DdrEyeScope.h"
#include "DdrLossyChannel.h"
#include "DiffFfe.h"
#include "EyeScope.h"
#include "JitterScope.h"
#include "LossyChannel.h"
#include "NoiseInjector.h"
#include "PllCore.h"
#include "PllJitterScope.h"
#include "SerdesReceiver.h"
#include "SignalGenerator.h"
#include "SParamChannel.h"
#include "TraceChannel.h"

namespace
{
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::String& identifier,
                                                       const std::shared_ptr<LinkSettings>& link,
                                                       const std::shared_ptr<DdrLinkSettings>& ddrLink,
                                                       const std::shared_ptr<PllLinkSettings>& pllLink)
    {
        if (identifier == SignalGenerator::id) return std::make_unique<SignalGenerator> (link);
        if (identifier == DiffFfe::id)         return std::make_unique<DiffFfe> (link);
        if (identifier == LossyChannel::id)    return std::make_unique<LossyChannel> (link);
        if (identifier == TraceChannel::id)    return std::make_unique<TraceChannel> (link);
        if (identifier == SParamChannel::id)   return std::make_unique<SParamChannel> (link);
        if (identifier == NoiseInjector::id)   return std::make_unique<NoiseInjector> (link);
        if (identifier == SerdesReceiver::id)  return std::make_unique<SerdesReceiver> (link);
        if (identifier == EyeScope::id)        return std::make_unique<EyeScope> (link);
        if (identifier == JitterScope::id)     return std::make_unique<JitterScope> (link);
        if (identifier == DdrController::id)   return std::make_unique<DdrController> (ddrLink);
        if (identifier == DdrChip::id)         return std::make_unique<DdrChip> (ddrLink);
        if (identifier == DdrLossyChannel::id) return std::make_unique<DdrLossyChannel> (ddrLink);
        if (identifier == DdrEyeScope::id)     return std::make_unique<DdrEyeScope> (ddrLink);
        if (identifier == PllCore::id)         return std::make_unique<PllCore> (pllLink);
        if (identifier == PllJitterScope::id)  return std::make_unique<PllJitterScope> (pllLink);
        return nullptr;
    }
}

BuiltInFormat::BuiltInFormat (std::shared_ptr<LinkSettings> linkSettings)
    : link (std::move (linkSettings))
{
}

// In signal-flow order. The DDR and PLL stages are their own domains (see BuiltInFormat.h) - listed
// after the SerDes ones, not interleaved with them.
juce::Array<juce::PluginDescription> BuiltInFormat::getDescriptions()
{
    juce::Array<juce::PluginDescription> descriptions;
    auto scratch = std::make_shared<LinkSettings>();
    auto ddrScratch = std::make_shared<DdrLinkSettings>();
    auto pllScratch = std::make_shared<PllLinkSettings>();

    for (auto* identifier : { SignalGenerator::id, DiffFfe::id, LossyChannel::id, TraceChannel::id, SParamChannel::id, NoiseInjector::id,
                             SerdesReceiver::id, EyeScope::id, JitterScope::id, DdrController::id, DdrLossyChannel::id, DdrChip::id,
                             DdrEyeScope::id, PllCore::id, PllJitterScope::id })
    {
        juce::PluginDescription description;
        create (identifier, scratch, ddrScratch, pllScratch)->fillInPluginDescription (description);
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
    if (auto instance = create (description.fileOrIdentifier, link, ddrLink, pllLink))
        callback (std::move (instance), {});
    else
        callback (nullptr, "Unknown built-in stage: " + description.fileOrIdentifier);
}
