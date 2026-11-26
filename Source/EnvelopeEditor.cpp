#include "EnvelopeEditor.h"

// ---------------------------------------------------------------- EnvelopeEditor

EnvelopeEditor::EnvelopeEditor(FreeLfoProcessor& p, int slotIndex)
    : proc(p), slot(slotIndex), env(p.getEnvelope(slotIndex))
{
    setWantsKeyboardFocus(true);
    initParams();
    if (slot > 0)
        for (int other = 1; other <= 3; ++other)
            if (other != slot) ghost[other - 1] = proc.getEnvelope(other);
}

void EnvelopeEditor::initParams()
{
    static const char* base[3] = { "depth", "smooth", "offset" };
    static const char* link[3] = { "linkDepth", "linkSmooth", "linkOffset" };
    for (int w = 0; w < 3; ++w)
    {
        paramP[w][0] = proc.apvts.getRawParameterValue(base[w]);
        for (int b = 0; b < 3; ++b)
            paramP[w][b + 1] = proc.apvts.getRawParameterValue(juce::String(base[w]) + FreeLfoProcessor::bandSuffix(b));
        linkP[w] = proc.apvts.getRawParameterValue(link[w]);
    }
    bandsP = proc.apvts.getRawParameterValue("mbBands");
}

float EnvelopeEditor::paramValue(int which) const
{
    int s = slot;
    if (s > 0 && linkP[which]->load() > 0.5f) s = bandsP->load() > 0.5f ? 2 : 1;   // linked: every band follows the mid (or low) band
    return paramP[which][s]->load();
}

void EnvelopeEditor::setSlot(int newSlot)
{
    if (newSlot == slot) return;
    slot = newSlot;
    selected = -1; dragPoint = -1; dragKind = 0; dragSeg = -1; controlsHidden = false;
    env = proc.getEnvelope(slot);
    for (int other = 1; other <= 3; ++other)
        if (other != slot) ghost[other - 1] = proc.getEnvelope(other);
    curveKey = 0;
    repaint();
}

void EnvelopeEditor::resetCurve()
{
    Envelope e;                       // the single-band pump
    if (slot >= 1) e.resetToBandPump(slot - 1); // each band's own default
    proc.pushUndo(proc.captureUndoState());
    proc.setEnvelope(e, slot);
    env = e;
    selected = -1;
    controlsHidden = false;
    repaint();
}

juce::Point<float> EnvelopeEditor::toScreen(float x, float y) const
{
    const auto b = plotBounds();
    return { b.getX() + x * b.getWidth(), b.getBottom() - y * b.getHeight() };
}

juce::Point<float> EnvelopeEditor::toNorm(juce::Point<float> p) const
{
    const auto b = plotBounds();
    return { juce::jlimit(0.f, 1.f, (p.x - b.getX()) / b.getWidth()),
             juce::jlimit(0.f, 1.f, (b.getBottom() - p.y) / b.getHeight()) };
}

bool EnvelopeEditor::hasHandle(int pt, int kind) const
{
    if (pt < 0 || pt >= (int) env.points.size()) return false;
    return kind == 1 ? pt > 0 : pt < (int) env.points.size() - 1; // ends only have one side
}

juce::Point<float> EnvelopeEditor::handleScreen(int pt, int kind) const
{
    const auto h = kind == 1 ? env.inHandle((size_t) pt) : env.outHandle((size_t) pt);
    return toScreen(h.first, h.second);
}

int EnvelopeEditor::hitPoint(juce::Point<float> p) const
{
    for (size_t i = 0; i < env.points.size(); ++i)
        if (toScreen(env.points[i].x, env.points[i].y).getDistanceFrom(p) < 9.f) return (int) i;
    return -1;
}

int EnvelopeEditor::hitSegment(juce::Point<float> p) const
{
    for (size_t i = 0; i + 1 < env.points.size(); ++i)
    {
        const auto m = env.bendHandle(i);
        if (toScreen(m.first, m.second).getDistanceFrom(p) < 11.f) return (int) i;
    }
    return -1;
}

bool EnvelopeEditor::handleVisible(int pt, int kind) const
{
    if (!hasHandle(pt, kind)) return false;
    if (pt == selected) return true;
    const auto o = toScreen(env.points[(size_t) pt].x, env.points[(size_t) pt].y);
    return handleScreen(pt, kind).getDistanceFrom(o) > 3.f;
}

std::pair<int, int> EnvelopeEditor::hitHandle(juce::Point<float> p, bool pullZero) const
{
    if (!bezierMode()) return { -1, 0 };

    float best = 10.f;
    std::pair<int, int> hit { -1, 0 };
    for (int pt = 0; pt < (int) env.points.size(); ++pt)
        for (int kind : { 2, 1 })
        {
            if (!handleVisible(pt, kind)) continue;
            const auto origin = toScreen(env.points[(size_t) pt].x, env.points[(size_t) pt].y);
            if (!pullZero && handleScreen(pt, kind).getDistanceFrom(origin) < 4.f) continue; // sits on the point: the point wins
            const float dh = handleScreen(pt, kind).getDistanceFrom(p);
            const float dp = toScreen(env.points[(size_t) pt].x, env.points[(size_t) pt].y).getDistanceFrom(p);
            if (pt != selected && dp < dh) continue;       // a short handle never steals a click aimed at its point
            const float score = dh - (pt == selected ? 3.f : 0.f); // a selected point's handles win ties (they may sit on it)
            if (score < best) { best = score; hit = { pt, kind }; }
        }
    return hit;
}

bool EnvelopeEditor::nearCurve(juce::Point<float> p) const
{
    // distance from p to the drawn polyline, sampled each pixel around p (steep edges are handled as line segments)
    constexpr float reach = 9.f;
    juce::Point<float> prev;
    bool havePrev = false;
    for (float x = p.x - reach - 1.f; x <= p.x + reach + 1.f; x += 1.f)
    {
        const auto n = toNorm({ x, p.y });
        const auto cur = toScreen(n.x, env.evaluate(juce::jmin(n.x, 0.999999f)));
        if (havePrev)
        {
            juce::Point<float> nearest;
            if (juce::Line<float>(prev, cur).getDistanceFromPoint(p, nearest) < reach) return true;
        }
        prev = cur;
        havePrev = true;
    }
    return false;
}

// The first change of a gesture records the state from before it (one undo step per drag, not per mouse move).
void EnvelopeEditor::commit()
{
    if (!gestureRecorded)
    {
        proc.pushUndo(haveGestureStart ? std::move(gestureStart) : proc.captureUndoState());
        haveGestureStart = false;
        gestureRecorded = true;
    }
    proc.setEnvelope(env, slot);
}

// One filled outline of a min / max envelope (the signal folded onto the LFO cycle), optionally scaled by the gain
// the plugin applies at each position. A single path per frame: smooth, and cheap.
void EnvelopeEditor::fillWavePath(juce::Graphics& g, const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& mnArr,
                                  const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& mxArr,
                                  const float* gain, const juce::FillType& fill)
{
    constexpr int bins = FreeLfoProcessor::waveBins;
    juce::Path w;
    for (int i = 0; i < bins; ++i)
    {
        const float k = gain != nullptr ? gain[i] : 1.f;
        const float mx = juce::jlimit(-1.f, 1.f, mxArr[(size_t) i].load(std::memory_order_relaxed) * k); // gain first, then cut off at the edge
        const auto p = toScreen((float) i / (bins - 1), 0.5f + 0.5f * mx);
        if (i == 0) w.startNewSubPath(p); else w.lineTo(p);
    }
    for (int i = bins - 1; i >= 0; --i)
    {
        const float k = gain != nullptr ? gain[i] : 1.f;
        const float mn = juce::jlimit(-1.f, 1.f, mnArr[(size_t) i].load(std::memory_order_relaxed) * k); // gain first, then cut off at the edge
        w.lineTo(toScreen((float) i / (bins - 1), 0.5f + 0.5f * mn));
    }
    w.closeSubPath();
    g.setFillType(fill);
    g.fillPath(w);
}

// The output waveform coloured by pitch like MiniMeters (red sub bass, pink low mids ... yellow highs). The hue per position comes from the audio thread's rough pitch tracker. Returns false until something was measured.
bool EnvelopeEditor::rainbowFill(float alpha, juce::FillType& out) const
{
    constexpr int stops = 64, bins = FreeLfoProcessor::waveBins, per = bins / stops;
    float hue[stops];
    bool any = false;
    for (int k = 0; k < stops; ++k)
    {
        float sum = 0.f; int n = 0;
        for (int i = k * per; i < (k + 1) * per; ++i)
        {
            const float h = proc.waveHue[(size_t) i].load(std::memory_order_relaxed);
            if (h >= 0.f) { sum += h; ++n; }
        }
        hue[k] = n > 0 ? sum / (float) n : -1.f;
        any = any || n > 0;
    }
    if (!any) return false;

    float last = -1.f; // gaps (silence) take the colour of their neighbours
    for (int k = 0; k < stops; ++k) { if (hue[k] >= 0.f) last = hue[k]; else hue[k] = last; }
    last = -1.f;
    for (int k = stops - 1; k >= 0; --k) { if (hue[k] >= 0.f) last = hue[k]; else hue[k] = last; }
    float soft[stops];
    for (int k = 0; k < stops; ++k) // a little blur, so the colours flow
        soft[k] = (hue[juce::jmax(0, k - 1)] + 2.f * hue[k] + hue[juce::jmin(stops - 1, k + 1)]) * 0.25f;

    float mix[stops];
    for (int k = 0; k < stops; ++k)
    {
        float sum = 0.f;
        for (int i = k * per; i < (k + 1) * per; ++i) sum += proc.waveMix[(size_t) i].load(std::memory_order_relaxed);
        mix[k] = sum / (float) per;
    }
    const float x0 = toScreen(0.f, 0.5f).x, x1 = toScreen(1.f, 0.5f).x;
    // t (0..1 on a log axis, 40 Hz .. 16 kHz) -> colour, following MiniMeters: red up to 100 Hz, orange 200, yellow 250, lime 300,
    // green 400, turquoise 1 k, light blue 1.5 k, full blue from 5 k. Hue is in degrees, interpolated between these anchors.
    auto colourAt = [alpha](float t, float m) {
        struct Anchor { float hz, hue, sat; };
        static const Anchor a[] = { { 40.f, 0.f, 0.90f }, { 100.f, 0.f, 0.90f }, { 200.f, 30.f, 0.92f }, { 250.f, 55.f, 0.92f },
                                    { 300.f, 85.f, 0.90f }, { 400.f, 125.f, 0.85f }, { 1000.f, 172.f, 0.85f }, { 1500.f, 200.f, 0.80f },
                                    { 5000.f, 230.f, 0.88f }, { 16000.f, 235.f, 0.90f } };
        const float hz = 40.f * std::pow(16000.f / 40.f, juce::jlimit(0.f, 1.f, t));
        size_t i = 0;
        while (i + 2 < std::size(a) && hz > a[i + 1].hz) ++i;
        const float k = juce::jlimit(0.f, 1.f, std::log(hz / a[i].hz) / std::log(a[i + 1].hz / a[i].hz));
        const float hue = a[i].hue + (a[i + 1].hue - a[i].hue) * k;
        const float sat = a[i].sat + (a[i + 1].sat - a[i].sat) * k;
        const auto c = juce::Colour::fromHSV(std::fmod(hue, 360.f) / 360.f, sat * 0.82f, theme::dark ? 1.f : 0.88f, alpha);
        // lots of bass and highs together: toward pink / purple
        const auto blend = juce::Colour::fromHSV(0.84f, 0.65f, theme::dark ? 1.f : 0.88f, alpha);
        return c.interpolatedWith(blend, juce::jlimit(0.f, 1.f, m * m * 1.4f) * 0.8f);
    };
    juce::ColourGradient grad(colourAt(soft[0], mix[0]), x0, 0.f, colourAt(soft[stops - 1], mix[stops - 1]), x1, 0.f, false);
    for (int k = 1; k < stops - 1; ++k) grad.addColour((double) k / (stops - 1), colourAt(soft[k], mix[k]));
    out = juce::FillType(grad);
    return true;
}

// Everything that only changes when the user edits (grid, curve, handles, gain preview) is drawn
// once into cached images; each frame then only blits those and redraws what really moves.
std::array<juce::String, 4> EnvelopeEditor::positionLabels() const
{
    std::array<juce::String, 4> out;
    auto value = [this](const juce::String& id) { return proc.apvts.getRawParameterValue(id)->load(); };
    juce::String suffix; // which rate drives this curve: its own, or in a linked Multiband the mid (2 bands: low) band's
    if (slot > 0)
    {
        const int source = value("mbLink") > 0.5f ? (bandsP->load() > 0.5f ? 1 : 0) : slot - 1;
        suffix = FreeLfoProcessor::bandSuffix(source);
    }
    const bool single = slot == 0; // the bands' rate parameters have shorter names than the single curve's
    if (value("mode" + suffix) > 0.5f) return out; // Hz: the cycle is not a note value

    const double beats = FreeLfoProcessor::divisionBeats[(size_t) juce::jlimit(0, FreeLfoProcessor::numDivisions - 1, (int) value(juce::String(single ? "division" : "div") + suffix))];
    const juce::String mark = value(juce::String(single ? "dotted" : "dot") + suffix) > 0.5f ? "D" : value(juce::String(single ? "triplet" : "trip") + suffix) > 0.5f ? "T" : ""; // like the Rate knob
    for (int k = 1; k <= 4; ++k)
    {
        const double whole = beats * k / 16.0; // beats are quarter notes: a quarter of the cycle, in whole notes (bars)
        juce::String text;
        if (whole >= 1.0) text = juce::String(whole) + (whole > 1.0 ? " bars" : " bar");
        else
        {
            int num = juce::roundToInt(whole * 256.0), den = 256;
            while (num % 2 == 0 && den > 1) { num /= 2; den /= 2; }
            text = juce::String(num) + "/" + juce::String(den);
        }
        out[(size_t) (k - 1)] = text + mark;
    }
    return out;
}

void EnvelopeEditor::renderGrid(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(theme::bg);
    g.fillRoundedRectangle(area, theme::size::radius);
    g.setColour(theme::line);
    g.drawRoundedRectangle(area.reduced(0.5f), theme::size::radius, theme::size::border);

    // grid
    const int gx = gridX(), gy = gridY();
    const int strong = juce::jmax(1, gx / 4); // every quarter of the cycle is a bit stronger
    // A 1 px line centred between two pixels is smeared over both and looks weak: snap lines onto whole pixels
    auto crisp = [](float v) { return std::floor(v) + 0.5f; };
    for (int i = 0; i <= gx; ++i)
    {
        g.setColour(i % strong == 0 ? theme::gridMajor() : theme::gridMinor());
        auto a = toScreen((float) i / gx, 0.f), b = toScreen((float) i / gx, 1.f);
        g.drawLine(crisp(a.x), a.y, crisp(b.x), b.y, 1.f);
    }
    const int strongY = juce::jmax(1, gy / 2); // the middle and the top / bottom edges
    for (int i = 0; i <= gy; ++i)
    {
        g.setColour(i % strongY == 0 ? theme::gridMajor() : theme::gridMinor());
        auto a = toScreen(0.f, (float) i / gy), b = toScreen(1.f, (float) i / gy);
        g.drawLine(a.x, crisp(a.y), b.x, crisp(b.y), 1.f);
    }

    // very small note values under the graph, to find your way in the cycle (drawn at the quarters, whatever the grid is)
    const auto labels = positionLabels();
    g.setFont(fonts::regular(theme::type::axis));
    g.setColour(theme::sub);
    for (int k = 1; k <= 4; ++k)
    {
        const auto at = toScreen((float) k / 4.f, 0.f);
        const auto box = juce::Rectangle<float>(48.f, 10.f).withPosition(k == 4 ? at.x - 48.f : at.x - 24.f, area.getBottom() - 10.5f);
        g.drawText(labels[(size_t) (k - 1)], box, k == 4 ? juce::Justification::centredRight : juce::Justification::centred, false);
    }
}

// The input signal is drawn faint and the output (input x the gain the plugin really applies) on top of it, so the
// ducking is visible on the audio itself. Single tab: neutral input, accent-coloured output. Multiband: the band's colour.
void EnvelopeEditor::drawWaveforms(juce::Graphics& g)
{
    constexpr float outputStrength = 0.82f; // clearly visible, but behind the curve you edit
    const float waveOpacity = proc.apvts.getRawParameterValue("waveOpacity")->load();
    if (waveOpacity < 0.01f) return;
    const auto in = slot > 0 ? theme::bandColour(slot - 1) : (theme::dark ? juce::Colours::white : juce::Colours::black);
    const auto out = slot > 0 ? theme::bandColour(slot - 1) : theme::accent;
    const auto& mnArr = slot > 0 ? proc.bwMin[(size_t) slot - 1] : proc.waveMin;
    const auto& mxArr = slot > 0 ? proc.bwMax[(size_t) slot - 1] : proc.waveMax;

    fillWavePath(g, mnArr, mxArr, nullptr, in.withAlpha(waveOpacity * 0.30f));
    juce::FillType outFill(out.withAlpha(waveOpacity * outputStrength));
    if (slot == 0) rainbowFill(waveOpacity * outputStrength, outFill); // single tab: rainbow; the multiband tab keeps its band colours
    fillWavePath(g, mnArr, mxArr, outGain, outFill);

    if (proc.sidechainActive.load())
    {
        // The sidechain signal (the kick routed in from another channel), and where it fights with the output
        const auto& scMn = slot > 0 ? proc.bandScMin[(size_t) slot - 1] : proc.scMin;
        const auto& scMx = slot > 0 ? proc.bandScMax[(size_t) slot - 1] : proc.scMax;
        drawOverlap(g, mnArr, mxArr, scMn, scMx, waveOpacity);

        constexpr int bins = FreeLfoProcessor::waveBins;
        juce::Path w;
        for (int i = 0; i < bins; ++i)
        {
            const float mx = juce::jlimit(-1.f, 1.f, scMx[(size_t) i].load(std::memory_order_relaxed));
            auto p = toScreen((float) i / (bins - 1), 0.5f + 0.5f * mx);
            if (i == 0) w.startNewSubPath(p); else w.lineTo(p);
        }
        for (int i = bins - 1; i >= 0; --i)
        {
            const float mn = juce::jlimit(-1.f, 1.f, scMn[(size_t) i].load(std::memory_order_relaxed));
            w.lineTo(toScreen((float) i / (bins - 1), 0.5f + 0.5f * mn));
        }
        w.closeSubPath();
        // A contrasting halo (dark on the dark theme, light on the light one), a see-through fill so the scope underneath still
        // shows, and a bright line on top: the kick stays readable over the rainbow and the grey input.
        g.setColour((theme::dark ? juce::Colours::black : juce::Colours::white).withAlpha(0.55f * juce::jmax(0.3f, waveOpacity)));
        g.strokePath(w, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(theme::amber.withAlpha(waveOpacity * 0.5f));
        g.fillPath(w);
        g.setColour(theme::amber.brighter(theme::dark ? 0.35f : 0.f).withAlpha(juce::jmin(1.f, waveOpacity + 0.2f)));
        g.strokePath(w, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// Where the sidechain signal (the kick) and the plugin's output are both loud at the same position in the cycle: hatched
// in the kick's colour. Each is measured against its own peak, so it does not depend on levels; the output is what is left
// after the ducking, so a spot where the curve really ducks under the kick is not marked. In Multiband each band is
// compared with the same band of the kick.
void EnvelopeEditor::drawOverlap(juce::Graphics& g, const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& mnArr,
                                 const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& mxArr,
                                 const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& scMn,
                                 const std::array<std::atomic<float>, FreeLfoProcessor::waveBins>& scMx, float opacity)
{
    constexpr int bins = FreeLfoProcessor::waveBins;
    float kick[bins], out[bins];
    float kMax = 0.f, oMax = 0.f;
    for (int i = 0; i < bins; ++i)
    {
        kick[i] = juce::jmax(std::abs(scMn[(size_t) i].load(std::memory_order_relaxed)), std::abs(scMx[(size_t) i].load(std::memory_order_relaxed)));
        out[i] = juce::jmax(std::abs(mnArr[(size_t) i].load(std::memory_order_relaxed)), std::abs(mxArr[(size_t) i].load(std::memory_order_relaxed))) * outGain[i];
        kMax = juce::jmax(kMax, kick[i]);
        oMax = juce::jmax(oMax, out[i]);
    }
    if (kMax < 0.01f || oMax < 0.01f) return;   // no kick, or no signal: nothing to compare

    float c[bins];
    for (int i = 0; i < bins; ++i) c[i] = juce::jmin(kick[i] / kMax, out[i] / oMax);
    float sm[bins];                                      // a little smoothing, so single wiggles do not make confetti
    for (int i = 0; i < bins; ++i)
    {
        float sum = 0.f; int n = 0;
        for (int k = juce::jmax(0, i - 3); k <= juce::jmin(bins - 1, i + 3); ++k) { sum += c[k]; ++n; }
        sm[i] = sum / (float) n;
    }

    const float top = toScreen(0.f, 1.f).y, bottom = toScreen(0.f, 0.f).y;
    const auto ink = theme::dark ? juce::Colours::white : juce::Colours::black;
    int runStart = -1;
    auto flush = [&](int endBin) {
        if (runStart < 0) return;
        const float x0 = toScreen((float) runStart / (bins - 1), 0.f).x, x1 = toScreen((float) endBin / (bins - 1), 0.f).x;
        const auto r = juce::Rectangle<float>(x0, top, juce::jmax(2.f, x1 - x0), bottom - top);
        g.setColour(theme::amber.withAlpha(0.14f * juce::jmax(0.3f, opacity)));
        g.fillRect(r);
        g.saveState();
        g.reduceClipRegion(r.toNearestInt());
        g.setColour(ink.withAlpha(0.28f * juce::jmax(0.4f, opacity)));
        for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 7.f)   // diagonal hatching
            g.drawLine(x, r.getBottom(), x + r.getHeight(), r.getY(), 1.f);
        g.restoreState();
        runStart = -1;
    };
    for (int i = 0; i < bins; ++i)
    {
        if (sm[i] > 0.4f) { if (runStart < 0) runStart = i; }
        else flush(i);
    }
    flush(bins - 1);
}

void EnvelopeEditor::renderCurveLayer(juce::Graphics& g)
{
    // The other bands' curves, faint and without points, so it's clear only one band is edited at a time
    if (slot > 0)
    {
        const bool three = proc.apvts.getRawParameterValue("mbBands")->load() > 0.5f;
        for (int other = 1; other <= 3; ++other)
        {
            if (other == slot || (other == 2 && !three)) continue;
            juce::Path gp;
            const int gsteps = juce::jmax(2, getWidth());
            for (int i = 0; i <= gsteps; ++i)
            {
                const auto p = toScreen((float) i / gsteps, ghost[other - 1].evaluate(juce::jmin((float) i / gsteps, 0.999999f)));
                if (i == 0) gp.startNewSubPath(p); else gp.lineTo(p);
            }
            gp.lineTo(toScreen(1.f, ghost[other - 1].points.back().y));
            g.setColour(theme::bandColour(other - 1).withAlpha(0.38f));
            g.strokePath(gp, juce::PathStrokeType(2.f, juce::PathStrokeType::curved));
        }
    }

    // curve
    juce::Path line, fill;
    const int steps = juce::jmax(2, (int) getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        auto p = toScreen((float) i / steps, env.evaluate(juce::jmin((float) i / steps, 0.999999f)));
        if (i == 0) { line.startNewSubPath(p); fill.startNewSubPath(toScreen(0.f, 0.f)); fill.lineTo(p); }
        else { line.lineTo(p); fill.lineTo(p); }
    }
    // The samples stop just short of x = 1 (evaluate(1) wraps around to the start), which hides a very steep
    // last edge: close the line on the real end point instead.
    {
        const auto end = toScreen(1.f, env.points.back().y);
        line.lineTo(end);
        fill.lineTo(end);
    }
    fill.lineTo(toScreen(1.f, 0.f));
    fill.closeSubPath();
    // Under the curve: a vertical gradient that fades from the curve colour at the top to almost nothing at the bottom
    const auto curveColour = mainColour();
    {
        const float yTop = toScreen(0.f, 1.f).y, yBottom = toScreen(0.f, 0.f).y;
        const float strength = slot == 0 ? 0.45f : 0.85f; // the neutral Single curve needs less fill than a coloured one
        juce::ColourGradient fade(curveColour.withAlpha((theme::dark ? 0.30f : 0.24f) * strength), 0.f, yTop,
                                  curveColour.withAlpha(theme::dark ? 0.03f : 0.02f), 0.f, yBottom, false);
        g.setGradientFill(fade);
        g.fillPath(fill);
    }
    // A contrasting halo under the line (dark on the dark theme, light on the light one) so the curve stays readable
    // on top of a waveform; one faint, narrow glow for depth. All of this is cached, no per-frame cost.
    g.setColour((theme::dark ? juce::Colours::black : juce::Colours::white).withAlpha(0.65f));
    g.strokePath(line, juce::PathStrokeType(5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(curveColour.withAlpha(theme::dark ? 0.16f : 0.09f));
    g.strokePath(line, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(theme::dark && slot > 0 ? curveColour.brighter(0.2f) : curveColour);
    g.strokePath(line, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Actual output gain: the curve after depth, smoothing and phase offset, as the audio thread will apply it.
    // Only drawn when it differs from the curve itself.
    const float depth = paramValue(0);
    const float smoothMs = paramValue(1);
    const float offset = paramValue(2);
    const bool differs = depth < 0.999f || smoothMs > 0.05f || offset > 0.0005f;
    {
        constexpr int N = FreeLfoProcessor::waveBins;
        float v[N];
        // Phase offset: at cycle position x the audio reads the curve at x + offset, so the grey line is the curve
        // slid to the left by the offset (and wrapped round)
        for (int i = 0; i < N; ++i) v[i] = env.evaluate((float) i / N + offset);

        const double dt = cycleSecs() / N;
        const float coef = smoothMs < 0.01f ? 0.f : (float) std::exp(-dt / (smoothMs * 0.001));
        float y = 1.f - depth * (1.f - v[N - 1]);
        float out[N];
        for (int pass = 0; pass < 6; ++pass) // run a few cycles so the loop settles to its steady state
            for (int i = 0; i < N; ++i)
            {
                y = coef * y + (1.f - coef) * (1.f - depth * (1.f - v[i]));
                out[i] = y;
            }
        std::copy(out, out + N, outGain);   // the waveform overlay uses the same gain

        if (differs)
        {
        juce::Path actual;
        for (int i = 0; i < N; ++i)
        {
            auto p = toScreen((float) i / (N - 1), out[i]);
            if (i == 0) actual.startNewSubPath(p); else actual.lineTo(p);
        }
        g.setColour(theme::text.withAlpha(0.3f));
        g.strokePath(actual, juce::PathStrokeType(2.f));
        }
    }

    // Bezier handles for every point, After Effects style: thin yellow line to the point, filled yellow dot
    if (bezierMode() && !controlsHidden)
    {
        const juce::Colour tangent = theme::handle;
        for (int pt = 0; pt < (int) env.points.size(); ++pt)
        {
            const auto origin = toScreen(env.points[(size_t) pt].x, env.points[(size_t) pt].y);
            for (int kind : { 1, 2 })
            {
                if (!handleVisible(pt, kind)) continue;
                const auto h = handleScreen(pt, kind);
                const bool active = dragKind == kind && pt == selected;
                g.setColour(tangent.withAlpha(0.95f));
                g.drawLine(origin.x, origin.y, h.x, h.y, 1.4f);
                g.setColour(active ? tangent.brighter(0.4f) : tangent);
                g.fillEllipse(h.x - 4.5f, h.y - 4.5f, 9.f, 9.f);
                g.setColour(tangent.darker(0.55f).withAlpha(0.9f));
                g.drawEllipse(h.x - 4.5f, h.y - 4.5f, 9.f, 9.f, 1.f);
            }
        }
    }

    // points (the selected one is filled); all hidden after a click on empty space
    for (int i = 0; i < (int) env.points.size() && !controlsHidden; ++i)
    {
        auto p = toScreen(env.points[(size_t) i].x, env.points[(size_t) i].y);
        g.setColour(i == selected ? mainColour() : theme::panel);
        g.fillEllipse(p.x - 5.5f, p.y - 5.5f, 11.f, 11.f);
        g.setColour(mainColour());
        g.drawEllipse(p.x - 5.5f, p.y - 5.5f, 11.f, 11.f, 2.f);
    }

    // Simple mode: one diamond per segment, sitting on the curve
    if (!bezierMode() && !controlsHidden)
    {
        for (int i = 0; i + 1 < (int) env.points.size(); ++i)
        {
            const auto m = env.bendHandle((size_t) i);
            const auto h = toScreen(m.first, m.second);
            juce::Path d;
            d.addQuadrilateral(h.x, h.y - 6, h.x + 6, h.y, h.x, h.y + 6, h.x - 6, h.y);
            g.setColour(theme::panel);
            g.fillPath(d);
            g.setColour(theme::text.withAlpha(dragSeg == i ? 1.f : 0.7f));
            g.strokePath(d, juce::PathStrokeType(1.5f));
        }
    }
}

size_t EnvelopeEditor::computeKey() const
{
    size_t h = 1469598103u;
    auto mix = [&h](float f) { h = (h ^ (size_t) (juce::roundToInt(f * 10000.f) + 1000003)) * 1099511628211ull; };
    for (auto& pt : env.points) { mix(pt.x); mix(pt.y); mix(pt.inDx); mix(pt.inDy); mix(pt.outDx); mix(pt.outDy); mix(pt.bend); mix(pt.bendX); }
    mix(paramValue(0));
    mix(paramValue(1));
    mix(paramValue(2));
    mix((float) cycleSecs());
    mix(proc.apvts.getRawParameterValue("waveOpacity")->load());
    mix((float) theme::version);
    mix((float) selected); mix((float) dragKind); mix((float) dragSeg); mix(controlsHidden ? 1.f : 0.f);
    if (slot > 0)
    {
        mix(proc.apvts.getRawParameterValue("mbBands")->load());
        for (int other = 1; other <= 3; ++other)
            if (other != slot)
                for (auto& pt : ghost[other - 1].points) { mix(pt.x); mix(pt.y); mix(pt.inDx); mix(pt.inDy); mix(pt.outDx); mix(pt.outDy); mix(pt.bend); mix(pt.bendX); }
    }
    mix(bezierMode() ? 1.f : 0.f);
    mix(proc.sidechainActive.load() ? 1.f : 0.f);
    return h;
}

// Playhead position at this instant: the audio thread publishes the phase once per block, so
// extrapolate from it for smooth motion between blocks.
float EnvelopeEditor::predictedPhase() const
{
    const double elapsedMs = juce::jlimit(0.0, 60.0, juce::Time::getMillisecondCounterHiRes()
                                                         - proc.uiPhaseTimeMs.load(std::memory_order_relaxed));
    const double cycle = juce::jmax(1e-3, cycleSecs());
    double ph = (slot == 0 ? proc.uiPhase.load(std::memory_order_relaxed) : proc.bandPhase[(size_t) slot - 1].load(std::memory_order_relaxed)) + elapsedMs * 0.001 / cycle;
    return (float) (ph - std::floor(ph));
}

void EnvelopeEditor::animate()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    // Copy a curve only when it changed (many times a second would otherwise copy up to three curves for nothing)
    if (dragPoint < 0 && dragKind == 0 && dragSeg < 0)
    {
        const int v = proc.envelopeVersion(slot);
        if (v != seenVersion[0] || seenSlot != slot) { env = proc.getEnvelope(slot); seenVersion[0] = v; seenSlot = slot; }
    }
    if (slot > 0)
        for (int other = 1; other <= 3; ++other)
            if (other != slot)
            {
                const int v = proc.envelopeVersion(other);
                if (v != seenVersion[other]) { ghost[other - 1] = proc.getEnvelope(other); seenVersion[other] = v; }
            }

    // Idle = no repaint: only redraw when audio is flowing or something changed.
    const unsigned blk = proc.blockCounter.load(std::memory_order_relaxed);
    const bool audioMoving = blk != lastBlock || (now - proc.uiPhaseTimeMs.load(std::memory_order_relaxed)) < 60.0;
    lastBlock = blk;
    const bool noteExpired = noteDrawn && !isShowingNote(); // one more repaint to take the note away
    if (noteExpired || audioMoving || positionLabels()[3] != cachedLabels || computeKey() != curveKey || cachedGrid != gridX() || cachedTheme != theme::version) repaint();
}

void EnvelopeEditor::paint(juce::Graphics& g)
{
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const int pw = juce::roundToInt(getWidth() * scale), ph = juce::roundToInt(getHeight() * scale);

    auto rebuild = [&](juce::Image& img, auto&& draw) {
        img = juce::Image(juce::Image::ARGB, juce::jmax(1, pw), juce::jmax(1, ph), true);
        juce::Graphics ig(img);
        ig.addTransform(juce::AffineTransform::scale(scale));
        draw(ig);
    };

    const bool sizeChanged = !gridImg.isValid() || gridImg.getWidth() != pw || gridImg.getHeight() != ph;
    const auto labelNow = positionLabels()[3]; // the end label changes with every rate, dotted, triplet or Hz change
    if (sizeChanged || labelNow != cachedLabels || cachedGrid != gridX() || cachedTheme != theme::version) // the grid is cached too: redraw when its lines or the theme change
    {
        rebuild(gridImg, [this](juce::Graphics& ig) { renderGrid(ig); });
        cachedGrid = gridX();
        cachedLabels = labelNow;
        cachedTheme = theme::version;
        curveKey = 0;
    }

    const size_t key = computeKey();
    if (key != curveKey || sizeChanged) { rebuild(curveImg, [this](juce::Graphics& ig) { renderCurveLayer(ig); }); curveKey = key; }

    const auto unscale = juce::AffineTransform::scale(1.f / scale);
    g.drawImageTransformed(gridImg, unscale);
    drawWaveforms(g);
    g.setColour(juce::Colours::black); // drawImage uses the current colour's alpha as the image's opacity: the scope must not dim the curve
    g.drawImageTransformed(curveImg, unscale);

    // playhead: just a thin line (nothing else is redrawn per frame)
    const float phase = predictedPhase();
    auto top = toScreen(phase, 1.f), bottom = toScreen(phase, 0.f);
    g.setColour(theme::text.withAlpha(0.7f));
    g.drawLine(top.x, top.y, bottom.x, bottom.y, 1.f);

    noteDrawn = isShowingNote();
    if (noteDrawn)
    {
        const float width = juce::jmin(470.f, (float) getWidth() - 16.f);
        const auto note = juce::Rectangle<float>(width, 26.f).withCentre({ (float) getWidth() * 0.5f, 29.f });
        g.setColour(theme::panel.withAlpha(0.96f));
        g.fillRoundedRectangle(note, 6.f);
        g.setColour(theme::amber);
        g.drawRoundedRectangle(note, 6.f, 1.2f);
        g.setColour(theme::text);
        g.setFont(fonts::regular(12.f));
        g.drawFittedText(noteText, note.reduced(6.f, 0.f).toNearestInt(), juce::Justification::centred, 2, 0.85f);
    }
}

void EnvelopeEditor::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    gestureStart = proc.captureUndoState(); haveGestureStart = true; gestureRecorded = false;
    env = proc.getEnvelope(slot);
    if (selected >= (int) env.points.size()) selected = -1;
    const auto pos = e.position;

    // With everything hidden, only a click on the curve itself brings the points and handles back
    if (controlsHidden)
    {
        dragKind = 0; dragPoint = -1; dragSeg = -1;
        if (nearCurve(pos)) controlsHidden = false;
        repaint();
        return;
    }

    const auto grabbed = e.mods.isPopupMenu() ? std::pair<int, int> { -1, 0 } : hitHandle(pos, e.mods.isAltDown());
    dragKind = grabbed.second;
    if (dragKind != 0) selected = grabbed.first; // grabbing any handle selects its point
    dragPoint = -1;

    // Mirror the opposite handle only if this point is already smooth (its two handles are collinear, or the
    // other one is still unset). A point whose handles point in different directions, e.g. right after a
    // conversion from simple mode, keeps them independent so a small nudge doesn't swing the other handle.
    mirrorDrag = false;
    if (dragKind != 0 && selected > 0 && selected < (int) env.points.size() - 1)
    {
        const auto origin = toScreen(env.points[(size_t) selected].x, env.points[(size_t) selected].y);
        const auto vo = handleScreen(selected, 2) - origin, vi = handleScreen(selected, 1) - origin;
        const float lo = vo.getDistanceFromOrigin(), li = vi.getDistanceFromOrigin();
        mirrorDrag = lo < 2.f || li < 2.f || (vo.x * vi.x + vo.y * vi.y) / (lo * li) < -0.985f; // within about 10 degrees
    }
    dragSeg = (!e.mods.isPopupMenu() && !bezierMode()) ? hitSegment(pos) : -1;
    if (dragSeg >= 0)
        selected = -1;
    else if (dragKind == 0)
    {
        const int hit = hitPoint(pos);
        selected = hit;
        dragPoint = e.mods.isPopupMenu() ? -1 : hit;
        // a click on neither a point, a handle nor the curve hides all the controls, so the bare curve shows
        if (hit < 0 && !e.mods.isPopupMenu() && !nearCurve(pos)) controlsHidden = true;
    }
    repaint();
}

void EnvelopeEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragPoint >= 0)
    {
        auto n = toNorm(e.position);
        if (snapNow(e.mods))
        {
            n.x = std::round(n.x * gridX()) / gridX();   // exactly on the drawn grid lines
            n.y = std::round(n.y * gridY()) / gridY();
        }
        auto& pt = env.points[(size_t) dragPoint];
        pt.y = n.y;
        // the first and last points always share one height (no click where the loop wraps), so they move together
        if (dragPoint == 0) env.points.back().y = n.y;
        else if (dragPoint == (int) env.points.size() - 1) env.points.front().y = n.y;
        if (dragPoint > 0 && dragPoint < (int) env.points.size() - 1)
        {
            const float lo = env.points[(size_t) dragPoint - 1].x + 0.002f;
            const float hi = env.points[(size_t) dragPoint + 1].x - 0.002f;
            pt.x = juce::jlimit(lo, hi, n.x);
        }
        commit();
        repaint();
    }
    else if (dragSeg >= 0)
    {
        // Simple mode: the diamond sits on the curve; the segment bends through wherever it's dropped.
        const auto n = toNorm(e.position);
        env.bendSegmentThrough((size_t) dragSeg, n.x, n.y);
        commit();
        repaint();
    }
    else if (dragKind != 0 && selected >= 0)
    {
        auto& p = env.points[(size_t) selected];
        auto n = toNorm(e.position);
        // Holding Option / Alt or Ctrl snaps the handle's tip to the grid lines (the Snap switch reverses that)
        if (snapHandleNow(e.mods))
        {
            n.x = std::round(n.x * gridX()) / gridX();
            n.y = std::round(n.y * gridY()) / gridY();
        }

        // Offsets are kept relative to the point; x is limited to the neighbouring segment.
        float dx = n.x - p.x, dy = n.y - p.y;
        if (dragKind == 2)
        {
            dx = juce::jlimit(0.f, env.points[(size_t) selected + 1].x - p.x, dx);
            p.outDx = dx; p.outDy = dy;
        }
        else
        {
            dx = juce::jlimit(p.x - env.points[(size_t) selected - 1].x > 0.f
                                  ? -(p.x - env.points[(size_t) selected - 1].x) : 0.f, 0.f, dx);
            p.inDx = dx; p.inDy = dy;
        }

        // Smooth points: the opposite handle mirrors this one's direction (keeps its length).
        // Hold Shift to break the handles apart.
        const bool interior = selected > 0 && selected < (int) env.points.size() - 1;
        const bool mirror = interior && mirrorDrag && !e.mods.isShiftDown();
        if (mirror)
        {
            const float len = std::hypot(dx, dy);
            if (len > 1e-4f)
            {
                float& oppDx = dragKind == 2 ? p.inDx : p.outDx;
                float& oppDy = dragKind == 2 ? p.inDy : p.outDy;
                float oppLen = std::hypot(oppDx, oppDy);
                if (oppLen < 1e-4f) oppLen = len;
                const float k = oppLen / len;
                // keep what's stored identical to what's drawn: x inside the neighbouring segment, y inside 0..1
                const float room = dragKind == 2 ? p.x - env.points[(size_t) selected - 1].x
                                                 : env.points[(size_t) selected + 1].x - p.x;
                oppDx = dragKind == 2 ? juce::jlimit(-room, 0.f, -dx * k) : juce::jlimit(0.f, room, -dx * k);
                oppDy = juce::jlimit(0.f, 1.f, p.y - dy * k) - p.y;
            }
        }

        // Handles and the simple-mode bend are two ways to shape the same segment: using one clears the other
        const int draggedSeg = dragKind == 2 ? selected : selected - 1;
        const int otherSeg = dragKind == 2 ? selected - 1 : selected;
        auto clearBend = [this](int seg) {
            if (seg >= 0 && seg + 1 < (int) env.points.size()) { env.points[(size_t) seg].bend = 0.f; env.points[(size_t) seg].bendX = 0.5f; }
        };
        clearBend(draggedSeg);
        if (mirror) clearBend(otherSeg);

        commit();
        repaint();
    }
}

void EnvelopeEditor::mouseUp(const juce::MouseEvent&) { haveGestureStart = false; gestureRecorded = false; dragPoint = -1; dragKind = 0; dragSeg = -1; repaint(); }

void EnvelopeEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    env = proc.getEnvelope(slot);
    const int hit = hitPoint(e.position);
    const int seg = bezierMode() ? -1 : hitSegment(e.position);

    if (hit < 0 && seg >= 0)
    {
        // Simple mode: double-click a diamond to straighten that segment
        auto& a = env.points[(size_t) seg];
        auto& b = env.points[(size_t) seg + 1];
        a.outDx = a.outDy = b.inDx = b.inDy = 0.f;
        a.bend = 0.f; a.bendX = 0.5f;
    }
    else if (hit > 0 && hit < (int) env.points.size() - 1)
    {
        // Double-click a point: delete it (the first and last points are fixed)
        env.points.erase(env.points.begin() + hit);
        selected = -1;
    }
    else if (hit >= 0)
    {
        // Double-click the first / last point: toggle "easy ease" (flat tangents). Again: back to a straight line.
        auto& p = env.points[(size_t) hit];
        if (hit > 0) { env.points[(size_t) hit - 1].bend = 0.f; env.points[(size_t) hit - 1].bendX = 0.5f; }
        if (hit + 1 < (int) env.points.size()) { p.bend = 0.f; p.bendX = 0.5f; }
        const bool hasAny = std::fabs(p.inDx) + std::fabs(p.inDy) + std::fabs(p.outDx) + std::fabs(p.outDy) > 1e-4f;
        if (hasAny)
            p.inDx = p.inDy = p.outDx = p.outDy = 0.f;
        else
        {
            if (hit > 0) p.inDx = -(p.x - env.points[(size_t) hit - 1].x) / 3.f;
            if (hit + 1 < (int) env.points.size()) p.outDx = (env.points[(size_t) hit + 1].x - p.x) / 3.f;
        }
        selected = hit;
    }
    else if (hitHandle(e.position).first < 0)
    {
        auto n = toNorm(e.position);
        if (snapNow(e.mods)) n.x = std::round(n.x * gridX()) / gridX();
        if (n.x <= 0.f || n.x >= 1.f) return;
        if (env.points.size() >= Envelope::maxPoints) // full: say so, and leave the curve and the undo history alone
        {
            showNote(limitNoteText());
            return;
        }
        env.points.push_back({ n.x, n.y });
        env.normalize();
        controlsHidden = false;
        for (int i = 0; i < (int) env.points.size(); ++i)
            if (env.points[(size_t) i].x == n.x && env.points[(size_t) i].y == n.y) selected = i;

        // A new point starts smooth, with its handles spread out to either side along the direction between its
        // neighbours (as in After Effects), so the point itself can be grabbed instead of two handles stacked on it.
        if (bezierMode() && selected > 0 && selected < (int) env.points.size() - 1)
        {
            auto& q = env.points[(size_t) selected];
            const auto& prev = env.points[(size_t) selected - 1];
            const auto& next = env.points[(size_t) selected + 1];
            const float run = next.x - prev.x;
            const float slope = run > 1e-4f ? (next.y - prev.y) / run : 0.f;

            auto fit = [&q](float dx, float dy) { // shrink along its own direction if it would leave 0..1
                float k = 1.f;
                if (dy > 0.f && q.y + dy > 1.f) k = (1.f - q.y) / dy;
                if (dy < 0.f && q.y + dy < 0.f) k = (0.f - q.y) / dy;
                return std::pair<float, float> { dx * k, dy * k };
            };
            // a third of the way to each neighbour, but never shorter than ~28 px (unless the neighbour is closer than that),
            // so the handles stay clearly apart from the point even when it is placed right next to another one
            const float minDx = 28.f / juce::jmax(1.f, (float) getWidth() - 20.f);
            const float dr = juce::jmax((next.x - q.x) / 3.f, juce::jmin(next.x - q.x, minDx));
            const float dl = juce::jmax((q.x - prev.x) / 3.f, juce::jmin(q.x - prev.x, minDx));
            const auto o = fit(dr, slope * dr), i = fit(-dl, -slope * dl);
            q.outDx = o.first; q.outDy = o.second;
            q.inDx = i.first;  q.inDy = i.second;
        }
    }
    commit();
    repaint();
}


bool EnvelopeEditor::keyPressed(const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int c = k.getKeyCode();
    const bool z = c == 'z' || c == 'Z', y = c == 'y' || c == 'Y';
    if (!m.isCommandDown() && !m.isCtrlDown()) return false;
    if ((z && m.isShiftDown()) || y) return proc.redo();
    if (z) return proc.undo();
    return false;
}
