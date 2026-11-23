#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Two themes: "light paper" (default) and a dark one inspired by After Effects' graph editor
// (charcoal panels, near-black graph area, blue highlight, yellow keyframe handles).
// The colours are plain variables that setDark() reassigns, so everything that draws with paper::xxx follows.
namespace paper
{
    inline bool dark = false;
    inline int version = 0; // bumps on every theme change, so cached drawings know to redraw
    inline juce::Colour bg     { 0xffebe8e1 };   // curve area / outer frame
    inline juce::Colour panel  { 0xfffbfaf7 };   // window panel, buttons, chips
    inline juce::Colour line   { 0xffd8d4ca };
    inline juce::Colour accent { 0xffe4572e };
    inline juce::Colour text   { 0xff25231f };
    inline juce::Colour sub    { 0xff7d786c };
    inline juce::Colour track  { 0xffe2ded3 };
    inline juce::Colour amber  { 0xffe0a21b };

    inline void setDark(bool on)
    {
        dark = on;
        if (on)
        {
            bg     = juce::Colour(0xff1b1b1b);  // like AE's graph background
            panel  = juce::Colour(0xff2a2a2a);  // AE panel grey
            line   = juce::Colour(0xff434343);
            accent = juce::Colour(0xff4a9df8);  // AE selection blue
            text   = juce::Colour(0xffd9d9d9);
            sub    = juce::Colour(0xff8f8f8f);
            track  = juce::Colour(0xff3d3d3d);
            amber  = juce::Colour(0xfff0b429);
        }
        else
        {
            bg     = juce::Colour(0xffebe8e1);
            panel  = juce::Colour(0xfffbfaf7);
            line   = juce::Colour(0xffd8d4ca);
            accent = juce::Colour(0xffe4572e);
            text   = juce::Colour(0xff25231f);
            sub    = juce::Colour(0xff7d786c);
            track  = juce::Colour(0xffe2ded3);
            amber  = juce::Colour(0xffe0a21b);
        }
        ++version;
    }

    // Same palette as the frequency-coloured waveform: red lows, green mids, blue highs
    inline juce::Colour bandColour(int band)
    {
        if (dark)
            return band == 0 ? juce::Colour::fromFloatRGBA(0.96f, 0.38f, 0.34f, 1.f)
                 : band == 1 ? juce::Colour::fromFloatRGBA(0.36f, 0.82f, 0.45f, 1.f)
                             : juce::Colour::fromFloatRGBA(0.33f, 0.60f, 1.00f, 1.f);
        return band == 0 ? juce::Colour::fromFloatRGBA(0.90f, 0.20f, 0.20f, 1.f)
             : band == 1 ? juce::Colour::fromFloatRGBA(0.20f, 0.70f, 0.30f, 1.f)
                         : juce::Colour::fromFloatRGBA(0.15f, 0.45f, 0.95f, 1.f);
    }

    // Grid lines, guides: dark ink on the light theme, light ink on the dark one
    inline juce::Colour ink(float alpha) { return dark ? juce::Colours::white.withAlpha(alpha * 0.7f) : juce::Colours::black.withAlpha(alpha); }

    // Curve editor grid: clearly visible from a distance. Major lines (every quarter of the cycle, and the frame) are stronger.
    inline juce::Colour gridMajor() { return dark ? juce::Colours::white.withAlpha(0.26f) : juce::Colours::black.withAlpha(0.24f); }
    inline juce::Colour gridMinor() { return dark ? juce::Colours::white.withAlpha(0.13f) : juce::Colours::black.withAlpha(0.11f); }

    // A colour for outlines of the band curves: a touch darker on light, a touch brighter on dark
    inline juce::Colour strong(juce::Colour c) { return dark ? c.brighter(0.15f) : c.darker(0.2f); }

    // Text colour for a band name
    inline juce::Colour bandText(int band) { return dark ? bandColour(band).brighter(0.1f) : bandColour(band).darker(0.25f); }
}

// A small half-filled circle: click to switch between the light and the dark theme
class ThemeToggle : public juce::Button
{
public:
    ThemeToggle() : juce::Button("theme") { setTooltip("Light / dark mode"); }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto b = getLocalBounds().toFloat().reduced(4.f);
        g.setColour(paper::text.withAlpha(over ? 1.f : 0.7f));
        g.drawEllipse(b, 1.6f);
        juce::Path half;
        half.addPieSegment(b, 0.f, juce::MathConstants<float>::pi, 0.f); // the right half
        g.fillPath(half);
    }
};

class PaperLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PaperLookAndFeel() { applyColours(); }

    // (re)load the theme colours; call after paper::setDark(), then sendLookAndFeelChange() on the editor
    void applyColours()
    {
        using namespace juce;
        setColour(Slider::textBoxTextColourId, paper::text);
        setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour(Label::textColourId, paper::sub);
        setColour(ComboBox::textColourId, paper::text);
        setColour(ComboBox::outlineColourId, paper::line);
        setColour(ComboBox::backgroundColourId, paper::panel);
        setColour(ComboBox::arrowColourId, paper::sub);
        setColour(PopupMenu::backgroundColourId, paper::panel);
        setColour(PopupMenu::textColourId, paper::text);
        setColour(PopupMenu::highlightedBackgroundColourId, paper::accent);
        setColour(PopupMenu::highlightedTextColourId, Colours::white);
        setColour(TextButton::textColourOffId, paper::text);
        setColour(TextButton::textColourOnId, Colours::white);
        setColour(ToggleButton::textColourId, paper::sub);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override
    {
        auto b = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(6.f);
        const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 3.f;
        const auto c = b.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track, value;
        track.addCentredArc(c.x, c.y, r, r, 0.f, startAngle, endAngle, true);
        value.addCentredArc(c.x, c.y, r, r, 0.f, startAngle, angle, true);
        g.setColour(paper::track);
        g.strokePath(track, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(paper::accent);
        g.strokePath(value, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float a = angle - juce::MathConstants<float>::halfPi;
        g.setColour(paper::text);
        g.drawLine(c.x + std::cos(a) * r * 0.35f, c.y + std::sin(a) * r * 0.35f,
                   c.x + std::cos(a) * (r - 4.f), c.y + std::sin(a) * (r - 4.f), 2.f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& bt, bool, bool) override
    {
        auto b = bt.getLocalBounds().toFloat();
        auto sw = b.removeFromRight(30.f).withSizeKeepingCentre(28.f, 14.f);
        g.setColour(bt.getToggleState() ? paper::accent : paper::track);
        g.fillRoundedRectangle(sw, 7.f);
        const float knobX = bt.getToggleState() ? sw.getRight() - 12.f : sw.getX() + 2.f;
        g.setColour(juce::Colours::white);
        g.fillEllipse(knobX, sw.getY() + 2.f, 10.f, 10.f);
        g.setColour(paper::sub);
        g.setFont(13.f);
        g.drawText(bt.getButtonText(), b.withTrimmedRight(6.f), juce::Justification::centredRight);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& bt, const juce::Colour&, bool hover, bool down) override
    {
        auto b = bt.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = bt.getToggleState();
        g.setColour(on ? paper::accent : down ? paper::track : hover ? paper::bg : paper::panel);
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(on ? paper::accent : paper::line);
        g.drawRoundedRectangle(b, 6.f, 1.f);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto b = juce::Rectangle<float>(0.f, 0.f, (float) w, (float) h).reduced(0.5f);
        g.setColour(paper::panel);
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(paper::line);
        g.drawRoundedRectangle(b, 6.f, 1.f);
        juce::Path p;
        const float cx = (float) w - 14.f, cy = (float) h * 0.5f;
        p.startNewSubPath(cx - 4.f, cy - 2.f); p.lineTo(cx, cy + 2.f); p.lineTo(cx + 4.f, cy - 2.f);
        g.setColour(paper::sub);
        g.strokePath(p, juce::PathStrokeType(1.5f));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(14.f); }

    // Bar-style sliders are used as small draggable number boxes (drag up / down to change the value)
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (!s.isBar())
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }
        auto b = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(0.5f);
        g.setColour(s.isMouseOverOrDragging() ? paper::bg : paper::panel);
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(s.isMouseButtonDown() ? paper::accent : paper::line);
        g.drawRoundedRectangle(b, 6.f, 1.f);
    }
};
