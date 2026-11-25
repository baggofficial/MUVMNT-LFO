#include "PluginProcessor.h"
#include "PluginEditor.h"

const std::array<const char*, FreeLfoProcessor::numDivisions> FreeLfoProcessor::divisionNames = {
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"
};

const std::array<double, FreeLfoProcessor::numDivisions> FreeLfoProcessor::divisionBeats = {
    32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625
};

namespace
{
    // A band parameter that, while its control is linked, is the one every band follows. The DAW should then call it
    // "All Bands ..." (in automation clips and lists), not after the band it happens to live in.
    struct LinkedName
    {
        juce::String shared;                                          // empty: never linked
        const std::atomic<float>* link = nullptr;                     // the link switch that applies to this control
        const std::atomic<float>* threeBands = nullptr;
        bool middle = false;                                          // linked bands follow Mid (3 bands) or Low (2 bands)
        bool isShared() const
        {
            return link != nullptr && link->load() > 0.5f && (threeBands->load() > 0.5f) == middle;
        }
    };

    template <class Parameter>
    struct BandParameter : Parameter, LinkedName
    {
        using Parameter::Parameter;
        juce::String getName(int maximumLength) const override
        {
            return isShared() ? shared.substring(0, maximumLength) : Parameter::getName(maximumLength);
        }
    };
}

void FreeLfoProcessor::refreshParameterNames()
{
    auto on = [this](const char* id) { return apvts.getRawParameterValue(id)->load() > 0.5f ? 1 : 0; };
    const int links = on("mbLink") | on("linkDepth") << 1 | on("linkSmooth") << 2 | on("linkOffset") << 3;
    const int state = links == 0 ? 0 : links | on("mbBands") << 4; // with nothing linked, the band count changes no names
    if (state == namedState) return;
    namedState = state;
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true));
}

juce::AudioProcessorValueTreeState::ParameterLayout FreeLfoProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "mode", 1 }, "Mode",
                                                 StringArray { "Sync", "Hz" }, 0));

    StringArray divs;
    for (auto* n : divisionNames) divs.add(n);
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "division", 1 }, "Rate", divs, 5)); // 1/4
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "dotted", 1 }, "Dotted", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "triplet", 1 }, "Triplet", false));
    StringArray gridNames;
    for (int n : gridStops) gridNames.add(String(n));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "gridDiv", 1 }, "Grid Lines", gridNames, defaultGridStop)); // curve editor grid + snap

    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "hz", 1 }, "Frequency",
                                                NormalisableRange<float>(0.01f, 40.f, 0.001f, 0.35f), 2.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "depth", 1 }, "Depth",
                                                NormalisableRange<float>(0.f, 1.f, 0.001f), 1.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "smooth", 1 }, "Smooth (ms)",
                                                NormalisableRange<float>(0.f, 200.f, 0.01f, 0.4f), 0.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "offset", 1 }, "Phase Offset",
                                                NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "waveOpacity", 1 }, "Waveform Opacity",
                                                NormalisableRange<float>(0.f, 1.f, 0.01f), 1.f));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "bezier", 1 }, "Bezier Handles", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "retrigger", 1 }, "MIDI Retrigger (Hz mode)", false));

    // ---- Multiband tab ----
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "mb", 1 }, "Multiband", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "bypass", 1 }, "Bypass", false));
    for (const char* id : { "inGain", "outGain" })
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, String(id) == "inGain" ? "Input Gain" : "Output Gain",
                                                    NormalisableRange<float>(-24.f, 24.f, 0.1f), 0.f,
                                                    AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "clip", 1 }, "Output Clipper", StringArray { "Off", "Soft", "Hard" }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "mbEngine", 1 }, "Multiband Type", StringArray { "Crossover", "Dynamic EQ" }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "mbBands", 1 }, "Bands", StringArray { "2 bands", "3 bands" }, 1));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "xLow", 1 }, "Low Split",
                                                NormalisableRange<float>(40.f, 12000.f, 1.f, 0.3f), 200.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "xHigh", 1 }, "High Split",
                                                NormalisableRange<float>(500.f, 12000.f, 1.f, 0.4f), 2500.f));
    for (const char* id : { "slopeLow", "slopeHigh" })
        l.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 },
                                                     String(id) == "slopeLow" ? "Low Split Slope" : "High Split Slope",
                                                     StringArray { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" }, 2));
    // Solo is one switch per band, so any combination can be heard together (e.g. lows + mids)
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "soloL", 1 }, "Solo Low", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "soloM", 1 }, "Solo Mid", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "soloH", 1 }, "Solo High", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "mbLink", 1 }, "Link Rates", true));

    const char* names[3] = { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b)
    {
        const String sfx = bandSuffix(b), nm = names[b];
        l.add(std::make_unique<BandParameter<AudioParameterChoice>>(ParameterID { "mode" + sfx, 1 }, nm + " Mode", StringArray { "Sync", "Hz" }, 0));
        l.add(std::make_unique<BandParameter<AudioParameterChoice>>(ParameterID { "div" + sfx, 1 }, nm + " Rate", divs, 5));
        l.add(std::make_unique<BandParameter<AudioParameterBool>>(ParameterID { "dot" + sfx, 1 }, nm + " Dotted", false));
        l.add(std::make_unique<BandParameter<AudioParameterBool>>(ParameterID { "trip" + sfx, 1 }, nm + " Triplet", false));
        l.add(std::make_unique<BandParameter<AudioParameterFloat>>(ParameterID { "hz" + sfx, 1 }, nm + " Frequency",
                                                    NormalisableRange<float>(0.01f, 40.f, 0.001f, 0.35f), 2.f));
        l.add(std::make_unique<BandParameter<AudioParameterFloat>>(ParameterID { "depth" + sfx, 1 }, nm + " Depth",
                                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 1.f));
        l.add(std::make_unique<BandParameter<AudioParameterFloat>>(ParameterID { "smooth" + sfx, 1 }, nm + " Smooth (ms)",
                                                    NormalisableRange<float>(0.f, 200.f, 0.01f, 0.4f), 0.f));
        l.add(std::make_unique<BandParameter<AudioParameterFloat>>(ParameterID { "offset" + sfx, 1 }, nm + " Phase Offset",
                                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    }
    // Per-knob links: when on, every band follows one band's value for that control
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "linkDepth", 1 }, "Link Depth", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "linkSmooth", 1 }, "Link Smooth", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "linkOffset", 1 }, "Link Offset", false));
    return l;
}

int FreeLfoProcessor::parseDivisionText(const juce::String& text)
{
    auto t = text.trim().toLowerCase().removeCharacters(" ");
    if (t.endsWithChar('d') || t.endsWithChar('t')) t = t.dropLastCharacters(1);
    if (t.containsOnly("0123456789"))            // "8" means 1/8; plain "1" means one bar
        t = t == "1" ? juce::String("1bar") : "1/" + t;
    for (int i = 0; i < numDivisions; ++i)
        if (juce::String(divisionNames[(size_t) i]).toLowerCase().removeCharacters(" ") == t) return i;
    if (t == "1bars") return 3;
    return -1;
}

FreeLfoProcessor::FreeLfoProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "STATE", createLayout())
{
    pMode = apvts.getRawParameterValue("mode");
    pDivision = apvts.getRawParameterValue("division");
    pDotted = apvts.getRawParameterValue("dotted");
    pTriplet = apvts.getRawParameterValue("triplet");
    pHz = apvts.getRawParameterValue("hz");
    pDepth = apvts.getRawParameterValue("depth");
    pSmooth = apvts.getRawParameterValue("smooth");
    pOffset = apvts.getRawParameterValue("offset");
    pRetrigger = apvts.getRawParameterValue("retrigger");

    pMb = apvts.getRawParameterValue("mb");
    pBypass = apvts.getRawParameterValue("bypass");
    pClip = apvts.getRawParameterValue("clip");
    pInGain = apvts.getRawParameterValue("inGain");
    pOutGain = apvts.getRawParameterValue("outGain");
    pEngine = apvts.getRawParameterValue("mbEngine");
    bypassParam = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter("bypass"));
    pBands = apvts.getRawParameterValue("mbBands");
    pXLow = apvts.getRawParameterValue("xLow");
    pXHigh = apvts.getRawParameterValue("xHigh");
    for (int b = 0; b < 3; ++b) pSoloBand[b] = apvts.getRawParameterValue(juce::String("solo") + bandSuffix(b));
    pSlopeLow = apvts.getRawParameterValue("slopeLow");
    pSlopeHigh = apvts.getRawParameterValue("slopeHigh");
    pLink = apvts.getRawParameterValue("mbLink");
    pLinkDepth = apvts.getRawParameterValue("linkDepth");
    pLinkSmooth = apvts.getRawParameterValue("linkSmooth");
    pLinkOffset = apvts.getRawParameterValue("linkOffset");
    // which parameters turn into "All Bands ..." when their control is linked
    {
        struct Control { const char* id; const char* name; const char* linkId; };
        const Control controls[] = { { "mode", "Mode", "mbLink" }, { "div", "Rate", "mbLink" }, { "dot", "Dotted", "mbLink" },
                                     { "trip", "Triplet", "mbLink" }, { "hz", "Frequency", "mbLink" }, { "depth", "Depth", "linkDepth" },
                                     { "smooth", "Smooth (ms)", "linkSmooth" }, { "offset", "Phase Offset", "linkOffset" } };
        for (const auto& control : controls)
            for (int b = 0; b < 2; ++b) // only Low and Mid can be the one that is followed
                if (auto* linked = dynamic_cast<LinkedName*>(apvts.getParameter(juce::String(control.id) + bandSuffix(b))))
                {
                    linked->shared = juce::String("All Bands ") + control.name;
                    linked->link = apvts.getRawParameterValue(control.linkId);
                    linked->threeBands = pBands;
                    linked->middle = b == 1;
                }
    }
    startTimerHz(10); // tells the DAW when a link (or the band count) changes what a parameter is called

    for (int b = 0; b < 3; ++b)
    {
        const juce::String sfx = bandSuffix(b);
        band[b] = { apvts.getRawParameterValue("mode" + sfx), apvts.getRawParameterValue("div" + sfx),
                    apvts.getRawParameterValue("hz" + sfx), apvts.getRawParameterValue("depth" + sfx),
                    apvts.getRawParameterValue("smooth" + sfx),
                    apvts.getRawParameterValue("dot" + sfx), apvts.getRawParameterValue("trip" + sfx),
                    apvts.getRawParameterValue("offset" + sfx) };
        bandCycleSeconds[(size_t) b].store(0.5);
    }

    setEnvelope(Envelope()); // default pump shape
    // multiband defaults: the same pump in every band, recovering slowest in the lows and fastest in the highs
    for (int b = 0; b < 3; ++b)
    {
        Envelope e;
        e.resetToBandPump(b);
        setEnvelope(e, b + 1);
    }
}

bool FreeLfoProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (out != layouts.getMainInputChannelSet()) return false;

    auto sc = layouts.getChannelSet(true, 1); // optional sidechain
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

// Every number the audio code takes from a setting goes through this. A NaN or infinity (typed text, damaged project
// data, a misbehaving host) would otherwise stick in the smoothers for good, or turn the LFO phase into a table index
// far outside the table and crash the whole DAW.
static inline float readParam(const std::atomic<float>* p, float fallback) noexcept
{
    const float v = p->load(std::memory_order_relaxed);
    return std::isfinite(v) ? v : fallback;
}

void FreeLfoProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto* g : { &inGain, &outGain }) g->reset(sampleRate, 0.020);
    inGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(readParam(pInGain, 0.f)));
    outGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(readParam(pOutGain, 0.f)));
    dryScratch.setSize(2, juce::jmax(samplesPerBlock, 8192));
    bypassMix = pBypass != nullptr && pBypass->load() > 0.5f ? 1.f : 0.f;
    zcrCoef = 1.f - std::exp(-1.f / (0.025f * (float) sampleRate));
    zcrSmooth = zcEnv = 0.f; zcState = 0;
    c150 = 1.f - std::exp(-juce::MathConstants<float>::twoPi * 150.f / (float) sampleRate);
    c1500 = 1.f - std::exp(-juce::MathConstants<float>::twoPi * 1500.f / (float) sampleRate);
    eCoef = 1.f - std::exp(-1.f / (0.025f * (float) sampleRate));
    lp150 = lp1500 = eLow = eHigh = 0.f;
    for (auto& m : waveMix) m.store(0.f, std::memory_order_relaxed);
    for (auto& h : waveHue) h.store(-1.f, std::memory_order_relaxed);
    freePhase = 0.0;
    smoothedGain = 1.f;
    for (auto& sl : slots) sl.audioVersion = -1;
    crossover.prepare(sampleRate);
    dynEq.reset(); scDynEq.reset();
    scCrossover.prepare(sampleRate);
    for (int b = 0; b < 3; ++b) { bandFreePhase[b] = 0.0; bandSmoothed[b] = 1.f; bandLastBin[b] = -1; laneSigValid[b] = false; laneDeclick[b] = 0; gate[b] = 1.f; }
    structureInit = false; fadeOutLeft = fadeInLeft = 0;
    singleSigValid = false; singleDeclick = 0;
    expectedPpqValid = false; fcLowS = fcHighS = -1.0;
}

Envelope FreeLfoProcessor::getEnvelope(int slot) const
{
    const auto& sl = slots[juce::jlimit(0, numSlots - 1, slot)];
    const juce::SpinLock::ScopedLockType lock(sl.lock);
    return sl.env;
}

void FreeLfoProcessor::setEnvelope(const Envelope& e, int slot)
{
    auto& sl = slots[juce::jlimit(0, numSlots - 1, slot)];
    Envelope copy = e;
    copy.normalize();
    const juce::SpinLock::ScopedLockType lock(sl.lock);
    sl.env = copy;
    sl.env.bake(sl.shared.data());
    sl.version.fetch_add(1, std::memory_order_release);
}

FreeLfoProcessor::UndoState FreeLfoProcessor::captureUndoState() const
{
    UndoState s;
    for (int i = 0; i < numSlots; ++i) s.env[(size_t) i] = getEnvelope(i);
    s.bezier = apvts.getRawParameterValue("bezier")->load() > 0.5f;
    return s;
}

void FreeLfoProcessor::pushUndo(UndoState before)
{
    undoStack.push_back(std::move(before));
    while (undoStack.size() > maxUndo) undoStack.pop_front();
    redoStack.clear();
}

static void setSwitch(juce::AudioProcessorValueTreeState& apvts, const char* id, bool on)
{
    if (auto* prm = apvts.getParameter(id))
        if ((prm->getValue() > 0.5f) != on)
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(on ? 1.f : 0.f);
            prm->endChangeGesture();
        }
}

void FreeLfoProcessor::applyUndoState(const UndoState& s)
{
    if (s.bands >= 0) setSwitch(apvts, "mbBands", s.bands == 1);
    restoringUndo = true; // setting the Bezier switch must not convert the curves a second time
    if (auto* prm = apvts.getParameter("bezier"))
        if ((prm->getValue() > 0.5f) != s.bezier)
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(s.bezier ? 1.f : 0.f);
            prm->endChangeGesture();
        }
    for (int i = 0; i < numSlots; ++i) setEnvelope(s.env[(size_t) i], i);
    restoringUndo = false;
}

void FreeLfoProcessor::applyCurve(Envelope e, int slot)
{
    if (apvts.getRawParameterValue("bezier")->load() > 0.5f) e.convertToBezier(); else e = e.simplifiedCopy();
    pushUndo(captureUndoState());
    setEnvelope(e, slot);
}

void FreeLfoProcessor::applyBandCurves(const std::array<Envelope, 3>& curves, bool threeBands)
{
    auto before = captureUndoState();
    before.bands = apvts.getRawParameterValue("mbBands")->load() > 0.5f ? 1 : 0;
    pushUndo(std::move(before));
    const bool bezier = apvts.getRawParameterValue("bezier")->load() > 0.5f;
    for (int b = 0; b < 3; ++b)
    {
        if (b == 1 && !threeBands) continue;
        Envelope e = curves[(size_t) b];
        if (bezier) e.convertToBezier(); else e = e.simplifiedCopy();
        setEnvelope(e, b + 1);
    }
    setSwitch(apvts, "mbBands", threeBands);
}

kickfit::Result FreeLfoProcessor::fitCurveToKick(int slot, kickfit::Style style)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    kickfit::Result result;
    if (! sidechainActive.load()) { result.status = kickfit::Status::noSidechain; return result; }
    if (scFresh[(size_t) slot].load() < waveBins - 8) { result.status = kickfit::Status::notReady; return result; }

    // the loudest |sample| of each slice, for one slot
    using Bins = std::array<std::atomic<float>, waveBins>;
    auto loudestPerSlice = [](const Bins& mn, const Bins& mx) {
        std::vector<float> peaks((size_t) waveBins);
        for (size_t i = 0; i < peaks.size(); ++i)
            peaks[i] = juce::jmax(std::abs(mn[i].load(std::memory_order_relaxed)), std::abs(mx[i].load(std::memory_order_relaxed)));
        return peaks;
    };
    auto peaksOf = [&](int which) {
        return loudestPerSlice(which > 0 ? bandScMin[(size_t) which - 1] : scMin, which > 0 ? bandScMax[(size_t) which - 1] : scMax);
    };
    const auto peaks = peaksOf(slot);
    // the sound being ducked, as it came in, over the same cycle (the band's own part of it in Multiband)
    const auto sound = loudestPerSlice(slot > 0 ? bwMin[(size_t) slot - 1] : waveMin, slot > 0 ? bwMax[(size_t) slot - 1] : waveMax);

    if (slot > 0) // the outline is scaled to its own peak, so a band with only a faint trace of the kick must be caught here
    {
        auto loudest = [](const std::vector<float>& v) { return *std::max_element(v.begin(), v.end()); };
        const bool three = apvts.getRawParameterValue("mbBands")->load() > 0.5f;
        float overall = 0.f;
        for (int other = 1; other <= 3; ++other)
            if (three || other != 2) overall = juce::jmax(overall, loudest(peaksOf(other)));
        if (overall >= kickfit::silence && loudest(peaks) < 0.1f * overall) // more than 20 dB under the strongest band
        {
            result.status = kickfit::Status::bandQuiet;
            return result;
        }
    }

    const double cycle = slot > 0 ? bandCycleSeconds[(size_t) slot - 1].load() : cycleSeconds.load();
    result = kickfit::analyse(peaks, cycle, style, sound);
    if (result.status == kickfit::Status::ok) applyCurve(result.curve, slot);
    return result;
}

bool FreeLfoProcessor::undo()
{
    if (undoStack.empty()) return false;
    const auto s = std::move(undoStack.back());
    undoStack.pop_back();
    auto current = captureUndoState();
    if (s.bands >= 0) current.bands = apvts.getRawParameterValue("mbBands")->load() > 0.5f ? 1 : 0;
    redoStack.push_back(std::move(current));
    applyUndoState(s);
    return true;
}

bool FreeLfoProcessor::redo()
{
    if (redoStack.empty()) return false;
    const auto s = std::move(redoStack.back());
    redoStack.pop_back();
    auto current = captureUndoState();
    if (s.bands >= 0) current.bands = apvts.getRawParameterValue("mbBands")->load() > 0.5f ? 1 : 0;
    undoStack.push_back(std::move(current));
    applyUndoState(s);
    return true;
}

void FreeLfoProcessor::convertCurveMode(bool toBezier)
{
    if (restoringUndo) return;
    { auto before = captureUndoState(); before.bezier = !toBezier; pushUndo(std::move(before)); }
    for (int slot = 0; slot < numSlots; ++slot)
    {
        Envelope e = getEnvelope(slot);
        if (toBezier) e.convertToBezier(); else e.convertToSimple();
        setEnvelope(e, slot);
    }
}

// Audio thread: pull in a newly baked table if one is available. Never blocks.
void FreeLfoProcessor::syncTable(int slot)
{
    auto& sl = slots[slot];
    const int v = sl.version.load(std::memory_order_acquire);
    if (v == sl.audioVersion) return;
    const juce::SpinLock::ScopedTryLockType lock(sl.lock);
    if (lock.isLocked())
    {
        sl.audio = sl.shared;
        sl.audioVersion = v;
    }
}

// One-pole smoothing step that always arrives exactly: a plain one-pole stalls a few ulps short of its target
// (the increment rounds to nothing), which would leave a "100%" gain at 0.999993 and break the perfect null
static inline float smoothStep(float y, float target, float c)
{
    const float next = c * y + (1.f - c) * target;
    return (next == y || std::fabs(next - target) < 1e-7f) ? target : next;
}

// Bypass: the audio passes untouched. Switching crossfades dry and processed over 10 ms, so it never clicks; while fully
// bypassed nothing is processed (no CPU).
void FreeLfoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;   // also covers the crossfade arithmetic below
    const float target = pBypass->load() > 0.5f ? 1.f : 0.f;
    const int n = buffer.getNumSamples();
    auto mainBuf = getBusBuffer(buffer, false, 0);
    const int nch = mainBuf.getNumChannels();

    if (bypassMix >= 1.f && target >= 1.f)
    {
        if (editorOpen.load(std::memory_order_relaxed) && nch > 0 && n > 0)   // keep the IN / OUT meters honest
        {
            float pk = 0.f;
            for (int ch = 0; ch < nch; ++ch) pk = juce::jmax(pk, mainBuf.getMagnitude(ch, 0, n));
            const float fall = std::pow(10.f, -80.f * ((float) n / (float) getSampleRate()) / 20.f);
            for (auto* m : { &meterIn, &meterOut }) m->store(juce::jmax(m->load(std::memory_order_relaxed) * fall, pk), std::memory_order_relaxed);
        }
        return;
    }
    if (bypassMix <= 0.f && target <= 0.f) { processActive(buffer, midi); return; }

    if (n > dryScratch.getNumSamples() || nch > dryScratch.getNumChannels())   // a host block larger than promised: switch at once
    {
        bypassMix = target;
        if (target < 1.f) processActive(buffer, midi);
        return;
    }
    for (int ch = 0; ch < nch; ++ch) dryScratch.copyFrom(ch, 0, mainBuf, ch, 0, n);
    processActive(buffer, midi);
    const float step = 1.f / juce::jmax(1.f, 0.010f * (float) getSampleRate());
    float m = bypassMix;
    for (int i = 0; i < n; ++i)
    {
        m = m < target ? juce::jmin(target, m + step) : juce::jmax(target, m - step);
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* wet = mainBuf.getWritePointer(ch);
            wet[i] = wet[i] * (1.f - m) + dryScratch.getReadPointer(ch)[i] * m;
        }
    }
    bypassMix = m;
}

void FreeLfoProcessor::processActive(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    for (int sl = 0; sl < numSlots; ++sl) syncTable(sl);

    const int numSamples = buffer.getNumSamples();
    // The host buffer holds main + sidechain channels; only touch the main bus.
    auto mainBuf = getBusBuffer(buffer, false, 0);
    const int numChannels = mainBuf.getNumChannels();
    const double sr = getSampleRate();
    const int clipMode = pClip != nullptr ? juce::roundToInt(readParam(pClip, 0.f)) : 0;

    // IN trim first, so the meters, the displays and the processing all see the trimmed signal.
    // At 0 dB (and settled) the audio is not touched at all, which keeps the null bit-exact.
    inGain.setTargetValue(juce::Decibels::decibelsToGain(readParam(pInGain, 0.f)));
    outGain.setTargetValue(juce::Decibels::decibelsToGain(readParam(pOutGain, 0.f)));
    if (inGain.isSmoothing() || inGain.getTargetValue() != 1.f) inGain.applyGain(mainBuf, numSamples);

    const float* scData[2] = { nullptr, nullptr };
    int scChannels = 0;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled())
    {
        auto sc = getBusBuffer(buffer, true, 1);
        scChannels = juce::jmin(2, sc.getNumChannels());
        for (int c = 0; c < scChannels; ++c) scData[c] = sc.getReadPointer(c);
    }
    sidechainActive.store(scChannels > 0, std::memory_order_relaxed);
    // slices that are not being recorded right now go stale
    for (int sl = 0; sl < numSlots; ++sl)
        if (scChannels == 0 || ! editorOpen.load(std::memory_order_relaxed) || (sl > 0) != (pMb->load() > 0.5f))
            scFresh[(size_t) sl].store(0, std::memory_order_relaxed);

    // peak meters and the live spectrum only run while the editor is open
    const bool watching = editorOpen.load(std::memory_order_relaxed) && numChannels > 0;
    auto meterUpdate = [&](std::atomic<float>& m, float peak) {
        const float fall = std::pow(10.f, -80.f * ((float) numSamples / (float) sr) / 20.f);   // 80 dB per second: follows loud, pumping material
        m.store(juce::jmax(m.load(std::memory_order_relaxed) * fall, peak), std::memory_order_relaxed);
    };
    if (watching)
    {
        float pk = 0.f;
        for (int ch = 0; ch < numChannels; ++ch) pk = juce::jmax(pk, mainBuf.getMagnitude(ch, 0, numSamples));
        meterUpdate(meterIn, pk);
    }

    // live spectrum (multiband strip): keep the newest input samples, before any processing
    int specStart = -1;
    if (watching && pMb->load() > 0.5f)
    {
        int w = specPos.load(std::memory_order_relaxed);
        specStart = w;
        for (int i = 0; i < numSamples; ++i)
        {
            float m = 0.f;
            for (int ch = 0; ch < numChannels; ++ch) m += mainBuf.getReadPointer(ch)[i];
            specFifo[(size_t) w].store(m / (float) numChannels, std::memory_order_relaxed);
            w = (w + 1) % specSize;
        }
        specPos.store(w, std::memory_order_release);
    }

    HostInfo host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) host.bpm = *b;
            if (auto p = pos->getPpqPosition()) { host.ppq = *p; host.havePpq = true; }
            host.playing = pos->getIsPlaying();
        }
    // the host's numbers get the same distrust as the settings
    if (! (std::isfinite(host.bpm) && host.bpm >= 1.0 && host.bpm <= 10000.0)) host.bpm = HostInfo{}.bpm;
    if (! (std::isfinite(host.ppq) && std::fabs(host.ppq) < 1e12)) { host.ppq = 0.0; host.havePpq = false; }

    Structure live;
    live.multiband = pMb->load() > 0.5f;
    live.dynEq = pEngine->load() > 0.5f;
    live.three = pBands->load() > 0.5f;
    live.slopeLow = juce::jlimit(0, numSlopes - 1, (int) readParam(pSlopeLow, 2.f));
    live.slopeHigh = juce::jlimit(0, numSlopes - 1, (int) readParam(pSlopeHigh, 2.f));
    if (!structureInit) { applied = live; structureInit = true; }

    const double ppqPerSample = host.bpm / 60.0 / sr;
    const int fadeN = juce::jmax(8, (int) (fadeMs * 0.001 * sr));
    const float* scAt[2] = { nullptr, nullptr };

    // Chop the block into pieces wherever the signal path has to change, so a structural switch is a quick
    // dip (old path out, new path in) instead of a click. Normally this is a single piece.
    int pos = 0;
    while (pos < numSamples)
    {
        if (fadeOutLeft == 0 && fadeInLeft == 0 && !(applied == live)) fadeOutLeft = fadeN;

        int seg = numSamples - pos;
        float g0 = 1.f, g1 = 1.f;
        if (fadeOutLeft > 0)
        {
            seg = juce::jmin(seg, fadeOutLeft);
            g0 = (float) fadeOutLeft / (float) fadeN;
            g1 = (float) (fadeOutLeft - seg) / (float) fadeN;
        }
        else if (fadeInLeft > 0)
        {
            seg = juce::jmin(seg, fadeInLeft);
            g0 = 1.f - (float) fadeInLeft / (float) fadeN;
            g1 = 1.f - (float) (fadeInLeft - seg) / (float) fadeN;
        }

        juce::AudioBuffer<float> view(mainBuf.getArrayOfWritePointers(), numChannels, pos, seg);
        HostInfo h = host;
        h.ppq = host.ppq + (double) pos * ppqPerSample;
        for (int c = 0; c < scChannels; ++c) scAt[c] = scData[c] + pos;

        if (applied.multiband && applied.dynEq) processMultiband<true>(view, h, scAt, scChannels);
        else if (applied.multiband) processMultiband<false>(view, h, scAt, scChannels);
        else processSingle(view, h, midi, pos, scAt, scChannels);

        if (g0 != 1.f || g1 != 1.f)
            for (int ch = 0; ch < numChannels; ++ch) view.applyGainRamp(ch, 0, seg, g0, g1);

        // OUT trim, ahead of the clipper so the clipper's 0 dB ceiling still holds
        if (outGain.isSmoothing() || outGain.getTargetValue() != 1.f) outGain.applyGain(view, seg);

        // Optional output clipper (multiband only: re-summing the bands can push the peaks past the input's)
        if (clipMode != 0 && applied.multiband)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* d = view.getWritePointer(ch);
                if (clipMode == 2)
                    for (int i = 0; i < seg; ++i) d[i] = juce::jlimit(-1.f, 1.f, d[i]);
                else   // soft: linear up to 0.8, then a tanh shoulder that approaches 1.0 smoothly
                    for (int i = 0; i < seg; ++i)
                    {
                        const float a = std::abs(d[i]);
                        if (a > 0.8f) d[i] = std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) * 5.f), d[i]);
                    }
            }

        if (fadeOutLeft > 0)
        {
            fadeOutLeft -= seg;
            if (fadeOutLeft == 0) // silent now: switch paths
            {
                if (live.multiband && (!applied.multiband || applied.dynEq != live.dynEq || applied.slopeLow != live.slopeLow || applied.slopeHigh != live.slopeHigh))
                {
                    crossover.reset();   // a different filter structure can't inherit the old state; nobody can hear it
                    dynEq.reset(); scDynEq.reset();
                    fcLowS = fcHighS = -1.0;
                }
                applied = live;
                singleSigValid = false;
                for (auto& v : laneSigValid) v = false;
                expectedPpqValid = false;
                fadeInLeft = fadeN;
            }
        }
        else if (fadeInLeft > 0)
            fadeInLeft -= seg;

        pos += seg;
    }

    if (watching)
    {
        float pk = 0.f;
        for (int ch = 0; ch < numChannels; ++ch) pk = juce::jmax(pk, mainBuf.getMagnitude(ch, 0, numSamples));
        meterUpdate(meterOut, pk);
        if (specStart >= 0) // the same stretch of samples, after the plugin (for the before / after spectrum)
            for (int i = 0; i < numSamples; ++i)
            {
                float m = 0.f;
                for (int ch = 0; ch < numChannels; ++ch) m += mainBuf.getReadPointer(ch)[i];
                specFifoOut[(size_t) ((specStart + i) % specSize)].store(m / (float) numChannels, std::memory_order_relaxed);
            }
    }

    uiPhaseTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    blockCounter.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------------
// Single band: one curve, one LFO, volume only.
// ---------------------------------------------------------------------------------------------
void FreeLfoProcessor::processSingle(juce::AudioBuffer<float>& mainBuf, const HostInfo& h, const juce::MidiBuffer& midi, int midiOffset,
                                     const float* const* scData, int scChannels)
{
    const auto& audioTable = slots[0].audio;
    const int numSamples = mainBuf.getNumSamples();
    const int numChannels = mainBuf.getNumChannels();
    if (numSamples == 0) return;
    const double sr = getSampleRate();
    const double bpm = h.bpm, ppq = h.ppq;

    const bool syncMode = pMode->load() < 0.5f;
    const double beats = divisionBeats[(size_t) juce::jlimit(0, numDivisions - 1, (int) readParam(pDivision, 5.f))]
                         * rateModifier(pDotted->load() > 0.5f, pTriplet->load() > 0.5f);
    const double offset = readParam(pOffset, 0.f);
    const float depth = readParam(pDepth, 1.f);
    const double hz = readParam(pHz, 2.f);

    const float smoothMs = readParam(pSmooth, 0.f);
    const float coef = smoothMs < 0.01f ? 0.f : std::exp(-1.f / (smoothMs * 0.001f * (float) sr));

    // First MIDI note-on inside this piece of the block (for Hz-mode retrigger)
    int retrigAt = -1;
    if (pRetrigger->load() > 0.5f && !syncMode)
        for (const auto m : midi)
        {
            const int rel = m.samplePosition - midiOffset;
            if (rel >= 0 && rel < numSamples && m.getMessage().isNoteOn()) { retrigAt = rel; break; }
        }

    cycleSeconds.store(syncMode ? beats * 60.0 / juce::jmax(1.0, bpm) : 1.0 / juce::jmax(0.001, hz), std::memory_order_relaxed);
    const double freeInc = (syncMode ? (bpm / 60.0) / beats : hz) / sr;
    const bool useHost = syncMode && h.playing && h.havePpq;
    const double ppqPerSample = bpm / 60.0 / sr;

    // Declick: something that makes the LFO value jump (new rate, mode, offset, depth, the transport moving, a
    // retrigger) smooths the gain for a few ms, even with Smooth at 0
    const float declickCoef = std::exp(-1.f / (0.003f * (float) sr));
    const int declickLen = (int) (0.016 * sr);
    LaneSig cur;
    cur.division = readParam(pDivision, 5.f); cur.depth = depth; cur.offset = (float) offset;
    cur.dotted = pDotted->load() > 0.5f; cur.triplet = pTriplet->load() > 0.5f; cur.sync = syncMode; cur.useHost = useHost;
    const bool relocated = useHost && expectedPpqValid && std::fabs(ppq - expectedPpq) > 5e-4;
    if (!singleSigValid) singleSigValid = true;
    else if (!(cur == singleSig) || relocated) singleDeclick = declickLen;
    singleSig = cur;
    int declickLeft = singleDeclick;

    // Phase is carried as one running value and wrapped by subtraction, instead of being
    // rebuilt (division + floor) for every sample.
    auto fract = [](double v) { return v - std::floor(v); };
    double p = useHost ? fract(ppq / beats + offset) : fract(freePhase + offset);
    const double inc = useHost ? ppqPerSample / beats : freeInc;
    const bool analyse = editorOpen.load(std::memory_order_relaxed); // waveform display only matters while the UI is open

    constexpr int chunk = 1024; // stack scratch, so no allocation on the audio thread
    float gain[chunk];
    int bins[chunk];

    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = juce::jmin(chunk, numSamples - start);

        // pass 1: the gain curve for this chunk
        for (int i = 0; i < n; ++i)
        {
            if (start + i == retrigAt) { p = fract(offset); declickLeft = declickLen; } // a MIDI restart glides in from its own sample

            const double pos = p * Envelope::tableSize;
            const int ti = (int) pos;
            const float frac = (float) (pos - ti);
            const float v = audioTable[(size_t) ti] + (audioTable[(size_t) ti + 1] - audioTable[(size_t) ti]) * frac;

            const float target = 1.f - depth * (1.f - v);
            // the extra smoothing eases out over the declick period, so it never ends with a step
            float c = coef;
            if (declickLeft > 0) { c = coef + (declickCoef - coef) * ((float) declickLeft / (float) declickLen); --declickLeft; }
            smoothedGain = smoothStep(smoothedGain, target, c); // lands exactly: gain 1 stays a perfect null
            gain[i] = smoothedGain;
            bins[i] = juce::jmin(waveBins - 1, (int) (p * waveBins));

            p += inc;
            if (p >= 1.0) p -= std::floor(p);
        }

        // pass 2 (editor open only): fold the pre-gain input onto the LFO cycle for display
        if (analyse)
        {
            for (int i = 0; i < n; ++i)
            {
                float mono = 0.f;
                for (int ch = 0; ch < numChannels; ++ch) mono += mainBuf.getReadPointer(ch)[start + i];
                if (numChannels > 0) mono /= (float) numChannels;

                float scMono = 0.f;
                for (int c = 0; c < scChannels; ++c) scMono += scData[c][start + i];
                if (scChannels > 0) scMono /= (float) scChannels;

                const int bin = bins[i];
                const bool isNew = bin != lastWaveBin;
                auto track = [bin, isNew](auto& mn, auto& mx, float v) {
                    auto& lo = mn[(size_t) bin];
                    auto& hi = mx[(size_t) bin];
                    if (isNew) { lo.store(v, std::memory_order_relaxed); hi.store(v, std::memory_order_relaxed); return; }
                    if (v < lo.load(std::memory_order_relaxed)) lo.store(v, std::memory_order_relaxed);
                    if (v > hi.load(std::memory_order_relaxed)) hi.store(v, std::memory_order_relaxed);
                };
                // output = input x gain. At the wrap: the level at the last point of the cycle, and the range over the end zone
                const float outNow = mono * gain[i];
                if (isNew && lastWaveBin >= 0 && bin < lastWaveBin)
                {
                    pushWrap(0, lastOut, endLo > endHi ? lastOut : endLo, endLo > endHi ? lastOut : endHi);
                    endLo = 1e9f; endHi = -1e9f;
                }
                if (bin >= (int) ((1.f - wrapWindow) * waveBins)) { endLo = juce::jmin(endLo, outNow); endHi = juce::jmax(endHi, outNow); }
                lastOut = outNow;
                track(waveMin, waveMax, mono);
                if (scChannels > 0) track(scMin, scMax, scMono);
                if (scChannels > 0 && isNew) noteSidechainSlice(0, cycleSeconds.load(std::memory_order_relaxed), (float) offset);

                // Rough pitch for the rainbow: zero crossings per sample (with hysteresis against noise), averaged over ~25 ms.
                // A sine of f Hz crosses zero 2f/sr times per sample.
                {
                    const int st = mono > 0.004f ? 1 : mono < -0.004f ? -1 : zcState;
                    const float crossed = (st != zcState && zcState != 0) ? 1.f : 0.f;
                    zcState = st;
                    zcrSmooth += zcrCoef * (crossed - zcrSmooth);
                    zcEnv = juce::jmax(std::abs(mono), zcEnv * 0.9995f);
                    // bass and highs at once (energy under 150 Hz vs. above 1.5 kHz): the scope blends that toward pink / purple
                    lp150 += c150 * (mono - lp150);
                    lp1500 += c1500 * (mono - lp1500);
                    eLow += eCoef * (std::abs(lp150) - eLow);
                    eHigh += eCoef * (std::abs(mono - lp1500) - eHigh);
                    if (isNew && zcEnv > 0.01f)   // the colour only needs updating when the position enters a new bin
                    {
                        const float mixNow = juce::jlimit(0.f, 1.f, 2.f * juce::jmin(eLow, eHigh) / (eLow + eHigh + 1e-6f));
                        auto& mixA = waveMix[(size_t) bin];
                        mixA.store(mixA.load(std::memory_order_relaxed) * 0.5f + mixNow * 0.5f, std::memory_order_relaxed);
                        const float hz = juce::jlimit(40.f, 16000.f, zcrSmooth * 0.5f * (float) sr);
                        const float t = std::log(hz * (1.f / 40.f)) * (1.f / 5.9914645f);   // ln(16000 / 40)
                        auto& hue = waveHue[(size_t) bin];
                        const float old = hue.load(std::memory_order_relaxed);
                        hue.store(old < 0.f ? t : old + (t - old) * 0.5f, std::memory_order_relaxed);
                    }
                }
                lastWaveBin = bin;
            }
        }

        // pass 3: apply the gain, vectorised, per channel
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::multiply(mainBuf.getWritePointer(ch) + start, gain, n);
    }
    singleDeclick = declickLeft;

    // free-running phase (fallback when the host isn't playing, and the Hz-mode oscillator)
    {
        const int from = retrigAt >= 0 ? retrigAt : 0;
        double fp = retrigAt >= 0 ? 0.0 : freePhase;
        fp += (numSamples - from) * freeInc;
        freePhase = fract(fp);
    }
    expectedPpqValid = useHost;
    expectedPpq = ppq + numSamples * ppqPerSample;

    uiPhase.store((float) p, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------------
// Multiband: split the signal into low / mid / high (or low / high), give every band its own
// curve and its own LFO rate, then add the bands back together.
// ---------------------------------------------------------------------------------------------
template <bool useDynamicEq>
void FreeLfoProcessor::processMultiband(juce::AudioBuffer<float>& buf, const HostInfo& h, const float* const* scData, int scChannels)
{
    const int n = buf.getNumSamples();
    if (n == 0) return;
    const int numCh = juce::jmin(2, buf.getNumChannels());
    const double sr = getSampleRate();
    const bool three = applied.three;
    const bool link = pLink->load() > 0.5f;
    const int rateSource = three ? 1 : 0; // when linked, every band follows this band's rate settings
    const bool linkDepth = pLinkDepth->load() > 0.5f, linkSmooth = pLinkSmooth->load() > 0.5f, linkOffset = pLinkOffset->load() > 0.5f;
    const bool soloOn[3] = { pSoloBand[0]->load() > 0.5f, three && pSoloBand[1]->load() > 0.5f, pSoloBand[2]->load() > 0.5f };
    const bool anySolo = soloOn[0] || soloOn[1] || soloOn[2]; // soloed bands play, the others are muted
    const bool analyse = editorOpen.load(std::memory_order_relaxed);
    const double bpm = h.bpm, ppq = h.ppq;

    // Crossover: the structure is fixed (see processBlock); the split frequencies glide toward the knobs
    // (about 20 ms, in the log domain) so a jump of a knob never turns into a burst
    const bool scOn = analyse && scChannels > 0;   // the kick (sidechain) is split like the audio, for the overlap display
    if constexpr (! useDynamicEq)
    {
        crossover.setSlopes((Slope) applied.slopeLow, (Slope) applied.slopeHigh);
        if (scOn) scCrossover.setSlopes((Slope) applied.slopeLow, (Slope) applied.slopeHigh);
    }
    const double targetLow = three ? juce::jmin(1000.0, (double) readParam(pXLow, 200.f)) : (double) readParam(pXLow, 200.f), // 3 bands: low split tops out at 1 kHz
                 targetHigh = readParam(pXHigh, 2500.f);
    if (fcLowS < 0.0) { fcLowS = targetLow; fcHighS = targetHigh; }
    auto tune = [&]() {
        if constexpr (useDynamicEq)
        {
            dynEq.setFrequencies(fcLowS, fcHighS, sr);
            if (scOn) scDynEq.setFrequencies(fcLowS, fcHighS, sr);
        }
        else
        {
            crossover.setFrequencies(fcLowS, fcHighS);
            if (scOn) scCrossover.setFrequencies(fcLowS, fcHighS);
        }
    };
    tune();
    constexpr int glideStep = 32; // while a knob is moving, the filters are retuned every 32 samples
    const double glideA = 1.0 - std::exp(-(double) glideStep / (0.030 * sr));
    auto glide = [&]() {
        if (fcLowS == targetLow && fcHighS == targetHigh) return;
        fcLowS = std::exp(std::log(fcLowS) + (std::log(targetLow) - std::log(fcLowS)) * glideA);
        fcHighS = std::exp(std::log(fcHighS) + (std::log(targetHigh) - std::log(fcHighS)) * glideA);
        if (std::fabs(fcLowS / targetLow - 1.0) < 1e-4) fcLowS = targetLow;
        if (std::fabs(fcHighS / targetHigh - 1.0) < 1e-4) fcHighS = targetHigh;
        tune();
    };

    auto fract = [](double v) { return v - std::floor(v); };
    const double ppqPerSample = bpm / 60.0 / sr;
    const float declickCoef = std::exp(-1.f / (0.003f * (float) sr));
    const int declickLen = (int) (0.016 * sr);
    const bool anyHost = h.playing && h.havePpq;
    const bool relocated = anyHost && expectedPpqValid && std::fabs(ppq - expectedPpq) > 5e-4;

    struct Lane { double p, inc, freeInc; float depth, coef; bool active; int declick; };
    Lane lane[3];
    for (int b = 0; b < 3; ++b)
    {
        const auto& rate = band[link ? rateSource : b];
        const bool syncMode = rate.mode->load() < 0.5f;
        const double beats = divisionBeats[(size_t) juce::jlimit(0, numDivisions - 1, (int) readParam(rate.division, 5.f))]
                             * rateModifier(rate.dotted->load() > 0.5f, rate.triplet->load() > 0.5f);
        const double hz = readParam(rate.hz, 2.f);
        const bool useHost = syncMode && anyHost;

        auto& L = lane[b];
        L.freeInc = (syncMode ? (bpm / 60.0) / beats : hz) / sr;
        L.inc = useHost ? ppqPerSample / beats : L.freeInc;
        const double off = readParam(band[linkOffset ? rateSource : b].offset, 0.f);   // each band has its own phase offset (unless linked)
        L.p = fract((useHost ? ppq / beats : bandFreePhase[b]) + off);
        L.depth = readParam(band[linkDepth ? rateSource : b].depth, 1.f);
        const float ms = readParam(band[linkSmooth ? rateSource : b].smooth, 0.f);
        L.coef = ms < 0.01f ? 0.f : std::exp(-1.f / (ms * 0.001f * (float) sr));
        L.active = three || b != 1;
        L.declick = laneDeclick[b];
        bandCycleSeconds[(size_t) b].store(syncMode ? beats * 60.0 / juce::jmax(1.0, bpm) : 1.0 / juce::jmax(0.001, hz),
                                           std::memory_order_relaxed);

        LaneSig cur;
        cur.division = readParam(rate.division, 5.f); cur.depth = L.depth; cur.offset = (float) off;
        cur.dotted = rate.dotted->load() > 0.5f; cur.triplet = rate.triplet->load() > 0.5f;
        cur.sync = syncMode; cur.useHost = useHost; cur.link = link; cur.three = three;
        if (!laneSigValid[b]) laneSigValid[b] = true;
        else if (!(cur == laneSig[b]) || (useHost && relocated)) L.declick = declickLen;
        laneSig[b] = cur;
    }

    float gateTarget[3];
    for (int b = 0; b < 3; ++b) gateTarget[b] = (!anySolo || soloOn[b]) ? 1.f : 0.f;

    float* data[2] = { buf.getWritePointer(0), numCh > 1 ? buf.getWritePointer(1) : nullptr };
    bool bad = false;

    for (int s = 0; s < n; ++s)
    {
        if (s % glideStep == glideStep - 1) glide();
        float g[3] = { 0.f, 0.f, 0.f };
        int bin[3] = { 0, 0, 0 };
        for (int b = 0; b < 3; ++b)
        {
            auto& L = lane[b];
            if (!L.active) continue;
            const auto& table = slots[b + 1].audio;

            const double pos = L.p * Envelope::tableSize;
            const int ti = (int) pos;
            const float frac = (float) (pos - ti);
            const float v = table[(size_t) ti] + (table[(size_t) ti + 1] - table[(size_t) ti]) * frac;

            const float target = 1.f - L.depth * (1.f - v);
            float c = L.coef;
            if (L.declick > 0) { c = L.coef + (declickCoef - L.coef) * ((float) L.declick / (float) declickLen); --L.declick; }
            bandSmoothed[b] = smoothStep(bandSmoothed[b], target, c);

            // solo / mute changes fade over a few ms instead of cutting
            gate[b] = smoothStep(gate[b], gateTarget[b], declickCoef);
            g[b] = bandSmoothed[b] * gate[b];
            bin[b] = juce::jmin(waveBins - 1, (int) (L.p * waveBins));

            L.p += L.inc;
            if (L.p >= 1.0) L.p -= std::floor(L.p);
        }

        double avg[3] = { 0.0, 0.0, 0.0 };
        [[maybe_unused]] DynamicEq::Mix eqMix {};
        if constexpr (useDynamicEq) eqMix = DynamicEq::mix(three, g[0], g[1], g[2]);
        for (int ch = 0; ch < numCh; ++ch)
        {
            double x = data[ch][s];
            if (!(std::fabs(x) < 1e9)) x = 0.0; // NaN / Inf / absurd input must never get into the filters
            double bands[3];
            float o;
            if constexpr (useDynamicEq)
                o = (float) dynEq.process(ch, x, three, eqMix, analyse ? bands : nullptr);
            else
            {
                if (three) crossover.process3(ch, x, bands);
                else crossover.process2(ch, x, bands);
                o = (float) (bands[0] * g[0] + bands[1] * g[1] + bands[2] * g[2]);
            }
            if (!(std::fabs(o) < 1e6f)) { o = 0.f; bad = true; } // the filters have gone unstable: start them afresh
            data[ch][s] = o;
            if (analyse) { avg[0] += bands[0]; avg[1] += bands[1]; avg[2] += bands[2]; }
        }

        double sb[3] = { 0.0, 0.0, 0.0 };
        if (scOn)
        {
            double x = 0.0;
            for (int c = 0; c < scChannels; ++c) x += scData[c][s];
            x /= (double) scChannels;
            if (!(std::fabs(x) < 1e9)) x = 0.0;
            if constexpr (useDynamicEq) scDynEq.process(0, x, three, DynamicEq::Mix {}, sb);
            else { if (three) scCrossover.process3(0, x, sb); else scCrossover.process2(0, x, sb); }
        }

        if (analyse)
            for (int b = 0; b < 3; ++b)
            {
                if (!lane[b].active) continue;
                const float v = (float) (avg[b] / numCh);
                const bool isNew = bin[b] != bandLastBin[b];
                const float outNow = v * g[b];
                if (isNew && bandLastBin[b] >= 0 && bin[b] < bandLastBin[b])
                {
                    pushWrap(b + 1, bandLastOut[b], bandEndLo[b] > bandEndHi[b] ? bandLastOut[b] : bandEndLo[b],
                             bandEndLo[b] > bandEndHi[b] ? bandLastOut[b] : bandEndHi[b]);
                    bandEndLo[b] = 1e9f; bandEndHi[b] = -1e9f;
                }
                if (bin[b] >= (int) ((1.f - wrapWindow) * waveBins)) { bandEndLo[b] = juce::jmin(bandEndLo[b], outNow); bandEndHi[b] = juce::jmax(bandEndHi[b], outNow); }
                bandLastOut[b] = outNow;
                if (scOn)
                {
                    if (isNew) noteSidechainSlice(b + 1, bandCycleSeconds[(size_t) b].load(std::memory_order_relaxed), laneSig[b].offset);
                    auto& klo = bandScMin[(size_t) b][(size_t) bin[b]];
                    auto& khi = bandScMax[(size_t) b][(size_t) bin[b]];
                    const float kv = (float) sb[b];
                    if (isNew) { klo.store(kv, std::memory_order_relaxed); khi.store(kv, std::memory_order_relaxed); }
                    else
                    {
                        if (kv < klo.load(std::memory_order_relaxed)) klo.store(kv, std::memory_order_relaxed);
                        if (kv > khi.load(std::memory_order_relaxed)) khi.store(kv, std::memory_order_relaxed);
                    }
                }
                auto& lo = bwMin[(size_t) b][(size_t) bin[b]];
                auto& hi = bwMax[(size_t) b][(size_t) bin[b]];
                if (isNew) { lo.store(v, std::memory_order_relaxed); hi.store(v, std::memory_order_relaxed); }
                else
                {
                    if (v < lo.load(std::memory_order_relaxed)) lo.store(v, std::memory_order_relaxed);
                    if (v > hi.load(std::memory_order_relaxed)) hi.store(v, std::memory_order_relaxed);
                }
                bandLastBin[b] = bin[b];
            }
    }
    if (bad) { crossover.reset(); dynEq.reset(); }

    for (int b = 0; b < 3; ++b)
    {
        laneDeclick[b] = lane[b].declick;
        bandFreePhase[b] = fract(bandFreePhase[b] + n * lane[b].freeInc);
        bandPhase[(size_t) b].store((float) lane[b].p, std::memory_order_relaxed);
    }
    expectedPpqValid = anyHost;
    expectedPpq = ppq + n * ppqPerSample;
}

void FreeLfoProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("envelope", juce::String(getEnvelope().serialize()), nullptr);
    state.setProperty("envLow", juce::String(getEnvelope(1).serialize()), nullptr);
    state.setProperty("envMid", juce::String(getEnvelope(2).serialize()), nullptr);
    state.setProperty("envHigh", juce::String(getEnvelope(3).serialize()), nullptr);
    state.setProperty("endZone", showEndZone.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, dest);
}

void FreeLfoProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid())
        {
            apvts.replaceState(state);
            for (auto* prm : getParameters()) // a damaged project must not leave "not a number" settings behind
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(prm))
                    if (! std::isfinite(apvts.getRawParameterValue(ranged->paramID)->load()))
                        ranged->setValueNotifyingHost(ranged->getDefaultValue());
            showEndZone.store(state.hasProperty("endZone") && (bool) state["endZone"]);
            if (state.hasProperty("envelope"))
                setEnvelope(Envelope::deserialize(state["envelope"].toString().toStdString()));
            const char* keys[3] = { "envLow", "envMid", "envHigh" };
            for (int b = 0; b < 3; ++b)
                if (state.hasProperty(keys[b]))
                {
                    Envelope fallback; // for damaged data: this band's own default
                    fallback.resetToBandPump(b);
                    setEnvelope(Envelope::deserialize(state[keys[b]].toString().toStdString(), fallback), b + 1);
                }
        }
    }
}

juce::AudioProcessorEditor* FreeLfoProcessor::createEditor() { return new FreeLfoEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FreeLfoProcessor(); }
