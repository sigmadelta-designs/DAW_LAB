#include "EyeScopeEditor.h"

EyeScopeEditor::EyeScopeEditor (EyeScope& scopeToEdit)
    : juce::AudioProcessorEditor (scopeToEdit), scope (scopeToEdit)
{
    for (auto* c : std::initializer_list<juce::Component*> { &scaleSlider, &persistenceBox, &clearButton,
                                                             &relockButton, &scaleLabel, &persistenceLabel, &viewBox, &viewLabel, &thruButton, &trainerButton })
        addAndMakeVisible (c);

    persistenceBox.addItemList ({ "Infinite", "Long", "Short" }, 1);
    viewBox.addItemList ({ "Differential (odd)", "Common-mode (even)", "+ leg (R)", "- leg (L)" }, 1);
    viewAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        scope.getParameterTree(), "view", viewBox);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        scope.getParameterTree(), "vscale", scaleSlider);
    persistenceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        scope.getParameterTree(), "persistence", persistenceBox);

    thruAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (scope.getParameterTree(), "thru", thruButton);
    trainerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (scope.getParameterTree(), "trainer", trainerButton);
    thruButton.setTooltip ("On: the signal continues to the next stage unchanged. Off: the scope terminates it (nothing is passed on).");
    clearButton.onClick = [this] { analyzer.clearImage(); };
    relockButton.onClick = [this] { analyzer.relock(); };

    setSize (760, 620);
    scope.setCaptureActive (true);
    startTimerHz (30);
}

EyeScopeEditor::~EyeScopeEditor()
{
    stopTimer();
    scope.setCaptureActive (false);
}

void EyeScopeEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto viewRow = area.removeFromBottom (30);
    viewLabel.setBounds (viewRow.removeFromLeft (50));
    viewBox.setBounds (viewRow.removeFromLeft (200));
    viewRow.removeFromLeft (16);
    thruButton.setBounds (viewRow.removeFromLeft (120));
    trainerButton.setBounds (viewRow.removeFromLeft (150));
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
    readoutArea = area.removeFromBottom (98);
    area.removeFromBottom (8);
    eyeArea = area;
}

void EyeScopeEditor::timerCallback()
{
    auto& link = scope.getLink();
    const float verticalScale = scope.getParameterTree().getRawParameterValue ("vscale")->load();
    analyzer.setup (link.samplesPerUI (scope.getSampleRate()), link.isPam4(), verticalScale);

    const int view = (int) scope.getParameterTree().getRawParameterValue ("view")->load();
    if (view != lastView)
    {
        lastView = view;
        analyzer.resetAll();   // a different signal: start the picture over
    }

    pulledSamples.clear();
    pulledIndices.clear();
    scope.readCaptured (pulledSamples, pulledIndices);

    // The FIFO can have gaps (drops, or a graph rebuild), so feed contiguous runs.
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

void EyeScopeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));
    drawEye (g, eyeArea.toFloat());

    // Readout
    auto& link = scope.getLink();
    const auto& metrics = analyzer.getMetrics();
    auto area = readoutArea;

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String (link.lineRateGBd.load(), 2) + " GBd " + (link.isPam4() ? "PAM4" : "NRZ")
                    + "   (" + juce::String (link.baudHz() / 1000.0, 2) + " kBd on the audio side)   UI = "
                    + juce::String (link.samplesPerUI (scope.getSampleRate()), 2) + " samples",
                area.removeFromTop (22), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (14.0f));

    {
        const auto& m = scope.measurements;
        g.setColour (juce::Colours::lightblue);
        g.setFont (juce::FontOptions (13.0f));
        if (m.valid.load())
        {
            const float odd = m.oddRms.load(), even = m.evenRms.load();
            const juce::String ratio = odd > 1.0e-6f ? juce::String (juce::Decibels::gainToDecibels (even / odd, -99.0f), 1) + " dB"
                                                     : juce::String ("--");
            g.drawText ("Odd (differential R-L): " + juce::String (odd, 3) + " rms, " + juce::String (m.oddPeakToPeak.load(), 3)
                            + " pk-pk     Even (common): " + juce::String (even, 3) + " rms, " + juce::String (m.evenPeakToPeak.load(), 3)
                            + " pk-pk     Even / odd: " + ratio,
                        area.removeFromBottom (22), juce::Justification::centredLeft);
        }
        else
            area.removeFromBottom (22);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f));
    }

    if (! analyzer.isLocked())
    {
        g.setColour (juce::Colours::orange);
        g.drawText ("Locking to the data edges - is a generator upstream, at this line rate?",
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
        const juce::String eyes = link.isPam4() ? "worst of 3 eyes" : "eye";
        g.drawText ("Height (" + eyes + "): " + juce::String (metrics.heightFS, 3) + " FS  =  "
                        + juce::String (juce::roundToInt (metrics.heightPct)) + "% of ideal" + (metrics.closed ? "   CLOSED" : ""),
                    area.removeFromTop (22), juce::Justification::centredLeft);
        g.drawText ("Width: " + juce::String (metrics.widthUI, 2) + " UI  =  " + juce::String (juce::roundToInt (metrics.widthPct))
                        + "%      Outer swing at sample point: " + juce::String (metrics.swing, 3) + " FS"
                        + (link.probe.valid.load() ? "      Decision error: " + juce::String (LinkSettings::nmseToDb (link.probe.nmse.load()), 1)
                                                         + " dB (NMSE)"
                                                   : juce::String()),
                    area, juce::Justification::centredLeft);
    }
}

void EyeScopeEditor::drawEye (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colours::black);
    g.fillRect (area);

    analyzer.render (image);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image, area);

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (EyeScope::getViewName (lastView < 0 ? 0 : lastView), area.reduced (6.0f).withHeight (16.0f), juce::Justification::topRight);

    // Graticule
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
