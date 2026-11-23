#include "PluginProcessor.h"
#include "PluginEditor.h"

const std::array<const char*, 15> FreeLfoProcessor::divisionNames = {
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32",
    "1/4 T", "1/8 T", "1/16 T", "1/4 D", "1/8 D", "1/16 D"
};

const std::array<double, 15> FreeLfoProcessor::divisionBeats = {
    32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
    2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75, 0.375
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

    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "hz", 1 }, "Frequency",
                                                NormalisableRange<float>(0.01f, 40.f, 0.001f, 0.35f), 2.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "depth", 1 }, "Depth",
                                                NormalisableRange<float>(0.f, 1.f, 0.001f), 1.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "smooth", 1 }, "Smooth (ms)",
                                                NormalisableRange<float>(0.f, 50.f, 0.01f, 0.5f), 2.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "offset", 1 }, "Phase Offset",
                                                NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "waveOpacity", 1 }, "Waveform Opacity",
                                                NormalisableRange<float>(0.f, 1.f, 0.01f), 0.35f));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "retrigger", 1 }, "MIDI Retrigger (Hz mode)", false));
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
    pHz = apvts.getRawParameterValue("hz");
    pDepth = apvts.getRawParameterValue("depth");
    pSmooth = apvts.getRawParameterValue("smooth");
    pOffset = apvts.getRawParameterValue("offset");
    pRetrigger = apvts.getRawParameterValue("retrigger");

    setEnvelope(Envelope()); // default pump shape
}

bool FreeLfoProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (out != layouts.getMainInputChannelSet()) return false;

    auto sc = layouts.getChannelSet(true, 1); // optional sidechain
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void FreeLfoProcessor::prepareToPlay(double, int)
{
    freePhase = 0.0;
    smoothedGain = 1.f;
    audioTableVersion = -1;
}

Envelope FreeLfoProcessor::getEnvelope() const
{
    const juce::SpinLock::ScopedLockType lock(envLock);
    return envelope;
}

void FreeLfoProcessor::setEnvelope(const Envelope& e)
{
    Envelope copy = e;
    copy.normalize();
    const juce::SpinLock::ScopedLockType lock(envLock);
    envelope = copy;
    envelope.bake(sharedTable.data());
    tableVersion.fetch_add(1, std::memory_order_release);
}

// Audio thread: pull in a newly baked table if one is available. Never blocks.
void FreeLfoProcessor::syncTable()
{
    const int v = tableVersion.load(std::memory_order_acquire);
    if (v == audioTableVersion) return;
    const juce::SpinLock::ScopedTryLockType lock(envLock);
    if (lock.isLocked())
    {
        audioTable = sharedTable;
        audioTableVersion = v;
    }
}

void FreeLfoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    syncTable();

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

    const bool syncMode = pMode->load() < 0.5f;
    const double beats = divisionBeats[(size_t) juce::jlimit(0, 14, (int) pDivision->load())];
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

    const double freeInc = (syncMode ? (bpm / 60.0) / beats : hz) / sr;
    const bool useHost = syncMode && playing && havePpq;
    const double ppqPerSample = bpm / 60.0 / sr;

    double phase = 0.0;
    for (int s = 0; s < numSamples; ++s)
    {
        if (s == retrigAt) freePhase = 0.0;

        if (useHost) phase = (ppq + s * ppqPerSample) / beats + offset;
        else phase = freePhase + offset;
        phase -= std::floor(phase);

        const double pos = phase * Envelope::tableSize;
        const int i = (int) pos;
        const float frac = (float) (pos - i);
        const float v = audioTable[(size_t) i] + (audioTable[(size_t) i + 1] - audioTable[(size_t) i]) * frac;

        const float target = 1.f - depth * (1.f - v);
        smoothedGain = coef * smoothedGain + (1.f - coef) * target;

        float mono = 0.f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* d = mainBuf.getWritePointer(ch);
            mono += d[s]; // pre-LFO input, for the waveform display
            d[s] *= smoothedGain;
        }
        if (numChannels > 0) mono /= (float) numChannels;

        const int bin = juce::jmin(waveBins - 1, (int) (phase * waveBins));
        float scMono = 0.f;
        for (int c = 0; c < scChannels; ++c) scMono += scData[c][s];
        if (scChannels > 0) scMono /= (float) scChannels;

        auto track = [bin, isNew = (bin != lastWaveBin)](auto& mn, auto& mx, float v) {
            auto& lo = mn[(size_t) bin];
            auto& hi = mx[(size_t) bin];
            if (isNew) { lo.store(v, std::memory_order_relaxed); hi.store(v, std::memory_order_relaxed); return; }
            if (v < lo.load(std::memory_order_relaxed)) lo.store(v, std::memory_order_relaxed);
            if (v > hi.load(std::memory_order_relaxed)) hi.store(v, std::memory_order_relaxed);
        };
        track(waveMin, waveMax, mono);
        if (scChannels > 0) track(scMin, scMax, scMono);
        lastWaveBin = bin;

        freePhase += freeInc;
        if (freePhase >= 1.0) freePhase -= std::floor(freePhase);
    }

    uiPhase.store((float) phase, std::memory_order_relaxed);
}

void FreeLfoProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("envelope", juce::String(getEnvelope().serialize()), nullptr);
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
        }
    }
}

juce::AudioProcessorEditor* FreeLfoProcessor::createEditor() { return new FreeLfoEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FreeLfoProcessor(); }
