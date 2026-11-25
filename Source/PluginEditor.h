#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "EnvelopeEditor.h"
#include "LevelMeter.h"
#include "MultibandPanel.h"
#include "PresetMenu.h"

class FreeLfoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FreeLfoEditor(FreeLfoProcessor&);
    ~FreeLfoEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void setDarkMode(bool dark, bool persist = true); // light / dark theme; persist = remember it for next time
    void selectBandForSnapshot(int band) { mbPanel.selectBand(band); }
    // One step of everything that moves in the window. Normally driven by the display; public for the screenless tests.
    void animate()
    {
        if (shownMb) { mbPanel.animate(); return; }
        curve.animate();
        curveMeter.animate();
        inMeter.animate();
        outMeter.animate();
    }

private:
    FreeLfoProcessor& proc;
    PaperLookAndFeel laf;
    EnvelopeEditor curve;
    WrapMeter curveMeter { proc, curve };
    LevelMeter inMeter { proc, false, "IN" }, outMeter { proc, true, "OUT" };
    MultibandPanel mbPanel;

    juce::ComboBox mode;
    Knob rate, depth, smooth, offset, wave;
    juce::ToggleButton retrigger { "MIDI retrigger" }, snap { "Snap" };
    BezierToggle bezier;
    PresetButton presetBtn;
    std::unique_ptr<juce::Component> presetPopup; // the open preset popup (a layer inside this window), if any
    juce::TooltipWindow tooltipWindow { this, 500 }; // the icon buttons explain themselves on hover
    juce::TextButton reset { "Reset" };
    ModeSwitchButton modeSwitch;   // Single <-> Multiband
    TrimKnob inTrim, outTrim;      // input / output gain, right under the meters
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inTrimA, outTrimA;
    ClipButton clipBtn;            // multiband only, under the trims
    std::unique_ptr<juce::ParameterAttachment> clipA;
    BypassButton bypassBtn;        // top right, beside Reset
    juce::TextButton dotBtn { juce::String::fromUTF8("\xE2\x80\xA2") }, tripBtn { "3" }; // dotted / triplet, like Xfer's LFO Tool
    ThemeToggle themeBtn;
    HistoryButton undoBtn { false }, redoBtn { true };
    juce::Slider gridBox;                                                                   // draggable number: grid lines + snap
    juce::Label rateL, depthL, smoothL, offsetL, waveL;

    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> modeA;
    std::unique_ptr<SA> rateA, depthA, smoothA, offsetA, waveA;
    std::unique_ptr<BA> retrigA, bezierA, dotA, tripA, bypassA;
    std::unique_ptr<SA> gridA;

    bool multibandOn() const { return proc.apvts.getRawParameterValue("mb")->load() > 0.5f; }
    void bindRate();  // one knob: snaps to note values in Sync mode, free Hz otherwise
    void updateTab(); // show the Single or the Multiband tab
    void setMultiband(bool on);
    void timerCallback() override // automation / project load changed "mb" or the dotted / triplet switches
    {
        undoBtn.setEnabled(proc.canUndo());
        redoBtn.setEnabled(proc.canRedo());
        if (multibandOn() != shownMb) updateTab();
        else if (!shownMb) rate.updateText();   // cheap: only touches the label if the text really changed
    }

    bool shownMb = false;

    // The one animation clock. Everything that moves is redrawn in the same step, because each window update has a
    // large fixed cost: separate timers meant two or three times as many updates. 30 steps a second.
    static constexpr double minStepMs = 30.0; // every 2nd refresh at 60 Hz, every 4th at 120 Hz
    double lastStepMs = 0.0;
    juce::VBlankAttachment clock { this, [this] {   // keep last: it can fire as soon as it exists
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastStepMs < minStepMs) return;
        lastStepMs = now;
        animate();
    } };
};
