#pragma once
// Pure C++ (no JUCE): the zero-latency "Dynamic EQ" multiband mode.
//
// Instead of splitting the audio and adding the bands back together (Crossover.h), this turns frequency ranges down
// with moving shelves. Each split is one 2nd-order state-variable filter (the standard trapezoidal "TPT" design).
// Its coefficients and state depend only on the split frequency; the band gains only change how its outputs are mixed
// with the input:
//
//   y = gM * x  +  (gL - gM) * lp1  +  (sqrt(gL gM) - gM) * k bp1          (low shelf at split 1)
//               +  (gH - gM) * hp2  +  (sqrt(gH gM) - gM) * k bp2          (high shelf at split 2, 3 bands only)
//
// (with 2 bands, gM is the high band's gain and the second line is absent). So:
//   - all gains at 1: every extra term is exactly zero and the output is the input, bit for bit (no phase shift)
//   - all gains equal to g: the output is exactly g * x
//   - gains can move every sample, down to 0, with nothing to destabilise (no divisions, no retuning)
//   - phase only shifts while the bands are at different levels
// The price: gentle slopes (about 12 dB/oct), and with 3 bands and close splits the mid band reaches less deep.
#include <cmath>
#include <complex>

class DynamicEq
{
public:
    void setFrequencies(double f1, double f2, double sampleRate)
    {
        const double maxF = 0.45 * sampleRate;
        f1 = f1 < 20.0 ? 20.0 : f1 > maxF ? maxF : f1;
        f2 = f2 < f1 * 1.2 ? f1 * 1.2 : f2;
        if (f2 > maxF) f2 = maxF;
        a.set(f1, sampleRate);
        b.set(f2, sampleRate);
    }

    void reset() { a.reset(); b.reset(); }

    // The gain-independent mix weights, worked out once per sample and shared by both channels
    struct Mix
    {
        double base, low, lowEdge, high, highEdge;
    };
    static Mix mix(bool three, double gLow, double gMid, double gHigh)
    {
        if (!three) return { gHigh, gLow - gHigh, std::sqrt(gLow * gHigh) - gHigh, 0.0, 0.0 };
        return { gMid, gLow - gMid, std::sqrt(gLow * gMid) - gMid, gHigh - gMid, std::sqrt(gHigh * gMid) - gMid };
    }

    // parts (optional, for the display): the low / mid / high share of the input; they add up to it
    double process(int ch, double x, bool three, const Mix& m, double* parts = nullptr)
    {
        double lp1, bp1, hp1, lp2 = 0.0, bp2 = 0.0, hp2 = 0.0;
        a.tick(ch, x, lp1, bp1, hp1);
        if (three) b.tick(ch, x, lp2, bp2, hp2);
        if (parts != nullptr)
        {
            parts[0] = lp1;
            parts[1] = three ? x - lp1 - hp2 : 0.0;
            parts[2] = three ? hp2 : x - lp1;
        }
        return m.base * x + m.low * lp1 + m.lowEdge * bp1 + m.high * hp2 + m.highEdge * bp2;
    }

    // For drawing: |response| with only one band open (0 = low, 1 = mid, 2 = high), from the filter's analog shape
    static double soloMagnitude(int band, double hz, double f1, double f2, bool three, double sampleRate)
    {
        const double pi = 3.14159265358979323846;
        auto warp = [&](double f) { return std::tan(pi * (f < 0.499 * sampleRate ? f : 0.499 * sampleRate) / sampleRate); };
        auto section = [&](double fc, std::complex<double>& lp, std::complex<double>& bp, std::complex<double>& hp) {
            const std::complex<double> s(0.0, warp(hz) / warp(fc)), d = s * s + Svf::k * s + 1.0;
            lp = 1.0 / d; bp = Svf::k * s / d; hp = s * s / d;
        };
        std::complex<double> lp1, bp1, hp1, lp2, bp2, hp2;
        section(f1, lp1, bp1, hp1);
        if (band == 0) return std::abs(lp1);
        if (!three) return std::abs(hp1);
        section(f2, lp2, bp2, hp2);
        return band == 2 ? std::abs(hp2) : std::abs(1.0 - lp1 - bp1 - hp2 - bp2);
    }

private:
    struct Svf
    {
        static constexpr double k = 1.4142135623730951; // 1 / Q, Butterworth
        double a1 = 1, a2 = 0, a3 = 0;
        double s1[2] = { 0, 0 }, s2[2] = { 0, 0 };

        void set(double fc, double sr)
        {
            const double g = std::tan(3.14159265358979323846 * fc / sr);
            a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        // bp comes out already multiplied by k, so lp + bp + hp = x
        void tick(int ch, double x, double& lp, double& bp, double& hp)
        {
            const double v3 = x - s2[ch], v1 = a1 * s1[ch] + a2 * v3, v2 = s2[ch] + a2 * s1[ch] + a3 * v3;
            s1[ch] = 2.0 * v1 - s1[ch];
            s2[ch] = 2.0 * v2 - s2[ch];
            lp = v2; bp = k * v1; hp = x - bp - lp;
        }
        void reset() { s1[0] = s1[1] = s2[0] = s2[1] = 0.0; }
    };
    Svf a, b;
};
