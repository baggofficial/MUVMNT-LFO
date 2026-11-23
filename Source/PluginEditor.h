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
    juce::Label rateL, depthL, smoothL, offsetL, waveL;

    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> modeA;
    std::unique_ptr<SA> rateA, depthA, smoothA, offsetA, waveA;
    std::unique_ptr<BA> retrigA, bezierA;

    bool multibandOn() const { return proc.apvts.getRawParameterValue("mb")->load() > 0.5f; }
    void bindRate();  // one knob: snaps to note values in Sync mode, free Hz otherwise
    void updateTab(); // show the Single or the Multiband tab
    void setMultiband(bool on);
    void timerCallback() override { if (multibandOn() != shownMb) updateTab(); } // automation / project load changed "mb"

    bool shownMb = false;
};
