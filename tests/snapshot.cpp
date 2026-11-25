// Renders the editor to PNG files without a screen, so the UI can be checked headlessly.
// Build: cmake -B build -DFREELFO_BENCH=ON && cmake --build build --target LfoSnap
// Run:   build/LfoSnap_artefacts/Release/LfoSnap <output-dir>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Presets.h"
#include "fake_host.h"
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
    void dragCtrl(juce::Point<float> pos) { ed.mouseDrag(make(pos, true, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier)); }
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
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };

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
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
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
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
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
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
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
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
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
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
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

// The note values under the graph must follow the rate that really drives each curve.
static bool labelRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    bool ok = true;
    std::printf("graph position labels:\n");
    auto expect = [&](int slot, const char* want, const char* what) {
        const EnvelopeEditor e(p, slot);
        const auto l = e.positionLabels();
        const auto got = l[0] + "," + l[1] + "," + l[2] + "," + l[3];
        const bool good = got == want;
        std::printf("  %-52s %s  (%s)\n", what, good ? "ok" : "FAILED", got.toRawUTF8());
        ok = ok && good;
    };
    expect(0, "1/16,1/8,3/16,1/4", "single, 1/4");
    setParam(p, "division", 2.f);
    expect(0, "1/2,1 bar,1.5 bars,2 bars", "single, 2 bars");
    setParam(p, "division", 6.f); setParam(p, "triplet", 1.f);
    expect(0, "1/32T,1/16T,3/32T,1/8T", "single, 1/8 triplet");
    setParam(p, "mode", 1.f);
    expect(0, ",,,", "single, Hz: no labels");
    for (int s = 1; s <= 3; ++s) expect(s, "1/16,1/8,3/16,1/4", "multiband, linked, default");
    setParam(p, "divM", 6.f); setParam(p, "dotM", 1.f);
    for (int s = 1; s <= 3; ++s) expect(s, "1/32D,1/16D,3/32D,1/8D", "multiband, linked: every band shows the mid rate");
    setParam(p, "mbLink", 0.f); setParam(p, "divL", 4.f); setParam(p, "divH", 7.f); setParam(p, "tripH", 1.f);
    expect(1, "1/8,1/4,3/8,1/2", "multiband, unlinked, low");
    expect(2, "1/32D,1/16D,3/32D,1/8D", "multiband, unlinked, mid");
    expect(3, "1/64T,1/32T,3/64T,1/16T", "multiband, unlinked, high");
    setParam(p, "modeH", 1.f);
    expect(3, ",,,", "multiband, high band in Hz: no labels");
    setParam(p, "mbLink", 1.f); setParam(p, "mbBands", 0.f);
    expect(3, "1/8,1/4,3/8,1/2", "2 bands, linked: follows the low rate");
    return ok;
}

// Ctrl-click (Cmd on macOS) and Alt-click put a knob back to its default.
static bool resetClickRegression()
{
    bool ok = true;
    std::printf("knob reset clicks:\n");
    auto click = [](juce::Slider& s, int mods) {
        const juce::ModifierKeys m(mods | juce::ModifierKeys::leftButtonModifier);
        const auto at = s.getLocalBounds().getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();
        auto event = [&](juce::Point<float> p, bool dragged) {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    &s, &s, now, at, now, 1, dragged);
        };
        juce::Component& c = s;
        c.mouseDown(event(at, false));
        c.mouseDrag(event(at.translated(0.f, -40.f), true));   // a shaky hand must not move it off the default again
        c.mouseUp(event(at.translated(0.f, -40.f), true));
    };
    auto expect = [&](juce::Slider& s, int mods, const char* what) {
        s.setBounds(0, 0, 60, 60);
        s.setRange(-24.0, 24.0, 0.1);
        s.setDoubleClickReturnValue(true, 0.0);   // as the parameter attachment does it
        s.setValue(9.0, juce::dontSendNotification);
        click(s, mods);
        const bool good = std::abs(s.getValue()) < 1e-9;
        std::printf("  %-52s %s\n", what, good ? "ok" : "FAILED");
        ok = ok && good;
    };
    { TrimKnob k; expect(k, juce::ModifierKeys::commandModifier, "gain knob, Ctrl-click (Cmd on macOS)"); }
    { TrimKnob k; expect(k, juce::ModifierKeys::ctrlModifier, "gain knob, the Ctrl key itself"); }
    { TrimKnob k; expect(k, juce::ModifierKeys::altModifier, "gain knob, Alt-click"); }
    { Knob k; k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); expect(k, juce::ModifierKeys::commandModifier, "main knob, Ctrl-click (Cmd on macOS)"); }
    { Knob k; k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); expect(k, juce::ModifierKeys::altModifier, "main knob, Alt-click"); }
    {   // typing into the value box ends in snapValue(): "nan" and "inf" must leave the knob alone
        Knob k; k.setRange(0.0, 200.0, 0.01); k.setValue(12.0, juce::dontSendNotification);
        bool good = true;
        for (const char* typed : { "nan", "NaN", "inf", "-inf" })
            good = good && k.snapValue(juce::String(typed).getDoubleValue(), juce::Slider::notDragging) >= 0.0
                        && std::isfinite(k.snapValue(juce::String(typed).getDoubleValue(), juce::Slider::notDragging));
        good = good && k.snapValue(std::numeric_limits<double>::quiet_NaN(), juce::Slider::notDragging) == 12.0;
        std::printf("  %-52s %s\n", "typing nan / inf into a knob is ignored", good ? "ok" : "FAILED");
        ok = ok && good;
    }
    return ok;
}

// A stand-in for a DAW that offers right-click options for a parameter: it remembers which parameter was asked about.
struct FakeHostMenus : juce::AudioProcessorEditorHostContext
{
    enum Offer { anItem, noMenu, emptyMenu };
    Offer offer = anItem;
    mutable juce::String asked, ran;

    struct Menu : juce::HostProvidedContextMenu
    {
        const FakeHostMenus& host;
        const juce::String id;
        Menu(const FakeHostMenus& h, juce::String i) : host(h), id(std::move(i)) {}
        juce::PopupMenu getEquivalentPopupMenu() const override
        {
            juce::PopupMenu m;
            if (host.offer == anItem) m.addItem("Create automation clip", true, false, [h = &host, i = id] { h->ran = i; });
            return m;
        }
        void showNativeMenu(juce::Point<int>) const override {}
    };
    std::unique_ptr<juce::HostProvidedContextMenu> getContextMenuForParameter(const juce::AudioProcessorParameter* prm) const override
    {
        const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(prm);
        asked = withId != nullptr ? withId->paramID : juce::String();
        if (offer == noMenu) return {};
        return std::make_unique<Menu>(*this, asked);
    }
};

// Right-clicking a knob offers the DAW's options for exactly the parameter that knob is driving at that moment.
static bool hostMenuRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    FakeHostMenus host;
    bool ok = true;
    std::printf("right-click DAW options:\n");
    auto report = [&](bool good, const char* what, const juce::String& detail) {
        std::printf("  %-52s %s  (%s)\n", what, good ? "ok" : "FAILED", detail.toRawUTF8());
        ok = ok && good;
    };

    // every knob on screen, left to right then top to bottom: the parameter its menu asks the DAW about
    auto knobs = [](juce::Component& root) {
        std::vector<ResetClickSlider*> found;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
            for (auto* child : c.getChildren())
                if (child->isVisible()) { if (auto* k = dynamic_cast<ResetClickSlider*>(child)) found.push_back(k); walk(*child); }
        };
        walk(root);
        return found;
    };
    auto expect = [&](const char* what, juce::StringArray want) {
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); // built from the current settings
        ed->setHostContext(&host);
        juce::StringArray got;
        bool offered = true;
        for (auto* k : knobs(*ed))
        {
            host.asked.clear();
            bool listed = false;
            const auto menu = k->buildMenu(); // the iterator only borrows it
            for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                if (it.getItem().text == "Create automation clip")
                {
                    listed = true;
                    host.ran.clear();
                    it.getItem().action(); // choosing it must reach the DAW's own action
                    listed = host.ran == host.asked;
                }
            offered = offered && listed;
            got.add(host.asked);
        }
        got.sort(false); want.sort(false);
        report(offered && got == want, what, got.joinIntoString(" "));
        p.editorBeingDeleted(ed.get());
    };

    expect("single, Sync", { "division", "depth", "smooth", "offset", "waveOpacity", "inGain", "outGain" });
    setParam(p, "mode", 1.f);
    expect("single, Hz", { "hz", "depth", "smooth", "offset", "waveOpacity", "inGain", "outGain" });
    setParam(p, "mb", 1.f); // multiband opens on the low band, with the rates linked to the mid band
    expect("multiband, low band, linked rate", { "divM", "depthL", "smoothL", "offsetL", "waveOpacity", "inGain", "outGain" });
    setParam(p, "mbLink", 0.f); setParam(p, "modeL", 1.f);
    expect("multiband, low band, own rate in Hz", { "hzL", "depthL", "smoothL", "offsetL", "waveOpacity", "inGain", "outGain" });
    setParam(p, "linkDepth", 1.f); setParam(p, "linkSmooth", 1.f); setParam(p, "linkOffset", 1.f);
    expect("multiband, linked knobs: the shared (mid) parameter", { "hzL", "depthM", "smoothM", "offsetM", "waveOpacity", "inGain", "outGain" });

    // a DAW with nothing to offer, or no support at all, leaves just our own choices
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto ownOnly = [&](const char* what) {
        bool good = true;
        for (auto* k : knobs(*ed))
        {
            const int own = dynamic_cast<Knob*>(k) != nullptr ? 2 : 1; // the small gain knobs have no typing box
            good = good && k->buildMenu().getNumItems() == own;
        }
        report(good, what, "own choices only");
    };
    ownOnly("DAW without right-click support");
    ed->setHostContext(&host);
    host.offer = FakeHostMenus::noMenu;    ownOnly("DAW returns no menu");
    host.offer = FakeHostMenus::emptyMenu; ownOnly("DAW returns an empty menu");
    p.editorBeingDeleted(ed.get());
    return ok;
}

// Double-clicking at the 64-point limit adds nothing and says why; older curves with more points stay editable.
static bool pointLimitRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    bool ok = true;
    std::printf("point limit:\n");
    auto check = [&](bool cond, const char* what) { std::printf("  %-60s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    auto flat = [](int n) {   // n points along the middle line
        Envelope e; e.points.clear();
        for (int i = 0; i < n; ++i) e.points.push_back({ (float) i / (float) (n - 1), 0.5f });
        e.normalize();
        return e;
    };
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
    p.setEnvelope(flat(63), 0);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    MouseDriver m { ed };
    auto doubleClick = [&](juce::Point<float> at) { m.downPos = at; ed.mouseDoubleClick(m.make(at, false, juce::ModifierKeys::leftButtonModifier)); };
    auto count = [&] { return (int) p.getEnvelope(0).points.size(); };

    doubleClick(toScreen(0.508f, 0.9f));
    check(count() == 64 && !ed.isShowingLimitNote(), "the 64th point can be added");
    const auto full = p.getEnvelope(0).serialize();
    doubleClick(toScreen(0.31f, 0.15f));
    check(count() == 64 && p.getEnvelope(0).serialize() == full, "a 65th is not added");
    check(ed.isShowingLimitNote(), "and a note says why");
    save(ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f), juce::File(g_outDir).getChildFile("point_limit.png"));
    p.undo();
    check(count() == 63, "the refused click left no undo step: undo removes the 64th");
    p.redo();
    doubleClick(toScreen(0.508f, 0.9f));
    check(count() == 63, "a point can still be deleted at the limit");

    p.setEnvelope(flat(100), 0);   // as loaded from a project made before the limit
    EnvelopeEditor old(p, 0);
    old.setBounds(0, 0, 800, 400);
    MouseDriver om { old };
    auto oldClick = [&](juce::Point<float> at) { om.downPos = at; old.mouseDoubleClick(om.make(at, false, juce::ModifierKeys::leftButtonModifier)); };
    oldClick(toScreen(0.31f, 0.15f));
    check(count() == 100 && old.isShowingLimitNote(), "over-limit older curve: unchanged, nothing can be added");
    const auto at = p.getEnvelope(0).points[50];
    oldClick(toScreen(at.x, at.y));
    check(count() == 99, "over-limit older curve: points can be deleted");

    // the preset popup says why it left a file out
    const auto file = PresetMenu::userFolder().getChildFile("zz_too_many.lfocurve");
    file.replaceWithText(presets::toText(flat(80)));
    {
        PaperLookAndFeel laf; laf.applyColours();
        auto panel = PresetMenu::createPanel(p, 0, nullptr, laf);
        save(panel->createComponentSnapshot(panel->getLocalBounds(), true, 1.0f), juce::File(g_outDir).getChildFile("point_limit_presets.png"));
    }
    file.deleteFile();
    return ok;
}

// The coloured output on the graph is input x gain, also for input over 0 dB: the gain is applied first, then the
// picture is cut off at the edge (not the other way round).
static bool loudScopeRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    p.editorOpen.store(true);
    Envelope half; half.points = { { 0.f, 0.5f }, { 1.f, 0.5f } };
    p.setEnvelope(half, 0);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
    long t = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < 512; ++i, ++t) { const float v = 1.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * (double) t / 48000.0); buf.setSample(0, i, v); buf.setSample(1, i, v); }
        p.processBlock(buf, midi);
    }
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    const auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
    save(img, juce::File(g_outDir).getChildFile("loud_scope.png"));
    // above the middle only the waveforms have colour (the input is grey): find how high the coloured output reaches
    int top = 400;
    for (int x = 60; x < 740; ++x)
        for (int y = 12; y < top; ++y)
        {
            const auto c = img.getPixelAt(x, y);
            if (c.getSaturation() > 0.45f && c.getBrightness() > 0.35f) { top = y; break; }
        }
    const float height = (386.f - (float) top) / 376.f * 2.f - 1.f; // in the scope's -1 .. +1
    const bool ok = std::fabs(height - 0.75f) < 0.05f;
    std::printf("loud input on the graph:\n  %-52s %s  (reaches %.2f, input 1.5 x gain 0.5 = 0.75)\n", "output drawn as input x gain above 0 dB", ok ? "ok" : "FAILED", height);
    return ok;
}

// The window can be resized: every control must stay inside it, without overlapping, at the smallest and a large size.
static bool resizeRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    bool ok = true;
    std::printf("resizing:\n");
    for (int mb = 0; mb < 2; ++mb)
    {
        FreeLfoEditor ed(p);
        ed.setSize(960, 610);
        if (mb) { setParam(p, "mb", 1.f); juce::MessageManager::getInstance()->runDispatchLoopUntil(300); }
        const int sizes[][2] = { { 880, 500 }, { 960, 610 }, { 1500, 900 }, { 2000, 1300 } };
        for (auto& sz : sizes)
        {
            ed.setSize(sz[0], sz[1]);
            const auto b = ed.getLocalBounds();
            bool inside = true, noOverlap = true;
            std::vector<juce::Rectangle<int>> r;
            for (int i = 0; i < ed.getNumChildComponents(); ++i)
            {
                auto* c = ed.getChildComponent(i);
                if (!c->isVisible()) continue;
                inside = inside && b.contains(c->getBounds());
                if (dynamic_cast<juce::Slider*>(c) || dynamic_cast<juce::ComboBox*>(c) || c->getBounds().getBottom() <= 56) r.push_back(c->getBounds()); // knobs, dropdowns and every header control
            }
            for (size_t i = 0; i < r.size(); ++i)
                for (size_t j = i + 1; j < r.size(); ++j)
                    if (r[i].intersects(r[j])) noOverlap = false;
            const bool good = inside && noOverlap && ed.getWidth() == sz[0];
            std::printf("  %s %dx%d  %s\n", mb ? "multiband" : "single   ", sz[0], sz[1], good ? "ok" : "FAILED");
            ok = ok && good;
            if (sz[0] == 880 || sz[0] == 1500)
                save(ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f),
                     juce::File(g_outDir).getChildFile(juce::String(mb ? "resize_mb_" : "resize_single_") + juce::String(sz[0]) + ".png"));
        }
    }
    setParam(p, "mb", 0.f);
    return ok;
}

// Undo / redo: one step per drag, redo, a new edit clears redo, reset and the Bezier switch are undoable, capped history.
static bool undoRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    auto ser = [&](int slot = 0) { return p.getEnvelope(slot).serialize(); };
    std::printf("undo / redo:\n");

    const auto e0 = ser();
    check(!p.canUndo() && !p.canRedo(), "nothing to undo at the start");

    MouseDriver m { ed };
    const auto pt = p.getEnvelope(0).points[2];
    const auto from = toScreen(pt.x, pt.y);
    m.down(from);
    for (int i = 1; i <= 8; ++i) m.drag(from + juce::Point<float>(3.f * (float) i, -2.f * (float) i));
    m.up(from + juce::Point<float>(24.f, -16.f));
    const auto e1 = ser();
    check(e1 != e0, "the drag changed the curve");
    check(p.undoDepth() == 1, "a whole drag is ONE undo step");

    check(p.undo() && ser() == e0, "undo restores the curve exactly");
    check(p.canRedo() && p.redo() && ser() == e1, "redo brings the drag back exactly");

    p.undo();
    const auto q = p.getEnvelope(0).points[3];
    const auto from2 = toScreen(q.x, q.y);
    m.down(from2); m.drag(from2 + juce::Point<float>(0.f, 20.f)); m.up(from2 + juce::Point<float>(0.f, 20.f));
    check(!p.canRedo(), "a new edit after undo clears the redo history");

    // keyboard shortcuts
    const auto afterEdit = ser();
    ed.keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 'z'));
    check(ser() == e0, "Cmd+Z undoes");
    ed.keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 'z'));
    check(ser() == afterEdit, "Shift+Cmd+Z redoes");

    // reset is one undoable step
    ed.resetCurve();
    const auto afterReset = ser();
    p.undo();
    check(ser() == afterEdit && afterReset != afterEdit, "reset can be undone");

    // the Bezier switch converts every curve; undo puts back curves AND the switch
    const auto before = ser();
    auto* bez = p.apvts.getParameter("bezier");
    bez->setValueNotifyingHost(0.f);
    p.convertCurveMode(false);
    check(ser() != before, "switching to simple mode converted the curve");
    p.undo();
    check(ser() == before && bez->getValue() > 0.5f, "undo restores the curve and the Bezier switch (no double conversion)");
    p.redo();
    check(bez->getValue() < 0.5f, "redo flips the switch back");
    p.undo();

    // capped history
    for (int i = 0; i < 120; ++i)
    {
        auto e = p.getEnvelope(0);
        e.points[2].y = 0.2f + 0.005f * (float) i;
        p.pushUndo(p.captureUndoState());
        p.setEnvelope(e, 0);
    }
    check(p.undoDepth() == FreeLfoProcessor::maxUndo, "history is capped");
    const size_t perEntry = sizeof(FreeLfoProcessor::UndoState) + 4 * p.getEnvelope(0).points.size() * sizeof(EnvPoint);
    std::printf("  about %zu bytes per entry, about %zu KB for a full history\n", perEntry, perEntry * FreeLfoProcessor::maxUndo / 1024);
    return ok;
}

// Presets and copy / paste: apply to the right curve, convert to the current style, undoable, clipboard round trip.
static bool presetRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("presets:\n");

    Envelope gate4;
    for (auto& pr : presets::factory()) if (pr.name == "Hard duck") gate4 = pr.env;
    const auto single0 = p.getEnvelope(0).serialize(), mid0 = p.getEnvelope(2).serialize();
    p.applyCurve(gate4, 2);
    check(p.getEnvelope(2).serialize() != mid0, "a preset replaces the chosen band's curve");
    check(p.getEnvelope(0).serialize() == single0, "the other curves are untouched");
    check(std::fabs(p.getEnvelope(2).evaluate(0.30f) - gate4.evaluate(0.30f)) < 1e-4f, "the curve really is the preset");
    p.undo();
    check(p.getEnvelope(2).serialize() == mid0, "applying a preset can be undone");

    // simple style: the preset arrives as simple bends, and still sounds like the same shape
    auto* bez = p.apvts.getParameter("bezier");
    bez->setValueNotifyingHost(0.f);
    Envelope sine;
    for (auto& pr : presets::factory()) if (pr.name == "Sine") sine = pr.env;
    p.applyCurve(sine, 0);
    bool hasHandles = false;
    for (auto& pt : p.getEnvelope(0).points) hasHandles = hasHandles || pt.outDx != 0.f || pt.inDx != 0.f;
    float worst = 0.f;
    for (int i = 0; i <= 100; ++i) worst = std::max(worst, std::fabs(p.getEnvelope(0).evaluate(i / 100.f) - sine.evaluate(i / 100.f)));
    std::printf("  simple-mode conversion of the sine: max error %.3f\n", worst);
    check(!hasHandles && worst < 0.03f, "in simple mode a preset is converted to bends (and stays close)");
    bez->setValueNotifyingHost(1.f);

    // clipboard text
    Envelope back;
    const auto text = presets::toText(p.getEnvelope(1));
    check(presets::fromText(text, back) && back.serialize() == p.getEnvelope(1).serialize(), "copy then paste gives the same curve");
    check(!presets::fromText("not a curve", back), "pasting something else is refused");

    // multiband presets set every band and the band count, as one undo step
    {
        const auto sets = presets::factoryMultiband();
        const BandPreset *three = nullptr, *two = nullptr;
        for (const auto& s : sets) { if (s.three && three == nullptr) three = &s; if (!s.three && two == nullptr) two = &s; }
        auto bands = [&] { return p.apvts.getRawParameterValue("mbBands")->load() > 0.5f ? 3 : 2; };
        auto curves = [&] { juce::String s; for (int k = 0; k < FreeLfoProcessor::numSlots; ++k) s << p.getEnvelope(k).serialize() << "|"; return s; };
        auto follows = [&](int slot, const Envelope& want) {
            float worstGap = 0.f;
            for (int i = 0; i <= 200; ++i) worstGap = std::max(worstGap, std::fabs(p.getEnvelope(slot).evaluate(i / 200.f) - want.evaluate(i / 200.f)));
            return worstGap < 0.03f;
        };
        const auto start = curves();
        const auto singleBefore = p.getEnvelope(0).serialize(), midBefore = p.getEnvelope(2).serialize();
        p.applyBandCurves({ two->low, two->mid, two->high }, false);
        check(bands() == 2 && follows(1, two->low) && follows(3, two->high), "a 2-band preset sets low and high and switches to 2 bands");
        check(p.getEnvelope(2).serialize() == midBefore && p.getEnvelope(0).serialize() == singleBefore, "it leaves the mid and single curves alone");
        const auto afterTwo = curves();
        p.undo();
        check(bands() == 3 && curves() == start, "one undo restores the curves and the band count");
        p.redo();
        check(bands() == 2 && curves() == afterTwo, "redo brings both back");
        p.applyBandCurves({ three->low, three->mid, three->high }, true);
        check(bands() == 3 && follows(1, three->low) && follows(2, three->mid) && follows(3, three->high), "a 3-band preset sets all three and switches to 3 bands");
        p.undo(); p.undo();
        check(bands() == 3 && curves() == start, "undoing both gets back to where we started");
        // an ordinary curve edit must not drag the band count back with it
        p.applyCurve(three->low, 1);
        p.apvts.getParameter("mbBands")->setValueNotifyingHost(0.f);
        p.undo();
        check(bands() == 2, "undoing a single-curve change leaves the band count alone");
        p.apvts.getParameter("mbBands")->setValueNotifyingHost(1.f);
    }

    // user preset files
    const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("FreeLfoTestPresets");
    f.createDirectory();
    const auto file = f.getChildFile("test-curve.lfocurve");
    file.replaceWithText(text);
    Envelope loaded;
    check(presets::fromText(file.loadFileAsString().trim().toStdString(), loaded) && loaded.serialize() == back.serialize(), "a saved preset file loads back exactly");
    file.deleteFile();
    return ok;
}

// Bezier handles snap to the grid with Option/Alt or Ctrl held, and move freely without.
// Double-click removes a point (not the first / last one); right-click no longer does.
static bool deletePointRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("deleting points:\n");

    p.setEnvelope(Envelope(), 0);
    p.apvts.getParameter("bezier")->setValueNotifyingHost(1.f);
    const auto before = p.getEnvelope(0);
    const size_t n = before.points.size();
    const auto pt = before.points[2];
    const auto pos = toScreen(pt.x, pt.y);

    MouseDriver m { ed };
    m.down(pos);
    m.ed.mouseUp(m.make(pos, false));
    // (a right-click: press and release with the popup modifier)
    ed.mouseDown(m.make(pos, false, juce::ModifierKeys::rightButtonModifier));
    ed.mouseUp(m.make(pos, false));
    check(p.getEnvelope(0).points.size() == n, "right-click leaves the point alone");

    ed.mouseDoubleClick(m.make(pos, false, juce::ModifierKeys::leftButtonModifier));
    check(p.getEnvelope(0).points.size() == n - 1, "double-click deletes an interior point");

    const auto first = toScreen(before.points.front().x, before.points.front().y);
    ed.mouseDoubleClick(m.make(first, false, juce::ModifierKeys::leftButtonModifier));
    check(p.getEnvelope(0).points.size() == n - 1, "double-click on the first point does not delete it");
    check(p.undo() && p.getEnvelope(0).points.size() == n, "the deletion can be undone");
    return ok;
}

static bool handleSnapRegression()
{
    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    EnvelopeEditor ed(p, 0);
    ed.setBounds(0, 0, 800, 400);
    auto toScreen = [](float x, float y) { return juce::Point<float>(10.f + x * 780.f, 386.f - y * 376.f); };
    bool ok = true;
    auto check = [&](bool cond, const char* what) { std::printf("  %-64s %s\n", what, cond ? "ok" : "FAILED"); ok = ok && cond; };
    std::printf("handle snapping:\n");

    p.setEnvelope(Envelope(), 0);
    auto* bez = p.apvts.getParameter("bezier");
    bez->setValueNotifyingHost(1.f);
    const int gx = ed.gridX(), gy = ed.gridY();
    auto onGrid = [&](float x, float y) {
        const float fx = x * gx, fy = y * gy;
        return std::fabs(fx - std::round(fx)) < 1e-3f && std::fabs(fy - std::round(fy)) < 1e-3f;
    };

    MouseDriver m { ed };
    const int sel = 3;                                           // the plateau start: it has a visible out handle
    const auto top = toScreen(p.getEnvelope(0).points[(size_t) sel].x, p.getEnvelope(0).points[(size_t) sel].y);
    m.down(top); m.up(top);                                      // select it
    auto grab = [&](int kind) { const auto h = kind == 2 ? p.getEnvelope(0).outHandle((size_t) sel) : p.getEnvelope(0).inHandle((size_t) sel); return toScreen(h.first, h.second); };

    for (int mode = 0; mode < 3; ++mode)                         // 0 = Alt, 1 = Ctrl, 2 = nothing
    {
        p.setEnvelope(Envelope(), 0);
        const auto start = grab(2);
        const auto target = start + juce::Point<float>(23.7f, 31.3f);   // an off-grid place
        m.down(start);
        if (mode == 0) m.dragAlt(target); else if (mode == 1) m.dragCtrl(target); else m.drag(target);
        m.up(target);
        const auto q = p.getEnvelope(0);
        const auto h = q.outHandle((size_t) sel);
        const bool snapped = onGrid(h.first, h.second);
        std::printf("  %s: handle tip at (%.4f, %.4f)\n", mode == 0 ? "Alt " : mode == 1 ? "Ctrl" : "free", h.first, h.second);
        check(mode == 2 ? !snapped : snapped, mode == 0 ? "Alt snaps the handle tip to the grid" : mode == 1 ? "Ctrl snaps the handle tip to the grid" : "no key: the handle moves freely");
    }
    return ok;
}

int main(int argc, char** argv)
{
    bool kickButtonOk = true;
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
    // Without a screen nothing drives the window's animation clock, so step it while the message loop runs
    auto settle = [](juce::AudioProcessorEditor& window, int ms) {
        for (int done = 0; done < ms; done += 33)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(33);
            if (auto* lfo = dynamic_cast<FreeLfoEditor*>(&window)) lfo->animate();
        }
    };
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

    { // a sub bass note that is still sounding when the curve drops: the click risk marker
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < bs; ++i, ++t)
            {
                const float v = 0.8f * (float) std::cos(2.0 * juce::MathConstants<double>::pi * 50.0 * (double) t / sr);
                buf.setSample(0, i, v); buf.setSample(1, i, v);
            }
            p.processBlock(buf, midi);
        }
        settle(*ed, 500); // the meter eases toward its values
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("click_risk.png"));
    }

    { // a bass note and a kick routed in through the sidechain: where they overlap in the cycle is hatched
        for (bool mbOn : { false, true })
        {
            FreeLfoProcessor q;
            q.setRateAndBufferSizeDetails(sr, bs);
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses = { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo() };
            layout.outputBuses = { juce::AudioChannelSet::stereo() };
            q.setBusesLayout(layout);
            q.prepareToPlay(sr, bs);
            std::unique_ptr<juce::AudioProcessorEditor> qe(q.createEditor());
            qe->setSize(960, 610);
            if (mbOn) setParam(q, "mb", 1.f); else setParam(q, "depth", 0.25f);   // weak ducking: the bass still sits on the kick
            settle(*qe, 300);
            juce::AudioBuffer<float> b4(4, bs);
            long tq = 0;
            for (int blk = 0; blk < 500; ++blk)
            {
                for (int i = 0; i < bs; ++i, ++tq)
                {
                    const double sec = (double) tq / sr, inBeat = std::fmod(sec, 0.5);
                    const float bass = 0.6f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 55.0 * sec);
                    const float kickSig = (float) (std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * inBeat) * std::exp(-inBeat * 14.0) * 0.9);
                    b4.setSample(0, i, bass); b4.setSample(1, i, bass); b4.setSample(2, i, kickSig); b4.setSample(3, i, kickSig);
                }
                q.processBlock(b4, midi);
            }
            settle(*qe, 300);
            save(qe->createComponentSnapshot(qe->getLocalBounds(), true, 1.0f), dir.getChildFile(mbOn ? "overlap_multiband.png" : "overlap_single.png"));

            // the Safe and Precise buttons: press each with a kick on the sidechain, at full depth, and look at the result
            {
                auto* window = dynamic_cast<FreeLfoEditor*>(qe.get());
                std::vector<KickButton*> chips;
                for (auto* child : window->getChildren()) if (auto* b = dynamic_cast<KickButton*>(child)) chips.push_back(b);
                bool clear = chips.size() == 2;
                for (const auto size : { juce::Point<int> { 880, 500 }, juce::Point<int> { 2000, 1300 }, juce::Point<int> { 960, 610 } })
                {
                    window->setSize(size.x, size.y);
                    for (auto* chip : chips)
                    {
                        // inside the window and clear of every other control that can be clicked
                        std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
                            for (auto* child : c.getChildren())
                            {
                                if (!child->isVisible() || child == chip) continue;
                                const auto area = window->getLocalArea(child->getParentComponent(), child->getBounds());
                                const bool knob = dynamic_cast<juce::Slider*>(child) != nullptr;
                                // a big knob's bounds are its whole column: what matters is the round knob in the middle
                                const auto hit = knob && area.getWidth() > 70 ? area.withSizeKeepingCentre(76, area.getHeight()) : area;
                                if ((knob || dynamic_cast<juce::Button*>(child) != nullptr) && hit.intersects(chip->getBounds())) clear = false;
                                walk(*child);
                            }
                        };
                        walk(*window);
                        clear = clear && chip->isVisible() && window->getLocalBounds().contains(chip->getBounds());
                    }
                }
                const int slot = mbOn ? 1 : 0;
                setParam(q, "depth", 1.f);
                setParam(q, "depthL", 1.f);
                FakeHost host2;
                host2.bpm = 130.0;
                // a bass that is plucked on every 16th, so there is room for Precise to use
                auto feed = [&](int blocks) {
                    for (int blk = 0; blk < blocks; ++blk)
                    {
                        for (int i = 0; i < bs; ++i, ++tq)
                        {
                            const double sec = (double) tq / sr, inBeat = std::fmod(sec, 60.0 / 130.0);
                            const float bass = (float) (0.75 * std::sin(2.0 * juce::MathConstants<double>::pi * 55.0 * sec) * std::exp(-std::fmod(inBeat, 15.0 / 130.0) / 0.07));
                            const float kickSig = (float) (std::sin(2.0 * juce::MathConstants<double>::pi * 60.0 * inBeat) * std::exp(-inBeat * 9.0) * 0.9);
                            b4.setSample(0, i, bass); b4.setSample(1, i, bass); b4.setSample(2, i, kickSig); b4.setSample(3, i, kickSig);
                        }
                        q.processBlock(b4, midi);
                        host2.ppq += bs * 130.0 / 60.0 / sr;
                    }
                };
                q.setPlayHead(&host2);
                host2.ppq = (double) tq / sr * 130.0 / 60.0;
                feed(300);
                for (auto* chip : chips)
                {
                    const auto beforeFit = q.getEnvelope(slot).serialize();
                    chip->onClick();
                    const bool made = q.getEnvelope(slot).serialize() != beforeFit && window->currentNote().startsWith(chip->precise ? "Precise" : "Safe");
                    const auto note = window->currentNote();
                    feed(300); // so the scope shows what the new curve does to the sound
                    settle(*qe, 300);
                    const juce::String name = juce::String("kickfit_") + (mbOn ? "multiband_" : "single_") + (chip->precise ? "precise" : "safe") + ".png";
                    save(qe->createComponentSnapshot(qe->getLocalBounds(), true, 1.0f), dir.getChildFile(name));
                    std::printf("%s button, %s: clear of other controls %s, makes a curve and says so %s  (%d points: %s)\n", chip->precise ? "Precise" : "Safe",
                                mbOn ? "multiband" : "single", clear ? "ok" : "FAILED", made ? "ok" : "FAILED", (int) q.getEnvelope(slot).points.size(), note.toRawUTF8());
                    kickButtonOk = kickButtonOk && clear && made;
                }
                q.setPlayHead(nullptr);
            }
        }
    }

    { // bypass switched on from the parameter side (what the host's bypass button does): the button must light up
        {   // the bypass button must be what a click on it lands on, in both tabs
            auto* fe = dynamic_cast<FreeLfoEditor*>(ed.get());
            (void) fe;
            ed->setVisible(true);   // only so getComponentAt works on this off-screen editor
            for (bool mbOn : { false, true })
            {
                setParam(p, "mb", mbOn ? 1.f : 0.f);
                settle(*ed, 300);
                juce::Component* hit = ed->getComponentAt(ed->getWidth() - 18 - 17, 18 + 18);
                std::printf("bypass click target (%s tab): %s (%s)\n", mbOn ? "multiband" : "single", hit != nullptr && dynamic_cast<juce::ToggleButton*>(hit) != nullptr ? "the bypass button" : "SOMETHING ELSE", hit != nullptr ? typeid(*hit).name() : "none");
            }
            setParam(p, "mb", 0.f);
            settle(*ed, 300);
        }
        setParam(p, "mb", 1.f);
        setParam(p, "clip", 1.f);
        settle(*ed, 300);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("clipper_soft.png"));
        setParam(p, "clip", 2.f);
        settle(*ed, 300);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("clipper_hard.png"));
        setParam(p, "clip", 0.f);
        setParam(p, "mbEngine", 1.f);
        settle(*ed, 300);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("multiband_dyneq.png"));
        setParam(p, "mbEngine", 0.f);
        setParam(p, "mb", 0.f);
        setParam(p, "bypass", 1.f);
        settle(*ed, 300);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("bypass_on.png"));
        setParam(p, "bypass", 0.f);
        settle(*ed, 200);
    }

    setParam(p, "mb", 1.f);
    settle(*ed, 300); // lets the tab switch and the band controls rebind
    std::printf("mb raw value after set: %.1f, blockCounter %u\n", p.apvts.getRawParameterValue("mb")->load(), p.blockCounter.load());
    run(400);
    settle(*ed, 100);
    save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("multiband.png"));
    for (int i = 0; i < 12; ++i) { run(10); settle(*ed, 40); } // keeps the live spectrum fed
    save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("multiband_spectrum.png"));

    // Dark theme: the same two tabs
    {
        auto* fe = static_cast<FreeLfoEditor*>(ed.get());
        fe->setDarkMode(true, false);
        settle(*ed, 300);
        run(30);
        settle(*ed, 100); // the meters redraw on a timer: let them catch up with this audio
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("dark_multiband.png"));
        setParam(p, "mb", 0.f);
        settle(*ed, 300);
        run(200); // several whole cycles, so no part of the scope still shows earlier material
        settle(*ed, 100); // the meters redraw on a timer: let them catch up with this audio
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("dark_single.png"));
        for (auto* c : ed->getChildren())   // the first knob is Rate: open its type-in box
            if (auto* k = dynamic_cast<Knob*>(c)) { k->showTextBox(); break; }
        settle(*ed, 200);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("knob_typein.png"));
        juce::Component::unfocusAllComponents();
        settle(*ed, 200);
        fe->setDarkMode(false, false);
        settle(*ed, 200);
        run(30);
        settle(*ed, 100); // the meters redraw on a timer: let them catch up with this audio
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("light_single.png"));
        setParam(p, "mb", 1.f);
        settle(*ed, 300);
        run(30);
    }

    // Make the bands' curves different so the faint reference curves are easy to see, then look at the mid band
    {
        Envelope dip; dip.points = { { 0.f, 1.f }, { 0.25f, 0.2f }, { 0.5f, 1.f }, { 0.75f, 0.2f }, { 1.f, 1.f } };
        Envelope saw; saw.points = { { 0.f, 0.f }, { 0.99f, 1.f }, { 1.f, 1.f } };
        p.setEnvelope(dip, 2);
        p.setEnvelope(saw, 3);
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(1);
        settle(*ed, 300);
        run(50);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("multiband_mid.png"));
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(2);
        settle(*ed, 300);
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
        settle(*ed, 150);
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(0);
        settle(*ed, 300);
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
        settle(*ed, 150);
        static_cast<FreeLfoEditor*>(ed.get())->selectBandForSnapshot(0);
        settle(*ed, 300);
        run(20);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("converted_to_simple.png"));
        setParam(p, "bezier", 1.f);
        p.convertCurveMode(true);
    }

    setParam(p, "slopeLow", 5.f);  // 96 dB/oct on the low split
    setParam(p, "soloL", 1.f); setParam(p, "soloM", 1.f); // and two bands soloed at once
    setParam(p, "slopeHigh", 0.f); // 6 dB/oct on the high split
    settle(*ed, 300);
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
    {   // the preset popup, rendered headless
        PaperLookAndFeel laf;
        FreeLfoProcessor pp;
        for (int dark = 0; dark < 2; ++dark)
        {
            paper::setDark(dark == 1);
            laf.applyColours();
            auto panel = PresetMenu::createPanel(pp, 0, nullptr, laf);
            save(panel->createComponentSnapshot(panel->getLocalBounds(), true, 2.0f), dir.getChildFile(dark ? "preset_popup_dark.png" : "preset_popup_light.png"));
        }
        paper::setDark(true);
    }
    {   // the popup open inside the editor: it must drop down from the button
        FreeLfoProcessor pp;
        pp.setRateAndBufferSizeDetails(48000.0, 512);
        pp.prepareToPlay(48000.0, 512);
        FreeLfoEditor fe(pp);
        fe.setSize(960, 610);
        fe.getChildComponent(0); // (layout is done by setSize)
        juce::Component* btn = nullptr;
        for (int i = 0; i < fe.getNumChildComponents(); ++i) if (dynamic_cast<PresetButton*>(fe.getChildComponent(i))) btn = fe.getChildComponent(i);
        if (btn != nullptr)
        {
            auto popup = PresetMenu::open(fe, *btn, pp, 0, nullptr, nullptr);
            save(fe.createComponentSnapshot(fe.getLocalBounds(), true, 1.0f), dir.getChildFile("preset_popup_open.png"));
            popup.reset();
            // the multiband popup, at the smallest window size, in both themes
            fe.setSize(880, 500);
            for (const bool dark : { true, false })
            {
                fe.setDarkMode(dark, false);
                auto mbPopup = PresetMenu::open(fe, *btn, pp, 1, nullptr, nullptr);
                save(fe.createComponentSnapshot(fe.getLocalBounds(), true, 2.0f), dir.getChildFile(dark ? "preset_popup_multiband.png" : "preset_popup_multiband_light.png"));
            }
        }
    }
    const bool rz = resizeRegression() && labelRegression() && resetClickRegression() && hostMenuRegression() && pointLimitRegression() && loudScopeRegression();
    const bool un = undoRegression() && presetRegression() && handleSnapRegression() && deletePointRegression();
    return kickButtonOk && un && a && b && c && d && f && g && h && k && m && n && o && q && rz ? 0 : 1;
}
