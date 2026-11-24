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
    void frame(); // once per display refresh (VBlank), capped to ~60 fps
    void renderGrid(juce::Graphics&);
    void drawWaveforms(juce::Graphics&);
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
    Envelope ghost[3]; // the other bands' curves (slots 1..3), drawn faintly for reference; not editable here
    int selected = -1;  // point whose Bezier handles are shown
    int dragPoint = -1;
    int dragSeg = -1;   // simple mode: segment whose handle is being dragged
    bool mirrorDrag = false; // this handle drag also moves the opposite handle (only for already-smooth points)
    bool controlsHidden = false; // click empty space: hide every point and handle to see the bare curve; click the curve to bring them back
    int dragKind = 0;  // 1 = dragging the selected point's in handle, 2 = out handle (grabbing a handle selects its point)

    juce::Image gridImg, curveImg; // cached layers, see paint()
    size_t curveKey = 0;
    double lastFrameMs = 0.0;
    unsigned lastBlock = 0;
    juce::VBlankAttachment vblank; // keep last: it calls frame() as soon as it exists
};


// A thin vertical meter beside the curve editor: where the input signal (-1 .. +1, same scale as the scope) was at the last
// point of the cycle, the moment the curve drops. The big dot is the newest cycle, the small ones the cycles before it.
// A dot far from the centre line means that note is still swinging when the gain falls: that is where bass clicks.
class WrapMeter : public juce::Component, private juce::Timer
{
public:
    WrapMeter(FreeLfoProcessor& p, const EnvelopeEditor& e) : proc(p), editor(e) { startTimerHz(30); setInterceptsMouseClicks(false, false); }
    ~WrapMeter() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float top = b.getY() + 10.f, h = b.getHeight() - 20.f;
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
        for (int k = juce::jmin(count, FreeLfoProcessor::wrapHistory) - 1; k >= 0; --k)   // oldest first, newest on top
        {
            const float v = proc.wrapVal[(size_t) s][(size_t) ((count - 1 - k) % FreeLfoProcessor::wrapHistory)].load(std::memory_order_relaxed);
            const float y = yOf(v);
            const float size = k == 0 ? 6.f : 3.5f;           // half height of the arrow
            const float tipX = b.getX() + 2.f, baseX = tipX + (k == 0 ? 11.f : 7.f);
            juce::Path arrow;                                    // "<": points left, at the curve editor
            arrow.addTriangle(tipX, y, baseX, y - size, baseX, y + size);
            const float a = k == 0 ? 1.f : 0.5f * (1.f - (float) k / FreeLfoProcessor::wrapHistory) + 0.1f;
            if (k == 0)
            {
                g.setColour(juce::Colours::black.withAlpha(0.5f));
                g.strokePath(arrow, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour(colourFor(v).withAlpha(a));
            g.fillPath(arrow);
        }
    }

private:
    void timerCallback() override
    {
        const int s = editor.getSlot();
        const int c = proc.wrapCount[(size_t) s].load(std::memory_order_relaxed);
        if (c != lastCount || s != lastSlot) { lastCount = c; lastSlot = s; repaint(); }
    }

    FreeLfoProcessor& proc;
    const EnvelopeEditor& editor;
    int lastCount = -1, lastSlot = -1;
};
