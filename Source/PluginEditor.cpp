#include "PluginEditor.h"

// ---------------------------------------------------------------- FreeLfoEditor

FreeLfoEditor::FreeLfoEditor(FreeLfoProcessor& p)
    : AudioProcessorEditor(&p), proc(p), curve(p, 0), mbPanel(p)
{
    setLookAndFeel(&laf);
    proc.editorOpen.store(true);
    setSize(900, 610);
    addAndMakeVisible(curve);
    addAndMakeVisible(mbPanel);

    mode.addItemList({ "Sync", "Hz" }, 1);
    addAndMakeVisible(mode);

    auto setup = [this](juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
        s.setColour(juce::Slider::textBoxTextColourId, paper::text); // explicit: the box is built before the theme reaches it
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
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
    snap.setToggleState(true, juce::dontSendNotification);
    snap.onClick = [this] { curve.snapToGrid = snap.getToggleState(); mbPanel.setSnap(snap.getToggleState()); };
    addAndMakeVisible(snap);
    addAndMakeVisible(bezier);
    // Flipping the switch converts every curve to the other style, so it stays editable after the switch
    bezier.onClick = [this] { proc.convertCurveMode(bezier.getToggleState()); curve.repaint(); };

    reset.onClick = [this] { if (multibandOn()) mbPanel.resetCurve(); else curve.resetCurve(); };
    addAndMakeVisible(reset);

    // Tabs: Single = the original one-curve LFO (bit-exact pass-through), Multiband = per-band curves
    tabSingle.onClick = [this] { setMultiband(false); };
    tabMulti.onClick = [this] { setMultiband(true); };
    addAndMakeVisible(tabSingle);
    addAndMakeVisible(tabMulti);

    depthA = std::make_unique<SA>(proc.apvts, "depth", depth);
    smoothA = std::make_unique<SA>(proc.apvts, "smooth", smooth);
    offsetA = std::make_unique<SA>(proc.apvts, "offset", offset);
    waveA = std::make_unique<SA>(proc.apvts, "waveOpacity", wave);
    retrigA = std::make_unique<BA>(proc.apvts, "retrigger", retrigger);
    bezierA = std::make_unique<BA>(proc.apvts, "bezier", bezier);

    mode.onChange = [this] { bindRate(); };
    modeA = std::make_unique<CA>(proc.apvts, "mode", mode);
    bindRate();
    updateTab();
    startTimerHz(10); // follows the "mb" parameter if the host or a project load changes it
    sendLookAndFeelChange(); // make every child (slider text boxes included) pick up the theme colours
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
    tabSingle.setToggleState(!mb, juce::dontSendNotification);
    tabMulti.setToggleState(mb, juce::dontSendNotification);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &curve, &mode, &rate, &depth, &smooth, &offset, &wave,
                                                                        &rateL, &depthL, &smoothL, &offsetL, &waveL })
        c->setVisible(!mb);
    mbPanel.setVisible(mb);

    if (mb) retrigger.setVisible(false);
    else bindRate(); // restores the retrigger switch for Hz mode, and relayouts
    resized();
}

void FreeLfoEditor::bindRate()
{
    const bool syncMode = mode.getSelectedItemIndex() <= 0;
    rateA.reset(); // detach before swapping which parameter the knob drives

    if (syncMode)
    {
        rate.textFromValueFunction = [](double v) {
            return juce::String(FreeLfoProcessor::divisionNames[(size_t) juce::jlimit(0, 14, (int) std::round(v))]);
        };
        rate.valueFromTextFunction = nullptr;
        rateA = std::make_unique<SA>(proc.apvts, "division", rate);
    }
    else
    {
        rate.textFromValueFunction = [](double v) {
            return juce::String(v, v < 10.0 ? 2 : 1) + " Hz";
        };
        rate.valueFromTextFunction = nullptr;
        rateA = std::make_unique<SA>(proc.apvts, "hz", rate);
    }
    rate.updateText();
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

    g.setColour(paper::text);
    g.setFont(juce::FontOptions(16.f, juce::Font::bold));
    g.drawText("Free LFO Tool", 24, 18, 160, 28, juce::Justification::centredLeft);
}

void FreeLfoEditor::resized()
{
    const bool mb = multibandOn();
    auto r = getLocalBounds().reduced(18);
    auto top = r.removeFromTop(36);
    top.removeFromLeft(150); // title
    tabSingle.setBounds(top.removeFromLeft(74).reduced(0, 4));
    tabMulti.setBounds(top.removeFromLeft(96).reduced(0, 4));
    top.removeFromLeft(14);
    if (!mb) mode.setBounds(top.removeFromLeft(90).reduced(0, 4));
    reset.setBounds(top.removeFromRight(70).reduced(0, 4));
    top.removeFromRight(10);
    snap.setBounds(top.removeFromRight(86));
    bezier.setBounds(top.removeFromRight(150));
    retrigger.setBounds(top.removeFromRight(150));

    r.removeFromTop(8);
    if (mb) { mbPanel.setBounds(r); return; }

    auto knobs = r.removeFromBottom(120);
    curve.setBounds(r.withTrimmedBottom(8));

    const int w = knobs.getWidth() / 5;
    auto place = [&](juce::Label& l, juce::Slider& s) {
        auto c = knobs.removeFromLeft(w);
        l.setBounds(c.removeFromTop(18));
        s.setBounds(c);
    };
    place(rateL, rate);
    place(depthL, depth);
    place(smoothL, smooth);
    place(offsetL, offset);
    place(waveL, wave);
}
