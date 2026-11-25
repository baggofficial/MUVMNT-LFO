#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "Fonts.h"

// An analog-style peak meter: a stack of small squares that light up from the bottom. Mostly green, then five yellow ones,
// and a single red square at the very top (lit at about 0 dBFS). Loud material lives in the top third, so the squares there are
// only 1.5 dB apart. Every square, the red one too, follows the signal: lit while the level is above it, dark as soon as it falls
// below (the fall, 80 dB/s, is done on the audio thread). This just reads the value and redraws when a square changes.
class LevelMeter : public juce::Component
{
public:
    LevelMeter(FreeLfoProcessor& p, bool isOutput, juce::String name) : proc(p), output(isOutput), label(std::move(name))
    {
        setInterceptsMouseClicks(false, false);
    }
    static constexpr int segments = 26;       // 20 green, 5 yellow, 1 red
    static constexpr float stepDb = 1.5f;
    static constexpr float redDb = -0.5f;     // the top (red) square lights from here; every square below is stepDb lower
    static float thresholdDb(int i) { return redDb - stepDb * (float) (segments - 1 - i); }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour(paper::bg);
        g.fillRoundedRectangle(b, 5.f);

        g.setColour(paper::sub.withAlpha(0.8f));
        g.setFont(fonts::semibold(8.5f));
        g.drawText(label, b.withHeight(12.f).translated(0.f, 2.f), juce::Justification::centred);

        const auto area = b.withTrimmedTop(17.f).reduced(3.f, 5.f);
        const float gap = 1.5f, cell = (area.getHeight() - gap * (segments - 1)) / (float) segments;
        const float w = juce::jmin(area.getWidth(), 10.f);
        const float x = area.getCentreX() - w * 0.5f;
        for (int i = 0; i < segments; ++i)
        {
            const bool red = i == segments - 1, yellow = i >= segments - 6 && !red;
            const auto on = red ? juce::Colour(0xffff4d4d) : yellow ? juce::Colour(0xfff0b429) : juce::Colour(0xff39c46a);
            const bool lit = i < litSegments;
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

    void animate() // one step of the editor's animation clock
    {
        const float lin = (output ? proc.meterOut : proc.meterIn).load(std::memory_order_relaxed);
        const float db = lin > 1e-5f ? 20.f * std::log10(lin) : -120.f;
        int lit = 0;
        for (int i = 0; i < segments; ++i) if (db >= thresholdDb(i)) lit = i + 1;
        if (lit != litSegments) { litSegments = lit; repaint(); }
    }

private:
    FreeLfoProcessor& proc;
    const bool output;
    const juce::String label;
    int litSegments = 0;
};
