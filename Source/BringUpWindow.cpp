#include "BringUpWindow.h"

class BringUpWindow::Content : public juce::Component,
                               private juce::ChangeListener
{
public:
    Content (HostEngine& engineToWatch, juce::File directory, std::function<void (const juce::File&)> loader)
        : engine (engineToWatch), loadStepFile (std::move (loader))
    {
        setLookAndFeel (&lookAndFeel);

        // BRINGUP_01_..., BRINGUP_02_... - the zero-padded step number is what makes plain filename
        // sorting double as plan order, the same trick the HOMEWORK_* presets already rely on.
        for (const auto& f : directory.findChildFiles (juce::File::findFiles, false, "BRINGUP_*.labchain"))
            steps.add (f);
        steps.sort();

        currentStep = juce::jlimit (0, steps.size(), engine.getSettings()->getIntValue ("bringupStep", 0));

        progressLabel.setFont (juce::FontOptions (13.0f));
        progressLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (progressLabel);

        titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (titleLabel);

        bodyEditor.setMultiLine (true, true);
        bodyEditor.setReadOnly (true);
        bodyEditor.setCaretVisible (false);
        bodyEditor.setScrollbarsShown (true);
        bodyEditor.setFont (juce::FontOptions (14.0f));
        bodyEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1b1f));
        bodyEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (bodyEditor);

        hintButton.onClick = [this] { hintRevealed = true; rebuildBody(); };
        explanationButton.onClick = [this] { explanationRevealed = true; rebuildBody(); };
        hintButton.setTooltip ("Shows a nudge toward what's wrong on this step, without giving away the fix.");
        explanationButton.setTooltip ("Shows the root cause and how a real bring-up session would confirm and fix it.");
        addAndMakeVisible (hintButton);
        addAndMakeVisible (explanationButton);

        previousButton.setTooltip ("Goes back and reloads the previous step's chain.");
        nextButton.setTooltip ("Loads the next step's chain - the one button you need once you're done with this one.");
        previousButton.onClick = [this] { goToStep (currentStep - 1); };
        nextButton.onClick = [this] { goToStep (currentStep + 1); };
        addAndMakeVisible (previousButton);
        addAndMakeVisible (nextButton);

        engine.chainChanged.addChangeListener (this);

        // First time this window has ever been opened: jump straight into step 1 rather than making
        // the very first thing you see be an empty "nothing loaded yet" screen.
        if (currentStep == 0 && ! steps.isEmpty())
            goToStep (1);
        else
            refresh();

        setSize (560, 470);
    }

    ~Content() override
    {
        engine.chainChanged.removeChangeListener (this);
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1b1b1f)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        progressLabel.setBounds (area.removeFromTop (18));
        area.removeFromTop (4);
        titleLabel.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);

        auto navRow = area.removeFromBottom (32);
        previousButton.setBounds (navRow.removeFromLeft (140));
        nextButton.setBounds (navRow.removeFromRight (160));
        area.removeFromBottom (8);

        auto revealRow = area.removeFromBottom (32);
        hintButton.setBounds (revealRow.removeFromLeft (150));
        revealRow.removeFromLeft (10);
        explanationButton.setBounds (revealRow.removeFromLeft (170));
        area.removeFromBottom (10);

        bodyEditor.setBounds (area);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void goToStep (int step)
    {
        step = juce::jlimit (1, juce::jmax (1, steps.size()), step);
        if (steps.isEmpty())
            return;

        currentStep = step;
        engine.getSettings()->setValue ("bringupStep", currentStep);
        loadStepFile (steps[currentStep - 1]);
        // The chain-loaded callback will fire engine.chainChanged shortly, which calls refresh() - but
        // refresh now too so the progress line updates immediately instead of looking unresponsive.
        refresh();
    }

    // A fresh step means a fresh exercise - any previously revealed hint/explanation hides again.
    void refresh()
    {
        hintRevealed = false;
        explanationRevealed = false;
        rebuildBody();
    }

    void rebuildBody()
    {
        if (steps.isEmpty())
        {
            progressLabel.setText ("No BRINGUP_*.labchain presets found", juce::dontSendNotification);
            titleLabel.setText ("Bring-Up & Validation Plan", juce::dontSendNotification);
            bodyEditor.setText ("Generate the BRINGUP_*.labchain presets into this app's preset folder "
                                 "to use this window.", juce::dontSendNotification);
            previousButton.setEnabled (false);
            nextButton.setEnabled (false);
            nextButton.setButtonText ("Next Step");
            hintButton.setEnabled (false);
            explanationButton.setEnabled (false);
            return;
        }

        progressLabel.setText ("Step " + juce::String (currentStep) + " of " + juce::String (steps.size()),
                               juce::dontSendNotification);

        const auto& hw = engine.homework;
        if (hw.present)
        {
            titleLabel.setText (hw.title, juce::dontSendNotification);
            juce::String body = hw.prompt;
            if (hintRevealed)
                body << juce::newLine << juce::newLine << "HINT" << juce::newLine << hw.hint;
            if (explanationRevealed)
                body << juce::newLine << juce::newLine << "EXPLANATION" << juce::newLine << hw.explanation;
            bodyEditor.setText (body, juce::dontSendNotification);
        }
        else
        {
            titleLabel.setText ("Loading step " + juce::String (currentStep) + "...", juce::dontSendNotification);
            bodyEditor.setText ({}, juce::dontSendNotification);
        }

        hintButton.setEnabled (hw.present && ! hintRevealed);
        hintButton.setButtonText (hintRevealed ? "Hint shown below" : "Reveal hint");
        explanationButton.setEnabled (hw.present && ! explanationRevealed);
        explanationButton.setButtonText (explanationRevealed ? "Explanation shown below" : "Reveal explanation");

        previousButton.setEnabled (currentStep > 1);
        const bool onLastStep = currentStep >= steps.size();
        nextButton.setEnabled (! onLastStep);
        nextButton.setButtonText (onLastStep ? "Plan complete" : "Next Step " + juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6")));
    }

    HostEngine& engine;
    std::function<void (const juce::File&)> loadStepFile;
    juce::Array<juce::File> steps;
    int currentStep = 0;   // 1-based; 0 only while no BRINGUP_* presets exist at all

    juce::Label progressLabel, titleLabel;
    juce::TextEditor bodyEditor;
    juce::TextButton hintButton { "Reveal hint" }, explanationButton { "Reveal explanation" };
    juce::TextButton previousButton { juce::CharPointer_UTF8 ("\xe2\x97\x80 Previous Step") }, nextButton { "Next Step" };
    bool hintRevealed = false, explanationRevealed = false;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };
};

BringUpWindow::BringUpWindow (HostEngine& engineToWatch, juce::File presetDirectory,
                              std::function<void (const juce::File&)> loadStepFile, std::function<void()> onClose)
    : DocumentWindow ("Bring-Up & Validation Plan", juce::Colours::darkgrey, DocumentWindow::closeButton),
      closeCallback (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (engineToWatch, std::move (presetDirectory), std::move (loadStepFile)), true);
    setResizable (true, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

BringUpWindow::~BringUpWindow() = default;
