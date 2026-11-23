#pragma once
// Pure C++ (no JUCE): band splitter with selectable slope, so it can be unit-tested anywhere.
//
//   6 dB/oct  : first-order complementary pair (low + high = the input exactly)
//   12 dB/oct : Linkwitz-Riley 2 (high side inverted so low + high is an allpass)
//   24 dB/oct : Linkwitz-Riley 4 (two cascaded Butterworth biquads per side)
//   48 dB/oct : Linkwitz-Riley 8 (Butterworth 4th order, cascaded twice per side)
//   72 dB/oct : Linkwitz-Riley 12 (Butterworth 6th order, cascaded twice per side)
//   96 dB/oct : Linkwitz-Riley 16 (Butterworth 8th order, cascaded twice per side)
//
// In every case low + high has a flat magnitude response, so with all bands at unity the
// recombined signal has no gap or bump at the crossover (only a gentle phase shift).
// These are the standard textbook designs (Linkwitz & Riley, 1976); nothing here is
// specific to any product.
#include <algorithm>
#include <cmath>
#include <complex>

enum class Slope { dB6 = 0, dB12 = 1, dB24 = 2, dB48 = 3, dB72 = 4, dB96 = 5 };
constexpr int numSlopes = 6;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = { 0, 0 }, z2[2] = { 0, 0 };

    void setLowpass(double fc, double sr, double q)
    {
        const double w = 2.0 * M_PI * fc / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 - c) / 2.0 / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = -2.0 * c / a0;        a2 = (1.0 - al) / a0;
    }

    void setHighpass(double fc, double sr, double q)
    {
        const double w = 2.0 * M_PI * fc / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (1.0 + c) / 2.0 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
        a1 = -2.0 * c / a0;        a2 = (1.0 - al) / a0;
    }

    // First-order (bilinear transform); LP + HP of the same cutoff sums to exactly 1
    void setLowpass1(double fc, double sr)
    {
        const double k = std::tan(M_PI * fc / sr);
        b0 = k / (1.0 + k); b1 = b0; b2 = 0.0; a1 = (k - 1.0) / (1.0 + k); a2 = 0.0;
    }

    void setHighpass1(double fc, double sr)
    {
        const double k = std::tan(M_PI * fc / sr);
        b0 = 1.0 / (1.0 + k); b1 = -b0; b2 = 0.0; a1 = (k - 1.0) / (1.0 + k); a2 = 0.0;
    }

    // Transposed direct form II, double precision (low cutoffs are numerically touchy in float)
    double process(int ch, double x)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }

    // |H(e^jw)| at angular frequency w (radians per sample), for drawing the filter's response
    double magnitude(double w) const
    {
        const std::complex<double> e1 = std::polar(1.0, -w), e2 = std::polar(1.0, -2.0 * w);
        return std::abs(b0 + b1 * e1 + b2 * e2) / std::abs(1.0 + a1 * e1 + a2 * e2);
    }
};

// One low/high split at one frequency
class Splitter
{
public:
    void set(double fc, double sr, Slope s)
    {
        invertHigh = (s == Slope::dB12);
        switch (s)
        {
            case Slope::dB6:
                n = 1; lp[0].setLowpass1(fc, sr); hp[0].setHighpass1(fc, sr);
                break;
            case Slope::dB12:
                n = 1; lp[0].setLowpass(fc, sr, 0.5); hp[0].setHighpass(fc, sr, 0.5);
                break;
            default: // 24, 48, 72, 96 dB/oct: Linkwitz-Riley = a Butterworth filter of order N, cascaded twice
            {
                const int order = 2 * ((int) s - 1); // N = 2, 4, 6, 8
                n = order;
                for (int k = 0; k < order; ++k)
                {
                    // Butterworth section Qs: 1 / (2 sin((2j - 1) pi / (2N))), j = 1 .. N/2, used twice
                    const int j = k % (order / 2) + 1;
                    const double q = 1.0 / (2.0 * std::sin((2.0 * j - 1.0) * M_PI / (2.0 * order)));
                    lp[k].setLowpass(fc, sr, q);
                    hp[k].setHighpass(fc, sr, q);
                }
                break;
            }
        }
    }

    void split(int ch, double x, double& lo, double& hi)
    {
        double l = x, h = x;
        for (int k = 0; k < n; ++k) { l = lp[k].process(ch, l); h = hp[k].process(ch, h); }
        lo = l;
        hi = invertHigh ? -h : h;
    }

    // Magnitude response of each side at a frequency (used to draw the crossover curves)
    double lowMagnitude(double hz, double sr) const
    {
        const double w = 2.0 * M_PI * hz / sr;
        double m = 1.0;
        for (int k = 0; k < n; ++k) m *= lp[k].magnitude(w);
        return m;
    }

    double highMagnitude(double hz, double sr) const
    {
        const double w = 2.0 * M_PI * hz / sr;
        double m = 1.0;
        for (int k = 0; k < n; ++k) m *= hp[k].magnitude(w);
        return m;
    }

    // lo + hi is an allpass: used to phase-match a band that skips a later split
    double allpass(int ch, double x) { double l, h; split(ch, x, l, h); return l + h; }

    void reset() { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }

private:
    Biquad lp[8], hp[8];
    int n = 2;
    bool invertHigh = false;
};

// Low | mid | high with two split frequencies (or just low | high).
class Crossover
{
public:
    void prepare(double sampleRate) { sr = sampleRate; curValid = false; reset(); apply(); }

    void setFrequencies(double f1, double f2) { want1 = f1; want2 = f2; apply(); }
    void setSlope(Slope s) { wantSlope1 = wantSlope2 = s; apply(); }          // both splits
    void setSlopes(Slope lowSplit, Slope highSplit) { wantSlope1 = lowSplit; wantSlope2 = highSplit; apply(); }

    void reset() { a.reset(); b.reset(); c.reset(); }

    // out[0] = low, out[1] = mid, out[2] = high
    void process3(int ch, double x, double* out)
    {
        double lo, hi, mid, high;
        a.split(ch, x, lo, hi);
        b.split(ch, hi, mid, high);
        out[0] = c.allpass(ch, lo); // low also gets the 2nd split's phase shift, so the sum stays flat
        out[1] = mid;
        out[2] = high;
    }

    // out[0] = low, out[1] = 0, out[2] = high  (two bands: only the first split is used)
    void process2(int ch, double x, double* out)
    {
        double lo, hi;
        a.split(ch, x, lo, hi);
        out[0] = lo; out[1] = 0.0; out[2] = hi;
    }

private:
    void apply()
    {
        const double maxF = 0.45 * sr;
        const double f1 = std::clamp(want1, 20.0, maxF);
        const double f2 = std::clamp(want2, f1 * 1.2, maxF);
        if (curValid && f1 == cur1 && f2 == cur2 && wantSlope1 == curSlope1 && wantSlope2 == curSlope2) return;

        // a different filter structure can't inherit the old filter state
        if (!curValid || wantSlope1 != curSlope1) a.reset();
        if (!curValid || wantSlope2 != curSlope2) { b.reset(); c.reset(); }

        cur1 = f1; cur2 = f2; curSlope1 = wantSlope1; curSlope2 = wantSlope2; curValid = true;
        a.set(f1, sr, curSlope1);
        b.set(f2, sr, curSlope2);
        c.set(f2, sr, curSlope2);
    }

    Splitter a, b, c;
    double sr = 48000.0, want1 = 200.0, want2 = 2500.0, cur1 = 0.0, cur2 = 0.0;
    Slope wantSlope1 = Slope::dB24, wantSlope2 = Slope::dB24, curSlope1 = Slope::dB24, curSlope2 = Slope::dB24;
    bool curValid = false;
};
