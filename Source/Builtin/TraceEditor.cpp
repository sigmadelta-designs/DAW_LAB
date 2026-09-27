#include "TraceEditor.h"

namespace
{
    const juce::Colour traceColour[2] = { juce::Colour (0xffffa630), juce::Colour (0xff35c8ff) };
    const char* const padName[2] = { "+", "-" };
}

// ---------------------------------------------------------------------------------
class TraceEditor::BoardView : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    BoardView (TraceEditor& ownerEditor) : owner (ownerEditor)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }

    static constexpr float boardWidth = (float) TraceBoard::width, boardHeight = (float) TraceBoard::height;

    float cellSize() const { return juce::jmin ((float) getWidth() / boardWidth, (float) getHeight() / boardHeight); }
    juce::Point<float> toScreen (double x, double y) const { return { (float) x * cellSize(), (float) y * cellSize() }; }
    juce::Point<double> toCells (juce::Point<float> p) const { return { p.x / cellSize(), p.y / cellSize() }; }

    void paint (juce::Graphics& g) override
    {
        const auto& board = owner.channel.getBoard();
        const float cell = cellSize();
        const auto boardBounds = juce::Rectangle<float> (0, 0, cell * boardWidth, cell * boardHeight);

        g.setColour (juce::Colour (0xff0d3b2c));
        g.fillRect (boardBounds);

        // grid: a dot every 0.1", a line every inch
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        for (int x = 0; x <= TraceBoard::width; x += 2)
            for (int y = 0; y <= TraceBoard::height; y += 2)
                g.fillRect ((float) x * cell - 0.5f, (float) y * cell - 0.5f, 1.0f, 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.setFont (juce::FontOptions (10.0f));
        for (int inch = 0; inch <= 8; ++inch)
        {
            g.drawVerticalLine ((int) ((float) inch * 20.0f * cell), 0.0f, boardHeight * cell);
            g.drawText (juce::String (inch) + "\"", (int) ((float) inch * 20.0f * cell) + 2, (int) (boardHeight * cell) - 12, 24, 12, juce::Justification::left);
        }
        for (int inch = 0; inch <= 5; ++inch)
            g.drawHorizontalLine ((int) ((float) inch * 20.0f * cell), 0.0f, boardWidth * cell);

        // keep-outs
        for (const auto& o : board.obstacles)
        {
            const auto r = juce::Rectangle<float> ((float) o.x * cell, (float) o.y * cell, (float) o.w * cell, (float) o.h * cell);
            g.setColour (juce::Colour (0xff3a3f45));
            g.fillRoundedRectangle (r, 3.0f);

            g.saveState();
            g.reduceClipRegion (r.toNearestInt());
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            for (float d = -r.getHeight(); d < r.getWidth(); d += 9.0f)
                g.drawLine (r.getX() + d, r.getBottom(), r.getX() + d + r.getHeight(), r.getY(), 1.0f);
            g.restoreState();

            g.setColour (juce::Colour (0xffc9a227));
            g.drawRoundedRectangle (r, 3.0f, 1.5f);
        }

        // aggressor lanes, with a faint halo showing how far their influence reaches
        for (size_t k = 0; k < board.aggressors.size(); ++k)
        {
            const auto& lane = board.aggressors[k];
            juce::Path path;
            for (size_t i = 0; i < lane.points.size(); ++i)
            {
                const auto p = toScreen (lane.points[i].x, lane.points[i].y);
                i == 0 ? path.startNewSubPath (p) : path.lineTo (p);
            }

            const auto magenta = juce::Colour (0xffe040c8);
            g.setColour (magenta.withAlpha (0.07f));
            g.strokePath (path, juce::PathStrokeType (2.0f * (float) TraceBoard::couplingReachCells * cell * 0.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (magenta.withAlpha (0.10f));
            g.strokePath (path, juce::PathStrokeType (2.0f * 4.0f * cell, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path dashed;
            const float dashes[] = { 9.0f, 5.0f };
            juce::PathStrokeType (2.0f).createDashedStroke (dashed, path, dashes, 2);
            g.setColour (magenta);
            g.fillPath (dashed);

            const auto label = toScreen (lane.points.front().x, lane.points.front().y);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("AGGRESSOR " + juce::String ((int) k + 1), (int) label.x + 4, (int) label.y - 15, 90, 12, juce::Justification::left);
        }

        // where the traces couple: the closer and longer they run together, the stronger the glow
        for (const auto& run : board.couplingRuns())
        {
            if (run.weight < 0.05)
                continue;

            juce::Path band;
            band.startNewSubPath (toScreen (run.x0, run.y0));
            band.lineTo (toScreen (run.x1, run.y1));
            band.lineTo (toScreen (run.x1 + run.offsetX, run.y1 + run.offsetY));
            band.lineTo (toScreen (run.x0 + run.offsetX, run.y0 + run.offsetY));
            band.closeSubPath();
            g.setColour (juce::Colour (0xffffe14d).withAlpha ((float) run.weight * 0.35f));
            g.fillPath (band);
        }

        // traces
        for (int t = 0; t < 2; ++t)
        {
            const auto& vertices = board.getTrace (t);
            juce::Path path;
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                const auto p = toScreen (vertices[i].x, vertices[i].y);
                i == 0 ? path.startNewSubPath (p) : path.lineTo (p);
            }

            g.setColour (traceColour[t].withAlpha (board.isComplete (t) ? 1.0f : 0.85f));
            g.strokePath (path, juce::PathStrokeType (juce::jmax (2.0f, cell * 0.7f), juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

            // unfinished: dotted line on to the output pad
            if (! board.isComplete (t))
            {
                const auto from = toScreen (vertices.back().x, vertices.back().y), to = toScreen (board.outPad[t].x, board.outPad[t].y);
                const float dashes[] = { 3.0f, 5.0f };
                juce::Path guide;
                guide.startNewSubPath (from);
                guide.lineTo (to);
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, guide, dashes, 2);
                g.setColour (traceColour[t].withAlpha (0.45f));
                g.fillPath (dashed);
            }
        }

        // pads
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        for (int t = 0; t < 2; ++t)
            for (int end = 0; end < 2; ++end)
            {
                const auto p = end == 0 ? board.inPad[t] : board.outPad[t];
                const auto c = toScreen (p.x, p.y);
                const float r = cell * 1.6f;
                g.setColour (juce::Colour (0xffd9d9d9));
                g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
                g.setColour (traceColour[t]);
                g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 2.0f);
                g.setColour (juce::Colours::white);
                g.drawText ((end == 0 ? "IN" : "OUT") + juce::String (padName[t]), (int) (c.x - 22), (int) (c.y - r - 15), 44, 13, juce::Justification::centred);
            }

        // candidate next piece
        if (candidateValid.has_value() && ! candidate.empty())
        {
            const auto& vertices = board.getTrace (owner.activeTrace);
            auto from = toScreen (vertices.back().x, vertices.back().y);
            g.setColour (*candidateValid ? traceColour[owner.activeTrace].withAlpha (0.6f) : juce::Colours::red.withAlpha (0.7f));

            for (auto p : candidate)
            {
                const auto to = toScreen (p.x, p.y);
                g.drawLine (from.x, from.y, to.x, to.y, juce::jmax (2.0f, cell * 0.7f));
                from = to;
            }

            g.fillEllipse (from.x - 4.0f, from.y - 4.0f, 8.0f, 8.0f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override    { updateCandidate (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override    { updateCandidate (e.position); }
    void mouseExit (const juce::MouseEvent&) override      { candidate.clear(); candidateValid.reset(); repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto& traceChannel = owner.channel;
        auto& board = traceChannel.getBoard();

        if (e.mods.isPopupMenu())
        {
            undo();
            return;
        }

        updateCandidate (e.position);
        if (candidateValid.value_or (false) && board.append (owner.activeTrace, candidate))
        {
            owner.assisted = false;
            owner.message = {};
            traceChannel.boardEdited();
            updateCandidate (e.position);
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
        {
            undo();
            return true;
        }

        return false;
    }

    void undo()
    {
        if (owner.channel.getBoard().undo (owner.activeTrace))
        {
            owner.assisted = false;
            owner.channel.boardEdited();
        }
    }

private:
    using Polyline = TraceBoard::Polyline;

    // Turn the pointer position into the next legal piece of trace.
    void updateCandidate (juce::Point<float> position)
    {
        auto& board = owner.channel.getBoard();
        const int t = owner.activeTrace;
        candidate.clear();
        candidateValid.reset();

        if (! board.isComplete (t))
        {
            const auto cursor = toCells (position);
            const auto last = board.getTrace (t).back();
            const auto pad = board.outPad[t];

            // near the output pad: offer a route straight into it
            if (std::hypot (cursor.x - pad.x, cursor.y - pad.y) < 6.0)
            {
                const int dx = pad.x - last.x, dy = pad.y - last.y;
                const int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
                const int m = juce::jmin (std::abs (dx), std::abs (dy)), excess = std::abs (std::abs (dx) - std::abs (dy));

                std::vector<Polyline> options;
                if (TraceBoard::isLegalStep (last, pad))
                    options.push_back ({ pad });

                options.push_back ({ { last.x + sx * m, last.y + sy * m }, pad });   // diagonal first, then straight
                if (std::abs (dx) > std::abs (dy))       options.push_back ({ { last.x + sx * excess, last.y }, pad });   // straight first, then diagonal
                else if (std::abs (dy) > std::abs (dx))  options.push_back ({ { last.x, last.y + sy * excess }, pad });

                for (auto& option : options)
                {
                    Polyline cleaned;
                    for (auto p : option)
                        if (cleaned.empty() ? p != last : p != cleaned.back())
                            cleaned.push_back (p);

                    if (! cleaned.empty() && board.canAppend (t, cleaned))
                    {
                        candidate = cleaned;
                        candidateValid = true;
                        repaint();
                        return;
                    }
                }
            }

            // otherwise the nearest 45-degree extension from the last corner
            double bestError = 1.0e9;
            TraceBoard::Point bestPoint = last;
            for (int d = 0; d < 8; ++d)
            {
                static const int dxs[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, dys[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
                const double along = ((cursor.x - last.x) * dxs[d] + (cursor.y - last.y) * dys[d]) / (double) (dxs[d] * dxs[d] + dys[d] * dys[d]);
                const int steps = juce::jmax (1, (int) std::lround (along));
                const TraceBoard::Point end { last.x + dxs[d] * steps, last.y + dys[d] * steps };
                const double error = std::hypot (cursor.x - end.x, cursor.y - end.y);
                if (error < bestError) { bestError = error; bestPoint = end; }
            }

            candidate = { bestPoint };
            candidateValid = board.canAppend (t, candidate);
        }

        repaint();
    }

    TraceEditor& owner;
    Polyline candidate;
    std::optional<bool> candidateValid;
};

// ---------------------------------------------------------------------------------
TraceEditor::TraceEditor (TraceChannel& channelToEdit)
    : juce::AudioProcessorEditor (channelToEdit), channel (channelToEdit)
{
    setLookAndFeel (&lookAndFeel);

    boardView = std::make_unique<BoardView> (*this);
    boardView->setTooltip ("Click to place trace corners (45/90 degree routing, snapped to the grid); click near the "
                            "output pad to finish. Right-click or Backspace undoes a corner. This draws the physical "
                            "board - the real thing being tested, not a setting of this simulation.");
    addAndMakeVisible (*boardView);

    for (auto* b : { &sandboxButton, &gameButton, &newGameButton, &solveButton, &undoButton, &clearPlusButton,
                     &clearMinusButton, &clearBothButton, &drawPlusButton, &drawMinusButton })
        addAndMakeVisible (b);
    addAndMakeVisible (difficultyBox);
    addAndMakeVisible (difficultyLabel);
    addAndMakeVisible (velocitySlider);
    addAndMakeVisible (lossSlider);
    addAndMakeVisible (velocityLabel);
    addAndMakeVisible (lossLabel);
    addAndMakeVisible (couplingSlider);
    addAndMakeVisible (couplingLabel);
    addAndMakeVisible (aggressorSlider);
    addAndMakeVisible (aggressorLabel);
    addAndMakeVisible (enabledButton);
    for (auto* c : std::initializer_list<juce::Component*> { &reflectionSlider, &connectorSlider, &sourceSlider, &loadSlider,
                                                             &reflectionLabel, &connectorLabel, &sourceLabel, &loadLabel,
                                                             &readoutsTab, &settingsTab, &tdrButton })
        addAndMakeVisible (c);

    using D = ControlTip::Domain;
    sandboxButton.setTooltip ("Free-form drawing: no obstacles, no scoring - just the two traces and what they do to the signal.");
    gameButton.setTooltip ("A generated practice board: route around obstacles and noisy aggressor lanes, then check your score.");
    difficultyBox.setTooltip ("How large and cluttered the next generated practice board is. A setting of this training exercise, not the board itself.");
    newGameButton.setTooltip ("Generates a new practice board at the chosen difficulty.");
    solveButton.setTooltip ("Runs this tool's own router (A* with a lane penalty, then length matching) and draws both traces for you.");
    undoButton.setTooltip ("Removes the last corner placed on the trace you're currently drawing.");
    drawPlusButton.setTooltip ("Clicks on the board add corners to the + trace.");
    drawMinusButton.setTooltip ("Clicks on the board add corners to the - trace.");
    clearPlusButton.setTooltip ("Erases the + trace so you can redraw it.");
    clearMinusButton.setTooltip ("Erases the - trace so you can redraw it.");
    clearBothButton.setTooltip ("Erases both traces so you can redraw them.");
    readoutsTab.setTooltip ("Shows the measurements this board implies: loss, skew, crosstalk, mode conversion, return loss.");
    settingsTab.setTooltip ("Shows the sliders below that control how strongly length, coupling and mismatches affect the signal.");
    tdrButton.setTooltip ("Switches the readouts page to a simulated time-domain-reflectometry trace of the impedance along each leg - "
                          "the same measurement a real TDR instrument makes on a real board.");
    velocitySlider.setTooltip (ControlTip::make ("Propagation delay", D::dut,
        "How fast a signal travels along the trace, in picoseconds per inch - a real PCB dielectric's propagation speed. "
        "Combined with the lengths you draw, this sets each trace's delay and therefore the pair's skew."));
    lossSlider.setTooltip (ControlTip::make ("Loss vs FR4", D::dut,
        "Scales the trace material's real loss relative to a reference FR4 recipe - stand-ins for a lower-loss laminate "
        "(below 1x) or a cheaper, lossier one (above 1x)."));
    couplingSlider.setTooltip (ControlTip::make ("Coupling strength", D::dut,
        "Scales how strongly the two traces couple where they run close and parallel - a real board's spacing/dielectric "
        "property. Drives the FEXT and odd/even mode splitting you see in the readouts."));
    aggressorSlider.setTooltip (ControlTip::make ("Aggressor pickup", D::dut,
        "Scales how strongly a neighbouring noisy lane's signal couples onto this pair - the same physical mechanism as "
        "Coupling strength, applied to the board's aggressor lanes."));
    reflectionSlider.setTooltip (ControlTip::make ("Reflection strength", D::dut,
        "Scales how much the drawn geometry's impedance bumps (corners, close-run sections) actually perturb the line - "
        "a real board's sensitivity to those discontinuities."));
    connectorSlider.setTooltip (ControlTip::make ("Pad/connector mismatch", D::dut,
        "The impedance mismatch, as a percentage, at each trace's connector - a real connector or via transition's "
        "characteristic impedance error."));
    sourceSlider.setTooltip (ControlTip::make ("Source resistance", D::dut,
        "The driver's real output impedance. Away from the line's impedance, it reflects energy bouncing back from the "
        "load - a real transmitter's output termination."));
    loadSlider.setTooltip (ControlTip::make ("Load resistance", D::dut,
        "The far end's real termination resistance. Away from the line's impedance, it reflects incoming energy back "
        "toward the source - a real receiver's input termination."));
    enabledButton.setTooltip (ControlTip::make ("Channel enabled", D::dut,
        "Puts the physical board in or out of the path - like swapping in a direct-attach loopback instead of the "
        "real traces, a real bring-up step."));

    sandboxButton.setClickingTogglesState (true);
    gameButton.setClickingTogglesState (true);
    sandboxButton.setRadioGroupId (1);
    gameButton.setRadioGroupId (1);
    drawPlusButton.setClickingTogglesState (true);
    drawMinusButton.setClickingTogglesState (true);
    drawPlusButton.setRadioGroupId (2);
    drawMinusButton.setRadioGroupId (2);
    drawPlusButton.setToggleState (true, juce::dontSendNotification);
    drawPlusButton.setColour (juce::TextButton::buttonOnColourId, traceColour[0].darker (0.3f));
    drawMinusButton.setColour (juce::TextButton::buttonOnColourId, traceColour[1].darker (0.3f));

    difficultyBox.addItemList ({ "1 - relaxed", "2", "3", "4", "5 - tight" }, 1);
    difficultyBox.setSelectedId (juce::jlimit (1, 5, channel.getBoard().difficulty), juce::dontSendNotification);

    velocityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (channel.getParameterTree(), "velocity", velocitySlider);
    lossAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (channel.getParameterTree(), "lossscale", lossSlider);
    couplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (channel.getParameterTree(), "coupling", couplingSlider);
    aggressorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (channel.getParameterTree(), "aggressors", aggressorSlider);
    reflectionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (channel.getParameterTree(), "reflections", reflectionSlider);
    connectorAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (channel.getParameterTree(), "connector", connectorSlider);
    sourceAttachment     = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (channel.getParameterTree(), "sourcer", sourceSlider);
    loadAttachment       = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (channel.getParameterTree(), "loadr", loadSlider);
    for (auto* l : { &reflectionLabel, &connectorLabel, &sourceLabel, &loadLabel })
        l->setJustificationType (juce::Justification::centredRight);
    readoutsTab.setClickingTogglesState (true);
    settingsTab.setClickingTogglesState (true);
    readoutsTab.setRadioGroupId (3);
    settingsTab.setRadioGroupId (3);
    readoutsTab.setToggleState (true, juce::dontSendNotification);
    readoutsTab.onClick = [this] { if (readoutsTab.getToggleState()) showPage (false); };
    settingsTab.onClick = [this] { if (settingsTab.getToggleState()) showPage (true); };
    tdrButton.setClickingTogglesState (true);
    tdrButton.onClick = [this] { repaint(); };
    enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (channel.getParameterTree(), "enabled", enabledButton);

    sandboxButton.onClick = [this] { if (sandboxButton.getToggleState()) setSandbox(); };
    gameButton.onClick    = [this] { if (gameButton.getToggleState() && channel.getBoard().mode != TraceBoard::Mode::game) newGame(); };
    newGameButton.onClick = [this] { newGame(); };
    solveButton.onClick   = [this] { autoSolve(); };
    undoButton.onClick    = [this] { boardView->undo(); };
    drawPlusButton.onClick  = [this] { activeTrace = TraceBoard::plus; };
    drawMinusButton.onClick = [this] { activeTrace = TraceBoard::minus; };
    clearPlusButton.onClick  = [this] { channel.getBoard().clear (TraceBoard::plus);  assisted = false; channel.boardEdited(); };
    clearMinusButton.onClick = [this] { channel.getBoard().clear (TraceBoard::minus); assisted = false; channel.boardEdited(); };
    clearBothButton.onClick  = [this] { channel.getBoard().clearTraces(); assisted = false; channel.boardEdited(); };

    velocityLabel.setJustificationType (juce::Justification::centredRight);
    lossLabel.setJustificationType (juce::Justification::centredRight);
    couplingLabel.setJustificationType (juce::Justification::centredRight);
    aggressorLabel.setJustificationType (juce::Justification::centredRight);

    refreshButtons();
    channel.boardChanged.addChangeListener (this);
    showPage (false);
    setSize (1130, 740);
    startTimerHz (5);
}

TraceEditor::~TraceEditor()
{
    stopTimer();
    channel.boardChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void TraceEditor::refreshButtons()
{
    const bool game = channel.getBoard().mode == TraceBoard::Mode::game;
    sandboxButton.setToggleState (! game, juce::dontSendNotification);
    gameButton.setToggleState (game, juce::dontSendNotification);
    newGameButton.setEnabled (game);
    difficultyBox.setEnabled (true);
}

void TraceEditor::setSandbox()
{
    channel.getBoard().resetSandbox();
    assisted = false;
    message = {};
    channel.boardEdited();
    refreshButtons();
}

void TraceEditor::newGame()
{
    const int seed = juce::Random::getSystemRandom().nextInt (0x7fffffff);
    auto& board = channel.getBoard();

    if (TraceRouter::generateGame (board, seed, difficultyBox.getSelectedId()))
        message = {};
    else
        message = "Could not build a solvable board - try again.";

    assisted = false;
    channel.boardEdited();
    refreshButtons();
}

void TraceEditor::autoSolve()
{
    auto& board = channel.getBoard();
    const auto result = TraceRouter::solve (board);
    assisted = result.routed;

    if (! result.routed)
        message = "The solver could not route this board.";
    else if (! result.matched)
        message = "Routed, but the lengths could not be fully matched (skew " + juce::String (result.skewInches * channel.getVelocityPsPerInch(), 1) + " ps).";
    else
        message = "Auto-solved in " + juce::String (juce::roundToInt (result.milliseconds)) + " ms.";

    channel.boardEdited();
}

void TraceEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    sandboxButton.setBounds (top.removeFromLeft (80));
    gameButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (10);
    difficultyLabel.setBounds (top.removeFromLeft (68));
    difficultyBox.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (6);
    newGameButton.setBounds (top.removeFromLeft (90));
    solveButton.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (16);
    drawPlusButton.setBounds (top.removeFromLeft (70));
    drawMinusButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (6);
    undoButton.setBounds (top.removeFromLeft (100));
    clearPlusButton.setBounds (top.removeFromLeft (70));
    clearMinusButton.setBounds (top.removeFromLeft (70));
    clearBothButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (10);
    tdrButton.setBounds (top.removeFromLeft (56));

    area.removeFromTop (8);
    auto right = area.removeFromRight (250);
    area.removeFromRight (10);
    boardView->setBounds (area.removeFromTop (juce::roundToInt ((float) area.getWidth() * TraceBoard::height / TraceBoard::width)));
    area.removeFromTop (8);
    gameArea = area;

    auto tabs = right.removeFromTop (26);
    readoutsTab.setBounds (tabs.removeFromLeft (120));
    settingsTab.setBounds (tabs.removeFromLeft (120));
    right.removeFromTop (6);
    panelArea = right;

    // the settings page uses the same space as the readouts
    auto sliders = right;
    enabledButton.setBounds (sliders.removeFromTop (26));
    sliders.removeFromTop (6);
    const auto place = [&] (juce::Label& label, juce::Slider& slider)
    {
        auto row = sliders.removeFromTop (28);
        label.setBounds (row.removeFromLeft (80));
        slider.setBounds (row);
    };
    place (velocityLabel, velocitySlider);
    place (lossLabel, lossSlider);
    place (couplingLabel, couplingSlider);
    place (aggressorLabel, aggressorSlider);
    sliders.removeFromTop (10);
    place (reflectionLabel, reflectionSlider);
    place (connectorLabel, connectorSlider);
    place (sourceLabel, sourceSlider);
    place (loadLabel, loadSlider);
}

void TraceEditor::showPage (bool settings)
{
    settingsPage = settings;
    for (auto* c : std::initializer_list<juce::Component*> { &enabledButton, &velocitySlider, &lossSlider, &couplingSlider, &aggressorSlider,
                                                             &reflectionSlider, &connectorSlider, &sourceSlider, &loadSlider,
                                                             &velocityLabel, &lossLabel, &couplingLabel, &aggressorLabel,
                                                             &reflectionLabel, &connectorLabel, &sourceLabel, &loadLabel })
        c->setVisible (settings);
    repaint();
}

// The reflection report and the TDR only change with the drawing, the profile settings and the terminations.
void TraceEditor::refreshReflectionCache()
{
    const double source = channel.getParameterTree().getRawParameterValue ("sourcer")->load();
    const double load = channel.getParameterTree().getRawParameterValue ("loadr")->load();
    const double rate = channel.getLink().lineRateGBd.load();

    if (cache.version == channel.getProfileVersion() && std::abs (cache.sourceR - source) < 1.0e-6
        && std::abs (cache.loadR - load) < 1.0e-6 && std::abs (cache.rate - rate) < 1.0e-9)
        return;

    cache.version = channel.getProfileVersion();
    cache.sourceR = source;
    cache.loadR = load;
    cache.rate = rate;
    cache.report = channel.getReflectionReport();

    for (int t = 0; t < 2; ++t)
        cache.tdr[t] = cache.report.active[t] ? TraceReflections::computeTdr (cache.report.impedance[t], source, load, cache.report.inchesPerSection[t])
                                              : TraceReflections::Tdr();
}

void TraceEditor::drawTdr (juce::Graphics& g, juce::Rectangle<int> area)
{
    refreshReflectionCache();

    g.setColour (juce::Colour (0xff101418));
    g.fillRect (area);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRect (area, 1);

    auto plot = area.reduced (46, 22).withTrimmedBottom (6);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("TDR: impedance seen looking into each trace (a step launched from a 50 ohm reference)", area.withHeight (20).translated (8, 2),
                juce::Justification::centredLeft);

    double maxDistance = 1.0, low = 40.0, high = 60.0;
    for (int t = 0; t < 2; ++t)
    {
        if (! cache.tdr[t].distanceInches.empty())
            maxDistance = juce::jmax (maxDistance, cache.tdr[t].distanceInches.back());
        for (auto z : cache.tdr[t].impedance)
        {
            low = juce::jmin (low, z);
            high = juce::jmax (high, z);
        }
    }

    low = std::floor (juce::jmax (10.0, low - 3.0) / 5.0) * 5.0;
    high = std::ceil (juce::jmin (150.0, high + 3.0) / 5.0) * 5.0;
    const auto x = [&] (double inches) { return (float) plot.getX() + (float) (inches / maxDistance * plot.getWidth()); };
    const auto y = [&] (double ohms) { return (float) plot.getBottom() - (float) ((ohms - low) / (high - low) * plot.getHeight()); };

    g.setFont (juce::FontOptions (11.0f));
    for (double z = low; z <= high + 1.0e-9; z += 5.0)
    {
        g.setColour (juce::Colours::white.withAlpha (std::abs (z - 50.0) < 1.0e-9 ? 0.4f : 0.10f));
        g.drawHorizontalLine ((int) y (z), (float) plot.getX(), (float) plot.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String (juce::roundToInt (z)), area.getX() + 4, (int) y (z) - 6, 38, 12, juce::Justification::right);
    }

    for (int inch = 0; inch <= (int) maxDistance; ++inch)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawVerticalLine ((int) x (inch), (float) plot.getY(), (float) plot.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String (inch) + "\"", (int) x (inch) - 12, plot.getBottom() + 2, 24, 12, juce::Justification::centred);
    }

    const juce::Colour colours[2] = { juce::Colour (0xffffa630), juce::Colour (0xff35c8ff) };
    for (int t = 0; t < 2; ++t)
    {
        const auto& tdr = cache.tdr[t];
        if (tdr.distanceInches.empty())
            continue;

        juce::Path path;
        for (size_t i = 0; i < tdr.distanceInches.size(); ++i)
        {
            const auto p = juce::Point<float> (x (tdr.distanceInches[i]), juce::jlimit ((float) plot.getY(), (float) plot.getBottom(), y (tdr.impedance[i])));
            i == 0 ? path.startNewSubPath (p) : path.lineTo (p);
        }

        g.setColour (colours[t]);
        g.strokePath (path, juce::PathStrokeType (1.8f));
    }

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("distance from the source end (inches).  The right-hand end settles to the load resistance ("
                    + juce::String (juce::roundToInt (cache.loadR)) + " ohm).", area.withTrimmedTop (area.getHeight() - 16).translated (46, 0), juce::Justification::centredLeft);
}

void TraceEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    const auto& board = channel.getBoard();
    const double rate = channel.getLink().lineRateGBd.load();
    const double uiPs = channel.getUnitIntervalPs();
    const double skewPs = channel.getSkewPs();

    // --- readouts
    auto panel = settingsPage ? juce::Rectangle<int>() : panelArea;
    g.setFont (juce::FontOptions (13.0f));

    for (int t = 0; t < 2; ++t)
    {
        auto block = panel.removeFromTop (78);
        g.setColour (traceColour[t]);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (juce::String ("Trace ") + (t == 0 ? "+  (R)" : "-  (L)") + (board.isComplete (t) ? "" : "   unfinished"), block.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Length  " + juce::String (channel.getLengthInches (t), 2) + " in", block.removeFromTop (18), juce::Justification::centredLeft);
        g.drawText ("Delay   " + juce::String (juce::roundToInt (channel.getDelayPs (t))) + " ps", block.removeFromTop (18), juce::Justification::centredLeft);
        g.drawText ("Loss    " + juce::String (channel.getLossDb (t), 1) + " dB at " + juce::String (rate / 2.0, 1) + " GHz", block.removeFromTop (18), juce::Justification::centredLeft);
    }

    panel.removeFromTop (6);
    const double skewUI = std::abs (skewPs) / uiPs;
    const auto skewColour = skewUI < 0.1 ? juce::Colours::lightgreen : (skewUI < 0.3 ? juce::Colours::orange : juce::Colours::hotpink);
    g.setColour (skewColour);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Skew  " + juce::String (skewPs, 1) + " ps  (" + juce::String (skewUI, 2) + " UI)", panel.removeFromTop (22), juce::Justification::centredLeft);
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("1 UI = " + juce::String (juce::roundToInt (uiPs)) + " ps at " + juce::String (rate, 2) + " GBd", panel.removeFromTop (18), juce::Justification::centredLeft);
    g.drawText ("Loss imbalance  " + juce::String (std::abs (channel.getLossDb (0) - channel.getLossDb (1)), 2) + " dB", panel.removeFromTop (18), juce::Justification::centredLeft);
    g.drawText ("Average loss  " + juce::String (0.5 * (channel.getLossDb (0) + channel.getLossDb (1)), 1) + " dB", panel.removeFromTop (18), juce::Justification::centredLeft);

    // --- odd / even mode, as a network analyser would show them
    {
        const auto mode = channel.getModeReport();
        panel.removeFromTop (6);
        g.setColour (juce::Colours::lightblue);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Modes at " + juce::String (rate / 2.0, 1) + " GHz", panel.removeFromTop (20), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (juce::Colours::white);
        g.drawText ("Odd (differential):  " + juce::String (juce::roundToInt (mode.oddDelayPs)) + " ps, " + juce::String (-mode.sdd21Db, 1) + " dB",
                    panel.removeFromTop (17), juce::Justification::centredLeft);
        g.drawText ("Even (common):  " + juce::String (juce::roundToInt (mode.evenDelayPs)) + " ps, " + juce::String (-mode.scc21Db, 1) + " dB",
                    panel.removeFromTop (17), juce::Justification::centredLeft);
        g.setColour (mode.sdc21Db < -30.0 ? juce::Colours::lightgreen : (mode.sdc21Db < -20.0 ? juce::Colours::orange : juce::Colours::hotpink));
        g.drawText ("Diff -> common (Sdc21):  " + juce::String (mode.sdc21Db, 1) + " dB", panel.removeFromTop (17), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.drawText ("Crosstalk, one trace driven:  " + juce::String (mode.fextPercent, 1) + " %", panel.removeFromTop (17), juce::Justification::centredLeft);
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Coupled " + juce::String (mode.coupledInches, 1) + " in (weighted " + juce::String (mode.weightedInches, 1) + " in)",
                    panel.removeFromTop (17), juce::Justification::centredLeft);
    }

    if (! board.aggressors.empty())
    {
        const auto report = channel.getAggressorReport();
        panel.removeFromTop (6);
        g.setColour (juce::Colour (0xffe040c8));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Aggressor pickup", panel.removeFromTop (20), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (juce::Colours::white);

        for (size_t k = 0; k < report.lanes.size(); ++k)
            g.drawText ("Lane " + juce::String ((int) k + 1) + ":  + " + juce::String (report.lanes[k].plusInches, 1) + " in   - "
                            + juce::String (report.lanes[k].minusInches, 1) + " in",
                        panel.removeFromTop (17), juce::Justification::centredLeft);

        g.setColour (report.sxrDb > 30.0 ? juce::Colours::lightgreen : (report.sxrDb > 20.0 ? juce::Colours::orange : juce::Colours::hotpink));
        g.drawText ("Differential: " + juce::String (report.differentialRms * 1000.0, 1) + " mFS rms  (S/X " + juce::String (juce::roundToInt (report.sxrDb)) + " dB)",
                    panel.removeFromTop (17), juce::Justification::centredLeft);
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Common-mode: " + juce::String (report.commonRms * 1000.0, 1) + " mFS rms", panel.removeFromTop (17), juce::Justification::centredLeft);
    }

    // --- reflections
    if (! settingsPage)
    {
        refreshReflectionCache();
        panel.removeFromTop (6);
        g.setColour (juce::Colour (0xff7fd6a0));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("Reflections   Rs " + juce::String (juce::roundToInt (cache.sourceR)) + "  RL " + juce::String (juce::roundToInt (cache.loadR)) + " ohm",
                    panel.removeFromTop (20), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));

        for (int t = 0; t < 2; ++t)
        {
            g.setColour (juce::Colours::white);
            if (! cache.report.active[t])
            {
                g.drawText (juce::String (t == 0 ? "+" : "-") + ":  (too short or too fast to model)", panel.removeFromTop (17), juce::Justification::centredLeft);
                continue;
            }

            const double s11 = cache.report.returnLossDb[t];
            g.setColour (s11 < -20.0 ? juce::Colours::lightgreen : (s11 < -12.0 ? juce::Colours::orange : juce::Colours::hotpink));
            g.drawText (juce::String (t == 0 ? "+" : "-") + ":  return loss S11 " + juce::String (s11, 1) + " dB", panel.removeFromTop (17), juce::Justification::centredLeft);

            g.setColour (juce::Colours::lightgrey);
            for (size_t k = 0; k < cache.report.worst[t].size() && k < 2; ++k)
            {
                const auto& d = cache.report.worst[t][k];
                g.drawText ("   " + juce::String (d.distanceInches, 1) + " in:  " + (d.rho >= 0 ? "+" : "") + juce::String (d.rho * 100.0, 1) + " %",
                            panel.removeFromTop (16), juce::Justification::centredLeft);
            }
        }
    }

    // --- game / help
    if (tdrButton.getToggleState())
    {
        drawTdr (g, gameArea.withTrimmedBottom (4));
        return;
    }

    auto text = gameArea;
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    if (board.mode == TraceBoard::Mode::game)
    {
        const auto score = TraceGame::evaluate (board, skewPs, uiPs);
        g.drawText ("GAME   board #" + juce::String (board.gameSeed) + "   difficulty " + juce::String (board.difficulty), text.removeFromTop (22), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Route both traces from IN to OUT around the keep-outs. Skew within " + juce::String (score.skewBudgetUI, 2) + " UI ("
                        + juce::String (juce::roundToInt (score.skewBudgetUI * uiPs)) + " ps), average length within "
                        + juce::String (score.lengthBudget, 1) + " in (par " + juce::String (board.parLengthInches, 1) + " in).",
                    text.removeFromTop (20), juce::Justification::centredLeft);
        if (! board.aggressors.empty())
            g.drawText ("Magenta lanes are noisy neighbours: cross them square, keep off them, pass them evenly.",
                        text.removeFromTop (20), juce::Justification::centredLeft);

        const auto tick = [] (bool ok) { return ok ? "OK" : "--"; };
        g.setColour (score.complete ? juce::Colours::lightgreen : juce::Colours::orange);
        g.drawText ("Complete: " + juce::String (tick (score.complete))
                        + "     Skew " + juce::String (score.skewUI, 2) + " UI: " + tick (score.skewUI <= score.skewBudgetUI)
                        + "     Avg length " + juce::String (score.averageLength, 2) + " in: " + tick (score.averageLength <= score.lengthBudget)
                        + (score.hasCrosstalk ? "     Crosstalk " + juce::String (score.crosstalkIndex, 2) + " (limit " + juce::String (score.crosstalkBudget, 2) + "): "
                                                    + tick (score.crosstalkIndex <= score.crosstalkBudget)
                                              : juce::String()),
                    text.removeFromTop (22), juce::Justification::centredLeft);

        auto starRow = text.removeFromTop (30);
        if (assisted)
        {
            g.setColour (juce::Colours::skyblue);
            g.drawText ("Auto-solved - no score.", starRow, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (score.passed ? juce::Colours::lightgreen : juce::Colours::lightgrey);
            g.drawText (score.passed ? "SOLVED" : "Not solved yet", starRow.removeFromLeft (110), juce::Justification::centredLeft);

            for (int i = 0; i < 3; ++i)
            {
                juce::Path star;
                star.addStar ({ (float) starRow.getX() + 14.0f + 28.0f * (float) i, (float) starRow.getCentreY() }, 5, 5.5f, 12.0f);
                g.setColour (i < score.stars ? juce::Colour (0xffffd24a) : juce::Colours::white.withAlpha (0.18f));
                g.fillPath (star);
            }
        }
    }
    else
    {
        g.drawText ("SANDBOX", text.removeFromTop (22), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colours::lightgrey);
        g.drawText ("Click to place corners (45/90 degree routing). Click near an OUT pad to finish. Right-click or Backspace removes the last corner.",
                    text.removeFromTop (20), juce::Justification::centredLeft);
        g.drawText ("Longer trace = more delay and more loss; unequal lengths = skew. Switch to Game for random obstacles.",
                    text.removeFromTop (20), juce::Justification::centredLeft);
    }

    if (message.isNotEmpty())
    {
        g.setColour (juce::Colours::skyblue);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (message, text.removeFromTop (22), juce::Justification::centredLeft);
    }
}
