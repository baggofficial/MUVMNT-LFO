#pragma once
// The MUVMNT design system: every colour, size and type size of the interface. Change the look here, not in the
// drawing code.
//
// The one rule behind the colours: HUE MEANS SOMETHING. Coral, teal and blue are the low, mid and high bands, gold is
// the sidechain kick, and the meters go green, yellow, red. Everything else (the Single curve, switches, knobs,
// selections) is graphite and off-white, so a colour on screen is always information.
//
// The colours are variables that setDark() reassigns, so everything that draws with theme::xxx follows.
#include <juce_graphics/juce_graphics.h>

namespace theme
{
    inline bool dark = true;
    inline int version = 0; // bumps on every theme change, so cached drawings know to redraw

    // ---- surfaces, from the back to the front
    inline juce::Colour window;   // behind everything
    inline juce::Colour bg;       // wells: the graph, the frequency strip, the meters
    inline juce::Colour panel;    // controls at rest, popups
    inline juce::Colour raised;   // hover, chips that sit on a well
    inline juce::Colour line;     // borders and separators
    inline juce::Colour track;    // the unlit part of knobs and switches; pressed

    // ---- text
    inline juce::Colour text;     // values, names
    inline juce::Colour sub;      // labels
    inline juce::Colour muted;    // quiet hints

    // ---- meaning
    inline juce::Colour accent;   // "on" and "selected", and the Single curve: neutral on purpose
    inline juce::Colour onAccent; // text and marks on top of accent
    inline juce::Colour amber;    // the sidechain kick, and warnings
    inline juce::Colour handle;   // curve tangent handles
    inline juce::Colour dialTop, dialBottom; // knob faces
    inline juce::Colour dialTrack;            // the whole range of a knob, behind its value

    // ---- the graph's grid (opaque: it must read the same under every waveform)
    inline juce::Colour gridStrong; // quarters of the cycle, the middle and the edges
    inline juce::Colour gridFine;

    inline void setDark(bool on)
    {
        dark = on;
        auto c = [on](juce::uint32 darkValue, juce::uint32 lightValue) { return juce::Colour(on ? darkValue : lightValue); };
        window     = c(0xff0b0d10, 0xffdfe2e6);
        bg         = c(0xff0e1115, 0xfff1f3f5);
        panel      = c(0xff12161b, 0xfffafbfc);
        raised     = c(0xff181d24, 0xffffffff);
        line       = c(0xff2a3038, 0xffc9ced6);
        track      = c(0xff272d36, 0xffd8dce2);
        text       = c(0xffe8ebef, 0xff161a20);
        sub        = c(0xff929aa5, 0xff565e6a);
        muted      = c(0xff606975, 0xff8a919b);
        accent     = c(0xffdce4ee, 0xff222a35);
        onAccent   = c(0xff0b0d10, 0xffffffff);
        amber      = c(0xfff2c94c, 0xffc99700);
        handle     = c(0xffeab308, 0xffd49d00); // the yellow tangent handles of After Effects' graph editor
        dialTop    = c(0xff252c36, 0xffffffff); // the face of a knob, lit from above
        dialBottom = c(0xff13171d, 0xffe4e7eb);
        dialTrack  = c(0xff333b46, 0xffc6ccd4);
        gridStrong = c(0xff414955, 0xffb4bac3);
        gridFine   = c(0xff29303a, 0xffd6dae0);
        ++version;
    }
    namespace detail { inline const bool initialised = (setDark(true), true); }

    // ---- frequency bands: low coral, mid teal, high blue
    inline juce::Colour bandColour(int band)
    {
        if (dark) return juce::Colour(band == 0 ? 0xffe58d62 : band == 1 ? 0xff54bfa0 : 0xff729eff);
        return juce::Colour(band == 0 ? 0xffc4632f : band == 1 ? 0xff1f8a6d : 0xff3a6bd8);
    }
    // Text colour for a band name
    inline juce::Colour bandText(int band) { return dark ? bandColour(band).brighter(0.1f) : bandColour(band).darker(0.25f); }
    // A colour for outlines of the band curves: a touch darker on light, a touch brighter on dark
    inline juce::Colour strong(juce::Colour c) { return dark ? c.brighter(0.15f) : c.darker(0.2f); }

    // guides and quiet marks: light ink on the dark theme, dark ink on the light one
    inline juce::Colour ink(float alpha) { return dark ? juce::Colours::white.withAlpha(alpha * 0.7f) : juce::Colours::black.withAlpha(alpha); }
    inline juce::Colour gridMajor() { return gridStrong; }
    inline juce::Colour gridMinor() { return gridFine; }

    // level meters
    // the end-of-cycle arrow, by distance from the centre line (0 .. 1): white when safe, then soft yellow and orange,
    // to red at the peaks. On the light theme the safe end is graphite, because white would vanish there.
    inline juce::Colour wrapColour(float distance)
    {
        const juce::Colour stop[] { juce::Colour(dark ? 0xffffffff : 0xff222a35), juce::Colour(dark ? 0xfff4d675 : 0xffb58900),
                                    juce::Colour(dark ? 0xfff0924a : 0xffd2691e), juce::Colour(dark ? 0xffe5484d : 0xffd32f36) };
        constexpr float at[] { 0.f, 0.4f, 0.7f, 1.f };
        const float a = juce::jlimit(0.f, 1.f, distance);
        const int i = a < at[1] ? 0 : a < at[2] ? 1 : 2;
        return stop[i].interpolatedWith(stop[i + 1], (a - at[i]) / (at[i + 1] - at[i]));
    }
    // the level bars: a quieter, studio-style progression from low to over
    inline const juce::Colour levelLow { 0xff2f7d62 }, levelNormal { 0xff45a878 }, levelHigh { 0xffc3b254 },
                              levelNear { 0xffd58a43 }, levelOver { 0xffe5484d };

    // ---- sizes (logical pixels)
    namespace size
    {
        constexpr float radius = 5.f;        // wells, buttons, popups
        constexpr float radiusSmall = 4.f;   // chips
        constexpr float border = 1.f;
        constexpr int edge = 14;             // window edge to content
        constexpr int top = 4;               // above the first bar
        constexpr int brandBar = 28;         // first bar: brand on the left; global controls on the right
        constexpr int toolBar = 28;          // second bar: the LFO's setup on the left, editing tools on the right
        constexpr int barGap = 4;
        constexpr int gap = 8;               // between controls that belong together
        constexpr int groupGap = 16;         // between groups
    }

    // ---- type sizes (Inter, bundled; see Fonts.h)
    namespace type
    {
        constexpr float wordmark = 18.f;
        constexpr float descriptor = 11.f;
        constexpr float label = 10.5f;       // small uppercase captions
        constexpr float control = 13.f;      // buttons, dropdowns, menus
        constexpr float value = 13.f;
        constexpr float axis = 9.5f;         // note values under the graph
    }
}
