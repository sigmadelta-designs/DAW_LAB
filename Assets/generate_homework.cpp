// Writes the HOMEWORK_*.labchain presets in Presets/: each one has something deliberately wrong, modelled
// on a real SerDes bring-up/debug scenario, plus a HostEngine::Homework block (title/prompt/hint/
// explanation) that the app's Homework Hint window reads. Not part of the app build - a one-off generator,
// run by hand whenever the homework set needs regenerating or a scenario needs changing.
//
// Build and run it as its own JUCE console app, e.g. from a scratch directory:
//   cmake_minimum_required(VERSION 3.22)
//   project(GenerateHomework)
//   set(CMAKE_CXX_STANDARD 20)
//   include(FetchContent)
//   FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4)
//   FetchContent_MakeAvailable(JUCE)
//   set(B /path/to/LAB_DAW/Source/Builtin)
//   set(E /path/to/LAB_DAW/Source)
//   juce_add_console_app(GenerateHomework PRODUCT_NAME GenerateHomework)
//   target_sources(GenerateHomework PRIVATE generate_homework.cpp
//       ${E}/HostEngine.cpp ${B}/BuiltInFormat.cpp ${B}/BitCompare.cpp ${B}/DiffFfe.cpp ${B}/EyeAnalyzer.cpp
//       ${B}/EyeScope.cpp ${B}/EyeScopeEditor.cpp ${B}/FfeEditor.cpp ${B}/GeneratorEditor.cpp
//       ${B}/LossyChannel.cpp ${B}/LossyEditor.cpp ${B}/NoiseInjector.cpp ${B}/NoiseEditor.cpp
//       ${B}/SignalGenerator.cpp ${B}/SimpleParamEditor.cpp ${B}/ReceiverEditor.cpp ${B}/SerdesReceiver.cpp
//       ${B}/SParamChannel.cpp ${B}/SParamEditor.cpp ${B}/SParamSynthesis.cpp ${B}/Touchstone.cpp
//       ${B}/TraceBoard.cpp ${B}/TraceChannel.cpp ${B}/TraceEditor.cpp ${B}/TraceReflections.cpp
//       ${B}/TraceRouter.cpp ${B}/SymbolProbe.cpp)
//   target_include_directories(GenerateHomework PRIVATE ${B} ${E})
//   target_compile_definitions(GenerateHomework PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
//   target_link_libraries(GenerateHomework PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
// then run the built binary with the preset directory as its one argument, e.g.
//   DAW_LAB_MUTE=1 ./GenerateHomework /path/to/LAB_DAW/Presets

#include "HostEngine.h"
#include "Builtin/BuiltInFormat.h"
#include "Builtin/BitCompare.h"
#include "Builtin/EyeAnalyzer.h"
#include "Builtin/TraceRouter.h"
#include "Builtin/SignalGenerator.h"
#include "Builtin/DiffFfe.h"
#include "Builtin/LossyChannel.h"
#include "Builtin/NoiseInjector.h"
#include "Builtin/SerdesReceiver.h"
#include "Builtin/TraceChannel.h"
#include "Builtin/SParamChannel.h"
#include "Builtin/EyeScope.h"
using namespace juce;

static void pump (const std::function<bool()>& until, int ms = 20000)
{
    auto end = Time::getMillisecondCounter() + (uint32) ms;
    while (! until() && Time::getMillisecondCounter() < end)
        MessageManager::getInstance()->runDispatchLoopUntil (20);
}

static void wait (int ms)
{
    auto end = Time::getMillisecondCounter() + (uint32) ms;
    while (Time::getMillisecondCounter() < end)
        MessageManager::getInstance()->runDispatchLoopUntil (20);
}

static PluginDescription builtin (const char* id)
{
    for (auto& d : BuiltInFormat::getDescriptions())
        if (d.fileOrIdentifier == id)
            return d;
    return {};
}

static void setParam (AudioProcessor& p, const String& id, float real)
{
    for (auto* prm : p.getParameters())
        if (auto* withId = dynamic_cast<RangedAudioParameter*> (prm))
            if (withId->paramID == id)
            {
                withId->setValueNotifyingHost (withId->convertTo0to1 (real));
                return;
            }
    std::printf ("  ! no param %s\n", id.toRawUTF8());
}

struct Homework
{
    const char* file;
    String title, prompt, hint, explanation;
    std::function<void (HostEngine&)> build;   // adds stages, sets params/board, in chain order
    int scopeSlot;                             // slot index of the Eye Scope, for the measurement pass
    int rxSlot = -1;                           // slot index of the Receiver, if present (-1: none)
};

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    File presetDir (argc > 1 ? argv[1] : "Presets");
    presetDir.createDirectory();
    HostEngine engine (true);

    auto addStage = [&] (const char* id)
    {
        bool done = false;
        engine.addPlugin (builtin (id), nullptr, false, [&] (bool, const String&) { done = true; });
        pump ([&] { return done; });
    };

    std::vector<Homework> set;

    // --- 1: TX FFE left flat against a lossy channel - the most common "new board looks terrible" false alarm.
    set.push_back ({
        "HOMEWORK_01_closed_eye_no_tx_eq",
        "Homework 1: \"Is the board bad?\"",
        "A summer intern set up this 10 GBd link to check out a new backplane sample and reports: "
            "\"the eye is basically closed, is the board bad?\" The channel loss looks normal for its length. "
            "Before telling them to reject the board, check the settings upstream of it.",
        "Compare the FFE's applied taps (shown in its editor) to what the channel is actually costing at "
            "Nyquist. Is anything upstream actually trying to compensate for that loss?",
        "The transmit FFE is left completely flat (no pre/post-cursor emphasis) and its auto-adapt is off, "
            "so none of the channel's ~18 dB of loss at Nyquist is being compensated. This isn't a bad board - "
            "turn on FFE auto-adapt (or hand-set taps for the loss) and the eye opens back up. This is the "
            "single most common \"new board looks terrible\" false alarm in a bring-up lab: check the TX "
            "equalization settings before blaming the hardware.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 0.0f);
            setParam (*e.getPlugin (2), "loss", 18.0f);
            setParam (*e.getPlugin (3), "vscale", 0.6f);
        }, 3 });

    // --- 2: FFE tap values are reasonable, but applied backwards - a real tap-direction wiring mistake.
    set.push_back ({
        "HOMEWORK_02_ffe_wrong_direction",
        "Homework 2: Equalizer makes it worse",
        "This link's FFE clearly has real tap values dialed in - someone tried to equalize it - but the eye "
            "looks worse than if the taps were all left at zero. What's going on?",
        "Check the FFE's \"Reverse tap direction\" switch against which taps actually have non-zero values. "
            "Which cursor is meant to fight post-cursor ISI - and is that the one that's active?",
        "The tap VALUES are a reasonable de-emphasis shape (meant as post-cursor taps), but \"Reverse tap "
            "direction\" is turned on, so the FFE applies them backwards - as pre-cursor emphasis instead. "
            "This is a real, easy-to-make configuration mistake (wiring or register-mapping the tap direction "
            "backwards during bring-up), and it actively makes the eye worse than doing nothing, since the "
            "equalizer is now fighting ISI that isn't there while leaving the real post-cursor ISI untouched. "
            "Turning Reverse back off fixes it.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            auto* ffe = e.getPlugin (1);
            setParam (*ffe, "txmode", 1.0f); setParam (*ffe, "adapt", 0.0f); setParam (*ffe, "reverse", 1.0f);
            setParam (*ffe, DiffFfe::getTapParamId (4), -0.22f);   // post1
            setParam (*ffe, DiffFfe::getTapParamId (5), -0.08f);   // post2
            setParam (*ffe, DiffFfe::getTapParamId (6), -0.03f);   // post3
            setParam (*e.getPlugin (2), "loss", 14.0f);
            setParam (*e.getPlugin (3), "vscale", 0.6f);
        }, 3 });

    // --- 3: receiver-end termination effectively missing - reflections that look like noise under real traffic.
    set.push_back ({
        "HOMEWORK_03_missing_termination",
        "Homework 3: Fine on one shot, bad with real traffic",
        "A colleague brought up a new receiver board and says: \"it looks OK on a single-shot scope capture, "
            "but the eye is a mess with real traffic.\" The trace is short and otherwise clean. Look at what's "
            "happening at the far end.",
        "Open the Trace Channel editor's TDR view. Does the impedance profile end where you'd expect (matched "
            "to the line), or does it jump hard right at the very end?",
        "The load resistance is set to 400 ohm against a 50 ohm line - the receiver's on-die termination is "
            "effectively not enabled (or the resistor is missing), so most of the incoming energy reflects "
            "back down the trace instead of being absorbed. That reflected energy from earlier bits interferes "
            "with later ones, which is exactly the \"fine on one shot, terrible with real traffic\" symptom - "
            "a single-shot capture only shows one edge, so it can look clean, but back-to-back real traffic "
            "keeps compounding the reflections. The receiver is present and in the path (its DFE is "
            "deliberately left off for this check, the way you'd isolate the raw channel first before "
            "trusting an adaptive stage to paper over it), so what you're seeing is the termination problem "
            "itself, not a receiver problem. Real fix: check that on-die termination is enabled in the "
            "receiver's control registers, or that the termination resistor is actually populated - then "
            "turn the DFE back on to clean up whatever's left.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (TraceChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            auto* trace = dynamic_cast<TraceChannel*> (e.getPlugin (2));
            TraceRouter::generateGame (trace->getBoard(), 7, 4);
            TraceRouter::solve (trace->getBoard());
            trace->boardEdited();
            setParam (*e.getPlugin (2), "reflections", 2.0f);
            setParam (*e.getPlugin (2), "connector", 0.0f);
            setParam (*e.getPlugin (2), "sourcer", 50.0f);
            setParam (*e.getPlugin (2), "loadr", 8000.0f);
            trace->rebuildProfile();
            auto* rx = e.getPlugin (3);
            setParam (*rx, "ctleboost", 0.0f); setParam (*rx, "dfeadapt", 0.0f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- 4: CDR loop gains far too low to track realistic reference-clock jitter - eye fine, BER bad anyway.
    set.push_back ({
        "HOMEWORK_04_cdr_too_slow",
        "Homework 4: The receiver won't stay locked",
        "This link's channel loss is modest and the eye isn't badly closed, but the receiver's clock recovery "
            "keeps dropping lock - the CDR status in the receiver editor flickers between locked and "
            "unlocked, and bits get lost whenever it does. What's actually going wrong?",
        "Look at the CDR's lock status and loop gains (Kp/Ki) in the receiver editor. Is the clock recovery "
            "loop actually able to track the jitter this link has?",
        "The channel and eye are not the problem - the clock recovery loop's proportional/integral gains "
            "(Kp/Ki) are set far too low to track the sinusoidal jitter present on this link's reference "
            "clock. The CDR falls behind the true data phase, drifts into the transition region, and loses "
            "lock outright rather than settling into a small steady-state phase error. This is a real jitter-"
            "tolerance failure: a CDR's loop bandwidth has to be fast enough to track the jitter spec it's "
            "meant to handle, and \"eye is fine but the receiver won't hold lock\" points squarely at the "
            "clock/timing recovery loop rather than the signal path. The fix is to raise the CDR loop gains "
            "(or confirm the reference clock's jitter is within the CDR's designed tolerance).",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (0), "sjamp", 0.5f); setParam (*e.getPlugin (0), "sjfreq", 20.0f);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 6.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "ctleboost", 3.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*rx, "cdrkp", 0.2f); setParam (*rx, "cdrki", 0.5f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- 5: trace routed hugging a noisy aggressor lane - fails only when the neighbour is active.
    set.push_back ({
        "HOMEWORK_05_aggressor_crosstalk",
        "Homework 5: Fails only when the neighbour is active",
        "Two channels share this board. Channel A tests clean by itself on the bench, but fails BER testing "
            "once channel B (right next to it) is active. The routing looks fine at a glance - find out "
            "what's coupling in.",
        "Check the Trace Channel's Readouts tab for the crosstalk/aggressor numbers (SXR), and look at how "
            "closely the routing runs next to the aggressor lane on the board view.",
        "The trace was routed hugging the noisy aggressor lane for a long parallel run (no lane-aware "
            "routing was used), so the aggressor's pickup dominates the differential noise budget - the SXR "
            "(signal-to-crosstalk ratio) readout is poor even though the trace's own loss and skew are fine. "
            "This is a textbook \"crosstalk from an adjacent trace, via, or connector pin\" bring-up bug: it "
            "only shows up when the aggressor is actually toggling, exactly like a neighbouring lane's "
            "traffic-dependent failure in the field. Note that the receiver's DFE is adapting here too, and "
            "it barely moves the needle - a DFE cancels inter-symbol interference from the wanted signal's "
            "own history, not noise-like interference from an unrelated aggressor, so it can't equalize this "
            "problem away. Re-routing with more spacing (or adding shielding/guard traces) away from the "
            "aggressor lane fixes it.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (TraceChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            auto* trace = dynamic_cast<TraceChannel*> (e.getPlugin (2));
            auto& board = trace->getBoard();
            TraceRouter::generateGame (board, 21, 5);
            TraceRouter::aggressorPenalty = 0.0;    // force a lane-blind route, as if nobody checked
            TraceRouter::solve (board);
            TraceRouter::aggressorPenalty = 0.6;    // restore the default for anything routed afterward
            trace->boardEdited();
            setParam (*e.getPlugin (2), "aggressors", 1.7f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "ctleboost", 4.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- 6: CTLE boost cranked far past what the channel needs - amplifies noise more than signal.
    set.push_back ({
        "HOMEWORK_06_ctle_overboost",
        "Homework 6: \"I turned it up and it got worse\"",
        "Someone \"fixed\" this receiver's poor BER by cranking the CTLE boost all the way up, and it got "
            "worse instead of better. The channel itself isn't that lossy. What actually happened?",
        "Compare the CTLE boost setting to how much loss the channel is actually reporting. What else, "
            "besides the signal, does a CTLE boost amplify?",
        "The channel only has about 8 dB of loss at Nyquist, but the CTLE boost is set near its maximum "
            "(about 17 dB) - far more gain than the channel needs. A CTLE boosts everything at high "
            "frequency, not just the attenuated signal, so the excess boost is mostly amplifying the noise "
            "floor instead of recovering signal, which is why BER got worse rather than better. This is a "
            "real and common equalization mistake: more boost isn't always better past the point that "
            "matches the actual channel loss. The fix is to bring the CTLE boost down to roughly match the "
            "measured/reported channel loss (or turn on CTLE auto-adapt) and let the DFE handle the rest.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (NoiseInjector::id); addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15); setParam (*e.getPlugin (0), "rj", 0.01f);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 0.0f);
            setParam (*e.getPlugin (2), "loss", 8.0f);
            setParam (*e.getPlugin (3), "type", (float) NoiseInjector::white); setParam (*e.getPlugin (3), "level", 0.05f);
            auto* rx = e.getPlugin (4);
            setParam (*rx, "ctleboost", 18.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*e.getPlugin (5), "vscale", 0.6f); setParam (*e.getPlugin (5), "trainer", 0.0f);
        }, 5, 4 });

    // --- 7: a mid-trace connector/via mismatch - both terminations are fine, the echo is from the middle.
    set.push_back ({
        "HOMEWORK_07_bad_connector",
        "Homework 7: Both ends are terminated, so where's the echo from?",
        "Both ends of this link are properly terminated (50 ohm in, 50 ohm out) and the trace itself is "
            "short and clean, yet there's a visible echo in the eye. Terminations aren't the problem this "
            "time - where else would a reflection come from?",
        "A matched source and load don't rule out a reflection from somewhere in the middle of the path. "
            "Check the TDR trace for a bump that isn't at either end.",
        "Source and load are both correctly matched at 50 ohm, but the connector/pad mismatch is set very "
            "high (28%), which models a bad connector or via transition partway along the trace - a real, "
            "physical impedance discontinuity that isn't at either termination. The TDR view shows the bump "
            "sitting in the middle of the trace rather than at an end, which is exactly how you'd localise "
            "this in a real lab: a mid-trace reflection means look at connectors, vias and layer transitions "
            "along the path, not the terminations. Fixing (or here, dialling down) that connector mismatch "
            "removes the echo.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (TraceChannel::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            auto* trace = dynamic_cast<TraceChannel*> (e.getPlugin (2));
            TraceRouter::generateGame (trace->getBoard(), 11, 2);
            TraceRouter::solve (trace->getBoard());
            trace->boardEdited();
            setParam (*e.getPlugin (2), "reflections", 2.0f);
            setParam (*e.getPlugin (2), "connector", 28.0f);
            setParam (*e.getPlugin (2), "sourcer", 50.0f);
            setParam (*e.getPlugin (2), "loadr", 50.0f);
            trace->rebuildProfile();
            setParam (*e.getPlugin (3), "vscale", 0.6f);
        }, 3 });

    // --- 8: no single broken setting - several individually-fine impairments stack up past the BER budget.
    set.push_back ({
        "HOMEWORK_08_margin_stackup",
        "Homework 8: Everything passes, the link still fails",
        "Each individual spec on this link passes when tested in isolation: jitter is within tolerance, "
            "channel loss is within budget, the noise floor is within budget, and the receiver equalization "
            "looks reasonable. Put together, though, the link fails its BER target. What's the actual problem?",
        "There isn't one broken setting to find this time - check the BER and eye margin with everything "
            "combined versus with each impairment turned off one at a time.",
        "This is a margin stack-up: random jitter, sinusoidal jitter, duty-cycle distortion, channel loss "
            "and noise are each individually within a \"reasonable\" range, but they combine to eat more eye "
            "margin than the link can tolerate - the BER only crosses the target once all of them are "
            "present together. This is a real and common validation lesson: passing every spec one at a "
            "time doesn't guarantee the system passes as a whole. Real qualification plans have to test "
            "worst-case combinations (or budget margin across a BER contour) rather than checking each "
            "impairment in isolation. There's no single fix here except reducing overall margin consumption - "
            "e.g. tightening the reference clock's jitter, or giving the receiver more equalization headroom.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (NoiseInjector::id); addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (0), "rj", 0.045f); setParam (*e.getPlugin (0), "sjamp", 0.16f);
            setParam (*e.getPlugin (0), "sjfreq", 5.0f); setParam (*e.getPlugin (0), "dcd", 0.09f);
            auto* ffe = e.getPlugin (1);
            setParam (*ffe, "txmode", 1.0f); setParam (*ffe, "adapt", 0.0f);
            setParam (*ffe, DiffFfe::getTapParamId (4), -0.15f); setParam (*ffe, DiffFfe::getTapParamId (5), -0.05f);
            setParam (*e.getPlugin (2), "loss", 14.0f);
            setParam (*e.getPlugin (3), "type", (float) NoiseInjector::white); setParam (*e.getPlugin (3), "level", 0.042f);
            auto* rx = e.getPlugin (4);
            setParam (*rx, "ctleboost", 6.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*e.getPlugin (5), "vscale", 0.6f); setParam (*e.getPlugin (5), "trainer", 0.0f);
        }, 5, 4 });

    // --- 9: DFE taps hand-entered with the wrong sign - the same mistake as #2, at the other end of the link.
    set.push_back ({
        "HOMEWORK_09_dfe_wrong_sign",
        "Homework 9: The DFE is dialed in, but it's not helping",
        "Someone hand-entered this receiver's DFE taps instead of letting it adapt, and the eye is worse "
            "than with the DFE simply switched off. The tap magnitudes look like they came from a real "
            "characterization. What's wrong with them?",
        "Compare the sign of the DFE tap(s) to the sign of the actual post-cursor ISI the channel leaves "
            "behind. A DFE tap is a cancellation term - does its sign cancel the ISI, or add to it?",
        "The DFE's auto-adapt is off and tap h1 was hand-set with the wrong sign - negative instead of "
            "positive. A DFE tap works by subtracting a scaled copy of the previous decision from the "
            "current sample to cancel that decision's leftover ISI contribution; with the sign flipped, it "
            "adds that contribution back in rather than removing it, making the ISI worse than doing "
            "nothing. This is a real, easy mistake when taps are copied by hand from a datasheet, a "
            "different board revision, or a different sign convention than the one this receiver actually "
            "uses - always double-check the sign convention, or just turn auto-adapt back on.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 0.0f);
            setParam (*e.getPlugin (2), "loss", 16.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "ctleboost", 5.0f); setParam (*rx, "dfeadapt", 0.0f);
            setParam (*rx, "dfe1", -0.4f);   // wrong sign: should be positive to cancel post-cursor ISI in this model
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- 10: TX and RX equalization both still set for a board that no longer exists - double over-equalization.
    set.push_back ({
        "HOMEWORK_10_stale_calibration",
        "Homework 10: Passed on the old board, fails on the new one",
        "This link's settings were carried over from the previous board revision, which everyone agreed was "
            "a good starting point. The new board is actually a cleaner design, but the link now fails. "
            "Nothing in the settings is obviously wrong by itself - find out what carrying the settings over "
            "actually did.",
        "Add up ALL the equalization in this chain - the TX FFE's boost AND the receiver's CTLE boost - and "
            "compare that total to how lossy this particular channel actually is.",
        "The TX FFE taps and the receiver's CTLE boost were both tuned for the old board's higher loss, and "
            "both were left exactly as they were for the new, less lossy board. Neither setting is wrong in "
            "isolation - each would be reasonable alone for a channel this lossy - but applied together they "
            "add up to significantly more equalization than the actual channel needs, and the excess boost "
            "mostly amplifies noise rather than recovering signal (the same mechanism as over-boosting the "
            "CTLE alone, just split across two stages so it's less obvious from either one). This is a real "
            "validation lesson: equalization settings don't automatically carry over across a hardware "
            "revision, and a setting that looks fine on its own stage can still be wrong in the context of "
            "the whole chain. The fix is to re-tune (or re-run auto-adapt on) both stages together for the "
            "new board, not reuse either setting from the old one. (Watch the CDR lock status for a while, "
            "too - the excess noise gain from the double boost is enough to make lock intermittent on this "
            "particular link, not just the eye smaller.)",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (NoiseInjector::id); addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            auto* ffe = e.getPlugin (1);
            setParam (*ffe, "txmode", 1.0f); setParam (*ffe, "adapt", 0.0f);
            // a reasonable TX FFE for an old ~18 dB board...
            setParam (*ffe, DiffFfe::getTapParamId (4), -0.28f); setParam (*ffe, DiffFfe::getTapParamId (5), -0.10f);
            setParam (*ffe, DiffFfe::getTapParamId (6), -0.04f);
            setParam (*e.getPlugin (2), "loss", 3.0f);   // ...but the new board only has about 3 dB of loss
            setParam (*e.getPlugin (3), "type", (float) NoiseInjector::white); setParam (*e.getPlugin (3), "level", 0.025f);
            auto* rx = e.getPlugin (4);
            setParam (*rx, "ctleboost", 14.0f);   // ...and the receiver's CTLE is still set for the old board too
            setParam (*rx, "dfeadapt", 1.0f);
            setParam (*e.getPlugin (5), "vscale", 0.6f); setParam (*e.getPlugin (5), "trainer", 0.0f);
        }, 5, 4 });

    // --- 11: an S-parameter channel's ports mapped to the wrong physical pair - a fixture-wiring mistake.
    set.push_back ({
        "HOMEWORK_11_sparam_wrong_ports",
        "Homework 11: The measured file doesn't match the bench",
        "This channel is a real measured Touchstone file from a 4-port VNA sweep of the actual board, not a "
            "synthetic model - so it should be trustworthy. But the eye through it looks far worse than the "
            "board's own datasheet suggests it should. Something about how the file is being used is wrong.",
        "Open the S-Param Channel editor and check the \"Ports\" setting against how the fixture was "
            "actually cabled to the VNA when this file was captured. A 4-port file can be read with more "
            "than one port-pair assignment.",
        "The Touchstone file itself is fine, but the \"Ports\" selection tells this stage which physical "
            "port pair is the input differential pair and which is the output - and it's set to the wrong "
            "mapping for how this particular fixture was cabled. Instead of pairing the two traces together "
            "(the real differential path), this mapping pairs each trace's own near and far ends against "
            "each other - two separate single-ended through-measurements, not a differential pair at all - "
            "which is a physically different, far lossier network. The result isn't a slightly worse eye, "
            "it's a link that barely passes any usable signal: the receiver cannot even recover bits "
            "reliably enough for the sent/received comparison to lock onto an alignment. This is a real and "
            "easy channel-characterization mistake: a VNA sweep with the ports cabled in a different order "
            "than the analysis assumes produces a completely valid-looking S-parameter file that models the "
            "wrong physical thing. The fix is to confirm the actual port wiring against the fixture and set "
            "\"Ports\" to match, not to distrust the measurement itself.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (SParamChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            auto* sparam = dynamic_cast<SParamChannel*> (e.getPlugin (2));
            String err;
            sparam->loadFile (File ("/Users/secdef/LAB_DAW/Channels/fr4_8in_coupled_pair.s4p"), err);
            setParam (*e.getPlugin (2), "ports", 1.0f);   // wrong mapping: this file wants P1,P2 -> P3,P4 (ports = 0)
            auto* rx = e.getPlugin (3);
            setParam (*rx, "ctleboost", 6.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- 12: probing the receiver's analog CTLE output instead of the retimed data - a measurement-point bug.
    set.push_back ({
        "HOMEWORK_12_wrong_probe_point",
        "Homework 12: The eye looks mediocre, but the link isn't failing",
        "This link's eye diagram looks unconvincing - a bit soft, not fully settled - and you'd expect "
            "marginal BER to go with it. But the bit-error counter in the receiver's compare panel shows "
            "zero errors over a long run. The eye and the BER are telling two different stories. Why?",
        "Check the receiver's \"Output\" setting - which internal node does it actually send downstream to "
            "the Eye Scope? Is that the same point the bit-error counter is measuring from?",
        "The receiver's \"Output\" is set to \"CTLE output (analog)\" - a tap before the DFE and the "
            "retiming slicer - so the Eye Scope is displaying the signal before the stage that actually "
            "cleans it up, while the bit-error counter compares the fully equalized, retimed, decided bits "
            "regardless of what the Output mux is set to. Both readings are correct; they're just not "
            "reading the same point in the circuit. This is a very real measurement-methodology mistake: "
            "probing an earlier test point than the one you think you're probing, and concluding the link is "
            "marginal when it isn't. Setting Output to \"Recovered data (retimed)\" shows the eye that "
            "actually matches the BER.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 16.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "ctleboost", 5.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*rx, "output", 0.0f);   // CTLE output (analog) - before the DFE and the slicer
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- 13: the DFE's adaptation step is far larger than it needs to be - it hunts instead of settling.
    set.push_back ({
        "HOMEWORK_13_dfe_step_too_large",
        "Homework 13: The DFE never quite settles",
        "This receiver's DFE is auto-adapting and the link mostly works, but the recovered eye has a "
            "persistent jitter/noise floor that won't go away no matter how long you let it run, and the DFE "
            "tap readout keeps twitching even once the link has been running steadily for a while. What's "
            "stopping it from settling down?",
        "Look at the DFE's adaptation step size. A sign-sign LMS loop with too large a step doesn't "
            "converge to a point - what does it do instead?",
        "The DFE's adaptation step is set far larger than it needs to be for this little channel loss. A "
            "sign-sign LMS loop the DFE uses corrects its taps by a fixed step each symbol in whichever "
            "direction reduces the current error - with a small step it settles close to the optimum and "
            "stays there; with too large a step it overshoots every time, so instead of converging it "
            "oscillates (\"hunts\") around the optimum indefinitely, adding its own residual noise on top "
            "of whatever ISI it's trying to cancel. This loop-gain-versus-steady-state-error trade-off is "
            "fundamental to any adaptive equalizer, not unique to this tool. The fix is simply a smaller "
            "step size - slower to converge at first, but steady once it gets there.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 0.0f);
            setParam (*e.getPlugin (2), "loss", 18.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "ctleboost", 5.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*rx, "dfestep", 0.01f);   // far above the default ~0.001
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- write ---
    for (auto& hw : set)
    {
        engine.clearChain();
        hw.build (engine);
        engine.homework.present = true;
        engine.homework.title = hw.title;
        engine.homework.prompt = hw.prompt;
        engine.homework.hint = hw.hint;
        engine.homework.explanation = hw.explanation;
        engine.createChainXml (hw.file)->writeTo (presetDir.getChildFile (String (hw.file) + ".labchain"));
        std::printf ("wrote %s\n", hw.file);
    }

    // --- reload each and measure live, so the symptom described is a symptom you can actually see/measure ---
    for (auto& hw : set)
    {
        engine.clearChain();
        engine.link->lineRateGBd = 3.0f;
        auto xml = parseXML (presetDir.getChildFile (String (hw.file) + ".labchain"));
        bool done = false; StringArray fails;
        engine.loadChain (*xml, [&] (const String&, const StringArray& f) { fails = f; done = true; });
        pump ([&] { return done; });

        if (! fails.isEmpty()) { std::printf ("LOAD PROBLEM %s: %s\n", hw.file, fails.joinIntoString ("; ").toRawUTF8()); continue; }
        if (! engine.homework.present) { std::printf ("HOMEWORK LOST ON RELOAD %s\n", hw.file); continue; }

        auto* scope = dynamic_cast<EyeScope*> (engine.getPlugin ((size_t) hw.scopeSlot));
        wait (6000);

        BitCompare compare;
        if (hw.rxSlot >= 0)
        {
            for (int i = 0; i < 60; ++i) { wait (100); compare.update (engine.link->txBits, engine.link->rxBits, (i % 4) == 0); }
        }

        scope->setCaptureActive (true);
        EyeAnalyzer analyzer;
        analyzer.setup (engine.link->samplesPerUI (scope->getSampleRate()), engine.link->isPam4(), 0.6f);
        std::vector<float> s; std::vector<double> idx;
        for (int t = 0; t < 90; ++t)
        {
            wait (33); s.clear(); idx.clear(); scope->readCaptured (s, idx);
            size_t rs = 0;
            for (size_t i = 1; i <= s.size(); ++i)
                if (i == s.size() || idx[i] - idx[i - 1] > 1.5)
                {
                    if (i > rs) analyzer.push (s.data() + rs, (int) (i - rs), idx[rs]);
                    rs = i;
                }
        }
        scope->setCaptureActive (false);
        auto m = analyzer.getMetrics();

        if (hw.rxSlot >= 0)
        {
            auto& st = compare.getStats();
            auto* rx = dynamic_cast<SerdesReceiver*> (engine.getPlugin ((size_t) hw.rxSlot));
            if (st.compared == 0)
                std::printf ("LIVE %-38s eye h=%5.1f%% w=%3.0f%% | NO VALID BITS RECOVERED (never synced) | locked=%d\n",
                             hw.file, m.heightPct, m.widthPct, rx ? (int) rx->status.locked.load() : -1);
            else
                std::printf ("LIVE %-38s eye h=%5.1f%% w=%3.0f%% | %6llu bits %5llu err BER %s | locked=%d\n", hw.file, m.heightPct, m.widthPct,
                             (unsigned long long) st.compared, (unsigned long long) st.errors,
                             (st.errors == 0 ? ("<" + String (compare.berUpperBound95(), 1, true)) : String (compare.ber(), 1, true)).toRawUTF8(),
                             rx ? (int) rx->status.locked.load() : -1);
        }
        else
        {
            std::printf ("LIVE %-38s eye h=%5.1f%% w=%3.0f%% | NMSE %6.1f dB\n", hw.file, m.heightPct, m.widthPct,
                         LinkSettings::nmseToDb (engine.link->probe.nmse.load()));
        }
    }

    return 0;
}
