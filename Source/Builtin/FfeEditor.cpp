#include "FfeEditor.h"

FfeEditor::FfeEditor (DiffFfe& ffeToEdit)
    : juce::AudioProcessorEditor (ffeToEdit), ffe (ffeToEdit)
{
    for (int tap = 0; tap < DiffFfe::numTaps; ++tap)
    {
        auto& slider = sliders[(size_t) tap];
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 20);
        slider.setDoubleClickReturnValue (true, tap == DiffFfe::mainTap ? 1.0 : 0.0);
        addAndMakeVisible (slider);

        auto& label = labels[(size_t) tap];
        label.setText (DiffFfe::getTapName (tap), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);

        attachments[(size_t) tap] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            ffe.getParameterTree(), DiffFfe::getTapParamId (tap), slider);
    }

    addAndMakeVisible (txModeButton);
    addAndMakeVisible (reverseButton);
    txAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        ffe.getParameterTree(), "txmode", txModeButton);
    reverseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        ffe.getParameterTree(), "reverse", reverseButton);

    addAndMakeVisible (adaptButton);
    addAndMakeVisible (restartButton);
    adaptAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        ffe.getParameterTree(), "adapt", adaptButton);
    restartButton.onClick = [this] { ffe.restartTraining(); };

    setSize (640, 620);
    startTimerHz (15);
}

FfeEditor::~FfeEditor()
{
    stopTimer();
}

void FfeEditor::timerCallback()
{
    repaint();
}

void FfeEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    txModeButton.setBounds (area.removeFromTop (24));
    reverseButton.setBounds (area.removeFromTop (24));

    auto adaptRow = area.removeFromTop (26);
    restartButton.setBounds (adaptRow.removeFromRight (80));
    adaptButton.setBounds (adaptRow);
    statusArea = area.removeFromTop (22);
    area.removeFromTop (4);

    textArea = area.removeFromBottom (96);
    area.removeFromBottom (6);
    plotArea = area.removeFromBottom (140);
    area.removeFromBottom (6);

    const int columnWidth = area.getWidth() / DiffFfe::numTaps;
    for (int tap = 0; tap < DiffFfe::numTaps; ++tap)
    {
        auto column = area.removeFromLeft (columnWidth);
        labels[(size_t) tap].setBounds (column.removeFromTop (20));
        sliders[(size_t) tap].setBounds (column);
    }
}

void FfeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    const auto taps = ffe.getEffectiveTaps();

    // Trainer status
    {
        const auto& status = ffe.trainerStatus;
        juce::String text;
        auto colour = juce::Colours::lightgrey;

        switch (status.state.load())
        {
            case DiffFfe::trainerOff:
                text = "Auto-adapt is off - taps are set by hand.";
                break;
            case DiffFfe::trainerStarting:
            case DiffFfe::trainerAdapting:
                text = "Adapting...  step " + juce::String (status.step.load(), 3) + "   decision error "
                       + juce::String (LinkSettings::nmseToDb (status.nmse.load()), 1) + " dB   (" + juce::String (status.evaluations.load()) + " trials)";
                colour = juce::Colours::orange;
                break;
            case DiffFfe::trainerConverged:
                text = "Converged: decision error " + juce::String (LinkSettings::nmseToDb (status.nmse.load()), 1)
                       + " dB after " + juce::String (status.evaluations.load()) + " trials. Retrains if the link changes.";
                colour = juce::Colours::lightgreen;
                break;
        }

        if (status.state.load() != DiffFfe::trainerOff && ! status.probeAlive.load())
        {
            text = "Waiting for measurements - put an Eye Scope after this stage (and a running signal).";
            colour = juce::Colours::hotpink;
        }

        g.setColour (colour);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (text, statusArea, juce::Justification::centredLeft);
    }

    // Stem plot of the taps that are actually applied (after TX-mode / reverse).
    auto plot = plotArea.toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (plot);
    const float zeroY = plot.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawHorizontalLine ((int) zeroY, plot.getX(), plot.getRight());

    const float scale = juce::jmax (1.0f, *std::max_element (taps.begin(), taps.end(),
                                                             [] (float a, float b) { return std::abs (a) < std::abs (b); }));
    const float halfHeight = plot.getHeight() * 0.42f;
    const float step = plot.getWidth() / (float) DiffFfe::numTaps;

    g.setFont (juce::FontOptions (12.0f));
    for (int tap = 0; tap < DiffFfe::numTaps; ++tap)
    {
        const float x = plot.getX() + step * ((float) tap + 0.5f);
        const float y = zeroY - taps[(size_t) tap] / std::abs (scale) * halfHeight;
        g.setColour (tap == DiffFfe::mainTap ? juce::Colours::orange : (taps[(size_t) tap] < 0 ? juce::Colours::skyblue : juce::Colours::hotpink));
        g.drawLine (x, zeroY, x, y, 3.0f);
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::white);
        g.drawText (juce::String (taps[(size_t) tap], 3), (int) x - 26, (int) (taps[(size_t) tap] >= 0 ? y - 20 : y + 6), 52, 14,
                    juce::Justification::centred);
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("Applied taps (time runs left to right: upcoming symbols on the left, past symbols on the right)",
                plotArea.withHeight (16).translated (4, 0), juce::Justification::topLeft);

    // Spectrum summary
    const float dc = DiffFfe::gainDbAtDc (taps);
    const float nyquist = DiffFfe::gainDbAtNyquist (taps);
    const float boost = nyquist - dc;
    const float channelLoss = ffe.getLink().getChannelLossDb();

    auto text = textArea;
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (juce::Colours::white);
    g.drawText ("Gain at DC: " + juce::String (dc, 1) + " dB      Gain at Nyquist: " + juce::String (nyquist, 1)
                    + " dB      Boost (Nyquist vs DC): " + (boost >= 0 ? "+" : "") + juce::String (boost, 1) + " dB",
                text.removeFromTop (22), juce::Justification::centredLeft);

    if (channelLoss > 0.05f)
    {
        const float residual = channelLoss - boost;   // >0: channel still wins, <0: FFE overshoots
        const auto colour = std::abs (residual) < 1.5f ? juce::Colours::lightgreen
                          : (residual > 0 ? juce::Colours::orange : juce::Colours::hotpink);
        g.setColour (colour);
        g.drawText ("Channel loss at Nyquist: " + juce::String (channelLoss, 1) + " dB   ->   "
                        + (std::abs (residual) < 1.5f ? "about matched"
                           : residual > 0 ? "UNDER-equalised by " + juce::String (residual, 1) + " dB"
                                          : "OVER-equalised by " + juce::String (-residual, 1) + " dB"),
                    text.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText ("(compares boost magnitude only - the eye scope shows whether the tap shape and direction are right too)",
                    text.removeFromTop (20), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText ("No lossy channel in the chain.", text.removeFromTop (22), juce::Justification::centredLeft);
    }
}
