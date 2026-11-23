// Build & run:  clang++ -std=c++17 tests/crossover_test.cpp -o /tmp/xotest && /tmp/xotest
#include "../Source/Crossover.h"
#include <cassert>
#include <cstdio>
#include <vector>

// Sine amplitude from RMS (peak-picking misses the crest when the sample grid doesn't hit it)
static double amplitudeOf(const std::vector<double>& v, size_t from)
{
    double acc = 0;
    for (size_t i = from; i < v.size(); ++i) acc += v[i] * v[i];
    return std::sqrt(2.0 * acc / (double) (v.size() - from));
}

int main()
{
    const double sr = 48000.0;
    const int N = 48000;
    const Slope slopes[6] = { Slope::dB6, Slope::dB12, Slope::dB24, Slope::dB48, Slope::dB72, Slope::dB96 };
    const char* names[6] = { "6 dB", "12 dB", "24 dB", "48 dB", "72 dB", "96 dB" };
    double leak[6] = {};

    for (int si = 0; si < 6; ++si)
    {
        const Slope slope = slopes[si];
        auto make = [&](double f1, double f2) { Crossover x; x.prepare(sr); x.setSlope(slope); x.setFrequencies(f1, f2); return x; };

        // 1) Three bands recombine to a flat magnitude, at frequencies on both sides of both splits
        for (double f : { 30.0, 100.0, 200.0, 500.0, 1000.0, 2500.0, 6000.0, 12000.0 })
        {
            auto x = make(200.0, 2500.0);
            std::vector<double> sum(N);
            for (int i = 0; i < N; ++i)
            {
                double in = std::sin(2.0 * M_PI * f * i / sr), b[3];
                x.process3(0, in, b);
                sum[(size_t) i] = b[0] + b[1] + b[2];
            }
            const double a = amplitudeOf(sum, N / 2);
            if (std::fabs(a - 1.0) >= 0.01) std::printf("FAIL %s 3-band sum at %.0f Hz: %.4f\n", names[si], f, a);
            assert(std::fabs(a - 1.0) < 0.01);
        }

        // 2) Two bands recombine flat too
        for (double f : { 50.0, 200.0, 1000.0, 8000.0 })
        {
            auto x = make(200.0, 2500.0);
            std::vector<double> sum(N);
            for (int i = 0; i < N; ++i)
            {
                double in = std::sin(2.0 * M_PI * f * i / sr), b[3];
                x.process2(0, in, b);
                sum[(size_t) i] = b[0] + b[1] + b[2];
            }
            assert(std::fabs(amplitudeOf(sum, N / 2) - 1.0) < 0.01);
        }

        // 3) Level of each band at its crossover: -6 dB for Linkwitz-Riley, -3 dB for the 6 dB pair
        {
            auto x = make(1000.0, 8000.0);
            std::vector<double> lo(N), mid(N);
            for (int i = 0; i < N; ++i) { double b[3]; x.process3(0, std::sin(2.0 * M_PI * 1000.0 * i / sr), b); lo[(size_t) i] = b[0]; mid[(size_t) i] = b[1]; }
            const double expect = slope == Slope::dB6 ? 0.7071 : 0.5012;
            std::printf("%s: band level at the crossover: low %.3f, mid %.3f (expect ~%.3f)\n", names[si], amplitudeOf(lo, N / 2), amplitudeOf(mid, N / 2), expect);
            assert(std::fabs(amplitudeOf(lo, N / 2) - expect) < 0.02);
            assert(std::fabs(amplitudeOf(mid, N / 2) - expect) < 0.02);
        }

        // 4) How much of an 800 Hz tone leaks into the mid band when the low/mid split is at 200 Hz (2 octaves above... wait: 3 octaves)
        {
            auto x = make(1000.0, 8000.0);
            std::vector<double> hi(N);
            for (int i = 0; i < N; ++i) { double b[3]; x.process3(0, std::sin(2.0 * M_PI * 125.0 * i / sr), b); hi[(size_t) i] = b[1]; }
            leak[si] = amplitudeOf(hi, N / 2); // a 125 Hz tone, three octaves below the split, in the mid band
            std::printf("%s: 125 Hz tone leaking into the mid band (split 1 kHz): %.5f\n", names[si], leak[si]);
        }
    }

    // Mixed slopes (different slope per split) still recombine flat
    {
        Crossover x; x.prepare(sr); x.setSlopes(Slope::dB96, Slope::dB6); x.setFrequencies(200.0, 2500.0);
        for (double f : { 60.0, 300.0, 1500.0, 9000.0 })
        {
            x.reset();
            std::vector<double> sum(N);
            for (int i = 0; i < N; ++i) { double b[3]; x.process3(0, std::sin(2.0 * M_PI * f * i / sr), b); sum[(size_t) i] = b[0] + b[1] + b[2]; }
            assert(std::fabs(amplitudeOf(sum, N / 2) - 1.0) < 0.01);
        }
    }

    // The drawn response curves agree with the real filters: -6 dB (LR) or -3 dB (6 dB/oct) at the split,
    // and steeper slopes are lower one octave away
    {
        double octaveAbove[6];
        for (int si = 0; si < 6; ++si)
        {
            Splitter sp; sp.set(1000.0, sr, slopes[si]);
            const double expect = slopes[si] == Slope::dB6 ? 0.7071 : 0.5012;
            assert(std::fabs(sp.lowMagnitude(1000.0, sr) - expect) < 0.01);
            assert(std::fabs(sp.highMagnitude(1000.0, sr) - expect) < 0.01);
            assert(sp.lowMagnitude(30.0, sr) > 0.99 && sp.highMagnitude(15000.0, sr) > 0.97);
            octaveAbove[si] = sp.lowMagnitude(2000.0, sr);
        }
        assert(octaveAbove[0] > octaveAbove[2] && octaveAbove[2] > octaveAbove[3] && octaveAbove[3] > octaveAbove[4] && octaveAbove[4] > octaveAbove[5]);
    }

    // Steeper slopes must leak less
    for (int i = 1; i < 6; ++i) assert(leak[i - 1] > leak[i]);

    std::puts("crossover tests passed");
    return 0;
}
