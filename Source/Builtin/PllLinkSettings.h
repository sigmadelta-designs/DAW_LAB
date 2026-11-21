#pragma once

#include <atomic>

// The "link" a PLL domain stage would share, parallel to LinkSettings (SerDes) and DdrLinkSettings (DDR) -
// a third, independent simulation domain on BuiltInFormat's shared app shell. A standalone frequency-
// synthesis PLL is naturally feed-forward (reference clock in, locked clock out), so unlike DDR it has no
// bidirectional-bus problem to solve. PllCore owns its own reference oscillator (the same way DdrController
// owns CK generation rather than needing an upstream clock-source stage), so this link carries no input -
// only PllCore's own live status, published here so a downstream stage that cannot see PllCore directly
// (PllJitterScope, probing the bus a stage later) still knows what frequency each probed channel should be
// running at, the same reason LinkSettings::baudHz()/DdrLinkSettings::busHz() exist for their domains.
class PllLinkSettings
{
public:
    static constexpr double simulationScale = 1.0e6;   // same 1e6 real-Hz-to-audio-Hz convention as LinkSettings/DdrLinkSettings

    std::atomic<float> refFreqHz { 25.0f };    // PllCore's "reffreq" param, as last applied
    std::atomic<float> vcoFreqHz { 1000.0f };  // PllCore's live, instantaneous VCO frequency
    std::atomic<int> fbDivider { 40 };         // PllCore's "fbdivider" param, as last applied
    std::atomic<bool> locked { false };
};
