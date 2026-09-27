// Generates AppIcon.png: a small, bold eye-diagram glyph for the app icon. Optimised to still read at
// 16x16 - few, thick strokes and a bright core, rather than the many delicate overlapping traces a real
// eye scope shows. Not part of the app build; this is a one-off image source, run by hand whenever the
// icon needs regenerating or restyling.
//
// Build and run it as its own tiny JUCE console app, e.g. from a scratch directory:
//   cmake_minimum_required(VERSION 3.22)
//   project(IconGen)
//   set(CMAKE_CXX_STANDARD 20)
//   include(FetchContent)
//   FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4)
//   FetchContent_MakeAvailable(JUCE)
//   juce_add_console_app(IconGen PRODUCT_NAME IconGen)
//   target_sources(IconGen PRIVATE generate_icon.cpp)
//   target_link_libraries(IconGen PRIVATE juce::juce_gui_extra juce::juce_recommended_config_flags)
// then run the built binary with the output path as its one argument, e.g.
//   ./IconGen /Users/you/LAB_DAW/Assets/AppIcon.png

#include <juce_gui_extra/juce_gui_extra.h>
using namespace juce;

int main (int argc, char** argv)
{
    const File out (argc > 1 ? String (argv[1]) : "AppIcon.png");

    constexpr int size = 1024;
    Image img (Image::ARGB, size, size, true);
    Graphics g (img);

    const float radius = size * 0.22f;
    Path bg;
    bg.addRoundedRectangle (0.0f, 0.0f, (float) size, (float) size, radius);
    g.setGradientFill (ColourGradient (Colour (0xff141827), 0, 0, Colour (0xff05060a), 0, (float) size, false));
    g.fillPath (bg);

    g.saveState();
    g.reduceClipRegion (bg);

    const float marginX = size * 0.20f, marginY = size * 0.24f;
    const float plotW = size - marginX * 2.0f, plotH = size - marginY * 2.0f;
    const float amp = plotH * 0.5f;
    const float midY = size * 0.5f, midX = size * 0.5f;

    auto raisedCosine = [] (float t) { return 0.5f - 0.5f * std::cos (jlimit (0.0f, 1.0f, t) * MathConstants<float>::pi); };

    // Just two bold traces: one starts high and ends low (orange), one starts low and ends high (blue) -
    // the minimum needed to draw the eye's crossing "X", kept thick enough to survive scaling to 16x16.
    // A couple of thin echoes behind them hint at "many overlaid traces" without hurting legibility.
    const Colour traceColour (0xffffa63c), glowColour (0xff35c8ff);

    auto tracePath = [&] (float rise, bool startHigh, bool endHigh)
    {
        Path p;
        for (int i = 0; i <= 96; ++i)
        {
            const float uiT = (float) i / 96.0f;
            const float edge = raisedCosine ((uiT - (0.5f - rise * 0.5f)) / rise);
            const float level = startHigh ? (endHigh ? 1.0f : (1.0f - 2.0f * edge)) : (endHigh ? (2.0f * edge - 1.0f) : -1.0f);
            const float x = marginX + uiT * plotW;
            const float y = midY - level * amp;
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        return p;
    };

    struct Trace { float rise; bool startHigh, endHigh; Colour colour; float alpha; float width; };
    const Trace traces[] =
    {
        // thin echoes first (drawn underneath), then the two bold main strokes on top
        { 0.30f, true,  false, traceColour, 0.35f, 14.0f },
        { 0.55f, true,  false, traceColour, 0.35f, 14.0f },
        { 0.30f, false, true,  glowColour,  0.35f, 14.0f },
        { 0.55f, false, true,  glowColour,  0.35f, 14.0f },
        { 0.42f, true,  false, traceColour, 1.0f,  34.0f },
        { 0.42f, false, true,  glowColour,  1.0f,  34.0f },
    };

    // Glow pass: same strokes, wider and blurred, on a separate layer.
    Image glow (Image::ARGB, size, size, true);
    {
        Graphics gg (glow);
        gg.reduceClipRegion (bg);
        for (auto& t : traces)
        {
            gg.setColour (t.colour.withAlpha (0.5f));
            gg.strokePath (tracePath (t.rise, t.startHigh, t.endHigh), PathStrokeType (t.width * 1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // bright bloom at the crossing point
        gg.setColour (Colours::white.withAlpha (0.9f));
        gg.fillEllipse (midX - 60.0f, midY - 60.0f, 120.0f, 120.0f);
    }
    ImageConvolutionKernel blur (31);
    blur.createGaussianBlur (20.0f);
    blur.applyToImage (glow, glow, glow.getBounds());
    g.drawImageAt (glow, 0, 0);

    // Crisp strokes on top.
    for (auto& t : traces)
    {
        g.setColour (t.colour.withAlpha (t.alpha));
        g.strokePath (tracePath (t.rise, t.startHigh, t.endHigh), PathStrokeType (t.width, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Bright white core at the crossing, so there's a strong focal point even at 16x16.
    g.setColour (Colours::white.withAlpha (0.95f));
    g.fillEllipse (midX - 20.0f, midY - 20.0f, 40.0f, 40.0f);

    g.restoreState();

    g.setColour (Colours::white.withAlpha (0.10f));
    g.strokePath (bg, PathStrokeType (3.0f));

    out.deleteFile();
    FileOutputStream os (out);
    PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
