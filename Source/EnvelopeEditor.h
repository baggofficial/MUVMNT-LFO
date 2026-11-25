#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"

// The curve editor: double-click empty space = add point, drag = move,
// double-click a point = delete it, drag the small diamond on a segment = bend it.
class EnvelopeEditor : public juce::Component
{
public:
    // slot 0 edits the single-band curve; slots 1..3 edit the low / mid / high band curves
    explicit EnvelopeEditor(FreeLfoProcessor&, int slot = 0);
    void setSlot(int newSlot);
    int getSlot() const { return slot; }
    void resetCurve();

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override; // Cmd/Ctrl+Z undo, Shift+Cmd/Ctrl+Z or Ctrl+Y redo

    bool areControlsHidden() const { return controlsHidden; }
    void animate(); // one step of the editor's animation clock: redraws if anything is moving or changed
    // A short message over the top of the graph, gone again after `ms`
    void showNote(const juce::String& text, double ms = 2800.0)
    {
        noteText = text;
        noteUntil = juce::Time::getMillisecondCounterHiRes() + ms;
        repaint();
    }
    bool isShowingNote() const { return noteUntil > juce::Time::getMillisecondCounterHiRes(); }
    juce::String currentNote() const { return isShowingNote() ? noteText : juce::String(); }
    static juce::String limitNoteText() { return juce::String((int) Envelope::maxPoints) + " points is the limit. Double-click a point to remove it."; }
    bool isShowingLimitNote() const { return currentNote() == limitNoteText(); }
    // Note values at 1/4, 1/2, 3/4 and the end of the cycle ("1/16", "1/8", ...); empty strings in Hz mode
    std::array<juce::String, 4> positionLabels() const;
    // Snapping is off by default; holding Option (macOS) / Alt (Windows) snaps while you drag. If the Snap switch
    // is on, it snaps all the time and holding Option / Alt turns it off instead.
    bool snapToGrid = false;
    // Number of grid lines (a saved parameter, set by the draggable number in the header). Snapping lands exactly
    // on the drawn lines. The vertical lines follow the horizontal ones at about half the count.
    int gridX() const
    {
        const int index = juce::jlimit(0, (int) FreeLfoProcessor::gridStops.size() - 1, (int) proc.apvts.getRawParameterValue("gridDiv")->load());
        return FreeLfoProcessor::gridStops[(size_t) index];
    }
    int gridY() const { return juce::jmax(2, gridX() / 2); }
    bool snapNow(const juce::ModifierKeys& m) const { return snapToGrid != m.isAltDown(); }
    // Bezier handles snap with Option / Alt or Ctrl held (Shift is for breaking the handles apart)
    bool snapHandleNow(const juce::ModifierKeys& m) const { return snapToGrid != (m.isAltDown() || m.isCtrlDown()); }

private:
    void renderGrid(juce::Graphics&);
    juce::Rectangle<float> plotBounds() const { return getLocalBounds().toFloat().reduced(10.f).withTrimmedBottom(4.f); } // room below for the labels
    juce::String cachedLabels; // the labels the cached grid image was drawn with
    void drawWaveforms(juce::Graphics&);
    void drawOverlap(juce::Graphics&, const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& mn,
                     const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& mx,
                     const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& scMn,
                     const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& scMx, float opacity);
    void renderCurveLayer(juce::Graphics&);
    size_t computeKey() const;
    float predictedPhase() const;

    juce::Point<float> toScreen(float x, float y) const;
    juce::Point<float> toNorm(juce::Point<float> p) const;
    int hitPoint(juce::Point<float> p) const;
    bool bezierMode() const { return proc.apvts.getRawParameterValue("bezier")->load() > 0.5f; }
    int hitSegment(juce::Point<float> p) const; // simple mode: segment whose single handle is under p, or -1
    // (point, kind) of the Bezier handle under p (kind 1 = in, 2 = out), or (-1, 0)
    // zero-length handles sit on their point and only intercept a click when pullZero (Alt) is held
    std::pair<int, int> hitHandle(juce::Point<float> p, bool pullZero = false) const;
    // Handles are all shown (After Effects style), except zero-length ones: those sit under their point,
    // so they only appear (and can be pulled out) while that point is selected.
    bool handleVisible(int point, int kind) const;
    bool nearCurve(juce::Point<float> p) const; // is p within a few pixels of the drawn curve line?
    juce::Point<float> handleScreen(int point, int kind) const;
    bool hasHandle(int point, int kind) const;
    void commit();

    juce::String bandId(const char* base) const { return slot == 0 ? juce::String(base) : juce::String(base) + FreeLfoProcessor::bandSuffix(slot - 1); }
    double cycleSecs() const
    {
        return slot == 0 ? proc.cycleSeconds.load(std::memory_order_relaxed)
                         : proc.bandCycleSeconds[(size_t) slot - 1].load(std::memory_order_relaxed);
    }
    void fillWavePath(juce::Graphics&, const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& mn,
                      const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& mx, const float* gain, const juce::FillType& fill);
    bool rainbowFill(float alpha, juce::FillType& out) const;
    float outGain[FreeLfoProcessor::waveBins] = {}; // the gain actually applied at each cycle position (filled with the curve layer)
    // Single tab: the accent colour. Multiband tab: the band's own colour (red / green / blue).
    juce::Colour mainColour() const { return slot == 0 ? paper::accent : paper::bandColour(slot - 1); }

    FreeLfoProcessor& proc;
    int cachedGrid = -1; // grid count the cached grid image was drawn with
    int cachedTheme = -1; // theme the cached grid image was drawn in
    int slot = 0;
    // depth / smooth / offset as the audio will use them for this curve (a linked control follows one band for all)
    void initParams();
    float paramValue(int which) const; // 0 = depth, 1 = smooth, 2 = offset
    std::atomic<float>* paramP[3][4] = {};   // [which][0 = single curve, 1..3 = bands]
    std::atomic<float>* linkP[3] = {};
    std::atomic<float>* bandsP = nullptr;
    Envelope env;
    FreeLfoProcessor::UndoState gestureStart; // the curves as they were when the mouse went down
    bool haveGestureStart = false, gestureRecorded = false;
    int seenVersion[4] = { -1, -1, -1, -1 }; // the curve versions last copied (own curve, then the three bands)
    int seenSlot = -1;
    Envelope ghost[3]; // the other bands' curves (slots 1..3), drawn faintly for reference; not editable here
    int selected = -1;  // point whose Bezier handles are shown
    int dragPoint = -1;
    int dragSeg = -1;   // simple mode: segment whose handle is being dragged
    bool mirrorDrag = false; // this handle drag also moves the opposite handle (only for already-smooth points)
    bool controlsHidden = false; // click empty space: hide every point and handle to see the bare curve; click the curve to bring them back
    int dragKind = 0;  // 1 = dragging the selected point's in handle, 2 = out handle (grabbing a handle selects its point)

    juce::Image gridImg, curveImg; // cached layers, see paint()
    size_t curveKey = 0;
    juce::String noteText;
    double noteUntil = 0.0;   // the note is on screen until this time
    bool noteDrawn = false;
    unsigned lastBlock = 0;
};


// A thin vertical meter beside the curve editor: where the output signal (what the plugin lets through, -1 .. +1, same scale as the scope) was at the last
// point of the cycle (the arrow), plus a bar for the lowest to highest output over the last 4% of the cycle (the end zone). The big dot is the newest cycle, the small ones the cycles before it.
// A dot far from the centre line means that note is still swinging when the gain falls: that is where bass clicks.
class WrapMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    WrapMeter(FreeLfoProcessor& p, const EnvelopeEditor& e) : proc(p), editor(e) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("End-of-cycle meter: the level at the cycle's last point.\nClick to show the end-zone bar (last 4%)");
    }

    // 28 px with the end-zone bar, 20 px without it
    int preferredWidth() const { return proc.showEndZone.load() ? 28 : 20; }

    void mouseDown(const juce::MouseEvent&) override
    {
        proc.showEndZone.store(!proc.showEndZone.load());
        if (auto* parent = getParentComponent()) parent->resized();   // the column changes width
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float top = b.getY() + 10.f, h = b.getHeight() - 24.f;   // the same vertical scale as the graph
        g.setColour(paper::bg);
        g.fillRoundedRectangle(b, 5.f);
        auto yOf = [&](float v) { return top + (1.f - (0.5f + 0.5f * juce::jlimit(-1.f, 1.f, v))) * h; };

        // a quiet track with a zero line; only the arrows carry colour
        g.setColour(paper::gridMajor());
        g.fillRoundedRectangle(juce::Rectangle<float>(b.getCentreX() - 1.f, top, 2.f, h), 1.f);
        g.setColour(paper::sub.withAlpha(0.7f));
        g.drawHorizontalLine((int) yOf(0.f), b.getX() + 3.f, b.getRight() - 3.f);
        g.setFont(fonts::regular(8.5f));
        g.drawText("+1", b.withHeight(10.f), juce::Justification::centred);
        g.drawText("-1", b.withTop(b.getBottom() - 10.f), juce::Justification::centred);

        // green at 0, through amber, to red at the peaks (+1 / -1)
        auto colourFor = [](float v) {
            const auto green = juce::Colour(0xff39c46a), amber = juce::Colour(0xfff0b429), red = juce::Colour(0xffff4d4d);
            const float a = juce::jlimit(0.f, 1.f, std::abs(v));
            return a < 0.44f ? green.interpolatedWith(amber, a / 0.44f) : amber.interpolatedWith(red, (a - 0.44f) / 0.56f);
        };

        const int s = editor.getSlot();
        const int count = proc.wrapCount[(size_t) s].load(std::memory_order_acquire);
        const int shown = juce::jmin(count - 1, FreeLfoProcessor::wrapHistory - 1);   // older cycles that exist
        auto alphaFor = [](int k) { return k <= 0 ? 1.f : juce::jmax(0.f, 0.5f * (1.f - (float) k / (float) (FreeLfoProcessor::wrapHistory - 1))) + 0.04f; };
        auto drawArrow = [&](float v, float size, float len, float alpha, bool outline) {
            const float y = yOf(v), tipX = b.getX() + 2.f, baseX = tipX + len;
            juce::Path arrow;                                    // "<": points left, at the curve editor
            arrow.addTriangle(tipX, y, baseX, y - size, baseX, y + size);
            if (outline)
            {
                g.setColour(juce::Colours::black.withAlpha(0.5f));
                g.strokePath(arrow, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour(colourFor(v).withAlpha(alpha));
            g.fillPath(arrow);
        };

        // older cycles: small, fading arrows. When a new cycle arrives they age one step, easing (not jumping) to the next look.
        for (int k = shown; k >= 1; --k)
        {
            const float v = proc.wrapVal[(size_t) s][(size_t) ((count - 1 - k) % FreeLfoProcessor::wrapHistory)].load(std::memory_order_relaxed);
            const float a = alphaFor(k - 1) + (alphaFor(k) - alphaFor(k - 1)) * ageProgress;
            const float t = k == 1 ? ageProgress : 1.f;       // the one that was the newest shrinks to the small size
            drawArrow(v, 6.f + (3.5f - 6.f) * t, 11.f + (7.f - 11.f) * t, a, false);
        }

        if (count > 0)
        {
            // the end zone bar (click the meter to show it): lowest to highest output over the last few percent of the cycle
            if (proc.showEndZone.load())
            {
            const float y0 = yOf(dispHi), y1 = yOf(dispLo);
            const auto bar = juce::Rectangle<float>(b.getRight() - 9.f, y0 - 1.5f, 5.f, juce::jmax(3.f, y1 - y0 + 3.f));
            g.setColour(colourFor(juce::jmax(std::abs(dispLo), std::abs(dispHi))).withAlpha(0.9f));
            g.fillRoundedRectangle(bar, 2.5f);
            }
            drawArrow(dispArrow, 6.f, 11.f, 1.f, true);   // the very last point of the newest cycle
        }
    }

    // The values only change once per cycle, so the display eases toward them: the arrow glides (about 120 ms), and the end-zone
    // bar grows quickly but falls back slowly, like a peak-hold meter.
    void animate() // one step of the editor's animation clock
    {
        const int s = editor.getSlot();
        const int c = proc.wrapCount[(size_t) s].load(std::memory_order_acquire);
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = (float) juce::jlimit(0.001, 0.1, (now - lastMs) / 1000.0);
        lastMs = now;

        float tv = 0.f, tlo = 0.f, thi = 0.f;
        if (c > 0)
        {
            const size_t i = (size_t) ((c - 1) % FreeLfoProcessor::wrapHistory);
            tv = proc.wrapVal[(size_t) s][i].load(std::memory_order_relaxed);
            tlo = proc.wrapLo[(size_t) s][i].load(std::memory_order_relaxed);
            thi = proc.wrapHi[(size_t) s][i].load(std::memory_order_relaxed);
        }
        if (s != lastSlot) // another band: show its values at once
        {
            lastSlot = s; lastCount = c; ageProgress = 1.f;
            dispArrow = tv; dispLo = tlo; dispHi = thi;
            repaint();
            return;
        }
        if (c != lastCount) { lastCount = c; ageProgress = 0.f; }

        auto ease = [dt](float cur, float target, float tau) { return cur + (target - cur) * (1.f - std::exp(-dt / tau)); };
        const float nArrow = ease(dispArrow, tv, 0.04f);
        const float nLo = ease(dispLo, tlo, tlo < dispLo ? 0.03f : 0.45f);    // grows down fast, falls back slowly
        const float nHi = ease(dispHi, thi, thi > dispHi ? 0.03f : 0.45f);
        const float nAge = 1.f - (1.f - ageProgress) * std::exp(-dt / 0.06f);
        const bool moving = std::abs(nArrow - dispArrow) > 1e-4f || std::abs(nLo - dispLo) > 1e-4f
                            || std::abs(nHi - dispHi) > 1e-4f || std::abs(nAge - ageProgress) > 1e-4f;
        dispArrow = nArrow; dispLo = nLo; dispHi = nHi; ageProgress = nAge > 0.999f ? 1.f : nAge;
        if (moving || firstPaint) { firstPaint = false; repaint(); }
    }

private:
    FreeLfoProcessor& proc;
    const EnvelopeEditor& editor;
    int lastCount = -1, lastSlot = -1;
    double lastMs = 0.0;
    float dispArrow = 0.f, dispLo = 0.f, dispHi = 0.f, ageProgress = 1.f;
    bool firstPaint = true;
};
