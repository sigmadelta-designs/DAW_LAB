#include "PllCoreEditor.h"

PllCoreEditor::PllCoreEditor (PllCore& coreToEdit)
    : juce::AudioProcessorEditor (coreToEdit), core (coreToEdit)
{
    setLookAndFeel (&lookAndFeel);

    for (auto* c : std::initializer_list<juce::Component*> {
            &refFreqSlider, &refFreqLabel, &fbDividerSlider, &fbDividerLabel, &vcoFreeSlider, &vcoFreeLabel,
            &loopKpSlider, &loopKpLabel, &loopKiSlider, &loopKiLabel, &refJitterSlider, &refJitterLabel })
        addAndMakeVisible (c);

    refFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        core.getParameterTree(), "reffreq", refFreqSlider);
    fbDividerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        core.getParameterTree(), "fbdivider", fbDividerSlider);
    vcoFreeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        core.getParameterTree(), "vcofree", vcoFreeSlider);
    loopKpAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        core.getParameterTree(), "loopkp", loopKpSlider);
    loopKiAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        core.getParameterTree(), "loopki", loopKiSlider);
    refJitterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        core.getParameterTree(), "refjitter", refJitterSlider);

    using D = ControlTip::Domain;
    refFreqSlider.setTooltip (ControlTip::make ("Reference frequency (MHz)", D::dut,
        "The clean reference clock the loop multiplies up - a real bench/crystal reference property."));
    fbDividerSlider.setTooltip (ControlTip::make ("Feedback divider N", D::dut,
        "The programmed feedback divider: locked, the VCO settles at exactly N x the reference. A wrong N "
        "locks confidently to the wrong frequency with no error reported at all - a real, easy-to-miss "
        "bring-up bug, not a hand-waved one."));
    vcoFreeSlider.setTooltip (ControlTip::make ("VCO free-run frequency (MHz)", D::dut,
        "Where the VCO sits with zero loop correction - a real property of the oscillator itself, before "
        "the loop pulls it onto N x reference."));
    loopKpSlider.setTooltip (ControlTip::make ("Loop filter proportional gain", D::dut,
        "The loop filter's R1/proportional path, folded into a direct gain rather than a literal resistance "
        "- the same disclosed simplification this project's SerDes CDR already makes for its own kp. Higher "
        "damps the lock transient harder (less overshoot, see the PLL Core's own strip chart)."));
    loopKiSlider.setTooltip (ControlTip::make ("Loop filter integral gain", D::dut,
        "The loop filter's C1/integral path - the loop's frequency memory, folded into a direct gain the "
        "same way loop kp is. Sets the loop bandwidth: too low and a frequency step never pulls in within "
        "a reasonable time; too high and the loop stops filtering reference jitter at all."));
    refJitterSlider.setTooltip (ControlTip::make ("Injected reference jitter (UI rms)", D::simulation,
        "A Gaussian per-edge timing jitter added to the reference, the same Box-Muller convention as the "
        "SerDes generator's RJ - an impairment you dial in to stress the loop, not a real PLL register."));

    for (auto* l : std::initializer_list<juce::Label*> {
            &refFreqLabel, &fbDividerLabel, &vcoFreeLabel, &loopKpLabel, &loopKiLabel, &refJitterLabel })
        l->setJustificationType (juce::Justification::centredRight);

    setSize (560, 460);
    startTimerHz (20);
}

PllCoreEditor::~PllCoreEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PllCoreEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    const auto row = [&] (juce::Label& label, juce::Slider& slider, int labelWidth)
    {
        auto r = area.removeFromTop (26);
        label.setBounds (r.removeFromLeft (labelWidth));
        slider.setBounds (r);
        area.removeFromTop (6);
    };

    row (refFreqLabel, refFreqSlider, 150);
    row (fbDividerLabel, fbDividerSlider, 150);
    row (vcoFreeLabel, vcoFreeSlider, 150);
    row (loopKpLabel, loopKpSlider, 150);
    row (loopKiLabel, loopKiSlider, 150);
    row (refJitterLabel, refJitterSlider, 150);

    area.removeFromTop (6);
    statusArea = area.removeFromTop (40);
    area.removeFromTop (8);
    transientArea = area;
}

void PllCoreEditor::timerCallback()
{
    const double targetHz = (double) core.getParameterTree().getRawParameterValue ("reffreq")->load()
                           * (double) core.getParameterTree().getRawParameterValue ("fbdivider")->load();

    // A real step (not just float noise) resets the transient history - this is meant to show "how the
    // loop responds to this change", not an endless blend of every change that ever happened.
    if (std::abs (targetHz - lastTargetHz) > 1.0e-6)
    {
        lastTargetHz = targetHz;
        transientHz.clear();
        rangeHz = 10.0f;
    }

    const float errHz = (float) (core.status.vcoFreqHz.load() - targetHz);
    transientHz.push_back (errHz);
    while ((int) transientHz.size() > transientLength)
        transientHz.pop_front();

    rangeHz = juce::jmax (10.0f, juce::jmax (rangeHz * 0.98f, std::abs (errHz) * 1.4f));

    repaint (statusArea);
    repaint (transientArea);
}

void PllCoreEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    drawTransient (g, transientArea.toFloat());

    const double targetHz = (double) core.getParameterTree().getRawParameterValue ("reffreq")->load()
                           * (double) core.getParameterTree().getRawParameterValue ("fbdivider")->load();
    const bool locked = core.status.locked.load();

    auto area = statusArea;
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (locked ? juce::Colours::lightgreen : juce::Colours::orange);
    g.drawText (locked ? "LOCKED" : "ACQUIRING", area.removeFromLeft (110), juce::Justification::centredLeft);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("VCO: " + juce::String (core.status.vcoFreqHz.load(), 4) + " Hz (target " + juce::String (targetHz, 4) + " Hz)   "
                    + "Phase err: " + juce::String (core.status.phaseErrorRad.load(), 4) + " rad",
                area, juce::Justification::centredLeft);
}

void PllCoreEditor::drawTransient (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff101418));
    g.fillRect (area);

    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

    if (transientHz.size() > 1)
    {
        juce::Path path;
        const int n = (int) transientHz.size();
        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (transientLength - 1);
            const float y = area.getCentreY() - (transientHz[(size_t) i] / rangeHz) * (area.getHeight() * 0.48f);
            i == 0 ? path.startNewSubPath (x, y) : path.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xff35c8ff));
        g.strokePath (path, juce::PathStrokeType (1.6f));
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("Lock-acquisition transient: VCO frequency error vs. time (+-" + juce::String (rangeHz, 1)
                    + " Hz full scale, last " + juce::String ((double) transientLength / 20.0, 0) + " s - resets on a reffreq/N change)",
                area.reduced (6.0f).withHeight (16.0f), juce::Justification::topLeft);
    g.drawRect (area, 1.0f);
}
