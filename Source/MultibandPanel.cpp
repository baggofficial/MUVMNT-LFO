#include "Fonts.h"
#include "MultibandPanel.h"

namespace
{
    constexpr double minHz = 20.0, maxHz = 20000.0;

    juce::String hzText(double hz)
    {
        return hz < 1000.0 ? juce::String(juce::roundToInt(hz)) + " Hz" : juce::String(hz / 1000.0, 2) + " kHz";
    }

    void setParam(juce::AudioProcessorValueTreeState& s, const char* id, float plain)
    {
        if (auto* p = s.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
    }
}

// ---------------------------------------------------------------- BandStrip

BandStrip::BandStrip(FreeLfoProcessor& p) : proc(p) { startTimerHz(15); }

bool BandStrip::threeBands() const { return proc.apvts.getRawParameterValue("mbBands")->load() > 0.5f; }

int BandStrip::signature() const
{
    const auto v = [this](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
    return (int) (v("xLow") * 3.f + v("xHigh") * 7.f + v("mbBands") * 100003.f
                  + (v("soloL") + v("soloM") * 2.f + v("soloH") * 4.f) * 7919.f
                  + v("slopeLow") * 1009.f + v("slopeHigh") * 2003.f + (float) paper::version * 31337.f);
}

void BandStrip::timerCallback()
{
    const int s = signature();
    if (s != lastSig) { lastSig = s; repaint(); }
}

float BandStrip::freqToX(double hz) const
{
    const auto b = getLocalBounds().toFloat().reduced(8.f, 0.f);
    const double t = std::log(juce::jlimit(minHz, maxHz, hz) / minHz) / std::log(maxHz / minHz);
    return b.getX() + (float) t * b.getWidth();
}

double BandStrip::xToFreq(float x) const
{
    const auto b = getLocalBounds().toFloat().reduced(8.f, 0.f);
    const double t = juce::jlimit(0.0, 1.0, (double) ((x - b.getX()) / b.getWidth()));
    return minHz * std::pow(maxHz / minHz, t);
}

std::array<BandStrip::Region, 3> BandStrip::regions(int& count) const
{
    const float lo = freqToX(proc.apvts.getRawParameterValue("xLow")->load());
    const float hi = freqToX(proc.apvts.getRawParameterValue("xHigh")->load());
    const float x0 = freqToX(minHz), x1 = freqToX(maxHz);
    if (threeBands()) { count = 3; return { Region { 0, x0, lo }, Region { 1, lo, hi }, Region { 2, hi, x1 } }; }
    count = 2;
    return { Region { 0, x0, lo }, Region { 2, lo, x1 }, Region { 2, 0, 0 } };
}

juce::Rectangle<float> BandStrip::soloChip(float regionRight) const
{
    return { regionRight - 26.f, 6.f, 20.f, 18.f };
}

// The small pill in the middle of a split line: shows that split's slope, click it to change it
juce::Rectangle<float> BandStrip::slopeChip(float splitX) const
{
    return { splitX - 27.f, (float) getHeight() * 0.5f - 9.f, 54.f, 18.f };
}

void BandStrip::plotArea(float& top, float& bottom) const
{
    top = 8.f;
    bottom = (float) getHeight() - 3.f; // the curves use the whole strip
}

void BandStrip::openSlopeMenu(int split)
{
    const char* id = split == 0 ? "slopeLow" : "slopeHigh";
    const int current = juce::roundToInt(proc.apvts.getRawParameterValue(id)->load());
    const char* names[numSlopes] = { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };

    juce::PopupMenu menu;
    for (int i = 0; i < numSlopes; ++i) menu.addItem(i + 1, names[i], true, i == current);

    const double hz = proc.apvts.getRawParameterValue(split == 0 ? "xLow" : "xHigh")->load();
    const auto area = localAreaToGlobal(slopeChip(freqToX(hz)).toNearestInt());
    juce::Component::SafePointer<BandStrip> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(area), [safe, id](int result) {
        if (safe != nullptr && result > 0) { setParam(safe->proc.apvts, id, (float) (result - 1)); safe->repaint(); }
    });
}

void BandStrip::paint(juce::Graphics& g)
{
    g.setColour(paper::bg);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.f);

    int count = 0;
    const auto regs = regions(count);
    const auto v = [this](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
    const char* names[3] = { "Low", "Mid", "High" };
    const float h = (float) getHeight();
    float plotTop, plotBottom;
    plotArea(plotTop, plotBottom);

    // light region tints, so the response curves on top stay readable
    for (int i = 0; i < count; ++i)
    {
        const auto& r = regs[(size_t) i];
        const auto area = juce::Rectangle<float>(r.x0, 1.f, r.x1 - r.x0, h - 2.f);
        const bool sel = r.band == selected;
        g.setColour(paper::bandColour(r.band).withAlpha(sel ? 0.16f : 0.07f));
        g.fillRect(area);
        if (sel) { g.setColour(paper::bandColour(r.band).withAlpha(0.7f)); g.drawRect(area, 1.5f); }
    }

    // octave guides + the -6 dB line where Linkwitz-Riley bands cross
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour(paper::ink(0.05f));
        g.drawVerticalLine((int) freqToX(f), 0.f, h);
    }
    const float y6 = plotTop + (plotBottom - plotTop) * (6.f / 36.f);
    g.setColour(paper::ink(0.10f));
    g.drawHorizontalLine((int) y6, freqToX(minHz), freqToX(maxHz));
    g.setColour(paper::sub.withAlpha(0.8f));
    g.setFont(fonts::regular(10.f));
    g.drawText("-6 dB", juce::Rectangle<float>(freqToX(minHz) + 4.f, y6 - 12.f, 40.f, 11.f), juce::Justification::centredLeft);

    // Each band's real filter response (the actual crossover maths), drawn across the whole axis,
    // so you can see how the neighbouring bands overlap at the split and how the slope changes it.
    {
        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        const bool three = count == 3;
        Splitter sp1, sp2;
        sp1.set(v("xLow"), sr, (Slope) juce::jlimit(0, numSlopes - 1, juce::roundToInt(v("slopeLow"))));
        sp2.set(juce::jmax(v("xHigh"), v("xLow") * 1.2f), sr, (Slope) juce::jlimit(0, numSlopes - 1, juce::roundToInt(v("slopeHigh"))));

        auto magnitude = [&](int band, double hz) {
            if (band == 0) return sp1.lowMagnitude(hz, sr);
            if (!three) return sp1.highMagnitude(hz, sr);
            return band == 1 ? sp1.highMagnitude(hz, sr) * sp2.lowMagnitude(hz, sr)
                             : sp1.highMagnitude(hz, sr) * sp2.highMagnitude(hz, sr);
        };

        const float xa = freqToX(minHz), xb = freqToX(maxHz);
        for (int pass = 0; pass < 2; ++pass)          // selected band last, so it sits on top
            for (int i = 0; i < count; ++i)
            {
                const int band = regs[(size_t) i].band;
                if ((band == selected) != (pass == 1)) continue;

                juce::Path line, fill;
                for (float x = xa; x <= xb + 0.5f; x += 2.f)
                {
                    const double mag = magnitude(band, xToFreq(x));
                    const double db = 20.0 * std::log10(juce::jmax(1e-6, mag));
                    const float y = plotTop + (float) juce::jlimit(0.0, 1.0, -db / 36.0) * (plotBottom - plotTop);
                    if (x == xa) { line.startNewSubPath(x, y); fill.startNewSubPath(x, plotBottom); fill.lineTo(x, y); }
                    else { line.lineTo(x, y); fill.lineTo(x, y); }
                }
                fill.lineTo(xb, plotBottom);
                fill.closeSubPath();
                const bool sel = band == selected;
                g.setColour(paper::bandColour(band).withAlpha(sel ? 0.26f : 0.16f));
                g.fillPath(fill);
                g.setColour(paper::strong(paper::bandColour(band)).withAlpha(sel ? 1.f : 0.75f));
                g.strokePath(line, juce::PathStrokeType(sel ? 2.f : 1.3f));
            }
    }

    for (int i = 0; i < count; ++i)
    {
        const auto& r = regs[(size_t) i];
        const auto area = juce::Rectangle<float>(r.x0, 1.f, r.x1 - r.x0, h - 2.f);
        const bool sel = r.band == selected;
        g.setColour(paper::text.withAlpha(sel ? 0.9f : 0.55f));
        g.setFont(fonts::semibold(13.f));
        g.drawText(names[r.band], area.withTrimmedLeft(8.f).withHeight(24.f), juce::Justification::centredLeft);

        const auto chip = soloChip(r.x1);
        const bool on = v((juce::String("solo") + FreeLfoProcessor::bandSuffix(r.band)).toRawUTF8()) > 0.5f;
        g.setColour(on ? paper::accent : paper::panel);
        g.fillRoundedRectangle(chip, 4.f);
        g.setColour(on ? juce::Colours::white : paper::sub);
        g.setFont(fonts::semibold(11.f));
        g.drawText("S", chip, juce::Justification::centred);
    }

    // split lines, their slope pills (click = menu), drag handles and frequency labels
    const char* slopeText[numSlopes] = { "6 dB", "12 dB", "24 dB", "48 dB", "72 dB", "96 dB" };
    for (int i = 0; i < count - 1; ++i)
    {
        const double hz = v(i == 0 ? "xLow" : "xHigh");
        const float x = freqToX(hz);
        g.setColour(paper::text.withAlpha(0.6f));
        g.drawVerticalLine((int) x, 0.f, h);

        const auto chip = slopeChip(x);
        g.setColour(paper::panel);
        g.fillRoundedRectangle(chip, 9.f);
        g.setColour(paper::text.withAlpha(0.7f));
        g.drawRoundedRectangle(chip, 9.f, 1.f);
        g.setColour(paper::text);
        g.setFont(fonts::semibold(11.f));
        g.drawText(slopeText[juce::jlimit(0, numSlopes - 1, juce::roundToInt(v(i == 0 ? "slopeLow" : "slopeHigh")))], chip, juce::Justification::centred);

        // No drag knob: the split line itself is the handle. The frequency chip sits at its foot.
        const auto label = juce::Rectangle<float>(x - 28.f, h - 22.f, 56.f, 15.f);
        g.setColour(paper::bg.withAlpha(0.92f)); // keeps the split line from running through the text
        g.fillRoundedRectangle(label, 4.f);
        g.setFont(fonts::semibold(11.f));
        g.setColour(paper::sub);
        g.drawText(hzText(hz), label, juce::Justification::centred);
    }
}

void BandStrip::mouseDown(const juce::MouseEvent& e)
{
    int count = 0;
    const auto regs = regions(count);

    for (int i = 0; i < count; ++i) // solo chips first
        if (soloChip(regs[(size_t) i].x1).contains(e.position))
        {
            // each band has its own solo switch, so several can be on at once
            const juce::String id = juce::String("solo") + FreeLfoProcessor::bandSuffix(regs[(size_t) i].band);
            const bool on = proc.apvts.getRawParameterValue(id)->load() > 0.5f;
            setParam(proc.apvts, id.toRawUTF8(), on ? 0.f : 1.f);
            repaint();
            return;
        }

    for (int i = 0; i < count - 1; ++i) // the pill on a split line opens that split's slope menu
        if (slopeChip(freqToX(proc.apvts.getRawParameterValue(i == 0 ? "xLow" : "xHigh")->load())).contains(e.position))
        {
            openSlopeMenu(i);
            return;
        }

    dragHandle = -1;
    for (int i = 0; i < count - 1; ++i)
    {
        const double hz = proc.apvts.getRawParameterValue(i == 0 ? "xLow" : "xHigh")->load();
        if (std::abs(freqToX(hz) - e.position.x) < 8.f)
        {
            dragHandle = i;
            if (auto* p = proc.apvts.getParameter(i == 0 ? "xLow" : "xHigh")) p->beginChangeGesture();
            return;
        }
    }

    for (int i = 0; i < count; ++i)
        if (e.position.x >= regs[(size_t) i].x0 && e.position.x < regs[(size_t) i].x1)
        {
            selected = regs[(size_t) i].band;
            if (onSelect) onSelect(selected);
            repaint();
            return;
        }
}

void BandStrip::mouseDrag(const juce::MouseEvent& e)
{
    if (dragHandle < 0) return;
    double hz = xToFreq(e.position.x);
    if (dragHandle == 0)
    {
        const double hi = proc.apvts.getRawParameterValue("xHigh")->load();
        hz = juce::jlimit(40.0, threeBands() ? juce::jmin(1000.0, hi / 1.2) : 1000.0, hz);
        setParam(proc.apvts, "xLow", (float) hz);
    }
    else
    {
        const double lo = proc.apvts.getRawParameterValue("xLow")->load();
        hz = juce::jlimit(juce::jmax(500.0, lo * 1.2), 12000.0, hz);
        setParam(proc.apvts, "xHigh", (float) hz);
    }
    repaint();
}

// Show a left-right resize cursor while the mouse is over a split line (the line is the drag handle)
void BandStrip::mouseMove(const juce::MouseEvent& e)
{
    int count = 0;
    regions(count);
    bool overLine = false;
    for (int i = 0; i < count - 1 && !overLine; ++i)
    {
        const double hz = proc.apvts.getRawParameterValue(i == 0 ? "xLow" : "xHigh")->load();
        overLine = std::abs(freqToX(hz) - e.position.x) < 8.f && !slopeChip(freqToX(hz)).contains(e.position);
    }
    setMouseCursor(overLine ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void BandStrip::mouseUp(const juce::MouseEvent&)
{
    if (dragHandle >= 0)
        if (auto* p = proc.apvts.getParameter(dragHandle == 0 ? "xLow" : "xHigh")) p->endChangeGesture();
    dragHandle = -1;
}

// ---------------------------------------------------------------- MultibandPanel

MultibandPanel::MultibandPanel(FreeLfoProcessor& p) : proc(p), strip(p), curve(p, 1)
{
    addAndMakeVisible(strip);
    addAndMakeVisible(curve);
    strip.onSelect = [this](int b) { selectBand(b); };

    modeBox.addItemList({ "Sync", "Hz" }, 1);
    bandsBox.addItemList({ "2 bands", "3 bands" }, 1);
    for (auto* b : { &dotBtn, &tripBtn }) { b->setClickingTogglesState(true); b->onStateChange = [this] { rate.updateText(); }; addAndMakeVisible(*b); }
    dotBtn.onClick = [this] { if (dotBtn.getToggleState()) tripBtn.setToggleState(false, juce::sendNotificationSync); rate.updateText(); };
    tripBtn.onClick = [this] { if (tripBtn.getToggleState()) dotBtn.setToggleState(false, juce::sendNotificationSync); rate.updateText(); };

    auto setup = [this](juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 16);
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(s);
        addAndMakeVisible(l);
    };
    setup(rate, rateL, "Rate");
    setup(depth, depthL, "Depth");
    setup(smooth, smoothL, "Smooth");
    setup(offset, offsetL, "Offset");
    setup(wave, waveL, "Waveform");
    smooth.setTextValueSuffix(" ms");


    struct { LinkButton* b; const char* what; const char* base; const char* id; std::unique_ptr<BA>* a; } links[] = {
        { &linkDepthBtn, "depth", "depth", "linkDepth", &linkDepthA }, { &linkSmoothBtn, "smoothing", "smooth", "linkSmooth", &linkSmoothA },
        { &linkOffsetBtn, "phase offset", "offset", "linkOffset", &linkOffsetA } };
    for (auto& l : links)
    {
        l.b->setTooltip(juce::String("Link ") + l.what + ": every band follows one value");
        addAndMakeVisible(*l.b);
        *l.a = std::make_unique<BA>(proc.apvts, l.id, *l.b);
        l.b->onClick = [this, b = l.b, base = l.base, id = l.id] { if (b->getToggleState()) copyToLinkSource(base, id); };
    }
    bandsA = std::make_unique<CA>(proc.apvts, "mbBands", bandsBox);
    linkBtn.setTooltip("Link rates: every band shares one rate");
    addAndMakeVisible(linkBtn);
    linkA = std::make_unique<BA>(proc.apvts, "mbLink", linkBtn);
    waveA = std::make_unique<SA>(proc.apvts, "waveOpacity", wave);

    rebind();
    startTimerHz(10);
}

void MultibandPanel::copyToLinkSource(const char* base, const char*)
{
    const bool three = proc.apvts.getRawParameterValue("mbBands")->load() > 0.5f;
    const int src = three ? 1 : 0;
    if (selected == src) return;
    auto* from = proc.apvts.getParameter(juce::String(base) + FreeLfoProcessor::bandSuffix(selected));
    auto* to = proc.apvts.getParameter(juce::String(base) + FreeLfoProcessor::bandSuffix(src));
    if (from == nullptr || to == nullptr) return;
    to->beginChangeGesture();
    to->setValueNotifyingHost(from->getValue());
    to->endChangeGesture();
}

int MultibandPanel::signature() const
{
    const auto v = [this](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
    const bool three = v("mbBands") > 0.5f, link = v("mbLink") > 0.5f;
    const int eff = link ? (three ? 1 : 0) : selected;
    const juce::String es = FreeLfoProcessor::bandSuffix(eff);
    const int links = (v("linkDepth") > 0.5f ? 1 : 0) | (v("linkSmooth") > 0.5f ? 2 : 0) | (v("linkOffset") > 0.5f ? 4 : 0);
    return (three ? 1 : 0) | (link ? 2 : 0) | (((int) v(("mode" + es).toRawUTF8())) << 2) | (selected << 3) | (links << 5) | (paper::version << 8);
}

void MultibandPanel::timerCallback()
{
    if (signature() != lastSig) rebind();
    else rate.updateText(); // picks up dotted / triplet changes (cheap: only touches the label if the text changed)
}

void MultibandPanel::selectBand(int band)
{
    selected = band;
    rebind();
}

void MultibandPanel::rebind()
{
    const auto v = [this](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
    const bool three = v("mbBands") > 0.5f, link = v("mbLink") > 0.5f;
    if (!three && selected == 1) selected = 0; // there is no mid band in 2-band mode

    const int eff = link ? (three ? 1 : 0) : selected; // linked bands all follow one band's rate settings
    const juce::String es = FreeLfoProcessor::bandSuffix(eff), ss = FreeLfoProcessor::bandSuffix(selected);

    rateA.reset(); modeA.reset(); depthA.reset(); smoothA.reset(); offsetA.reset(); dotA.reset(); tripA.reset();

    modeA = std::make_unique<CA>(proc.apvts, "mode" + es, modeBox);
    const bool syncMode = proc.apvts.getRawParameterValue("mode" + es)->load() < 0.5f;
    // NB: creating a SliderAttachment installs its own text functions, so ours go in AFTER it, or they are ignored
    if (syncMode)
    {
        rateA = std::make_unique<SA>(proc.apvts, "div" + es, rate);
        rate.textFromValueFunction = [this](double x) {
            return juce::String(FreeLfoProcessor::divisionNames[(size_t) juce::jlimit(0, FreeLfoProcessor::numDivisions - 1, (int) std::round(x))])
                   + (dotBtn.getToggleState() ? " D" : tripBtn.getToggleState() ? " T" : "");
        };
        rate.valueFromTextFunction = [this](const juce::String& t) { const int i = FreeLfoProcessor::parseDivisionText(t); return i < 0 ? rate.getValue() : (double) i; };
    }
    else
    {
        rateA = std::make_unique<SA>(proc.apvts, "hz" + es, rate);
        rate.textFromValueFunction = [](double x) { return juce::String(x, x < 10.0 ? 2 : 1) + " Hz"; };
        rate.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
    }
    dotA = std::make_unique<BA>(proc.apvts, "dot" + es, dotBtn);   // dotted / triplet follow the same band as the rate
    tripA = std::make_unique<BA>(proc.apvts, "trip" + es, tripBtn);
    dotBtn.setVisible(syncMode);
    tripBtn.setVisible(syncMode);
    rate.updateText();

    // a linked control shows (and edits) the one band every band follows
    const juce::String src = FreeLfoProcessor::bandSuffix(three ? 1 : 0);
    const bool ld = v("linkDepth") > 0.5f, ls = v("linkSmooth") > 0.5f, lo = v("linkOffset") > 0.5f;
    depthA = std::make_unique<SA>(proc.apvts, "depth" + (ld ? src : ss), depth);
    smoothA = std::make_unique<SA>(proc.apvts, "smooth" + (ls ? src : ss), smooth);
    offsetA = std::make_unique<SA>(proc.apvts, "offset" + (lo ? src : ss), offset);
    depthL.setText(ld ? "Depth (all bands)" : "Depth", juce::dontSendNotification);
    smoothL.setText(ls ? "Smooth (all bands)" : "Smooth", juce::dontSendNotification);
    offsetL.setText(lo ? "Offset (all bands)" : "Offset", juce::dontSendNotification);

    curve.setSlot(selected + 1);
    strip.setSelected(selected);

    rateL.setText(link ? "Rate (all bands)" : "Rate", juce::dontSendNotification);

    lastSig = signature();
}

void MultibandPanel::resized()
{
    auto r = getLocalBounds();
    strip.setBounds(r.removeFromTop(84));
    r.removeFromTop(8);
    auto knobs = r.removeFromBottom(92);   // compact knobs: more room for the curve
    curve.setBounds(r.withTrimmedBottom(8));

    // same five knob columns as the single tab; the dropdowns and the link chain live in the editor's header
    const int w = knobs.getWidth() / 5;
    auto place = [&](juce::Label& l, juce::Slider& s) {
        auto c = knobs.removeFromLeft(w);
        l.setBounds(c.removeFromTop(16));
        s.setBounds(c);
    };
    const auto rateColumn = knobs.withWidth(w);
    place(rateL, rate);
    linkBtn.setBounds(rate.getBounds().getCentreX() + 36, rate.getBounds().getY() + 14, 22, 22);
    dotBtn.setBounds(rateColumn.getCentreX() - 56, rateColumn.getBottom() - 22, 24, 20);
    tripBtn.setBounds(rateColumn.getCentreX() + 32, rateColumn.getBottom() - 22, 24, 20);
    auto chain = [&](LinkButton& b, const juce::Slider& s) {   // beside the knob, like dotted / triplet beside the rate
        b.setBounds(s.getBounds().getCentreX() + 36, s.getBounds().getY() + 14, 22, 22);
    };
    place(depthL, depth);
    chain(linkDepthBtn, depth);
    place(smoothL, smooth);
    chain(linkSmoothBtn, smooth);
    place(offsetL, offset);
    chain(linkOffsetBtn, offset);
    place(waveL, wave);
}
