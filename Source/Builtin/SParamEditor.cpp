#include "SParamEditor.h"

namespace
{
    const juce::Colour differentialColour (0xffffa630), commonColour (0xff35c8ff), conversionColour (0xffe040c8);
}

SParamEditor::SParamEditor (SParamChannel& channelToEdit)
    : juce::AudioProcessorEditor (channelToEdit), channel (channelToEdit)
{
    for (auto* c : std::initializer_list<juce::Component*> { &loadButton, &clearButton, &portsBox, &tapsBox, &portsLabel, &tapsLabel, &enabledButton })
        addAndMakeVisible (c);

    portsBox.addItemList ({ "P1,P2 in -> P3,P4 out", "P1,P3 in -> P2,P4 out" }, 1);
    tapsBox.addItemList ({ "512 taps", "1024 taps", "2048 taps" }, 1);
    portsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (channel.getParameterTree(), "ports", portsBox);
    tapsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (channel.getParameterTree(), "taps", tapsBox);
    enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (channel.getParameterTree(), "enabled", enabledButton);

    loadButton.onClick = [this] { chooseFile(); };
    clearButton.onClick = [this] { channel.clearData(); message = {}; };

    channel.changed.addChangeListener (this);
    setSize (920, 700);
    startTimerHz (4);
}

SParamEditor::~SParamEditor()
{
    stopTimer();
    channel.changed.removeChangeListener (this);
}

void SParamEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load an S-parameter file", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.s2p;*.s4p;*.s6p;*.s8p;*.s3p;*.s1p");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<SParamEditor> (this)] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr || fc.getResult() == juce::File())
                return;

            juce::String error;
            safeThis->message = safeThis->channel.loadFile (fc.getResult(), error) ? juce::String() : error;
            safeThis->repaint();
        });
}

void SParamEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (28);
    loadButton.setBounds (top.removeFromLeft (150));
    clearButton.setBounds (top.removeFromLeft (60));
    top.removeFromLeft (16);
    portsLabel.setBounds (top.removeFromLeft (44));
    portsBox.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (10);
    tapsLabel.setBounds (top.removeFromLeft (44));
    tapsBox.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (10);
    enabledButton.setBounds (top.removeFromLeft (90));

    area.removeFromTop (8);
    responseArea = area.removeFromTop (290);
    area.removeFromTop (8);
    stepArea = area.removeFromTop (190);
    area.removeFromTop (8);
    textArea = area;
}

void SParamEditor::drawResponse (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff101418));
    g.fillRect (area);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRect (area, 1);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Insertion loss and mode conversion vs frequency", area.withHeight (20).translated (8, 2), juce::Justification::centredLeft);

    if (! channel.hasData())
        return;

    const auto& data = channel.getData();
    const auto map = channel.getPortMap();
    auto plot = area.reduced (46, 26).withTrimmedBottom (6);

    const double maxHz = data.frequencyHz.back();
    const double topDb = 0.0, bottomDb = -60.0;
    const auto x = [&] (double hz) { return (float) plot.getX() + (float) (hz / maxHz * plot.getWidth()); };
    const auto y = [&] (double db) { return (float) plot.getY() + (float) ((topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb) * plot.getHeight()); };

    g.setFont (juce::FontOptions (11.0f));
    for (double db = bottomDb; db <= topDb + 1.0e-9; db += 10.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawHorizontalLine ((int) y (db), (float) plot.getX(), (float) plot.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String (juce::roundToInt (db)), area.getX() + 2, (int) y (db) - 6, 40, 12, juce::Justification::right);
    }

    const double stepGHz = maxHz >= 20.0e9 ? 5.0e9 : (maxHz >= 8.0e9 ? 2.0e9 : 1.0e9);
    for (double f = 0.0; f <= maxHz + 1.0; f += stepGHz)
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawVerticalLine ((int) x (f), (float) plot.getY(), (float) plot.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String (f * 1.0e-9, 0) + " GHz", (int) x (f) - 24, plot.getBottom() + 2, 48, 12, juce::Justification::centred);
    }

    const size_t stride = juce::jmax<size_t> (1, data.numFrequencies() / 700);
    const auto curve = [&] (juce::Colour colour, const std::function<TouchstoneData::Complex (double)>& value)
    {
        juce::Path path;
        bool first = true;
        for (size_t i = 0; i < data.numFrequencies(); i += stride)
        {
            const double hz = data.frequencyHz[i];
            const auto p = juce::Point<float> (x (hz), y (20.0 * std::log10 (juce::jmax (1.0e-6, std::abs (value (hz))))));
            first ? path.startNewSubPath (p) : path.lineTo (p);
            first = false;
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.6f));
    };

    const auto t = [&] (int i, int j, double hz) { return SParamSynthesis::transfer (data, map, i, j, hz); };

    if (data.ports == 2)
        curve (differentialColour, [&] (double hz) { return t (0, 0, hz); });
    else
    {
        curve (differentialColour, [&] (double hz) { return 0.5 * (t (0, 0, hz) - t (0, 1, hz) - t (1, 0, hz) + t (1, 1, hz)); });   // Sdd21
        curve (commonColour,       [&] (double hz) { return 0.5 * (t (0, 0, hz) + t (0, 1, hz) + t (1, 0, hz) + t (1, 1, hz)); });   // Scc21
        curve (conversionColour,   [&] (double hz) { return 0.5 * (t (0, 0, hz) - t (0, 1, hz) + t (1, 0, hz) - t (1, 1, hz)); });   // Sdc21
    }

    // the link's Nyquist frequency
    const double nyquist = 0.5 * channel.getLink().baudHz() * 1.0e6;
    if (nyquist < maxHz)
    {
        const float dashes[] = { 4.0f, 4.0f };
        juce::Path line;
        line.startNewSubPath (x (nyquist), (float) plot.getY());
        line.lineTo (x (nyquist), (float) plot.getBottom());
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (juce::Colours::lightgreen);
        g.fillPath (dashed);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Nyquist " + juce::String (nyquist * 1.0e-9, 1) + " GHz", (int) x (nyquist) + 4, plot.getY() + 2, 120, 12, juce::Justification::left);
    }

    // legend
    g.setFont (juce::FontOptions (12.0f));
    int lx = area.getRight() - 330;
    const auto legend = [&] (juce::Colour c, const juce::String& text)
    {
        g.setColour (c);
        g.fillRect (lx, area.getY() + 8, 14, 3);
        g.setColour (juce::Colours::white);
        g.drawText (text, lx + 18, area.getY() + 2, 96, 16, juce::Justification::left);
        lx += 106;
    };
    if (data.ports == 2)
        legend (differentialColour, "S21");
    else
    {
        legend (differentialColour, "Sdd21 (odd)");
        legend (commonColour, "Scc21 (even)");
        legend (conversionColour, "Sdc21 (conv.)");
    }
}

void SParamEditor::drawStep (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff101418));
    g.fillRect (area);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRect (area, 1);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Step response of the filters actually running", area.withHeight (20).translated (8, 2), juce::Justification::centredLeft);

    const auto& firs = channel.getFirs();
    if (! channel.hasData() || firs.taps == 0 || ! firs.present[0][0])
        return;

    const auto tap = [&] (int i, int j, size_t n) { return firs.present[i][j] ? firs.h[i][j][n] : 0.0f; };

    std::vector<double> differential ((size_t) firs.taps), common ((size_t) firs.taps);
    double runningD = 0.0, runningC = 0.0;
    for (size_t n = 0; n < (size_t) firs.taps; ++n)
    {
        runningD += 0.5 * (tap (0, 0, n) - tap (0, 1, n) - tap (1, 0, n) + tap (1, 1, n));
        runningC += 0.5 * (tap (0, 0, n) + tap (0, 1, n) + tap (1, 0, n) + tap (1, 1, n));
        differential[n] = runningD;
        common[n] = runningC;
    }

    const int shown = juce::jmin (firs.taps, (int) (firs.delaySamples + SParamSynthesis::designMargin) + 220);
    auto plot = area.reduced (46, 24).withTrimmedBottom (6);
    const double sampleRate = channel.getSampleRate();
    const auto x = [&] (double n) { return (float) plot.getX() + (float) (n / shown * plot.getWidth()); };
    const auto y = [&] (double v) { return (float) plot.getBottom() - (float) ((juce::jlimit (-0.4, 1.4, v) + 0.4) / 1.8 * plot.getHeight()); };

    g.setFont (juce::FontOptions (11.0f));
    for (double v : { 0.0, 0.5, 1.0 })
    {
        g.setColour (juce::Colours::white.withAlpha (v == 0.0 || v == 1.0 ? 0.25f : 0.10f));
        g.drawHorizontalLine ((int) y (v), (float) plot.getX(), (float) plot.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String (v, 1), area.getX() + 2, (int) y (v) - 6, 40, 12, juce::Justification::right);
    }

    const double nsPerSample = 1.0e3 / sampleRate;   // real-world nanoseconds per audio sample
    for (double ns = 0.0; ns / nsPerSample <= shown; ns += 1.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawVerticalLine ((int) x (ns / nsPerSample), (float) plot.getY(), (float) plot.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String ((int) ns) + " ns", (int) x (ns / nsPerSample) - 20, plot.getBottom() + 1, 40, 12, juce::Justification::centred);
    }

    const auto draw = [&] (const std::vector<double>& v, juce::Colour c)
    {
        juce::Path path;
        for (int n = 0; n < shown; ++n)
        {
            const auto p = juce::Point<float> (x (n), y (v[(size_t) n]));
            n == 0 ? path.startNewSubPath (p) : path.lineTo (p);
        }
        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (1.6f));
    };

    if (channel.getData().ports > 2)
        draw (common, commonColour);
    draw (differential, differentialColour);
}

void SParamEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));
    drawResponse (g, responseArea);
    drawStep (g, stepArea);

    auto text = textArea;
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);

    if (! channel.hasData())
    {
        g.drawText ("No channel loaded.", text.removeFromTop (22), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Load a Touchstone file: .s2p (one trace, applied to both legs) or .s4p and up (two coupled traces, with crosstalk and skew).",
                    text.removeFromTop (20), juce::Justification::centredLeft);
        g.drawText ("Frequencies in the file are real-world; here 1 GHz plays as 1 kHz. The file is stored in the chain preset.",
                    text.removeFromTop (20), juce::Justification::centredLeft);
    }
    else
    {
        const auto s = channel.getSummary();
        g.drawText (channel.getFileName() + "   " + juce::String (s.ports) + "-port, " + juce::String (s.points) + " points, "
                        + juce::String (s.firstHz * 1.0e-9, 2) + " - " + juce::String (s.lastHz * 1.0e-9, 1) + " GHz",
                    text.removeFromTop (22), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::white);
        g.drawText ("At the link's Nyquist frequency (" + juce::String (s.nyquistHz * 1.0e-9, 1) + " GHz):", text.removeFromTop (20), juce::Justification::centredLeft);

        if (s.ports > 2)
        {
            g.drawText ("Differential (odd) loss " + juce::String (-s.sdd21Db, 1) + " dB      common (even) loss " + juce::String (-s.scc21Db, 1)
                            + " dB      differential-to-common conversion " + juce::String (s.sdc21Db, 1) + " dB",
                        text.removeFromTop (20), juce::Justification::centredLeft);
            g.drawText ("Delay + " + juce::String (juce::roundToInt (s.delayPlusPs)) + " ps   - " + juce::String (juce::roundToInt (s.delayMinusPs))
                            + " ps      skew " + juce::String (s.delayPlusPs - s.delayMinusPs, 1) + " ps ("
                            + juce::String (std::abs (s.delayPlusPs - s.delayMinusPs) / (1000.0 / juce::jmax (0.01, (double) channel.getLink().lineRateGBd.load())), 2) + " UI)",
                        text.removeFromTop (20), juce::Justification::centredLeft);
        }
        else
            g.drawText ("Insertion loss " + juce::String (-s.sdd21Db, 1) + " dB      delay " + juce::String (juce::roundToInt (s.delayPlusPs)) + " ps",
                        text.removeFromTop (20), juce::Justification::centredLeft);

        if (s.warning.isNotEmpty())
        {
            g.setColour (juce::Colours::orange);
            g.drawText (s.warning, text.removeFromTop (20), juce::Justification::centredLeft);
        }
        else if (s.nyquistHz > s.lastHz)
        {
            g.setColour (juce::Colours::orange);
            g.drawText ("The file stops at " + juce::String (s.lastHz * 1.0e-9, 1) + " GHz, below the link's Nyquist frequency: the response is tapered to zero above it.",
                        text.removeFromTop (20), juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("The file is the channel between matched 50 ohm ports: transmitter and receiver reflections are not included.",
                    text.removeFromTop (18), juce::Justification::centredLeft);
    }

    if (message.isNotEmpty())
    {
        g.setColour (juce::Colours::hotpink);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (message, text.removeFromTop (22), juce::Justification::centredLeft);
    }
}
