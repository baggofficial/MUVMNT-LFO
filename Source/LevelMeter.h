#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "Fonts.h"

// An analog-style peak meter: a stack of small squares that light up from the bottom. Mostly green, then three yellow ones,
// and a single red square at the very top (lit at about 0 dBFS, and held for a moment so a clip is not missed).
// The fall (24 dB/s) is done on the audio thread, so this just reads the value and redraws when a square changes.
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter(FreeLfoProcessor& p, bool isOutput, juce::String name) : proc(p), output(isOutput), label(std::move(name))
    {
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }
    ~LevelMeter() override { stopTimer(); }

    static constexpr int segments = 15;       // 11 green, 3 yellow, 1 red
    static constexpr float lowestDb = -42.f;  // first square; 3 dB per square: the red one lights at 0 dB (-0.5 dB and up)

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour(paper::bg);
        g.fillRoundedRectangle(b, 5.f);

        g.setColour(paper::sub.withAlpha(0.8f));
        g.setFont(fonts::semibold(8.5f));
        g.drawText(label, b.withHeight(12.f).translated(0.f, 2.f), juce::Justification::centred);

        const auto area = b.withTrimmedTop(17.f).reduced(3.f, 5.f);
        const float gap = 2.f, cell = (area.getHeight() - gap * (segments - 1)) / (float) segments;
        const float w = juce::jmin(area.getWidth(), 10.f);
        const float x = area.getCentreX() - w * 0.5f;
        for (int i = 0; i < segments; ++i)
        {
            const bool red = i == segments - 1, yellow = i >= segments - 4 && !red;
            const auto on = red ? juce::Colour(0xffff4d4d) : yellow ? juce::Colour(0xfff0b429) : juce::Colour(0xff39c46a);
            const bool lit = (red && clipHold > 0.f) || i < litSegments;
            const float y = area.getBottom() - (float) (i + 1) * cell - (float) i * gap;
            const auto r = juce::Rectangle<float>(x, y, w, cell);
            if (lit)   // a soft glow: a few wider, fainter squares underneath
                for (const auto glow : { std::pair<float, float> { 3.5f, 0.07f }, { 2.4f, 0.12f }, { 1.3f, 0.2f } })
                {
                    g.setColour(on.withAlpha(glow.second * (paper::dark ? 1.4f : 0.9f)));
                    g.fillRoundedRectangle(r.expanded(glow.first, glow.first * 0.7f), 1.5f + glow.first);
                }
            g.setColour(lit ? on.brighter(0.1f) : on.withAlpha(paper::dark ? 0.13f : 0.16f));
            g.fillRoundedRectangle(r, 1.5f);
        }
    }

private:
    void timerCallback() override
    {
        const float lin = (output ? proc.meterOut : proc.meterIn).load(std::memory_order_relaxed);
        const float db = lin > 1e-5f ? 20.f * std::log10(lin) : -120.f;
        int lit = 0;
        for (int i = 0; i < segments - 1; ++i)               // all but the red one: lit from lowestDb in 3 dB steps
            if (db >= lowestDb + 3.f * (float) i) lit = i + 1;
        const bool clipping = db >= -0.5f;
        clipHold = clipping ? 1.5f : juce::jmax(0.f, clipHold - 1.f / 30.f);
        if (lit != litSegments || (clipHold > 0.f) != wasHolding) { litSegments = lit; wasHolding = clipHold > 0.f; repaint(); }
    }

    FreeLfoProcessor& proc;
    const bool output;
    const juce::String label;
    int litSegments = 0;
    float clipHold = 0.f;
    bool wasHolding = false;
};
