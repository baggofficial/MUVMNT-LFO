#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Fonts.h"
#include "Theme.h"

// The animation speed, shown as its number: click to switch between 60 and 30 frames a second
class FrameRateButton : public juce::Button
{
public:
    FrameRateButton() : juce::Button("animation speed")
    {
        setTooltip("Animation speed of this window: 60 fps is smoother, 30 fps uses much less CPU. Click to switch. The sound is not affected.");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    int shown() const { return fps; }
    void setShown(int framesPerSecond) { if (framesPerSecond != fps) { fps = framesPerSecond; repaint(); } }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? theme::track : over ? theme::bg : theme::panel);
        g.fillRoundedRectangle(b, 5.f);
        g.setColour(theme::line);
        g.drawRoundedRectangle(b, 5.f, 1.f);
        g.setColour(theme::text.withAlpha(over ? 1.f : 0.75f));
        g.setFont(fonts::semibold(11.f));
        g.drawText(juce::String(fps), b.withTrimmedBottom(8.f).translated(0.f, 1.f), juce::Justification::centred);
        g.setColour(theme::sub);
        g.setFont(fonts::regular(8.f));
        g.drawText("fps", b.withTrimmedTop(13.f), juce::Justification::centred);
    }

private:
    int fps = 60;
};

// A small half-filled circle: click to switch between the light and the dark theme
class ThemeToggle : public juce::Button
{
public:
    ThemeToggle() : juce::Button("theme") { setTooltip("Light / dark mode"); }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        auto b = getLocalBounds().toFloat().reduced(4.f);
        g.setColour(theme::text.withAlpha(over ? 1.f : 0.7f));
        g.drawEllipse(b, 1.6f);
        juce::Path half;
        half.addPieSegment(b, 0.f, juce::MathConstants<float>::pi, 0.f); // the right half
        g.fillPath(half);
    }
};

// "Curve from kick": a two-part switch (Simple | Detailed on screen; "safe" and "precise" in the code) in its own group at
// the end of the control strip. Gold (the kick) on hover.
class KickButton : public juce::Button
{
public:
    const bool precise;

    explicit KickButton(bool isPrecise) : juce::Button(isPrecise ? "precise curve from kick" : "safe curve from kick"), precise(isPrecise)
    {
        setTooltip(isPrecise ? "Detailed: more points. Uses your kick and your sound to keep more of your sound. Press while playing."
                             : "Simple: a few points. Gives your kick plenty of room. Press while playing.");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    // The two buttons sit edge to edge and read as one control: Safe is its left half, Precise its right half
    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.f, 0.5f);
        if (precise) b.removeFromRight(0.5f); else b.removeFromLeft(0.5f);
        juce::Path half;
        half.addRoundedRectangle(b.getX(), b.getY(), b.getWidth(), b.getHeight(), theme::size::radius, theme::size::radius,
                                 !precise, precise, !precise, precise);
        g.setColour(down ? theme::amber.withAlpha(0.28f) : over ? theme::raised : theme::panel);
        g.fillPath(half);
        g.setColour(over || down ? theme::amber.withAlpha(0.85f) : theme::line.brighter(theme::dark ? 0.12f : 0.f));
        g.strokePath(half, juce::PathStrokeType(theme::size::border));
        g.setColour(over || down ? (theme::dark ? theme::amber : theme::amber.darker(0.6f)) : theme::text);
        g.setFont(fonts::semibold(10.f).withExtraKerningFactor(0.05f));
        g.drawText(precise ? "DETAILED" : "SIMPLE", b, juce::Justification::centred);
    }
};

// Sync / Hz as a small upright slider beside the Rate knob: a thumb that sits on a note (Sync, the top) or on "Hz" (the
// bottom). Click either half, or drag the thumb. It is still a two-item ComboBox underneath, so the parameter attachments
// (and therefore automation, undo in the DAW and project files) work exactly as they did with the dropdown.
class SyncSwitch : public juce::ComboBox
{
public:
    static constexpr int width = 26, height = 48;

    SyncSwitch()
    {
        addItemList({ "Sync", "Hz" }, 1);
        setScrollWheelEnabled(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Follow the song's tempo, or set a speed in Hz.");
        hideText();
    }

    void mouseDown(const juce::MouseEvent& e) override { choose(e); }   // never opens a menu
    void mouseDrag(const juce::MouseEvent& e) override { choose(e); }
    void mouseUp(const juce::MouseEvent&) override {}
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void lookAndFeelChanged() override { juce::ComboBox::lookAndFeelChanged(); hideText(); }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced(0.5f);
        const bool hz = getSelectedItemIndex() == 1, over = isMouseOver(true);
        g.setColour(theme::bg);
        g.fillRoundedRectangle(b, 7.f);
        g.setColour(over ? theme::sub.withAlpha(0.6f) : theme::line.brighter(theme::dark ? 0.12f : 0.f));
        g.drawRoundedRectangle(b, 7.f, theme::size::border);

        auto halves = b.reduced(2.f);
        const auto top = halves.removeFromTop(halves.getHeight() * 0.5f);
        const auto thumb = (hz ? halves : top).reduced(0.f, 0.5f);
        g.setGradientFill(juce::ColourGradient(theme::dialTop.brighter(theme::dark ? 0.12f : 0.f), 0.f, thumb.getY(),
                                               theme::dialBottom.brighter(theme::dark ? 0.08f : 0.f), 0.f, thumb.getBottom(), false));
        g.fillRoundedRectangle(thumb, theme::size::radius);
        g.setColour(theme::sub.withAlpha(0.55f));
        g.drawRoundedRectangle(thumb, theme::size::radius, theme::size::border);

        // Sync: two beamed notes
        const auto lit = [&](bool on) { return on ? theme::text : theme::muted; };
        g.setColour(lit(!hz));
        {
            const auto c = top.getCentre();
            for (const float x : { -4.f, 3.5f })
            {
                juce::Path head;
                head.addEllipse(-2.6f, -1.9f, 5.2f, 3.8f);
                g.fillPath(head, juce::AffineTransform::rotation(-0.35f).translated(c.x + x - 0.6f, c.y + 4.2f));
                g.fillRect(juce::Rectangle<float>(c.x + x + 0.6f, c.y - 5.6f, 1.2f, 9.8f)); // stem
            }
            juce::Path beam;
            beam.addRectangle(c.x - 3.4f, c.y - 6.2f, 8.7f, 2.4f);
            g.fillPath(beam);
        }
        g.setColour(lit(hz));
        g.setFont(fonts::semibold(10.5f));
        g.drawText("Hz", halves, juce::Justification::centred);
    }

private:
    void choose(const juce::MouseEvent& e)
    {
        if (!isEnabled() || e.mods.isPopupMenu()) return;
        setSelectedItemIndex(e.position.y < (float) getHeight() * 0.5f ? 0 : 1, juce::sendNotificationSync);
        repaint();
    }
    void hideText() { for (auto* child : getChildren()) child->setVisible(false); } // the ComboBox's own text label
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
        g.setColour(theme::text.withAlpha(a));
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
    BezierToggle() { setClickingTogglesState(true); setTooltip("On: two handles for shaping curves. Off: one bend handle per section."); setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(4.f);
        const bool on = getToggleState();
        // on: the icon in the yellow of the handles it switches on, in a lit chip
        const auto yellow = theme::dark ? theme::handle : theme::handle.darker(0.25f);
        if (on)
        {
            const auto chip = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(theme::raised);
            g.fillRoundedRectangle(chip, theme::size::radius);
            g.setColour(yellow.withAlpha(0.55f));
            g.drawRoundedRectangle(chip, theme::size::radius, theme::size::border);
        }
        const auto c = on ? yellow : theme::sub.withAlpha(isMouseOver() ? 1.f : 0.75f);
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

// The Single / Multiband selector: both choices are always on screen, each with its icon (one wave / three stacked waves)
// and its name, and the one in use is lit. Click the other to switch; clicking the lit one does nothing. While Multiband
// is in use its three waves wear the band colours; otherwise they are as quiet as its name.
class ModeSwitchButton : public juce::Button
{
public:
    static constexpr int width = 210, singleWidth = 98;

    ModeSwitchButton() : juce::Button("mode") { setMouseCursor(juce::MouseCursor::PointingHandCursor); setMode(false); }

    void setMode(bool multiband)
    {
        multi = multiband;
        setTooltip("Full Band: one curve for the whole sound.  Multiband: a curve each for the lows, mids and highs.");
        repaint();
    }
    bool isMultiband() const { return multi; }

    // onClick means "switch to the other mode", so only a click on the half that is not in use may reach it
    void mouseDown(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (isEnabled() && !e.mods.isPopupMenu() && getLocalBounds().contains(e.getPosition()) && (e.x >= singleWidth) != multi
            && onClick != nullptr)
            onClick();
        repaint();
    }
    void mouseMove(const juce::MouseEvent& e) override { setHoverHalf(e.x >= singleWidth ? 1 : 0); }
    void mouseExit(const juce::MouseEvent& e) override { setHoverHalf(-1); juce::Button::mouseExit(e); }

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(theme::bg);
        g.fillRoundedRectangle(b, 7.f);
        g.setColour(theme::line.brighter(theme::dark ? 0.12f : 0.f));
        g.drawRoundedRectangle(b, 7.f, theme::size::border);

        for (int half = 0; half < 2; ++half)
        {
            auto area = half == 0 ? b.withWidth((float) singleWidth - 0.5f) : b.withTrimmedLeft((float) singleWidth - 0.5f);
            area = area.reduced(2.f);
            const bool active = (half == 1) == multi, over = hoverHalf == half && !active;
            if (active)
            {
                g.setGradientFill(juce::ColourGradient(theme::dialTop.brighter(theme::dark ? 0.14f : 0.f), 0.f, area.getY(),
                                                       theme::dialBottom.brighter(theme::dark ? 0.10f : 0.f), 0.f, area.getBottom(), false));
                g.fillRoundedRectangle(area, theme::size::radius);
                g.setColour(theme::accent.withAlpha(0.75f));
                g.drawRoundedRectangle(area, theme::size::radius, theme::size::border);
            }
            else if (over)
            {
                g.setColour(theme::raised);
                g.fillRoundedRectangle(area, theme::size::radius);
            }
            const auto words = active ? theme::text : over ? theme::text.withAlpha(0.9f) : theme::sub;
            area.removeFromLeft(7.f);
            drawWaves(g, area.removeFromLeft(20.f), half == 1, words, half == 1 && active);
            area.removeFromLeft(5.f);
            g.setColour(words);
            g.setFont(fonts::semibold(11.f).withExtraKerningFactor(0.02f));
            g.drawText(half == 0 ? "Full Band" : "Multiband", area, juce::Justification::centredLeft, false);
        }
    }

private:
    // the icons as they have always been: one wave = Single, three stacked waves = Multiband (low at the bottom)
    static void drawWaves(juce::Graphics& g, juce::Rectangle<float> box, bool stacked, juce::Colour plain, bool coloured)
    {
        const int numWaves = stacked ? 3 : 1;
        const auto area = box.withSizeKeepingCentre(box.getWidth(), stacked ? 14.f : 10.f);
        const float amp = stacked ? 1.7f : 3.8f;
        for (int k = 0; k < numWaves; ++k)
        {
            const float cy = stacked ? area.getY() + area.getHeight() * ((float) k + 0.5f) / 3.f : area.getCentreY();
            juce::Path wave;
            for (float x = 0.f; x <= 1.001f; x += 0.04f)
            {
                const float y = cy - amp * std::sin(x * juce::MathConstants<float>::twoPi + (float) k * 0.9f);
                if (x == 0.f) wave.startNewSubPath(area.getX() + x * area.getWidth(), y);
                else wave.lineTo(area.getX() + x * area.getWidth(), y);
            }
            g.setColour(coloured ? theme::bandColour(2 - k) : plain);
            g.strokePath(wave, juce::PathStrokeType(stacked ? 1.4f : 1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    void setHoverHalf(int half) { if (half != hoverHalf) { hoverHalf = half; repaint(); } }

    bool multi = false;
    int hoverHalf = -1;
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
        const auto amber = theme::amber;
        g.setColour(bypassed ? amber.withAlpha(0.22f) : hover ? theme::bg : theme::panel);
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(bypassed ? amber : theme::line);
        g.drawRoundedRectangle(b, 6.f, 1.f);

        const auto c = bypassed ? amber : theme::accent.withAlpha(hover ? 1.f : 0.9f);
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
        const auto amber = theme::amber;
        const auto c = state == 0 ? theme::sub.withAlpha(hover ? 0.9f : 0.6f) : state == 1 ? theme::accent : amber;
        g.setColour(state == 0 ? (hover ? theme::bg : theme::panel) : c.withAlpha(0.2f));
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(state == 0 ? theme::line : c);
        g.drawRoundedRectangle(b, 6.f, 1.f);
        g.setColour(c);
        auto words = getLocalBounds().toFloat().reduced(1.f, 3.f);
        g.setFont(fonts::regular(8.5f));
        g.drawText("Clipper", words.removeFromTop(words.getHeight() * 0.45f), juce::Justification::centred, false);
        g.setFont(fonts::semibold(11.f));
        g.drawText(state == 0 ? "Off" : state == 1 ? "Soft" : "Hard", words, juce::Justification::centred, false);
    }

private:
    int state = 0;
};

// A slider that also goes back to its default on Ctrl-click (Windows / Linux) or Cmd-click (macOS). Alt / Option-click
// and the rest work as in any JUCE slider. On macOS, Ctrl-click is the right-click, so it is left to the subclass.
// A right click opens a menu: the knob's own choices, followed by whatever the DAW offers for the parameter the knob
// is driving (usually things like creating automation). Hosts that offer nothing leave just our own choices.
class ResetClickSlider : public juce::Slider
{
public:
    juce::RangedAudioParameter* hostParam = nullptr; // the parameter the knob drives right now; set wherever it is attached

    juce::PopupMenu buildMenu()
    {
        juce::PopupMenu m;
        if (canType()) m.addItem(1, "Enter value...");
        m.addItem(2, "Reset to default", isDoubleClickReturnEnabled());

        auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
        auto* host = editor != nullptr ? editor->getHostContext() : nullptr;
        if (host != nullptr && hostParam != nullptr)
            if (const auto fromHost = host->getContextMenuForParameter(hostParam))
            {
                const auto items = fromHost->getEquivalentPopupMenu(); // each of these carries its own action
                if (items.getNumItems() > 0)
                {
                    m.addSeparator();
                    for (juce::PopupMenu::MenuItemIterator it(items); it.next();) m.addItem(it.getItem());
                }
            }
        return m;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMenu(e.getScreenPosition()); return; }
        resetClick = isEnabled() && !e.mods.isPopupMenu() && e.mods.isCommandDown(); // "command" is Ctrl outside macOS
        if (resetClick) juce::Slider::mouseDoubleClick(e);   // JUCE's own reset, so the host sees a proper edit
        else juce::Slider::mouseDown(e);
    }
    // typed text goes through here too: "nan" and friends are not values, so the knob stays where it is
    double snapValue(double attempted, DragMode) override { return std::isfinite(attempted) ? attempted : getValue(); }
    void mouseDrag(const juce::MouseEvent& e) override { if (!resetClick) juce::Slider::mouseDrag(e); }
    void mouseUp(const juce::MouseEvent& e) override { if (!resetClick) juce::Slider::mouseUp(e); }

protected:
    virtual bool canType() const { return false; }
    virtual void showTypeBox() {}
    bool resetClick = false; // this click was a reset or opened the menu: no drag, and no double-click action

private:
    void showMenu(juce::Point<int> at)
    {
        resetClick = true;
        auto m = buildMenu();
        m.setLookAndFeel(&getLookAndFeel());   // the menu is its own native window, so give it our font and colours explicitly
        juce::Component::SafePointer<ResetClickSlider> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(at, at).expanded(1)).withDeletionCheck(*this),
                        [safe](int r) { // the DAW's items report -1 and have already run their own action
                            if (safe == nullptr) return;
                            if (r == 1) safe->showTypeBox();
                            else if (r == 2) safe->setValue(safe->getDoubleClickReturnValue(), juce::sendNotificationSync);
                        });
    }
};

// The very small IN / OUT gain knobs under the meters. 0 dB points straight up; double-click, Ctrl-click or Alt-click goes back there.
class TrimKnob : public ResetClickSlider
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

    // unlike the big knobs, a Mac's Ctrl-click resets here too (a right click opens the menu)
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isCtrlDown() && !e.mods.isRightButtonDown()) { resetClick = true; juce::Slider::mouseDoubleClick(e); return; }
        ResetClickSlider::mouseDown(e);
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
        g.setColour(theme::dialTrack);
        g.strokePath(track, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (std::abs(angle) > 0.01f)   // lit from straight up (0 dB) to where the knob is
        {
            value.addCentredArc(c.x, c.y, r, r, 0.f, juce::jmin(0.f, angle), juce::jmax(0.f, angle), true);
            g.setColour(theme::accent);
            g.strokePath(value, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const float face = r - 2.5f; // the same face as the big knobs
        {
            const auto disc = juce::Rectangle<float>(face * 2.f, face * 2.f).withCentre(c);
            g.setGradientFill(juce::ColourGradient(theme::dialTop, c.x, disc.getY(), theme::dialBottom, c.x, disc.getBottom(), false));
            g.fillEllipse(disc);
            g.setColour(hot ? theme::sub : theme::line.brighter(theme::dark ? 0.12f : 0.f));
            g.drawEllipse(disc.reduced(0.5f), 1.f);
        }
        g.setColour(hot ? theme::accent : theme::text.withAlpha(0.9f));
        const juce::Point<float> dir(std::sin(angle), -std::cos(angle));
        g.drawLine({ c + dir * 1.f, c + dir * (r - 1.5f) }, 1.8f);

        const double v = getValue();
        const juce::String text = std::abs(v) < 0.05 ? juce::String("0.0") : (v > 0.0 ? "+" : "") + juce::String(v, 1);
        g.setColour(hot ? theme::text : theme::sub);
        g.setFont(fonts::regular(9.f));
        g.drawFittedText(text, readout.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
};

// A rotary knob you can also type into: double-click the knob (or choose it from the right-click menu) to enter an
// exact value; right-click also offers a reset, and Ctrl-click (Cmd on macOS) or Alt-click resets at once.
class Knob : public ResetClickSlider
{
public:
    Knob() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void mouseDoubleClick(const juce::MouseEvent&) override { if (!resetClick) showTextBox(); }

protected:
    bool canType() const override { return true; }
    void showTypeBox() override { showTextBox(); }
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
    juce::Font getLabelFont(juce::Label& l) override
    {
        return dynamic_cast<ValueLabel*>(&l) != nullptr ? fonts::semibold(theme::type::value) : fonts::regular(theme::type::control);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return fonts::semibold(13.f); }
    juce::Font getPopupMenuFont() override { return fonts::regular(12.5f); }

    // ---- menus (dropdowns, split slopes, knob right-clicks): a rounded dark card in the interface font, generous rows,
    // a soft highlight under the mouse, and the current choice in bold
    int getPopupMenuBorderSize() override { return 5; }

    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight) override
    {
        if (isSeparator) { idealWidth = 60; idealHeight = 9; return; }
        juce::GlyphArrangement ga;
        ga.addLineOfText(fonts::semibold(12.5f), text, 0.f, 0.f);
        idealWidth = (int) std::ceil(ga.getBoundingBox(0, -1, true).getWidth()) + 26; // just the margins
        idealHeight = 27;
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        const auto b = juce::Rectangle<float>(0.f, 0.f, (float) width, (float) height);
        g.setColour(theme::raised);
        g.fillRoundedRectangle(b, 8.f);
        g.setColour(theme::line.brighter(theme::dark ? 0.18f : 0.f));
        g.drawRoundedRectangle(b.reduced(0.5f), 8.f, theme::size::border);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                           const juce::Drawable*, const juce::Colour*) override
    {
        if (isSeparator)
        {
            g.setColour(theme::line);
            g.fillRect(area.reduced(8, 0).withSizeKeepingCentre(area.getWidth() - 16, 1));
            return;
        }
        const auto row = area.toFloat().reduced(1.f, 1.f);
        const bool hot = isHighlighted && isActive;
        if (hot || isTicked)
        {
            g.setColour(theme::accent.withAlpha(hot ? 0.17f : 0.10f));
            g.fillRoundedRectangle(row, theme::size::radius);
        }
        auto inner = area.reduced(12, 0);
        const auto colour = !isActive ? theme::muted : hot || isTicked ? theme::text : theme::text.withAlpha(0.82f);
        g.setColour(colour);
        if (hasSubMenu)
        {
            juce::Path arrow;
            const auto c = inner.removeFromRight(10).toFloat().getCentre();
            arrow.startNewSubPath(c.x - 1.5f, c.y - 3.5f);
            arrow.lineTo(c.x + 2.f, c.y);
            arrow.lineTo(c.x - 1.5f, c.y + 3.5f);
            g.strokePath(arrow, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setFont(isTicked ? fonts::semibold(12.5f) : fonts::regular(12.5f));
        g.drawFittedText(text, inner, juce::Justification::centredLeft, 1, 0.9f);
        if (shortcut.isNotEmpty())
        {
            g.setColour(theme::sub);
            g.drawText(shortcut, inner, juce::Justification::centredRight, false);
        }
    }

    juce::Label* createSliderTextBox(juce::Slider& s) override
    {
        if (s.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
        {
            auto* l = new ValueLabel();
            l->setJustificationType(juce::Justification::centred);
            l->setKeyboardType(juce::TextInputTarget::decimalKeyboard);
            l->setColour(juce::Label::textColourId, s.findColour(juce::Slider::textBoxTextColourId));
            l->setColour(juce::Label::textWhenEditingColourId, s.findColour(juce::Slider::textBoxTextColourId));
            l->setColour(juce::Label::outlineWhenEditingColourId, theme::accent);
            return l;
        }
        return LookAndFeel_V4::createSliderTextBox(s);
    }

    // (re)load the theme colours; call after theme::setDark(), then sendLookAndFeelChange() on the editor
    void applyColours()
    {
        using namespace juce;
        setColour(Slider::textBoxTextColourId, theme::text);
        setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour(Label::textColourId, theme::sub);
        setColour(ComboBox::textColourId, theme::text);
        setColour(ComboBox::outlineColourId, theme::line);
        setColour(ComboBox::backgroundColourId, theme::panel);
        setColour(ComboBox::arrowColourId, theme::sub);
        setColour(PopupMenu::backgroundColourId, theme::raised.withAlpha(0.996f)); // not quite opaque: lets the corners be round
        setColour(PopupMenu::textColourId, theme::text);
        setColour(PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha(0.16f));
        setColour(PopupMenu::highlightedTextColourId, theme::text);
        setColour(TextButton::textColourOffId, theme::text);
        setColour(TextButton::textColourOnId, theme::onAccent);
        setColour(ToggleButton::textColourId, theme::sub);
    }

    // What colour a knob's value is drawn in: a band's own colour when it edits that band, otherwise the neutral accent
    // (a single curve, a linked "all bands" control, a view setting)
    static void setKnobBand(juce::Slider& knob, int band)
    {
        if ((int) knob.getProperties().getWithDefault("band", -1) == band) return;
        knob.getProperties().set("band", band);
        knob.repaint();
    }
    static juce::Colour knobTint(const juce::Slider& knob)
    {
        const int band = knob.getProperties().getWithDefault("band", -1);
        return band >= 0 ? theme::bandColour(band) : theme::accent;
    }

    // The face of a knob: a dark disc lit from above, with a rim that answers to the mouse
    static void drawDialFace(juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour tint, bool hover, bool dragging)
    {
        const auto disc = juce::Rectangle<float>(radius * 2.f, radius * 2.f).withCentre(c);
        g.setGradientFill(juce::ColourGradient(theme::dialTop, c.x, disc.getY(), theme::dialBottom, c.x, disc.getBottom(), false));
        g.fillEllipse(disc);
        g.setColour(dragging ? tint.withAlpha(0.8f) : hover ? theme::sub.withAlpha(0.75f) : theme::line.brighter(theme::dark ? 0.12f : 0.f));
        g.drawEllipse(disc.reduced(0.5f), theme::size::border);
        juce::Path shine; // 1 px along the top edge
        shine.addCentredArc(c.x, c.y, radius - 1.5f, radius - 1.5f, 0.f, -1.1f, 1.1f, true);
        g.setColour(juce::Colours::white.withAlpha(theme::dark ? (hover || dragging ? 0.17f : 0.10f) : 0.8f));
        g.strokePath(shine, juce::PathStrokeType(1.f));
    }

    // Three layers: a thin track for the whole range, a slim arc up to the value, and the face with its indicator
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto area = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h);
        const float room = juce::jmin(area.getWidth(), area.getHeight()) - 2.f;
        const float diameter = juce::jmin(room, (float) slider.getProperties().getWithDefault("dial", room));
        const float r = diameter * 0.5f;
        const auto c = area.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto tint = knobTint(slider);
        const bool dragging = slider.isMouseButtonDown(), hover = slider.isMouseOverOrDragging();

        const float arcRadius = r - 1.5f;
        const juce::PathStrokeType thin(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path track, value;
        track.addCentredArc(c.x, c.y, arcRadius, arcRadius, 0.f, startAngle, endAngle, true);
        g.setColour(theme::dialTrack);
        g.strokePath(track, thin);
        if (angle - startAngle > 0.01f)
        {
            value.addCentredArc(c.x, c.y, arcRadius, arcRadius, 0.f, startAngle, angle, true);
            if (dragging) // a little light around the arc, only while the knob is held
            {
                g.setColour(tint.withAlpha(0.18f));
                g.strokePath(value, juce::PathStrokeType(5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour(dragging ? tint.brighter(0.2f) : tint.withAlpha(hover ? 1.f : 0.9f));
            g.strokePath(value, thin);
        }

        const float face = r - 5.5f;
        drawDialFace(g, c, face, tint, hover, dragging);
        const juce::Point<float> dir(std::sin(angle), -std::cos(angle));
        g.setColour(dragging ? tint.brighter(0.25f) : theme::text);
        g.drawLine({ c + dir * (face * 0.42f), c + dir * (face - 2.f) }, 2.f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& bt, bool, bool) override
    {
        auto b = bt.getLocalBounds().toFloat();
        auto sw = b.removeFromRight(30.f).withSizeKeepingCentre(28.f, 14.f);
        g.setColour(bt.getToggleState() ? theme::accent : theme::track);
        g.fillRoundedRectangle(sw, 7.f);
        const float knobX = bt.getToggleState() ? sw.getRight() - 12.f : sw.getX() + 2.f;
        g.setColour(bt.getToggleState() ? theme::onAccent : theme::text);
        g.fillEllipse(knobX, sw.getY() + 2.f, 10.f, 10.f);
        g.setColour(theme::sub);
        g.setFont(fonts::regular(13.f));
        g.drawText(bt.getButtonText(), b.withTrimmedRight(6.f), juce::Justification::centredRight);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& bt, const juce::Colour&, bool hover, bool down) override
    {
        auto b = bt.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = bt.getToggleState();
        if (bt.getProperties().contains("quiet") && !hover && !down)
        {
            g.setColour(theme::line);
            g.drawRoundedRectangle(b, theme::size::radius, theme::size::border);
            return;
        }
        g.setColour(on ? theme::accent : down ? theme::track : hover ? theme::raised : theme::panel);
        g.fillRoundedRectangle(b, theme::size::radius);
        g.setColour(on ? theme::accent : theme::line);
        g.drawRoundedRectangle(b, theme::size::radius, theme::size::border);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto b = juce::Rectangle<float>(0.f, 0.f, (float) w, (float) h).reduced(0.5f);
        g.setColour(theme::panel);
        g.fillRoundedRectangle(b, theme::size::radius);
        g.setColour(theme::line);
        g.drawRoundedRectangle(b, theme::size::radius, theme::size::border);
        g.setColour(theme::ink(0.06f)); // a 1 px top highlight: just enough depth
        g.fillRect(b.reduced(theme::size::radius, 0.f).withHeight(1.f).translated(0.f, 1.f));
        juce::Path p;
        const float cx = (float) w - 14.f, cy = (float) h * 0.5f;
        p.startNewSubPath(cx - 4.f, cy - 2.f); p.lineTo(cx, cy + 2.f); p.lineTo(cx + 4.f, cy - 2.f);
        g.setColour(theme::sub);
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
        g.setColour(s.isMouseOverOrDragging() ? theme::bg : theme::panel);
        g.fillRoundedRectangle(b, 6.f);
        g.setColour(s.isMouseButtonDown() ? theme::accent : theme::line);
        g.drawRoundedRectangle(b, 6.f, 1.f);
    }
};
