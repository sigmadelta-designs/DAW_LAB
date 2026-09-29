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
            "keeps compounding the reflections. Real fix: check that on-die termination is enabled in the "
            "receiver's control registers, or that the termination resistor is actually populated.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (TraceChannel::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            auto* trace = dynamic_cast<TraceChannel*> (e.getPlugin (2));
            TraceRouter::generateGame (trace->getBoard(), 7, 4);
            TraceRouter::solve (trace->getBoard());
            trace->boardEdited();
            setParam (*e.getPlugin (2), "reflections", 2.0f);
            setParam (*e.getPlugin (2), "connector", 0.0f);
            setParam (*e.getPlugin (2), "sourcer", 50.0f);
            setParam (*e.getPlugin (2), "loadr", 3000.0f);
            trace->rebuildProfile();
            setParam (*e.getPlugin (3), "vscale", 0.6f);
        }, 3 });

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
            "traffic-dependent failure in the field. Re-routing with more spacing (or adding shielding/guard "
            "traces) away from the aggressor lane fixes it.",
        [&] (HostEngine& e)
        {
            e.link->lineRateGBd = 10.0f; e.link->modulation = LinkSettings::nrz;
            addStage (SignalGenerator::id); addStage (DiffFfe::id); addStage (TraceChannel::id); addStage (EyeScope::id);
            setParam (*e.getPlugin (0), "pattern", (float) PrbsGenerator::prbs15);
            setParam (*e.getPlugin (1), "txmode", 1.0f); setParam (*e.getPlugin (1), "adapt", 1.0f);
            auto* trace = dynamic_cast<TraceChannel*> (e.getPlugin (2));
            auto& board = trace->getBoard();
            TraceRouter::generateGame (board, 21, 5);
            TraceRouter::aggressorPenalty = 0.0;    // force a lane-blind route, as if nobody checked
            TraceRouter::solve (board);
            TraceRouter::aggressorPenalty = 0.6;    // restore the default for anything routed afterward
            trace->boardEdited();
            setParam (*e.getPlugin (3), "vscale", 0.6f);
        }, 3 });

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
