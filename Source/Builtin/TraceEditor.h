#pragma once

#include "TraceChannel.h"
#include "TraceRouter.h"
#include "DawLabLookAndFeel.h"

// Board view (left), readouts (right) and the game panel (bottom).
// Click to place trace corners (45/90 degree routing, snapped to the 0.05" grid);
// click near the output pad to finish; right-click or Backspace to undo a corner.
class TraceEditor : public juce::AudioProcessorEditor,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit TraceEditor (TraceChannel& channelToEdit);
    ~TraceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class BoardView;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override { repaint(); }

    void setSandbox();
    void newGame();
    void autoSolve();
    void refreshButtons();
    void showPage (bool settings);
    void refreshReflectionCache();
    void drawTdr (juce::Graphics&, juce::Rectangle<int>);

    // reflection report and TDR, recomputed only when the profile or the terminations change
    struct ReflectionCache
    {
        int version = -1;
        double sourceR = 0.0, loadR = 0.0, rate = 0.0;
        TraceChannel::ReflectionReport report;
        TraceReflections::Tdr tdr[2];
    } cache;
    bool settingsPage = false;

    TraceChannel& channel;
    std::unique_ptr<BoardView> boardView;

    juce::TextButton sandboxButton { "Sandbox" }, gameButton { "Game" }, newGameButton { "New game" },
                     solveButton { "Auto-solve" }, undoButton { "Undo corner" },
                     clearPlusButton { "Clear +" }, clearMinusButton { "Clear -" }, clearBothButton { "Clear both" },
                     drawPlusButton { "Draw +" }, drawMinusButton { "Draw -" };
    juce::ComboBox difficultyBox;
    juce::Label difficultyLabel { {}, "Difficulty" };
    juce::Slider velocitySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 lossSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 couplingSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 aggressorSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 reflectionSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 connectorSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 sourceSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 loadSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label velocityLabel { {}, "Delay ps/in" }, lossLabel { {}, "Loss x FR4" }, couplingLabel { {}, "Coupling x" }, aggressorLabel { {}, "Aggressor x" },
                 reflectionLabel { {}, "Reflect x" }, connectorLabel { {}, "Connector %" }, sourceLabel { {}, "Source ohm" }, loadLabel { {}, "Load ohm" };
    juce::TextButton readoutsTab { "Readouts" }, settingsTab { "Settings" }, tdrButton { "TDR" };
    juce::ToggleButton enabledButton { "Channel enabled" };
    // declared after the controls they drive, so they are destroyed first
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> velocityAttachment, lossAttachment, couplingAttachment, aggressorAttachment,
                                                                                            reflectionAttachment, connectorAttachment, sourceAttachment, loadAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;

    juce::Rectangle<int> panelArea, gameArea;
    int activeTrace = TraceBoard::plus;
    bool assisted = false;
    juce::String message;

    DawLabLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TraceEditor)
};
