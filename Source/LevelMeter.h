#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "Fonts.h"

// A studio-style peak meter: one narrow bar in a thin frame, cut into fine segments, with a clip lamp above it and its
// name below. Placed at the same height as the graph, the bar spans exactly what the curve does, from 0% to 100%.
// In Full Band the scale ends at 0 dB: the plugin can only turn the sound down there, so a full bar is the top, level
// with the top of the curve. In Multiband, where re-summed bands and boosts can go over, it runs up to +6 dB, with a
// bright line at 0 dB and a faintly red zone above it; whatever reaches into that zone is red.
// The colour belongs to the level, not to the bar: muted green, turning yellow and then orange just under 0 dB.
// The bar follows the signal (the 80 dB/s fall is done on the audio thread; there is no peak hold). The lamp is the one
// thing that holds: any peak above 0 dBFS keeps it lit for two seconds, so even a single short one is seen.
// This just reads the values and redraws when the bar moves by half a decibel or the lamp changes.
class LevelMeter : public juce::Component
{
public:
    static constexpr float fullBandTopDb = 0.f, multibandTopDb = 6.f;

    LevelMeter(FreeLfoProcessor& p, bool isOutput, juce::String name, float fullAtDb)
        : topDb(fullAtDb), proc(p), output(isOutput), label(std::move(name)), seenOvers(overs().load(std::memory_order_relaxed))
    {
        setInterceptsMouseClicks(false, false);
    }
    static constexpr float bottomDb = -36.f;  // an empty bar
    static constexpr float stepDb = 0.5f;
    const float topDb;                        // a full one
    static constexpr double lampHoldMs = 2000.0;

    // Where the bar runs: the graph's plot has the same insets (10 above, 14 below), see EnvelopeEditor::plotBounds
    juce::Rectangle<float> barArea() const
    {
        return getLocalBounds().toFloat().withSizeKeepingCentre(7.f, (float) getHeight()).withTrimmedTop(10.f).withTrimmedBottom(14.f);
    }
    bool isClipLampLit() const { return clipped; }
    int litSteps() const { return lit; }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour(theme::sub);
        g.setFont(fonts::semibold(8.5f).withExtraKerningFactor(0.04f));
        g.drawText(label, b.withTop(b.getBottom() - 11.f), juce::Justification::centred);

        const auto bar = barArea();
        const auto frame = juce::Rectangle<float>(12.f, bar.getBottom() + 2.5f).withX(b.getCentreX() - 6.f);
        g.setColour(theme::bg);
        g.fillRoundedRectangle(frame, 2.5f);
        g.setColour(theme::line);
        g.drawRoundedRectangle(frame.reduced(0.5f), 2.5f, 1.f);

        const auto lamp = bar.withY(2.5f).withHeight(5.f); // in the room above the bar
        const auto inside = bar;
        g.setColour(clipped ? theme::levelOver : theme::levelOver.withAlpha(theme::dark ? 0.2f : 0.18f));
        g.fillRect(lamp);

        const bool hasRoomAbove = topDb > 0.f;
        auto yOf = [&](float db) { return std::floor(inside.getBottom() - inside.getHeight() * (db - bottomDb) / (topDb - bottomDb)); };
        const float zeroY = yOf(0.f);
        if (hasRoomAbove)
        {
            g.setColour(theme::levelOver.withAlpha(theme::dark ? 0.14f : 0.12f));
            g.fillRect(inside.withBottom(zeroY));
        }

        // the scale beside the bar: quiet ticks at -6, -12 and -24 dB, a longer bright one at 0 dB
        g.setColour(theme::muted.withAlpha(0.8f));
        for (const float db : { -6.f, -12.f, -24.f })
        {
            g.fillRect(juce::Rectangle<float>(frame.getX() - 3.f, yOf(db), 2.f, 1.f));
            g.fillRect(juce::Rectangle<float>(frame.getRight() + 1.f, yOf(db), 2.f, 1.f));
        }
        if (hasRoomAbove)
        {
            g.setColour(theme::text);
            g.fillRect(juce::Rectangle<float>(frame.getX() - 5.f, zeroY, 5.f, 1.f));
            g.fillRect(juce::Rectangle<float>(frame.getRight(), zeroY, 5.f, 1.f));
        }

        if (lit > 0)
        {
            auto at = [](float db) { return (double) juce::jlimit(0.f, 1.f, (db - bottomDb) / -bottomDb); };
            juce::ColourGradient fill(theme::levelLow, 0.f, inside.getBottom(), theme::levelNear, 0.f, zeroY, false);
            fill.addColour(at(-18.f), theme::levelNormal);
            fill.addColour(at(-9.f), theme::levelNormal);
            fill.addColour(at(-6.f), theme::levelHigh);
            fill.addColour(at(-2.f), theme::levelNear);
            const float top = inside.getBottom() - inside.getHeight() * (float) juce::jmin(lit, steps()) / (float) steps();
            g.setGradientFill(fill);
            g.fillRect(inside.withTop(juce::jmax(top, zeroY)));
            if (top < zeroY) // over: that part is plain red
            {
                g.setColour(theme::levelOver);
                g.fillRect(inside.withTop(top).withBottom(zeroY));
            }
            g.setColour(theme::bg.withAlpha(0.85f)); // fine segments: a 1 px cut every 3 px
            for (float y = inside.getBottom() - 3.f; y > top; y -= 3.f)
                g.fillRect(juce::Rectangle<float>(inside.getX(), y, inside.getWidth(), 1.f));
        }
        if (hasRoomAbove)
        {
            g.setColour(theme::text.withAlpha(0.75f)); // the 0 dB line stays visible over the bar
            g.fillRect(juce::Rectangle<float>(inside.getX(), zeroY, inside.getWidth(), 1.f));
        }
    }

    void animate() // one step of the editor's animation clock
    {
        const float lin = (output ? proc.meterOut : proc.meterIn).load(std::memory_order_relaxed);
        const float db = lin > 1e-5f ? 20.f * std::log10(lin) : -120.f;
        // rounded up to a whole step, so that 0 dB ends exactly on its line and only a real over shows red above it
        const int now = db <= bottomDb ? 0 : juce::jmin(steps(), (int) std::ceil((db - bottomDb) / stepDb - 0.01f));

        const double time = juce::Time::getMillisecondCounterHiRes();
        const unsigned count = overs().load(std::memory_order_relaxed);
        if (count != seenOvers) { seenOvers = count; lampOffAt = time + lampHoldMs; }
        const bool lampNow = time < lampOffAt;

        if (now != lit || lampNow != clipped) { lit = now; clipped = lampNow; repaint(); }
    }

private:
    int steps() const { return juce::roundToInt((topDb - bottomDb) / stepDb); }
    const std::atomic<unsigned>& overs() const { return output ? proc.oversOut : proc.oversIn; }

    FreeLfoProcessor& proc;
    const bool output;
    const juce::String label;
    unsigned seenOvers;
    double lampOffAt = 0.0;
    bool clipped = false;
    int lit = 0; // how much of the bar is on, in steps of half a decibel
};
