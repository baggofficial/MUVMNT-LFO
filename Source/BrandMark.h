#pragma once
// The product's name in the top left corner: the brand as the wordmark, "LFO" as the small descriptor beside it.
// This is a placeholder set in the interface font. When the final MUVMNT wordmark exists (a vector path), draw it in
// place of the first text here; nothing else in the layout needs to change.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Fonts.h"
#include "Theme.h"

class BrandMark : public juce::Component
{
public:
    static constexpr int width = 150;

    BrandMark()
    {
        setInterceptsMouseClicks(false, false);
        setTitle("MUVMNT LFO");
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const auto word = fonts::semibold(theme::type::wordmark).withExtraKerningFactor(0.01f);
        juce::GlyphArrangement measure;
        measure.addLineOfText(word, "MUVMNT", 0.f, 0.f);
        const float wordWidth = std::ceil(measure.getBoundingBox(0, -1, true).getWidth()) + 1.f;
        g.setColour(theme::text);
        g.setFont(word);
        g.drawText("MUVMNT", b.removeFromLeft(wordWidth + 2.f), juce::Justification::centredLeft, false);
        b.removeFromLeft(7.f);
        g.setColour(theme::sub);
        g.setFont(fonts::regular(theme::type::descriptor).withExtraKerningFactor(0.18f));
        g.drawText("LFO", b.translated(0.f, 2.f), juce::Justification::centredLeft, false);
    }
};
