// Offline audio tests for Free LFO Tool: a fake DAW (130 bpm) renders audio through the real processor and checks it.
//
//   Build:  cmake -B build -DFREELFO_BENCH=ON && cmake --build build --target LfoAudioTest
//   Run:    build/LfoAudioTest_artefacts/Release/LfoAudioTest           (exit code 0 = everything passed)
//
// Covered: bit-exact null at 100%, true silence at 0%, block-size independence, clicks on parameter changes,
// filter stability, NaN/Inf and extreme-setting robustness, tempo accuracy at 130 bpm, the sidechain bus, no
// allocations on the audio thread, thread safety while the curve is edited, state save/load, and CPU in worst cases.
#include "../Source/PluginProcessor.h"
#include "../Source/Spectrum.h"
#include "fake_host.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Count heap allocations while a flag is set (real-time safety: the audio thread must never allocate)
// ---------------------------------------------------------------------------------------------
static std::atomic<long> g_allocs { 0 };
static std::atomic<bool> g_countAllocs { false };
static std::atomic<long> g_otherThreadAllocs { 0 };     // allocations by other threads while counting (JUCE / OS helper threads)
static thread_local bool t_isAudioThread = false;       // only the thread that runs processBlock counts: that is the real-time rule

void* operator new(std::size_t n)
{
    if (g_countAllocs.load(std::memory_order_relaxed))
        (t_isAudioThread ? g_allocs : g_otherThreadAllocs).fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// Shared CI machines are noisy: timing-based limits are loosened there (GitHub sets CI=true); correctness checks never are
static const bool kLenientTiming = std::getenv("CI") != nullptr;

// ---------------------------------------------------------------------------------------------
// Tiny test framework
// ---------------------------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;

static std::string fmt(const char* f, ...)
{
    char buf[512];
    va_list a; va_start(a, f); std::vsnprintf(buf, sizeof buf, f, a); va_end(a);
    return buf;
}

static void check(bool ok, const std::string& name, const std::string& detail = {})
{
    std::printf("  [%s] %s%s%s\n", ok ? "ok" : "FAIL", name.c_str(), detail.empty() ? "" : "  - ", detail.c_str());
    ok ? ++g_pass : ++g_fail;
}

static void section(const char* title) { std::printf("\n%s\n", title); }

// ---------------------------------------------------------------------------------------------
// Signals and measurements
// ---------------------------------------------------------------------------------------------
using Sig = std::vector<float>;
using Stereo = std::array<Sig, 2>;

static Stereo noise(size_t n, unsigned seed, float amp = 0.5f)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amp, amp);
    Stereo s { Sig(n), Sig(n) };
    for (size_t i = 0; i < n; ++i) { s[0][i] = d(rng); s[1][i] = d(rng); }
    return s;
}

static Stereo sine(size_t n, double hz, double sr, float amp = 0.5f)
{
    Stereo s { Sig(n), Sig(n) };
    for (size_t i = 0; i < n; ++i) s[0][i] = s[1][i] = amp * (float) std::sin(2.0 * 3.14159265358979323846 * hz * (double) i / sr);
    return s;
}

static Stereo dc(size_t n, float v) { return { Sig(n, v), Sig(n, v) }; }

static float maxAbs(const Sig& a, size_t from = 0)
{
    float m = 0.f;
    for (size_t i = from; i < a.size(); ++i) m = std::max(m, std::fabs(a[i]));
    return m;
}

static float maxDiff(const Sig& a, const Sig& b, size_t from = 0)
{
    float m = 0.f;
    for (size_t i = from; i < std::min(a.size(), b.size()); ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

static double rms(const Sig& a, size_t from = 0)
{
    double acc = 0.0;
    for (size_t i = from; i < a.size(); ++i) acc += (double) a[i] * a[i];
    return std::sqrt(acc / (double) std::max<size_t>(1, a.size() - from));
}

static bool allFinite(const Sig& a, size_t from = 0)
{
    for (size_t i = from; i < a.size(); ++i) if (!std::isfinite(a[i])) return false;
    return true;
}

static float maxStep(const Sig& a, size_t from, size_t to) // biggest jump between neighbouring samples
{
    float m = 0.f;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, a.size()); ++i) m = std::max(m, std::fabs(a[i] - a[i - 1]));
    return m;
}

static float maxSecondDiff(const Sig& a, size_t from, size_t to) // a click shows up as a spike in the second difference
{
    float m = 0.f;
    for (size_t i = std::max<size_t>(from, 2); i < std::min(to, a.size()); ++i) m = std::max(m, std::fabs(a[i] - 2.f * a[i - 1] + a[i - 2]));
    return m;
}

static double db(double x) { return 20.0 * std::log10(std::max(1e-12, x)); }

// ---------------------------------------------------------------------------------------------
// The rig: the real processor + a fake DAW transport
// ---------------------------------------------------------------------------------------------
struct Rig
{
    FreeLfoProcessor p;
    FakeHost host;
    double sr;

    explicit Rig(double sampleRate = 48000.0, int maxBlock = 512) : sr(sampleRate)
    {
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(sr, maxBlock);
        p.prepareToPlay(sr, maxBlock);
    }

    void param(const char* id, float plain)
    {
        auto* prm = p.apvts.getParameter(id);
        prm->setValueNotifyingHost(prm->convertTo0to1(plain));
    }

    void allCurves(const Envelope& e) { for (int s = 0; s < FreeLfoProcessor::numSlots; ++s) p.setEnvelope(e, s); }

    // Process `in` in blocks of the given sizes (cycled). hook(blockIndex) runs before each block (change a parameter,
    // move the transport, ...); midiHook may add MIDI to the block.
    Stereo render(const Stereo& in, const std::vector<int>& sizes, const std::function<void(int)>& hook = {},
                  const std::function<void(juce::MidiBuffer&, int)>& midiHook = {})
    {
        const size_t n = in[0].size();
        Stereo out { Sig(n), Sig(n) };
        size_t pos = 0;
        int blockIndex = 0;
        juce::MidiBuffer midi;
        while (pos < n)
        {
            const int size = (int) std::min<size_t>((size_t) sizes[(size_t) blockIndex % sizes.size()], n - pos);
            if (hook) hook(blockIndex);
            midi.clear();
            if (midiHook) midiHook(midi, blockIndex);
            juce::AudioBuffer<float> buf(2, size);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < size; ++i) buf.setSample(ch, i, in[(size_t) ch][pos + (size_t) i]);
            p.processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < size; ++i) out[(size_t) ch][pos + (size_t) i] = buf.getSample(ch, i);
            pos += (size_t) size;
            if (host.playing) host.ppq += (double) size * host.bpm / 60.0 / sr;
            ++blockIndex;
        }
        return out;
    }

    Stereo render(const Stereo& in, int blockSize = 512, const std::function<void(int)>& hook = {})
    {
        return render(in, std::vector<int> { blockSize }, hook);
    }
};

static Envelope flatCurve(float y) { Envelope e; e.points = { { 0.f, y }, { 1.f, y } }; return e; }

// A gentle triangle LFO (0.5 -> 1 -> 0.5 -> 0 -> 0.5): no sharp edges of its own, so any click is caused by what a test does
static Envelope triangleCurve()
{
    Envelope e;
    e.points = { { 0.f, 0.5f }, { 0.25f, 1.f }, { 0.5f, 0.5f }, { 0.75f, 0.f }, { 1.f, 0.5f } };
    return e;
}

// ---------------------------------------------------------------------------------------------
// 1. Null tests
// ---------------------------------------------------------------------------------------------
static void testNull()
{
    section("1. Null tests: with the effect neutral, output == input bit for bit");
    const auto in = noise(48000 * 2, 1);

    for (bool syncMode : { true, false })
        for (float smooth : { 0.f, 50.f })
        {
            Rig r; r.param("mode", syncMode ? 0.f : 1.f); r.param("smooth", smooth);
            r.allCurves(flatCurve(1.f)); r.param("depth", 1.f);
            const auto out = r.render(in);
            check(maxDiff(out[0], in[0]) == 0.f && maxDiff(out[1], in[1]) == 0.f,
                  fmt("curve flat at 100%%, %s, smooth %.0f ms", syncMode ? "Sync" : "Hz", smooth));
        }
    {
        Rig r; r.param("depth", 0.f);   // the default curve, depth 0
        const auto out = r.render(in);
        check(maxDiff(out[0], in[0]) == 0.f && maxDiff(out[1], in[1]) == 0.f, "depth 0 on the default curve");
    }
    for (float smooth : { 0.f, 5.f, 50.f })
    {
        // After automation settles the gain must land on exactly 1, not 0.9999999
        Rig r; r.param("smooth", smooth); r.param("depth", 0.7f);
        r.render(noise(48000, 2));
        r.param("depth", 0.f);
        const auto in2 = noise(48000 * 3, 3);
        const auto out = r.render(in2);
        check(maxDiff(out[0], in2[0], 48000 * 2) == 0.f && maxDiff(out[1], in2[1], 48000 * 2) == 0.f,
              fmt("depth 0.7 -> 0 (smooth %.0f ms): exact null once settled", smooth));
    }
    {
        // Solo a band and un-solo it again: afterwards the sound is exactly what it would have been without ever soloing
        const auto in3 = noise(48000 * 2, 5);
        Rig a; a.param("mb", 1.f); a.allCurves(flatCurve(1.f));
        Rig b; b.param("mb", 1.f); b.allCurves(flatCurve(1.f));
        const auto ref = a.render(in3);
        const auto out = b.render(in3, 256, [&](int blk) { if (blk == 40) b.param("soloH", 1.f); if (blk == 90) b.param("soloH", 0.f); });
        check(maxDiff(out[0], ref[0], 48000) == 0.f && maxDiff(out[1], ref[1], 48000) == 0.f, "multiband: solo then un-solo returns to the exact original sound");
    }
}

// ---------------------------------------------------------------------------------------------
// 2. Silence
// ---------------------------------------------------------------------------------------------
static void testSilence()
{
    section("2. Silence at 0%: a curve at zero gives exactly no sound");
    const auto in = noise(48000 * 2, 4, 1.0f);
    for (float smooth : { 0.f, 50.f })
    {
        Rig r; r.param("smooth", smooth); r.allCurves(flatCurve(0.f)); r.param("depth", 1.f);
        const auto out = r.render(in);
        check(maxAbs(out[0], 48000) == 0.f && maxAbs(out[1], 48000) == 0.f, fmt("single band, curve at 0, smooth %.0f ms: exact silence once settled", smooth));
    }
    {
        Rig r; r.param("mb", 1.f); r.allCurves(flatCurve(0.f));
        const auto out = r.render(in);
        check(maxAbs(out[0]) == 0.f && maxAbs(out[1]) == 0.f, "multiband, all three curves at 0: exact silence");
    }
    {
        Rig r; r.param("mb", 1.f); r.allCurves(flatCurve(0.f)); r.param("depthL", 0.f); r.param("depthM", 0.f); r.param("depthH", 0.f);
        const auto out = r.render(in);
        check(maxAbs(out[0], 20000) > 0.1f, "multiband, curves at 0 but depth 0: the sound is NOT muted (depth wins)");
    }
    {
        Rig r;   // default curve at 100% depth: the floor of the pump is true silence
        const auto out = r.render(dc(48000 * 4, 1.f));
        size_t zeros = 0;
        for (float v : out[0]) if (v == 0.f) ++zeros;
        const double share = 100.0 * (double) zeros / (double) out[0].size();
        check(share > 8.0, "default pump at 100% depth: floor is exactly 0", fmt("%.1f%% of samples are exactly 0", share));
    }
}

// ---------------------------------------------------------------------------------------------
// 3. Multiband transparency (the crossover must not colour the sound)
// ---------------------------------------------------------------------------------------------
static void testMultibandTransparent()
{
    section("3. Multiband with flat curves: bands add back to the input's spectrum");
    const auto in = noise(48000 * 2, 7);
    const char* names[6] = { "6", "12", "24", "48", "72", "96" };
    for (int slope = 0; slope < 6; ++slope)
        for (bool three : { true, false })
        {
            Rig r; r.param("mb", 1.f); r.allCurves(flatCurve(1.f));
            r.param("slopeLow", (float) slope); r.param("slopeHigh", (float) slope); r.param("mbBands", three ? 1.f : 0.f);
            const auto out = r.render(in);

            Crossover ref; ref.prepare(48000.0); ref.setSlopes((Slope) slope, (Slope) slope); ref.setFrequencies(200.0, 2500.0);
            Stereo expect { Sig(in[0].size()), Sig(in[0].size()) };
            for (size_t i = 0; i < in[0].size(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    double b[3];
                    if (three) ref.process3(ch, in[(size_t) ch][i], b); else ref.process2(ch, in[(size_t) ch][i], b);
                    expect[(size_t) ch][i] = (float) (b[0] + b[1] + b[2]);
                }
            const float diff = std::max(maxDiff(out[0], expect[0]), maxDiff(out[1], expect[1]));
            const double level = db(rms(out[0], 24000) / rms(in[0], 24000));
            check(diff < 2e-6f && std::fabs(level) < 0.05,
                  fmt("%s dB/oct, %d bands", names[slope], three ? 3 : 2),
                  fmt("matches the reference crossover to %.0f dB, overall level %+.3f dB", db(diff), level));
        }
}

// Per-knob links: with a link on, every band follows the mid band's value (the low band's in 2-band mode)
static void testKnobLinks()
{
    section("3b. Linked depth / smooth / offset: every band follows one band");
    const auto in = noise(48000, 11);
    for (bool three : { true, false })
    {
        const char* src = three ? "M" : "L";
        auto diffFromAll = [&](const char* link, const char* base, float a, float b, float c, bool linked) {
            Rig r1; r1.param("mb", 1.f); r1.param("mbBands", three ? 1.f : 0.f);
            r1.param(link, linked ? 1.f : 0.f);
            r1.param((std::string(base) + "L").c_str(), a); r1.param((std::string(base) + "M").c_str(), b); r1.param((std::string(base) + "H").c_str(), c);
            const float follow = std::string(src) == "M" ? b : a;
            Rig r2; r2.param("mb", 1.f); r2.param("mbBands", three ? 1.f : 0.f);
            for (const char* sfx : { "L", "M", "H" }) r2.param((std::string(base) + sfx).c_str(), follow);
            const auto o1 = r1.render(in), o2 = r2.render(in);
            return std::max(maxDiff(o1[0], o2[0]), maxDiff(o1[1], o2[1]));
        };
        for (auto [link, base, a, b, c] : { std::tuple<const char*, const char*, float, float, float> { "linkDepth", "depth", 0.2f, 0.8f, 0.5f },
                                            { "linkSmooth", "smooth", 4.f, 1.f, 9.f }, { "linkOffset", "offset", 0.3f, 0.1f, 0.6f } })
        {
            const float on = diffFromAll(link, base, a, b, c, true), off = diffFromAll(link, base, a, b, c, false);
            check(on < 1e-6f && off > 1e-3f, fmt("%s, %d bands", link, three ? 3 : 2),
                  fmt("linked matches 'all bands = source' (diff %.1e), unlinked differs (%.1e)", on, off));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Peak meters: the input meter reads the input peak, the output meter never reads more than it (the plugin only ducks).
// ---------------------------------------------------------------------------------------------
static void testMeters()
{
    section("3c. Peak meters");
    Rig r; r.p.editorOpen.store(true);
    Stereo in { Sig(4800), Sig(4800) };
    for (size_t i = 0; i < in[0].size(); ++i) in[0][i] = in[1][i] = 0.5f * (float) std::sin(0.05 * (double) i);
    r.render(in, { 480 });
    const float mi = r.p.meterIn.load(), mo = r.p.meterOut.load();
    check(mi > 0.45f && mi < 0.52f, "input meter reads the input peak", fmt("%.3f (input peak 0.5)", mi));
    check(mo <= mi + 1e-4f && mo > 0.f, "output meter reads at most the input peak", fmt("%.3f", mo));
    {   // and not just at the end: after every block, with levels jumping around and the meters falling in between
        Rig q; q.p.editorOpen.store(true);
        std::mt19937 rng(71);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        double worst = -1.0;
        for (int block = 0; block < 2000; ++block)
        {
            const size_t n = 1 + (size_t) (u(rng) * 700.f);
            q.render(noise(n, 1000u + (unsigned) block, u(rng) < 0.2f ? 0.9f : 0.9f * u(rng) * u(rng)), { (int) n });
            worst = std::max(worst, (double) q.p.meterOut.load() - (double) q.p.meterIn.load());
        }
        check(worst <= 1e-6, "single band: OUT is never above IN, after any of 2000 blocks", fmt("largest OUT minus IN %.6f", worst));
    }
}

// ---------------------------------------------------------------------------------------------
// The spectrum analyser: a sine lands in the right bin at the right level.
// ---------------------------------------------------------------------------------------------
static void testSpectrum()
{
    section("3c2. Spectrum analyser");
    spectrum::Analyser a;
    std::vector<float> sig((size_t) spectrum::fftSize), db;
    const int bin = 100;
    for (int i = 0; i < spectrum::fftSize; ++i)
        sig[(size_t) i] = 0.5f * (float) std::sin(6.283185307179586 * (double) bin * (double) i / (double) spectrum::fftSize);
    a.magnitudesDb(sig.data(), db);
    size_t peak = 0;
    for (size_t k = 1; k < db.size(); ++k) if (db[k] > db[peak]) peak = k;
    check(peak == (size_t) bin, "a sine lands in the right bin", fmt("bin %zu (expected %d)", peak, bin));
    check(std::abs(db[peak] - (-6.02f)) < 0.6f, "a 0.5 sine reads about -6 dB", fmt("%.2f dB", db[peak]));
    check(db[(size_t) bin + 40] < db[peak] - 60.f, "far from the sine it is quiet", fmt("%.1f dB below", db[peak] - db[(size_t) bin + 40]));
}

// ---------------------------------------------------------------------------------------------
// Bypass: bit-exact pass-through once the 10 ms crossfade is done, no jump while it fades, and back again.
// ---------------------------------------------------------------------------------------------
static void testBypass()
{
    section("3d. Bypass");
    for (bool multiband : { false, true })
    {
        Rig r; r.param("mb", multiband ? 1.f : 0.f);
        const auto in = noise(48000, 21);
        const auto warm = r.render(in);   // processed: not equal to the input
        (void) warm;
        r.param("bypass", 1.f);
        const auto byp = r.render(in);
        // the first 10 ms are the crossfade; after that the output is the input, bit for bit
        const size_t from = 1000;
        float diff = 0.f;
        for (size_t i = from; i < in[0].size(); ++i) diff = std::max(diff, std::abs(byp[0][i] - in[0][i]));
        check(diff == 0.f, multiband ? "multiband: bypassed output equals the input exactly" : "single band: bypassed output equals the input exactly", fmt("max diff %.1e", diff));
        {   // no jump while fading: a steady 0.5 signal, so any step in the output comes from the crossfade itself
            Rig d; d.param("mb", multiband ? 1.f : 0.f);
            Stereo dc { Sig(4800, 0.5f), Sig(4800, 0.5f) };
            d.render(dc);
            d.param("bypass", 1.f);
            const auto o = d.render(dc);
            float step = 0.f;
            for (size_t i = 1; i < 1500; ++i) step = std::max(step, std::abs(o[0][i] - o[0][i - 1]));
            check(step < 0.02f, "the crossfade has no jump", fmt("largest step %.4f (signal 0.5)", step));
        }
        r.param("bypass", 0.f);
        const auto back = r.render(in);
        float movement = 0.f;
        for (size_t i = 0; i < 48000; i += 997) movement = std::max(movement, std::abs(back[0][i] - in[0][i]));
        check(movement > 1e-3f, "switching bypass off processes the audio again", fmt("deviation from input %.3f", movement));
    }
}

static void testClipper()
{
    section("3e. Output clipper (multiband)");
    Stereo loud { Sig(9600, 0.f), Sig(9600, 0.f) };
    for (size_t i = 0; i < 9600; ++i) loud[0][i] = loud[1][i] = 1.8f * std::sin((float) i * 0.05f);   // peaks well past 0 dB
    Stereo quiet { Sig(9600, 0.f), Sig(9600, 0.f) };
    for (size_t i = 0; i < 9600; ++i) quiet[0][i] = quiet[1][i] = 0.5f * std::sin((float) i * 0.05f);
    auto peak = [](const Stereo& s) { float m = 0.f; for (auto v : s[0]) m = std::max(m, std::abs(v)); return m; };
    for (int mode : { 0, 1, 2 })
    {
        Rig r; r.param("mb", 1.f); r.param("depth", 0.f); r.param("clip", (float) mode);
        r.render(loud);
        const float pk = peak(r.render(loud));
        if (mode == 0) check(pk > 1.2f, "clipper off: peaks pass through", fmt("peak %.2f", pk));
        else check(pk <= 1.0001f, mode == 1 ? "soft clipper: output never exceeds 0 dB" : "hard clipper: output never exceeds 0 dB", fmt("peak %.4f", pk));
    }
    {
        Rig r; r.param("mb", 1.f); r.param("clip", 1.f);
        Rig o; o.param("mb", 1.f);
        r.render(quiet); o.render(quiet);
        const auto a = r.render(quiet), b = o.render(quiet);
        float d = 0.f; for (size_t i = 0; i < a[0].size(); ++i) d = std::max(d, std::abs(a[0][i] - b[0][i]));
        check(d == 0.f, "soft clipper leaves quiet signals untouched", fmt("max diff %.1e", d));
    }
    {
        Rig r; r.param("mb", 0.f); r.param("depth", 0.f); r.param("clip", 2.f);
        r.render(loud);
        const float pk = peak(r.render(loud));
        check(pk > 1.2f, "clipper only acts in multiband mode", fmt("peak %.2f", pk));
    }
}

static void testDynamicEqMode()
{
    section("3f. Multiband, Dynamic EQ type (zero latency, moving shelves)");
    const auto in = noise(48000, 31);
    for (float bands : { 0.f, 1.f })
    {
        const char* n = bands > 0.5f ? "3 bands" : "2 bands";
        Rig r; r.param("mb", 1.f); r.param("mbEngine", 1.f); r.param("mbBands", bands);
        for (const char* id : { "depthL", "depthM", "depth" "H" }) r.param(id, 0.f);
        r.param("linkDepth", 0.f);
        const auto out = r.render(in);
        check(maxDiff(out[0], in[0]) == 0.f && maxDiff(out[1], in[1]) == 0.f, fmt("%s, curves open: output equals the input bit for bit (no phase shift)", n),
              fmt("max diff %.1e", maxDiff(out[0], in[0])));
    }
    {   // the filter on its own: equal gains are a plain volume change, and no setting of the gains ever boosts a tone
        bool equalExact = true; double worstBoost = 0.0;
        const float levels[] = { 0.f, 0.25f, 0.5f, 1.f };
        for (bool three : { false, true })
            for (float gl : levels) for (float gm : levels) for (float gh : levels)
            {
                const float top = std::max({ gl, three ? gm : 0.f, gh });
                if (top == 0.f) continue;
                for (double hz : { 40.0, 120.0, 200.0, 350.0, 700.0, 1500.0, 2500.0, 4000.0, 10000.0 })
                {
                    DynamicEq e; e.setFrequencies(200.0, 2500.0, 48000.0);
                    const auto m = DynamicEq::mix(three, gl, gm, gh);
                    double peak = 0.0;
                    for (int i = 0; i < 12000; ++i)
                    {
                        const double x = std::sin(2.0 * 3.14159265358979323846 * hz * i / 48000.0), y = e.process(0, x, three, m);
                        if (i > 6000) peak = std::max(peak, std::fabs(y));
                        if (gl == gh && (!three || gl == gm) && y != (double) gh * x) equalExact = false;
                    }
                    worstBoost = std::max(worstBoost, db(peak / top));
                }
            }
        check(equalExact, "all bands at the same gain: exactly that gain times the input");
        check(worstBoost < 0.1, "no combination of band gains boosts a tone", fmt("worst %+.3f dB above the loudest band's gain", worstBoost));
    }
    {
        auto level = [](double hz, const char* solo) {
            Rig r; r.param("mb", 1.f); r.param("mbEngine", 1.f); r.param("linkDepth", 0.f); r.param("depthL", 0.f); r.param("depthM", 0.f); r.param("depth" "H", 0.f);
            r.param(solo, 1.f);
            const auto t = sine(48000, hz, 48000.0);
            return db(rms(r.render(t)[0], 24000) / rms(t[0], 24000));
        };
        const double lowAt50 = level(50.0, "soloL"), lowAt8k = level(8000.0, "soloL"), highAt50 = level(50.0, "soloH"), highAt8k = level(8000.0, "soloH"), midAt700 = level(700.0, "soloM");
        check(lowAt50 > -1.0 && lowAt8k < -50.0, "solo low keeps the lows and removes the highs", fmt("50 Hz %+.2f dB, 8 kHz %+.1f dB", lowAt50, lowAt8k));
        check(highAt8k > -1.0 && highAt50 < -50.0, "solo high keeps the highs and removes the lows", fmt("8 kHz %+.2f dB, 50 Hz %+.1f dB", highAt8k, highAt50));
        check(midAt700 > -3.0, "solo mid keeps the middle (200 Hz / 2.5 kHz splits)", fmt("700 Hz %+.2f dB", midAt700));
    }
    {
        Rig r; r.param("mb", 1.f); r.param("mbEngine", 1.f);
        const auto out = r.render(in);
        check(allFinite(out[0]) && maxDiff(out[0], in[0]) > 0.05f, "the default curves duck the audio");
        Rig s; s.param("mb", 1.f);
        const auto split = s.render(in);
        check(maxDiff(out[0], split[0]) > 0.01f, "the two multiband types really are different signal paths");
    }
}

static void testTrims()
{
    section("3g. IN / OUT gain knobs");
    const auto in = noise(48000, 41, 0.1f);
    auto gainOf = [](const Stereo& out, const Stereo& src) { return db(rms(out[0], 24000) / rms(src[0], 24000)); };
    {
        Rig r; r.param("depth", 0.f);
        const auto a = r.render(in);
        check(maxDiff(a[0], in[0]) == 0.f && maxDiff(a[1], in[1]) == 0.f, "both at 0 dB: the audio is not touched");
    }
    for (const char* id : { "inGain", "outGain" })
        for (float mb : { 0.f, 1.f })
        {
            Rig r; r.param("mb", mb); r.param("mbEngine", 1.f); r.param("depth", 0.f);
            for (const char* d : { "depthL", "depthM", "depth" "H" }) r.param(d, 0.f);
            r.param(id, -6.f);
            r.render(in);
            const double g = gainOf(r.render(in), in);
            check(std::fabs(g + 6.0) < 0.01, fmt("%s at -6 dB, %s: level drops 6 dB", id, mb > 0.5f ? "multiband" : "single band"), fmt("%+.3f dB", g));
            r.param(id, 0.f);
            r.render(in);
            const auto back = r.render(in);
            check(maxDiff(back[0], in[0]) == 0.f, fmt("%s back at 0 dB: bit-exact again", id));
        }
    {
        const auto steady = dc(9600, 0.25f);
        Rig r; r.param("depth", 0.f);
        r.render(steady);
        r.param("outGain", 12.f);
        const auto o = r.render(steady);
        check(maxStep(o[0], 1, o[0].size()) < 0.005f, "a 12 dB jump of a knob glides without a click", fmt("largest step %.5f", maxStep(o[0], 1, o[0].size())));
    }
    {
        Rig r; r.param("mb", 1.f); r.param("clip", 2.f); r.param("outGain", 24.f);
        const auto o = r.render(noise(48000, 42, 0.8f));
        check(maxAbs(o[0], 4800) <= 1.0001f, "OUT sits before the clipper: the 0 dB ceiling holds with OUT at +24 dB", fmt("peak %.4f", maxAbs(o[0], 4800)));
    }
    {
        Rig r; r.param("inGain", -12.f); r.param("outGain", 12.f); r.param("bypass", 1.f);
        const auto o = r.render(in);
        check(maxDiff(o[0], in[0], 1000) == 0.f, "bypass skips both knobs");
    }
}

// ---------------------------------------------------------------------------------------------
// 4. Block-size independence
// ---------------------------------------------------------------------------------------------
static void testBlockSizes()
{
    section("4. The result must not depend on how the host chops up the audio");
    const auto in = noise(48000, 8);
    struct Config { const char* name; std::function<void(Rig&)> setup; };
    const Config configs[] = {
        { "single band, Sync 1/4, smooth 2 ms", [](Rig& r) { r.param("smooth", 2.f); } },
        { "single band, Hz 3.3, offset 0.2, smooth 5 ms", [](Rig& r) { r.param("mode", 1.f); r.param("hz", 3.3f); r.param("offset", 0.2f); r.param("smooth", 5.f); } },
        { "single band, Sync 1/8 triplet", [](Rig& r) { r.param("division", 6.f); r.param("triplet", 1.f); } },
        { "multiband, linked rates", [](Rig& r) { r.param("mb", 1.f); r.param("smoothM", 3.f); } },
        { "multiband, Dynamic EQ", [](Rig& r) { r.param("mb", 1.f); r.param("mbEngine", 1.f); r.param("smoothM", 3.f); } },
        { "multiband, separate Hz rates, 48 dB slopes", [](Rig& r) {
              r.param("mb", 1.f); r.param("mbLink", 0.f); r.param("slopeLow", 3.f); r.param("slopeHigh", 3.f);
              r.param("modeL", 1.f); r.param("hzL", 1.5f); r.param("modeM", 1.f); r.param("hzM", 4.f); r.param("divH", 7.f); } },
    };
    const std::vector<std::vector<int>> patterns = { { 1 }, { 3 }, { 7, 64, 13 }, { 100 }, { 1031 }, { 37, 5000, 2 }, { 4096 } };
    for (const auto& c : configs)
    {
        Rig refRig; c.setup(refRig);
        const auto ref = refRig.render(in, { 8192 });
        float worst = 0.f;
        for (const auto& pat : patterns)
        {
            Rig r; c.setup(r);
            const auto out = r.render(in, pat);
            worst = std::max({ worst, maxDiff(out[0], ref[0]), maxDiff(out[1], ref[1]) });
        }
        check(worst < 2e-5f, c.name, fmt("worst difference between block sizes 1..8192: %.0f dB", db(worst)));
    }
}

// ---------------------------------------------------------------------------------------------
// 5. Clicks
// ---------------------------------------------------------------------------------------------
static void testSmoothingSlew()
{
    section("5a. Smoothing really limits how fast the gain can move (no clicks inside a curve)");
    for (float ms : { 1.f, 5.f, 20.f })
    {
        Rig r; r.param("smooth", ms);
        const auto out = r.render(dc(48000 * 3, 1.f), std::vector<int> { 64, 511, 3, 200 });
        const double bound = (1.0 - std::exp(-1.0 / (ms * 0.001 * 48000.0)));
        const float step = maxStep(out[0], 0, out[0].size());
        check(step <= (float) (bound * 1.02 + 1e-6), fmt("smooth %.0f ms", ms), fmt("largest gain step %.5f, allowed %.5f", step, bound));
    }
}

struct ClickCase { const char* name; std::function<void(Rig&)> setup; std::function<void(Rig&)> event; };

// Renders the same signal twice (with and without the event) and compares the worst "spike" (second difference)
static double clickRatio(const ClickCase& c, double sineHz, bool useDc = false)
{
    const auto in = useDc ? dc(48000 * 2, 1.f) : sine(48000 * 2, sineHz, 48000.0, 0.5f);
    constexpr int blockSize = 256, eventBlock = 187; // ~1 s in
    auto run = [&](bool withEvent) {
        Rig r; r.param("smooth", 0.f); r.allCurves(triangleCurve());
        c.setup(r);
        return r.render(in, blockSize, [&](int b) { if (withEvent && b == eventBlock) c.event(r); });
    };
    const auto base = run(false), ev = run(true);
    const size_t from = (size_t) eventBlock * blockSize - 4, to = from + 6000;
    if (useDc) return (double) maxStep(ev[0], from, to);     // DC in = the gain itself: its biggest single step
    return (double) maxSecondDiff(ev[0], from, to) / std::max(1e-6, (double) maxSecondDiff(base[0], from, to));
}

static void testClicks()
{
    section("5b. Parameter changes while playing must not click");
    std::printf("      (a 1 kHz sine is rendered with and without the change; the spike ratio compares the loudest kink.\n"
                "       Before the declick fix these were 7 to 47; a click-free change stays near 1-2. Single-band changes are\n"
                "       also checked on DC, where the output IS the gain: no step may exceed 0.01 = a 3 ms glide.)\n");
    const ClickCase cases[] = {
        { "rate 1/4 -> 1/8",          [](Rig&) {}, [](Rig& r) { r.param("division", 6.f); } },
        { "rate 1/4 -> 8 bars",       [](Rig&) {}, [](Rig& r) { r.param("division", 0.f); } },
        { "dotted on",                [](Rig&) {}, [](Rig& r) { r.param("dotted", 1.f); } },
        { "triplet on",               [](Rig&) {}, [](Rig& r) { r.param("triplet", 1.f); } },
        { "Sync -> Hz",               [](Rig&) {}, [](Rig& r) { r.param("mode", 1.f); } },
        { "Hz 2 -> 9 (knob turn)",    [](Rig& r) { r.param("mode", 1.f); r.param("hz", 2.f); }, [](Rig& r) { r.param("hz", 9.f); } },
        { "offset 0 -> 0.4",          [](Rig&) {}, [](Rig& r) { r.param("offset", 0.4f); } },
        { "depth 100% -> 20%",        [](Rig&) {}, [](Rig& r) { r.param("depth", 0.2f); } },
        { "depth 100% -> 0%",         [](Rig&) {}, [](Rig& r) { r.param("depth", 0.f); } },
        { "host jumps +1.37 beats",   [](Rig&) {}, [](Rig& r) { r.host.ppq += 1.37; } },
        { "host jumps back (loop)",   [](Rig&) {}, [](Rig& r) { r.host.ppq -= 1.9; } },
        { "host stops",               [](Rig&) {}, [](Rig& r) { r.host.playing = false; } },
        { "host tempo 130 -> 90",     [](Rig&) {}, [](Rig& r) { r.host.bpm = 90.0; } },
        { "multiband: solo high on",  [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("soloH", 1.f); } },
        { "multiband: solo mid on",   [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("soloM", 1.f); } },
        { "multiband: mid depth 100% -> 10%", [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("depthM", 0.1f); } },
        { "multiband: rate 1/4 -> 1/16", [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("divM", 7.f); } },
        { "multiband: link off + mid rate change", [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("mbLink", 0.f); r.param("divM", 3.f); } },
        { "multiband: split 200 Hz -> 700 Hz", [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("xLow", 700.f); } },
        { "multiband: split 2.5 kHz -> 700 Hz", [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("xHigh", 700.f); } },
        { "multiband: slope 24 -> 96 dB/oct", [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("slopeLow", 5.f); r.param("slopeHigh", 5.f); } },
        { "multiband: Crossover -> Dynamic EQ", [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("mbEngine", 1.f); } },
        { "multiband: Dynamic EQ -> Crossover", [](Rig& r) { r.param("mb", 1.f); r.param("mbEngine", 1.f); }, [](Rig& r) { r.param("mbEngine", 0.f); } },
        { "multiband: Dynamic EQ mid depth 100% -> 10%", [](Rig& r) { r.param("mb", 1.f); r.param("mbEngine", 1.f); }, [](Rig& r) { r.param("depthM", 0.1f); } },
        { "multiband: Dynamic EQ split 200 Hz -> 700 Hz", [](Rig& r) { r.param("mb", 1.f); r.param("mbEngine", 1.f); }, [](Rig& r) { r.param("xLow", 700.f); } },
        { "multiband: Dynamic EQ solo low on", [](Rig& r) { r.param("mb", 1.f); r.param("mbEngine", 1.f); }, [](Rig& r) { r.param("soloL", 1.f); } },
        { "multiband: 3 -> 2 bands",  [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("mbBands", 0.f); } },
        { "switch Single -> Multiband", [](Rig&) {}, [](Rig& r) { r.param("mb", 1.f); } },
        { "switch Multiband -> Single", [](Rig& r) { r.param("mb", 1.f); }, [](Rig& r) { r.param("mb", 0.f); } },
    };
    for (const auto& c : cases)
    {
        const double ratio = clickRatio(c, 1000.0);
        const std::string name = c.name;
        const bool single = name.rfind("multiband", 0) != 0 && name.rfind("switch", 0) != 0 && name != "host tempo 130 -> 90";
        if (single)
        {
            const double step = clickRatio(c, 1000.0, true);
            check(ratio < 2.5 && step < 0.01, c.name, fmt("spike ratio %.2f, largest gain step %.4f", ratio, step));
        }
        else
            check(ratio < 2.5, c.name, fmt("spike ratio %.2f", ratio));
    }
}

// ---------------------------------------------------------------------------------------------
// 6. Stability
// ---------------------------------------------------------------------------------------------
static void testStability()
{
    section("6. Filter stability: the crossover can never ring forever or blow up (no feedback runaway)");
    bool allStable = true;
    double worstTail = -999.0;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int slope = 0; slope < 6; ++slope)
            for (double f1 : { 20.0, 40.0, 200.0, 1000.0 })
                for (double f2 : { 500.0, 2500.0, 12000.0, 0.45 * sr })
                {
                    Crossover x; x.prepare(sr); x.setSlopes((Slope) slope, (Slope) slope); x.setFrequencies(f1, f2);
                    const size_t n = (size_t) (sr * 3.0);
                    double tail = 0.0, peak = 0.0;
                    for (size_t i = 0; i < n; ++i)
                    {
                        double b[3];
                        x.process3(0, i == 0 ? 1.0 : 0.0, b);
                        const double s = b[0] + b[1] + b[2];
                        if (!std::isfinite(s)) { allStable = false; break; }
                        peak = std::max(peak, std::fabs(s));
                        if (i > n - (size_t) (0.5 * sr)) tail = std::max(tail, std::fabs(s));
                    }
                    worstTail = std::max(worstTail, db(tail));
                    if (peak > 2.0 || tail > 1e-6) allStable = false;
                }
    check(allStable, "impulse response decays to silence for every slope, split and sample rate (44.1 - 192 kHz)",
          fmt("loudest leftover after 2.5 s: %.0f dB", worstTail));

    Rig r(96000.0, 512); r.param("mb", 1.f); r.param("slopeLow", 5.f); r.param("slopeHigh", 5.f); r.param("xLow", 40.f);
    const auto out = r.render(noise(96000 * 6, 9, 1.0f));
    check(allFinite(out[0]) && allFinite(out[1]) && maxAbs(out[0]) < 4.f, "6 s of full-scale noise through the harshest multiband setting stays bounded",
          fmt("peak %.2f", maxAbs(out[0])));
}

// ---------------------------------------------------------------------------------------------
// 7. Robustness: bad input and odd hosts
// ---------------------------------------------------------------------------------------------
static void testRobustness()
{
    section("7. Robustness: NaN / Inf / huge input, odd block sizes, every sample rate");
    for (bool multiband : { false, true })
    {
        Rig r; r.param("mb", multiband ? 1.f : 0.f);
        Stereo in = noise(48000, 10);
        in[0][1000] = std::nanf(""); in[0][2000] = INFINITY; in[1][3000] = -INFINITY; in[0][4000] = 1e30f;
        const auto out = r.render(in, 256);
        size_t nonFinite = 0;
        for (size_t i = 0; i < out[0].size(); ++i) { if (!std::isfinite(out[0][i])) ++nonFinite; if (!std::isfinite(out[1][i])) ++nonFinite; }
        check(allFinite(out[0], 6000) && allFinite(out[1], 6000), multiband ? "multiband recovers from NaN / Inf in the input" : "single band recovers from NaN / Inf in the input",
              fmt("%zu non-finite output samples, none after the bad input has passed", nonFinite));
    }
    {
        Rig r; r.param("mb", 1.f);
        const auto out = r.render(noise(48000, 11), std::vector<int> { 1 });
        check(allFinite(out[0]) && allFinite(out[1]), "1-sample blocks (multiband)");
    }
    {
        Rig r; r.param("mb", 1.f);
        const auto in = noise(200000, 12);
        const auto out = r.render(in, std::vector<int> { 65536 });
        check(allFinite(out[0]) && allFinite(out[1]), "65536-sample blocks (multiband)");
    }
    {
        Rig r;
        juce::AudioBuffer<float> empty(2, 0); juce::MidiBuffer midi;
        r.p.processBlock(empty, midi);
        check(true, "a zero-length block is accepted");
    }
    for (double sr : { 22050.0, 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        Rig r(sr, 512); r.allCurves(flatCurve(1.f));
        const auto in = noise((size_t) sr, 13);
        const auto out = r.render(in);
        Rig m(sr, 512); m.param("mb", 1.f); m.allCurves(flatCurve(1.f));
        const auto outM = m.render(in);
        check(maxDiff(out[0], in[0]) == 0.f && allFinite(outM[0]) && std::fabs(db(rms(outM[0], (size_t) sr / 2) / rms(in[0], (size_t) sr / 2))) < 0.05,
              fmt("sample rate %.0f Hz: exact null, multiband transparent", sr));
    }
    {
        // Silence in: silence out, always, even with multiband and extreme settings
        Rig r; r.param("mb", 1.f); r.param("slopeLow", 5.f); r.param("xLow", 40.f);
        const auto out = r.render(dc(48000 * 2, 0.f), 128);
        check(maxAbs(out[0]) == 0.f && maxAbs(out[1]) == 0.f, "silence in gives exactly silence out (multiband, 96 dB slopes)");
    }
}

// ---------------------------------------------------------------------------------------------
// 8. Fuzz: random automation of everything
// ---------------------------------------------------------------------------------------------
static void testFuzz()
{
    section("8. Fuzz: every parameter randomised every block, many block sizes and sample rates");
    struct Combo { double sr; int block; };
    for (auto c : { Combo { 44100.0, 64 }, Combo { 48000.0, 513 }, Combo { 96000.0, 1000 }, Combo { 192000.0, 32 } })
    {
        Rig r(c.sr, c.block);
        std::mt19937 rng(77);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        bool finite = true, bounded = true, singleNeverLouder = true;
        const auto params = r.p.getParameters();
        long settledSamples = 0; // samples since the signal path last changed (a switch makes a short dip)
        auto structure = [&] {
            auto v = [&](const char* id) { return (int) r.p.apvts.getRawParameterValue(id)->load(); };
            return v("bypass") * 10000 + v("mbEngine") * 100000 + v("mb") * 1000 + v("mbBands") * 100 + v("slopeLow") * 10 + v("slopeHigh");   // bypass counts: while bypassed the path switch is paused, so un-bypassing fades through the old path
        };
        int lastStructure = structure();
        for (int b = 0; b < 3000; ++b)
        {
            for (auto* prm : params) if (u(rng) < 0.15f)
            {
                const float nv = u(rng);
                auto* pp = dynamic_cast<juce::RangedAudioParameter*>(prm);
                if (pp != nullptr && (pp->paramID == "inGain" || pp->paramID == "outGain")) continue;   // they may boost, which this test forbids; tested in 3g
                prm->setValueNotifyingHost(nv);
            }
            if (structure() != lastStructure) { lastStructure = structure(); settledSamples = 0; }
            // sometimes yank the transport around too
            if (u(rng) < 0.05f) r.host.ppq = u(rng) * 400.0;
            if (u(rng) < 0.03f) r.host.bpm = 40.0 + u(rng) * 250.0;
            if (u(rng) < 0.03f) r.host.playing = !r.host.playing;

            juce::AudioBuffer<float> buf(2, c.block);
            juce::MidiBuffer midi;
            if (u(rng) < 0.1f) midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), (int) (u(rng) * (c.block - 1)));
            Sig original((size_t) c.block);
            for (int i = 0; i < c.block; ++i)
            {
                const float v = (u(rng) * 2.f - 1.f);
                buf.setSample(0, i, v); buf.setSample(1, i, v); original[(size_t) i] = v;
            }
            r.p.processBlock(buf, midi);
            const bool mb = r.p.apvts.getRawParameterValue("mb")->load() > 0.5f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < c.block; ++i)
                {
                    const float o = buf.getSample(ch, i);
                    if (!std::isfinite(o)) finite = false;
                    if (std::fabs(o) > 4.f) bounded = false;
                    if (!mb && settledSamples > (long) (0.1 * c.sr) && std::fabs(o) > std::fabs(original[(size_t) i]) * 1.000001f + 1e-9f) singleNeverLouder = false;
                }
            settledSamples += c.block;
            if (r.host.playing) r.host.ppq += (double) c.block * r.host.bpm / 60.0 / c.sr;
        }
        check(finite && bounded, fmt("%.0f Hz, blocks of %d: output always finite and below 4x full scale", c.sr, c.block));
        check(singleNeverLouder, fmt("%.0f Hz, blocks of %d: single band is never louder than its input", c.sr, c.block));
    }
}

// ---------------------------------------------------------------------------------------------
// 9. Timing at 130 bpm
// ---------------------------------------------------------------------------------------------
static void testTiming()
{
    section("9. Timing: locked to the 130 bpm transport, sample accurate");
    struct Rate { const char* name; int division; bool dotted, triplet; };
    for (auto rate : { Rate { "1/4", 5, false, false }, Rate { "1/8 triplet", 6, false, true }, Rate { "1/2 dotted", 4, true, false }, Rate { "1 bar", 3, false, false }, Rate { "1/16", 7, false, false } })
        for (double startPpq : { 0.0, 7.3 })
        {
            Rig r; r.param("smooth", 0.f); r.allCurves(triangleCurve());
            r.param("division", (float) rate.division); r.param("dotted", rate.dotted ? 1.f : 0.f); r.param("triplet", rate.triplet ? 1.f : 0.f);
            r.host.ppq = startPpq;
            const auto out = r.render(dc(48000 * 3, 1.f), 512);
            const double beats = FreeLfoProcessor::divisionBeats[(size_t) rate.division] * FreeLfoProcessor::rateModifier(rate.dotted, rate.triplet);
            const auto curve = triangleCurve();
            float worst = 0.f;
            for (size_t i = 0; i < out[0].size(); ++i)
            {
                const double ppq = startPpq + (double) i * kTestBpm / 60.0 / 48000.0;
                // the plugin reads its curve at the start of each block and then steps per sample, so compare to the same arithmetic
                const double blockStart = startPpq + (double) (i / 512 * 512) * kTestBpm / 60.0 / 48000.0;
                const double phase = blockStart / beats + (double) (i % 512) * kTestBpm / 60.0 / 48000.0 / beats;
                (void) ppq;
                worst = std::max(worst, std::fabs(out[0][i] - curve.evaluate((float) (phase - std::floor(phase)))));
            }
            check(worst < 3e-3f, fmt("%s starting at beat %.1f, 130 bpm", rate.name, startPpq), fmt("worst deviation from the exact LFO position %.5f", worst));
        }
    {
        // The LFO position only depends on the song position: same beat at a different tempo = same gain
        Rig a, b; a.param("smooth", 0.f); b.param("smooth", 0.f); a.allCurves(triangleCurve()); b.allCurves(triangleCurve());
        a.host.ppq = b.host.ppq = 12.34; a.host.bpm = 130.0; b.host.bpm = 77.0;
        const auto oa = a.render(dc(64, 1.f), 64), ob = b.render(dc(64, 1.f), 64);
        check(std::fabs(oa[0][0] - ob[0][0]) < 1e-6f, "same song position at 130 bpm and 77 bpm gives the same LFO position");
    }
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Rig r(sr, 512); r.param("mode", 1.f); r.param("hz", 2.f); r.param("smooth", 0.f); r.allCurves(triangleCurve());
        const auto out = r.render(dc((size_t) (sr * 4), 1.f), 512);
        std::vector<double> ups;
        for (size_t i = 1; i < out[0].size(); ++i) if (out[0][i - 1] < 0.75f && out[0][i] >= 0.75f && out[0][i] > out[0][i - 1]) ups.push_back((double) i);
        double period = 0.0;
        if (ups.size() >= 3) period = (ups.back() - ups.front()) / (double) (ups.size() - 1);
        check(ups.size() >= 6 && std::fabs(period - sr / 2.0) < 1.5, fmt("Hz mode: 2 Hz at %.0f Hz sample rate", sr), fmt("period %.2f samples, expected %.2f", period, sr / 2.0));
    }
    {
        // MIDI retrigger restarts the cycle at the exact sample of the note
        Rig r; r.param("mode", 1.f); r.param("hz", 2.f); r.param("retrigger", 1.f); r.param("smooth", 0.f); r.allCurves(triangleCurve());
        const auto out = r.render(dc(24000, 1.f), std::vector<int> { 512 }, {}, [](juce::MidiBuffer& m, int block) {
            if (block == 12) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 100);
        });
        const size_t note = 12 * 512 + 100;
        // The restart is sample-exact; the gain glides to the new position over a few ms so there is no click
        const size_t later = note + 1000;                      // 21 ms later the glide is over: the gain follows the restarted cycle
        const float expected = 0.5f + 0.5f * (1000.f / 24000.f) / 0.25f;
        check(out[0][note - 1] > 0.9f && std::fabs(out[0][later] - expected) < 0.01f && maxStep(out[0], note - 2, note + 400) < 0.03f,
              "MIDI note restarts the LFO at its sample, without a click (Hz mode, retrigger on)",
              fmt("gain before %.3f, 21 ms after %.3f (expected %.3f), largest step %.4f", out[0][note - 1], out[0][later], expected, maxStep(out[0], note - 2, note + 400)));
    }
}

// ---------------------------------------------------------------------------------------------
// 10. Sidechain bus, state, real-time safety, threads
// ---------------------------------------------------------------------------------------------
static void testSidechainBus()
{
    section("10. The sidechain input is for display only: it never leaks into the output");
    for (int type : { 0, 1, 2 })
    {
        const bool multiband = type > 0;
        const float engine = type == 2 ? 1.f : 0.f;
        const auto a = noise(48000, 14), b = noise(48000, 15, 1.0f);
        Rig plain; plain.param("mb", multiband ? 1.f : 0.f); plain.param("mbEngine", engine);
        const auto expect = plain.render(a);

        Rig r; r.param("mb", multiband ? 1.f : 0.f); r.param("mbEngine", engine);
        r.p.editorOpen.store(true);   // the display analysis (incl. the kick split in Multiband) must not touch the audio either
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses = { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo() };
        layout.outputBuses = { juce::AudioChannelSet::stereo() };
        const bool accepted = r.p.setBusesLayout(layout);
        r.p.prepareToPlay(48000.0, 512);
        Stereo out { Sig(48000), Sig(48000) };
        if (accepted)
            for (int pos = 0; pos < 48000; pos += 512)
            {
                const int size = std::min(512, 48000 - pos);
                juce::AudioBuffer<float> buf(4, size); juce::MidiBuffer midi;
                for (int i = 0; i < size; ++i)
                {
                    buf.setSample(0, i, a[0][(size_t) (pos + i)]); buf.setSample(1, i, a[1][(size_t) (pos + i)]);
                    buf.setSample(2, i, b[0][(size_t) (pos + i)]); buf.setSample(3, i, b[1][(size_t) (pos + i)]);
                }
                r.p.processBlock(buf, midi);
                for (int i = 0; i < size; ++i) { out[0][(size_t) (pos + i)] = buf.getSample(0, i); out[1][(size_t) (pos + i)] = buf.getSample(1, i); }
                r.host.ppq += (double) size * kTestBpm / 60.0 / 48000.0;
            }
        check(accepted && maxDiff(out[0], expect[0]) == 0.f && maxDiff(out[1], expect[1]) == 0.f,
              type == 2 ? "multiband Dynamic EQ: output identical with and without a loud sidechain" : multiband ? "multiband: output identical with and without a loud sidechain" : "single band: output identical with and without a loud sidechain");
    }
}

static void testState()
{
    section("11. Saving and loading a project restores everything");
    Rig a;
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    for (auto* prm : a.p.getParameters()) prm->setValueNotifyingHost(u(rng));
    a.param("mb", 1.f);
    Envelope e; e.points = { { 0.f, 0.3f }, { 0.4f, 0.9f, -0.1f, 0.1f, 0.1f, 0.f, 0.3f, 0.4f }, { 1.f, 0.3f } };
    for (int s = 0; s < FreeLfoProcessor::numSlots; ++s) { a.p.setEnvelope(e, s); e.points[1].x += 0.05f; }
    juce::MemoryBlock block;
    a.p.getStateInformation(block);

    Rig b;
    b.p.setStateInformation(block.getData(), (int) block.getSize());
    const auto in = noise(48000, 16);
    const auto oa = a.render(in), ob = b.render(in);
    bool curvesMatch = true;
    for (int s = 0; s < FreeLfoProcessor::numSlots; ++s) curvesMatch = curvesMatch && a.p.getEnvelope(s).serialize() == b.p.getEnvelope(s).serialize();
    check(curvesMatch, "all four curves restored exactly");
    check(maxDiff(oa[0], ob[0]) == 0.f && maxDiff(oa[1], ob[1]) == 0.f, "a restored instance sounds identical to the original");
}

static void testPointLimitState()
{
    section("11b. Saved projects and the 64-point limit");
    auto many = [](int n) {
        Envelope m; m.points.clear();
        for (int i = 0; i < n; ++i) m.points.push_back({ (float) i / (float) (n - 1), i % 2 == 0 ? 0.2f : 0.8f });
        m.normalize();
        return m;
    };
    {
        Rig a; for (int s = 0; s < FreeLfoProcessor::numSlots; ++s) a.p.setEnvelope(many(150), s);
        juce::MemoryBlock block; a.p.getStateInformation(block);
        Rig b; b.p.setStateInformation(block.getData(), (int) block.getSize());
        bool same = true;
        for (int s = 0; s < FreeLfoProcessor::numSlots; ++s) same = same && b.p.getEnvelope(s).serialize() == many(150).serialize();
        const auto in = noise(48000, 51);
        const auto oa = a.render(in), ob = b.render(in);
        check(same && maxDiff(oa[0], ob[0]) == 0.f, "a project with 150 points per curve loads unchanged and sounds the same");
    }
    {
        Rig a; for (int s = 0; s < FreeLfoProcessor::numSlots; ++s) a.p.setEnvelope(many(2000), s);
        juce::MemoryBlock block; a.p.getStateInformation(block);
        Rig b; b.p.setEnvelope(many(7), 0); b.p.setStateInformation(block.getData(), (int) block.getSize());
        bool defaults = b.p.getEnvelope(0).serialize() == Envelope().serialize();
        for (int band = 0; band < 3; ++band) { Envelope d; d.resetToBandPump(band); defaults = defaults && b.p.getEnvelope(band + 1).serialize() == d.serialize(); }
        check(defaults, "damaged data (over 512 points) falls back to each curve's own default");
    }
}

// A kick on the sidechain, locked to the transport: the whole path from the audio to a fitted curve and back to audio.
static void testCurveFromKick()
{
    section("11d. Curve from kick (sidechain)");
    using kickfit::Status;
    constexpr int bs = 256;
    const double sr = 48000.0, beat = 60.0 / kTestBpm;
    struct KickRig
    {
        Rig r;
        long t = 0;
        bool sidechainOk = false;
        KickRig()
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses = { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo() };
            layout.outputBuses = { juce::AudioChannelSet::stereo() };
            sidechainOk = r.p.setBusesLayout(layout);
            r.p.prepareToPlay(48000.0, 512);
        }
        bool plucks = false;   // false: a constant 0.5 signal; true: a plucked 220 Hz tone on every 16th
        // plays `seconds` of that signal with a kick on every beat of the sidechain; returns the output
        Sig play(double seconds, float kickLevel = 0.9f)
        {
            Sig out;
            const double beatSeconds = 60.0 / kTestBpm;
            for (int done = 0; done < (int) (seconds * 48000.0); done += bs)
            {
                juce::AudioBuffer<float> buf(4, bs);
                juce::MidiBuffer midi;
                for (int i = 0; i < bs; ++i, ++t)
                {
                    const double into = std::fmod((double) t / 48000.0, beatSeconds);
                    const float k = kickLevel * (float) (std::sin(2.0 * juce::MathConstants<double>::pi * (50.0 * into + 2.4 * (1.0 - std::exp(-into / 0.02)))) * std::exp(-into / 0.09));
                    const float in = plucks ? (float) (0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * (double) t / 48000.0)
                                                       * std::exp(-std::fmod(into, beatSeconds / 4.0) / 0.05))
                                            : 0.5f;
                    buf.setSample(0, i, in); buf.setSample(1, i, in); buf.setSample(2, i, k); buf.setSample(3, i, k);
                }
                r.p.processBlock(buf, midi);
                for (int i = 0; i < bs; ++i) out.push_back(buf.getSample(0, i));
                r.host.ppq += (double) bs * kTestBpm / 60.0 / 48000.0;
            }
            return out;
        }
    };
    auto curves = [](FreeLfoProcessor& p) { std::string s; for (int k = 0; k < FreeLfoProcessor::numSlots; ++k) s += p.getEnvelope(k).serialize() + "|"; return s; };
    auto status = [](FreeLfoProcessor& p, int slot) { return p.fitCurveToKick(slot).status; };

    {
        Rig plain;
        plain.p.editorOpen.store(true);
        plain.render(noise(48000, 3));
        check(status(plain.p, 0) == Status::noSidechain, "without a sidechain it says so and changes nothing");
    }

    for (float offset : { 0.f, 0.25f })
    {
        KickRig k;
        k.r.param("offset", offset);
        k.r.p.editorOpen.store(true);
        const auto before = curves(k.r.p);
        k.play(beat * 0.5);
        const bool early = status(k.r.p, 0) == Status::notReady && curves(k.r.p) == before;
        k.play(beat * 2.0);
        const auto res = k.r.p.fitCurveToKick(0);
        const bool made = k.sidechainOk && res.status == Status::ok && curves(k.r.p) != before;
        check(early && made, fmt("offset %.2f: refused before one full cycle, fits after", offset), fmt("%d points", (int) res.curve.points.size()));

        // the point of it all: a steady signal is now ducked while the kick hits and back at full level before the next one
        k.play(beat * 3.0);
        const long startT = k.t;
        const auto out = k.play(beat * 4.0);
        float onHit = 0.f, between = 1.f;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const double into = std::fmod((double) (startT + (long) i) / sr, beat);
            if (into > 0.005 && into < 0.025) onHit = std::max(onHit, std::fabs(out[i]));
            if (into > 0.75 * beat && into < 0.90 * beat) between = std::min(between, std::fabs(out[i]));
        }
        check(onHit < 0.05f && between > 0.47f, fmt("offset %.2f: the output ducks on the kick and recovers", offset),
              fmt("0.5 in: at most %.3f on the hit, at least %.3f between hits", onHit, between));
        k.r.p.undo();
        check(curves(k.r.p) == before, fmt("offset %.2f: one undo brings the old curve back", offset));
    }

    // Safe and Precise at full depth (the usual setting) and at low depth, heard through the plugin
    for (float depth : { 1.f, 0.25f })
        for (bool plucks : { false, true })
        {
            double kept[2] = {};
            for (int style = 0; style < 2; ++style)
            {
                KickRig k;
                k.plucks = plucks;
                k.r.param("depth", depth);
                k.r.p.editorOpen.store(true);
                k.play(beat * 3.0);
                const auto res = k.r.p.fitCurveToKick(0, style == 0 ? kickfit::Style::safe : kickfit::Style::precise);
                k.play(beat * 3.0);
                const long startT = k.t;
                const auto out = k.play(beat * 8.0);
                double energy = 0.0, full = 0.0;
                float onHit = 0.f;
                for (size_t i = 0; i < out.size(); ++i)
                {
                    const double sec = (double) (startT + (long) i) / sr, into = std::fmod(sec, beat);
                    const double in = plucks ? 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * sec) * std::exp(-std::fmod(into, beat / 4.0) / 0.05) : 0.5;
                    energy += (double) out[i] * out[i];
                    full += in * in;
                    if (into > 0.004 && into < 0.012) onHit = std::max(onHit, std::fabs(out[i]));
                }
                kept[style] = 100.0 * energy / full;
                // on the kick's loudest moment the gain is 1 - depth
                const float expected = 0.5f * (1.f - depth);
                const char* name = style == 0 ? "Safe" : "Precise";
                check(res.status == Status::ok && onHit < expected + 0.04f && (plucks || onHit > expected - 0.01f),
                      fmt("%s, depth %.2f, %s: ducked by the depth on the hit", name, depth, plucks ? "plucked bass" : "steady signal"),
                      fmt("%d points, peak on the hit %.3f (expected about %.3f), %.0f%% of the energy kept", (int) res.curve.points.size(), onHit, expected, kept[style]));
            }
            check(plucks ? kept[1] > kept[0] + (depth > 0.5f ? 5.0 : 1.0) : kept[1] > kept[0] - 3.0,
                  fmt("depth %.2f, %s: Precise keeps %s", depth, plucks ? "plucked bass" : "steady signal", plucks ? "more of the sound than Safe" : "about as much as Safe or more"),
                  fmt("Safe %.0f%%, Precise %.0f%%", kept[0], kept[1]));
        }

    {
        KickRig k;
        k.r.p.editorOpen.store(true);
        k.play(beat * 3.0);
        k.r.param("division", 4.f); // half-length cycles: what was recorded no longer matches
        k.play(0.05);
        const bool stale = status(k.r.p, 0) == Status::notReady;
        k.r.param("division", 5.f);
        k.play(beat * 3.0);
        check(stale && status(k.r.p, 0) == Status::ok, "after a rate change it waits for a new full cycle");
        k.r.p.editorOpen.store(false);
        k.play(0.1);
        k.r.p.editorOpen.store(true);
        check(status(k.r.p, 0) == Status::notReady, "nothing recorded while the window was closed is trusted");
        k.play(beat * 3.0, 0.f);
        const auto before = curves(k.r.p);
        check(status(k.r.p, 0) == Status::quiet && curves(k.r.p) == before, "a silent sidechain is reported and changes nothing");
        k.r.param("division", 3.f); // one bar: four kicks
        k.play(beat * 9.0);
        const auto res = k.r.p.fitCurveToKick(0);
        check(res.status == Status::severalHits && res.hits == 4 && curves(k.r.p) == before, "four kicks in one cycle are counted and refused", fmt("%d hits", res.hits));
    }

    for (float engine : { 0.f, 1.f })
    {
        KickRig k;
        k.r.param("mb", 1.f); k.r.param("mbEngine", engine);
        k.r.p.editorOpen.store(true);
        k.play(beat * 4.0);
        const auto before = curves(k.r.p);
        const auto low = k.r.p.fitCurveToKick(1);
        const bool onlyLow = low.status == Status::ok && k.r.p.getEnvelope(0).serialize() + "|" == before.substr(0, k.r.p.getEnvelope(0).serialize().size() + 1)
                             && k.r.p.getEnvelope(1).evaluate(0.01f) < 0.05f;
        const auto afterLow = curves(k.r.p);
        const auto high = k.r.p.fitCurveToKick(3);
        const char* type = engine > 0.5f ? "Dynamic EQ" : "Crossover";
        check(onlyLow, fmt("multiband %s: the low band gets a curve from the kick's lows", type));
        check(high.status == Status::bandQuiet && curves(k.r.p) == afterLow, fmt("multiband %s: a band with hardly any kick is left alone", type));
        check(status(k.r.p, 0) == Status::notReady, fmt("multiband %s: the single curve has no recording", type));
    }
}

// Linked multiband controls drive one band's parameter: the DAW must hear it called "All Bands ...".
static void testLinkedNames()
{
    section("11e. Linked multiband controls are named \"All Bands\" for the DAW");
    Rig r;
    struct Listener : juce::AudioProcessorListener
    {
        int renames = 0;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& d) override { if (d.parameterInfoChanged) ++renames; }
    } listener;
    r.p.addListener(&listener);
    auto name = [&r](const char* id) { return r.p.apvts.getParameter(id)->getName(128); };
    auto expect = [&](const char* what, std::initializer_list<std::pair<const char*, const char*>> wanted) {
        bool good = true;
        juce::String got;
        for (const auto& w : wanted) { good = good && name(w.first) == w.second; got << name(w.first) << "; "; }
        check(good, what, got.toStdString());
    };

    r.param("mbLink", 0.f);
    r.p.refreshParameterNames();
    listener.renames = 0;
    expect("nothing linked: every parameter is named after its band",
           { { "depthL", "Low Depth" }, { "depthM", "Mid Depth" }, { "depthH", "High Depth" }, { "divM", "Mid Rate" } });
    r.param("linkDepth", 1.f);
    r.p.refreshParameterNames();
    expect("Depth linked, 3 bands: the Mid parameter is All Bands Depth, nothing else changes",
           { { "depthM", "All Bands Depth" }, { "depthL", "Low Depth" }, { "smoothM", "Mid Smooth (ms)" }, { "divM", "Mid Rate" } });
    const bool told = listener.renames == 1;
    r.p.refreshParameterNames();
    check(told && listener.renames == 1, "the DAW is told once per change", fmt("%d notices", listener.renames));
    r.param("mbBands", 0.f);
    r.p.refreshParameterNames();
    expect("Depth linked, 2 bands: the Low parameter is the shared one", { { "depthL", "All Bands Depth" }, { "depthM", "Mid Depth" } });
    r.param("mbBands", 1.f);
    r.param("mbLink", 1.f); r.param("linkSmooth", 1.f); r.param("linkOffset", 1.f);
    r.p.refreshParameterNames();
    expect("everything linked: rate, mode, dotted, triplet, Hz, smooth and offset too",
           { { "divM", "All Bands Rate" }, { "modeM", "All Bands Mode" }, { "dotM", "All Bands Dotted" }, { "tripM", "All Bands Triplet" },
             { "hzM", "All Bands Frequency" }, { "smoothM", "All Bands Smooth (ms)" }, { "offsetM", "All Bands Phase Offset" }, { "divL", "Low Rate" } });
    r.param("mbLink", 0.f); r.param("linkDepth", 0.f); r.param("linkSmooth", 0.f); r.param("linkOffset", 0.f);
    listener.renames = 0;
    r.p.refreshParameterNames();
    expect("unlinking brings the band names back", { { "depthM", "Mid Depth" }, { "divM", "Mid Rate" } });
    r.param("mbBands", 0.f);
    r.p.refreshParameterNames();
    check(listener.renames == 1, "with nothing linked, changing the band count sends no notice", fmt("%d notices", listener.renames));
    r.p.removeListener(&listener);
}

static void testBadValues()
{
    section("11c. A setting or curve that turns into NaN / Inf never breaks the audio, or leaves it broken");
    const auto in = noise(24000, 61);
    auto allOk = [](const Stereo& s) { return allFinite(s[0]) && allFinite(s[1]) && maxAbs(s[0]) < 400.f && maxAbs(s[1]) < 400.f; };
    const float bad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    struct Mode { const char* name; float mb, engine, hz; };
    const Mode modes[] = { { "single band", 0.f, 0.f, 0.f }, { "single band, Hz", 0.f, 0.f, 1.f }, { "multiband crossover", 1.f, 0.f, 0.f },
                           { "multiband Dynamic EQ", 1.f, 1.f, 0.f }, { "multiband, Hz, unlinked", 1.f, 0.f, 1.f } };
    for (const auto& m : modes)
    {
        std::string broke, stuck;
        Rig probe;
        for (auto* prm : probe.p.getParameters())
        {
            auto* ranged = dynamic_cast<juce::AudioParameterFloat*>(prm);
            if (ranged == nullptr) continue;
            const auto id = ranged->paramID;
            for (float v : bad)
            {
                Rig r;
                r.param("mb", m.mb); r.param("mbEngine", m.engine); r.param("mode", m.hz); r.param("smooth", 5.f);
                for (const char* sfx : { "L", "M", "H" }) { r.param((juce::String("mode") + sfx).toRawUTF8(), m.hz); r.param((juce::String("smooth") + sfx).toRawUTF8(), 5.f); }
                if (m.hz > 0.f) r.param("mbLink", 0.f);
                r.render(in);
                auto* raw = r.p.apvts.getRawParameterValue(id);
                const float good = raw->load();
                raw->store(v);   // however it got there: typed text, damaged project data, a misbehaving host
                if (!allOk(r.render(in))) broke += " " + id.toStdString();
                raw->store(good);
                r.render(in);
                if (!allOk(r.render(in))) stuck += " " + id.toStdString();
            }
        }
        check(broke.empty(), fmt("%s: output stays valid while any one setting is NaN / Inf", m.name), broke);
        check(stuck.empty(), fmt("%s: and is valid again once the setting is", m.name), stuck);
    }
    {
        Envelope loaded = Envelope::deserialize("0,nan,0,0,0,0;0.5,inf,nan,0,0,nan;nan,1,0,nan,0,0,0,nan;1,nan,0,0,0,0;");
        bool finite = true;
        for (const auto& pt : loaded.points)
            for (float v : { pt.x, pt.y, pt.inDx, pt.inDy, pt.outDx, pt.outDy, pt.bend, pt.bendX }) finite = finite && std::isfinite(v);
        Rig r; r.p.setEnvelope(loaded, 0);
        check(finite && allOk(r.render(in)), "curve text containing nan / inf (pasted, or in a damaged project) gives a valid curve");
    }
    {
        Rig a; juce::MemoryBlock block; a.p.getStateInformation(block);
        auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), (int) block.getSize());
        for (auto* node : xml->getChildIterator())
            if (node->hasAttribute("value")) node->setAttribute("value", "nan");
        juce::MemoryBlock damaged; juce::AudioProcessor::copyXmlToBinary(*xml, damaged);
        Rig b; b.p.setStateInformation(damaged.getData(), (int) damaged.getSize());
        bool finiteParams = true;
        for (auto* prm : b.p.getParameters()) finiteParams = finiteParams && std::isfinite(prm->getValue());
        check(finiteParams && allOk(b.render(in)), "a project whose every setting reads nan loads with usable settings");
    }
}

static void testRealTimeSafety()
{
    section("12. Real-time safety: no memory allocation on the audio thread");
    for (int type : { 0, 1, 2 })
        for (bool editorOpen : { false, true })
        for (bool bypassed : { false, true })
        {
            const bool multiband = type > 0;
            Rig r; r.param("mb", multiband ? 1.f : 0.f); r.param("mbEngine", type == 2 ? 1.f : 0.f); r.param("bypass", bypassed ? 1.f : 0.f); r.p.editorOpen.store(editorOpen);
            juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 10);
            for (int i = 0; i < 200; ++i) { for (int k = 0; k < 512; ++k) { buf.setSample(0, k, 0.1f); buf.setSample(1, k, 0.1f); } r.p.processBlock(buf, midi); }
            t_isAudioThread = true; g_allocs = 0; g_otherThreadAllocs = 0; g_countAllocs = true;
            for (int i = 0; i < 2000; ++i) r.p.processBlock(buf, midi);
            g_countAllocs = false; t_isAudioThread = false;
            check(g_allocs.load() == 0, fmt("%s, editor %s%s", type == 2 ? "multiband Dynamic EQ" : multiband ? "multiband" : "single band", editorOpen ? "open" : "closed", bypassed ? ", bypassed" : ""),
                  fmt("%ld allocations in 2000 blocks (%ld by other threads, not counted)", g_allocs.load(), g_otherThreadAllocs.load()));
        }

    // the bypass crossfade itself (switching on and off, every few blocks) must not allocate either
    for (bool multiband : { false, true })
    {
        Rig r; r.param("mb", multiband ? 1.f : 0.f); r.p.editorOpen.store(true);
        juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
        for (int i = 0; i < 200; ++i) { for (int k = 0; k < 512; ++k) { buf.setSample(0, k, 0.1f); buf.setSample(1, k, 0.1f); } r.p.processBlock(buf, midi); }
        t_isAudioThread = true; g_allocs = 0; g_otherThreadAllocs = 0;
        for (int i = 0; i < 2000; ++i)
        {
            if (i % 5 == 0) { g_countAllocs = false; r.param("bypass", (i / 5) % 2 == 0 ? 1.f : 0.f); }   // the switch itself is a UI-side call
            g_countAllocs = true;
            r.p.processBlock(buf, midi);
        }
        g_countAllocs = false; t_isAudioThread = false;
        check(g_allocs.load() == 0, multiband ? "multiband, bypass switching on and off" : "single band, bypass switching on and off",
              fmt("%ld allocations in 2000 blocks", g_allocs.load()));
    }
}

static void testThreads()
{
    section("13. Editing the curve and parameters from the UI thread while audio runs");
    Rig r; r.param("mb", 1.f);
    std::atomic<bool> stop { false };
    std::thread editor([&] {
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        const auto params = r.p.getParameters();
        while (!stop.load())
        {
            Envelope e;
            const int n = 2 + (int) (u(rng) * 8);
            e.points.clear();
            for (int i = 0; i < n; ++i) e.points.push_back({ (float) i / (float) (n - 1), u(rng), -u(rng) * 0.1f, (u(rng) - 0.5f), u(rng) * 0.1f, (u(rng) - 0.5f) });
            e.normalize();
            for (int s = 0; s < FreeLfoProcessor::numSlots; ++s) r.p.setEnvelope(e, s);
            for (auto* prm : params) if (u(rng) < 0.1f)
            {
                auto* pp = dynamic_cast<juce::RangedAudioParameter*>(prm);
                if (pp != nullptr && (pp->paramID == "inGain" || pp->paramID == "outGain")) continue;   // up to +48 dB would trip the runaway limit
                prm->setValueNotifyingHost(u(rng));
            }
        }
    });
    bool finite = true;
    double worstMs = 0.0, totalMs = 0.0;
    int blocks = 0;
    const auto start = std::chrono::steady_clock::now();
    juce::AudioBuffer<float> buf(2, 256); juce::MidiBuffer midi;
    std::mt19937 rng(9);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(3))
    {
        for (int k = 0; k < 256; ++k) { buf.setSample(0, k, u(rng)); buf.setSample(1, k, u(rng)); }
        const auto t0 = std::chrono::steady_clock::now();
        r.p.processBlock(buf, midi);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        worstMs = std::max(worstMs, ms); totalMs += ms; ++blocks;
        for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < 256; ++k) if (!std::isfinite(buf.getSample(ch, k)) || std::fabs(buf.getSample(ch, k)) > 6.f) finite = false;
        r.host.ppq += 256.0 * kTestBpm / 60.0 / 48000.0;
    }
    stop = true; editor.join();
    check(finite, "no NaN / runaway output while the curve and parameters change constantly", fmt("%d blocks", blocks));
    check(worstMs < (kLenientTiming ? 100.0 : 10.0), "the audio thread never stalls waiting for the editor", fmt("average %.4f ms, worst %.3f ms per 256-sample block (budget 5.3 ms)", totalMs / blocks, worstMs));
}

// ---------------------------------------------------------------------------------------------
// 14. CPU
// ---------------------------------------------------------------------------------------------
static double timeBlocks(Rig& r, int block, const Stereo& source, int count)
{
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    auto fill = [&] { for (int k = 0; k < block; ++k) { buf.setSample(0, k, source[0][(size_t) k]); buf.setSample(1, k, source[1][(size_t) k]); } };
    for (int i = 0; i < 100; ++i) { fill(); r.p.processBlock(buf, midi); }
    double total = 0.0;
    for (int i = 0; i < count; ++i)
    {
        fill();
        r.host.ppq += (double) block * kTestBpm / 60.0 / r.sr;
        const auto t0 = std::chrono::steady_clock::now();
        r.p.processBlock(buf, midi);
        total += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    return 100.0 * (total / count) / ((double) block / r.sr);   // percent of one core
}

static void testCpu()
{
    section("14. CPU (share of ONE core; lower is better)");
    std::printf("      %-34s %9s %9s %9s %9s\n", "", "44.1 kHz", "48 kHz", "96 kHz", "192 kHz");
    struct Mode { const char* name; std::function<void(Rig&)> setup; bool editor; };
    const Mode modes[] = {
        { "single band, editor closed", [](Rig&) {}, false },
        { "single band, editor open", [](Rig&) {}, true },
        { "multiband 24 dB/oct, editor closed", [](Rig& r) { r.param("mb", 1.f); }, false },
        { "multiband 96 dB/oct, editor closed", [](Rig& r) { r.param("mb", 1.f); r.param("slopeLow", 5.f); r.param("slopeHigh", 5.f); }, false },
        { "multiband Dynamic EQ, editor closed", [](Rig& r) { r.param("mb", 1.f); r.param("mbEngine", 1.f); }, false },
        { "multiband Dynamic EQ, editor open", [](Rig& r) { r.param("mb", 1.f); r.param("mbEngine", 1.f); }, true },
        { "multiband 96 dB/oct, editor open", [](Rig& r) { r.param("mb", 1.f); r.param("slopeLow", 5.f); r.param("slopeHigh", 5.f); }, true },
    };
    double worst = 0.0;
    for (int block : { 32, 512 })
    {
        std::printf("    blocks of %d samples\n", block);
        for (const auto& m : modes)
        {
            std::printf("      %-34s", m.name);
            for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                Rig r(sr, block); m.setup(r); r.p.editorOpen.store(m.editor);
                const double pct = timeBlocks(r, block, noise((size_t) block, 17), block == 32 ? 20000 : 4000);
                worst = std::max(worst, pct);
                std::printf(" %8.3f%%", pct);
            }
            std::printf("\n");
        }
    }
    check(worst < (kLenientTiming ? 20.0 : 5.0), "worst case across all of the above stays under 5% of one core", fmt("worst %.3f%%", worst));

    // Denormals: a signal that decays into denormal numbers must not make the CPU spike
    Rig n; n.param("mb", 1.f); n.param("slopeLow", 5.f); n.param("slopeHigh", 5.f);
    Rig d; d.param("mb", 1.f); d.param("slopeLow", 5.f); d.param("slopeHigh", 5.f);
    const double normal = timeBlocks(n, 256, noise(256, 18, 0.5f), 4000);
    const double tiny = timeBlocks(d, 256, noise(256, 19, 1e-39f), 4000);
    check(tiny < normal * (kLenientTiming ? 5.0 : 2.5), "denormal-range input costs no more CPU than normal input (flush-to-zero works)", fmt("normal %.3f%%, denormal input %.3f%%", normal, tiny));
}

static void testTypedDivisions()
{
    using P = FreeLfoProcessor;
    check(P::parseDivisionText("1/8") == 6 && P::parseDivisionText(" 1/4 D") == 5 && P::parseDivisionText("1/16T") == 7, "typed rates: 1/8, 1/4 D, 1/16T");
    check(P::parseDivisionText("8") == 6 && P::parseDivisionText("1") == 3 && P::parseDivisionText("1 bar") == 3 && P::parseDivisionText("2 bars") == 2, "typed rates: 8, 1, 1 bar, 2 bars");
    check(P::parseDivisionText("banana") == -1 && P::parseDivisionText("") == -1, "typed rates: nonsense is rejected");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::printf("Free LFO Tool audio tests (fake DAW at %.0f bpm)\n", kTestBpm);
    testTypedDivisions();
    testNull();
    testSilence();
    testMultibandTransparent();
    testKnobLinks();
    testMeters();
    testSpectrum();
    testBypass();
    testClipper();
    testDynamicEqMode();
    testTrims();
    testBlockSizes();
    testSmoothingSlew();
    testClicks();
    testStability();
    testRobustness();
    testFuzz();
    testTiming();
    testSidechainBus();
    testState();
    testPointLimitState();
    testCurveFromKick();
    testLinkedNames();
    testBadValues();
    testRealTimeSafety();
    testThreads();
    testCpu();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
