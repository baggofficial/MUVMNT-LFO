#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "EnvelopeEditor.h"
#include "Spectrum.h"
#include "LevelMeter.h"

// Frequency strip: three coloured regions (low / mid / high) with two draggable split points
// on a log-frequency axis. Click a region to edit that band; "S" solos a band.
class BandStrip : public juce::Component
{
public:
    explicit BandStrip(FreeLfoProcessor&);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    void setSelected(int band) { selected = band; repaint(); }
    std::function<void(int)> onSelect;
    void animate(); // one step of the editor's animation clock: the live spectrum

private:
    int signature() const;
    bool threeBands() const;
    bool dynamicEq() const;
    double splitHz(int split) const;
    float freqToX(double hz) const;
    double xToFreq(float x) const;
    juce::Rectangle<float> soloChip(float regionRight) const;
    juce::Rectangle<float> slopeChip(float splitX) const;
    void openSlopeMenu(int split);
    void plotArea(float& top, float& bottom) const;
    struct Region { int band; float x0, x1; };
    std::array<Region, 3> regions(int& count) const;

    FreeLfoProcessor& proc;
    int selected = 0;
    int dragHandle = -1; // 0 = low split, 1 = high split
    int lastSig = -1;
    unsigned lastBlock = 0;
    spectrum::Analyser analyser;                       // preallocated: the 30 Hz update allocates nothing
    std::vector<float> specSamples, specTmpDb;
    std::vector<float> specDb, specDbOut;   // smoothed live spectrum (dB per FFT bin): input, and output after the plugin
    bool specLive = false;          // something is still visible
    void updateSpectrum();
    void drawSpectrum(juce::Graphics&, float plotTop, float plotBottom) const;
    juce::Path spectrumPath(const std::vector<float>& db, float plotTop, float plotBottom, juce::Path* fill) const;
};

// The whole Multiband tab: strip, one curve editor (for the selected band) and that band's controls.
// A chain-link icon toggle: blue when on, grey when off.
class LinkButton : public juce::ToggleButton
{
public:
    LinkButton() { setClickingTogglesState(true); setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics& g) override
    {
        const bool on = getToggleState();
        auto c = on ? paper::accent : paper::sub.withAlpha(isMouseOver() ? 0.9f : 0.6f);
        const auto ctr = getLocalBounds().toFloat().getCentre();
        g.setColour(c);
        for (int s = -1; s <= 1; s += 2)
        {
            juce::Path link;
            link.addRoundedRectangle(-5.f, -2.7f, 10.f, 5.4f, 2.7f);
            g.strokePath(link, juce::PathStrokeType(1.5f),
                         juce::AffineTransform::rotation(-juce::MathConstants<float>::pi / 4.f)
                             .translated(ctr.x + (float) s * 1.9f, ctr.y - (float) s * 1.9f));
        }
    }
};

class MultibandPanel : public juce::Component, private juce::Timer
{
public:
    explicit MultibandPanel(FreeLfoProcessor&);
    ~MultibandPanel() override { stopTimer(); }

    void resized() override;
    int currentSlot() const { return curve.getSlot(); }
    void resetCurve() { curve.resetCurve(); }
    void selectBand(int band); // which band's curve is edited (also used by the headless snapshot tool)
    void setSnap(bool on) { curve.snapToGrid = on; }
    void showNote(const juce::String& text, double ms) { curve.showNote(text, ms); }
    juce::String currentNote() const { return curve.currentNote(); }
    juce::Rectangle<int> waveKnobBounds() const { return wave.getBounds(); }
    void animate() // one step of the editor's animation clock, for everything that moves in this tab
    {
        strip.animate();
        curve.animate();
        curveMeter.animate();
        inMeter.animate();
        outMeter.animate();
    }

public:
    // These two live in the editor's header row (the editor adopts them), so they are public.
    juce::ComboBox modeBox, bandsBox, engineBox;

private:
    LinkButton linkBtn;   // beside the Rate knob: all bands share one rate
    // Every control must be declared before the attachments below: members are destroyed in reverse order, and an
    // attachment lets go of its control as it dies, so the control has to be alive until then.
    LinkButton linkDepthBtn, linkSmoothBtn, linkOffsetBtn;   // sit beside the knobs: all bands follow one value
    void timerCallback() override;
    int signature() const;
    void copyToLinkSource(const char* base, const char* linkId); // turning a link on: the shown band's value goes to every band
    void rebind(); // point the knobs at the selected band's parameters

    FreeLfoProcessor& proc;
    BandStrip strip;
    EnvelopeEditor curve;
    WrapMeter curveMeter { proc, curve };
    LevelMeter inMeter { proc, false, "IN" }, outMeter { proc, true, "OUT" };

    juce::TextButton dotBtn { juce::String::fromUTF8("\xE2\x80\xA2") }, tripBtn { "3" };
    Knob rate, depth, smooth, offset, wave;
    juce::Label rateL, depthL, smoothL, offsetL, waveL;

    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> modeA, bandsA, engineA;
    std::unique_ptr<SA> rateA, depthA, smoothA, offsetA, waveA;
    std::unique_ptr<BA> linkA, dotA, tripA, linkDepthA, linkSmoothA, linkOffsetA;

    int selected = 0;
    int lastSig = -1;
};
