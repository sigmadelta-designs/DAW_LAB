#include "HintWindow.h"

class HintWindow::Content : public juce::Component,
                            private juce::ChangeListener
{
public:
    explicit Content (HostEngine& engineToWatch) : engine (engineToWatch)
    {
        setLookAndFeel (&lookAndFeel);

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
        hintButton.setTooltip ("Shows a nudge toward what's wrong, without giving away the fix.");
        explanationButton.setTooltip ("Shows the root cause and how a real bring-up/debug session would confirm and fix it.");
        addAndMakeVisible (hintButton);
        addAndMakeVisible (explanationButton);

        engine.chainChanged.addChangeListener (this);
        refresh();
        setSize (540, 420);
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
        titleLabel.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);

        auto buttons = area.removeFromBottom (32);
        hintButton.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (10);
        explanationButton.setBounds (buttons.removeFromLeft (170));
        area.removeFromBottom (10);

        bodyEditor.setBounds (area);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    // Called whenever the loaded chain changes - a fresh preset means a fresh exercise, so any
    // previously revealed hint/explanation hides again rather than carrying over.
    void refresh()
    {
        hintRevealed = false;
        explanationRevealed = false;
        rebuildBody();
    }

    void rebuildBody()
    {
        const auto& hw = engine.homework;

        if (! hw.present)
        {
            titleLabel.setText ("No homework on this chain", juce::dontSendNotification);
            bodyEditor.setText ("Load a HOMEWORK_*.labchain preset (Load Preset...) to get a scenario with "
                                "something deliberately wrong in it, and a hint you can check here if you get stuck.",
                                juce::dontSendNotification);
            hintButton.setEnabled (false);
            explanationButton.setEnabled (false);
            hintButton.setButtonText ("Reveal hint");
            explanationButton.setButtonText ("Reveal explanation");
            return;
        }

        titleLabel.setText (hw.title, juce::dontSendNotification);

        juce::String body = hw.prompt;
        if (hintRevealed)
            body << juce::newLine << juce::newLine << "HINT" << juce::newLine << hw.hint;
        if (explanationRevealed)
            body << juce::newLine << juce::newLine << "EXPLANATION" << juce::newLine << hw.explanation;

        bodyEditor.setText (body, juce::dontSendNotification);
        hintButton.setEnabled (! hintRevealed);
        hintButton.setButtonText (hintRevealed ? "Hint shown below" : "Reveal hint");
        explanationButton.setEnabled (! explanationRevealed);
        explanationButton.setButtonText (explanationRevealed ? "Explanation shown below" : "Reveal explanation");
    }

    HostEngine& engine;
    juce::Label titleLabel;
    juce::TextEditor bodyEditor;
    juce::TextButton hintButton { "Reveal hint" }, explanationButton { "Reveal explanation" };
    bool hintRevealed = false, explanationRevealed = false;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };
};

HintWindow::HintWindow (HostEngine& engineToWatch, std::function<void()> onClose)
    : DocumentWindow ("Homework Hint", juce::Colours::darkgrey, DocumentWindow::closeButton),
      closeCallback (std::move (onClose))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (engineToWatch), true);
    setResizable (true, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

HintWindow::~HintWindow() = default;
