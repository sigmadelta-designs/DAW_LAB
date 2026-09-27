#include "ReceiverEditor.h"

ReceiverEditor::SliderRow& ReceiverEditor::addSlider (const char* parameterId, const juce::String& text,
                                                     ControlTip::Domain domain, const juce::String& reason, bool vertical)
{
    auto row = std::make_unique<SliderRow>();
    row->slider.setSliderStyle (vertical ? juce::Slider::LinearVertical : juce::Slider::LinearHorizontal);
    row->slider.setTextBoxStyle (vertical ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight, false, vertical ? 52 : 60, 20);
    row->label.setText (text, juce::dontSendNotification);
    row->label.setJustificationType (vertical ? juce::Justification::centred : juce::Justification::centredRight);
    row->label.setFont (juce::FontOptions (12.0f));
    row->slider.setTooltip (ControlTip::make (text, domain, reason));
    row->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (receiver.getParameterTree(), parameterId, row->slider);
    addAndMakeVisible (row->slider);
    addAndMakeVisible (row->label);
    byId[parameterId] = row.get();
    rows.push_back (std::move (row));
    return *rows.back();
}

ReceiverEditor::ReceiverEditor (SerdesReceiver& receiverToEdit)
    : juce::AudioProcessorEditor (receiverToEdit), receiver (receiverToEdit)
{
    setLookAndFeel (&lookAndFeel);

    using D = ControlTip::Domain;
    addSlider ("target", "Target level", D::dut,
        "The signal level the receiver's automatic gain control aims for at its input - a real AGC target register.");
    addSlider ("ctleboost", "CTLE boost dB", D::dut,
        "High-frequency boost the continuous-time linear equalizer applies - a real receiver's CTLE gain register.");
    addSlider ("ctlezero", "Zero GHz", D::dut,
        "Where the CTLE's boost starts rising - a real CTLE's zero-frequency setting.");
    addSlider ("ctlepole2", "High pole GHz", D::dut,
        "Where the CTLE's response rolls back off above the boost - a real CTLE's second-pole setting.");
    addSlider ("ctledc", "DC gain dB", D::dut,
        "Overall gain the CTLE applies at DC, before its high-frequency boost - a real CTLE's DC gain register.");
    addSlider ("dfestep", "DFE step", D::dut,
        "How big a correction the DFE's sign-sign LMS loop makes per symbol - a real adaptive DFE's loop-gain register. "
        "Larger settles faster but tracks noisier; smaller is steadier but slower to adapt.");
    for (int k = 1; k <= SerdesReceiver::dfeTaps; ++k)
        addSlider (("dfe" + juce::String (k)).toRawUTF8(), "h" + juce::String (k), D::dut,
            "A decision-feedback equalizer tap - cancels this much of the residual ISI from " + juce::String (k)
                + " symbol" + (k == 1 ? "" : "s") + " ago. A real DFE's tap register; when auto-adapt is on, the loop sets it for you.", true);
    addSlider ("cdrkp", "Kp (mUI)", D::dut,
        "The clock-recovery loop's proportional step - how hard it corrects clock phase per bang-bang decision. A real CDR's loop-filter register.");
    addSlider ("cdrki", "Ki (uUI)", D::dut,
        "The clock-recovery loop's integral step - how fast it corrects a steady frequency offset. A real CDR's loop-filter register.");
    addSlider ("clockppm", "Ref. offset ppm", ControlTip::Domain::simulation,
        "Mistunes the transmitter's clock away from the receiver's nominal rate, to see how much frequency offset this "
        "simulated CDR can pull in and track. A stress condition you dial in to characterise the receiver, not a setting on it.");

    for (auto* b : { &enabledButton, &ctleAdaptButton, &dfeAdaptButton, &trainerButton })
        addAndMakeVisible (b);
    addAndMakeVisible (outputBox);
    addAndMakeVisible (outputLabel);
    addAndMakeVisible (resetButton);

    outputBox.addItemList ({ "CTLE output (analog)", "Slicer input (after DFE)", "Recovered data (retimed)" }, 1);
    auto& tree = receiver.getParameterTree();
    enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (tree, "enabled", enabledButton);
    ctleAdaptAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (tree, "ctleadapt", ctleAdaptButton);
    dfeAdaptAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (tree, "dfeadapt", dfeAdaptButton);
    trainerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (tree, "feedtrainer", trainerButton);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (tree, "output", outputBox);
    resetButton.onClick = [this] { compare.reset(); };

    enabledButton.setTooltip (ControlTip::make ("Receiver enabled", ControlTip::Domain::dut,
        "Puts the physical receiver chip in or out of the signal path - like swapping in a direct-attach loopback "
        "instead of the real receiver, a real bring-up step."));
    ctleAdaptButton.setTooltip (ControlTip::make ("Auto-adapt boost", ControlTip::Domain::dut,
        "Has the CTLE tune its own boost against the slicer's decision error, the way many real receiver ICs "
        "auto-calibrate their equalizer on link-up instead of using a fixed register value."));
    dfeAdaptButton.setTooltip (ControlTip::make ("Auto-adapt DFE taps", ControlTip::Domain::dut,
        "Has the DFE's sign-sign LMS loop set its own taps, the way a real adaptive DFE does, instead of using "
        "the h1..h5 sliders' fixed values."));
    outputBox.setTooltip (ControlTip::make ("Output", ControlTip::Domain::dut,
        "Chooses which internal node the receiver hands to the next stage - many real receiver ICs expose exactly "
        "this kind of debug/eye-monitor tap mux (pre-CTLE, post-CTLE, or fully retimed data)."));
    trainerButton.setTooltip (ControlTip::make ("Feed the FFE trainer", ControlTip::Domain::simulation,
        "Routes this receiver's slicer error into the upstream TX FFE's auto-adapt search - a wiring choice for "
        "this measurement setup, not a signal-path property."));
    resetButton.setTooltip ("Zeroes the bit-error counters below and restarts the sent/received alignment search.");

    setSize (1010, 840);
    startTimerHz (20);
}

ReceiverEditor::~ReceiverEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ReceiverEditor::timerCallback()
{
    auto& link = receiver.getLink();
    compare.update (link.txBits, link.rxBits, (++ticks % 8) == 0);   // the alignment search is the expensive part
    repaint();
}

void ReceiverEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (28);
    enabledButton.setBounds (top.removeFromLeft (150));
    outputLabel.setBounds (top.removeFromLeft (52));
    outputBox.setBounds (top.removeFromLeft (210));
    top.removeFromLeft (16);
    byId["target"]->label.setBounds (top.removeFromLeft (84));
    byId["target"]->slider.setBounds (top.removeFromLeft (230));
    trainerButton.setBounds (top.removeFromRight (190));

    area.removeFromTop (8);
    auto upper = area.removeFromTop (470);
    auto left = upper.removeFromLeft (490);
    upper.removeFromLeft (12);
    auto right = upper;

    const auto place = [] (juce::Rectangle<int>& column, SliderRow& row)
    {
        auto line = column.removeFromTop (26);
        row.label.setBounds (line.removeFromLeft (110));
        row.slider.setBounds (line);
    };

    // CTLE
    ctleTitleArea = left.removeFromTop (22);
    for (auto id : { "ctleboost", "ctlezero", "ctlepole2", "ctledc" })
        place (left, *byId[id]);
    ctleAdaptButton.setBounds (left.removeFromTop (24));
    left.removeFromTop (6);
    ctleArea = left.removeFromTop (150);
    left.removeFromTop (10);

    // CDR
    cdrTitleArea = left.removeFromTop (22);
    for (auto id : { "cdrkp", "cdrki", "clockppm" })
        place (left, *byId[id]);
    cdrTextArea = left;

    // DFE
    dfeArea = right.removeFromTop (22);
    dfeAdaptButton.setBounds (right.removeFromTop (24));
    place (right, *byId["dfestep"]);
    right.removeFromTop (6);
    auto taps = right.removeFromTop (190);
    const int columnWidth = taps.getWidth() / SerdesReceiver::dfeTaps;
    for (int k = 1; k <= SerdesReceiver::dfeTaps; ++k)
    {
        auto column = taps.removeFromLeft (columnWidth);
        auto& row = *byId[("dfe" + juce::String (k)).toRawUTF8()];
        row.label.setBounds (column.removeFromTop (18));
        row.slider.setBounds (column);
    }
    right.removeFromTop (6);
    dfeNoteArea = right.removeFromTop (70);

    area.removeFromTop (6);
    compareArea = area;
    resetButton.setBounds (compareArea.removeFromTop (26).removeFromRight (120));
}

void ReceiverEditor::drawCtle (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (juce::Colour (0xff101418));
    g.fillRect (area);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRect (area, 1);

    const double sampleRate = receiver.getSampleRateForDisplay();
    auto plot = area.reduced (34, 14).withTrimmedBottom (8);
    // show up to about 3x the link's Nyquist frequency; the digital filter has a null at the sample-rate Nyquist that is not of interest
    const double maxAudioHz = juce::jmin (0.4 * sampleRate, 3.2 * 0.5 * receiver.getLink().baudHz());
    std::vector<double> response;
    double lo = -6.0, hi = 6.0;
    for (int i = 0; i <= 160; ++i)
    {
        const double v = receiver.getCtleResponseDb (maxAudioHz * i / 160.0 + 1.0);
        response.push_back (v);
        lo = juce::jmin (lo, v);
        hi = juce::jmax (hi, v);
    }
    lo = juce::jmax (-30.0, std::floor ((lo - 1.0) / 6.0) * 6.0);
    hi = std::ceil ((hi + 1.0) / 6.0) * 6.0;

    const auto x = [&] (double f) { return (float) plot.getX() + (float) (f / maxAudioHz * plot.getWidth()); };
    const auto y = [&] (double db) { return (float) plot.getBottom() - (float) ((db - lo) / (hi - lo) * plot.getHeight()); };

    g.setFont (juce::FontOptions (10.0f));
    for (double db = lo; db <= hi + 1.0e-9; db += 6.0)
    {
        g.setColour (juce::Colours::white.withAlpha (db == 0.0 ? 0.35f : 0.10f));
        g.drawHorizontalLine ((int) y (db), (float) plot.getX(), (float) plot.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String (juce::roundToInt (db)), area.getX() + 1, (int) y (db) - 6, 30, 12, juce::Justification::right);
    }

    juce::Path path;
    for (size_t i = 0; i < response.size(); ++i)
    {
        const auto p = juce::Point<float> (x (maxAudioHz * (double) i / 160.0), juce::jlimit ((float) plot.getY(), (float) plot.getBottom(), y (response[i])));
        i == 0 ? path.startNewSubPath (p) : path.lineTo (p);
    }
    g.setColour (juce::Colour (0xffffa630));
    g.strokePath (path, juce::PathStrokeType (1.8f));

    const double nyquist = 0.5 * receiver.getLink().baudHz();
    g.setColour (juce::Colours::lightgreen);
    g.drawVerticalLine ((int) x (nyquist), (float) plot.getY(), (float) plot.getBottom());
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawText ("CTLE gain (dB) vs GHz   Nyquist " + juce::String (nyquist * 1.0e-3, 1) + " GHz: "
                    + juce::String (receiver.getCtleResponseDb (nyquist), 1) + " dB   (peaking "
                    + juce::String (receiver.getCtleResponseDb (nyquist) - receiver.getCtleResponseDb (1.0), 1) + " dB)",
                area.getX() + 6, area.getY() + 1, area.getWidth() - 8, 12, juce::Justification::left);
    for (double ghz = 0.0; ghz * 1.0e3 <= maxAudioHz; ghz += 5.0)
        g.drawText (juce::String ((int) ghz), (int) x (ghz * 1.0e3) - 10, plot.getBottom() - 1, 20, 10, juce::Justification::centred);
}

void ReceiverEditor::drawCompare (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& stats = compare.getStats();

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Sequence comparison: bits sent by the transmitter vs bits recovered by the slicer", area.withHeight (22), juce::Justification::centredLeft);

    auto text = area.withTrimmedTop (24);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (stats.synced ? juce::Colours::lightgreen : juce::Colours::orange);
    g.drawText (stats.synced ? "ALIGNED   received bit r = sent bit r + " + juce::String ((juce::int64) stats.offset)
                             : juce::String ("SEARCHING for the alignment (needs a running transmitter and a locked receiver)"),
                text.removeFromTop (20), juce::Justification::centredLeft);

    g.setColour (juce::Colours::white);
    const juce::String ber = stats.compared == 0 ? juce::String ("--")
                           : (stats.errors == 0 ? "< " + juce::String (compare.berUpperBound95(), 2, true) + " (95% confidence)"
                                                : juce::String (compare.ber(), 2, true));
    g.drawText ("Compared " + juce::String ((juce::int64) stats.compared) + " bits    errors " + juce::String ((juce::int64) stats.errors)
                    + "    BER " + ber + "    since last error " + juce::String ((juce::int64) stats.sinceLastError) + " bits",
                text.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (juce::Colours::lightgrey);
    g.drawText ("Error bursts " + juce::String ((juce::int64) stats.bursts) + " (longest " + juce::String ((juce::int64) stats.longestBurst)
                    + ")    alignment lost " + juce::String (stats.resyncs) + " times", text.removeFromTop (18), juce::Justification::centredLeft);

    std::vector<uint8_t> sent, received;
    uint64_t first = 0;
    compare.recent (128, sent, received, first);
    if (sent.empty())
        return;

    text.removeFromTop (6);
    const int cell = juce::jmin (7, (text.getWidth() - 70) / 128);
    const int x0 = text.getX() + 66;
    const int rowHeight = 22;
    const auto labelRow = [&] (int y, const juce::String& name) { g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (12.0f, juce::Font::bold)); g.drawText (name, text.getX(), y, 64, rowHeight, juce::Justification::centredLeft); };

    const int yTx = text.getY(), yMark = yTx + rowHeight, yRx = yMark + 10;
    labelRow (yTx, "TX sent");
    labelRow (yRx, "RX got");

    for (size_t i = 0; i < sent.size(); ++i)
    {
        const bool wrong = sent[i] != received[i];
        const int x = x0 + (int) i * cell;
        g.setColour (sent[i] ? juce::Colour (0xff35c8ff) : juce::Colour (0xff1f3a48));
        g.fillRect (x, yTx + 2, cell - 1, rowHeight - 6);
        g.setColour (received[i] ? (wrong ? juce::Colours::red : juce::Colour (0xff35c8ff)) : (wrong ? juce::Colour (0xff7a1c1c) : juce::Colour (0xff1f3a48)));
        g.fillRect (x, yRx + 2, cell - 1, rowHeight - 6);
        if (wrong)
        {
            g.setColour (juce::Colours::red);
            g.fillRect (x, yMark, cell - 1, 8);
        }
    }

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("received bits " + juce::String ((juce::int64) first) + " .. " + juce::String ((juce::int64) (first + sent.size() - 1))
                    + "   (bright = 1, dark = 0, red = disagreement)",
                x0, yRx + rowHeight, 500, 14, juce::Justification::left);
}

void ReceiverEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("CTLE  (zero + two poles)", ctleTitleArea, juce::Justification::left);
    drawCtle (g, ctleArea);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("CDR  (Alexander bang-bang, PI loop)", cdrTitleArea, juce::Justification::left);
    g.drawText ("DFE  (5 post-cursor taps)", dfeArea, juce::Justification::left);

    const auto& s = receiver.status;
    auto text = cdrTextArea;
    text.removeFromTop (0);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (s.locked.load() ? juce::Colours::lightgreen : juce::Colours::orange);
    g.drawText (s.locked.load() ? "CDR LOCKED" : "CDR searching...", text.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.drawText ("Learned frequency offset " + juce::String (s.frequencyPpm.load(), 0) + " ppm      edge level " + juce::String (s.edgeLevel.load(), 2) + " (small = on the edges)",
                text.removeFromTop (18), juce::Justification::centredLeft);
    g.drawText ("Slicer error " + juce::String (LinkSettings::nmseToDb (s.nmse.load()), 1) + " dB (NMSE)      AGC gain " + juce::String (s.agcGain.load(), 2)
                    + "      symbols " + juce::String ((juce::int64) s.symbols.load()),
                text.removeFromTop (18), juce::Justification::centredLeft);
    if (receiver.status.ctleState.load() != 0)
    {
        static const char* names[] = { "", "starting", "adapting", "converged" };
        g.setColour (juce::Colours::skyblue);
        g.drawText (juce::String ("CTLE boost training: ") + names[receiver.status.ctleState.load()], text.removeFromTop (18), juce::Justification::centredLeft);
    }

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText ("Taps are in units of the target level: feedback = T x (h1 d[n-1] + h2 d[n-2] + ...). Adaptation is sign-sign LMS on the slicer "
                      "error; the adapted values are written back to the sliders so they save with the preset. The Output menu picks what the next stage sees.",
                      dfeNoteArea, juce::Justification::topLeft, 4);

    drawCompare (g, compareArea.withTrimmedTop (0));
}
