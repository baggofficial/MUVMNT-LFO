#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// The curve editor: double-click empty space = add point, drag = move,
// right-click point = delete, drag the small diamond on a segment = bend it.
class EnvelopeEditor : public juce::Component, private juce::Timer
{
public:
    explicit EnvelopeEditor(FreeLfoProcessor&);
    ~EnvelopeEditor() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    bool snapToGrid = true;
    int gridX = 16, gridY = 4;

private:
    void timerCallback() override
    {
        // Pick up changes made outside the editor (reset button, preset/state load)
        if (dragPoint < 0 && dragHandle < 0)
            env = proc.getEnvelope();
        repaint();
    }

    juce::Point<float> toScreen(float x, float y) const;
    juce::Point<float> toNorm(juce::Point<float> p) const;
    int hitPoint(juce::Point<float> p) const;
    int hitHandle(juce::Point<float> p) const;
    juce::Point<float> handlePos(int segment) const;
    void commit();

    FreeLfoProcessor& proc;
    Envelope env;
    int dragPoint = -1, dragHandle = -1;
    float lastY = 0.f;
};

class FreeLfoEditor : public juce::AudioProcessorEditor
{
public:
    explicit FreeLfoEditor(FreeLfoProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FreeLfoProcessor& proc;
    EnvelopeEditor curve;

    juce::ComboBox mode, division;
    juce::Slider hz, depth, smooth, offset, wave;
    juce::ToggleButton retrigger { "MIDI retrigger" }, snap { "Snap" };
    juce::TextButton reset { "Reset curve" };
    juce::Label hzL, depthL, smoothL, offsetL, waveL;

    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> modeA, divA;
    std::unique_ptr<SA> hzA, depthA, smoothA, offsetA, waveA;
    std::unique_ptr<BA> retrigA;

    void updateModeVisibility();
};
