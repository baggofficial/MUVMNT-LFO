#include "PluginProcessor.h"
#include "PluginEditor.h"

const std::array<const char*, FreeLfoProcessor::numDivisions> FreeLfoProcessor::divisionNames = {
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"
};

const std::array<double, FreeLfoProcessor::numDivisions> FreeLfoProcessor::divisionBeats = {
    32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625
};

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
                                                NormalisableRange<float>(0.f, 50.f, 0.01f, 0.5f), 0.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "offset", 1 }, "Phase Offset",
                                                NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "waveOpacity", 1 }, "Waveform Opacity",
                                                NormalisableRange<float>(0.f, 1.f, 0.01f), 0.35f));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "bezier", 1 }, "Bezier Handles", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "retrigger", 1 }, "MIDI Retrigger (Hz mode)", false));

    // ---- Multiband tab ----
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "mb", 1 }, "Multiband", false));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "mbBands", 1 }, "Bands", StringArray { "2 bands", "3 bands" }, 1));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "xLow", 1 }, "Low Split",
                                                NormalisableRange<float>(40.f, 1000.f, 1.f, 0.4f), 200.f));
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
        l.add(std::make_unique<AudioParameterChoice>(ParameterID { "mode" + sfx, 1 }, nm + " Mode", StringArray { "Sync", "Hz" }, 0));
        l.add(std::make_unique<AudioParameterChoice>(ParameterID { "div" + sfx, 1 }, nm + " Rate", divs, 5));
        l.add(std::make_unique<AudioParameterBool>(ParameterID { "dot" + sfx, 1 }, nm + " Dotted", false));
        l.add(std::make_unique<AudioParameterBool>(ParameterID { "trip" + sfx, 1 }, nm + " Triplet", false));
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { "hz" + sfx, 1 }, nm + " Frequency",
                                                    NormalisableRange<float>(0.01f, 40.f, 0.001f, 0.35f), 2.f));
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { "depth" + sfx, 1 }, nm + " Depth",
                                                    NormalisableRange<float>(0.f, 1.f, 0.001f), 1.f));
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { "smooth" + sfx, 1 }, nm + " Smooth (ms)",
                                                    NormalisableRange<float>(0.f, 50.f, 0.01f, 0.5f), 0.f));
    }
    return l;
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
    pBands = apvts.getRawParameterValue("mbBands");
    pXLow = apvts.getRawParameterValue("xLow");
    pXHigh = apvts.getRawParameterValue("xHigh");
    for (int b = 0; b < 3; ++b) pSoloBand[b] = apvts.getRawParameterValue(juce::String("solo") + bandSuffix(b));
    pSlopeLow = apvts.getRawParameterValue("slopeLow");
    pSlopeHigh = apvts.getRawParameterValue("slopeHigh");
    pLink = apvts.getRawParameterValue("mbLink");
    for (int b = 0; b < 3; ++b)
    {
        const juce::String sfx = bandSuffix(b);
        band[b] = { apvts.getRawParameterValue("mode" + sfx), apvts.getRawParameterValue("div" + sfx),
                    apvts.getRawParameterValue("hz" + sfx), apvts.getRawParameterValue("depth" + sfx),
                    apvts.getRawParameterValue("smooth" + sfx),
                    apvts.getRawParameterValue("dot" + sfx), apvts.getRawParameterValue("trip" + sfx) };
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

void FreeLfoProcessor::prepareToPlay(double sampleRate, int)
{
    coefLow = 1.f - std::exp(-juce::MathConstants<float>::twoPi * 250.f / (float) sampleRate);
    coefHigh = 1.f - std::exp(-juce::MathConstants<float>::twoPi * 3000.f / (float) sampleRate);
    lpLow = lpHigh = 0.f;
    freePhase = 0.0;
    smoothedGain = 1.f;
    for (auto& sl : slots) sl.audioVersion = -1;
    crossover.prepare(sampleRate);
    for (int b = 0; b < 3; ++b) { bandFreePhase[b] = 0.0; bandSmoothed[b] = 1.f; bandLastBin[b] = -1; }
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

void FreeLfoProcessor::convertCurveMode(bool toBezier)
{
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

void FreeLfoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    syncTable(0);
    const auto& audioTable = slots[0].audio;

    const int numSamples = buffer.getNumSamples();
    // The host buffer holds main + sidechain channels; only touch the main bus.
    auto mainBuf = getBusBuffer(buffer, false, 0);
    const int numChannels = mainBuf.getNumChannels();
    const double sr = getSampleRate();

    const float* scData[2] = { nullptr, nullptr };
    int scChannels = 0;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled())
    {
        auto sc = getBusBuffer(buffer, true, 1);
        scChannels = juce::jmin(2, sc.getNumChannels());
        for (int c = 0; c < scChannels; ++c) scData[c] = sc.getReadPointer(c);
    }
    sidechainActive.store(scChannels > 0, std::memory_order_relaxed);

    double bpm = 120.0;
    double ppq = 0.0;
    bool havePpq = false, playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) { ppq = *p; havePpq = true; }
            playing = pos->getIsPlaying();
        }

    // Multiband tab: a separate path, so the single-band tab keeps its bit-exact pass-through.
    if (pMb->load() > 0.5f)
    {
        for (int sl = 1; sl < numSlots; ++sl) syncTable(sl);
        processMultiband(mainBuf, bpm, ppq, havePpq, playing);
        uiPhaseTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
        blockCounter.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const bool syncMode = pMode->load() < 0.5f;
    const double beats = divisionBeats[(size_t) juce::jlimit(0, numDivisions - 1, (int) pDivision->load())]
                         * rateModifier(pDotted->load() > 0.5f, pTriplet->load() > 0.5f);
    const double offset = pOffset->load();
    const float depth = pDepth->load();
    const double hz = pHz->load();

    const float smoothMs = pSmooth->load();
    const float coef = smoothMs < 0.01f ? 0.f : std::exp(-1.f / (smoothMs * 0.001f * (float) sr));

    // First MIDI note-on in this block (for Hz-mode retrigger)
    int retrigAt = -1;
    if (pRetrigger->load() > 0.5f && !syncMode)
        for (const auto m : midi)
            if (m.getMessage().isNoteOn()) { retrigAt = m.samplePosition; break; }

    cycleSeconds.store(syncMode ? beats * 60.0 / juce::jmax(1.0, bpm) : 1.0 / juce::jmax(0.001, hz), std::memory_order_relaxed);
    const double freeInc = (syncMode ? (bpm / 60.0) / beats : hz) / sr;
    const bool useHost = syncMode && playing && havePpq;
    const double ppqPerSample = bpm / 60.0 / sr;

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
            if (start + i == retrigAt) p = fract(offset);

            const double pos = p * Envelope::tableSize;
            const int ti = (int) pos;
            const float frac = (float) (pos - ti);
            const float v = audioTable[(size_t) ti] + (audioTable[(size_t) ti + 1] - audioTable[(size_t) ti]) * frac;

            const float target = 1.f - depth * (1.f - v);
            smoothedGain = coef * smoothedGain + (1.f - coef) * target;
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
                track(waveMin, waveMax, mono);
                if (scChannels > 0) track(scMin, scMax, scMono);

                // three-band split with one-pole filters: low | mid | high
                lpLow += coefLow * (mono - lpLow);
                lpHigh += coefHigh * (mono - lpHigh);
                const float bLow = std::abs(lpLow), bMid = std::abs(lpHigh - lpLow), bHigh = std::abs(mono - lpHigh);
                auto peak = [bin, isNew](auto& arr, float v) {
                    auto& a = arr[(size_t) bin];
                    if (isNew || v > a.load(std::memory_order_relaxed)) a.store(v, std::memory_order_relaxed);
                };
                peak(bandLow, bLow);
                peak(bandMid, bMid);
                peak(bandHigh, bHigh);
                lastWaveBin = bin;
            }
        }

        // pass 3: apply the gain, vectorised, per channel
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::multiply(mainBuf.getWritePointer(ch) + start, gain, n);
    }

    // free-running phase (fallback when the host isn't playing, and the Hz-mode oscillator)
    {
        const int from = retrigAt >= 0 ? retrigAt : 0;
        double fp = retrigAt >= 0 ? 0.0 : freePhase;
        fp += (numSamples - from) * freeInc;
        freePhase = fract(fp);
    }
    const double phase = p;

    uiPhase.store((float) phase, std::memory_order_relaxed);
    uiPhaseTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    blockCounter.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------------
// Multiband: split the signal into low / mid / high (or low / high), give every band its own
// curve and its own LFO rate, then add the bands back together.
// ---------------------------------------------------------------------------------------------
void FreeLfoProcessor::processMultiband(juce::AudioBuffer<float>& buf, double bpm, double ppq, bool havePpq, bool playing)
{
    const int n = buf.getNumSamples();
    const int numCh = juce::jmin(2, buf.getNumChannels());
    const double sr = getSampleRate();
    const bool three = pBands->load() > 0.5f;
    const bool link = pLink->load() > 0.5f;
    const int rateSource = three ? 1 : 0; // when linked, every band follows this band's rate settings
    const bool soloOn[3] = { pSoloBand[0]->load() > 0.5f, three && pSoloBand[1]->load() > 0.5f, pSoloBand[2]->load() > 0.5f };
    const bool anySolo = soloOn[0] || soloOn[1] || soloOn[2]; // soloed bands play, the others are muted
    const bool analyse = editorOpen.load(std::memory_order_relaxed);

    crossover.setSlopes((Slope) juce::jlimit(0, numSlopes - 1, (int) pSlopeLow->load()), (Slope) juce::jlimit(0, numSlopes - 1, (int) pSlopeHigh->load()));
    crossover.setFrequencies(pXLow->load(), pXHigh->load());

    auto fract = [](double v) { return v - std::floor(v); };
    const double ppqPerSample = bpm / 60.0 / sr;

    struct Lane { double p, inc, freeInc; float depth, coef; bool active; };
    Lane lane[3];
    for (int b = 0; b < 3; ++b)
    {
        const auto& rate = band[link ? rateSource : b];
        const bool syncMode = rate.mode->load() < 0.5f;
        const double beats = divisionBeats[(size_t) juce::jlimit(0, numDivisions - 1, (int) rate.division->load())]
                             * rateModifier(rate.dotted->load() > 0.5f, rate.triplet->load() > 0.5f);
        const double hz = rate.hz->load();
        const bool useHost = syncMode && playing && havePpq;

        auto& L = lane[b];
        L.freeInc = (syncMode ? (bpm / 60.0) / beats : hz) / sr;
        L.inc = useHost ? ppqPerSample / beats : L.freeInc;
        L.p = fract(useHost ? ppq / beats : bandFreePhase[b]);
        L.depth = band[b].depth->load();
        const float ms = band[b].smooth->load();
        L.coef = ms < 0.01f ? 0.f : std::exp(-1.f / (ms * 0.001f * (float) sr));
        L.active = three || b != 1;
        bandCycleSeconds[(size_t) b].store(syncMode ? beats * 60.0 / juce::jmax(1.0, bpm) : 1.0 / juce::jmax(0.001, hz),
                                           std::memory_order_relaxed);
    }

    float* data[2] = { buf.getWritePointer(0), numCh > 1 ? buf.getWritePointer(1) : nullptr };

    for (int s = 0; s < n; ++s)
    {
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
            bandSmoothed[b] = L.coef * bandSmoothed[b] + (1.f - L.coef) * target;
            g[b] = (!anySolo || soloOn[b]) ? bandSmoothed[b] : 0.f;
            bin[b] = juce::jmin(waveBins - 1, (int) (L.p * waveBins));

            L.p += L.inc;
            if (L.p >= 1.0) L.p -= std::floor(L.p);
        }

        double avg[3] = { 0.0, 0.0, 0.0 };
        for (int ch = 0; ch < numCh; ++ch)
        {
            double bands[3];
            if (three) crossover.process3(ch, data[ch][s], bands);
            else crossover.process2(ch, data[ch][s], bands);

            data[ch][s] = (float) (bands[0] * g[0] + bands[1] * g[1] + bands[2] * g[2]);
            if (analyse) { avg[0] += bands[0]; avg[1] += bands[1]; avg[2] += bands[2]; }
        }

        if (analyse)
            for (int b = 0; b < 3; ++b)
            {
                if (!lane[b].active) continue;
                const float v = (float) (avg[b] / numCh);
                const bool isNew = bin[b] != bandLastBin[b];
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

    for (int b = 0; b < 3; ++b)
    {
        bandFreePhase[b] = fract(bandFreePhase[b] + n * lane[b].freeInc);
        bandPhase[(size_t) b].store((float) lane[b].p, std::memory_order_relaxed);
    }
}

void FreeLfoProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("envelope", juce::String(getEnvelope().serialize()), nullptr);
    state.setProperty("envLow", juce::String(getEnvelope(1).serialize()), nullptr);
    state.setProperty("envMid", juce::String(getEnvelope(2).serialize()), nullptr);
    state.setProperty("envHigh", juce::String(getEnvelope(3).serialize()), nullptr);
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
            if (state.hasProperty("envelope"))
                setEnvelope(Envelope::deserialize(state["envelope"].toString().toStdString()));
            const char* keys[3] = { "envLow", "envMid", "envHigh" };
            for (int b = 0; b < 3; ++b)
                if (state.hasProperty(keys[b]))
                    setEnvelope(Envelope::deserialize(state[keys[b]].toString().toStdString()), b + 1);
        }
    }
}

juce::AudioProcessorEditor* FreeLfoProcessor::createEditor() { return new FreeLfoEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FreeLfoProcessor(); }
