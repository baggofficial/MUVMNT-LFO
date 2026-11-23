#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// "Light paper" theme
namespace paper
{
    inline const juce::Colour bg     { 0xffebe8e1 };
    inline const juce::Colour panel  { 0xfffbfaf7 };
    inline const juce::Colour line   { 0xffd8d4ca };
    inline const juce::Colour accent { 0xffe4572e };
    inline const juce::Colour text   { 0xff25231f };
    inline const juce::Colour sub    { 0xff7d786c };
    inline const juce::Colour track  { 0xffe2ded3 };
    inline const juce::Colour amber  { 0xffe0a21b };

    // Same palette as the frequency-coloured waveform: red lows, green mids, blue highs
    inline juce::Colour bandColour(int band)
    {
        return band == 0 ? juce::Colour::fromFloatRGBA(0.90f, 0.20f, 0.20f, 1.f)
             : band == 1 ? juce::Colour::fromFloatRGBA(0.20f, 0.70f, 0.30f, 1.f)
                         : juce::Colour::fromFloatRGBA(0.15f, 0.45f, 0.95f, 1.f);
    }
}

class PaperLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PaperLookAndFeel()
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
};
