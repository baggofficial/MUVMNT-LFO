#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Envelope.h"
#include "Crossover.h"

class FreeLfoProcessor : public juce::AudioProcessor
{
public:
    FreeLfoProcessor();
    ~FreeLfoProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Free LFO Tool"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Message-thread API for the editor
    // slot 0 = the single-band curve; slots 1..3 = the low / mid / high band curves (Multiband tab)
    static constexpr int numSlots = 4;
    Envelope getEnvelope(int slot = 0) const;
    void setEnvelope(const Envelope& e, int slot = 0);
    // Bezier handles <-> simple bend, applied to every curve (single + the three bands) when the user flips the switch
    void convertCurveMode(bool toBezier);
    static const char* bandSuffix(int band) { return band == 0 ? "L" : band == 1 ? "M" : "H"; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> uiPhase { 0.f };          // LFO phase at the end of the last block
    std::atomic<double> uiPhaseTimeMs { 0.0 };   // when it was published (for the editor to extrapolate)
    std::atomic<unsigned> blockCounter { 0 };    // bumps every block: tells the editor audio is flowing

    // Input signal min/max per LFO-phase bin (audio thread writes, UI reads).
    // One LFO cycle is split into waveBins slots, so the display lines up with the curve.
    static constexpr int waveBins = 512;
    std::array<std::atomic<float>, waveBins> waveMin {}, waveMax {};
    // Same, for the optional sidechain input (e.g. the kick that triggers the pump).
    std::array<std::atomic<float>, waveBins> scMin {}, scMax {};
    std::atomic<bool> sidechainActive { false };
    std::atomic<double> cycleSeconds { 0.5 }; // length of one LFO cycle, for the editor's smoothing preview
    std::atomic<bool> editorOpen { false }; // set by the editor; the waveform analysis only runs while it is true
    // Peak level per band per phase bin (low < 250 Hz, mid, high > 3 kHz) for the coloured waveform.
    std::array<std::atomic<float>, waveBins> bandLow {}, bandMid {}, bandHigh {};

    // Multiband tab: each band's own input signal folded onto that band's own LFO cycle.
    std::array<std::array<std::atomic<float>, waveBins>, 3> bwMin {}, bwMax {};
    std::array<std::atomic<float>, 3> bandPhase {};
    std::array<std::atomic<double>, 3> bandCycleSeconds {};

    // Straight note values only, slowest to fastest, so turning the knob always speeds the LFO up. Dotted (x1.5 longer)
    // and triplet (x2/3) are separate switches that modify whatever value is selected.
    // Grid line counts the draggable grid number steps through (handy musical subdivisions: straight and triplet-friendly)
    static constexpr std::array<int, 12> gridStops { 2, 3, 4, 6, 8, 12, 16, 24, 32, 36, 48, 64 };
    static constexpr int defaultGridStop = 6; // index of 16

    static constexpr int numDivisions = 10;
    static const std::array<const char*, numDivisions> divisionNames;
    static const std::array<double, numDivisions> divisionBeats; // quarter-note beats per LFO cycle
    static double rateModifier(bool dotted, bool triplet) { return dotted ? 1.5 : triplet ? 2.0 / 3.0 : 1.0; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void syncTable(int slot);
    void processMultiband(juce::AudioBuffer<float>& buf, double bpm, double ppq, bool havePpq, bool playing);

    // One baked curve per slot. The UI edits on the message thread; the audio thread only
    // ever copies a finished table with a try-lock, so it never waits.
    struct EnvSlot
    {
        Envelope env;
        mutable juce::SpinLock lock;
        std::array<float, Envelope::tableSize + 1> shared {}, audio {};
        std::atomic<int> version { 0 };
        int audioVersion = -1;
    };
    EnvSlot slots[numSlots];

    struct BandCtl { std::atomic<float> *mode, *division, *hz, *depth, *smooth, *dotted, *triplet; };
    BandCtl band[3] {};
    std::atomic<float> *pMb = nullptr, *pBands = nullptr, *pXLow = nullptr, *pXHigh = nullptr,
                       *pLink = nullptr, *pSlopeLow = nullptr, *pSlopeHigh = nullptr;

    std::atomic<float>* pSoloBand[3] = { nullptr, nullptr, nullptr };
    Crossover crossover;
    double bandFreePhase[3] = { 0, 0, 0 };
    float bandSmoothed[3] = { 1.f, 1.f, 1.f };
    int bandLastBin[3] = { -1, -1, -1 };

    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pDivision = nullptr;
    std::atomic<float>* pDotted = nullptr;
    std::atomic<float>* pTriplet = nullptr;
    std::atomic<float>* pHz = nullptr;
    std::atomic<float>* pDepth = nullptr;
    std::atomic<float>* pSmooth = nullptr;
    std::atomic<float>* pOffset = nullptr;
    std::atomic<float>* pRetrigger = nullptr;

    double freePhase = 0.0;
    float smoothedGain = 1.f;
    int lastWaveBin = -1;
    float lpLow = 0.f, lpHigh = 0.f, coefLow = 0.f, coefHigh = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeLfoProcessor)
};
