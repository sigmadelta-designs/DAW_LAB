#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ControlTip.h"

// Renders tooltips built with ControlTip::make() as a title line with a coloured DUT/SIM tag, followed
// by the reason text - instead of LookAndFeel_V4's default single line. An ordinary (uncoded) tooltip,
// such as a plain setTooltip() on an action button, still renders as LookAndFeel_V4 would.
//
// Every custom stage editor (and MainComponent) owns one of these and installs it with setLookAndFeel();
// nothing else about it differs from LookAndFeel_V4, so widgets look exactly as before.
class DawLabLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr int maxWidth = 340, margin = 8, tagWidth = 34, tagHeight = 16;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override
    {
        const auto size = measure (tipText);
        return juce::Rectangle<int> (screenPos.x, screenPos.y + 18, size.getWidth(), size.getHeight())
                 .constrainedWithin (parentArea);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& tipText, int width, int height) override
    {
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 4.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 4.0f, 1.0f);

        ControlTip::Parts parts;
        if (! ControlTip::parse (tipText, parts))
        {
            LookAndFeel_V4::drawTooltip (g, tipText, width, height);
            return;
        }

        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (margin, 6);
        auto titleRow = area.removeFromTop (18);

        auto tagArea = titleRow.removeFromRight (tagWidth).withSizeKeepingCentre (tagWidth, tagHeight);
        g.setColour (ControlTip::colourFor (parts.domain));
        g.fillRoundedRectangle (tagArea.toFloat(), 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText (ControlTip::tagFor (parts.domain), tagArea, juce::Justification::centred);

        g.setColour (findColour (juce::TooltipWindow::textColourId));
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (parts.title, titleRow.withTrimmedRight (6), juce::Justification::centredLeft);

        area.removeFromTop (4);
        g.setColour (findColour (juce::TooltipWindow::textColourId).withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawFittedText (parts.reason, area, juce::Justification::topLeft, 8);
    }

private:
    juce::TextLayout wrap (const juce::String& text, float width) const
    {
        juce::AttributedString s (text);
        s.setFont (juce::Font (juce::FontOptions (12.5f)));
        juce::TextLayout tl;
        tl.createLayout (s, width);
        return tl;
    }

    juce::Rectangle<int> measure (const juce::String& tipText) const
    {
        ControlTip::Parts parts;
        if (! ControlTip::parse (tipText, parts))
        {
            juce::AttributedString s (tipText);
            juce::TextLayout tl;
            tl.createLayout (s, (float) maxWidth);
            return { juce::jmin (maxWidth, (int) tl.getWidth() + 16), (int) tl.getHeight() + 12 };
        }

        const auto body = wrap (parts.reason, (float) (maxWidth - margin * 2));
        return { maxWidth, 18 + 4 + (int) std::ceil (body.getHeight()) + 12 };
    }
};
