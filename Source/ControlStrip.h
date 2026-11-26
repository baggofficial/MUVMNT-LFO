#pragma once
// The control strip under the graph, laid out the same way in both tabs:
//     RATE   DEPTH   SMOOTH   OFFSET   AUDIO VIEW  |  AUTO CURVE
// Five equal knob columns, then the kick analyser as a group of its own. The meters' column to the right of the graph
// continues down beside the strip (the gain knobs and the clipper sit there), so nothing here has to dodge them.
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "Fonts.h"
#include "Theme.h"

namespace controls
{
    constexpr int height = 86;       // caption 16 + dial 54 + value 16
    constexpr int captionH = 16;
    constexpr int kickWidth = 88;
    constexpr int meterColumn = 46;  // two 22 px meters, 2 px apart
    constexpr int meterGap = 6;

    // dial diameters: Rate and Depth lead, Smooth and Offset follow, Waveform (display only) is the quietest
    constexpr float leadDial = 54.f, dial = 46.f, viewDial = 38.f;

    struct Columns
    {
        std::array<juce::Rectangle<int>, 5> knob;
        juce::Rectangle<int> kick;
    };

    inline Columns columns(juce::Rectangle<int> strip)
    {
        Columns c;
        c.kick = strip.removeFromRight(kickWidth);
        const int w = strip.getWidth() / 5;
        for (auto& column : c.knob) column = strip.removeFromLeft(w);
        return c;
    }

    inline void place(juce::Label& caption, juce::Slider& knob, juce::Rectangle<int> column, float diameter)
    {
        caption.setBounds(column.removeFromTop(captionH));
        knob.getProperties().set("dial", diameter);
        knob.setBounds(column);
    }

    // The Rate group: the dial with a matching two-part capsule on each side, Sync / Hz on its left and dotted / triplet
    // on its right. Both are the same size, the same distance from the dial's arc, and end level with the arc's two ends.
    constexpr int capsuleWidth = 24, capsuleHalf = 21, capsuleGap = 7;

    inline void placeRateExtras(juce::Component& syncSwitch, juce::Component& dotted, juce::Component& triplet, juce::Rectangle<int> column)
    {
        const int centreX = column.getCentreX(), centreY = column.getY() + captionH + (height - captionH - 16) / 2; // the dial's middle
        const int reach = (int) (leadDial * 0.5f) + capsuleGap;  // from the middle to a capsule's near side
        const int top = centreY + 19 - 2 * capsuleHalf;          // the arc ends 19 px below the middle
        syncSwitch.setBounds(centreX - reach - capsuleWidth, top, capsuleWidth, 2 * capsuleHalf);
        dotted.setBounds(centreX + reach, top, capsuleWidth, capsuleHalf);
        triplet.setBounds(centreX + reach, top + capsuleHalf, capsuleWidth, capsuleHalf);
        dotted.getProperties().set("capsule", 1);  // the upper and lower half of one capsule (see PaperLookAndFeel)
        triplet.getProperties().set("capsule", 2);
        syncSwitch.toFront(false); // the knob's own bounds are the whole column: these must be above it to be clicked
        dotted.toFront(false);
        triplet.toFront(false);
    }

    // A link switch sits right after its knob's caption: "DEPTH [chain]" reads as "Depth, linked"
    inline void placeChain(juce::Component& chain, const juce::Label& caption)
    {
        juce::GlyphArrangement text;
        text.addLineOfText(fonts::semibold(theme::type::label).withExtraKerningFactor(0.09f), caption.getText().toUpperCase(), 0.f, 0.f);
        const int width = (int) std::ceil(text.getBoundingBox(0, -1, true).getWidth());
        chain.setBounds(caption.getBounds().getCentreX() + width / 2 + 3, caption.getY() - 1, 18, 18);
    }
}
