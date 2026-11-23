// Renders the editor to PNG files without a screen, so the UI can be checked headlessly.
// Build: cmake -B build -DFREELFO_BENCH=ON && cmake --build build --target LfoSnap
// Run:   build/LfoSnap_artefacts/Release/LfoSnap <output-dir>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

static void setParam(FreeLfoProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(plain));
}

static void save(const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream out(f);
    juce::PNGImageFormat().writeImageToStream(img, out);
    std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
}


// Drives an EnvelopeEditor with synthetic mouse events (headless).
struct MouseDriver
{
    EnvelopeEditor& ed;
    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::Point<float> downPos;
    juce::Time downTime = juce::Time::getCurrentTime();

    juce::MouseEvent make(juce::Point<float> pos, bool dragged, juce::ModifierKeys mods = {})
    {
        return juce::MouseEvent(src, pos, mods, 1.f, 0.f, 0.f, 0.f, 0.f, &ed, &ed, juce::Time::getCurrentTime(),
                                downPos, downTime, 1, dragged);
    }
    void down(juce::Point<float> pos) { downPos = pos; ed.mouseDown(make(pos, false, juce::ModifierKeys::leftButtonModifier)); }
    void drag(juce::Point<float> pos) { ed.mouseDrag(make(pos, true, juce::ModifierKeys::leftButtonModifier)); }
    void dragAlt(juce::Point<float> pos) { ed.mouseDrag(make(pos, true, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier)); }
    void up(juce::Point<float> pos) { ed.mouseUp(make(pos, true)); }
};

static float curveDifference(const Envelope& a, const Envelope& b, float from, float to)
{
    float worst = 0.f;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = from + (to - from) * (float) i / 200.f;
        worst = std::max(worst, std::fabs(a.evaluate(x) - b.evaluate(x)));
    }
    return worst;
}

// The reported flow: finish a curve in simple mode, switch to Bezier handles, edit the left handle, then nudge
// the right handle slightly. The nudge must not swing the shape of the curve you just made.
static bool handleNudgeRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 390.f - y * 380.f); };

    Envelope e; e.points = { { 0.f, 0.f }, { 0.5f, 1.f }, { 1.f, 1.f } };
    e.bendSegmentThrough(0, 0.37f, 0.17f);              // "final edit in simple mode"
    p.setEnvelope(e, 0);
    p.convertCurveMode(true);                            // switch to Bezier handles: converted
    auto* bez = p.apvts.getParameter("bezier");
    bez->setValueNotifyingHost(1.f);

    MouseDriver m { ed };
    const auto top = toScreen(0.5f, 1.f);
    m.down(top); m.up(top);                              // select the peak point: its handles appear

    // left (in) handle: grab it where it is drawn and move it
    auto env0 = p.getEnvelope(0);
    const auto inH = env0.inHandle(1);
    const auto inScreen = toScreen(inH.first, inH.second);
    m.down(inScreen); m.drag(inScreen + juce::Point<float>(30.f, 25.f)); m.up(inScreen + juce::Point<float>(30.f, 25.f));
    const auto afterLeft = p.getEnvelope(0);

    // right (out) handle: grab where it's drawn and nudge by a few pixels
    const auto outH = afterLeft.outHandle(1);
    const auto outScreen = toScreen(outH.first, outH.second);
    m.down(outScreen); m.drag(outScreen + juce::Point<float>(6.f, 0.f)); m.up(outScreen + juce::Point<float>(6.f, 0.f));
    const auto afterRight = p.getEnvelope(0);

    const float change = curveDifference(afterLeft, afterRight, 0.f, 0.5f);
    std::printf("handle nudge regression: left-of-peak curve changed by %.4f (must stay tiny)\n", change);
    return change < 0.02f;
}

// Click empty space: points and handles hide. Click the curve: they come back.
static juce::String g_outDir;
static bool hideShowRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 390.f - y * 380.f); };
    MouseDriver m { ed };
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-52s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };

    const auto empty = toScreen(0.9f, 0.3f);
    const auto onCurve = toScreen(0.3f, p.getEnvelope(0).evaluate(0.3f));
    const auto nearCurve = onCurve + juce::Point<float>(0.f, 5.f);
    const auto farFromCurve = toScreen(0.3f, p.getEnvelope(0).evaluate(0.3f) - 0.25f);

    std::printf("hide/show controls:\n");
    check(!ed.areControlsHidden(), "starts with controls visible");
    m.down(farFromCurve); m.up(farFromCurve);
    check(ed.areControlsHidden(), "click away from the curve hides them");
    save(ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f), juce::File(g_outDir).getChildFile("bare_curve.png"));
    m.down(empty); m.up(empty);
    check(ed.areControlsHidden(), "another empty click keeps them hidden");
    m.down(nearCurve); m.up(nearCurve);
    check(!ed.areControlsHidden(), "click on the curve brings them back");
    m.down(onCurve); m.up(onCurve);
    check(!ed.areControlsHidden(), "clicking the curve again keeps them visible");
    const auto pt = toScreen(0.5f, 1.f);                    // the first point on the full-level plateau
    m.down(pt); m.up(pt);
    check(!ed.areControlsHidden(), "clicking a point does not hide them");
    m.down(farFromCurve); m.up(farFromCurve);
    check(ed.areControlsHidden(), "hidden again");
    m.down(farFromCurve); ed.mouseDoubleClick(m.make(farFromCurve, false)); m.up(farFromCurve);
    check(!ed.areControlsHidden() && p.getEnvelope(0).points.size() == 7, "double-click on empty space adds a point and shows them");
    return ok;
}

// A point added by double-click gets handles spread to its sides, so the point can be dragged on its own.
static bool newPointRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    p.apvts.getParameter("bezier")->setValueNotifyingHost(1.f);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 390.f - y * 380.f); };
    MouseDriver m { ed };
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-60s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("new point handles:\n");

    const auto click = toScreen(0.3f, 0.4f);
    m.down(click); ed.mouseDoubleClick(m.make(click, false)); m.up(click);
    auto e1 = p.getEnvelope(0);
    check(e1.points.size() == 7, "double-click adds a point");
    size_t qi = 0;                                           // the new point is the one at the clicked x (snapping is off by default)
    for (size_t i = 0; i < e1.points.size(); ++i) if (std::fabs(e1.points[i].x - 0.3f) < 2e-3f) qi = i;
    const auto& q = e1.points[qi];
    const auto origin = toScreen(q.x, q.y);
    const float lenOut = toScreen(q.x + q.outDx, q.y + q.outDy).getDistanceFrom(origin);
    const float lenIn = toScreen(q.x + q.inDx, q.y + q.inDy).getDistanceFrom(origin);
    std::printf("  handle lengths: out %.0f px, in %.0f px\n", lenOut, lenIn);
    check(lenOut > 20.f && lenIn > 20.f, "handles are spread well away from the point");
    check(q.outDx > 0.f && q.inDx < 0.f, "one handle on each side");
    const auto vo = toScreen(q.x + q.outDx, q.y + q.outDy) - origin, vi = toScreen(q.x + q.inDx, q.y + q.inDy) - origin;
    check((vo.x * vi.x + vo.y * vi.y) / (lenOut * lenIn) < -0.98f, "handles are smooth (opposite directions)");

    // grabbing the point drags the point only, the handles keep their offsets
    const auto before = q;
    m.down(origin); m.drag(origin + juce::Point<float>(20.f, -15.f)); m.up(origin + juce::Point<float>(20.f, -15.f));
    const auto afterDrag = p.getEnvelope(0);   // (a named copy: getEnvelope returns by value)
    const auto& moved = afterDrag.points[qi];
    std::printf("  before: x %.4f y %.4f out(%.4f,%.4f) in(%.4f,%.4f)\n  after:  x %.4f y %.4f out(%.4f,%.4f) in(%.4f,%.4f)\n",
                before.x, before.y, before.outDx, before.outDy, before.inDx, before.inDy,
                moved.x, moved.y, moved.outDx, moved.outDy, moved.inDx, moved.inDy);
    check(std::fabs(moved.y - before.y) > 0.01f, "dragging the point moves it");
    check(std::fabs(moved.outDx - before.outDx) < 1e-5f && std::fabs(moved.outDy - before.outDy) < 1e-5f
              && std::fabs(moved.inDx - before.inDx) < 1e-5f && std::fabs(moved.inDy - before.inDy) < 1e-5f,
          "...and the handles keep their offsets");
    return ok;
}

// The first and last points of the default curve show handles straight away, and they can be dragged at once.
static bool endHandleRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    p.apvts.getParameter("bezier")->setValueNotifyingHost(1.f);
    Envelope straight; straight.points = { { 0.f, 0.4f }, { 0.5f, 1.f }, { 1.f, 0.4f } }; // no handles set anywhere; ends share a height
    p.setEnvelope(straight, 0);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 390.f - y * 380.f); };
    MouseDriver m { ed };
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-60s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("end handles on an untouched curve:\n");

    const auto env0 = p.getEnvelope(0);
    const auto first = toScreen(0.f, 0.4f), last = toScreen(1.f, 0.4f);
    const auto firstOut = toScreen(env0.outHandle(0).first, env0.outHandle(0).second);
    const auto lastIn = toScreen(env0.inHandle(2).first, env0.inHandle(2).second);
    check(firstOut.getDistanceFrom(first) > 20.f, "first point has a visible handle");
    check(lastIn.getDistanceFrom(last) > 20.f, "last point has a visible handle");

    m.down(lastIn); m.drag(lastIn + juce::Point<float>(10.f, 60.f)); m.up(lastIn);
    const auto after = p.getEnvelope(0);
    check(after.points[2].inDx != 0.f || after.points[2].inDy != 0.f, "dragging the last point's handle works on the first try");
    check(std::fabs(after.points[2].x - 1.f) < 1e-6f && std::fabs(after.points[2].y - 0.4f) < 1e-6f, "...without moving the point");
    check(std::fabs(after.evaluate(0.8f) - env0.evaluate(0.8f)) > 0.02f, "...and it reshapes the curve");

    m.down(firstOut); m.drag(firstOut + juce::Point<float>(-5.f, 80.f)); m.up(firstOut);
    const auto after2 = p.getEnvelope(0);
    check(after2.points[0].outDx != 0.f || after2.points[0].outDy != 0.f, "same for the first point's handle");
    return ok;
}

// The loop's first and last points move together vertically (no click at the seam).
static bool seamRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 390.f - y * 380.f); };
    MouseDriver m { ed };
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-60s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("start and end points follow each other:\n");

    const auto start = toScreen(0.f, 0.f);
    m.down(start); m.drag(start + juce::Point<float>(0.f, -120.f)); m.up(start + juce::Point<float>(0.f, -120.f));
    auto e1 = p.getEnvelope(0);
    check(e1.points.front().y > 0.2f, "dragging the first point up raises it");
    check(std::fabs(e1.points.back().y - e1.points.front().y) < 1e-6f, "the last point followed");

    const auto end = toScreen(1.f, e1.points.back().y);
    m.down(end); m.drag(end + juce::Point<float>(0.f, 60.f)); m.up(end + juce::Point<float>(0.f, 60.f));
    auto e2 = p.getEnvelope(0);
    check(e2.points.back().y < e1.points.back().y - 0.05f, "dragging the last point down lowers it");
    check(std::fabs(e2.points.front().y - e2.points.back().y) < 1e-6f, "the first point followed");
    return ok;
}

// The Rate readout says "1/4 T" / "1/4 D" when the buttons are on, and 1/64 exists.
static bool rateReadoutRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    FreeLfoEditor ed(p);
    ed.setSize(960, 610);
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("rate readout:\n");

    // find the rate knob (the slider whose text is "1/4") and the two small toggle buttons
    juce::Slider* rate = nullptr; juce::TextButton *dot = nullptr, *trip = nullptr;
    for (int i = 0; i < ed.getNumChildComponents(); ++i)
    {
        auto* c = ed.getChildComponent(i);
        if (auto* s = dynamic_cast<juce::Slider*>(c)) if (s->getTextFromValue(s->getValue()) == "1/4") rate = s;
        if (auto* b = dynamic_cast<juce::TextButton*>(c)) { if (b->getButtonText() == "3") trip = b; else if (b->getButtonText().startsWith(juce::String::fromUTF8("\xE2\x80\xA2"))) dot = b; }
    }
    check(rate != nullptr && dot != nullptr && trip != nullptr, "found the rate knob and the dotted / triplet buttons");
    auto click = [](juce::Button* b) { // triggerClick() posts a message: let it run
        b->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    };
    auto boxText = [](juce::Slider* s) { // the text under the knob is a Label inside the slider
        for (int i = 0; i < s->getNumChildComponents(); ++i)
            if (auto* l = dynamic_cast<juce::Label*>(s->getChildComponent(i))) return l->getText();
        return juce::String();
    };
    if (!ok) return false;
    check(rate->getTextFromValue(rate->getValue()) == "1/4", "starts as 1/4");
    click(trip);
    check(rate->getTextFromValue(rate->getValue()) == "1/4 T", "triplet on: reads 1/4 T");
    check(boxText(rate) == "1/4 T", "...and that is what the knob's text box shows");
    click(dot);
    check(rate->getTextFromValue(rate->getValue()) == "1/4 D" && !trip->getToggleState(), "dotted on: reads 1/4 D, triplet switches off");
    click(dot);
    check(rate->getTextFromValue(rate->getValue()) == "1/4", "both off: back to 1/4");
    rate->setValue(FreeLfoProcessor::numDivisions - 1.0, juce::sendNotificationSync);
    check(rate->getTextFromValue(rate->getValue()) == "1/64", "the knob's fastest position is 1/64");
    click(trip);
    check(boxText(rate) == "1/64 T", "1/64 takes the modifiers too");

    p.apvts.getParameter("mode")->setValueNotifyingHost(1.f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    check(boxText(rate).endsWith(" Hz"), "Hz mode: the readout carries the unit");
    return ok;
}

// Snapping: off by default, Option/Alt snaps; with the switch on it is reversed. 4 snap steps between grid lines on both axes.
static bool snapRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    p.apvts.getParameter("bezier")->setValueNotifyingHost(1.f);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 390.f - y * 380.f); };
    MouseDriver m { ed };
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    auto onGrid = [](float v, int steps) { return std::fabs(v * (float) steps - std::round(v * (float) steps)) < 1e-3f; };
    std::printf("snapping:\n");
    check(!ed.snapToGrid, "snapping is off by default");

    // drag the plateau point (index 3, at x 0.5) somewhere that is not on any snap position
    const float tx = 0.4137f, ty = 0.5321f;
    auto dragPoint3 = [&](bool alt) {
        const auto start = toScreen(p.getEnvelope(0).points[3].x, p.getEnvelope(0).points[3].y);
        const auto target = toScreen(tx, ty);
        m.down(start);
        if (alt) m.dragAlt(target); else m.drag(target);
        m.up(target);
        return p.getEnvelope(0).points[3];
    };
    auto free = dragPoint3(false);
    check(std::fabs(free.x - tx) < 3e-3f && std::fabs(free.y - ty) < 3e-3f, "a plain drag is free (no snapping)");
    auto snapped = dragPoint3(true);
    check(onGrid(snapped.x, 16), "Option/Alt drag snaps x exactly onto the 16 drawn grid lines");
    check(onGrid(snapped.y, 8), "...and y onto the 8 horizontal lines");
    check(std::fabs(snapped.x - tx) < 1.f / 32.f && std::fabs(snapped.y - ty) < 1.f / 16.f, "...to the nearest line");
    check(!onGrid(free.x, 16) || !onGrid(free.y, 8), "(sanity: the free drag was off-grid)");

    // the draggable number changes the number of lines, and snapping follows it
    auto* grid = p.apvts.getParameter("gridDiv");
    auto setGrid = [grid](int lines) { // the parameter picks one of the musical stops by index
        for (size_t i = 0; i < FreeLfoProcessor::gridStops.size(); ++i)
            if (FreeLfoProcessor::gridStops[i] == lines) grid->setValueNotifyingHost(grid->convertTo0to1((float) i));
    };
    setGrid(8);
    auto coarse = dragPoint3(true);
    check(onGrid(coarse.x, 8) && onGrid(coarse.y, 4), "grid set to 8: Option/Alt snaps to 8 x 4 lines");
    setGrid(32);
    auto fine = dragPoint3(true);
    check(onGrid(fine.x, 32) && onGrid(fine.y, 16) && std::fabs(fine.x - tx) < 1.f / 64.f, "grid set to 32: snaps to 32 x 16 lines");
    setGrid(12);                                                  // a triplet-friendly stop
    auto triplet = dragPoint3(true);
    check(onGrid(triplet.x, 12) && onGrid(triplet.y, 6), "grid set to 12: snaps to 12 x 6 lines");
    setGrid(16);

    ed.snapToGrid = true;                                       // Snap switch on: reversed
    auto always = dragPoint3(false);
    check(onGrid(always.x, 16) && onGrid(always.y, 8), "with the switch on, a plain drag snaps");
    auto off = dragPoint3(true);
    check(std::fabs(off.x - tx) < 3e-3f && std::fabs(off.y - ty) < 3e-3f, "...and Option/Alt then turns snapping off");

    ed.snapToGrid = false;                                      // adding a point honours it too
    const auto click = toScreen(0.3f, 0.2f);
    m.down(click); ed.mouseDoubleClick(m.make(click, false, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier)); m.up(click);
    bool found = false;
    for (auto& pt : p.getEnvelope(0).points) if (std::fabs(pt.x - 0.3f) < 1.f / 32.f && onGrid(pt.x, 16) && pt.x > 0.f && pt.x < 1.f && pt.x != 0.5f) found = true;
    check(found, "double-click with Option/Alt places the new point on the snap grid");
    return ok;
}

// The draggable grid number only lands on musical stops (8, 12, 24, 36...), one step per notch.
static bool gridStopsRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    FreeLfoEditor ed(p);
    ed.setSize(960, 610);
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("grid number stops:\n");

    juce::Slider* box = nullptr;
    for (int i = 0; i < ed.getNumChildComponents(); ++i)
        if (auto* s = dynamic_cast<juce::Slider*>(ed.getChildComponent(i))) if (s->isBar()) box = s;
    check(box != nullptr, "found the grid number box");
    if (!box) return false;
    auto shown = [&] { return box->getTextFromValue(box->getValue()); };
    check(shown() == "16", "starts at 16");
    box->setValue(box->getValue() + 1, juce::sendNotificationSync);
    check(shown() == "24", "one step up from 16 is 24");
    box->setValue(box->getValue() + 1, juce::sendNotificationSync);
    check(shown() == "32", "...then 32");
    box->setValue(box->getValue() + 1, juce::sendNotificationSync);
    check(shown() == "36", "...then 36");
    box->setValue(box->getValue() - 5, juce::sendNotificationSync);
    check(shown() == "8", "five steps down from 36 lands on 8");
    box->setValue(box->getValue() + 1, juce::sendNotificationSync);
    check(shown() == "12", "one step up from 8 is 12");
    box->setValue(box->getMinimum(), juce::sendNotificationSync);
    check(shown() == "2", "bottom stop is 2");
    box->setValue(box->getMaximum(), juce::sendNotificationSync);
    check(shown() == "64", "top stop is 64");
    return ok;
}

// Switching theme repaints the curve editor's cached drawing (grid + curve layers) in the new colours.
static bool themeRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    Envelope floorCurve; floorCurve.points = { { 0.f, 0.f }, { 1.f, 0.f } };   // nothing drawn over the background
    p.setEnvelope(floorCurve, 0);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 400, 240);
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    auto luma = [&] { auto c = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f).getPixelAt(318, 130); return c.getBrightness(); };
    std::printf("theme switch:\n");
    paper::setDark(false);
    const float light = luma();
    paper::setDark(true);
    const float dark = luma();
    paper::setDark(false);
    const float lightAgain = luma();
    std::printf("  background brightness: light %.2f, dark %.2f, light again %.2f\n", light, dark, lightAgain);
    check(light > 0.7f, "light theme: bright curve area");
    check(dark < 0.25f, "dark theme: dark curve area (the cached drawing was redrawn)");
    check(lightAgain > 0.7f, "and back to light again");
    return ok;
}

// The grey "actual gain" line also appears for a phase offset, slid by that offset.
static bool offsetPreviewRegression(const juce::File& dir)
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 600, 300);
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("offset preview:\n");

    auto snap = [&] { return ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f); };
    auto differingPixels = [](const juce::Image& a, const juce::Image& b) {
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) ++n;
        return n;
    };

    auto noOffset = snap();
    auto* off = p.apvts.getParameter("offset");
    off->setValueNotifyingHost(off->convertTo0to1(0.25f));
    auto shifted = snap();
    save(shifted, dir.getChildFile("offset_preview.png"));
    off->setValueNotifyingHost(off->convertTo0to1(0.5f));
    auto shiftedMore = snap();
    off->setValueNotifyingHost(0.f);
    auto back = snap();

    const int d1 = differingPixels(noOffset, shifted), d2 = differingPixels(shifted, shiftedMore), d3 = differingPixels(noOffset, back);
    std::printf("  changed pixels: offset 0.25 vs none %d, 0.5 vs 0.25 %d, back to 0 vs none %d\n", d1, d2, d3);
    check(d1 > 300, "a phase offset draws the grey line");
    check(d2 > 300, "a different offset moves it");
    check(d3 == 0, "offset back to 0 removes it again");
    return ok;
}

// Header: nothing overlaps, the light/dark button sits just left of Reset, and dark is the default.
static bool headerRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    FreeLfoEditor ed(p);
    ed.setSize(960, 610);
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("header:\n");
    check(paper::dark, "dark mode is the default");

    juce::Component *theme = nullptr, *reset = nullptr, *grid = nullptr;
    std::vector<juce::Component*> row;
    for (int i = 0; i < ed.getNumChildComponents(); ++i)
    {
        auto* c = ed.getChildComponent(i);
        if (!c->isVisible() || c->getY() > 44 || c->getHeight() > 40) continue;
        row.push_back(c);
        if (dynamic_cast<ThemeToggle*>(c)) theme = c;
        if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == "Reset") reset = c;
        if (auto* s = dynamic_cast<juce::Slider*>(c)) if (s->isBar()) grid = c;
    }
    check(theme && reset && grid, "found the theme button, Reset and the grid number");
    if (!(theme && reset && grid)) return false;
    bool overlap = false;
    for (size_t i = 0; i < row.size(); ++i)
        for (size_t j = i + 1; j < row.size(); ++j)
            if (row[i]->getBounds().intersects(row[j]->getBounds())) overlap = true;
    check(!overlap, "no header items overlap");
    const int gapToReset = reset->getX() - theme->getRight();
    const int gapToGrid = theme->getX() - grid->getRight();
    std::printf("  gaps: theme -> Reset %d px, grid -> theme %d px\n", gapToReset, gapToGrid);
    check(gapToReset > 4 && gapToReset <= 14, "the light/dark button sits right next to Reset, on its left");
    check(gapToGrid >= 12, "...with room between it and the grid number");
    return ok;
}

// The grid is easy to see: a line is clearly different from the background between lines, in both themes.
static bool gridContrastRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    Envelope floorCurve; floorCurve.points = { { 0.f, 0.f }, { 1.f, 0.f } };   // a flat curve at the bottom: nothing is drawn over the grid
    p.setEnvelope(floorCurve, 0);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 600, 300);
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("grid visibility:\n");
    const bool wasDark = paper::dark;
    for (bool dark : { true, false })
    {
        paper::setDark(dark);
        auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
        const int y = 120;                                // lines at 10/16 (minor) and 12/16 (major): clear of the playhead trail
        const int majorX = (int) std::round(10.f + 0.75f * 580.f), minorX = (int) std::round(10.f + (10.f / 16.f) * 580.f), emptyX = majorX + 14;
        const float bgLuma = img.getPixelAt(emptyX, y).getBrightness();
        auto strongest = [&](int cx) { // a 1 px line can straddle two pixel columns: take the strongest of the neighbourhood
            float best = 0.f;
            for (int dx = -1; dx <= 1; ++dx) best = std::max(best, std::fabs(img.getPixelAt(cx + dx, y).getBrightness() - bgLuma));
            return best;
        };
        const float major = strongest(majorX), minor = strongest(minorX);
        std::printf("  %s: contrast against the background: major line %.3f, minor line %.3f\n", dark ? "dark " : "light", major, minor);
        check(major > 0.10f, dark ? "dark theme: major grid lines stand out" : "light theme: major grid lines stand out");
        check(minor > 0.045f, dark ? "dark theme: minor grid lines are visible" : "light theme: minor grid lines are visible");
        check(major > minor, "major lines are stronger than minor ones");
    }
    paper::setDark(wasDark);
    return ok;
}

int main(int argc, char** argv)
{
    setenv("FREELFO_NO_PREFS", "1", 1); // never touch the user's saved light / dark preference
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir(argc > 1 ? argv[1] : ".");
    g_outDir = dir.getFullPathName();
    constexpr double sr = 48000.0;
    constexpr int bs = 512;

    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(sr, bs);
    p.prepareToPlay(sr, bs);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); // sets editorOpen, so the waveform gets analysed
    ed->setSize(960, 610);

    // Material to look at: a 90 Hz "kick" every half second plus short hi-hat noise bursts
    juce::AudioBuffer<float> buf(2, bs);
    juce::MidiBuffer midi;
    juce::Random rng(5);
    long t = 0;
    auto run = [&](int blocks) {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < bs; ++i, ++t)
            {
                const double sec = (double) t / sr, inBeat = std::fmod(sec, 0.5);
                float v = (float) (std::sin(2.0 * juce::MathConstants<double>::pi * 90.0 * inBeat) * std::exp(-inBeat * 10.0) * 0.8);
                if (inBeat > 0.25 && inBeat < 0.28) v += (rng.nextFloat() * 2.f - 1.f) * 0.25f;
                buf.setSample(0, i, v); buf.setSample(1, i, v);
            }
            p.processBlock(buf, midi);
        }
    };

    run(400);
    save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("single.png"));

    setParam(p, "mb", 1.f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300); // lets the tab switch and the band controls rebind
    std::printf("mb raw value after set: %.1f, blockCounter %u\n", p.apvts.getRawParameterValue("mb")->load(), p.blockCounter.load());
    run(400);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("multiband.png"));

    // Dark theme: the same two tabs
    {
        auto* fe = static_cast<FreeLfoEditor*>(ed.get());
        fe->setDarkMode(true, false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        run(30);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("dark_multiband.png"));
        setParam(p, "mb", 0.f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        run(30);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("dark_single.png"));
        fe->setDarkMode(false, false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        run(30);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("light_single.png"));
        setParam(p, "mb", 1.f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        run(30);
    }

    // Make the bands' curves different so the faint reference curves are easy to see, then look at the mid band
    {
        Envelope dip; dip.points = { { 0.f, 1.f }, { 0.25f, 0.2f }, { 0.5f, 1.f }, { 0.75f, 0.2f }, { 1.f, 1.f } };
        Envelope saw; saw.points = { { 0.f, 0.f }, { 0.99f, 1.f }, { 1.f, 1.f } };
        p.setEnvelope(dip, 2);
        p.setEnvelope(saw, 3);
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        run(50);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("multiband_mid.png"));
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        run(50);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("multiband_high.png"));
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(0);
    }

    // Simple mode with the sharpest bends: a hard "knee" up, then a quick drop
    {
        setParam(p, "bezier", 0.f);
        Envelope sharp; sharp.points = { { 0.f, 0.f }, { 0.5f, 1.f }, { 1.f, 0.f } };
        sharp.bendSegmentThrough(0, 0.8f, 0.01f);   // slow, then abrupt rise at the end of the segment
        sharp.bendSegmentThrough(1, 0.1f, 0.01f);   // abrupt drop right after the peak
        p.setEnvelope(sharp, 1);
        // no display refresh runs headless, so hop to another band and back to make the editor reload this curve
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        run(20);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("simple_sharp.png"));
        setParam(p, "bezier", 1.f);
    }

    // Flip the switch: the default Bezier pump curve is converted to a single bend and stays editable
    {
        p.setEnvelope(Envelope(), 1);                    // the default pump, shaped with Bezier handles
        setParam(p, "bezier", 0.f);
        p.convertCurveMode(false);
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        run(20);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("converted_to_simple.png"));
        setParam(p, "bezier", 1.f);
        p.convertCurveMode(true);
    }

    setParam(p, "slopeLow", 5.f);  // 96 dB/oct on the low split
    setParam(p, "soloL", 1.f); setParam(p, "soloM", 1.f); // and two bands soloed at once
    setParam(p, "slopeHigh", 0.f); // 6 dB/oct on the high split
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    run(100);
    save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("multiband_slopes.png"));
    {
        // the reported case: the last edge pushed to its maximum steepness (handles straight up at the right edge)
        FreeLfoProcessor sp;
        sp.setRateAndBufferSizeDetails(48000.0, 512);
        sp.prepareToPlay(48000.0, 512);
        Envelope steep; steep.points = { { 0.f, 0.f, 0.f, 0.f, 0.1f, 0.5f },
                                        { 0.9f, 1.f, -0.2f, 0.f, 0.1f, 0.f },
                                        { 1.f, 0.f, -0.0001f, 1.f, 0.f, 0.f } };
        sp.setEnvelope(steep, 0);
        EnvelopeEditor ed2(sp, 0);
        ed2.setBounds(0, 0, 400, 300);
        save(ed2.createComponentSnapshot(ed2.getLocalBounds(), true, 2.0f), dir.getChildFile("steep_end.png"));
    }
    const bool a = handleNudgeRegression();
    const bool b = hideShowRegression();
    const bool c = newPointRegression();
    const bool d = endHandleRegression();
    const bool f = seamRegression();
    const bool g = snapRegression();
    const bool h = rateReadoutRegression();
    const bool k = gridStopsRegression();
    const bool m = themeRegression();
    const bool n = offsetPreviewRegression(dir);
    const bool o = headerRegression();
    const bool q = gridContrastRegression();
    return a && b && c && d && f && g && h && k && m && n && o && q ? 0 : 1;
}
