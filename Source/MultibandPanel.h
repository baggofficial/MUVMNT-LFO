#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PaperLookAndFeel.h"
#include "EnvelopeEditor.h"
#include "Spectrum.h"
#include "LevelMeter.h"

// Frequency strip: three coloured regions (low / mid / high) with two draggable split points
// on a log-frequency axis. Click a region to edit that band; "S" solos a band.
// Each band's response line is also its volume control: drag it up or down.
// A split is one handle from top to bottom: its line, its slope pill and its frequency tag all drag it, and a click
// on the pill that does not move opens the slope menu. Nothing else sits near a split line.
class BandStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit BandStrip(FreeLfoProcessor&);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    juce::Point<float> volumeHandle(int band) const; // the grip in the middle of a band's line
    int volumeLineAt(juce::Point<float>) const;      // the band whose line is under this point, or -1
    juce::PopupMenu volumeMenu(int band);            // right-click on a line: reset, then the DAW's own entries
    static juce::String volumeText(float db);
    int splitAt(juce::Point<float>) const;           // the split that a press here would grab, or -1
    float splitLineX(int split) const;
    juce::Rectangle<float> slopeBounds(int split) const { return slopeChip(splitLineX(split)); }
    juce::Rectangle<float> frequencyTag(int split) const;
    juce::Rectangle<float> soloBounds(int band) const;

    void setSelected(int band) { selected = band; repaint(); }
    std::function<void(int)> onSelect;
    void animate(); // one step of the editor's animation clock: the live spectrum
    static juce::PopupMenu slopeMenu(int current); // the choices behind a split's slope pill

private:
    int signature() const;
    bool threeBands() const;
    bool dynamicEq() const;
    double splitHz(int split) const;
    float freqToX(double hz) const;
    double xToFreq(float x) const;
    juce::Rectangle<float> slopeChip(float splitX) const;
    void openSlopeMenu(int split);
    void plotArea(float& top, float& bottom) const;
    float yForDb(double db) const;
    float volumeDb(int band) const;
    juce::RangedAudioParameter* volumeParam(int band) const;
    void resetVolume(int band);
    std::function<double(int, double)> response() const; // (band, Hz) -> that band's level in dB, its volume included
    struct Region { int band; float x0, x1; };
    juce::Rectangle<float> soloChip(const Region&) const;
    static bool nameFits(const Region&);
    std::array<Region, 3> regions(int& count) const;

    FreeLfoProcessor& proc;
    int selected = 0;
    int dragHandle = -1; // 0 = low split, 1 = high split
    int hoverSplit = -1;
    float splitGrabOffset = 0.f, splitDownX = 0.f; // grabbing beside the line must not make it jump
    bool splitMoved = false, pressedSlope = false;
    int volumeBand = -1, hoverBand = -1;       // the line being dragged / under the mouse
    float volumeDragDb = 0.f, volumeDragY = 0.f, volumeDownY = 0.f;
    bool volumeGesture = false;                // set once the mouse really moves, so a plain click changes nothing
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
        const auto ctr = getLocalBounds().toFloat().getCentre();
        if (on) // a filled chip says "linked" at a glance
        {
            g.setColour(theme::accent);
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), theme::size::radius);
        }
        auto c = on ? theme::onAccent : theme::sub.withAlpha(isMouseOver() ? 0.9f : 0.6f);
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
    void animate(int stepsPerSpectrum) // one step of the editor's animation clock, for everything that moves in this tab
    {
        // the spectrum stays at 30 a second whatever the window's rate: that is plenty, and its ballistics assume it
        if (++step % (unsigned) stepsPerSpectrum == 0) strip.animate();
        curve.animate();
        curveMeter.animate();
        inMeter.animate();
        outMeter.animate();
    }

public:
    // These two live in the editor's header row (the editor adopts them), so they are public.
    juce::ComboBox bandsBox, engineBox;

private:
    SyncSwitch modeBox;   // Sync / Hz, beside the Rate knob
    LinkButton linkBtn;   // beside the Rate knob: all bands share one rate
    // Every control must be declared before the attachments below: members are destroyed in reverse order, and an
    // attachment lets go of its control as it dies, so the control has to be alive until then.
    LinkButton linkDepthBtn, linkSmoothBtn, linkOffsetBtn;   // sit beside the knobs: all bands follow one value
    void timerCallback() override;
    int signature() const;
    void copyToLinkSource(const char* base, const char* linkId); // turning a link on: the shown band's value goes to every band
    void rebind(); // point the knobs at the selected band's parameters
    void placeChains();

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
    unsigned step = 0;
};
