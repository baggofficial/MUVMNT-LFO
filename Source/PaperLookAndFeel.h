#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Fonts.h"

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

// Curved arrow: undo (points left) or redo (points right). Greyed out when there is nothing to undo / redo.
class HistoryButton : public juce::Button
{
public:
    explicit HistoryButton(bool isRedo) : juce::Button(isRedo ? "redo" : "undo"), redo(isRedo)
    {
        setTooltip(isRedo ? "Redo (Shift+Ctrl/Cmd+Z)" : "Undo (Ctrl/Cmd+Z)");
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced(6.f, 7.f);
        const float a = !isEnabled() ? 0.28f : down ? 1.f : over ? 0.95f : 0.7f;
        g.setColour(paper::text.withAlpha(a));
        juce::Path arc;
        const auto ctr = b.getCentre();
        const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
        const float pi = juce::MathConstants<float>::pi;
        // an open arc over the top, ending in an arrowhead on one side
        arc.addCentredArc(ctr.x, ctr.y + 1.f, r, r, 0.f, -0.5f * pi, 0.95f * pi, true);
        g.strokePath(arc, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                     redo ? juce::AffineTransform() : juce::AffineTransform::scale(-1.f, 1.f, ctr.x, ctr.y));
        const float tipX = ctr.x + (redo ? -1.f : 1.f) * 0.0f, tipY = ctr.y + 1.f + r;
        juce::Path head;
        const float dir = redo ? -1.f : 1.f;
        head.addTriangle(tipX - dir * 3.5f, tipY - 3.5f, tipX - dir * 3.5f, tipY + 3.5f, tipX + dir * 1.5f, tipY);
        g.fillPath(head);
    }

private:
    bool redo;
};

// The Bezier-handles switch as an icon: a curve with two handles. Accent colour when on, grey when off.
class BezierToggle : public juce::ToggleButton
{
public:
    BezierToggle() { setClickingTogglesState(true); setTooltip("Bezier handles (off = simple one-diamond bends)"); setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(4.f);
        const bool on = getToggleState();
        const auto c = on ? paper::accent : paper::sub.withAlpha(isMouseOver() ? 0.9f : 0.6f);
        g.setColour(c);
        const juce::Point<float> a(b.getX(), b.getBottom() - 2.f), z(b.getRight(), b.getY() + 2.f);
        const juce::Point<float> ha(b.getX() + b.getWidth() * 0.45f, b.getBottom() - 2.f), hz(b.getRight() - b.getWidth() * 0.45f, b.getY() + 2.f);
        g.drawLine(a.x, a.y, ha.x, ha.y, 1.1f);
        g.drawLine(z.x, z.y, hz.x, hz.y, 1.1f);
        juce::Path curve;
        curve.startNewSubPath(a);
        curve.cubicTo(ha, hz, z);
        g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse(ha.x - 1.8f, ha.y - 1.8f, 3.6f, 3.6f);
        g.fillEllipse(hz.x - 1.8f, hz.y - 1.8f, 3.6f, 3.6f);
        g.fillRoundedRectangle(a.x - 2.2f, a.y - 2.2f, 4.4f, 4.4f, 1.f);
        g.fillRoundedRectangle(z.x - 2.2f, z.y - 2.2f, 4.4f, 4.4f, 1.f);
    }
};

// The Single / Multiband switch: one button that shows the current mode as an icon (one wave = Single, three stacked waves =
// Multiband) and flips to the other mode when clicked.
class ModeSwitchButton : public juce::Button
{
public:
    ModeSwitchButton() : juce::Button("mode") { setMouseCursor(juce::MouseCursor::PointingHandCursor); setMode(false); }

    void setMode(bool multiband)
    {
        multi = multiband;
        setTooltip(multi ? "Multiband: a curve for each of the lows, mids and highs. Click for Single band."
                         : "Single band: one curve for the whole signal. Click for Multiband.");
        repaint();
    }

    void paintButton(juce::Graphics& g, bool hover, bool down) override
    {
        const auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? paper::track : hover ? paper::bg : paper::panel);
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(hover ? paper::accent.withAlpha(0.7f) : paper::line);
        g.drawRoundedRectangle(b, 6.f, 1.f);

        g.setColour(paper::accent);
        const int numWaves = multi ? 3 : 1;
        const auto area = b.reduced(11.f, multi ? 7.f : 6.f);
        const float amp = multi ? 2.1f : 4.2f;
        for (int k = 0; k < numWaves; ++k)
        {
            const float cy = multi ? area.getY() + area.getHeight() * ((float) k + 0.5f) / 3.f : area.getCentreY();
            juce::Path wave;
            for (float x = 0.f; x <= 1.001f; x += 0.04f)
            {
                const float y = cy - amp * std::sin(x * juce::MathConstants<float>::twoPi + (float) k * 0.9f);
                if (x == 0.f) wave.startNewSubPath(area.getX() + x * area.getWidth(), y);
                else wave.lineTo(area.getX() + x * area.getWidth(), y);
            }
            g.strokePath(wave, juce::PathStrokeType(multi ? 1.4f : 2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

private:
    bool multi = false;
};

// Bypass: a power icon. Lit (accent) while the plugin is working; amber with a glow while it is bypassed.
class BypassButton : public juce::ToggleButton
{
public:
    BypassButton()
    {
        setClickingTogglesState(true);
        setTooltip("Bypass: pass the audio through untouched");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool hover, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced(0.5f);
        const bool bypassed = getToggleState();
        const auto amber = juce::Colour(0xfff0b429);
        g.setColour(bypassed ? amber.withAlpha(0.22f) : hover ? paper::bg : paper::panel);
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(bypassed ? amber : paper::line);
        g.drawRoundedRectangle(b, 6.f, 1.f);

        const auto c = bypassed ? amber : paper::accent.withAlpha(hover ? 1.f : 0.9f);
        const auto ctr = b.getCentre();
        const float r = 6.5f;
        if (bypassed)   // a soft glow
            for (const auto glow : { std::pair<float, float> { 3.f, 0.08f }, { 2.f, 0.13f } })
            {
                g.setColour(amber.withAlpha(glow.second));
                juce::Path p;
                p.addCentredArc(ctr.x, ctr.y + 0.5f, r, r, 0.f, 0.55f, juce::MathConstants<float>::twoPi - 0.55f, true);
                g.strokePath(p, juce::PathStrokeType(1.8f + glow.first, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        g.setColour(c);
        juce::Path power;   // the power symbol: an open ring with a line through the gap
        power.addCentredArc(ctr.x, ctr.y + 0.5f, r, r, 0.f, 0.55f, juce::MathConstants<float>::twoPi - 0.55f, true);
        power.startNewSubPath(ctr.x, ctr.y - r - 0.5f);
        power.lineTo(ctr.x, ctr.y - 0.5f);
        g.strokePath(power, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

// Output clipper: one button cycling Off / Soft / Hard. Dim while off, accent for soft, amber for hard.
class ClipButton : public juce::Button
{
public:
    ClipButton() : juce::Button("clip")
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Output clipper (multiband): keeps the output from going past 0 dB.\nClick to cycle Off / Soft / Hard");
    }
    int mode() const { return state; }
    void setMode(int m) { if (m != state) { state = m; repaint(); } }
    std::function<void(int)> onModeChange;

    void clicked() override
    {
        state = (state + 1) % 3;
        repaint();
        if (onModeChange) onModeChange(state);
    }

    void paintButton(juce::Graphics& g, bool hover, bool) override
    {
        const auto b = getLocalBounds().toFloat().reduced(0.5f);
        const auto amber = juce::Colour(0xfff0b429);
        const auto c = state == 0 ? paper::sub.withAlpha(hover ? 0.9f : 0.6f) : state == 1 ? paper::accent : amber;
        g.setColour(state == 0 ? (hover ? paper::bg : paper::panel) : c.withAlpha(0.2f));
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(state == 0 ? paper::line : c);
        g.drawRoundedRectangle(b, 6.f, 1.f);
        g.setColour(c);
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        g.drawText(state == 0 ? "CLIP" : state == 1 ? "SOFT" : "HARD", getLocalBounds(), juce::Justification::centred);
    }

private:
    int state = 0;
};

// The very small IN / OUT gain knobs under the meters. 0 dB points straight up; double-click or Alt-click goes back there.
class TrimKnob : public juce::Slider
{
public:
    TrimKnob()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setDoubleClickReturnValue(true, 0.0, juce::ModifierKeys::altModifier);
        setMouseDragSensitivity(220);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const auto readout = b.removeFromBottom(11.f);   // the value in dB, under the knob
        const auto c = b.getCentre();
        const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 1.5f;
        const float sweep = juce::MathConstants<float>::pi * 0.75f;   // 135 degrees to each side of straight up
        const float angle = (float) (valueToProportionOfLength(getValue()) * 2.0 - 1.0) * sweep;
        const bool hot = isMouseOverOrDragging();

        juce::Path track, value;
        track.addCentredArc(c.x, c.y, r, r, 0.f, -sweep, sweep, true);
        g.setColour(paper::line);
        g.strokePath(track, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (std::abs(angle) > 0.01f)   // lit from straight up (0 dB) to where the knob is
        {
            value.addCentredArc(c.x, c.y, r, r, 0.f, juce::jmin(0.f, angle), juce::jmax(0.f, angle), true);
            g.setColour(paper::accent);
            g.strokePath(value, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(paper::panel);
        g.fillEllipse(juce::Rectangle<float>(2.f * (r - 2.5f), 2.f * (r - 2.5f)).withCentre(c));
        g.setColour(hot ? paper::accent : paper::text.withAlpha(0.85f));
        const juce::Point<float> dir(std::sin(angle), -std::cos(angle));
        g.drawLine({ c + dir * 1.f, c + dir * (r - 1.5f) }, 1.8f);

        const double v = getValue();
        const juce::String text = std::abs(v) < 0.05 ? juce::String("0.0") : (v > 0.0 ? "+" : "") + juce::String(v, 1);
        g.setColour(hot ? paper::text : paper::sub);
        g.setFont(fonts::regular(9.f));
        g.drawFittedText(text, readout.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
};

// A rotary knob you can also type into: double-click the knob (or choose it from the right-click menu) to enter an
// exact value; right-click also resets to the default. (Cmd / Ctrl-click still resets, like any JUCE slider.)
class Knob : public juce::Slider
{
public:
    Knob() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void mouseDoubleClick(const juce::MouseEvent&) override { showTextBox(); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isPopupMenu()) { juce::Slider::mouseDown(e); return; }
        juce::PopupMenu m;
        m.setLookAndFeel(&getLookAndFeel());   // the menu is its own native window, so give it our font and colours explicitly
        m.addItem(1, "Enter value...");
        m.addItem(2, "Reset to default", isDoubleClickReturnEnabled());
        juce::Component::SafePointer<Knob> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(e.getScreenPosition(), e.getScreenPosition()).expanded(1)),
                        [safe](int r) {
                            if (safe == nullptr) return;
                            if (r == 1) safe->showTextBox();
                            else if (r == 2) safe->setValue(safe->getDoubleClickReturnValue(), juce::sendNotificationSync);
                        });
    }
};

class PaperLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PaperLookAndFeel() { setDefaultSansSerifTypeface(fonts::regularFace()); }

    // Small uppercase captions (knob names); the value boxes inside sliders keep a normal readable font
    void drawLabel(juce::Graphics& g, juce::Label& l) override
    {
        if (dynamic_cast<juce::Slider*>(l.getParentComponent()) != nullptr || dynamic_cast<juce::ComboBox*>(l.getParentComponent()) != nullptr
            || l.isBeingEdited())
        {
            LookAndFeel_V4::drawLabel(g, l);
            return;
        }
        g.setColour(l.findColour(juce::Label::textColourId));
        g.setFont(fonts::semibold(10.5f).withExtraKerningFactor(0.09f));
        g.drawText(l.getText().toUpperCase(), l.getLocalBounds(), l.getJustificationType(), false);
    }

    juce::Font getLabelFont(juce::Label&) override { return fonts::regular(13.f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return fonts::semibold(13.f); }
    juce::Font getPopupMenuFont() override { return fonts::regular(13.f); }

    // The value readout under a knob: when you type into it, the editor is a narrow, centred box in the caption font
    struct ValueLabel : public juce::Label
    {
        void resized() override
        {
            juce::Label::resized();
            if (auto* ed = getCurrentTextEditor())
                ed->setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmin(60, getWidth()), getHeight()));
        }
        juce::TextEditor* createEditorComponent() override
        {
            auto* ed = juce::Label::createEditorComponent();
            ed->setFont(fonts::semibold(11.5f));
            ed->setJustification(juce::Justification::centred);
            ed->setIndents(0, 0);
            ed->setBorder(juce::BorderSize<int>(0));
            return ed;
        }
    };
    juce::Label* createSliderTextBox(juce::Slider& s) override
    {
        if (s.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
        {
            auto* l = new ValueLabel();
            l->setJustificationType(juce::Justification::centred);
            l->setKeyboardType(juce::TextInputTarget::decimalKeyboard);
            l->setColour(juce::Label::textColourId, s.findColour(juce::Slider::textBoxTextColourId));
            l->setColour(juce::Label::textWhenEditingColourId, s.findColour(juce::Slider::textBoxTextColourId));
            l->setColour(juce::Label::outlineWhenEditingColourId, paper::accent);
            return l;
        }
        return LookAndFeel_V4::createSliderTextBox(s);
    }

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
        g.setFont(fonts::regular(13.f));
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

    juce::Font getComboBoxFont(juce::ComboBox&) override { return fonts::regular(14.f); }

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
