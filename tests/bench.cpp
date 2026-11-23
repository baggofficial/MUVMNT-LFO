// CPU benchmark for the audio thread. Build with:  cmake -B build -DFREELFO_BENCH=ON && cmake --build build --target LfoBench
// Run:  build/LfoBench_artefacts/Release/LfoBench
#include "../Source/PluginProcessor.h"
#include <chrono>
#include <cstdio>

// Minimal transport so the "synced to host" path is exercised too.
struct FakeHost : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 140.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(true);
        return info;
    }
};

static void setParam(FreeLfoProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(plain));
}

struct Scenario { const char* name; bool hostSync; bool sync; bool editorOpen; bool sidechain; };

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    constexpr double sr = 48000.0;
    constexpr int blockSize = 512, blocks = 20000;

    const Scenario scenarios[] = {
        { "Sync, host transport, editor closed", true, true, false, false },
        { "Sync, host transport, editor open  ", true, true, true, false },
        { "Hz mode (free run), editor closed  ", false, false, false, false },
        { "Sync + sidechain, editor open      ", true, true, true, true },
    };

    for (auto& s : scenarios)
    {
        FreeLfoProcessor p;
        FakeHost host;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(sr, blockSize);
        p.prepareToPlay(sr, blockSize);
        setParam(p, "mode", s.sync ? 0.f : 1.f);
        setParam(p, "hz", 3.f);
        setParam(p, "smooth", 0.f);
        p.editorOpen.store(s.editorOpen);
        (void) s.sidechain;

        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        juce::Random rng(1);
        auto refill = [&] {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i) buf.setSample(ch, i, rng.nextFloat() * 2.f - 1.f);
        };

        for (int i = 0; i < 200; ++i) { refill(); p.processBlock(buf, midi); } // warm up

        double total = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            refill();
            host.ppq += blockSize * host.bpm / 60.0 / sr;
            auto t0 = std::chrono::steady_clock::now();
            p.processBlock(buf, midi);
            total += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }

        const double perBlockUs = total / blocks * 1e6;
        const double budgetUs = blockSize / sr * 1e6;
        std::printf("%s  %7.2f us/block  %6.2f ns/sample  %.3f%% of one core\n",
                    s.name, perBlockUs, perBlockUs * 1000.0 / blockSize, 100.0 * perBlockUs / budgetUs);
    }

    // Null tests: when the effect should be inactive, output must equal input bit-for-bit.
    auto nullTest = [&](const char* name, auto&& configure) {
        FreeLfoProcessor p;
        FakeHost host;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(sr, blockSize);
        p.prepareToPlay(sr, blockSize);
        configure(p);
        juce::AudioBuffer<float> buf(2, blockSize), ref(2, blockSize);
        juce::MidiBuffer midi;
        juce::Random rng(7);
        float maxDiff = 0.f;
        long mismatches = 0;
        for (int b = 0; b < 2000; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i) ref.setSample(ch, i, rng.nextFloat() * 2.f - 1.f);
            buf.makeCopyOf(ref);
            host.ppq += blockSize * host.bpm / 60.0 / sr;
            p.processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float d = std::abs(buf.getSample(ch, i) - ref.getSample(ch, i));
                    if (d != 0.f) ++mismatches;
                    maxDiff = juce::jmax(maxDiff, d);
                }
        }
        std::printf("null test (%s): %ld differing samples, max diff %.3g (%.1f dB)\n", name, mismatches, maxDiff,
                    maxDiff > 0 ? 20.0 * std::log10(maxDiff) : -999.0);
    };
    nullTest("depth 0", [](FreeLfoProcessor& p) { setParam(p, "depth", 0.f); });
    nullTest("depth 100%, flat curve at 1, sync", [](FreeLfoProcessor& p) {
        Envelope e; e.points = { { 0.f, 1.f }, { 1.f, 1.f } }; p.setEnvelope(e); setParam(p, "depth", 1.f); });
    nullTest("depth 100%, flat curve at 1, Hz, smooth 50ms", [](FreeLfoProcessor& p) {
        Envelope e; e.points = { { 0.f, 1.f }, { 1.f, 1.f } }; p.setEnvelope(e);
        setParam(p, "depth", 1.f); setParam(p, "mode", 1.f); setParam(p, "smooth", 50.f); });
    nullTest("default pump, depth 100% (should differ)", [](FreeLfoProcessor& p) { setParam(p, "depth", 1.f); });

    // ---- Multiband checks ----
    auto toneRms = [&](FreeLfoProcessor& p, double freq) {
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        double acc = 0.0; long count = 0, t = 0;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++t)
            {
                const float v = (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * (double) t / sr);
                buf.setSample(0, i, v); buf.setSample(1, i, v);
            }
            p.processBlock(buf, midi);
            if (b >= 100) for (int i = 0; i < blockSize; ++i) { acc += buf.getSample(0, i) * buf.getSample(0, i); ++count; }
        }
        return std::sqrt(2.0 * acc / (double) count); // amplitude of a unit sine = 1.0
    };
    auto mbSetup = [&](FreeLfoProcessor& p) {
        p.setRateAndBufferSizeDetails(sr, blockSize);
        p.prepareToPlay(sr, blockSize);
        setParam(p, "mb", 1.f);
        Envelope flat; flat.resetToFlat();
        for (int slot = 1; slot <= 3; ++slot) p.setEnvelope(flat, slot);
    };
    auto db = [](double x) { return 20.0 * std::log10(juce::jmax(1e-9, x)); };
    bool mbOk = true;

    for (double f : { 60.0, 1000.0, 8000.0 })
    {
        FreeLfoProcessor p; mbSetup(p);
        const double a = toneRms(p, f);
        std::printf("multiband, curves flat, %5.0f Hz tone: level %+.3f dB (expect 0)\n", f, db(a));
        mbOk = mbOk && std::abs(db(a)) < 0.05;
    }
    {
        // Duck only the low band to silence: lows vanish, highs are untouched
        Envelope zero; zero.points = { { 0.f, 0.f }, { 1.f, 0.f } };
        FreeLfoProcessor p1; mbSetup(p1); p1.setEnvelope(zero, 1);
        const double lowTone = toneRms(p1, 60.0);
        FreeLfoProcessor p2; mbSetup(p2); p2.setEnvelope(zero, 1);
        const double highTone = toneRms(p2, 8000.0);
        std::printf("multiband, low band ducked: 60 Hz %+.1f dB, 8 kHz %+.3f dB\n", db(lowTone), db(highTone));
        mbOk = mbOk && db(lowTone) < -40.0 && std::abs(db(highTone)) < 0.05;
    }
    {
        FreeLfoProcessor p; mbSetup(p); setParam(p, "soloL", 1.f); // solo the low band
        const double hi = toneRms(p, 8000.0);
        FreeLfoProcessor q; mbSetup(q); setParam(q, "soloL", 1.f);
        const double lo = toneRms(q, 60.0);
        std::printf("multiband, solo low: 60 Hz %+.3f dB, 8 kHz %+.1f dB\n", db(lo), db(hi));
        mbOk = mbOk && std::abs(db(lo)) < 0.15 && db(hi) < -40.0; // 60 Hz is just under the 200 Hz split, so the band itself is down ~0.07 dB
    }
    {
        // Solo several bands at once: low + mid play, high is muted
        auto make = [&](double f) { FreeLfoProcessor p; mbSetup(p); setParam(p, "soloL", 1.f); setParam(p, "soloM", 1.f); return toneRms(p, f); };
        const double lo = make(60.0), mid = make(1000.0), hi = make(8000.0);
        std::printf("multiband, solo low+mid: 60 Hz %+.3f dB, 1 kHz %+.3f dB, 8 kHz %+.1f dB\n", db(lo), db(mid), db(hi));
        mbOk = mbOk && std::abs(db(lo)) < 0.15 && std::abs(db(mid)) < 0.3 && db(hi) < -40.0; // 1 kHz sits below the 2.5 kHz split, so the mid band alone is down ~0.2 dB
    }
    for (int slopeIdx = 0; slopeIdx < 6; ++slopeIdx)
        for (double f : { 60.0, 1000.0, 8000.0 })
        {
            FreeLfoProcessor p; mbSetup(p); setParam(p, "slopeLow", (float) slopeIdx); setParam(p, "slopeHigh", (float) slopeIdx);
            const double a = toneRms(p, f);
            mbOk = mbOk && std::abs(db(a)) < 0.05;
            if (std::abs(db(a)) >= 0.05) std::printf("slope %d, %.0f Hz: %+.3f dB\n", slopeIdx, f, db(a));
        }
    {
        // Duck the low band and look at a tone one octave above the split. Informational only:
        // how much of it gets hit depends on the slope AND on the crossover's phase shift, so it is
        // not monotonic across slopes. (Isolation steepness is verified in crossover_test.cpp.)
        double leakAbove[6];
        for (int slopeIdx = 0; slopeIdx < 6; ++slopeIdx)
        {
            Envelope zero; zero.points = { { 0.f, 0.f }, { 1.f, 0.f } };
            FreeLfoProcessor p; mbSetup(p); setParam(p, "slopeLow", (float) slopeIdx); setParam(p, "slopeHigh", (float) slopeIdx); p.setEnvelope(zero, 1);
            leakAbove[slopeIdx] = db(toneRms(p, 400.0)); // one octave above the 200 Hz split
        }
        std::printf("low band ducked, 400 Hz tone (1 octave above the split): 6dB %+.1f, 12dB %+.1f, 24dB %+.1f, 48dB %+.1f, 72dB %+.1f, 96dB %+.1f dB\n",
                    leakAbove[0], leakAbove[1], leakAbove[2], leakAbove[3], leakAbove[4], leakAbove[5]);
    }
    std::printf("multiband checks: %s\n", mbOk ? "OK" : "FAILED");

    for (bool editor : { false, true })
    {
        FreeLfoProcessor p; FakeHost host;
        p.setPlayHead(&host); mbSetup(p); p.editorOpen.store(editor);
        juce::AudioBuffer<float> buf(2, blockSize); juce::MidiBuffer midi; juce::Random rng(3);
        for (int i = 0; i < 200; ++i) { for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < blockSize; ++k) buf.setSample(ch, k, rng.nextFloat() * 2.f - 1.f); p.processBlock(buf, midi); }
        double total = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < blockSize; ++k) buf.setSample(ch, k, rng.nextFloat() * 2.f - 1.f);
            host.ppq += blockSize * host.bpm / 60.0 / sr;
            auto t0 = std::chrono::steady_clock::now();
            p.processBlock(buf, midi);
            total += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
        const double perBlockUs = total / blocks * 1e6;
        std::printf("Multiband (3 bands), editor %s  %7.2f us/block  %6.2f ns/sample  %.3f%% of one core\n",
                    editor ? "open  " : "closed", perBlockUs, perBlockUs * 1000.0 / blockSize, 100.0 * perBlockUs / (blockSize / sr * 1e6));
    }

    // Sanity: a constant signal through a 1 Hz LFO at full depth must sweep the whole 0..1 range
    {
        FreeLfoProcessor p;
        p.setRateAndBufferSizeDetails(sr, blockSize);
        p.prepareToPlay(sr, blockSize);
        setParam(p, "mode", 1.f);
        setParam(p, "hz", 1.f);
        setParam(p, "smooth", 0.f);
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        float lo = 10.f, hi = -10.f;
        for (int b = 0; b < (int) (sr / blockSize) + 1; ++b)
        {
            for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill(buf.getWritePointer(ch), 1.f, blockSize);
            p.processBlock(buf, midi);
            for (int i = 0; i < blockSize; ++i) { lo = juce::jmin(lo, buf.getSample(0, i)); hi = juce::jmax(hi, buf.getSample(0, i)); }
        }
        std::printf("sanity (1 Hz, full depth): min %.3f  max %.3f  %s\n", lo, hi, (lo < 0.02f && hi > 0.98f) ? "OK" : "WRONG");
        return (lo < 0.02f && hi > 0.98f) ? 0 : 1;
    }
}
