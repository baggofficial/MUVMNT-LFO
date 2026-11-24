#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "EnvelopeEditor.h"
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

private:
    FreeLfoProcessor& proc;
    PaperLookAndFeel laf;
    EnvelopeEditor curve;
    MultibandPanel mbPanel;

    juce::ComboBox mode;
    Knob rate, depth, smooth, offset, wave;
    juce::ToggleButton retrigger { "MIDI retrigger" }, snap { "Snap" };
    BezierToggle bezier;
    PresetButton presetBtn;
    std::unique_ptr<juce::Component> presetPopup; // the open preset popup (a layer inside this window), if any
    juce::TooltipWindow tooltipWindow { this, 500 }; // the icon buttons explain themselves on hover
    juce::TextButton reset { "Reset" }, tabSingle { "Single" }, tabMulti { "Multiband" };
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
    std::unique_ptr<BA> retrigA, bezierA, dotA, tripA;
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
};
