#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "EnvelopeEditor.h"
#include "MultibandPanel.h"

class FreeLfoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FreeLfoEditor(FreeLfoProcessor&);
    ~FreeLfoEditor() override { stopTimer(); proc.editorOpen.store(false); setLookAndFeel(nullptr); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void selectBandForSnapshot(int band) { mbPanel.selectBand(band); }

private:
    FreeLfoProcessor& proc;
    PaperLookAndFeel laf;
    EnvelopeEditor curve;
    MultibandPanel mbPanel;

    juce::ComboBox mode;
    juce::Slider rate, depth, smooth, offset, wave;
    juce::ToggleButton retrigger { "MIDI retrigger" }, snap { "Snap" }, bezier { "Bezier handles" };
    juce::TextButton reset { "Reset" }, tabSingle { "Single" }, tabMulti { "Multiband" };
    juce::TextButton dotBtn { juce::String::fromUTF8("\xE2\x80\xA2") }, tripBtn { "3" }; // dotted / triplet, like Xfer's LFO Tool
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
        if (multibandOn() != shownMb) updateTab();
        else if (!shownMb) rate.updateText();   // cheap: only touches the label if the text really changed
    }

    bool shownMb = false;
};
