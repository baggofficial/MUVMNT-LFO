#include "PluginEditor.h"

// ---------------------------------------------------------------- EnvelopeEditor

EnvelopeEditor::EnvelopeEditor(FreeLfoProcessor& p) : proc(p), env(p.getEnvelope())
{
    startTimerHz(30);
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

juce::Point<float> EnvelopeEditor::handlePos(int seg) const
{
    const float mx = 0.5f * (env.points[(size_t) seg].x + env.points[(size_t) seg + 1].x);
    return toScreen(mx, env.evaluate(mx));
}

int EnvelopeEditor::hitPoint(juce::Point<float> p) const
{
    for (size_t i = 0; i < env.points.size(); ++i)
        if (toScreen(env.points[i].x, env.points[i].y).getDistanceFrom(p) < 9.f) return (int) i;
    return -1;
}

int EnvelopeEditor::hitHandle(juce::Point<float> p) const
{
    for (size_t i = 0; i + 1 < env.points.size(); ++i)
        if (handlePos((int) i).getDistanceFrom(p) < 8.f) return (int) i;
    return -1;
}

void EnvelopeEditor::commit() { proc.setEnvelope(env); }

void EnvelopeEditor::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff15181d));
    g.fillRoundedRectangle(area, 6.f);

    // grid
    for (int i = 0; i <= gridX; ++i)
    {
        g.setColour(juce::Colours::white.withAlpha(i % 4 == 0 ? 0.14f : 0.06f));
        auto a = toScreen((float) i / gridX, 0.f), b = toScreen((float) i / gridX, 1.f);
        g.drawLine(a.x, a.y, b.x, b.y);
    }
    for (int i = 0; i <= gridY; ++i)
    {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        auto a = toScreen(0.f, (float) i / gridY), b = toScreen(1.f, (float) i / gridY);
        g.drawLine(a.x, a.y, b.x, b.y);
    }

    // input waveform folded onto one LFO cycle (opacity is a parameter)
    const float waveOpacity = proc.apvts.getRawParameterValue("waveOpacity")->load();
    if (waveOpacity > 0.01f)
    {
        auto drawWave = [&](auto& mnArr, auto& mxArr, juce::Colour colour) {
            constexpr int bins = FreeLfoProcessor::waveBins;
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
            g.setColour(colour.withAlpha(waveOpacity));
            g.fillPath(w);
        };
        drawWave(proc.waveMin, proc.waveMax, juce::Colour(0xff6aa8ff));                  // plugin input (blue)
        if (proc.sidechainActive.load())
            drawWave(proc.scMin, proc.scMax, juce::Colour(0xffff9a3c));                  // sidechain / kick (orange)
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
    fill.lineTo(toScreen(1.f, 0.f));
    fill.closeSubPath();
    const auto accent = juce::Colour(0xff39d0a5);
    g.setColour(accent.withAlpha(0.16f));
    g.fillPath(fill);
    g.setColour(accent);
    g.strokePath(line, juce::PathStrokeType(2.f));

    // bend handles
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    for (int i = 0; i + 1 < (int) env.points.size(); ++i)
    {
        auto h = handlePos(i);
        juce::Path d;
        d.addQuadrilateral(h.x, h.y - 4, h.x + 4, h.y, h.x, h.y + 4, h.x - 4, h.y);
        g.fillPath(d);
    }

    // points
    for (auto& pt : env.points)
    {
        auto p = toScreen(pt.x, pt.y);
        g.setColour(juce::Colours::white);
        g.fillEllipse(p.x - 5.f, p.y - 5.f, 10.f, 10.f);
        g.setColour(accent);
        g.drawEllipse(p.x - 5.f, p.y - 5.f, 10.f, 10.f, 1.5f);
    }

    // playhead
    auto top = toScreen(proc.uiPhase.load(), 1.f), bottom = toScreen(proc.uiPhase.load(), 0.f);
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.drawLine(top.x, top.y, bottom.x, bottom.y, 1.f);
}

void EnvelopeEditor::mouseDown(const juce::MouseEvent& e)
{
    env = proc.getEnvelope();
    const auto pos = e.position;
    dragPoint = hitPoint(pos);
    dragHandle = dragPoint < 0 ? hitHandle(pos) : -1;
    lastY = pos.y;

    if (e.mods.isPopupMenu() && dragPoint > 0 && dragPoint < (int) env.points.size() - 1)
    {
        env.points.erase(env.points.begin() + dragPoint);
        dragPoint = -1;
        commit();
        repaint();
    }
}

void EnvelopeEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragPoint >= 0)
    {
        auto n = toNorm(e.position);
        if (snapToGrid && !e.mods.isAltDown())
        {
            n.x = std::round(n.x * gridX) / gridX;
            n.y = std::round(n.y * (gridY * 4)) / (gridY * 4);
        }
        auto& pt = env.points[(size_t) dragPoint];
        pt.y = n.y;
        if (dragPoint > 0 && dragPoint < (int) env.points.size() - 1)
        {
            const float lo = env.points[(size_t) dragPoint - 1].x + 0.002f;
            const float hi = env.points[(size_t) dragPoint + 1].x - 0.002f;
            pt.x = juce::jlimit(lo, hi, n.x);
        }
        commit();
        repaint();
    }
    else if (dragHandle >= 0)
    {
        auto& a = env.points[(size_t) dragHandle];
        const auto& b = env.points[(size_t) dragHandle + 1];
        const float sign = b.y >= a.y ? 1.f : -1.f;
        const float delta = (lastY - e.position.y) / (float) juce::jmax(1, getHeight()) * 3.f;
        a.curve = juce::jlimit(-1.f, 1.f, a.curve + delta * sign);
        lastY = e.position.y;
        commit();
        repaint();
    }
}

void EnvelopeEditor::mouseUp(const juce::MouseEvent&) { dragPoint = dragHandle = -1; }

void EnvelopeEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    env = proc.getEnvelope();
    if (hitPoint(e.position) >= 0) return;

    if (hitHandle(e.position) >= 0) // double-click a handle: straighten the segment
    {
        env.points[(size_t) hitHandle(e.position)].curve = 0.f;
    }
    else
    {
        auto n = toNorm(e.position);
        if (snapToGrid) n.x = std::round(n.x * gridX) / gridX;
        if (n.x <= 0.f || n.x >= 1.f) return;
        env.points.push_back({ n.x, n.y, 0.f });
        env.normalize();
    }
    commit();
    repaint();
}

// ---------------------------------------------------------------- FreeLfoEditor

FreeLfoEditor::FreeLfoEditor(FreeLfoProcessor& p) : AudioProcessorEditor(&p), proc(p), curve(p)
{
    setSize(760, 480);
    addAndMakeVisible(curve);

    mode.addItemList({ "Sync", "Hz" }, 1);
    for (size_t i = 0; i < FreeLfoProcessor::divisionNames.size(); ++i)
        division.addItem(FreeLfoProcessor::divisionNames[i], (int) i + 1);
    addAndMakeVisible(mode);
    addAndMakeVisible(division);

    auto setup = [this](juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(s);
        addAndMakeVisible(l);
    };
    setup(hz, hzL, "Hz");
    setup(depth, depthL, "Depth");
    setup(smooth, smoothL, "Smooth");
    setup(offset, offsetL, "Offset");
    setup(wave, waveL, "Waveform");
    hz.setTextValueSuffix(" Hz");
    smooth.setTextValueSuffix(" ms");

    addAndMakeVisible(retrigger);
    snap.setToggleState(true, juce::dontSendNotification);
    snap.onClick = [this] { curve.snapToGrid = snap.getToggleState(); };
    addAndMakeVisible(snap);

    reset.onClick = [this] { proc.setEnvelope(Envelope()); curve.repaint(); };
    addAndMakeVisible(reset);

    modeA = std::make_unique<CA>(proc.apvts, "mode", mode);
    divA = std::make_unique<CA>(proc.apvts, "division", division);
    hzA = std::make_unique<SA>(proc.apvts, "hz", hz);
    depthA = std::make_unique<SA>(proc.apvts, "depth", depth);
    smoothA = std::make_unique<SA>(proc.apvts, "smooth", smooth);
    offsetA = std::make_unique<SA>(proc.apvts, "offset", offset);
    waveA = std::make_unique<SA>(proc.apvts, "waveOpacity", wave);
    retrigA = std::make_unique<BA>(proc.apvts, "retrigger", retrigger);

    mode.onChange = [this] { updateModeVisibility(); };
    updateModeVisibility();
}

void FreeLfoEditor::updateModeVisibility()
{
    const bool syncMode = mode.getSelectedItemIndex() == 0;
    division.setVisible(syncMode);
    hz.setVisible(!syncMode);
    hzL.setVisible(!syncMode);
    retrigger.setVisible(!syncMode);
    resized(); // knob row re-flows so Sync mode has no empty Hz slot
}

void FreeLfoEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0e1013));
}

void FreeLfoEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    auto top = r.removeFromTop(32);
    mode.setBounds(top.removeFromLeft(90));
    top.removeFromLeft(8);
    division.setBounds(top.removeFromLeft(110));
    retrigger.setBounds(top.removeFromLeft(140).translated(8, 0));
    reset.setBounds(top.removeFromRight(110));
    snap.setBounds(top.removeFromRight(70));

    r.removeFromTop(8);
    auto knobs = r.removeFromBottom(110);
    curve.setBounds(r);

    const bool showHz = hz.isVisible();
    const int w = knobs.getWidth() / (showHz ? 5 : 4);
    auto place = [&](juce::Label& l, juce::Slider& s) {
        auto c = knobs.removeFromLeft(w);
        l.setBounds(c.removeFromTop(18));
        s.setBounds(c);
    };
    if (showHz) place(hzL, hz);
    place(depthL, depth);
    place(smoothL, smooth);
    place(offsetL, offset);
    place(waveL, wave);
}
