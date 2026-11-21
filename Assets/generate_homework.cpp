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
//       ${B}/JitterAnalyzer.cpp ${B}/JitterScope.cpp ${B}/JitterScopeEditor.cpp
//       ${B}/DdrController.cpp ${B}/DdrChip.cpp ${B}/DdrChipEditor.cpp ${B}/DdrEyeScope.cpp
//       ${B}/DdrEyeScopeEditor.cpp ${B}/DdrLossyChannel.cpp
//       ${B}/PllCore.cpp ${B}/PllCoreEditor.cpp ${B}/PllJitterScope.cpp ${B}/PllJitterScopeEditor.cpp
//       ${B}/LossyChannel.cpp ${B}/LossyEditor.cpp ${B}/NoiseInjector.cpp ${B}/NoiseEditor.cpp
//       ${B}/SignalGenerator.cpp ${B}/SimpleParamEditor.cpp ${B}/ReceiverEditor.cpp ${B}/SerdesReceiver.cpp
//       ${B}/SParamChannel.cpp ${B}/SParamEditor.cpp ${B}/SParamSynthesis.cpp ${B}/Touchstone.cpp
//       ${B}/TraceBoard.cpp ${B}/TraceChannel.cpp ${B}/TraceEditor.cpp ${B}/TraceReflections.cpp
//       ${B}/TraceRouter.cpp ${B}/SymbolProbe.cpp)
//   target_include_directories(GenerateHomework PRIVATE ${B} ${E})
//   target_compile_definitions(GenerateHomework PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
//   target_link_libraries(GenerateHomework PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
// then run the built binary with the preset directory as its one argument, e.g.
//   DAW_LAB_MUTE=1 ./GenerateHomework /path/to/LAB_DAW/Presets

#include "HostEngine.h"
#include "Builtin/BuiltInFormat.h"
#include "Builtin/BitCompare.h"
#include "Builtin/EyeAnalyzer.h"
#include "Builtin/JitterAnalyzer.h"
#include "Builtin/TraceRouter.h"
#include "Builtin/SignalGenerator.h"
#include "Builtin/DiffFfe.h"
#include "Builtin/LossyChannel.h"
#include "Builtin/NoiseInjector.h"
#include "Builtin/SerdesReceiver.h"
#include "Builtin/TraceChannel.h"
#include "Builtin/SParamChannel.h"
#include "Builtin/EyeScope.h"
#include "Builtin/JitterScope.h"
#include "Builtin/DdrController.h"
#include "Builtin/DdrChip.h"
#include "Builtin/DdrEyeScope.h"
#include "Builtin/DdrLossyChannel.h"
#include "Builtin/PllCore.h"
#include "Builtin/PllJitterScope.h"
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
    int scopeSlot = -1;                        // slot index of the Eye Scope, for the measurement pass
    int rxSlot = -1;                           // slot index of the Receiver, if present (-1: none)
    int jitterSlot = -1;                       // slot index of the Jitter Scope, if present (-1: none)
    int ddrControllerSlot = -1;                // slot index of the DDR Controller - a wholly different
                                                // measurement path (BIST word counters, not an eye/BER)
    int ddrScopeSlot = -1;                     // slot index of the DDR Eye Scope, if present (-1: none) -
                                                // a DIFFERENT measurement from scopeSlot/EyeScope above
    int pllCoreSlot = -1;                      // slot index of the PLL Core - a third, wholly different
                                                // measurement path (lock status/frequency over time, not
                                                // an eye/BER or a BIST word counter)
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
    // The bring-up & validation plan: BRINGUP_01..NN, a step-by-step walk through standing up a SerDes
    // link (TX alone, then channel, then TX EQ, then RX clocking, then RX linear/decision-feedback EQ,
    // then the closed adaptive loop, then margin stress, then final sign-off) rather than a flat set of
    // independent scenarios. Same Homework struct/mechanism as `set` above - the Bring-Up window in the
    // app just scans for this filename prefix and walks it in sorted (= step) order.
    std::vector<Homework> bringups;

    // The DDR-domain counterpart to `bringups` above: DDRBRINGUP_01..NN, a step-by-step DRAM bring-up
    // walk (chip-select sanity -> channel characterization -> ZQ cal -> CS/CA training -> write
    // levelling -> per-bit DQ deskew -> Vref training -> margin stress -> final sign-off), in the real
    // JEDEC-ish training order rather than a flat set of independent scenarios. Deliberately a DIFFERENT
    // filename prefix from `bringups` (DDRBRINGUP_, not BRINGUP_DDR_) so the two plans can live in the
    // same preset folder without the generic "BRINGUP_*" scan picking up both as one sequence - the app
    // opens them from two separate buttons/windows instead.
    std::vector<Homework> ddrBringups;

    // The PLL domain's counterpart to `bringups`/`ddrBringups` above: PLLBRINGUP_01..NN, a step-by-step
    // frequency-synthesizer bring-up (free-run VCO check -> close the loop with the right divider ->
    // bring the loop gains up to spec -> tune out ringing -> stress-test against reference jitter ->
    // final sign-off), in a real bench bring-up order rather than a flat set of independent scenarios.
    // Deliberately yet another distinct filename prefix (PLLBRINGUP_, not BRINGUP_PLL_ or DDRBRINGUP_PLL_)
    // so all three plans coexist in one preset folder without any of their filename-prefix scans picking
    // up another plan's files.
    std::vector<Homework> pllBringups;

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

    // --- 14: a strong, specific periodic jitter tone riding in on an otherwise reasonable-looking link -
    // the Eye Scope alone can't tell you it's periodic, let alone its frequency; the Jitter Scope can.
    set.push_back ({
        "HOMEWORK_14_emi_coupled_jitter",
        "Homework 14: The eye looks fine, BER isn't",
        "This link's eye diagram looks reasonably open and the receiver is equalizing normally, but BER "
            "testing still shows a measurable error rate that the eye alone doesn't explain. The bench next "
            "to this one was recently set up with a board that uses a common 100 MHz reference oscillator. "
            "Is that actually related, or a coincidence?",
        "Add a Jitter / Phase Noise Scope to the chain and look at its spectral density plot. Is there a "
            "narrow spike standing out above the noise floor - and if so, what frequency is it at?",
        "The transmitted signal has a strong sinusoidal jitter component at exactly 100 MHz offset - the same "
            "frequency as that neighbouring board's reference oscillator. A single, specific spectral line "
            "like this is invisible as a distinguishable root cause from the eye diagram alone: the eye just "
            "looks a little softer than expected, which could be blamed on dozens of things. The Jitter / "
            "Phase Noise Scope's spectrum is the right tool here - a narrow, isolated line (not a broad, flat "
            "noise floor) is the signature of a specific external aggressor, not generic random jitter, and "
            "its exact frequency is often enough to identify the culprit by matching it against known clocks "
            "and switching frequencies nearby, exactly like this 100 MHz coincidence. There's no setting to "
            "flip here - the fix is physical (shielding, spacing, or a guard trace), but finding it starts "
            "with recognising that spike and reading off its frequency, not guessing from the eye.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (JitterScope::id); addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (0), "rj", 0.03f);
            setParam (*e.getPlugin (0), "sjamp", 0.42f); setParam (*e.getPlugin (0), "sjfreq", 100.0f);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 12.0f);
            auto* rx = e.getPlugin (4);
            setParam (*rx, "ctleboost", 3.3f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*e.getPlugin (5), "vscale", 0.6f); setParam (*e.getPlugin (5), "trainer", 0.0f);
        }, 5, 4, 3 });

    // --- 15: a CDR with perfectly normal loop gains simply can't track sinusoidal jitter this fast - a
    // jitter-tolerance-vs-frequency issue, not a misconfigured receiver (contrast with #4).
    set.push_back ({
        "HOMEWORK_15_jitter_tolerance_exceeded",
        "Homework 15: Same jitter spec, different frequency, different result",
        "This link passed its jitter tolerance check comfortably at low offset frequency. For a compliance "
            "sweep, someone re-ran the same nominal sinusoidal jitter amplitude at a much higher offset "
            "frequency, and now the receiver can't hold a clean lock. Nobody touched the receiver's clock "
            "recovery settings between the two runs. What's actually different?",
        "Measure the jitter actually hitting the receiver with the Jitter / Phase Noise Scope - read off the "
            "dominant tone's frequency. Then check the receiver's CDR loop gains (Kp/Ki) in its editor - are "
            "they unusually low, or just normal?",
        "The CDR's Kp/Ki are left at their normal defaults - this isn't a misconfigured receiver (contrast "
            "that with the earlier \"CDR too slow\" homework, where the gains themselves were the problem). "
            "The sinusoidal jitter here is simply at a much higher offset frequency than before, with the "
            "same amplitude - and every clock recovery loop has a limited tracking bandwidth no matter how "
            "well-tuned its gains are. Jitter well below that bandwidth gets tracked (the CDR's recovered "
            "phase just follows it, so bits still land in the right slot); the same amplitude far above it "
            "cannot be tracked, so the phase error eats directly into the slicer's margin instead. This is "
            "exactly the idea behind a real jitter-tolerance mask in SerDes compliance specs: tolerable "
            "sinusoidal jitter amplitude is large at low offset frequency and drops off sharply at high "
            "offset frequency, tracing out the receiver's own CDR bandwidth. Passing a jitter tolerance check "
            "once, at one frequency, doesn't prove it passes across the whole spec - you have to sweep "
            "frequency, not just amplitude, and the Jitter / Phase Noise Scope is how you confirm what "
            "frequency you actually stressed it at.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (JitterScope::id); addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (0), "sjamp", 0.58f); setParam (*e.getPlugin (0), "sjfreq", 100.0f);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 6.0f);
            auto* rx = e.getPlugin (4);
            setParam (*rx, "ctleboost", 3.0f); setParam (*rx, "dfeadapt", 1.0f);
            // cdrkp/cdrki deliberately left at their normal defaults (3.0 / 30.0) - the point is that even
            // a properly-tuned loop has a bandwidth limit, not that this one is misconfigured.
            setParam (*e.getPlugin (5), "vscale", 0.6f); setParam (*e.getPlugin (5), "trainer", 0.0f);
        }, 5, 4, 3 });

    // --- 16: elevated BER that's actually just broadband random jitter, not a periodic aggressor - the
    // negative-result counterpart to #14: confirm there's no tone before spending a day chasing one.
    set.push_back ({
        "HOMEWORK_16_random_jitter_not_a_tone",
        "Homework 16: Chasing a tone that isn't there",
        "BER on this link is worse than expected, and a colleague is already convinced it's a new EMI source "
            "coupling in from somewhere on the board - they want to start probing around with a near-field "
            "H-field probe to find it. Before spending a day chasing hardware, check whether there's actually "
            "a dominant periodic component to chase in the first place.",
        "Look at the Jitter / Phase Noise Scope's spectrum - is there a clear, narrow spike standing out "
            "above the noise floor, or does the whole plot look like a fairly flat, noisy floor? Check the "
            "TIE histogram's shape too.",
        "There's no dominant tone here - the spectrum is broadly flat (noisy, but without a standout spike), "
            "and the TIE histogram has a smooth, bell-like (roughly Gaussian) shape rather than the sharper, "
            "more bounded shape a strong sinusoidal component would produce. This link's jitter is dominated "
            "by elevated broadband random jitter (RJ) - e.g. a noisier-than-expected reference clock "
            "synthesizer or wideband supply noise - not a specific coupled aggressor. This is a genuinely "
            "useful negative result: confirming the absence of a periodic/EMI cause before spending real lab "
            "time physically hunting for a phantom tone that was never there. The place to look instead is "
            "broadband noise sources - a clock synthesizer's phase-noise floor, supply ripple's noise floor, "
            "thermal effects - not a single coupled clock. The lesson isn't \"it's never EMI\" (homework 14 "
            "shows a case where it genuinely was) - it's that you look at the spectrum either way instead of "
            "assuming.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (JitterScope::id); addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (0), "rj", 0.065f);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 10.0f);
            auto* rx = e.getPlugin (4);
            setParam (*rx, "ctleboost", 4.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*e.getPlugin (5), "vscale", 0.6f); setParam (*e.getPlugin (5), "trainer", 0.0f);
        }, 5, 4, 3 });

    // --- 17 (DDR): controller and chip simply disagree on which CS line is in use - no signal problem
    // at all, a pure protocol/strapping mismatch, the simplest possible DDR bring-up bug.
    set.push_back ({
        "HOMEWORK_17_ddr_wrong_chip_targeted",
        "DDR Homework 1: Reads never come back",
        "This board has exactly one DDR chip populated, but the built-in memory test never passes a "
            "single word - every read comes back wrong, as if nothing is even there. CK and the command "
            "bus are clearly toggling into the chip (you can see the activity), so it isn't dead or "
            "unpowered. What's actually missing?",
        "Check the controller's Target chip setting against the chip's own Chip number setting - do they "
            "agree on which CS line is actually in use? A DDR Eye Scope on the bus confirms this isn't a "
            "signal-integrity question, though notice how long it takes to build any picture at all.",
        "The controller's Target chip is set to chip 1, but the only chip on this board is strapped as "
            "chip number 0 - the controller is asserting CS1, a line nothing on this board is wired to "
            "answer. The chip isn't broken or unselected by a signal problem; it's faithfully ignoring "
            "every command because CS1 was simply never its line. This is a real, easy bring-up mistake: a "
            "rank-select register programmed for a populated slot that doesn't match the board, or a strap "
            "wired to the wrong CS pin during assembly. Matching Target chip to the chip that's actually "
            "there fixes it immediately - there's nothing to retrain or re-tune. (A DDR Eye Scope dropped "
            "into this chain eventually shows a wide-open DQ/DQS eye - the bus itself was never the "
            "problem - but it takes unusually long to get there: every read here times out waiting for an "
            "answer that will never come, and with no write traffic in between, the scope sees almost "
            "nothing but dead air. That sluggishness is itself a clue worth noticing, not just an "
            "inconvenience.)",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 1.0f);   // asks for chip 1...
            setParam (*e.getPlugin (2), "chipnumber", 0.0f);   // ...but the only chip here is strapped as 0
        }, -1, -1, -1, 0, 1 });

    // --- 18 (DDR): Vref trained for a near-ideal bench connection, carried over unchanged to a real,
    // lossier board trace - needs the new DDR Lossy Channel stage to be a genuine signal-level scenario.
    set.push_back ({
        "HOMEWORK_18_ddr_vref_not_retrained",
        "DDR Homework 2: Passed on the bench, fails once installed",
        "This chip was characterized and Vref-trained on an open bench fixture, where it tested perfectly. "
            "Installed on the real board, with the real trace between the controller and the chip, the "
            "memory test now fails almost every word. The chip tests fine again if you put it back on the "
            "bench fixture. What changed?",
        "Compare the DQ swing this board's trace actually delivers (the DDR Lossy Channel's setting) "
            "against the Vref the chip is still using. Was Vref retrained for this board's real channel, "
            "or carried over from somewhere else? A DDR Eye Scope placed after the trace will show the "
            "real, attenuated swing directly rather than making you infer it from a setting.",
        "This board's real trace attenuates the DQ/DQS swing to about 30% of ideal, but the chip's Vref is "
            "still 0.6 - left over from the bench fixture's essentially ideal, near-full-swing connection. "
            "A Vref trained for a near-ideal swing sits far outside the real, attenuated eye, so almost "
            "every bit slices to the same fixed value no matter what's actually driven. This is a real and "
            "common bring-up lesson: Vref (like any receiver calibration) has to be retrained for the "
            "ACTUAL channel a part will see in the field, not assumed to carry over from a bench or "
            "reference setup - a part that tests perfectly in isolation can still fail once installed on "
            "the real board it has to work on. This is the one DDR homework in this set where a DDR Eye "
            "Scope actually shows something degraded on the bus itself - though read the right number: the "
            "eye's own \"% of ideal\" quality readout still says 100%, because nothing else is wrong with "
            "its shape; what actually shows the attenuation is the absolute swing (\"Outer swing at sample "
            "point\"), which reads about 0.6 FS here against this bus's normal 2.0 FS, and the trace itself "
            "renders visibly small against the scope's fixed graticule. Contrast every other DDR homework "
            "here, where the fault is inside the chip's own trained settings and the bus eye - both its "
            "shape and its absolute swing - looks perfectly normal regardless.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrLossyChannel::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            setParam (*e.getPlugin (1), "swing", 30.0f);       // this board's real trace: only 30% of ideal swing survives
            setParam (*e.getPlugin (3), "chipnumber", 0.0f);
            setParam (*e.getPlugin (3), "vref", 0.6f);         // trained for the bench fixture's near-ideal swing, not this
        }, -1, -1, -1, 0, 2 });

    // --- 19 (DDR): two populated chips, but both strapped as the same chip number - a board-assembly
    // mistake, not a missing/dead part, and a different root cause from #17 despite the same symptom.
    set.push_back ({
        "HOMEWORK_19_ddr_duplicate_chip_select",
        "DDR Homework 3: Second rank never answers, first rank is fine",
        "This board has two DDR chips populated as two separate ranks. The first rank's memory test runs "
            "clean. The second rank's test never gets a single word back, as if it isn't populated at all "
            "- but it physically is, and both chips show the same CK/command activity on a scope. What's "
            "wrong?",
        "Check each populated chip's own Chip number setting - do the two chips actually disagree with "
            "each other, the way two separate ranks should? (The DDR Eye Scope sees a clean bus here too, "
            "once it builds a picture - see the note on how slowly that happens in DDR Homework 1.)",
        "Both chips are strapped Chip number = 0 - a board assembly mistake, not a missing or dead part: "
            "the second chip's CS strap was never actually changed from the first rank's wiring. When the "
            "controller addresses \"chip 1\" to test the second rank, nothing on the board answers CS1, "
            "because nothing is actually wired to it - both chips are listening on CS0 instead. This is a "
            "different root cause from the single-chip \"wrong target\" homework (DDR Homework 1) even "
            "though the symptom looks identical: a populated-but-silent rank means check chip-select "
            "strapping on every populated chip, not just assume the one you can't reach is faulty or "
            "unpowered. (Same timeout-dominated eye-scope behaviour as DDR Homework 1, for the identical "
            "reason: every read to the untargeted CS1 line times out before the scope ever sees another "
            "write.)",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 1.0f);   // testing the second rank...
            setParam (*e.getPlugin (2), "chipnumber", 0.0f);   // first chip: correctly chip 0
            setParam (*e.getPlugin (3), "chipnumber", 0.0f);   // ...but the second chip was ALSO left at 0
        }, -1, -1, -1, 0, 1 });

    // --- 20 (DDR): ZQ calibration against the external 240 ohm reference never completed cleanly - the
    // target ODT setting is fine, but the chip's *actual* termination lands well off it. Validation-sequence
    // step: "ZQ calibration trims the DRAM's output driver and ODT resistors against the external 240 ohm
    // reference."
    set.push_back ({
        "HOMEWORK_20_ddr_zq_cal_failed",
        "DDR Homework 4: ZQ calibration never finished cleanly",
        "This chip's on-die termination is programmed to a perfectly sensible 60 ohm - matched to the "
            "board's trace - and Vref was trained carefully, close to the real centre for this channel. The "
            "memory test still fails almost every word. Nothing about the *settings* looks wrong by "
            "themselves - what haven't you checked yet?",
        "A programmed ODT value isn't the same thing as the chip's *actual* termination. Check whether ZQ "
            "calibration (against the external 240 ohm reference) actually landed on that target, or drifted "
            "well away from it.",
        "The ODT target (60 ohm) and the trained Vref are both reasonable - the problem is that ZQ "
            "calibration itself never landed cleanly: the chip's real, effective termination ends up "
            "significantly off from the 60 ohm the mode register asked for. A mismatched termination reflects "
            "a scaled copy of the signal back into the same bit roughly a unit interval later - a real, "
            "physical echo, not a wrong number on a register readout - and against a Vref trained close to "
            "the edge of its margin (which looked fine for a clean channel), that echo is enough to flip bits "
            "almost every time data toggles. This is exactly why real DDR controllers re-run ZQ calibration "
            "periodically (including a poll after reset) rather than trusting it once: a *programmed* "
            "termination value tells you nothing about whether the calibration that was supposed to realise "
            "it in silicon actually worked. The fix is to re-run ZQ cal (here, bring the ZQ cal error back "
            "toward 0%), not to change the ODT setting that was already correct. (A DDR Eye Scope on the "
            "bus here would show a perfectly open eye the whole time - the reflection this homework models "
            "happens entirely inside the chip's own capture logic, not as a channel-level distortion a scope "
            "upstream of it could ever see.)",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.82f);      // trained close to the edge, assuming a clean ZQ cal
            setParam (*chip, "odt", 60.0f);       // the programmed target is fine...
            setParam (*chip, "zqerror", 48.0f);   // ...but the calibration against it badly missed
        }, -1, -1, -1, 0, 1 });

    // --- 21 (DDR): same reflection mechanism as #20, different real cause - the ODT target itself was
    // programmed wrong (effectively disabled), with a perfectly clean ZQ calibration. Contrast deliberately
    // mirrors the existing #17/#19 "same symptom, different cause" pairing.
    set.push_back ({
        "HOMEWORK_21_ddr_odt_misconfigured",
        "DDR Homework 5: ZQ cal is clean, and it still fails the same way",
        "This board's memory test fails almost every word, the same way DDR Homework 4's did - but this "
            "time, ZQ calibration reports 0% error: whatever the mode register asks for, the chip delivers "
            "exactly. If the calibration itself isn't the problem here, what is?",
        "Check what the ODT mode register is actually asking the chip to be calibrated *to* - not whether "
            "the calibration hit that target, but whether the target itself makes sense for this bus.",
        "ZQ calibration is genuinely clean here - the chip really does deliver whatever termination its mode "
            "register asks for. The register itself is just programmed wrong: it asks for on-die termination "
            "effectively off (a very high resistance), far from the bus's matched impedance, rather than a "
            "reasonable on value. The result is the identical reflection mechanism as DDR Homework 4 - a "
            "mismatched termination echoing a scaled copy of the signal back into later bits - just reached "
            "by a different real mistake: a bad ODT *setting*, not a bad *calibration* of an otherwise-correct "
            "setting. This is a genuinely useful distinction during bring-up: the same measured symptom (ZQ/ "
            "ODT-related data corruption) can come from either a calibration failure or a flat-out wrong mode "
            "register value, and you tell them apart by checking the calibration error itself before assuming "
            "the programmed target is fine. The fix here is to reprogram ODT to a sensible value, not to "
            "re-run a calibration that was never the problem. (Same as DDR Homework 4: a DDR Eye Scope on "
            "the bus shows a perfectly open eye - this reflection lives entirely inside the chip's own "
            "capture logic, invisible to anything watching the bus from outside.)",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.5f);        // an ordinary, well-centred Vref - not the issue this time
            setParam (*chip, "odt", 240.0f);       // the mode register itself asks for ODT effectively off...
            setParam (*chip, "zqerror", 0.0f);     // ...and calibration against that (wrong) target is clean
        }, -1, -1, -1, 0, 1 });

    // --- 22 (DDR): CS/CA training never ran - this chip's own chip-select sampling lags the real signal by
    // most of a unit interval. Validation-sequence step: "CA/CS training... CS timing, then CA sampling
    // against the Vref(CA) point."
    set.push_back ({
        "HOMEWORK_22_ddr_cs_training_missing",
        "DDR Homework 6: The controller thinks every read just times out",
        "CK, the command bus, and chip select are all clearly toggling - a scope on the bus shows completely "
            "normal-looking activity - but the built-in memory test reports almost nothing but timeouts: it "
            "waits its full timeout budget on nearly every read, then gives up and counts it as a miss. "
            "Writes look like they're happening. What would make a chip see activity but still miss its own "
            "selection?",
        "CS/CA training aligns *when* a chip samples chip-select against the rest of the command bus - it "
            "isn't automatic. Check this chip's own CS timing-error setting: is its chip-select sampling "
            "actually aligned with the real signal, or still lagging from before training ran?",
        "CS/CA training was simply never run on this chip: its own chip-select sampling lags the real signal "
            "by most of a unit interval. A read command is only asserted for a single unit interval before the "
            "controller moves on - with CS sampling arriving this late, the chip's own \"am I selected\" check "
            "doesn't catch up until that window has already closed, so it misses the read command outright, "
            "the controller polls for a reply that will never come, and eventually times out and records a "
            "mismatch. Note that writes do go through here: the write burst holds CS asserted continuously "
            "across many consecutive unit intervals, so a timing lag that would miss a single brief pulse "
            "doesn't matter once CS has been high for a while - which is exactly why \"writes look like "
            "they're happening\" was a real clue, not a red herring. Real DDR5/LPDDR5 CA/CS training exists "
            "precisely to find and correct this kind of per-chip sampling skew before relying on short, "
            "individually-addressed commands like reads. (Checking a DDR Eye Scope here is a genuinely "
            "useful first step, just not the one that solves it: the write traffic that does get through is "
            "perfectly clean, since CS timing has nothing to do with the data signal's own quality - but "
            "notice the scope struggles to build any picture at all, the same timeout-dominated symptom as "
            "DDR Homeworks 1 and 3, because every read here times out before answering. That slow, "
            "struggling eye is itself part of the signature: a clean channel that can barely keep the scope "
            "fed points at a timing/protocol problem, not a signal-integrity one.)",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            setParam (*e.getPlugin (0), "burstwords", 16.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "cstiming", 0.97f);   // CS/CA training never ran on this chip
        }, -1, -1, -1, 0, 1 });

    // --- 23 (DDR): write levelling never ran - one shared DQS-to-byte-lane skew, all eight bits equally
    // wrong. Validation-sequence step: "Write levelling aligns DQS to CK at each DRAM... The DRAM samples CK
    // on the DQS edge and sends the result back on DQ."
    set.push_back ({
        "HOMEWORK_23_ddr_write_leveling_missing",
        "DDR Homework 7: Every single bit of every word is wrong",
        "This chip's memory test fails almost every word it checks - and when a word is wrong, it's not just "
            "a bit or two off, the whole byte usually comes back looking unrelated to what was written. "
            "There's no obvious per-pin pattern to it (it's not consistently one DQ line). What kind of "
            "mistake corrupts an entire byte lane uniformly like that?",
        "Check this chip's per-bit DQ deskew settings in its editor (the small indicator under each DQ "
            "column, or the parameters themselves) - are all eight bits skewed by about the same amount, or "
            "does one stand out from the rest?",
        "All eight DQ bits are skewed by the same amount - a single, shared DQS-to-byte-lane timing error, "
            "which is exactly what write levelling exists to correct and clearly never ran (or was undone by "
            "a board/trace change since). Because every bit lags identically, the chip captures essentially "
            "the *previous* word's data for the whole byte rather than the current one - a uniform, whole-byte "
            "corruption, not an isolated-pin problem. This is the real, textbook write-levelling symptom: DQS "
            "has to be aligned to CK at each individual DRAM to compensate for fly-by trace-length differences "
            "between ranks, and until that alignment is trained, every bit in the lane is wrong together, not "
            "just one troublesome pin. Contrast this with DDR Homework 8 (per-bit write-DQ deskew), where only "
            "a couple of individual pins are off and the rest of the byte lane is fine - a different real "
            "training step with a visibly different symptom. (A DDR Eye Scope on the bus shows a perfectly "
            "open eye regardless - write levelling is about *when* the chip samples an otherwise-clean "
            "signal, not whether the signal itself is clean.)",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            for (int b = 0; b < 8; ++b)
                setParam (*chip, "dqskew" + String (b), 0.4f);   // write levelling: never ran, whole byte lane equally skewed
        }, -1, -1, -1, 0, 1 });

    // --- 24 (DDR): write DQ per-bit deskew left incomplete on just two pins - a sparser, pattern-dependent
    // symptom, deliberately contrasted with #23's uniform, near-total failure. Validation-sequence step:
    // "Write DQ training, per-bit deskew plus centering DQ against DQS at the DRAM."
    set.push_back ({
        "HOMEWORK_24_ddr_per_bit_deskew_incomplete",
        "DDR Homework 8: It only fails sometimes, and only on certain data",
        "This chip's memory test mostly passes - most words check out clean - but a noticeable minority "
            "consistently come back wrong, and it isn't random: the same handful of addresses keep failing "
            "every run, while most others are completely solid. Write levelling looks fine (CK and DQS are "
            "well aligned as a byte lane). What training step is this, if it's not write levelling?",
        "Open the chip's editor and look at the per-bit DQ deskew indicators under each DQ column - is it "
            "every bit, evenly, or just one or two that stand out from an otherwise well-trained byte lane? "
            "A DDR Eye Scope confirms the bus itself is healthy (any untouched pin, like DQ0, locks to a "
            "clean, fully open eye) - whatever's wrong with the failing pins isn't visible as a bad signal.",
        "Write levelling (the whole-byte-lane DQS-to-CK alignment) is genuinely fine here - the symptom is "
            "narrower than that. Two individual DQ pins were left with real per-bit deskew error while the "
            "rest of the byte lane is correctly centred - the training step real DDR calls per-bit write-DQ "
            "deskew, centring each individual pin's data against DQS rather than the byte lane as a whole. "
            "Because only two of eight bits are actually wrong, a captured byte is only corrupted on the "
            "specific words where at least one of those two bits happens to differ from the previous word's "
            "same bit - which, for ordinary address-counting data, only happens on a minority of addresses. "
            "That's exactly why this looks \"intermittent\" and \"only on certain data\" rather than totally "
            "broken: it's a real, sparser failure signature, and it's the reason real DDR training per-bit "
            "deskews every single DQ pin individually rather than assuming one shared, byte-wide alignment "
            "(write levelling) is enough. (The per-bit skew lives entirely in how the chip times its own "
            "sample of DQ3/DQ6, not in anything an external scope on the bus could flag as bad - which is "
            "exactly why probing DQ0 here, rather than one of the actual failing pins, is the faster way to "
            "confirm the bus itself is clean. DQ3 and DQ6 toggle too rarely under this BIST pattern for a "
            "scope aimed right at them to build a picture quickly anyway.)",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "dqskew3", 0.4f);   // only two of eight pins left untrained...
            setParam (*chip, "dqskew6", 0.4f);   // ...the rest of the byte lane is correctly deskewed
        }, -1, -1, -1, 0, 1 });

    // --- 25 (DDR): the same ODT-mismatch reflection as #21, but this time the intended fix is the chip's
    // own DFE, not the ODT setting - demonstrating DQ receiver equalization as a real alternative mitigation,
    // not just "reduce the reflection at its source". Validation-sequence step: "DFE/equalization. DDR5
    // DRAMs have a 4-tap DFE on the DQ receivers."
    set.push_back ({
        "HOMEWORK_25_ddr_dfe_disabled",
        "DDR Homework 9: There's a DFE right there, and it's off",
        "This chip has the same ODT-mismatch reflection problem as DDR Homework 5 - the mode register asks "
            "for termination effectively off - and the test fails almost every word the same way. This time, "
            "assume the ODT setting itself can't be changed (it's fixed by another requirement on this "
            "board). Is there anything else on this chip that could still clean this up?",
        "Check the chip's own DFE control. DDR5 DRAMs carry a real decision-feedback equalizer on their DQ "
            "receivers specifically to cancel known inter-symbol interference like this - is it actually "
            "turned on?",
        "The chip's single-tap DFE is sitting disabled. A DFE cancels inter-symbol interference using the "
            "receiver's own prior decision - here, specifically the kind of reflection-driven echo an ODT "
            "mismatch creates - and with it enabled, the same ODT problem that broke DDR Homework 5 is fully "
            "absorbed: the test passes cleanly even though nothing about the termination itself changed. This "
            "mirrors a real DDR5 capability: DQ receivers on the DRAM carry their own (4-tap, in a real part) "
            "DFE for exactly this reason, so a signal-integrity problem that can't be fixed at its physical "
            "source (board respin required, a fixed ODT requirement elsewhere, etc.) can sometimes still be "
            "absorbed downstream by turning on equalization that was simply left off. It's not a substitute "
            "for fixing a real root cause when you can (see DDR Homework 5) - but it's a genuine, real option "
            "when you can't. (A DDR Eye Scope on the bus shows the same wide-open eye whether the DFE is on "
            "or off - the DFE, like the ODT problem it's cancelling, acts entirely inside the chip's own "
            "receive path, not as something that changes what the bus itself looks like.)",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.5f);
            setParam (*chip, "odt", 240.0f);     // same fixed ODT problem as DDR Homework 5...
            setParam (*chip, "zqerror", 0.0f);
            setParam (*chip, "dfe", 0.0f);       // ...but this chip's own DFE is simply left off
        }, -1, -1, -1, 0, 1 });

    // =====================================================================================
    // PLL homeworks: a third, independent domain (PllCore, lab.pllcore) alongside SerDes and DDR -
    // real clock-synthesis-PLL bring-up/debug scenarios, not symbol timing recovery. Dispatch is via
    // pllCoreSlot (see the Homework struct and the measurement pass above).
    // =====================================================================================

    // --- 26: a wrong feedback divider locks confidently - to the wrong frequency. ---
    set.push_back ({
        "HOMEWORK_26_pll_wrong_feedback_divider",
        "Homework 26: It's locked. It's also wrong.",
        "A frequency synthesizer is supposed to multiply a 25 MHz reference up to 1000 MHz. Lab status "
            "shows LOCKED, steady, no alarms. A technician downstream reports every measurement off this "
            "clock is subtly wrong. Before assuming their equipment is miscalibrated, check what this "
            "synthesizer is actually putting out.",
        "Read the VCO output frequency directly, not just the lock indicator. Does it match 25 MHz x the "
            "programmed feedback divider?",
        "The feedback divider is programmed for N=41, not the intended N=40. The loop does exactly what "
            "it's designed to do - it locks the VCO to N times the reference - it just locks to the wrong "
            "N. A PLL's lock detector only confirms the loop found a stable, phase-aligned state; it says "
            "nothing about whether that state is the one you actually wanted. This is a real, easy-to-make "
            "bring-up mistake (a register field one off, a strapping error) that produces zero alarms and a "
            "perfectly confident LOCKED status - always cross-check the actual output frequency against "
            "reference x N, not just the lock flag, before trusting a synthesizer's output.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 41.0f);    // wrong - the system was designed for 40
            setParam (*pll, "vcofree", 950.0f);
            setParam (*pll, "loopkp", 290.0f);
            setParam (*pll, "loopki", 1100.0f);
        }, -1, -1, -1, -1, -1, 0 });

    // --- 27: loop gains far too weak to pull in a realistic cold-start error in any reasonable time. ---
    set.push_back ({
        "HOMEWORK_27_pll_loop_bandwidth_too_narrow",
        "Homework 27: It never quite gets there",
        "This PLL's feedback divider and reference are both correct, but it's been sitting on the bench "
            "for a very long time now and still reports not locked - the VCO output frequency is close to "
            "the target, but visibly, persistently short of it. Narrow down what's actually wrong before "
            "assuming the VCO itself is faulty.",
        "Check the loop filter's gain settings against how big a cold-start frequency error they actually "
            "need to pull in. Is the loop bandwidth anywhere close to being fast enough?",
        "Both loop filter gains (proportional and integral) are set roughly 50x weaker than this design's "
            "normal values. The feedback divider and reference are correct, and the loop IS pulling the VCO "
            "in the right direction - it's just doing it so slowly that it hasn't finished within any "
            "reasonable bring-up window. This is a genuine \"loop bandwidth too narrow\" fault, not a "
            "hand-wave: a real PLL's loop gains set how fast it can acquire lock after a frequency step or "
            "cold start, and gains dialled in too conservatively (perhaps copied from a much higher-N design "
            "without rescaling) leave a synthesizer that technically works but never actually finishes "
            "locking on any timescale a bring-up lab can wait for.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 40.0f);
            setParam (*pll, "vcofree", 950.0f);     // a real, modest 50 Hz cold-start gap to pull in
            setParam (*pll, "loopkp", 5.0f);        // both ~50x weaker than the normal 290 / 1100
            setParam (*pll, "loopki", 5.0f);
        }, -1, -1, -1, -1, -1, 0 });

    // --- 28: loop filter badly underdamped - rings/overshoots hard before (eventually) settling. ---
    set.push_back ({
        "HOMEWORK_28_pll_underdamped_ringing",
        "Homework 28: It locks, but it takes the scenic route",
        "This PLL does eventually report LOCKED, but a technician watching the output frequency during "
            "power-up says it visibly overshoots and rings back and forth several times before settling, "
            "instead of smoothly approaching the target. For a clock that's supposed to be ready quickly "
            "and cleanly after power-up, is this normal?",
        "Compare the loop filter's proportional gain against its integral gain. A PLL's damping comes "
            "from the balance between those two, not either one alone - is this one balanced, or is one "
            "path dominating?",
        "The loop filter's proportional gain is set roughly 6x lower than this design's normal value while "
            "the integral gain is unchanged - a classic underdamped loop filter. The integral path still has "
            "plenty of authority to eventually pull the frequency all the way in, but with so little "
            "proportional damping to restrain it, the loop overshoots the target and rings back and forth "
            "before settling, instead of approaching it smoothly. This is exactly the lesson a real loop "
            "filter's R1 (proportional) / C1 (integral) balance teaches: raising the proportional gain back "
            "toward its normal value damps the ringing out without changing how far the loop can ultimately "
            "pull in.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 40.0f);
            setParam (*pll, "vcofree", 950.0f);
            setParam (*pll, "loopkp", 50.0f);       // normal is 290 - badly underdamped against ki below
            setParam (*pll, "loopki", 1100.0f);
        }, -1, -1, -1, -1, -1, 0 });

    // --- 29: a methodology lesson - a noisy reference can trip a simple lock detector even when the ---
    // --- loop is genuinely tracking the right frequency on average. ---
    set.push_back ({
        "HOMEWORK_29_pll_jitter_trips_lock_detector",
        "Homework 29: The lock light won't stay on, but the frequency is right",
        "This PLL's reference, divider and loop gains are all the normal, correct values for this design - "
            "but the lock indicator flickers on and off instead of staying solid, which someone has flagged "
            "as a loop stability problem. Before touching the loop filter, actually check what the output "
            "frequency is doing.",
        "Average the VCO output frequency over a few seconds rather than reading the lock flag alone. Is "
            "the average close to reference x N, even while the lock indicator flickers?",
        "The reference input itself carries a large amount of injected timing jitter - large enough that "
            "individual reference edges arrive measurably early or late, which is enough to trip a simple "
            "instantaneous-phase-error lock detector on its own, even though the loop filter is correctly "
            "averaging the jitter out and the VCO's output frequency lands close to the intended reference "
            "x N. This is a real measurement-methodology lesson, the PLL-domain counterpart to this project's "
            "earlier \"check the absolute swing, not just the eye height percentage\" lesson: a lock detector "
            "built on an instantaneous threshold can read as unlocked even when the loop is genuinely doing "
            "its job, if the thing it's watching (phase error, here) is itself noisy for a reason that has "
            "nothing to do with loop stability. The real problem to chase, if there is one, is upstream of "
            "the PLL - whatever's injecting that much jitter onto the reference - not the loop filter.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 40.0f);
            setParam (*pll, "vcofree", 1000.0f);
            setParam (*pll, "loopkp", 290.0f);
            setParam (*pll, "loopki", 1100.0f);
            setParam (*pll, "refjitter", 0.05f);    // large enough to trip the instantaneous lock threshold
        }, -1, -1, -1, -1, -1, 0 });

    // =====================================================================================
    // Bring-up & validation plan: standing a SerDes link up one real bring-up step at a time
    // (TX alone -> channel -> TX EQ -> RX clocking -> RX linear EQ -> RX decision-feedback EQ
    // -> closed adaptive loop -> margin stress -> final sign-off), instead of a flat set of
    // independent scenarios. Every step still has something real to find - these aren't just
    // "turn this on" checkboxes - but the fault is the one that actually belongs at that point
    // in a real bring-up sequence, and each step's correct settings are exactly what the next
    // step's scenario is built on. The in-app Bring-Up window loads these in filename order.
    // =====================================================================================

    // --- Bring-up 1: is the transmitter itself actually launching a full-swing signal? ---
    bringups.push_back ({
        "BRINGUP_01_tx_output_check",
        "Bring-Up 1: Does the driver actually work?",
        "Before a channel, a receiver, or any equalizer is anywhere in the picture, confirm the "
            "transmitter alone launches a full, clean signal. Just a generator and an Eye Scope - look at "
            "what's actually coming out of the driver.",
        "Don't stop at the eye's \"height\" percentage - it normalizes to whatever swing is actually "
            "there, even a small one. Check the swing figure right next to it.",
        "The transmitter's output amplitude is set to about 0.04, roughly a sixth of the usual 0.25 - the "
            "driver is alive and the Eye Scope's height reads a reassuring 100% (it's normalized to "
            "whatever swing is actually present), but the swing readout right next to it shows only about "
            "0.16 FS where a healthy transmitter launches close to 1.0 FS. This is the single most "
            "important bring-up habit for reading any eye diagram, real or simulated: a \"wide open\" "
            "percentage only means the signal is clean relative to itself - always also check the absolute "
            "swing before concluding the driver is actually putting out a healthy signal. The fix is "
            "turning the TX output level/drive strength back up to spec.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (0), "amplitude", 0.04f);
            setParam (*e.getPlugin (1), "vscale", 1.5f);
        }, 1 });

    // --- Bring-up 2: characterize the channel for real before anyone tunes an equalizer against it. ---
    bringups.push_back ({
        "BRINGUP_02_channel_loss_characterization",
        "Bring-Up 2: Measure the channel, don't trust its datasheet",
        "The transmitter is confirmed healthy. Before anyone starts tuning an equalizer, characterize the "
            "channel itself: add a Lossy Channel between the generator and the Eye Scope and see how "
            "closed the eye actually gets. The cable assembly's datasheet lists about 11 dB of insertion "
            "loss at Nyquist for a run this length - is that actually what you're measuring?",
        "Check the Lossy Channel editor's own \"loss\" setting against the 11 dB the datasheet claims. "
            "Does the as-built part match its own paperwork?",
        "The channel is actually configured for 16 dB of loss at Nyquist, not the 11 dB its datasheet "
            "claims for this length - a real \"the as-built part doesn't match its own paperwork\" finding "
            "(a different cable gauge, an uncounted connector, extra board loss nobody budgeted for). With "
            "zero equalization anywhere in the chain the eye is almost completely shut - around 2% "
            "height, under a fifth of a UI wide - exactly what 16 dB alone does at this rate. The number "
            "that matters for every later step is the one you just measured, 16 dB, not the 11 dB on the "
            "datasheet: sizing the TX FFE for the wrong number is exactly the mistake Bring-Up 3 walks "
            "into.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (LossyChannel::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (0), "amplitude", 0.25f);
            setParam (*e.getPlugin (1), "loss", 16.0f);
            setParam (*e.getPlugin (2), "vscale", 0.6f);
        }, 2 });

    // --- Bring-up 3: TX FFE hand-sized for the datasheet's loss number, not the real measured one. ---
    bringups.push_back ({
        "BRINGUP_03_tx_ffe_wrong_budget",
        "Bring-Up 3: A real equalizer, budgeted for the wrong channel",
        "Pre-emphasize the transmitter for the channel - taps are hand-set (TX mode on, auto-adapt off, "
            "the way you'd dial in a first guess before ever closing a training loop) using the 11 dB the "
            "datasheet claims, the way you would if you trusted the paperwork instead of Bring-Up 2's "
            "measurement.",
        "The FFE's stem plot will show real, sensible pre-emphasis - it's not flat, and it's not "
            "backwards. Compare its boost at Nyquist (under the plot) to the channel's actual measured "
            "loss, not to what the datasheet said the channel should be.",
        "These taps are exactly the ones that converge cleanly for an 11 dB channel - a real, "
            "correctly-shaped equalizer (right direction, sensible magnitudes), just budgeted for the "
            "wrong number. Against the real 16 dB channel from Bring-Up 2, the eye only reaches about 39% "
            "height - clearly short of the roughly 55% you get from the same FFE re-budgeted for the real "
            "measured loss (a bit more post-cursor tap, a bit less main tap, to spend more of the budget "
            "cancelling the extra 5 dB). This is exactly the bring-up trap Bring-Up 2 existed to prevent: "
            "an equalizer that looks completely reasonable - correct shape, correct direction, real "
            "non-zero taps - but calibrated off a datasheet number instead of what you actually measured.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            auto* ffe = e.getPlugin (1);
            setParam (*ffe, "txmode", 1.0f); setParam (*ffe, "adapt", 0.0f);
            setParam (*ffe, DiffFfe::getTapParamId (0), -0.010f);   // pre3
            setParam (*ffe, DiffFfe::getTapParamId (1), 0.010f);    // pre2
            setParam (*ffe, DiffFfe::getTapParamId (2), -0.048f);   // pre1
            setParam (*ffe, DiffFfe::getTapParamId (3), 0.643f);    // main - sized for 11 dB, not the real 16 dB
            setParam (*ffe, DiffFfe::getTapParamId (4), -0.257f);   // post1
            setParam (*ffe, DiffFfe::getTapParamId (5), 0.029f);    // post2
            setParam (*ffe, DiffFfe::getTapParamId (6), -0.010f);   // post3
            setParam (*e.getPlugin (2), "loss", 16.0f);
            setParam (*e.getPlugin (3), "vscale", 0.6f);
        }, 3 });

    // --- Bring-up 4: receiver clocking - a real, unsynchronized reference-clock ppm offset the CDR's
    // frequency loop is set too weak to ever track out. DFE deliberately left off to isolate the CDR. ---
    bringups.push_back ({
        "BRINGUP_04_cdr_frequency_tracking_too_slow",
        "Bring-Up 4: The clock recovery never actually catches up",
        "FFE and channel are sorted. Bring up the receiver's clock recovery next, before touching "
            "anything else in the receiver (DFE is deliberately left off) - a stable recovered clock has "
            "to come first. These two boards each run their own independent reference oscillator rather "
            "than sharing one, so expect a real, nonzero ppm offset between TX and RX for the CDR to track "
            "out.",
        "Check the receiver editor's reported CDR frequency offset (ppm) against where it needs to end "
            "up. Is it actually converging toward the real offset, or stuck nowhere near it?",
        "The CDR's frequency-tracking (integral) gain is set to 0.5 uUI - fine if these boards shared one "
            "reference clock and only had a few ppm of local drift to chase, but these boards each have an "
            "independent oscillator and the real offset here is 3000 ppm. With the integral gain this low "
            "the loop never gets anywhere near the real offset (it settles hundreds of ppm off, not the "
            "needed -3000) - roughly a third of compared bits come back wrong, a real link failure even on "
            "runs where the CDR's own \"locked\" flag still happens to read true (the frequency error, not "
            "the lock flag, is the number to trust here). Turning the CDR's integral step back up into its normal range "
            "(tens of uUI) lets it close that gap and lock cleanly, with nothing about the channel or TX "
            "changed. The real-world version of this mistake: sizing a CDR's frequency-loop bandwidth for "
            "the wrong clocking architecture - synchronous (one shared reference) versus asynchronous "
            "(independent oscillators, a real ppm offset that has to be tracked, not just a little local "
            "jitter).",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 8.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "ctleboost", 4.0f); setParam (*rx, "dfeadapt", 0.0f);
            setParam (*rx, "clockppm", 3000.0f); setParam (*rx, "cdrki", 0.5f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- Bring-up 5: receiver linear EQ - boost dB is generous, but the zero frequency is set past
    // Nyquist, so almost none of the dialed-in boost actually lands where it's needed. DFE still off. ---
    bringups.push_back ({
        "BRINGUP_05_ctle_zero_past_nyquist",
        "Bring-Up 5: 10 dB of boost that never arrives",
        "CDR is locked. Bring up the receiver's linear equalizer (CTLE) next, before the DFE (still off) "
            "- get the analog boost right first. The boost knob is turned up to 10 dB, clearly more than a "
            "flat response, so this should help against the channel's loss.",
        "The CTLE has three real knobs, not one: the boost in dB, and where that boost actually starts "
            "(the \"zero\" frequency) and stops (the high pole). Check the boost the receiver editor "
            "reports AT Nyquist specifically, not just the headline boost setting.",
        "10 dB of boost is dialed in, but the CTLE's zero (where the peaking starts) is set to 8 GHz - "
            "above this link's 5 GHz Nyquist frequency entirely. A CTLE only delivers its full boost once "
            "you're past the zero; at Nyquist, still below the zero, almost none of that 10 dB has actually "
            "arrived (about 0.3 dB, read straight off the receiver), so the channel's real loss goes "
            "essentially uncorrected and a couple of percent of bits come back wrong even though the CDR "
            "is locked and the boost knob isn't remotely conservative. Moving the zero down to around "
            "2 GHz - comfortably below Nyquist, where a CTLE sized for this channel would actually put it "
            "- lets the same 10 dB actually land where it's needed instead. A CTLE's shape (where the "
            "zero and pole sit), not just its boost magnitude, has to be sized for the link's real baud "
            "rate.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 0.0f);
            setParam (*e.getPlugin (2), "loss", 14.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "clockppm", 0.0f); setParam (*rx, "dfeadapt", 0.0f);
            setParam (*rx, "ctleboost", 10.0f); setParam (*rx, "ctlezero", 8.0f); setParam (*rx, "ctlepole2", 16.0f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- Bring-up 6: receiver DFE brought up last (it needs a locked clock and a settled CTLE first) -
    // adaptation step left at its coarse, fast-sweep setting instead of tightened for steady-state. ---
    bringups.push_back ({
        "BRINGUP_06_dfe_step_too_coarse",
        "Bring-Up 6: The last tap, still on its coarse setting",
        "CDR locked, CTLE boost set. Bring up the DFE last - it needs correct decisions from an "
            "already-locked, already-equalized receiver, which is exactly why it comes after CDR and CTLE "
            "in this plan, not before. Auto-adapt is on.",
        "A short BER check here might come back completely clean either way. Look at the eye's height and "
            "width readouts instead of just the error count - do they look as good as they could?",
        "The DFE's adaptation step is left at 0.01, its coarsest setting - good for grabbing a rough tap "
            "value fast during an initial sweep, too coarse for settled, steady-state operation: the taps "
            "keep hunting back and forth around the right value instead of settling onto it. A short BER "
            "run can come back with zero errors either way (there usually isn't enough margin lost to flip "
            "a bit most of the time), which is exactly why this step matters: eye height comes in around "
            "55% here, versus about 66% once the step is tightened to a normal 0.001 (see Bring-Up 9's "
            "otherwise-identical configuration) - real margin given "
            "up to a setting that's easy to forget to tighten after bring-up's first, coarse pass. This is "
            "the same lesson Bring-Up 9 ends on: a clean BER count over a short run doesn't mean there's "
            "nothing left to improve, and eye margin is a more sensitive instrument to check than a short "
            "error count alone.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 10.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "clockppm", 0.0f); setParam (*rx, "ctleboost", 6.0f);
            setParam (*rx, "dfeadapt", 1.0f); setParam (*rx, "dfestep", 0.01f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- Bring-up 7: close the full adaptive loop (FFE + CTLE + DFE all auto-adapting) - but the Eye
    // Scope's own "Feed FFE trainer" switch, deliberately turned off back in Bring-Ups 4-6 to isolate
    // the CTLE/DFE from the FFE's training loop while bringing each of THOSE up on its own, never got
    // turned back on now that the full loop is supposed to close. ---
    bringups.push_back ({
        "BRINGUP_07_trainer_still_switched_off",
        "Bring-Up 7: The trainer that's still switched off",
        "Every piece of the receiver has now been brought up and checked individually - CDR, CTLE, DFE. "
            "Close the loop: turn on auto-adaptation for all three (TX FFE, CTLE, DFE) together. The "
            "FFE's \"Auto-adapt\" box is checked, the same as CTLE's and DFE's.",
        "A checked box doesn't prove a training loop is actually receiving feedback. Open the FFE's own "
            "editor and read its status line, not just whether the checkbox is ticked - is anything "
            "actually happening?",
        "All three auto-adapt boxes are genuinely checked, but the Eye Scope's own \"Feed FFE trainer\" "
            "switch - turned off deliberately back in Bring-Ups 4-6 to isolate the CTLE and DFE from the "
            "FFE's training loop while bringing each of THOSE up on its own - never got turned back on now "
            "that the full loop is supposed to close. The FFE's trainer sits in \"Waiting for "
            "measurements\" forever (zero adaptation trials ever run; its own editor's status line says "
            "exactly this), even though its checkbox reads identically to the two loops that ARE actually "
            "running. CTLE and DFE still adapt fine on their own (they judge themselves against the "
            "receiver's own slicer error, not this shared probe), so the eye isn't catastrophic - around "
            "71% height - just quietly worse than the roughly 80% you get once the switch is back on and "
            "the FFE actually contributes. This is a real link-training mistake: an \"enabled\" checkbox "
            "doesn't prove the loop it names is receiving feedback - a trainer's own status (not just its "
            "own enable switch) is what to check before trusting that a trained link is actually trained.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 14.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "clockppm", 0.0f);
            setParam (*rx, "ctleadapt", 1.0f); setParam (*rx, "dfeadapt", 1.0f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // --- Bring-up 8: margin stress test - the fully-trained link from Steps 4-7, now with a realistic
    // crosstalk aggressor actually running, not just a quiet bench. ---
    bringups.push_back ({
        "BRINGUP_08_crosstalk_margin_stress",
        "Bring-Up 8: Fine on the bench, not fine next to a neighbour",
        "The link is fully brought up - CDR, CTLE, and DFE all correctly set the way the earlier steps "
            "left them. Before signing off, stress it the way a real validation lab would: with a "
            "neighbouring aggressor lane actually running traffic, not just on a quiet bench. A second "
            "lane couples into this one at a modest level, slightly offset in rate (a realistic "
            "independent-clock aggressor, not a synchronized one).",
        "Nothing about the link itself - FFE, CTLE, DFE - changed from the end of the earlier steps. "
            "Check the Noise Injector's type and level before assuming the receiver regressed.",
        "Nothing about the receiver's own settings changed from where the earlier bring-up steps left "
            "them - a crosstalk aggressor at a coupling level of only 0.1 (modest, not an extreme value) "
            "is enough to collapse the eye height from about 66% down to under 5% - a margin that was "
            "comfortable moments ago is now right at the edge of actually failing, run to run. This is the actual point "
            "of a margin stress test: a link that looks completely finished on a quiet bench can still "
            "have very little real margin, and a crosstalk-free bring-up pass alone doesn't prove the "
            "board will hold up once its neighbours are actually running traffic. (Pushing the coupling "
            "level higher still - 0.2 and up, in this same setup - stops the receiver from syncing to the "
            "data at all, the more obvious, easy-to-catch version of this failure; the quietly-degraded-"
            "but-still-running case shown here is the one that's easy to miss if you only watch the error "
            "counter.)",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (NoiseInjector::id); addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 0.0f);
            setParam (*e.getPlugin (2), "loss", 10.0f);
            auto* noise = e.getPlugin (3);
            setParam (*noise, "type", (float) NoiseInjector::crosstalk); setParam (*noise, "level", 0.1f);
            setParam (*noise, "aggoffset", 3.0f);
            auto* rx = e.getPlugin (4);
            setParam (*rx, "clockppm", 0.0f); setParam (*rx, "ctleboost", 6.0f);
            setParam (*rx, "dfeadapt", 1.0f); setParam (*rx, "dfestep", 0.001f);
            setParam (*e.getPlugin (5), "vscale", 0.6f); setParam (*e.getPlugin (5), "trainer", 0.0f);
        }, 5, 4 });

    // --- Bring-up 9: final sign-off - everything correctly brought up, the actual issue to solve is
    // methodological (how much does a clean short run really prove), not a new technical defect. ---
    bringups.push_back ({
        "BRINGUP_09_statistical_ber_validation",
        "Bring-Up 9: Zero errors isn't the same as validated",
        "Every earlier step is fixed and correctly brought up. Run the final validation the way a real "
            "bring-up lab would, and decide: is this link actually ready to ship?",
        "Zero errors over a short run doesn't, by itself, tell you the BER. What's the actual confidence "
            "bound on the number you just measured, and how does it compare to a real link's typical spec "
            "(often 1e-12 or tighter)?",
        "Everything here is correctly brought up and a short run comes back with zero bit errors out of "
            "roughly 66,000 compared - which sounds perfect, but zero errors only gives you an upper bound "
            "on the true BER, not the true BER itself. By the \"rule of three\" (3 divided by the bit "
            "count), that upper bound is only about 4.5e-5 at 95% confidence here. Real SerDes specs are "
            "routinely 1e-12 or tighter - proving that statistically would take on the order of a trillion "
            "compared bits, not sixty-six thousand. The honest conclusion from this run is \"no errors seen yet, with "
            "a lot of margin left to prove,\" not \"validated to spec.\" Deciding how long a real bring-up "
            "run actually needs to go - and watching eye margin as a leading indicator along the way, the "
            "way Bring-Up 6 did - is the real last step of a validation plan, not just watching the error "
            "counter stay at zero for a while.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (LossyChannel::id);
            addStage (SerdesReceiver::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            setParam (*e.getPlugin (2), "loss", 10.0f);
            auto* rx = e.getPlugin (3);
            setParam (*rx, "clockppm", 0.0f); setParam (*rx, "ctleboost", 6.0f);
            setParam (*rx, "dfeadapt", 1.0f); setParam (*rx, "dfestep", 0.001f);
            setParam (*e.getPlugin (4), "vscale", 0.6f); setParam (*e.getPlugin (4), "trainer", 0.0f);
        }, 4, 3 });

    // =====================================================================================
    // DDR bring-up & validation plan: standing a DRAM byte lane up one real training step at a
    // time (chip-select sanity -> channel characterization -> ZQ cal -> CS/CA training -> write
    // levelling -> per-bit DQ deskew -> Vref training -> margin stress -> final sign-off), the
    // DDR-domain counterpart to the SerDes bring-up plan above and in the same real JEDEC-ish
    // training order the existing flat DDR homeworks (17-25) already individually model. Each
    // step's correct settings are exactly what the next step's scenario is built on, with two
    // deliberate exceptions found only by actually measuring through the real write/reload round
    // trip (the same discipline the SerDes plan's own Bring-Up 7 pivot followed):
    //   - Every DdrController/DdrChip BIST run shows a one-time ~burstwords-sized burst of errors
    //     in the very first fraction of a second after a chain loads, then runs completely clean
    //     for as long as it keeps going (confirmed by sampling the live counters repeatedly, not
    //     just reading a final total) - a cold-start artifact of this project's existing DDR BIST
    //     logic, present on every DDR chain this plan builds, not something this feature
    //     introduced. Bring-Up 2 and 9 call this out explicitly rather than let a nonzero error
    //     count on an otherwise-correct step look like a fault.
    //   - Bring-Up 8/9's "ODT fixed, DFE absorbs it" scenario (mirroring DDR Homework 9's own
    //     claim) measures true at full, rated swing but FALSE at Bring-Up 7's real ~30%-of-ideal
    //     board swing: this chip's single-tap DFE cancels using the *full-scale* prior bit
    //     decision, not the actual swing-scaled echo, so at reduced swing it overcorrects and
    //     makes the error rate worse, not better. Bring-Up 8/9 are therefore chip-level
    //     characterization at full swing, deliberately not chained onto Bring-Up 7's board
    //     channel - see the comment on Bring-Up 8 below.
    // =====================================================================================

    // --- DDR Bring-up 1: does the chip even answer its own chip-select? ---
    ddrBringups.push_back ({
        "DDRBRINGUP_01_chip_select_sanity",
        "DDR Bring-Up 1: Does the chip even answer?",
        "First power-on of a new DDR byte lane: CK is toggling, the controller is issuing commands, but "
            "the built-in memory test reports nothing but timeouts - every single read comes back "
            "unanswered. Before training anything, confirm the most basic thing: is this chip listening "
            "on the chip-select line the controller is actually asserting?",
        "Compare the controller's \"Target chip\" setting against this chip's own \"Chip number\" strap. "
            "Do they actually agree?",
        "The controller is addressing chip 0, but this chip is strapped as chip 1 - so it never asserts "
            "its own CS line while the controller's commands are flying, and every read times out waiting "
            "for an answer that was never going to come. This is the first thing a real bring-up checks, "
            "before any training: does the part even respond to its own address at all. Set the chip's "
            "Chip number to match the controller's Target chip and the BIST starts passing immediately - "
            "there's nothing else wrong with this link yet.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            setParam (*e.getPlugin (2), "chipnumber", 1.0f);   // strapped as chip 1, controller is addressing chip 0
        }, -1, -1, -1, 0, 1 });

    // --- DDR Bring-up 2: measure the real channel before training anything against an assumed swing. ---
    ddrBringups.push_back ({
        "DDRBRINGUP_02_channel_swing_characterization",
        "DDR Bring-Up 2: Measure the channel, don't assume it",
        "Chip-select is fixed. The memory test shows a small burst of errors right when the chain loads, "
            "then runs clean for as long as you watch it. Before training Vref or anything else "
            "amplitude-sensitive later in this plan, characterize what this board's real trace actually "
            "delivers - don't carry over a number from a datasheet or a different board.",
        "Put the DDR Eye Scope on DQS and read its \"Outer swing at sample point\" readout directly, "
            "rather than assuming this board's trace behaves like a short bench fixture. Separately: when "
            "exactly did those first errors happen, and have there been any more since?",
        "This board's real trace attenuates DQ/DQS to about 30% of ideal swing - a perfectly normal amount "
            "of loss for a populated board, not a fault by itself (Vref is still at its centred default, "
            "which stays correct at any swing as long as nothing else is wrong). The handful of errors "
            "right after the chain loads are a separate, one-time thing worth recognizing on sight: this "
            "controller/chip pair's built-in memory test always shows one burst's worth of mismatches in "
            "the first fraction of a second after any chain loads (the BIST needs one full address sweep "
            "to synchronize), then runs completely clean afterward, no matter how good or bad the settings "
            "are otherwise - it shows up again in Bring-Up 9's own final run. Telling a real, persistent "
            "fault apart from this kind of load transient (by checking *when* errors happened, not just "
            "whether the count is zero) is itself a real bring-up skill. The point of this step is purely "
            "measurement: note the real 30%-of-ideal swing this board delivers now, before Vref Bring-Up 7 "
            "later in this plan has to train against it. Skipping this and assuming a bench-fixture-like "
            "swing is exactly how DDR Homework 2 in the flat homework set gets built.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrLossyChannel::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            setParam (*e.getPlugin (1), "swing", 30.0f);   // this board's real trace - noted here, trained against in Bring-Up 7
            setParam (*e.getPlugin (3), "chipnumber", 0.0f);
        }, -1, -1, -1, 0, 2 });

    // --- DDR Bring-up 3: ZQ calibration never landed cleanly. ---
    ddrBringups.push_back ({
        "DDRBRINGUP_03_zq_calibration_drifted",
        "DDR Bring-Up 3: Run ZQ calibration - and check that it actually worked",
        "Back on the bench (full swing, no board trace yet) with CS fixed: Vref has been trained close "
            "to the edge of its margin, assuming a clean ZQ calibration. The memory test still fails a "
            "large fraction of words. The programmed ODT target (60 ohm) looks completely reasonable. "
            "What haven't you actually confirmed yet?",
        "A programmed ODT target isn't the same thing as the chip's calibrated, *actual* termination. "
            "Check the ZQ cal error itself, not just the register value it was calibrating toward.",
        "ZQ calibration against the external 240 ohm reference never landed cleanly here: the chip's real "
            "termination ends up significantly off from the 60 ohm the mode register asked for. A "
            "mismatched termination reflects a scaled copy of the signal back into the same bit roughly a "
            "unit interval later - and because Vref is trained close to the edge of its margin (reasonable "
            "if calibration is trusted to be clean), that echo is enough to flip a real fraction of bits. "
            "Re-run ZQ cal (bring the cal error back toward 0%) rather than touching the ODT target that "
            "was already correct - the same real distinction DDR Homework 4 makes in the flat set.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.82f);     // trained close to the edge, assuming a clean ZQ cal
            setParam (*chip, "odt", 60.0f);      // the programmed target is fine...
            setParam (*chip, "zqerror", 48.0f);  // ...but the calibration against it badly missed
        }, -1, -1, -1, 0, 1 });

    // --- DDR Bring-up 4: CS/CA training never ran. ---
    ddrBringups.push_back ({
        "DDRBRINGUP_04_cs_ca_training_missing",
        "DDR Bring-Up 4: Train CS/CA timing next",
        "ZQ cal is clean now. The memory test still fails almost every read with a timeout, even though "
            "CK, the command bus, and chip-select all look completely normal on a scope. Writes seem to "
            "go through. What training step runs right after ZQ cal, and is it done yet?",
        "CS/CA training aligns *when* this chip samples chip-select against the rest of the command bus "
            "- it isn't automatic after reset. Check this chip's own CS timing-error setting.",
        "CS/CA training was simply never run: this chip's own chip-select sampling lags the real signal "
            "by most of a unit interval. A read is only asserted for a single unit interval before the "
            "controller moves on, so by the time this chip's own \"am I selected\" check catches up, the "
            "window has already closed - it misses the read outright and the controller eventually times "
            "out. Writes still work because a write burst holds CS asserted continuously across many "
            "consecutive unit intervals, so a timing lag that would miss one brief pulse doesn't matter "
            "once CS has been high for a while. Run CS/CA training (bring the CS timing error back to 0) "
            "before moving on to the write-path training steps - the identical real symptom as DDR "
            "Homework 6 in the flat set.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            setParam (*e.getPlugin (0), "burstwords", 16.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.82f);     // carried forward, still harmless at full swing with ZQ cal clean
            setParam (*chip, "odt", 60.0f); setParam (*chip, "zqerror", 0.0f);
            setParam (*chip, "cstiming", 0.97f);   // CS/CA training never ran
        }, -1, -1, -1, 0, 1 });

    // --- DDR Bring-up 5: write levelling never ran - one shared DQS-to-byte-lane skew. ---
    ddrBringups.push_back ({
        "DDRBRINGUP_05_write_leveling_missing",
        "DDR Bring-Up 5: Align DQS to CK - write levelling",
        "CS/CA timing is trained. Now the memory test fails almost every word, and when a word is wrong "
            "the whole byte usually comes back looking unrelated to what was written - no single pin "
            "stands out. What training step aligns DQS to CK at the DRAM, and is it done yet?",
        "Check this chip's per-bit DQ deskew settings - are all eight bits skewed by about the same "
            "amount, or does one stand out?",
        "All eight DQ bits are skewed by the same amount - a single, shared DQS-to-byte-lane timing error, "
            "exactly what write levelling exists to correct. Because every bit lags identically, the chip "
            "captures essentially the *previous* word's data for the whole byte rather than the current "
            "one - a uniform, whole-byte corruption, not an isolated-pin problem. Run write levelling "
            "(bring all eight DQ skews back to 0 together) before per-bit deskew in the next step - the "
            "identical real symptom as DDR Homework 7 in the flat set.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.82f); setParam (*chip, "odt", 60.0f); setParam (*chip, "zqerror", 0.0f);
            for (int b = 0; b < 8; ++b)
                setParam (*chip, "dqskew" + String (b), 0.4f);   // write levelling: never ran, whole byte lane equally skewed
        }, -1, -1, -1, 0, 1 });

    // --- DDR Bring-up 6: per-bit write-DQ deskew left incomplete on two pins. ---
    ddrBringups.push_back ({
        "DDRBRINGUP_06_per_bit_deskew_incomplete",
        "DDR Bring-Up 6: Finish per-bit DQ deskew",
        "Write levelling is done - CK and DQS are well aligned as a byte lane. The memory test mostly "
            "passes now, but a consistent minority of addresses still fail every run, while most are "
            "completely solid. What finer-grained training step comes after write levelling?",
        "Look at the per-bit DQ deskew indicators again now that write levelling is done - is it still "
            "every bit evenly, or just one or two that stand out from an otherwise well-trained byte lane?",
        "Two individual DQ pins were left with real per-bit deskew error while the rest of the byte lane "
            "is correctly centred - the training step real DDR calls per-bit write-DQ deskew, centring "
            "each pin's data against DQS individually rather than the byte lane as a whole. Because only "
            "two of eight bits are actually wrong, a captured byte is only corrupted on the addresses "
            "where at least one of those two bits happens to differ from the previous word's same bit, "
            "which is exactly why this looks sparser and \"only on certain data\" rather than totally "
            "broken. Deskew the remaining two pins individually - the identical real symptom as DDR "
            "Homework 8 in the flat set - and every DQ bit in the byte lane is now fully trained.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.82f); setParam (*chip, "odt", 60.0f); setParam (*chip, "zqerror", 0.0f);
            setParam (*chip, "dqskew3", 0.4f);   // only two of eight pins left untrained...
            setParam (*chip, "dqskew6", 0.4f);   // ...the rest of the byte lane is correctly deskewed
        }, -1, -1, -1, 0, 1 });

    // --- DDR Bring-up 7: move onto the real board and Vref isn't retrained for its real swing. ---
    ddrBringups.push_back ({
        "DDRBRINGUP_07_vref_not_retrained_for_real_channel",
        "DDR Bring-Up 7: Retrain Vref for the real board, not the bench",
        "Every training step so far was done on the bench, at essentially full swing, and the memory "
            "test passed cleanly there. Installed on the real board - the one Bring-Up 2 measured earlier "
            "in this plan - the memory test now fails almost every word. CS/CA, write levelling, and "
            "per-bit deskew are all still correctly trained. What changed?",
        "Compare the real DQ swing Bring-Up 2 measured on this board against the Vref this chip is still "
            "using. Was Vref retrained for this board's actual channel, or carried over from the bench?",
        "Vref is still 0.82 - left over from training it on the bench's essentially full-swing connection "
            "- but this board's real trace only delivers about 30% of that swing (the same number Bring-Up "
            "2 measured). A Vref trained for near-ideal swing sits far outside the real, attenuated eye, "
            "so almost every bit slices to the same fixed value no matter what's actually driven. Every "
            "other training step (CS/CA timing, write levelling, per-bit deskew) is purely about *when* "
            "this chip samples, so none of it cared about the bench-vs-board swing difference - Vref is "
            "the one setting that does, and it's exactly why real bring-up retrains Vref specifically "
            "against the real, installed channel rather than carrying over a bench value. Retrain Vref "
            "back toward the real channel's actual centre and the test passes cleanly again - the same "
            "real lesson as DDR Homework 2 in the flat set.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrLossyChannel::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            setParam (*e.getPlugin (1), "swing", 30.0f);   // the real board, measured back in Bring-Up 2
            auto* chip = e.getPlugin (3);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.82f);   // never retrained off the bench value
            setParam (*chip, "odt", 60.0f); setParam (*chip, "zqerror", 0.0f);
        }, -1, -1, -1, 0, 2 });

    // --- DDR Bring-up 8: margin stress at the chip's own rated interface - ODT can't be changed, can the
    // chip's own DFE absorb it? Deliberately done at full swing, NOT the real board's attenuated channel
    // from Bring-Up 2/7: live testing (see the long methodological note where this plan is introduced)
    // showed this chip's simplified single-tap DFE cancels its correction using the *full-scale* prior bit
    // decision, not the actual (swing-scaled) echo - so at this board's real ~30%-of-ideal swing, turning
    // the DFE on massively OVERCORRECTS and makes the error rate worse, not better. At full, rated swing
    // the two scale together and DFE genuinely fixes it, the same real chip-level characterization a part
    // gets on a clean test fixture, separate from any one board's own channel.
    ddrBringups.push_back ({
        "DDRBRINGUP_08_margin_stress_fixed_odt",
        "DDR Bring-Up 8: ODT is fixed elsewhere on the board - now what?",
        "Separate from the specific board Bring-Up 2 and 7 dealt with, this chip is now being "
            "characterized on a clean test fixture at its full rated swing - standard practice for a "
            "chip-level spec, independent of any one board's own channel. A late system-level finding "
            "says this chip's ODT target has to stay at its current (effectively-off) value for an "
            "unrelated board requirement - it cannot be reprogrammed. The memory test fails almost every "
            "word at that fixed ODT. Is there anything else on this chip that could still clean this up, "
            "given ODT itself is off the table?",
        "Check the chip's own DFE control. DDR5 DRAMs carry a real decision-feedback equalizer on their "
            "DQ receivers specifically to cancel known inter-symbol interference like a termination "
            "mismatch - is it actually turned on?",
        "With ODT fixed at a mismatched value, the resulting termination reflects a scaled echo of the "
            "signal back into later bits - the same reflection mechanism as Bring-Up 3, just with the fix "
            "(reprogramming ODT) taken off the table this time. The chip's own single-tap DFE is sitting "
            "disabled; turning it on cancels that known echo using the chip's own prior decision, fully "
            "absorbing the same ODT problem without changing the termination itself - DFE Bring-Up 9 "
            "confirms with a live measurement, not just this claim. This mirrors a real DDR5 capability "
            "(DQ receivers carry their own, 4-tap-in-a-real-part DFE for exactly this reason) and the same "
            "real lesson as DDR Homework 9 in the flat set: when a signal-integrity problem can't be fixed "
            "at its physical source, equalization that was simply left off can sometimes still absorb it "
            "downstream - but (as Bring-Up 9 also found) only within the operating range that simplified "
            "equalizer was actually characterized for.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.5f);      // an ordinary, well-centred Vref for this fixture's full swing
            setParam (*chip, "odt", 240.0f);     // fixed elsewhere on the board - effectively off, can't change
            setParam (*chip, "zqerror", 0.0f);
            setParam (*chip, "dfe", 0.0f);        // ...and this chip's own DFE is simply left off
        }, -1, -1, -1, 0, 1 });

    // --- DDR Bring-up 9: everything is trained - run the final validation and decide if it's honest. ---
    ddrBringups.push_back ({
        "DDRBRINGUP_09_statistical_bist_validation",
        "DDR Bring-Up 9: A few errors isn't automatically a failure - but it isn't validated either",
        "DFE is on, absorbing the fixed-ODT problem from Bring-Up 8. Run the final validation the way a "
            "real bring-up lab would and decide: is this byte lane actually ready to ship?",
        "A validation run rarely comes back at a perfectly flat zero. Before calling it a failure, check "
            "*when* the errors happened - were they spread evenly through the run, or all bunched at the "
            "very start? And once you've accounted for that, what does a short run actually tell you about "
            "the real, steady-state word-error rate?",
        "A short validation run on this fully-trained chip comes back with 450 words checked and 64 errors "
            "- 14.2%, which looks alarming until you check when those errors actually happened: all 64 of "
            "them land in the very first fraction of a second after the chain loads, the same one-time "
            "BIST-synchronization transient Bring-Up 2 already flagged, and exactly zero more happen in "
            "the roughly 386 words checked after that. So the honest reading isn't \"14% error rate,\" "
            "it's \"zero steady-state errors out of about 386 words (roughly 3,100 bits) once the chain "
            "has settled\" - and even that only bounds the true steady-state word-error rate at about "
            "3 / 386, or under 1%, at 95% confidence (the same rule-of-three logic the SerDes side's "
            "Bring-Up 9 uses on bits). Real DRAM word-error-rate specs are routinely many orders of "
            "magnitude tighter than that - proving one statistically would take vastly more than a few "
            "seconds of BIST, the same as the SerDes side's own final lesson. \"No persistent errors in "
            "the time I watched\" and \"validated to spec\" are different claims, and conflating them is "
            "exactly the mistake a real final sign-off has to avoid.",
        [&] (HostEngine& e)
        {
            addStage (DdrController::id); addStage (DdrEyeScope::id); addStage (DdrChip::id);
            setParam (*e.getPlugin (0), "targetchip", 0.0f);
            auto* chip = e.getPlugin (2);
            setParam (*chip, "chipnumber", 0.0f);
            setParam (*chip, "vref", 0.5f);
            setParam (*chip, "odt", 240.0f); setParam (*chip, "zqerror", 0.0f);
            setParam (*chip, "dfe", 1.0f);
        }, -1, -1, -1, 0, 1 });

    // =====================================================================================
    // PLL bring-up & validation plan: bringing a frequency synthesizer up one real step at a
    // time (free-run VCO check -> close the loop with the right divider -> bring loop gains up
    // to spec -> tune out ringing -> stress-test against reference jitter -> final sign-off).
    // Numbers below are from the real GenerateHomework tool's own write->XML->reload->measure
    // round trip (see PllCore.cpp's own header comment for the wn/zeta theory this is checked
    // against, and the HOMEWORK_26-29 flat scenarios this plan's middle steps are drawn from).
    // =====================================================================================

    // --- PLL Bring-up 1: check the free-running oscillator itself before ever closing the loop. ---
    pllBringups.push_back ({
        "PLLBRINGUP_01_vco_freerun_check",
        "PLL Bring-Up 1: Before closing any loop, check the oscillator alone",
        "A new frequency synthesizer board is meant to multiply a 25 MHz reference up to 1000 MHz. Before "
            "wiring up the feedback path at all, characterize the VCO by itself: with the loop filter "
            "disconnected (zero gain, so the VCO just runs at whatever its own free-run frequency is), what "
            "does it actually put out?",
        "With both loop filter gains at zero, the VCO's output is simply its own free-run frequency - no "
            "correction is being applied at all. Read that frequency directly. Is it anywhere close to "
            "the 1000 MHz this board is specified to lock to?",
        "With the loop filter gains both at zero (an intentionally open loop, so the VCO's output is "
            "exactly its own free-run frequency with no correction), the VCO measures at roughly 800 MHz - "
            "200 MHz, a full 20%, short of the 1000 MHz this board needs to lock to. This is the first thing "
            "a real PLL bring-up checks, before the loop is ever closed: a VCO's free-run (\"center\") "
            "frequency has to land within the loop's capture range of the actual target, or closing the "
            "loop may never achieve lock no matter how well the loop filter itself is tuned - exactly the "
            "same \"check the foundation before building on it\" habit as the SerDes side's own Bring-Up 1 "
            "(TX output alone) and the DDR side's Bring-Up 1 (chip-select alone). The fix here is correcting "
            "the VCO's free-run frequency setting to land close to the intended target before closing the loop.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 40.0f);
            setParam (*pll, "vcofree", 800.0f);     // badly short of the 1000 MHz target
            setParam (*pll, "loopkp", 0.0f);        // loop deliberately open for this check
            setParam (*pll, "loopki", 0.0f);
        }, -1, -1, -1, -1, -1, 0 });

    // --- PLL Bring-up 2: VCO corrected, loop closed - but the feedback divider is wrong. ---
    pllBringups.push_back ({
        "PLLBRINGUP_02_feedback_divider_check",
        "PLL Bring-Up 2: Closing the loop - to the wrong frequency",
        "The VCO's free-run frequency is now close to spec, so the loop filter is connected for the first "
            "time. Status reports LOCKED almost immediately. Before moving on, confirm the thing that "
            "actually matters: is the output frequency the right one?",
        "Read the VCO output frequency directly and compare it against reference x the programmed feedback "
            "divider. Does the divider actually match what this board is specified to use?",
        "The feedback divider is programmed for N=41 instead of the specified N=40, so the now-closed loop "
            "locks exactly as designed - just to 25 MHz x 41 = 1025 MHz instead of the intended 1000 MHz. "
            "A solid LOCKED status only confirms the loop found a stable, phase-aligned state; it says "
            "nothing about whether that state is the one you wanted (HOMEWORK_26 demonstrates the same "
            "fault in isolation). Always cross-check the actual output frequency against reference x N "
            "before trusting a freshly-closed loop's lock indicator. The fix is correcting the feedback "
            "divider to the specified N=40.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 41.0f);    // wrong - the board is specified for 40
            setParam (*pll, "vcofree", 950.0f);     // corrected from Bring-Up 1, close to the (true) target
            setParam (*pll, "loopkp", 290.0f);
            setParam (*pll, "loopki", 1100.0f);
        }, -1, -1, -1, -1, -1, 0 });

    // --- PLL Bring-up 3: divider corrected - but the loop gains are left far too weak to ever finish. ---
    pllBringups.push_back ({
        "PLLBRINGUP_03_loop_gain_too_weak",
        "PLL Bring-Up 3: Right divider, but it still never gets there",
        "The feedback divider is corrected to N=40. The output frequency is now heading in the right "
            "direction, but after a long wait it's still visibly, persistently short of 1000 MHz and never "
            "reports LOCKED. Before suspecting the VCO or divider again, look at how hard the loop filter "
            "is actually correcting.",
        "Compare the loop filter's gains against the values Bring-Up 2 just used successfully. Were they "
            "carried forward, or left at something much weaker?",
        "Both loop filter gains were left roughly 50x weaker than Bring-Up 2's working values - the loop is "
            "pulling the VCO in the right direction, just far too slowly to finish in any reasonable bring-up "
            "window (HOMEWORK_27 demonstrates the same fault in isolation). The divider and reference are "
            "both correct; this is a genuine loop-bandwidth problem, not a sign of anything wrong upstream. "
            "The fix is restoring both loop filter gains to this design's normal values.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 40.0f);    // corrected
            setParam (*pll, "vcofree", 950.0f);
            setParam (*pll, "loopkp", 5.0f);        // ~50x weaker than the normal 290 / 1100
            setParam (*pll, "loopki", 5.0f);
        }, -1, -1, -1, -1, -1, 0 });

    // --- PLL Bring-up 4: gains restored - but unbalanced, so the loop rings before settling. ---
    pllBringups.push_back ({
        "PLLBRINGUP_04_damping_check",
        "PLL Bring-Up 4: It locks now, but the scenic route",
        "The loop gains are back up near their normal magnitude and the loop does eventually report LOCKED "
            "- but a technician watching the output frequency during acquisition says it visibly overshoots "
            "and rings back and forth several times before settling, instead of approaching the target "
            "smoothly. Check whether the two loop gains are actually balanced against each other.",
        "A PLL's damping comes from the BALANCE between the proportional and integral loop-filter gains, "
            "not either one alone. Is the proportional gain anywhere near its normal value relative to the "
            "integral gain, or is one path left dominating?",
        "The integral gain is back at its normal value, but the proportional gain was left roughly 6x lower "
            "than normal - a classically underdamped loop filter (HOMEWORK_28 demonstrates the same fault "
            "in isolation). The integral path still has enough authority to eventually pull the frequency "
            "all the way in, but with too little proportional damping to restrain it, the loop overshoots "
            "and rings before settling rather than approaching smoothly. The fix is restoring the "
            "proportional gain to its normal value, which damps the ringing out without changing how far "
            "the loop can ultimately pull in.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 40.0f);
            setParam (*pll, "vcofree", 950.0f);
            setParam (*pll, "loopkp", 50.0f);       // normal is 290 - underdamped against ki below
            setParam (*pll, "loopki", 1100.0f);
        }, -1, -1, -1, -1, -1, 0 });

    // --- PLL Bring-up 5: fully tuned - now stress-test it against a noisy reference. ---
    pllBringups.push_back ({
        "PLLBRINGUP_05_reference_jitter_stress",
        "PLL Bring-Up 5: A margin stress test - how noisy a reference can this tolerate?",
        "The loop is fully tuned to its normal values and locks cleanly. Before sign-off, stress it the way "
            "a real validation lab would: feed it a deliberately noisy reference and see what actually "
            "degrades. A technician watching the lock indicator during this test reports it flickering on "
            "and off and calls it a loop stability failure - is that the right read?",
        "Don't trust the lock indicator alone under a noisy reference. Average the VCO output frequency "
            "over a few seconds instead - is the average still close to reference x N, even while the "
            "indicator flickers?",
        "A large amount of timing jitter is injected onto the reference - enough that individual reference "
            "edges arrive measurably early or late, which alone is enough to trip a simple instantaneous-"
            "phase-error lock detector, even though the loop filter is correctly averaging the jitter out "
            "and the VCO's output frequency, averaged over a few seconds, still lands close to the intended "
            "reference x N (HOMEWORK_29 demonstrates the same effect in isolation). This is a genuine "
            "measurement-methodology lesson for a stress test, not a loop-stability failure: a lock detector "
            "built on an instantaneous threshold can read as unlocked even while the loop is doing its job "
            "correctly, if the thing it's watching is itself noisy for a reason that has nothing to do with "
            "loop stability. The real action item from a result like this is upstream - tightening whatever "
            "is injecting that much jitter onto the reference - not retuning the loop filter.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 40.0f);
            setParam (*pll, "vcofree", 1000.0f);
            setParam (*pll, "loopkp", 290.0f);
            setParam (*pll, "loopki", 1100.0f);
            setParam (*pll, "refjitter", 0.05f);    // the deliberate stress for this step only
        }, -1, -1, -1, -1, -1, 0 });

    // --- PLL Bring-up 6: final sign-off, with a clean reference again. ---
    pllBringups.push_back ({
        "PLLBRINGUP_06_final_signoff",
        "PLL Bring-Up 6: Final sign-off",
        "Every step so far is corrected and the stress test is behind it. With a clean reference restored, "
            "run the final sign-off the way a real bring-up lab would and confirm this synthesizer is ready: "
            "locked, on frequency, and settled - not just technically running.",
        "Check all three together: the lock indicator, the output frequency against reference x N, and the "
            "instantaneous phase error once settled - not just whichever one looks best on its own.",
        "With every earlier fault corrected and a clean reference restored, the synthesizer locks cleanly, "
            "settles without the ringing Bring-Up 4 showed, and the output frequency lands on reference x N "
            "to within a small fraction of a percent - genuinely ready, not just technically running. This "
            "is the real final-sign-off habit this whole plan has been building toward: confirm the lock "
            "indicator, the actual output frequency, and the settled phase error together, the same "
            "\"don't trust a single number alone\" discipline Bring-Up 2 and Bring-Up 5 each already needed "
            "for their own reasons.",
        [&] (HostEngine& e)
        {
            addStage (PllCore::id);
            auto* pll = e.getPlugin (0);
            setParam (*pll, "reffreq", 25.0f);
            setParam (*pll, "fbdivider", 40.0f);
            setParam (*pll, "vcofree", 1000.0f);
            setParam (*pll, "loopkp", 290.0f);
            setParam (*pll, "loopki", 1100.0f);
        }, -1, -1, -1, -1, -1, 0 });

    // --- write + measure, shared between the homework set and the bring-up plan below ---
    auto processHomeworkSet = [&] (std::vector<Homework>& list)
    {
    // --- write ---
    for (auto& hw : list)
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
    for (auto& hw : list)
    {
        engine.clearChain();
        engine.link->lineRateGBd = 3.0f;
        auto xml = parseXML (presetDir.getChildFile (String (hw.file) + ".labchain"));
        bool done = false; StringArray fails;
        engine.loadChain (*xml, [&] (const String&, const StringArray& f) { fails = f; done = true; });
        pump ([&] { return done; });

        if (! fails.isEmpty()) { std::printf ("LOAD PROBLEM %s: %s\n", hw.file, fails.joinIntoString ("; ").toRawUTF8()); continue; }
        if (! engine.homework.present) { std::printf ("HOMEWORK LOST ON RELOAD %s\n", hw.file); continue; }

        // DDR homeworks are a wholly different measurement: BIST word counters, not an eye/BER - skip the
        // SerDes-side measurement entirely rather than reach for an EyeScope that isn't in this chain.
        if (hw.ddrControllerSlot >= 0)
        {
            auto* controller = dynamic_cast<DdrController*> (engine.getPlugin ((size_t) hw.ddrControllerSlot));
            wait (5000);
            const auto checked = controller->status.wordsChecked.load(), errors = controller->status.errors.load();
            std::printf ("LIVE %-38s BIST words checked=%6llu errors=%6llu (%.1f%%)\n", hw.file,
                         (unsigned long long) checked, (unsigned long long) errors,
                         checked == 0 ? 0.0 : 100.0 * (double) errors / (double) checked);

            // Confirms, rather than just asserts in the explanation text, what the DDR Eye Scope actually
            // shows for this homework: either a genuinely degraded bus (Homework 2, the lossy-channel one)
            // or a perfectly open eye despite the BIST failures above (every other DDR homework, whose
            // faults live entirely inside the chip's own trained settings, not on the bus itself).
            if (hw.ddrScopeSlot >= 0)
            {
                auto* ddrScope = dynamic_cast<DdrEyeScope*> (engine.getPlugin ((size_t) hw.ddrScopeSlot));
                const int probeParam = (int) ddrScope->getParameterTree().getRawParameterValue ("probe")->load();
                const bool isDqsProbe = probeParam == DdrBus::dqWidth;

                ddrScope->setCaptureActive (true);
                EyeAnalyzer ddrAnalyzer;
                ddrAnalyzer.setup (ddrScope->getLink().samplesPerUI (ddrScope->getSampleRate()) * (isDqsProbe ? 0.5 : 1.0),
                                   false, 0.75f);
                std::vector<float> ds; std::vector<double> didx;
                for (int t = 0; t < 90; ++t)
                {
                    wait (33); ds.clear(); didx.clear(); ddrScope->readCaptured (ds, didx);
                    size_t rs = 0;
                    for (size_t i = 1; i <= ds.size(); ++i)
                        if (i == ds.size() || didx[i] - didx[i - 1] > 1.5)
                        {
                            if (i > rs) ddrAnalyzer.push (ds.data() + rs, (int) (i - rs), didx[rs]);
                            rs = i;
                        }
                }
                ddrScope->setCaptureActive (false);
                auto dm = ddrAnalyzer.getMetrics();
                std::printf ("     %-38s DQ/DQS eye (%s) locked=%d h=%5.1f%% (%.3f FS) w=%3.0f%% swing=%.3f FS\n", "",
                             DdrEyeScope::getProbeName (probeParam), (int) ddrAnalyzer.isLocked(), dm.heightPct, dm.heightFS,
                             dm.widthPct, dm.swing);
            }
            continue;
        }

        // PLL homeworks are a third, wholly different measurement again: lock status and VCO frequency
        // sampled repeatedly over a window (not a single final read), so a homework can distinguish
        // "never locked", "locked but rang/overshot on the way", and "locked confidently, just to the
        // wrong frequency" - not just a single pass/fail number.
        if (hw.pllCoreSlot >= 0)
        {
            auto* pll = dynamic_cast<PllCore*> (engine.getPlugin ((size_t) hw.pllCoreSlot));
            const double targetHz = (double) pll->getParameterTree().getRawParameterValue ("reffreq")->load()
                                   * (double) pll->getParameterTree().getRawParameterValue ("fbdivider")->load();

            int64_t lockedAtMs = -1;
            float maxAbsPhaseErrorRad = 0.0f;
            constexpr int numSamples = 300;
            double freqSum = 0.0;
            int freqAveraged = 0;
            for (int t = 0; t < numSamples; ++t)
            {
                wait (33);
                maxAbsPhaseErrorRad = jmax (maxAbsPhaseErrorRad, std::abs (pll->status.phaseErrorRad.load()));
                if (lockedAtMs < 0 && pll->status.locked.load())
                    lockedAtMs = (int64_t) t * 33;

                // A single instantaneous VCO-frequency reading is itself noisy under injected reference
                // jitter (the loop filter smooths it, but doesn't eliminate sample-to-sample wiggle) - only
                // the SECOND half of the window (past any startup/acquisition transient) is averaged, which
                // is what the "average over a few seconds, don't trust one reading" homework lesson itself
                // asks the reader to do - this measurement does the same thing it's teaching, not a
                // one-shot snapshot that would understate how close the loop really tracks on average.
                if (t >= numSamples / 2)
                {
                    freqSum += pll->status.vcoFreqHz.load();
                    ++freqAveraged;
                }
            }

            const float finalFreq = pll->status.vcoFreqHz.load();
            const double avgFreq = freqAveraged > 0 ? freqSum / freqAveraged : (double) finalFreq;
            std::printf ("LIVE %-38s locked=%s at %6lldms  final VCO=%10.4f Hz, avg(2nd half)=%10.4f Hz (target %10.4f Hz, %.3f%% off avg)  max|phase err|=%.3f rad\n",
                         hw.file, lockedAtMs < 0 ? "no " : "yes", (long long) lockedAtMs, (double) finalFreq, avgFreq, targetHz,
                         targetHz == 0.0 ? 0.0 : 100.0 * std::abs (avgFreq - targetHz) / targetHz, (double) maxAbsPhaseErrorRad);
            continue;
        }

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
                std::printf ("LIVE %-38s eye h=%5.1f%% (%.3f FS) w=%3.0f%% swing=%.3f | NO VALID BITS RECOVERED (never synced) | locked=%d\n",
                             hw.file, m.heightPct, m.heightFS, m.widthPct, m.swing, rx ? (int) rx->status.locked.load() : -1);
            else
                std::printf ("LIVE %-38s eye h=%5.1f%% (%.3f FS) w=%3.0f%% swing=%.3f | %6llu bits %5llu err BER %s | locked=%d\n", hw.file,
                             m.heightPct, m.heightFS, m.widthPct, m.swing,
                             (unsigned long long) st.compared, (unsigned long long) st.errors,
                             (st.errors == 0 ? ("<" + String (compare.berUpperBound95(), 1, true)) : String (compare.ber(), 1, true)).toRawUTF8(),
                             rx ? (int) rx->status.locked.load() : -1);
        }
        else
        {
            std::printf ("LIVE %-38s eye h=%5.1f%% (%.3f FS) w=%3.0f%% swing=%.3f | NMSE %6.1f dB\n", hw.file,
                         m.heightPct, m.heightFS, m.widthPct, m.swing,
                         LinkSettings::nmseToDb (engine.link->probe.nmse.load()));
        }

        if (hw.jitterSlot >= 0)
        {
            auto* jitter = dynamic_cast<JitterScope*> (engine.getPlugin ((size_t) hw.jitterSlot));
            jitter->setCaptureActive (true);
            JitterAnalyzer janalyzer;
            janalyzer.setup (engine.link->samplesPerUI (jitter->getSampleRate()), engine.link->baudHz(), engine.link->isPam4());
            std::vector<float> js; std::vector<double> jidx;
            // Needs lockUI (1500) plus a full Welch average (fftSize * segmentsAveraged) worth of UI symbols
            // to report a spectrum; at 10 GBd (10 kHz sim) that's a couple of seconds, so several more give
            // comfortable margin.
            for (int t = 0; t < 220; ++t)
            {
                wait (33); js.clear(); jidx.clear(); jitter->readCaptured (js, jidx);
                size_t rs = 0;
                for (size_t i = 1; i <= js.size(); ++i)
                    if (i == js.size() || jidx[i] - jidx[i - 1] > 1.5)
                    {
                        if (i > rs) janalyzer.push (js.data() + rs, (int) (i - rs), jidx[rs]);
                        rs = i;
                    }
            }
            jitter->setCaptureActive (false);
            auto jm = janalyzer.getMetrics();
            const double ps = janalyzer.uiToRealPs();
            std::printf ("     %-38s jitter locked=%d rms=%.2fps pk-pk=%.2fps | tone %s",
                         "", (int) janalyzer.isLocked(), jm.rmsUI * ps, jm.ppUI * ps, jm.toneAboveNoise ? "YES" : "no");
            if (jm.toneAboveNoise)
                std::printf (" %.2fps rms at %.3f MHz", jm.toneUI * ps, jm.toneHzSim);
            std::printf ("\n");
        }
    }
    };

    processHomeworkSet (set);
    processHomeworkSet (bringups);
    processHomeworkSet (ddrBringups);
    processHomeworkSet (pllBringups);

    return 0;
}
