#include "Fonts.h"
#include "PluginEditor.h"
#include "ControlStrip.h"

// ---------------------------------------------------------------- FreeLfoEditor

// The light / dark choice is a user preference shared by every instance, so it lives in a small settings file
// (tests set FREELFO_NO_PREFS so they never read or write it).
static bool usePrefs() { return juce::SystemStats::getEnvironmentVariable("FREELFO_NO_PREFS", "").isEmpty(); }

static juce::PropertiesFile::Options uiPrefsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "FreeLfoTool UI";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = "FreeLfo";
    return o;
}

static constexpr bool defaultDark = true; // dark mode is the default look

static bool loadDarkPreference()
{
    if (!usePrefs()) return defaultDark;
    juce::PropertiesFile f(uiPrefsOptions());
    return f.getBoolValue("dark", defaultDark);
}

// Width a switch with a text label needs (the label sits left of the switch, see PaperLookAndFeel::drawToggleButton)
static int toggleWidth(const juce::String& text)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText(fonts::regular(13.f), text, 0.f, 0.f);
    return (int) std::ceil(ga.getBoundingBox(0, -1, true).getWidth()) + 6 + 30 + 2;
}

static juce::Point<int> loadSizePreference()
{
    if (!usePrefs()) return { 960, 610 };
    juce::PropertiesFile f(uiPrefsOptions());
    return { juce::jlimit(880, 2000, f.getIntValue("width", 960)), juce::jlimit(500, 1300, f.getIntValue("height", 610)) };
}

static void saveSizePreference(int w, int h)
{
    if (!usePrefs()) return;
    juce::PropertiesFile f(uiPrefsOptions());
    f.setValue("width", w);
    f.setValue("height", h);
    f.saveIfNeeded();
}

static bool loadThirtyFpsPreference()
{
    if (!usePrefs()) return false; // 60 is the default
    juce::PropertiesFile f(uiPrefsOptions());
    return f.getBoolValue("fps30", false);
}

static void saveThirtyFpsPreference(bool thirty)
{
    if (!usePrefs()) return;
    juce::PropertiesFile f(uiPrefsOptions());
    f.setValue("fps30", thirty);
    f.saveIfNeeded();
}

static void saveDarkPreference(bool dark)
{
    if (!usePrefs()) return;
    juce::PropertiesFile f(uiPrefsOptions());
    f.setValue("dark", dark);
    f.saveIfNeeded();
}

FreeLfoEditor::FreeLfoEditor(FreeLfoProcessor& p)
    : AudioProcessorEditor(&p), proc(p), curve(p, 0), mbPanel(p)
{
    theme::setDark(loadDarkPreference());
    laf.applyColours();
    setLookAndFeel(&laf);
    proc.editorOpen.store(true);
    setResizable(true, true);
    setResizeLimits(880, 500, 2000, 1300);
    const auto size = loadSizePreference();
    setSize(size.x, size.y);
    addAndMakeVisible(curve);
    addAndMakeVisible(curveMeter);
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    addAndMakeVisible(mbPanel);
    addAndMakeVisible(inTrim);
    addAndMakeVisible(outTrim);
    addAndMakeVisible(polarityBtn);
    addAndMakeVisible(clipBtn);
    addAndMakeVisible(bypassBtn);   // after the panel, so it sits on top of it
    addAndMakeVisible(kickSafeBtn);
    addAndMakeVisible(kickPreciseBtn);
    kickSafeBtn.onClick = [this] { kickSafeBtn.flash(); kickPreciseBtn.clearFlash(); curveFromKick(kickfit::Style::safe); };
    kickPreciseBtn.onClick = [this] { kickPreciseBtn.flash(); kickSafeBtn.clearFlash(); curveFromKick(kickfit::Style::precise); };
    addAndMakeVisible(kickHeading);
    // the multiband tab's band-count and type dropdowns sit in this header
    addChildComponent(mbPanel.bandsBox);
    addChildComponent(mbPanel.engineBox);

    addAndMakeVisible(mode);

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
    setup(wave, waveL, "Audio view");
    smooth.setTextValueSuffix(" ms");

    addAndMakeVisible(retrigger);
    snap.setToggleState(false, juce::dontSendNotification); // off by default: hold Option / Alt to snap
    snap.onClick = [this] { curve.snapToGrid = snap.getToggleState(); mbPanel.setSnap(snap.getToggleState()); };
    addAndMakeVisible(snap);
    addAndMakeVisible(bezier);

    // Draggable grid number: how many grid lines the curve editor has, and where snapping lands
    gridBox.setSliderStyle(juce::Slider::LinearBarVertical);
    gridBox.setMouseDragSensitivity(260);   // about 22 px of dragging per step, so each stop is easy to land on
    gridBox.setVelocityBasedMode(false);
    gridBox.setTooltip("Grid lines and snap steps (drag up or down)");
    addAndMakeVisible(gridBox);

    // Dotted / triplet modifiers for the straight note value on the Rate knob (one at a time)
    for (auto* b : { &dotBtn, &tripBtn }) { b->setClickingTogglesState(true); b->onStateChange = [this] { rate.updateText(); }; addAndMakeVisible(*b); }
    dotBtn.onClick = [this] { if (dotBtn.getToggleState()) tripBtn.setToggleState(false, juce::sendNotificationSync); rate.updateText(); };
    tripBtn.onClick = [this] { if (tripBtn.getToggleState()) dotBtn.setToggleState(false, juce::sendNotificationSync); rate.updateText(); };
    // Flipping the switch converts every curve to the other style, so it stays editable after the switch
    bezier.onClick = [this] { proc.convertCurveMode(bezier.getToggleState()); curve.repaint(); };

    reset.onClick = [this] { if (multibandOn()) mbPanel.resetCurve(); else curve.resetCurve(); };
    addAndMakeVisible(reset);

    addAndMakeVisible(brand);
    reset.getProperties().set("quiet", true); // an action on the curve, not the most important thing on screen
    addAndMakeVisible(themeBtn);
    addAndMakeVisible(fpsBtn);
    fpsBtn.onClick = [this] { setFrameRate(thirtyFps ? 60 : 30); };
    setFrameRate(loadThirtyFpsPreference() ? 30 : 60, false);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(presetBtn);
    presetBtn.onClick = [this] {
        const int slot = multibandOn() ? mbPanel.currentSlot() : 0;
        if (presetPopup != nullptr) { presetPopup.reset(); return; }
        juce::Component::SafePointer<FreeLfoEditor> safe(this);
        presetPopup = PresetMenu::open(*this, presetBtn, proc, slot, [safe] { if (safe != nullptr) safe->repaint(); },
                                       [safe] { juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->presetPopup.reset(); }); });
    };
    addAndMakeVisible(undoBtn);
    addAndMakeVisible(redoBtn);
    undoBtn.setEnabled(false);
    redoBtn.setEnabled(false);
    undoBtn.onClick = [this] { proc.undo(); undoBtn.setEnabled(proc.canUndo()); redoBtn.setEnabled(proc.canRedo()); };
    redoBtn.onClick = [this] { proc.redo(); undoBtn.setEnabled(proc.canUndo()); redoBtn.setEnabled(proc.canRedo()); };
    themeBtn.onClick = [this] { setDarkMode(!theme::dark); };

    // Tabs: Single = the original one-curve LFO (bit-exact pass-through), Multiband = per-band curves
    bypassA = std::make_unique<BA>(proc.apvts, "bypass", bypassBtn);
    polarityA = std::make_unique<BA>(proc.apvts, "polarity", polarityBtn);
    for (auto* k : { &inTrim, &outTrim })
    {
        const bool in = k == &inTrim;
        auto& a = in ? inTrimA : outTrimA;
        a = std::make_unique<SA>(proc.apvts, in ? "inGain" : "outGain", *k);
    }
    {
        auto* clipParam = proc.apvts.getParameter("clip");
        clipA = std::make_unique<juce::ParameterAttachment>(*clipParam, [this](float v) { clipBtn.setMode(juce::roundToInt(v)); });
        clipA->sendInitialUpdate();
        clipBtn.onModeChange = [this](int m) { clipA->setValueAsCompleteGesture((float) m); };
    }
    modeSwitch.onClick = [this] { setMultiband(!multibandOn()); };
    addAndMakeVisible(modeSwitch);

    // which parameter each knob drives, for the DAW's right-click options (the Rate knob's is set in bindRate)
    for (auto& [knob, id] : std::initializer_list<std::pair<ResetClickSlider*, const char*>> {
             { &depth, "depth" }, { &smooth, "smooth" }, { &offset, "offset" }, { &wave, "waveOpacity" },
             { &inTrim, "inGain" }, { &outTrim, "outGain" } })
        knob->hostParam = proc.apvts.getParameter(id);
    depthA = std::make_unique<SA>(proc.apvts, "depth", depth);
    smoothA = std::make_unique<SA>(proc.apvts, "smooth", smooth);
    offsetA = std::make_unique<SA>(proc.apvts, "offset", offset);
    // The grey gain line follows these knobs: redraw as soon as one moves, even when no audio is running (idle = no redraws)
    for (auto* k : { &depth, &smooth, &offset }) k->onValueChange = [this] { curve.repaint(); };
    waveA = std::make_unique<SA>(proc.apvts, "waveOpacity", wave);
    retrigA = std::make_unique<BA>(proc.apvts, "retrigger", retrigger);
    bezierA = std::make_unique<BA>(proc.apvts, "bezier", bezier);
    dotA = std::make_unique<BA>(proc.apvts, "dotted", dotBtn);
    tripA = std::make_unique<BA>(proc.apvts, "triplet", tripBtn);
    gridA = std::make_unique<SA>(proc.apvts, "gridDiv", gridBox); // steps through FreeLfoProcessor::gridStops
    gridBox.setDoubleClickReturnValue(true, FreeLfoProcessor::defaultGridStop);

    mode.onChange = [this] { bindRate(); };
    modeA = std::make_unique<CA>(proc.apvts, "mode", mode);
    bindRate();
    updateTab();
    startTimerHz(10); // follows the "mb" parameter if the host or a project load changes it
    reset.setColour(juce::TextButton::textColourOffId, theme::sub);
    sendLookAndFeelChange(); // make every child (slider text boxes included) pick up the theme colours
}

void FreeLfoEditor::setFrameRate(int framesPerSecond, bool persist)
{
    thirtyFps = framesPerSecond <= 30;
    fpsBtn.setShown(frameRate());
    if (persist) saveThirtyFpsPreference(thirtyFps);
}

void FreeLfoEditor::setDarkMode(bool dark, bool persist)
{
    theme::setDark(dark);
    laf.applyColours();
    reset.setColour(juce::TextButton::textColourOffId, theme::sub);
    if (persist) saveDarkPreference(dark);
    sendLookAndFeelChange(); // every child re-reads the theme colours
    repaint();
}

// Why no curve was made. Nothing to say when it worked: the new curve on screen shows that.
static juce::String kickMessage(const kickfit::Result& r)
{
    using S = kickfit::Status;
    switch (r.status)
    {
        case S::ok:          return {};
        case S::noSidechain: return "No sidechain. Route your kick into the plugin's sidechain first.";
        case S::notReady:    return "Not enough recorded yet. Play at least one full cycle, then press again.";
        case S::quiet:       return "No kick heard. Check the sidechain routing and press while playing.";
        case S::bandQuiet:   return "The kick has very little in this band, so the curve was left alone.";
        case S::noGaps:      return "The sidechain never fades far enough here to find a kick shape.";
        case S::severalHits: return juce::String(r.hits) + " hits in one cycle. Set Rate so a cycle holds one kick (often 1/4).";
        case S::crossesLoop: return "The kick is still ringing at the loop point. Check its timing or Offset.";
    }
    return {};
}

void FreeLfoEditor::curveFromKick(kickfit::Style style)
{
    const auto result = proc.fitCurveToKick(shownMb ? mbPanel.currentSlot() : 0, style);
    const auto text = kickMessage(result);
    const double showFor = text.isEmpty() ? 0.0 : 4500.0; // success also takes an earlier press's message away
    if (shownMb) mbPanel.showNote(text, showFor);
    else curve.showNote(text, showFor);
    undoBtn.setEnabled(proc.canUndo());
    redoBtn.setEnabled(proc.canRedo());
}

// Switch tabs explicitly. (ParameterAttachment ignores changes it made itself, so relying on its
// callback left the old tab on screen while the audio had already switched.)
void FreeLfoEditor::setMultiband(bool on)
{
    if (auto* prm = proc.apvts.getParameter("mb"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(on ? 1.f : 0.f);
        prm->endChangeGesture();
    }
    updateTab();
}

void FreeLfoEditor::updateTab()
{
    const bool mb = multibandOn();
    shownMb = mb;
    modeSwitch.setMode(mb);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &curve, &curveMeter, &inMeter, &outMeter, &mode, &rate, &depth, &smooth, &offset, &wave,
                                                                        &rateL, &depthL, &smoothL, &offsetL, &waveL })
        c->setVisible(!mb);
    mbPanel.setVisible(mb);
    mbPanel.bandsBox.setVisible(mb);
    mbPanel.engineBox.setVisible(mb);

    if (mb) { retrigger.setVisible(false); dotBtn.setVisible(false); tripBtn.setVisible(false); }
    else bindRate(); // restores the retrigger switch for Hz mode, and relayouts
    resized();
}

void FreeLfoEditor::bindRate()
{
    const bool syncMode = mode.getSelectedItemIndex() <= 0;
    rateA.reset(); // detach before swapping which parameter the knob drives
    rate.hostParam = proc.apvts.getParameter(syncMode ? "division" : "hz");

    // NB: creating a SliderAttachment installs its own text functions, so ours go in AFTER it, or they are ignored
    if (syncMode)
    {
        rateA = std::make_unique<SA>(proc.apvts, "division", rate);
        rate.textFromValueFunction = [this](double v) {
            return juce::String(FreeLfoProcessor::divisionNames[(size_t) juce::jlimit(0, FreeLfoProcessor::numDivisions - 1, (int) std::round(v))])
                   + (dotBtn.getToggleState() ? " D" : tripBtn.getToggleState() ? " T" : "");
        };
        rate.valueFromTextFunction = [this](const juce::String& t) { const int i = FreeLfoProcessor::parseDivisionText(t); return i < 0 ? rate.getValue() : (double) i; };
    }
    else
    {
        rateA = std::make_unique<SA>(proc.apvts, "hz", rate);
        rate.textFromValueFunction = [](double v) {
            return juce::String(v, v < 10.0 ? 2 : 1) + " Hz";
        };
        rate.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
    }
    rate.updateText();
    dotBtn.setVisible(syncMode && !multibandOn());   // dotted / triplet only mean something in Sync mode
    tripBtn.setVisible(syncMode && !multibandOn());
    retrigger.setVisible(!syncMode && !multibandOn());
    resized();
}

void FreeLfoEditor::paint(juce::Graphics& g)
{
    // One surface for the whole instrument: no outer card. Thin lines mark off the top bar and the groups in the bars.
    g.fillAll(theme::window);
    const int under = theme::size::top + theme::size::brandBar + theme::size::barGap / 2;
    g.setColour(theme::line);
    g.fillRect(0, under, getWidth(), 1);
    for (const auto& s : separators) g.fillRect(s);
}

FreeLfoEditor::~FreeLfoEditor()
{
    saveSizePreference(getWidth(), getHeight());   // remembered for next time
    stopTimer();
    proc.editorOpen.store(false);
    setLookAndFeel(nullptr);
}

void FreeLfoEditor::resized()
{
    const bool mb = multibandOn();
    namespace sz = theme::size;
    auto r = getLocalBounds().reduced(sz::edge, 0);
    r.removeFromTop(sz::top);
    auto top = r.removeFromTop(sz::brandBar);
    r.removeFromTop(sz::barGap);
    auto tools = r.removeFromTop(sz::toolBar);
    r.removeFromTop(6);
    r.removeFromBottom(8);

    separators.clear();
    auto divide = [this](juce::Rectangle<int>& bar, bool fromRight) { // a short vertical line in a 17 px gap
        const auto slot = fromRight ? bar.removeFromRight(sz::groupGap + 1) : bar.removeFromLeft(sz::groupGap + 1);
        separators.push_back(slot.withSizeKeepingCentre(1, 16));
    };

    // ---- first bar, global:  MUVMNT LFO  ...  undo redo | fps, light/dark | bypass
    brand.setBounds(top.removeFromLeft(BrandMark::width));
    bypassBtn.setBounds(top.removeFromRight(32).withSizeKeepingCentre(32, 26));   // the top right corner, in both tabs
    divide(top, true);
    themeBtn.setBounds(top.removeFromRight(24).withSizeKeepingCentre(22, 22));
    top.removeFromRight(sz::gap);
    fpsBtn.setBounds(top.removeFromRight(26).withSizeKeepingCentre(26, 25));
    divide(top, true);
    redoBtn.setBounds(top.removeFromRight(26).withSizeKeepingCentre(26, 26));
    undoBtn.setBounds(top.removeFromRight(26).withSizeKeepingCentre(26, 26));

    // ---- second bar.  Left, how the sound is processed:  Single / Multiband, then (Multiband only) bands and type.
    //      Right, tools for the curve:  retrigger, handles, snap, grid | presets, reset
    constexpr int together = 6; // controls that belong to one group
    modeSwitch.setBounds(tools.removeFromLeft(ModeSwitchButton::width));
    if (mb)
    {
        divide(tools, false);
        mbPanel.bandsBox.setBounds(tools.removeFromLeft(92).reduced(0, 2));
        tools.removeFromLeft(together);
        mbPanel.engineBox.setBounds(tools.removeFromLeft(112).reduced(0, 2));
    }
    reset.setBounds(tools.removeFromRight(88).reduced(0, 3));
    tools.removeFromRight(together);
    presetBtn.setBounds(tools.removeFromRight(96)); // choosing a shape and resetting it sit together: both replace the curve
    divide(tools, true);
    gridBox.setBounds(tools.removeFromRight(46).reduced(0, 3));
    tools.removeFromRight(6);
    snap.setBounds(tools.removeFromRight(toggleWidth("Snap")));
    tools.removeFromRight(14);
    bezier.setBounds(tools.removeFromRight(28).withSizeKeepingCentre(28, 26));
    tools.removeFromRight(14);
    retrigger.setBounds(tools.removeFromRight(toggleWidth("MIDI retrigger")));

    clipBtn.setVisible(mb);

    // ---- under the graph: the control strip, with the meters' column carrying on beside it (gains, clipper)
    auto body = r;
    auto strip = body.removeFromBottom(controls::height);
    const auto side = strip.removeFromRight(controls::meterColumn);
    strip.removeFromRight(controls::meterGap);
    inTrim.setBounds(side.getX(), side.getY() + 2, 22, 33);
    outTrim.setBounds(side.getX() + 24, side.getY() + 2, 22, 33);
    inTrim.toFront(false);
    outTrim.toFront(false);
    polarityBtn.setBounds(side.getX() + 1, side.getY() + 37, 44, 14);
    polarityBtn.toFront(false);
    clipBtn.setBounds(side.getX() + 1, side.getY() + 53, 44, 25); // ends above the resize corner

    // the kick tool: its own group, a heading in line with the knob captions and two push buttons, one above the other, level with the dials
    const auto column = controls::columns(strip);
    {
        auto k = column.kick;
        separators.push_back({ k.getX(), k.getY() + 18, 1, 48 });
        kickHeading.setBounds(k.removeFromTop(controls::captionH));
        // centred on the dials' middle, a pixel low: that leaves clear air under the heading
        auto both = k.withTrimmedBottom(16).withSizeKeepingCentre(kicktool::buttonWidth, 2 * kicktool::buttonHeight + kicktool::buttonGap).translated(0, 1);
        kickSafeBtn.setBounds(both.removeFromTop(kicktool::buttonHeight));
        kickPreciseBtn.setBounds(both.removeFromBottom(kicktool::buttonHeight));
    }

    if (mb) { mbPanel.setBounds(r); repaint(); return; }

    {
        auto area = body.withTrimmedBottom(6);
        auto meters = area.removeFromRight(controls::meterColumn);
        inMeter.setBounds(meters.removeFromLeft(22));
        outMeter.setBounds(meters.removeFromRight(22));
        area.removeFromRight(controls::meterGap);
        curveMeter.setBounds(area.removeFromRight(curveMeter.preferredWidth()));
        area.removeFromRight(controls::meterGap);
        curve.setBounds(area);
    }

    controls::place(rateL, rate, column.knob[0], controls::leadDial);
    controls::placeRateExtras(mode, dotBtn, tripBtn, column.knob[0]);
    controls::place(depthL, depth, column.knob[1], controls::leadDial);
    controls::place(smoothL, smooth, column.knob[2], controls::dial);
    controls::place(offsetL, offset, column.knob[3], controls::dial);
    controls::place(waveL, wave, column.knob[4], controls::viewDial);
    repaint(); // the separators may have moved
}

bool FreeLfoEditor::keyPressed(const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int c = k.getKeyCode();
    if (!m.isCommandDown() && !m.isCtrlDown()) return false;
    const bool z = c == 'z' || c == 'Z', y = c == 'y' || c == 'Y';
    if ((z && m.isShiftDown()) || y) return proc.redo();
    if (z) return proc.undo();
    return false;
}
