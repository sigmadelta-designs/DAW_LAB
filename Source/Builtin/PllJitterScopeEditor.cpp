#include "PllJitterScopeEditor.h"

PllJitterScopeEditor::PllJitterScopeEditor (PllJitterScope& scopeToEdit)
    : juce::AudioProcessorEditor (scopeToEdit), scope (scopeToEdit)
{
    setLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> { &probeBox, &probeLabel, &thruButton, &relockButton, &resetButton })
        addAndMakeVisible (c);

    probeBox.addItemList ({ "Reference", "VCO output", "Feedback" }, 1);
    probeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        scope.getParameterTree(), "probe", probeBox);
    thruAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        scope.getParameterTree(), "thru", thruButton);

    probeBox.setTooltip (ControlTip::make ("Probe", ControlTip::Domain::simulation,
        "Which of the PLL's three clock nodes the strip chart, histogram and spectrum measure. Pointed at "
        "the VCO output, this is the real bench measurement for reference spurs and jitter peaking; pointed "
        "at Feedback, it shows the divided clock that should match Reference once locked."));
    thruButton.setTooltip (ControlTip::make ("Pass-through", ControlTip::Domain::simulation,
        "On: the bus continues to the next stage unchanged, so probing here doesn't disturb the chain. "
        "Off: the scope terminates it instead."));
    relockButton.setTooltip ("Re-estimates the reference clock's starting phase, in case the measurement never locked or has drifted.");
    resetButton.setTooltip ("Clears the TIE history, histogram and spectrum averaging and starts over.");

    relockButton.onClick = [this] { analyzer.relock(); };
    resetButton.onClick = [this] { analyzer.resetAll(); };

    setSize (900, 760);
    scope.setCaptureActive (true);
    startTimerHz (20);
}

PllJitterScopeEditor::~PllJitterScopeEditor()
{
    stopTimer();
    scope.setCaptureActive (false);
    setLookAndFeel (nullptr);
}

void PllJitterScopeEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto controls = area.removeFromBottom (30);
    probeLabel.setBounds (controls.removeFromLeft (50));
    probeBox.setBounds (controls.removeFromLeft (160));
    controls.removeFromLeft (16);
    thruButton.setBounds (controls.removeFromLeft (120));
    controls.removeFromLeft (16);
    relockButton.setBounds (controls.removeFromLeft (80));
    controls.removeFromLeft (6);
    resetButton.setBounds (controls.removeFromLeft (100));

    area.removeFromBottom (8);
    readoutArea = area.removeFromBottom (88);
    area.removeFromBottom (8);

    auto top = area.removeFromTop (area.getHeight() / 2 - 4);
    histArea = top.removeFromRight (160);
    top.removeFromRight (8);
    stripArea = top;

    area.removeFromTop (8);
    spectrumArea = area;
}

double PllJitterScopeEditor::expectedFreqHzForProbe (int probe) const
{
    auto& link = scope.getLink();
    if (probe == PllJitterScope::probeRef) return link.refFreqHz.load();
    if (probe == PllJitterScope::probeVco) return link.vcoFreqHz.load();
    return link.vcoFreqHz.load() / (double) juce::jmax (1, link.fbDivider.load());   // Feedback = VCO / N
}

void PllJitterScopeEditor::timerCallback()
{
    const int probe = (int) scope.getParameterTree().getRawParameterValue ("probe")->load();
    const double freqHz = juce::jmax (1.0, expectedFreqHzForProbe (probe));
    analyzer.setup (juce::jmax (3.0, scope.getSampleRate() / freqHz), freqHz, false);

    if (probe != lastProbe)
    {
        lastProbe = probe;
        analyzer.resetAll();
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

    const auto& m = analyzer.getMetrics();
    if (m.valid)
    {
        const float neededPs = (float) (m.ppUI * analyzer.uiToRealPs()) * 1.4f;
        rangePs = juce::jmax (20.0f, juce::jmax (rangePs * 0.98f, neededPs));
    }

    repaint();
}

void PllJitterScopeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    drawStrip (g, stripArea.toFloat());
    drawHistogram (g, histArea.toFloat());
    drawSpectrum (g, spectrumArea.toFloat());

    auto& link = scope.getLink();
    const auto& m = analyzer.getMetrics();
    auto area = readoutArea;
    const int probe = (int) scope.getParameterTree().getRawParameterValue ("probe")->load();

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String (link.refFreqHz.load(), 3) + " MHz ref, /" + juce::String (link.fbDivider.load())
                    + " -> " + juce::String (link.vcoFreqHz.load(), 3) + " MHz VCO"
                    + (link.locked.load() ? "  [locked]" : "  [not locked]")
                    + "   UI = " + juce::String (scope.getSampleRate() / juce::jmax (1.0, expectedFreqHzForProbe (probe)), 2) + " samples",
                area.removeFromTop (22), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (13.0f));

    if (! analyzer.isLocked())
    {
        g.setColour (juce::Colours::orange);
        g.drawText ("Locking the reference clock to the probed signal's edges...",
                    area, juce::Justification::centredLeft);
        return;
    }

    if (! m.valid)
    {
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Measuring...", area, juce::Justification::centredLeft);
        return;
    }

    const double ps = analyzer.uiToRealPs();
    g.setColour (juce::Colours::lightgreen);
    g.drawText ("RMS jitter: " + juce::String (m.rmsUI * ps, 1) + " ps      Pk-Pk: " + juce::String (m.ppUI * ps, 1)
                    + " ps      (over the last " + juce::String (juce::jmin ((juce::int64) JitterAnalyzer::tieHistoryLength, analyzer.getTieTotalCount()))
                    + " edges)",
                area.removeFromTop (20), juce::Justification::centredLeft);

    g.setColour (juce::Colours::lightblue);
    g.drawText ("Spectral integral check: " + juce::String (m.rmsFromSpectrumUI * ps, 1)
                    + " ps rms  (should roughly match the direct RMS above - Parseval's theorem)",
                area.removeFromTop (20), juce::Justification::centredLeft);

    if (m.toneAboveNoise)
    {
        g.setColour (juce::Colours::hotpink);
        g.drawText ("Dominant periodic jitter / spur: " + juce::String (m.toneUI * ps, 1) + " ps rms at "
                        + juce::String (m.toneHzSim * 1.0e6, 1) + " Hz offset",
                    area.removeFromTop (20), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText ("No periodic jitter/spur stands out above the noise floor.",
                    area.removeFromTop (20), juce::Justification::centredLeft);
    }
}

void PllJitterScopeEditor::drawStrip (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff101418));
    g.fillRect (area);

    const double ps = analyzer.uiToRealPs();
    const auto& ring = analyzer.getTieHistoryUI();
    const int writePos = analyzer.getTieWritePos();
    const auto count = juce::jmin ((juce::int64) ring.size(), analyzer.getTieTotalCount());

    const auto y = [&] (float tieUI) { return area.getCentreY() - (tieUI * (float) ps / rangePs) * (area.getHeight() * 0.48f); };

    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

    if (count > 1)
    {
        juce::Path path;
        const int n = (int) count;
        for (int i = 0; i < n; ++i)
        {
            const int idx = (writePos - n + i + (int) ring.size() * 4) % (int) ring.size();
            const float x = area.getX() + area.getWidth() * (float) i / (float) (ring.size() - 1);
            const float py = y (ring[(size_t) idx]);
            i == 0 ? path.startNewSubPath (x, py) : path.lineTo (x, py);
        }
        g.setColour (juce::Colour (0xffffa63c));
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("TIE vs time (" + juce::String (JitterAnalyzer::tieHistoryLength) + " most recent edges, +-"
                    + juce::String (rangePs, 0) + " ps full scale)",
                area.reduced (6.0f).withHeight (16.0f), juce::Justification::topLeft);
    g.drawRect (area, 1.0f);
}

void PllJitterScopeEditor::drawHistogram (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff101418));
    g.fillRect (area);

    const double ps = analyzer.uiToRealPs();
    const auto& ring = analyzer.getTieHistoryUI();
    const auto count = juce::jmin ((juce::int64) ring.size(), analyzer.getTieTotalCount());

    static constexpr int numBins = 32;
    float bins[numBins] {};
    float maxBin = 1.0f;

    if (count > 0)
    {
        for (juce::int64 i = 0; i < count; ++i)
        {
            const float tiePs = ring[(size_t) i] * (float) ps;
            const float t = juce::jlimit (0.0f, 0.9999f, (tiePs / rangePs + 1.0f) * 0.5f);
            bins[(int) (t * numBins)] += 1.0f;
        }
        for (auto b : bins)
            maxBin = juce::jmax (maxBin, b);
    }

    for (int i = 0; i < numBins; ++i)
    {
        const float y0 = area.getY() + area.getHeight() * (float) i / (float) numBins;
        const float y1 = area.getY() + area.getHeight() * (float) (i + 1) / (float) numBins;
        const float w = (bins[numBins - 1 - i] / maxBin) * area.getWidth();
        g.setColour (juce::Colour (0xffffa63c).withAlpha (0.8f));
        g.fillRect (area.getX(), y0 + 0.5f, w, juce::jmax (1.0f, y1 - y0 - 1.0f));
    }

    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("Histogram", area.reduced (6.0f).withHeight (16.0f), juce::Justification::topLeft);
    g.drawRect (area, 1.0f);
}

void PllJitterScopeEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff101418));
    g.fillRect (area);

    const auto& spectrum = analyzer.getSpectrumDb();
    const double binHzReal = analyzer.getSpectrumBinHzSim() * 1.0e6;
    const double nyquistHz = binHzReal * (double) (spectrum.empty() ? 1 : spectrum.size() - 1);
    const double loHz = juce::jmax (1.0, binHzReal);
    const double loLog = std::log10 (loHz), hiLog = std::log10 (juce::jmax (loHz * 10.0, nyquistHz));

    float dbLo = -120.0f, dbHi = -40.0f;
    if (spectrum.size() > 2)
    {
        float lo = spectrum[1], hi = spectrum[1];
        for (size_t k = 1; k < spectrum.size(); ++k) { lo = juce::jmin (lo, spectrum[k]); hi = juce::jmax (hi, spectrum[k]); }
        dbLo = juce::jlimit (-220.0f, -20.0f, 20.0f * std::floor ((lo - 5.0f) / 20.0f));
        dbHi = juce::jlimit (dbLo + 20.0f, 20.0f, 20.0f * std::ceil ((hi + 5.0f) / 20.0f));
    }
    const auto x = [&] (double hz) { return area.getX() + (float) ((std::log10 (juce::jmax (loHz, hz)) - loLog) / (hiLog - loLog)) * area.getWidth(); };
    const auto yOf = [&] (float db) { return area.getBottom() - (juce::jlimit (dbLo, dbHi, db) - dbLo) / (dbHi - dbLo) * area.getHeight(); };

    g.setFont (juce::FontOptions (10.0f));
    for (double decade = std::pow (10.0, std::floor (loLog)); decade <= std::pow (10.0, std::ceil (hiLog)); decade *= 10.0)
    {
        if (decade < loHz)
            continue;

        const float px = x (decade);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawVerticalLine ((int) px, area.getY(), area.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        const juce::String label = decade >= 1000.0 ? juce::String (decade / 1000.0, 0) + " kHz" : juce::String (decade, 0) + " Hz";
        const int labelX = juce::jmax ((int) area.getX(), (int) px - 30);
        g.drawText (label, labelX, (int) area.getBottom() - 12, 60, 12, juce::Justification::centred);
    }
    for (float db = dbLo; db <= dbHi; db += 20.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (db == 0.0f ? 0.3f : 0.1f));
        g.drawHorizontalLine ((int) yOf (db), area.getX(), area.getRight());
        if (db > dbLo)
        {
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText (juce::String ((int) db), (int) area.getX() + 2, (int) yOf (db) - 11, 40, 12, juce::Justification::left);
        }
    }

    if (spectrum.size() > 2)
    {
        juce::Path path;
        bool started = false;
        for (size_t k = 1; k < spectrum.size(); ++k)
        {
            const double hz = (double) k * binHzReal;
            const float px = x (hz), py = yOf (spectrum[k]);
            if (! started) { path.startNewSubPath (px, py); started = true; }
            else path.lineTo (px, py);
        }
        g.setColour (juce::Colour (0xff35c8ff));
        g.strokePath (path, juce::PathStrokeType (1.4f));

        const auto& m = analyzer.getMetrics();
        if (m.toneAboveNoise)
        {
            const float px = x (m.toneHzSim * 1.0e6);
            g.setColour (juce::Colours::hotpink.withAlpha (0.8f));
            g.drawVerticalLine ((int) px, area.getY(), area.getBottom());
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("Jitter / phase-noise spectral density (dB, log frequency)", area.reduced (6.0f).withHeight (16.0f), juce::Justification::topLeft);
    g.drawRect (area, 1.0f);
}
