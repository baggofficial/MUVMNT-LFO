#pragma once
#include <deque>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Envelope.h"
#include "Crossover.h"
#include "DynamicEq.h"
#include "KickToCurve.h"

class FreeLfoProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    FreeLfoProcessor();
    ~FreeLfoProcessor() override { stopTimer(); }
    // Linked controls rename their parameter to "All Bands ...": this tells the DAW if that has changed (message thread)
    void refreshParameterNames();

    static constexpr float minBandDb = -12.f, maxBandDb = 6.f; // the range of a Multiband band's volume
    // what shows as 0.0 dB is a gain of exactly 1, whatever rounding the host's 0..1 value went through
    static float volumeGain(float db) { return std::abs(db) < 0.05f ? 1.f : juce::Decibels::decibelsToGain(db); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }   // the host's bypass button drives this too

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
    int envelopeVersion(int slot) const { return slots[(size_t) juce::jlimit(0, numSlots - 1, slot)].version.load(std::memory_order_acquire); }   // bumps on every change: lets the editor skip copying an unchanged curve
    void setEnvelope(const Envelope& e, int slot = 0);
    // Bezier handles <-> simple bend, applied to every curve (single + the three bands) when the user flips the switch
    void convertCurveMode(bool toBezier);
    // Undo / redo for the curves (message thread only). One entry is a snapshot of all four curves plus the
    // Bezier switch, so an entry costs well under 2 KB; the history is capped at maxUndo entries (about 100 KB).
    struct UndoState
    {
        std::array<Envelope, numSlots> env;
        bool bezier = true;
        int bands = -1; // 0 = two bands, 1 = three; -1 = this step leaves the band count alone
    };
    static constexpr size_t maxUndo = 50;
    UndoState captureUndoState() const;
    void pushUndo(UndoState before);          // call just before a change; a new change clears the redo history
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    size_t undoDepth() const { return undoStack.size(); }
    // Replace one curve with a preset / pasted curve: converted to the current style (Bezier handles or simple
    // bend), recorded for undo.
    void applyCurve(Envelope e, int slot);
    // Replace every band's curve at once (a multiband preset) and switch to its band count: one undo step for both.
    // With two bands only low and high are replaced.
    void applyBandCurves(const std::array<Envelope, 3>& curves, bool threeBands);
    // "Curve from kick" (message thread): builds a starting curve for `slot` from the last cycle of the sidechain signal
    // (precise: and of the sound being ducked) and applies it as one undo step. Anything but ok leaves every curve untouched.
    kickfit::Result fitCurveToKick(int slot, kickfit::Style style = kickfit::Style::safe);
    bool undo();
    bool redo();
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
    // How many sidechain slices have been recorded in a row at the current rate and offset, per curve slot
    // (up to waveBins = one whole cycle): "curve from kick" must not read leftovers from an earlier setting
    std::array<std::atomic<int>, 4> scFresh {};
    // Multiband: the sidechain signal split by a copy of the crossover, so each band can be compared with the same part of it.
    std::array<std::array<std::atomic<float>, waveBins>, 3> bandScMin {}, bandScMax {};
    std::atomic<double> cycleSeconds { 0.5 }; // length of one LFO cycle, for the editor's smoothing preview
    std::atomic<bool> editorOpen { false }; // set by the editor; the waveform analysis only runs while it is true
    // Rough pitch of the input at each phase bin (0 = low ... 1 = high, on a log axis; -1 = not measured yet).
    // The single-tab scope uses it to colour the waveform like a rainbow.
    std::array<std::atomic<float>, waveBins> waveHue {};
    // The OUTPUT level (-1..+1, input x the gain the plugin applies) at the last point of the cycle, for the newest 8 cycles: slot 0 = single tab, 1..3 = bands.
    // Shown by the thin meter beside the curve: a bass note that is away from 0 there will click when the curve drops.
    static constexpr int wrapHistory = 8;
    static constexpr float wrapWindow = 0.04f;   // the "end zone": the last 4% of the cycle, where the curve usually drops
    std::array<std::array<std::atomic<float>, wrapHistory>, 4> wrapVal {};              // output at the very last point
    std::array<std::array<std::atomic<float>, wrapHistory>, 4> wrapLo {}, wrapHi {};   // lowest / highest output in the end zone
    std::array<std::atomic<int>, 4> wrapCount {};
    void pushWrap(int slot, float v, float lo, float hi)
    {
        const int n = wrapCount[(size_t) slot].load(std::memory_order_relaxed);
        const size_t i = (size_t) (n % wrapHistory);
        wrapVal[(size_t) slot][i].store(v, std::memory_order_relaxed);
        wrapLo[(size_t) slot][i].store(lo, std::memory_order_relaxed);
        wrapHi[(size_t) slot][i].store(hi, std::memory_order_relaxed);
        wrapCount[(size_t) slot].store(n + 1, std::memory_order_release);
    }
    // How much bass and highs are present together (0..1); the scope tints that blend pink / purple.
    std::array<std::atomic<float>, waveBins> waveMix {};
    // The newest input samples (mono), for the live spectrum in the multiband strip. Only filled while the editor is open.
    static constexpr int specSize = 4096;
    std::array<std::atomic<float>, specSize> specFifo {};
    std::atomic<int> specPos { 0 };
    std::array<std::atomic<float>, specSize> specFifoOut {};   // the same samples after the plugin: the output spectrum
    // Peak meters (linear, 0..1+): input and output, with a fast fall (80 dB/s) applied here so the UI can just read them.
    std::atomic<float> meterIn { 0.f }, meterOut { 0.f };
    // Counts the blocks with a peak above 0 dBFS. The window may look too rarely to catch a short one in the falling
    // meter value, but it cannot miss a count that went up.
    std::atomic<unsigned> oversIn { 0 }, oversOut { 0 };
    static constexpr float overLevel = 1.00001f; // above 0 dBFS (the Hard clipper's exact 1.0 is not over)
    std::atomic<bool> showEndZone { false };   // the end-zone bar beside the end-of-cycle arrow (click the arrow meter to toggle); saved with the project

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
    // Typed text -> division index ("1/8", "8", "2 bars", "1 bar", optional trailing D / T); -1 if it is not a division
    static int parseDivisionText(const juce::String& text);
    static const std::array<double, numDivisions> divisionBeats; // quarter-note beats per LFO cycle
    static double rateModifier(bool dotted, bool triplet) { return dotted ? 1.5 : triplet ? 2.0 / 3.0 : 1.0; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override { refreshParameterNames(); }
    int namedState = 0; // the link settings the DAW was last told about; 0 = nothing linked
    void syncTable(int slot);
    double scKeyCycle[4] = {};   // audio thread: the cycle length and offset the sidechain slices were recorded with
    float scKeyOffset[4] = {};
    void noteSidechainSlice(int slot, double cycle, float offset)   // once per new slice
    {
        auto& fresh = scFresh[(size_t) slot];
        int count = fresh.load(std::memory_order_relaxed);
        if (std::fabs(cycle - scKeyCycle[slot]) > 0.005 * cycle || offset != scKeyOffset[slot])
        {
            scKeyCycle[slot] = cycle;
            scKeyOffset[slot] = offset;
            count = 0;
        }
        if (count < waveBins) ++count;
        fresh.store(count, std::memory_order_relaxed);
    }
    struct HostInfo { double bpm = 120.0, ppq = 0.0; bool havePpq = false, playing = false; };
    void processSingle(juce::AudioBuffer<float>& main, const HostInfo& host, const juce::MidiBuffer& midi, int midiOffset,
                       const float* const* scData, int scChannels);
    template <bool useDynamicEq> // two compiled copies, so the crossover path carries nothing of the other one
    void processMultiband(juce::AudioBuffer<float>& buf, const HostInfo& host, const float* const* scData, int scChannels);

    // Structure = things that rebuild the signal path. Changing them while playing would click, so the output dips
    // to silence for a few ms around the switch (old path fades out, new path fades in).
    struct Structure
    {
        bool dynEq = false; bool multiband = false, three = true;
        int slopeLow = 2, slopeHigh = 2;
        bool operator==(const Structure& o) const { return multiband == o.multiband && three == o.three && slopeLow == o.slopeLow && slopeHigh == o.slopeHigh && dynEq == o.dynEq; }
    };
    static constexpr double fadeMs = 4.0; // length of each half of the dip around a structural switch
    Structure applied;
    bool structureInit = false;
    int fadeOutLeft = 0, fadeInLeft = 0;

    // Anything that makes the LFO value jump (rate, mode, offset, depth, transport jump...) starts a short extra
    // smoothing, so changes never click even with Smooth at 0.
    struct LaneSig
    {
        float division = -1.f, depth = -1.f, offset = 0.f;
        bool dotted = false, triplet = false, sync = true, useHost = false, link = false, three = true;
        bool operator==(const LaneSig& o) const
        {
            return division == o.division && depth == o.depth && offset == o.offset && dotted == o.dotted && triplet == o.triplet
                   && sync == o.sync && useHost == o.useHost && link == o.link && three == o.three;
        }
    };
    LaneSig singleSig, laneSig[3];
    bool singleSigValid = false, laneSigValid[3] = { false, false, false };
    int singleDeclick = 0, laneDeclick[3] = { 0, 0, 0 };
    double expectedPpq = 0.0;     // where the transport should be if nobody moved it
    bool expectedPpqValid = false;
    float gate[3] = { 1.f, 1.f, 1.f }; // smoothed solo / band on-off gates
    double fcLowS = -1.0, fcHighS = -1.0; // crossover frequencies, gliding toward the knobs

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

    struct BandCtl { std::atomic<float> *mode, *division, *hz, *depth, *smooth, *dotted, *triplet, *offset; };
    BandCtl band[3] {};
    void processActive(juce::AudioBuffer<float>&, juce::MidiBuffer&);   // the real processing (processBlock adds the bypass crossfade)
    juce::RangedAudioParameter* bypassParam = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pClip = nullptr;
    std::atomic<float>* pInGain = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    juce::SmoothedValue<float> inGain { 1.f }, outGain { 1.f };   // the IN / OUT trims, glided so turning them never clicks
    std::atomic<float>* pBandGain[3] = { nullptr, nullptr, nullptr };
    juce::SmoothedValue<float> bandVolume[3] { 1.f, 1.f, 1.f };   // the Multiband volumes, glided the same way
    std::atomic<float>* pEngine = nullptr;
    float bypassMix = 0.f;                // 0 = processed, 1 = dry; ramps over 10 ms so bypass never clicks
    juce::AudioBuffer<float> dryScratch;  // the dry copy during a bypass crossfade (sized in prepareToPlay)
    std::atomic<float> *pMb = nullptr, *pBands = nullptr, *pXLow = nullptr, *pXHigh = nullptr,
                       *pLink = nullptr, *pLinkDepth = nullptr, *pLinkSmooth = nullptr, *pLinkOffset = nullptr, *pSlopeLow = nullptr, *pSlopeHigh = nullptr;

    std::deque<UndoState> undoStack, redoStack;
    bool restoringUndo = false;
    void applyUndoState(const UndoState&);

    std::atomic<float>* pSoloBand[3] = { nullptr, nullptr, nullptr };
    Crossover crossover;
    DynamicEq dynEq, scDynEq;   // the zero-latency alternative to the crossover (and its copy for the sidechain display)
    Crossover scCrossover;   // the same split for the sidechain signal (display only, runs while the editor is open)
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
    // zero-crossing pitch tracker for the rainbow scope
    float endLo = 1e9f, endHi = -1e9f, lastOut = 0.f;                       // end-zone tracking (single tab)
    float bandEndLo[3] = { 1e9f, 1e9f, 1e9f }, bandEndHi[3] = { -1e9f, -1e9f, -1e9f }, bandLastOut[3] = {};
    float zcrSmooth = 0.f, zcrCoef = 0.f, zcEnv = 0.f;
    float lp150 = 0.f, lp1500 = 0.f, eLow = 0.f, eHigh = 0.f, c150 = 0.f, c1500 = 0.f, eCoef = 0.f;
    int zcState = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeLfoProcessor)
};
