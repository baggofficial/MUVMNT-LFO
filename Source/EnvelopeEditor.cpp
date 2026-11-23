#include "EnvelopeEditor.h"

// ---------------------------------------------------------------- EnvelopeEditor

EnvelopeEditor::EnvelopeEditor(FreeLfoProcessor& p, int slotIndex)
    : proc(p), slot(slotIndex), env(p.getEnvelope(slotIndex)), vblank(this, [this] { frame(); })
{
    depthP = proc.apvts.getRawParameterValue(bandId("depth"));
    smoothP = proc.apvts.getRawParameterValue(bandId("smooth"));
    if (slot > 0)
        for (int other = 1; other <= 3; ++other)
            if (other != slot) ghost[other - 1] = proc.getEnvelope(other);
}

void EnvelopeEditor::setSlot(int newSlot)
{
    if (newSlot == slot) return;
    slot = newSlot;
    depthP = proc.apvts.getRawParameterValue(bandId("depth"));
    smoothP = proc.apvts.getRawParameterValue(bandId("smooth"));
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
    proc.setEnvelope(e, slot);
    env = e;
    selected = -1;
    controlsHidden = false;
    repaint();
}

juce::Point<float> EnvelopeEditor::toScreen(float x, float y) const
{
    auto b = getLocalBounds().toFloat().reduced(10.f);
    return { b.getX() + x * b.getWidth(), b.getBottom() - y * b.getHeight() };
}

juce::Point<float> EnvelopeEditor::toNorm(juce::Point<float> p) const
{
    auto b = getLocalBounds().toFloat().reduced(10.f);
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

void EnvelopeEditor::commit() { proc.setEnvelope(env, slot); }

// Rekordbox-style: each slice of the LFO cycle is coloured by its frequency content.
// Red = lows, green = mids, blue = highs, blended by how much of each band is present.
void EnvelopeEditor::drawInputWaveform(juce::Graphics& g, float opacity)
{
    constexpr int bins = FreeLfoProcessor::waveBins;
    const float binW = toScreen(1.f, 0.f).x - toScreen(0.f, 0.f).x;
    const float w = binW / (float) bins + 0.6f;

    for (int i = 0; i < bins; ++i)
    {
        const float mx = juce::jlimit(-1.f, 1.f, proc.waveMax[(size_t) i].load(std::memory_order_relaxed));
        const float mn = juce::jlimit(-1.f, 1.f, proc.waveMin[(size_t) i].load(std::memory_order_relaxed));
        if (mx - mn < 1e-5f) continue;

        // boost mids/highs a little: they carry far less amplitude than lows in most material
        const float lo = proc.bandLow[(size_t) i].load(std::memory_order_relaxed);
        const float mi = proc.bandMid[(size_t) i].load(std::memory_order_relaxed) * 1.6f;
        const float hi = proc.bandHigh[(size_t) i].load(std::memory_order_relaxed) * 3.2f;
        const float sum = lo + mi + hi + 1e-9f;

        const float r = (0.90f * lo + 0.20f * mi + 0.15f * hi) / sum;
        const float gr = (0.20f * lo + 0.70f * mi + 0.45f * hi) / sum;
        const float b = (0.20f * lo + 0.30f * mi + 0.95f * hi) / sum;

        auto top = toScreen((float) i / (bins - 1), 0.5f + 0.5f * mx);
        auto bot = toScreen((float) i / (bins - 1), 0.5f + 0.5f * mn);
        g.setColour(juce::Colour::fromFloatRGBA(r, gr, b, opacity));
        g.fillRect(top.x - w * 0.5f, top.y, w, juce::jmax(1.f, bot.y - top.y));
    }
}

// Multiband tab: this band's own signal, folded onto this band's own LFO cycle, in the band's colour.
void EnvelopeEditor::drawBandWaveform(juce::Graphics& g, float opacity)
{
    constexpr int bins = FreeLfoProcessor::waveBins;
    const auto& mnArr = proc.bwMin[(size_t) slot - 1];
    const auto& mxArr = proc.bwMax[(size_t) slot - 1];
    juce::Path w;
    for (int i = 0; i < bins; ++i)
    {
        const float mx = juce::jlimit(-1.f, 1.f, mxArr[(size_t) i].load(std::memory_order_relaxed));
        auto p = toScreen((float) i / (bins - 1), 0.5f + 0.5f * mx);
        if (i == 0) w.startNewSubPath(p); else w.lineTo(p);
    }
    for (int i = bins - 1; i >= 0; --i)
    {
        const float mn = juce::jlimit(-1.f, 1.f, mnArr[(size_t) i].load(std::memory_order_relaxed));
        w.lineTo(toScreen((float) i / (bins - 1), 0.5f + 0.5f * mn));
    }
    w.closeSubPath();
    g.setColour(paper::bandColour(slot - 1).withAlpha(opacity));
    g.fillPath(w);
}

// Everything that only changes when the user edits (grid, curve, handles, gain preview) is drawn
// once into cached images; each frame then only blits those and redraws what really moves.
void EnvelopeEditor::renderGrid(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(paper::bg);
    g.fillRoundedRectangle(area, 6.f);

    // grid
    const int gx = gridX(), gy = gridY();
    const int strong = juce::jmax(1, gx / 4); // every quarter of the cycle is a bit stronger
    for (int i = 0; i <= gx; ++i)
    {
        g.setColour(juce::Colours::black.withAlpha(i % strong == 0 ? 0.14f : 0.06f));
        auto a = toScreen((float) i / gx, 0.f), b = toScreen((float) i / gx, 1.f);
        g.drawLine(a.x, a.y, b.x, b.y);
    }
    for (int i = 0; i <= gy; ++i)
    {
        g.setColour(juce::Colours::black.withAlpha(0.07f));
        auto a = toScreen(0.f, (float) i / gy), b = toScreen(1.f, (float) i / gy);
        g.drawLine(a.x, a.y, b.x, b.y);
    }

}

void EnvelopeEditor::drawWaveforms(juce::Graphics& g)
{
    const float waveOpacity = proc.apvts.getRawParameterValue("waveOpacity")->load();
    if (slot > 0) { if (waveOpacity > 0.01f) drawBandWaveform(g, waveOpacity); return; }
    if (waveOpacity > 0.01f)
    {
        drawInputWaveform(g, waveOpacity);

        if (proc.sidechainActive.load())
        {
            constexpr int bins = FreeLfoProcessor::waveBins;
            juce::Path w;
            for (int i = 0; i < bins; ++i)
            {
                const float mx = juce::jlimit(-1.f, 1.f, proc.scMax[(size_t) i].load(std::memory_order_relaxed));
                auto p = toScreen((float) i / (bins - 1), 0.5f + 0.5f * mx);
                if (i == 0) w.startNewSubPath(p); else w.lineTo(p);
            }
            for (int i = bins - 1; i >= 0; --i)
            {
                const float mn = juce::jlimit(-1.f, 1.f, proc.scMin[(size_t) i].load(std::memory_order_relaxed));
                w.lineTo(toScreen((float) i / (bins - 1), 0.5f + 0.5f * mn));
            }
            w.closeSubPath();
            g.setColour(paper::amber.withAlpha(waveOpacity * 0.8f));
            g.fillPath(w);
            g.setColour(paper::amber.darker(0.3f).withAlpha(juce::jmin(1.f, waveOpacity + 0.2f)));
            g.strokePath(w, juce::PathStrokeType(1.f));
        }
    }

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
            g.setColour(paper::bandColour(other - 1).withAlpha(0.38f));
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
    g.setColour(mainColour().withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(mainColour());
    g.strokePath(line, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved));

    // Actual output gain: the curve after depth and smoothing, as the audio thread will apply it.
    // Only drawn when it differs from the curve itself.
    const float depth = depthP->load();
    const float smoothMs = smoothP->load();
    const bool differs = depth < 0.999f || smoothMs > 0.05f;
    if (differs)
    {
        constexpr int N = 512;
        float v[N];
        for (int i = 0; i < N; ++i) v[i] = env.evaluate((float) i / N);

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

        juce::Path actual;
        for (int i = 0; i < N; ++i)
        {
            auto p = toScreen((float) i / (N - 1), out[i]);
            if (i == 0) actual.startNewSubPath(p); else actual.lineTo(p);
        }
        g.setColour(paper::text.withAlpha(0.3f));
        g.strokePath(actual, juce::PathStrokeType(2.f));
    }

    // Bezier handles for every point, After Effects style: thin yellow line to the point, filled yellow dot
    if (bezierMode() && !controlsHidden)
    {
        const juce::Colour yellow(0xffeab308);
        for (int pt = 0; pt < (int) env.points.size(); ++pt)
        {
            const auto origin = toScreen(env.points[(size_t) pt].x, env.points[(size_t) pt].y);
            for (int kind : { 1, 2 })
            {
                if (!handleVisible(pt, kind)) continue;
                const auto h = handleScreen(pt, kind);
                const bool active = dragKind == kind && pt == selected;
                g.setColour(yellow.withAlpha(0.95f));
                g.drawLine(origin.x, origin.y, h.x, h.y, 1.4f);
                g.setColour(active ? yellow.brighter(0.4f) : yellow);
                g.fillEllipse(h.x - 4.5f, h.y - 4.5f, 9.f, 9.f);
                g.setColour(yellow.darker(0.55f).withAlpha(0.9f));
                g.drawEllipse(h.x - 4.5f, h.y - 4.5f, 9.f, 9.f, 1.f);
            }
        }
    }

    // points (the selected one is filled); all hidden after a click on empty space
    for (int i = 0; i < (int) env.points.size() && !controlsHidden; ++i)
    {
        auto p = toScreen(env.points[(size_t) i].x, env.points[(size_t) i].y);
        g.setColour(i == selected ? mainColour() : paper::panel);
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
            g.setColour(paper::panel);
            g.fillPath(d);
            g.setColour(paper::text.withAlpha(dragSeg == i ? 1.f : 0.7f));
            g.strokePath(d, juce::PathStrokeType(1.5f));
        }
    }
}

size_t EnvelopeEditor::computeKey() const
{
    size_t h = 1469598103u;
    auto mix = [&h](float f) { h = (h ^ (size_t) (juce::roundToInt(f * 10000.f) + 1000003)) * 1099511628211ull; };
    for (auto& pt : env.points) { mix(pt.x); mix(pt.y); mix(pt.inDx); mix(pt.inDy); mix(pt.outDx); mix(pt.outDy); mix(pt.bend); mix(pt.bendX); }
    mix(depthP->load());
    mix(smoothP->load());
    mix((float) cycleSecs());
    mix(proc.apvts.getRawParameterValue("waveOpacity")->load());
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

void EnvelopeEditor::frame()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastFrameMs < 14.0) return; // ~60 fps cap, even on 120 Hz displays
    lastFrameMs = now;

    if (dragPoint < 0 && dragKind == 0 && dragSeg < 0)
        env = proc.getEnvelope(slot);
    if (slot > 0)
        for (int other = 1; other <= 3; ++other)
            if (other != slot) ghost[other - 1] = proc.getEnvelope(other);

    // Idle = no repaint: only redraw when audio is flowing or something changed.
    const unsigned blk = proc.blockCounter.load(std::memory_order_relaxed);
    const bool audioMoving = blk != lastBlock || (now - proc.uiPhaseTimeMs.load(std::memory_order_relaxed)) < 60.0;
    lastBlock = blk;
    if (audioMoving || computeKey() != curveKey || cachedGrid != gridX()) repaint();
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
    if (sizeChanged || cachedGrid != gridX()) // the grid is cached too: redraw when its line count changes
    {
        rebuild(gridImg, [this](juce::Graphics& ig) { renderGrid(ig); });
        cachedGrid = gridX();
        curveKey = 0;
    }

    const size_t key = computeKey();
    if (key != curveKey || sizeChanged) { rebuild(curveImg, [this](juce::Graphics& ig) { renderCurveLayer(ig); }); curveKey = key; }

    const auto unscale = juce::AffineTransform::scale(1.f / scale);
    g.drawImageTransformed(gridImg, unscale);
    drawWaveforms(g);
    g.drawImageTransformed(curveImg, unscale);

    // playhead
    const float phase = predictedPhase();
    auto top = toScreen(phase, 1.f), bottom = toScreen(phase, 0.f);
    g.setColour(paper::text.withAlpha(0.7f));
    g.drawLine(top.x, top.y, bottom.x, bottom.y, 1.f);
}

void EnvelopeEditor::mouseDown(const juce::MouseEvent& e)
{
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
        if (e.mods.isPopupMenu() && hit > 0 && hit < (int) env.points.size() - 1)
        {
            env.points.erase(env.points.begin() + hit);
            selected = -1;
            commit();
        }
        else
        {
            selected = hit;
            dragPoint = hit;
            // a click on neither a point, a handle nor the curve hides all the controls, so the bare curve shows
            if (hit < 0 && !e.mods.isPopupMenu() && !nearCurve(pos)) controlsHidden = true;
        }
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
        const auto n = toNorm(e.position);

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
        // Hold Alt to break the handles apart.
        const bool interior = selected > 0 && selected < (int) env.points.size() - 1;
        const bool mirror = interior && mirrorDrag && !e.mods.isAltDown();
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

void EnvelopeEditor::mouseUp(const juce::MouseEvent&) { dragPoint = -1; dragKind = 0; dragSeg = -1; repaint(); }

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
    else if (hit >= 0)
    {
        // Double-click a point: toggle "easy ease" (flat tangents). Again: back to a straight line.
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

