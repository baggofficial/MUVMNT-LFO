#include "Fonts.h"
#include "MultibandPanel.h"
#include "ControlStrip.h"

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

BandStrip::BandStrip(FreeLfoProcessor& p) : proc(p) {}

double BandStrip::splitHz(int split) const
{
    const double lo = proc.apvts.getRawParameterValue("xLow")->load();
    if (split == 1) return proc.apvts.getRawParameterValue("xHigh")->load();
    return threeBands() ? juce::jmin(1000.0, lo) : lo;   // matches the audio: with 3 bands the low split tops out at 1 kHz
}

bool BandStrip::dynamicEq() const { return proc.apvts.getRawParameterValue("mbEngine")->load() > 0.5f; }

bool BandStrip::threeBands() const { return proc.apvts.getRawParameterValue("mbBands")->load() > 0.5f; }

int BandStrip::signature() const
{
    const auto v = [this](const char* id) { return proc.apvts.getRawParameterValue(id)->load(); };
    return (int) (v("xLow") * 3.f + v("xHigh") * 7.f + v("mbBands") * 100003.f
                  + (v("soloL") + v("soloM") * 2.f + v("soloH") * 4.f) * 7919.f
                  + v("mbEngine") * 500009.f + v("slopeLow") * 1009.f + v("slopeHigh") * 2003.f + (float) theme::version * 31337.f);
}

void BandStrip::animate()
{
    const int s = signature();
    const bool wasLive = specLive;
    updateSpectrum();
    if (s != lastSig || specLive || wasLive) { lastSig = s; repaint(); }
}

// Live spectrum of the plugin's input and output: newest samples -> FFT -> dB, fast attack and slow release so it is easy to read.
void BandStrip::updateSpectrum()
{
    const unsigned bc = proc.blockCounter.load(std::memory_order_relaxed);
    const bool flowing = bc != lastBlock;
    lastBlock = bc;
    if (specDb.empty()) { specDb.assign((size_t) spectrum::fftSize / 2, -120.f); specDbOut = specDb; }

    auto analyse = [&](const std::array<std::atomic<float>, FreeLfoProcessor::specSize>& fifo, std::vector<float>& smooth) {
        specSamples.resize((size_t) spectrum::fftSize);
        auto& samples = specSamples;
        const int w = proc.specPos.load(std::memory_order_acquire);
        for (int i = 0; i < spectrum::fftSize; ++i)
            samples[(size_t) i] = fifo[(size_t) ((w + FreeLfoProcessor::specSize - spectrum::fftSize + i) % FreeLfoProcessor::specSize)]
                                      .load(std::memory_order_relaxed);
        auto& db = specTmpDb;
        analyser.magnitudesDb(samples.data(), db);
        for (size_t k = 0; k < smooth.size(); ++k)
            smooth[k] += (db[k] - smooth[k]) * (db[k] > smooth[k] ? 0.6f : 0.12f);
    };

    if (flowing)
    {
        analyse(proc.specFifo, specDb);
        analyse(proc.specFifoOut, specDbOut);
    }
    else
    {
        for (auto& v : specDb) v = juce::jmax(-120.f, v - 4.f);   // no audio: fade out
        for (auto& v : specDbOut) v = juce::jmax(-120.f, v - 4.f);
    }

    specLive = false;
    for (float v : specDb) if (v > -100.f) { specLive = true; break; }
}

// One spectrum as a line (and optionally a closed fill) across the whole frequency axis.
juce::Path BandStrip::spectrumPath(const std::vector<float>& db, float plotTop, float plotBottom, juce::Path* fill) const
{
    const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
    const double binHz = sr / (double) spectrum::fftSize;
    const float xa = freqToX(minHz), xb = freqToX(maxHz);

    juce::Path line;
    bool first = true;
    for (float x = xa; x <= xb + 0.5f; x += 2.f)
    {
        const double f0 = xToFreq(x - 1.f), f1 = xToFreq(x + 1.f), fc = xToFreq(x);
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        float v;
        if (b1 - b0 < 1.0) // low frequencies: fewer bins than pixels, so interpolate
        {
            const double b = fc / binHz;
            const int i = juce::jlimit(0, (int) db.size() - 2, (int) b);
            const float t = (float) juce::jlimit(0.0, 1.0, b - i);
            v = db[(size_t) i] + (db[(size_t) i + 1] - db[(size_t) i]) * t;
        }
        else
        {
            v = -120.f;
            for (int i = juce::jmax(1, (int) b0); i <= juce::jmin((int) db.size() - 1, (int) b1); ++i) v = juce::jmax(v, db[(size_t) i]);
        }
        v += 3.f * (float) std::log2(fc / 1000.0);   // 3 dB per octave tilt: pink-ish music reads roughly flat
        const float y = plotBottom - juce::jlimit(0.f, 1.f, (v + 90.f) / 80.f) * (plotBottom - plotTop);
        if (first)
        {
            line.startNewSubPath(x, y);
            if (fill != nullptr) { fill->startNewSubPath(x, plotBottom); fill->lineTo(x, y); }
            first = false;
        }
        else { line.lineTo(x, y); if (fill != nullptr) fill->lineTo(x, y); }
    }
    if (fill != nullptr) { fill->lineTo(xb, plotBottom); fill->closeSubPath(); }
    return line;
}

void BandStrip::drawSpectrum(juce::Graphics& g, float plotTop, float plotBottom) const
{
    if (!specLive || specDb.empty()) return;
    juce::Path fill;
    const auto inLine = spectrumPath(specDb, plotTop, plotBottom, &fill);
    g.setColour(theme::ink(0.15f));
    g.fillPath(fill);
    g.setColour(theme::ink(0.42f));
    g.strokePath(inLine, juce::PathStrokeType(1.f));

    // after the plugin: a bright line, so you can see what the ducking does to the balance
    g.setColour(theme::accent.withAlpha(0.9f));
    g.strokePath(spectrumPath(specDbOut, plotTop, plotBottom, nullptr), juce::PathStrokeType(1.4f));

    // tiny legend
    const float xr = freqToX(maxHz) - 6.f, y = plotBottom - 12.f;
    g.setColour(theme::bg.withAlpha(0.88f)); // so the spectrum lines don't run through the words
    g.fillRoundedRectangle({ xr - 56.f, y - 1.f, 60.f, 12.f }, theme::size::radiusSmall);
    g.setFont(fonts::regular(9.5f));
    g.setColour(theme::accent);
    g.drawText("OUT", juce::Rectangle<float>(xr - 30.f, y, 30.f, 10.f), juce::Justification::centredRight);
    g.setColour(theme::ink(0.55f));
    g.drawText("IN", juce::Rectangle<float>(xr - 62.f, y, 24.f, 10.f), juce::Justification::centredRight);
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
    const float lo = freqToX(splitHz(0));
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
    return { splitX - 31.f, (float) getHeight() * 0.5f - 10.f, 62.f, 20.f };
}

void BandStrip::plotArea(float& top, float& bottom) const
{
    top = 8.f;
    bottom = (float) getHeight() - 3.f; // the curves use the whole strip
}

juce::PopupMenu BandStrip::slopeMenu(int current)
{
    const char* names[numSlopes] = { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
    juce::PopupMenu menu;
    for (int i = 0; i < numSlopes; ++i) menu.addItem(i + 1, names[i], true, i == current);
    return menu;
}

void BandStrip::openSlopeMenu(int split)
{
    const char* id = split == 0 ? "slopeLow" : "slopeHigh";
    auto menu = slopeMenu(juce::roundToInt(proc.apvts.getRawParameterValue(id)->load()));
    menu.setLookAndFeel(&getLookAndFeel()); // a menu is its own window: without this it gets JUCE's stock grey one

    const double hz = splitHz(split);
    const auto area = localAreaToGlobal(slopeChip(freqToX(hz)).toNearestInt());
    juce::Component::SafePointer<BandStrip> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(area.expanded(0, 3)).withDeletionCheck(*this),
                       [safe, id](int result) {
        if (safe != nullptr && result > 0) { setParam(safe->proc.apvts, id, (float) (result - 1)); safe->repaint(); }
    });
}

void BandStrip::paint(juce::Graphics& g)
{
    g.setColour(theme::bg);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), theme::size::radius);

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
        g.setColour(theme::bandColour(r.band).withAlpha(sel ? 0.17f : 0.05f));
        g.fillRect(area);
        if (sel) // a solid bar along the top and a fine frame: which band is being edited is never in doubt
        {
            g.setColour(theme::bandColour(r.band).withAlpha(0.55f));
            g.drawRect(area, theme::size::border);
            g.setColour(theme::bandColour(r.band));
            g.fillRect(area.withHeight(3.f));
        }
    }

    // octave guides + the -6 dB line where Linkwitz-Riley bands cross
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour(theme::ink(0.05f));
        g.drawVerticalLine((int) freqToX(f), 0.f, h);
    }
    const bool dyn = dynamicEq();
    if (! dyn)
    {
        const float y6 = plotTop + (plotBottom - plotTop) * (6.f / 36.f);
        g.setColour(theme::ink(0.10f));
        g.drawHorizontalLine((int) y6, freqToX(minHz), freqToX(maxHz));
        g.setColour(theme::sub.withAlpha(0.8f));
        g.setFont(fonts::regular(10.f));
        g.drawText("-6 dB", juce::Rectangle<float>(freqToX(minHz) + 4.f, y6 + 3.f, 40.f, 11.f), juce::Justification::centredLeft);
    }

    drawSpectrum(g, plotTop, plotBottom);

    // Each band's real filter response (the actual crossover maths), drawn across the whole axis,
    // so you can see how the neighbouring bands overlap at the split and how the slope changes it.
    {
        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        const bool three = count == 3;
        Splitter sp1, sp2;
        sp1.set(splitHz(0), sr, (Slope) juce::jlimit(0, numSlopes - 1, juce::roundToInt(v("slopeLow"))));
        sp2.set(juce::jmax((double) v("xHigh"), splitHz(0) * 1.2), sr, (Slope) juce::jlimit(0, numSlopes - 1, juce::roundToInt(v("slopeHigh"))));

        const double f2 = juce::jmax((double) v("xHigh"), splitHz(0) * 1.2);
        auto magnitude = [&](int band, double hz) {
            if (dyn)   // what you hear with only this band open: the response of the Dynamic EQ's shelves
            {
                return DynamicEq::soloMagnitude(band, hz, splitHz(0), f2, three, sr);
            }
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
                g.setColour(theme::bandColour(band).withAlpha(sel ? 0.24f : 0.10f));
                g.fillPath(fill);
                g.setColour(theme::strong(theme::bandColour(band)).withAlpha(sel ? 1.f : 0.6f));
                g.strokePath(line, juce::PathStrokeType(sel ? 2.f : 1.2f));
            }
    }

    for (int i = 0; i < count; ++i)
    {
        const auto& r = regs[(size_t) i];
        const auto area = juce::Rectangle<float>(r.x0, 1.f, r.x1 - r.x0, h - 2.f);
        const bool sel = r.band == selected;
        g.setColour(sel ? theme::bandText(r.band) : theme::bandColour(r.band).withAlpha(0.75f));
        g.setFont(fonts::semibold(theme::type::label).withExtraKerningFactor(0.09f));
        g.drawText(juce::String(names[r.band]).toUpperCase(), area.withTrimmedLeft(8.f).withTrimmedTop(2.f).withHeight(22.f), juce::Justification::centredLeft);

        const auto chip = soloChip(r.x1);
        const bool on = v((juce::String("solo") + FreeLfoProcessor::bandSuffix(r.band)).toRawUTF8()) > 0.5f;
        g.setColour(on ? theme::accent : theme::panel);
        g.fillRoundedRectangle(chip, 4.f);
        g.setColour(on ? theme::onAccent : theme::sub);
        g.setFont(fonts::semibold(11.f));
        g.drawText("S", chip, juce::Justification::centred);
    }

    // split lines, their slope pills (click = menu), drag handles and frequency labels
    const char* slopeText[numSlopes] = { "6 dB", "12 dB", "24 dB", "48 dB", "72 dB", "96 dB" };
    for (int i = 0; i < count - 1; ++i)
    {
        const double hz = splitHz(i);
        const float x = freqToX(hz);
        g.setColour(theme::text.withAlpha(0.6f));
        g.drawVerticalLine((int) x, 0.f, h);

        const auto chip = slopeChip(x);
        if (! dynamicEq())   // the Dynamic EQ has one fixed slope, so there is nothing to choose
        {
            g.setColour(theme::raised);
            g.fillRoundedRectangle(chip, theme::size::radiusSmall);
            g.setColour(theme::sub.withAlpha(0.6f));
            g.drawRoundedRectangle(chip, theme::size::radiusSmall, theme::size::border);
            g.setColour(theme::text);
            g.setFont(fonts::semibold(11.f));
            g.drawText(slopeText[juce::jlimit(0, numSlopes - 1, juce::roundToInt(v(i == 0 ? "slopeLow" : "slopeHigh")))],
                       chip.withTrimmedRight(11.f), juce::Justification::centred);
            juce::Path chevron;
            const juce::Point<float> c(chip.getRight() - 9.f, chip.getCentreY());
            chevron.startNewSubPath(c.x - 3.f, c.y - 1.4f);
            chevron.lineTo(c.x, c.y + 1.6f);
            chevron.lineTo(c.x + 3.f, c.y - 1.4f);
            g.setColour(theme::sub);
            g.strokePath(chevron, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // No drag knob: the split line itself is the handle. The frequency chip sits at its foot.
        const auto label = juce::Rectangle<float>(x - 28.f, h - 22.f, 56.f, 15.f);
        g.setColour(theme::bg.withAlpha(0.92f)); // keeps the split line from running through the text
        g.fillRoundedRectangle(label, 4.f);
        g.setFont(fonts::semibold(11.f));
        g.setColour(theme::sub);
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
        if (! dynamicEq() && slopeChip(freqToX(splitHz(i))).contains(e.position))
        {
            openSlopeMenu(i);
            return;
        }

    dragHandle = -1;
    for (int i = 0; i < count - 1; ++i)
    {
        const double hz = splitHz(i);
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
        hz = juce::jlimit(40.0, threeBands() ? juce::jmin(1000.0, hi / 1.2) : 12000.0, hz);
        setParam(proc.apvts, "xLow", (float) hz);
    }
    else
    {
        const double lo = splitHz(0);
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
        const double hz = splitHz(i);
        overLine = std::abs(freqToX(hz) - e.position.x) < 8.f && (dynamicEq() || !slopeChip(freqToX(hz)).contains(e.position));
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
    addAndMakeVisible(curveMeter);
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    strip.onSelect = [this](int b) { selectBand(b); };

    addAndMakeVisible(modeBox);
    bandsBox.addItemList({ "2 bands", "3 bands" }, 1);
    engineBox.addItemList({ "Crossover", "Dynamic EQ" }, 1);
    engineBox.setTooltip("Crossover: splits the audio into bands. Steep, selectable slopes.\n"
                         "Dynamic EQ: keeps the audio unchanged when the curves are fully open (gain knobs at 0 dB, clipper off),\n"
                         "but separates the bands more gently.");
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
    // the grey gain line follows these knobs: redraw as soon as one moves, even when no audio is running
    for (auto* k : { &depth, &smooth, &offset }) k->onValueChange = [this] { curve.repaint(); };
    setup(wave, waveL, "Audio view");
    smooth.setTextValueSuffix(" ms");


    struct { LinkButton* b; const char* what; const char* base; const char* id; std::unique_ptr<BA>* a; } links[] = {
        { &linkDepthBtn, "Depth", "depth", "linkDepth", &linkDepthA }, { &linkSmoothBtn, "Smooth", "smooth", "linkSmooth", &linkSmoothA },
        { &linkOffsetBtn, "Offset", "offset", "linkOffset", &linkOffsetA } };
    for (auto& l : links)
    {
        l.b->setTooltip(juce::String("Use the same ") + l.what + " for all bands");
        addAndMakeVisible(*l.b);
        *l.a = std::make_unique<BA>(proc.apvts, l.id, *l.b);
        l.b->onClick = [this, b = l.b, base = l.base, id = l.id] { if (b->getToggleState()) copyToLinkSource(base, id); };
    }
    bandsA = std::make_unique<CA>(proc.apvts, "mbBands", bandsBox);
    engineA = std::make_unique<CA>(proc.apvts, "mbEngine", engineBox);
    linkBtn.setTooltip("Use the same Rate for all bands");
    addAndMakeVisible(linkBtn);
    linkA = std::make_unique<BA>(proc.apvts, "mbLink", linkBtn);
    waveA = std::make_unique<SA>(proc.apvts, "waveOpacity", wave);
    wave.hostParam = proc.apvts.getParameter("waveOpacity");

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
    return (three ? 1 : 0) | (link ? 2 : 0) | (((int) v(("mode" + es).toRawUTF8())) << 2) | (selected << 3) | (links << 5) | (theme::version << 8);
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
    rate.hostParam = proc.apvts.getParameter((syncMode ? "div" : "hz") + es);
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
    depth.hostParam = proc.apvts.getParameter("depth" + (ld ? src : ss));
    smooth.hostParam = proc.apvts.getParameter("smooth" + (ls ? src : ss));
    offset.hostParam = proc.apvts.getParameter("offset" + (lo ? src : ss));
    depthL.setText(ld ? "Depth (all bands)" : "Depth", juce::dontSendNotification);
    smoothL.setText(ls ? "Smooth (all bands)" : "Smooth", juce::dontSendNotification);
    offsetL.setText(lo ? "Offset (all bands)" : "Offset", juce::dontSendNotification);

    curve.setSlot(selected + 1);
    strip.setSelected(selected);

    // in the band's own colour while a knob edits that band, neutral while it drives all of them
    PaperLookAndFeel::setKnobBand(rate, link ? -1 : selected);
    PaperLookAndFeel::setKnobBand(depth, ld ? -1 : selected);
    PaperLookAndFeel::setKnobBand(smooth, ls ? -1 : selected);
    PaperLookAndFeel::setKnobBand(offset, lo ? -1 : selected);

    rateL.setText(link ? "Rate (all bands)" : "Rate", juce::dontSendNotification);
    placeChains();

    lastSig = signature();
}

void MultibandPanel::resized()
{
    auto r = getLocalBounds();
    strip.setBounds(r.removeFromTop(84));
    r.removeFromTop(8);
    auto knobs = r.removeFromBottom(controls::height);
    knobs.removeFromRight(controls::meterColumn + controls::meterGap); // the gains and the clipper live there
    {
        auto area = r.withTrimmedBottom(6);
        auto meters = area.removeFromRight(controls::meterColumn);
        inMeter.setBounds(meters.removeFromLeft(22));
        outMeter.setBounds(meters.removeFromRight(22));
        area.removeFromRight(controls::meterGap);
        curveMeter.setBounds(area.removeFromRight(curveMeter.preferredWidth()));
        area.removeFromRight(controls::meterGap);
        curve.setBounds(area);
    }

    // the same strip as the single tab; the dropdowns live in the editor's second bar
    const auto column = controls::columns(knobs);
    controls::place(rateL, rate, column.knob[0], controls::leadDial);
    controls::placeRateExtras(modeBox, dotBtn, tripBtn, column.knob[0]);
    controls::place(depthL, depth, column.knob[1], controls::leadDial);
    controls::place(smoothL, smooth, column.knob[2], controls::dial);
    controls::place(offsetL, offset, column.knob[3], controls::dial);
    controls::place(waveL, wave, column.knob[4], controls::viewDial);
    placeChains();
}

// the chains follow their captions, whose text changes when a link is switched
void MultibandPanel::placeChains()
{
    controls::placeChain(linkBtn, rateL);
    controls::placeChain(linkDepthBtn, depthL);
    controls::placeChain(linkSmoothBtn, smoothL);
    controls::placeChain(linkOffsetBtn, offsetL);
}
