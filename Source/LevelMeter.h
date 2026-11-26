#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "Fonts.h"

// A studio-style peak meter: one narrow bar in a thin frame, cut into fine segments, with a clip lamp above it. The colour
// belongs to the level, not to the bar: muted green, turning yellow and then orange near the top; the lamp is red and lights
// at about 0 dBFS. Everything follows the signal (the 80 dB/s fall is done on the audio thread; there is no peak hold).
// This just reads the value and redraws when the bar moves by half a decibel.
class LevelMeter : public juce::Component
{
public:
    LevelMeter(FreeLfoProcessor& p, bool isOutput, juce::String name) : proc(p), output(isOutput), label(std::move(name))
    {
        setInterceptsMouseClicks(false, false);
    }
    static constexpr float bottomDb = -38.f;  // an empty bar
    static constexpr float redDb = -0.5f;     // the clip lamp lights from here, and the bar is full
    static constexpr float stepDb = 0.5f;
    static constexpr int steps = 75;          // (redDb - bottomDb) / stepDb

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(theme::sub);
        g.setFont(fonts::semibold(8.5f).withExtraKerningFactor(0.04f));
        g.drawText(label, b.removeFromTop(13.f), juce::Justification::centred);
        b.removeFromTop(1.f);

        const auto frame = b.withSizeKeepingCentre(12.f, b.getHeight());
        g.setColour(theme::bg);
        g.fillRoundedRectangle(frame, 2.5f);
        g.setColour(theme::line);
        g.drawRoundedRectangle(frame.reduced(0.5f), 2.5f, 1.f);

        auto inside = frame.reduced(2.5f);
        const auto lamp = inside.removeFromTop(4.f);
        inside.removeFromTop(2.f);
        g.setColour(lit >= steps ? theme::levelOver : theme::levelOver.withAlpha(theme::dark ? 0.2f : 0.18f));
        g.fillRect(lamp);

        // the scale beside the bar: quiet ticks at -6, -12, -24 and -36 dB
        g.setColour(theme::muted.withAlpha(0.8f));
        for (const float db : { -6.f, -12.f, -24.f, -36.f })
        {
            const float y = std::floor(inside.getBottom() - inside.getHeight() * (db - bottomDb) / (redDb - bottomDb));
            g.fillRect(juce::Rectangle<float>(frame.getX() - 3.f, y, 2.f, 1.f));
            g.fillRect(juce::Rectangle<float>(frame.getRight() + 1.f, y, 2.f, 1.f));
        }

        if (lit <= 0) return;
        auto at = [](float db) { return (double) juce::jlimit(0.f, 1.f, (db - bottomDb) / (redDb - bottomDb)); };
        juce::ColourGradient fill(theme::levelLow, 0.f, inside.getBottom(), theme::levelNear, 0.f, inside.getY(), false);
        fill.addColour(at(-18.f), theme::levelNormal);
        fill.addColour(at(-9.f), theme::levelNormal);
        fill.addColour(at(-6.f), theme::levelHigh);
        fill.addColour(at(-2.5f), theme::levelNear);
        const float height = inside.getHeight() * (float) juce::jmin(lit, steps) / (float) steps;
        g.setGradientFill(fill);
        g.fillRect(inside.withTop(inside.getBottom() - height));

        g.setColour(theme::bg.withAlpha(0.85f)); // fine segments: a 1 px cut every 3 px
        for (float y = inside.getBottom() - 3.f; y > inside.getBottom() - height; y -= 3.f)
            g.fillRect(juce::Rectangle<float>(inside.getX(), y, inside.getWidth(), 1.f));
    }

    void animate() // one step of the editor's animation clock
    {
        const float lin = (output ? proc.meterOut : proc.meterIn).load(std::memory_order_relaxed);
        const float db = lin > 1e-5f ? 20.f * std::log10(lin) : -120.f;
        const int now = db >= redDb ? steps : db <= bottomDb ? 0 : juce::jmin(steps - 1, (int) ((db - bottomDb) / stepDb) + 1);
        if (now != lit) { lit = now; repaint(); }
    }

private:
    FreeLfoProcessor& proc;
    const bool output;
    const juce::String label;
    int lit = 0; // how much of the bar is on, in steps of half a decibel; "steps" also lights the clip lamp
};
