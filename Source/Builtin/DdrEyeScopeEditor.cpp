#include "DdrEyeScopeEditor.h"

DdrEyeScopeEditor::DdrEyeScopeEditor (DdrEyeScope& scopeToEdit)
    : juce::AudioProcessorEditor (scopeToEdit), scope (scopeToEdit)
{
    setLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &scaleSlider, &persistenceBox, &clearButton,
                                                             &relockButton, &scaleLabel, &persistenceLabel, &probeBox, &probeLabel, &thruButton })
        addAndMakeVisible (c);

    persistenceBox.addItemList ({ "Infinite", "Long", "Short" }, 1);
    juce::StringArray probeNames;
    for (int p = 0; p < DdrEyeScope::numProbes; ++p)
        probeNames.add (DdrEyeScope::getProbeName (p));
    probeBox.addItemList (probeNames, 1);

    probeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        scope.getParameterTree(), "probe", probeBox);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        scope.getParameterTree(), "vscale", scaleSlider);
    persistenceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        scope.getParameterTree(), "persistence", persistenceBox);
    thruAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        scope.getParameterTree(), "thru", thruButton);

    using D = ControlTip::Domain;
    scaleSlider.setTooltip (ControlTip::make ("Vertical scale", D::simulation,
        "Sets how much signal amplitude fills the plot - a display range control on this simulated scope, "
        "the same idea as Volts/div on a real one. It changes what you see, not the signal itself."));
    persistenceBox.setTooltip (ControlTip::make ("Persistence", D::simulation,
        "How long each trace glows before fading - a display setting on this simulated scope, the same "
        "idea as an oscilloscope's persistence control. Longer makes an intermittent problem easier to "
        "spot; shorter makes the current eye shape clearer."));
    probeBox.setTooltip (ControlTip::make ("Probe", D::simulation,
        "Which bus signal this scope is actually watching - any one of the eight single-ended DQ bits, or "
        "DQS itself as a true differential pair (dqsP - dqsN). A single DQ bit has no second leg to form a "
        "differential signal from, the way DQS (and CK) genuinely do - this is a probe-point choice, like "
        "moving a real scope's input to a different pin, not a property of the signal itself."));
    thruButton.setTooltip (ControlTip::make ("Pass-through", D::simulation,
        "On: the bus continues to the next stage unchanged, so probing here doesn't disturb the chain - "
        "important on the DDR bus specifically, since turning this off also silences every chip-select, "
        "command and address line past this point, not just DQ/DQS. Off: this scope terminates the whole "
        "bus here instead, the way removing a probe and terminating the line would."));
    clearButton.setTooltip ("Clears the persistence image and starts it over.");
    relockButton.setTooltip ("Re-estimates the clock phase this display uses to line up symbols, in case the picture has drifted.");

    clearButton.onClick = [this] { analyzer.clearImage(); };
    relockButton.onClick = [this] { analyzer.relock(); };

    setSize (760, 600);
    scope.setCaptureActive (true);
    startTimerHz (30);
}

DdrEyeScopeEditor::~DdrEyeScopeEditor()
{
    stopTimer();
    scope.setCaptureActive (false);
    setLookAndFeel (nullptr);
}

void DdrEyeScopeEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto probeRow = area.removeFromBottom (30);
    probeLabel.setBounds (probeRow.removeFromLeft (50));
    probeBox.setBounds (probeRow.removeFromLeft (160));
    probeRow.removeFromLeft (16);
    thruButton.setBounds (probeRow.removeFromLeft (120));
    area.removeFromBottom (6);

    auto controls = area.removeFromBottom (30);
    scaleLabel.setBounds (controls.removeFromLeft (90));
    scaleSlider.setBounds (controls.removeFromLeft (230));
    controls.removeFromLeft (16);
    persistenceLabel.setBounds (controls.removeFromLeft (76));
    persistenceBox.setBounds (controls.removeFromLeft (100));
    controls.removeFromLeft (16);
    clearButton.setBounds (controls.removeFromLeft (70));
    controls.removeFromLeft (6);
    relockButton.setBounds (controls.removeFromLeft (80));

    area.removeFromBottom (8);
    readoutArea = area.removeFromBottom (76);
    area.removeFromBottom (8);
    eyeArea = area;
}

void DdrEyeScopeEditor::timerCallback()
{
    auto& link = scope.getLink();
    const float verticalScale = scope.getParameterTree().getRawParameterValue ("vscale")->load();
    const int probe = (int) scope.getParameterTree().getRawParameterValue ("probe")->load();

    // DQS toggles every HALF of a DQ unit interval (it's the strobe, not the data - see DdrController),
    // so the eye analyzer's own notion of "one symbol period" has to be halved when probing it, or its
    // zero-crossing clock recovery locks to the wrong rate and the eye collapses to nothing. DQ/DQS are
    // both always NRZ-like (never PAM4) in this simulation.
    const bool isDqs = probe == DdrBus::dqWidth;
    analyzer.setup (link.samplesPerUI (scope.getSampleRate()) * (isDqs ? 0.5 : 1.0), false, verticalScale);
    if (probe != lastProbe)
    {
        lastProbe = probe;
        analyzer.resetAll();   // a different signal: start the picture over
    }

    pulledSamples.clear();
    pulledIndices.clear();
    scope.readCaptured (pulledSamples, pulledIndices);

    size_t runStart = 0;
    for (size_t i = 1; i <= pulledSamples.size(); ++i)
    {
        if (i == pulledSamples.size() || pulledIndices[i] - pulledIndices[i - 1] > 1.5)
        {
            if (i > runStart)
                analyzer.push (pulledSamples.data() + runStart, (int) (i - runStart), pulledIndices[runStart]);
            runStart = i;
        }
    }

    static const float decays[] = { 1.0f, 0.97f, 0.85f };
    const int persistence = (int) scope.getParameterTree().getRawParameterValue ("persistence")->load();
    analyzer.applyDecay (decays[juce::jlimit (0, 2, persistence)]);

    repaint();
}

void DdrEyeScopeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));
    drawEye (g, eyeArea.toFloat());

    auto& link = scope.getLink();
    const auto& metrics = analyzer.getMetrics();
    auto area = readoutArea;

    // Shows the eye's OWN time base, which for DQS is half the data UI (see timerCallback) - labelling
    // it as a plain "UI" either way, since that's genuinely what this plot's x-axis unit is for whichever
    // signal is currently probed, not always literally one data bit's unit interval.
    const bool isDqsReadout = lastProbe == DdrBus::dqWidth;
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String (link.dataRateMTs.load(), 0) + " MT/s NRZ   ("
                    + juce::String (link.busHz() / 1000.0, 2) + " kHz on the audio side)   UI = "
                    + juce::String (link.samplesPerUI (scope.getSampleRate()) * (isDqsReadout ? 0.5 : 1.0), 2) + " samples"
                    + (isDqsReadout ? " (DQS toggles twice per data UI)" : ""),
                area.removeFromTop (22), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (14.0f));

    {
        const auto& m = scope.measurements;
        g.setColour (juce::Colours::lightblue);
        g.setFont (juce::FontOptions (13.0f));
        if (m.valid.load())
            g.drawText (juce::String (DdrEyeScope::getProbeName (lastProbe < 0 ? 0 : lastProbe)) + ": "
                            + juce::String (m.rms.load(), 3) + " rms, " + juce::String (m.peakToPeak.load(), 3) + " pk-pk",
                        area.removeFromBottom (22), juce::Justification::centredLeft);
        else
            area.removeFromBottom (22);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f));
    }

    if (! analyzer.isLocked())
    {
        g.setColour (juce::Colours::orange);
        g.drawText ("Locking to the data edges - is a DDR Controller upstream, actually writing?",
                    area, juce::Justification::centredLeft);
    }
    else if (! metrics.valid)
    {
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Measuring...", area, juce::Justification::centredLeft);
    }
    else
    {
        const auto status = metrics.closed || metrics.widthPct <= 0.0f ? juce::Colours::red
                          : (metrics.heightPct < 40.0f || metrics.widthPct < 50.0f ? juce::Colours::orange
                                                                                   : juce::Colours::lightgreen);
        g.setColour (status);
        g.drawText ("Height: " + juce::String (metrics.heightFS, 3) + " FS  =  "
                        + juce::String (juce::roundToInt (metrics.heightPct)) + "% of ideal" + (metrics.closed ? "   CLOSED" : ""),
                    area.removeFromTop (22), juce::Justification::centredLeft);
        g.drawText ("Width: " + juce::String (metrics.widthUI, 2) + " UI  =  " + juce::String (juce::roundToInt (metrics.widthPct))
                        + "%      Outer swing at sample point: " + juce::String (metrics.swing, 3) + " FS",
                    area, juce::Justification::centredLeft);
    }
}

void DdrEyeScopeEditor::drawEye (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colours::black);
    g.fillRect (area);

    analyzer.render (image);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image, area);

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (DdrEyeScope::getProbeName (lastProbe < 0 ? 0 : lastProbe), area.reduced (6.0f).withHeight (16.0f), juce::Justification::topRight);

    const float scale = scope.getParameterTree().getRawParameterValue ("vscale")->load();
    g.setColour (juce::Colours::white.withAlpha (0.18f));

    for (int i = 0; i <= 4; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    g.setFont (juce::FontOptions (11.0f));
    for (int i = 0; i <= 4; ++i)
    {
        const float v = scale * (1.0f - 0.5f * (float) i);
        const float y = area.getY() + area.getHeight() * (float) i / 4.0f;
        g.setColour (juce::Colours::white.withAlpha (i == 2 ? 0.35f : 0.15f));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawText (juce::String (v, 2), (int) area.getX() + 3, (int) y - (i == 0 ? 0 : 12), 40, 12, juce::Justification::left);
    }

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    for (int i = 0; i <= 4; ++i)
        g.drawText (juce::String ((float) i * 0.5f, 1) + " UI", (int) (area.getX() + area.getWidth() * (float) i / 4.0f) - 20,
                    (int) area.getBottom() - 14, 40, 12, juce::Justification::centred);

    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.drawRect (area, 1.0f);
}
