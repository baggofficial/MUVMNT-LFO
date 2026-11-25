#include "Fonts.h"
#include "PluginEditor.h"

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
    paper::setDark(loadDarkPreference());
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
    addAndMakeVisible(clipBtn);
    addAndMakeVisible(bypassBtn);   // after the panel, so it sits on top of it
    addChildComponent(mbPanel.modeBox);    // the multiband tab's Sync/Hz and band-count dropdowns sit in this header, like the single tab's
    addChildComponent(mbPanel.bandsBox);

    mode.addItemList({ "Sync", "Hz" }, 1);
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
    setup(wave, waveL, "Waveform");
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

    addAndMakeVisible(themeBtn);
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
    themeBtn.onClick = [this] { setDarkMode(!paper::dark); };

    // Tabs: Single = the original one-curve LFO (bit-exact pass-through), Multiband = per-band curves
    bypassA = std::make_unique<BA>(proc.apvts, "bypass", bypassBtn);
    {
        auto* clipParam = proc.apvts.getParameter("clip");
        clipA = std::make_unique<juce::ParameterAttachment>(*clipParam, [this](float v) { clipBtn.setMode(juce::roundToInt(v)); });
        clipA->sendInitialUpdate();
        clipBtn.onModeChange = [this](int m) { clipA->setValueAsCompleteGesture((float) m); };
    }
    modeSwitch.onClick = [this] { setMultiband(!multibandOn()); };
    addAndMakeVisible(modeSwitch);

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
    sendLookAndFeelChange(); // make every child (slider text boxes included) pick up the theme colours
}

void FreeLfoEditor::setDarkMode(bool dark, bool persist)
{
    paper::setDark(dark);
    laf.applyColours();
    if (persist) saveDarkPreference(dark);
    sendLookAndFeelChange(); // every child re-reads the theme colours
    repaint();
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
    mbPanel.modeBox.setVisible(mb);
    mbPanel.bandsBox.setVisible(mb);

    if (mb) { retrigger.setVisible(false); dotBtn.setVisible(false); tripBtn.setVisible(false); }
    else bindRate(); // restores the retrigger switch for Hz mode, and relayouts
    resized();
}

void FreeLfoEditor::bindRate()
{
    const bool syncMode = mode.getSelectedItemIndex() <= 0;
    rateA.reset(); // detach before swapping which parameter the knob drives

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
    g.fillAll(paper::bg);
    auto panelArea = getLocalBounds().toFloat().reduced(8.f);
    g.setColour(paper::panel);
    g.fillRoundedRectangle(panelArea, 10.f);
    g.setColour(paper::line);
    g.drawRoundedRectangle(panelArea, 10.f, 1.f);

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
    auto r = getLocalBounds().reduced(18);
    auto top = r.removeFromTop(36);
    modeSwitch.setBounds(top.removeFromLeft(64).reduced(0, 4));
    top.removeFromLeft(14);
    if (!mb) mode.setBounds(top.removeFromLeft(80).reduced(0, 4));
    else
    {
        mbPanel.modeBox.setBounds(top.removeFromLeft(80).reduced(0, 4));
        top.removeFromLeft(8);
        mbPanel.bandsBox.setBounds(top.removeFromLeft(92).reduced(0, 4));
    }
    // Right-hand group, right to left: Reset | light-dark button | Snap switch + grid number | Bezier switch | retrigger.
    // Items of a pair sit close together; groups are an even 16 px apart.
    constexpr int gap = 16;
    reset.setBounds(top.removeFromRight(70).reduced(0, 4));
    top.removeFromRight(12);
    themeBtn.setBounds(top.removeFromRight(24).withSizeKeepingCentre(22, 22));
    top.removeFromRight(gap);
    redoBtn.setBounds(top.removeFromRight(26).withSizeKeepingCentre(26, 26));
    undoBtn.setBounds(top.removeFromRight(26).withSizeKeepingCentre(26, 26));
    top.removeFromRight(6);
    presetBtn.setBounds(top.removeFromRight(52));
    top.removeFromRight(gap);
    gridBox.setBounds(top.removeFromRight(46).reduced(0, 5));
    top.removeFromRight(6);
    snap.setBounds(top.removeFromRight(toggleWidth("Snap")));
    top.removeFromRight(gap);
    bezier.setBounds(top.removeFromRight(28).withSizeKeepingCentre(28, 28));
    top.removeFromRight(gap);
    retrigger.setBounds(top.removeFromRight(toggleWidth("MIDI retrigger")));

    r.removeFromTop(8);
    clipBtn.setVisible(mb);
    clipBtn.setBounds(r.getRight() - 40, r.getBottom() - 92 + 6, 40, 28);   // multiband only, above the bypass button
    clipBtn.toFront(false);
    bypassBtn.setBounds(r.getRight() - 40, r.getBottom() - 92 + 6 + 32, 40, 28);   // under the IN / OUT meters, in both tabs
    bypassBtn.toFront(false);   // above the knob row of the single tab, which otherwise catches the clicks
    if (mb) { mbPanel.setBounds(r); return; }

    auto knobs = r.removeFromBottom(92);   // compact knobs: more room for the curve
    {
        auto area = r.withTrimmedBottom(8);
        outMeter.setBounds(area.removeFromRight(18));
        area.removeFromRight(4);
        inMeter.setBounds(area.removeFromRight(18));
        area.removeFromRight(6);
        curveMeter.setBounds(area.removeFromRight(curveMeter.preferredWidth()));
        area.removeFromRight(6);
        curve.setBounds(area);
    }

    const int w = knobs.getWidth() / 5;
    auto place = [&](juce::Label& l, juce::Slider& s) {
        auto c = knobs.removeFromLeft(w);
        l.setBounds(c.removeFromTop(16));
        s.setBounds(c);
    };
    const auto rateColumn = knobs.withWidth(w);
    place(rateL, rate);
    // dotted / triplet sit either side of the rate readout, like the note-value buttons on Xfer's LFO Tool
    dotBtn.setBounds(rateColumn.getCentreX() - 56, rateColumn.getBottom() - 22, 24, 20);
    tripBtn.setBounds(rateColumn.getCentreX() + 32, rateColumn.getBottom() - 22, 24, 20);
    place(depthL, depth);
    place(smoothL, smooth);
    place(offsetL, offset);
    place(waveL, wave);
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
