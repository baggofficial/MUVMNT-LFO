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
    size_t qi = 0;                                           // the new point is the one at the snapped x
    for (size_t i = 0; i < e1.points.size(); ++i) if (std::fabs(e1.points[i].x - 0.3125f) < 1e-4f) qi = i;
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

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir(argc > 1 ? argv[1] : ".");
    g_outDir = dir.getFullPathName();
    constexpr double sr = 48000.0;
    constexpr int bs = 512;

    FreeLfoProcessor p;
    p.setRateAndBufferSizeDetails(sr, bs);
    p.prepareToPlay(sr, bs);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); // sets editorOpen, so the waveform gets analysed
    ed->setSize(900, 610);

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
    return a && b && c && d && f ? 0 : 1;
}
