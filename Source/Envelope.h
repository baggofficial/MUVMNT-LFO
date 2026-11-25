#pragma once
// Pure C++ (no JUCE) so it can be unit-tested anywhere.
//
// The curve is a chain of cubic Beziers, like After Effects keyframes: every point has an
// "in" handle (to its left) and an "out" handle (to its right), stored as offsets from the
// point. Zero handles give a straight line.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

struct EnvPoint
{
    float x = 0.f, y = 0.f;       // position, both 0..1
    float inDx = 0.f, inDy = 0.f;   // in handle offset  (inDx <= 0)
    float outDx = 0.f, outDy = 0.f; // out handle offset (outDx >= 0)

    // Simple mode: bends the segment that starts at this point with a power curve, which can be far
    // sharper than anything the Bezier handles reach. -1..1; > 0 = fast start, < 0 = slow start.
    float bend = 0.f;
    float bendX = 0.5f;             // where the single bend handle sits along that segment (0..1)
};

class Envelope
{
public:
    static constexpr int tableSize = 2048;
    // The most points a curve can be given (adding, pasting, preset files, mode conversion). Beyond this the editor
    // gets slow to redraw and the dots sit on top of each other.
    static constexpr size_t maxPoints = 64;
    // A saved project may hold more than maxPoints (made before there was a limit) and loads unchanged, so it keeps
    // its sound. Anything above this much is taken to be damaged data.
    static constexpr size_t loadCap = 512;

    std::vector<EnvPoint> points;

    Envelope() { resetToPump(); }

    // Fast-rising pump: drops to zero, springs back, then eases into full level.
    // The classic house sidechain pump, 6 points: the signal is fully ducked as the kick hits, held for a moment,
    // swells back up in a steep S, stays at full level, then drops sharply at the very end for the next kick.
    // First and last points share the same height (0), so the loop has no click at its seam.
    // Each point is { x, y, inDx, inDy, outDx, outDy }.
    void resetToPump()
    {
        points = { { 0.000f, 0.f,  0.f,      0.f,     0.044f, 0.f    },
                   { 0.133f, 0.f, -0.063f,   0.f,     0.096f, 0.f    },
                   { 0.250f, 0.5f, -0.004f, -0.357f,  0.003f, 0.234f },
                   { 0.500f, 1.f, -0.250f,   0.f,     0.063f, 0.f    },
                   { 0.940f, 1.f, -0.050f,   0.f,     0.061f, 0.f    },
                   { 1.000f, 0.f,  0.f,      1.f,     0.f,    0.f    } };
    }

    // Multiband defaults, one per band (0 = low, 1 = mid, 2 = high): the same pump, but the lows stay ducked longest and
    // the highs recover fastest, so the kick punches through. The shapes were fitted to a reference sketch (max error
    // under 1% of the height). Same 6-point layout and sharp drop at the end as the single-band default.
    void resetToBandPump(int band)
    {
        //                  x      y     inDx     inDy    outDx   outDy
        const EnvPoint start[3] = { { 0.000f, 0.f,  0.f,      0.f,  0.044f, 0.f },
                                    { 0.000f, 0.f,  0.f,      0.f,  0.044f, 0.f },
                                    { 0.000f, 0.f,  0.f,      0.f,  0.006f, 0.f } };
        const EnvPoint rise1[3] = { { 0.133f, 0.f, -0.063f,   0.f,  0.1571f, 0.0015f },   // low
                                    { 0.133f, 0.f, -0.063f,   0.f,  0.0579f, 0.0103f },   // mid
                                    { 0.020f, 0.f, -0.0067f,  0.f,  0.0745f, 0.0443f } }; // high
        const EnvPoint mid[3]   = { { 0.338f, 0.5f, -0.0085f, -0.0934f, 0.0368f, 0.3994f },
                                    { 0.270f, 0.5f, -0.0444f, -0.2812f, 0.0845f, 0.5000f },
                                    { 0.175f, 0.5f, -0.0381f, -0.2639f, 0.0297f, 0.1595f } };
        const EnvPoint top[3]   = { { 0.500f, 1.f, -0.0924f, -0.0011f, 0.063f, 0.f },
                                    { 0.500f, 1.f, -0.1136f,  0.0000f, 0.063f, 0.f },
                                    { 0.500f, 1.f, -0.3168f, -0.0009f, 0.063f, 0.f } };
        const int b = std::clamp(band, 0, 2);
        points = { start[b], rise1[b], mid[b], top[b],
                   { 0.940f, 1.f, -0.050f, 0.f, 0.061f, 0.f },     // end of the plateau
                   { 1.000f, 0.f,  0.f,    1.f, 0.f,    0.f } };   // sharp drop: back to where the loop started
    }

    void resetToFlat() { points = { { 0.f, 1.f }, { 1.f, 1.f } }; }

    // Handle positions as drawn/evaluated: x limited to the neighbouring segment, y to 0..1.
    std::pair<float, float> outHandle(size_t i) const
    {
        const auto& p = points[i];
        if (i + 1 >= points.size()) return { p.x, p.y };
        const float span = points[i + 1].x - p.x;
        // A handle that was never set sits a third of the way along the straight line to the next point. That is
        // the exact same (straight) curve as having no handle, but it gives every point, including the first
        // and last, a handle to see and grab straight away.
        if (p.outDx == 0.f && p.outDy == 0.f) return { p.x + span / 3.f, p.y + (points[i + 1].y - p.y) / 3.f };
        return { p.x + std::clamp(p.outDx, 0.f, span), std::clamp(p.y + p.outDy, 0.f, 1.f) };
    }

    std::pair<float, float> inHandle(size_t i) const
    {
        const auto& p = points[i];
        if (i == 0) return { p.x, p.y };
        const float span = p.x - points[i - 1].x;
        if (p.inDx == 0.f && p.inDy == 0.f) return { p.x - span / 3.f, p.y + (points[i - 1].y - p.y) / 3.f };
        return { p.x + std::clamp(p.inDx, -span, 0.f), std::clamp(p.y + p.inDy, 0.f, 1.f) };
    }

    // Simple-mode bend shape: an exponential ease. lambda > 0 rises fast and settles (ease-out), lambda < 0 is
    // slow and then abrupt (ease-in), 0 is a straight line. Unlike a power curve its knee stays rounded and
    // grows sharper only gradually (about logarithmically) as you drag, which keeps smooth curves easy to make.
    // bend (-1..1) maps to lambda in +-maxLambda; 400 is a near-vertical edge.
    static constexpr double maxLambda = 400.0;

    static double easeExp(double t, double lambda)
    {
        if (std::fabs(lambda) < 1e-4) return t;
        if (lambda < 0.0) return 1.0 - easeExp(1.0 - t, -lambda); // ease-in is the mirror image of ease-out
        return std::expm1(-lambda * t) / std::expm1(-lambda);
    }

    // Where the "simple mode" handle sits: on the curve, at its own position along the segment.
    std::pair<float, float> bendHandle(size_t i) const
    {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        const float x = a.x + a.bendX * (b.x - a.x);
        return { x, evaluate(x) };
    }

    // Simple mode: bend segment i with ONE handle dragged to (mx, my). The segment becomes a power curve
    // that passes through that point, and the Bezier handles of the segment are cleared so the two
    // modes never fight each other.
    void bendSegmentThrough(size_t i, float mx, float my)
    {
        auto& a = points[i];
        auto& b = points[i + 1];
        const float spanX = b.x - a.x, spanY = b.y - a.y;
        if (spanX < 1e-4f) return;

        a.outDx = a.outDy = 0.f;
        b.inDx = b.inDy = 0.f;
        a.bendX = std::clamp((mx - a.x) / spanX, 0.05f, 0.95f);
        if (std::fabs(spanY) < 1e-3f) { a.bend = 0.f; return; } // flat segment: nothing to bend

        // find the lambda whose curve passes through (bendX, ty); easeExp rises with lambda, so bisect
        const double ty = std::clamp((double) ((my - a.y) / spanY), 1e-6, 1.0 - 1e-6);
        double lo = -maxLambda, hi = maxLambda;
        for (int k = 0; k < 60; ++k)
        {
            const double mid = 0.5 * (lo + hi);
            if (easeExp(a.bendX, mid) < ty) lo = mid; else hi = mid;
        }
        a.bend = (float) std::clamp(0.5 * (lo + hi) / maxLambda, -1.0, 1.0);
    }

    // ---- Converting between the two editing styles, so the curve you see stays editable after a switch ----
    // Bezier handles -> one simple-mode bend, and back. Each conversion fits the new representation to the
    // old curve as closely as it can; shapes the target can't express (an S-curve has no single bend) are
    // approximated, and sharp bends are limited by what a cubic can do.

    bool segmentHasHandles(size_t i) const
    {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        return std::fabs(a.outDx) + std::fabs(a.outDy) + std::fabs(b.inDx) + std::fabs(b.inDy) > 1e-4f;
    }

    // Simple bend -> Bezier handles (segments without a bend are left alone)
    void convertSegmentToHandles(size_t i)
    {
        auto& a = points[i];
        auto& b = points[i + 1];
        const double span = b.x - a.x, dy = b.y - a.y;
        const bool bent = std::fabs(a.bend) > 1e-4f;
        if (!bent || span < 1e-4 || std::fabs(dy) < 1e-4) { a.bend = 0.f; a.bendX = 0.5f; return; }

        const double lam = (double) a.bend * maxLambda;
        constexpr int N = 48;
        double target[N];
        for (int k = 0; k < N; ++k) target[k] = a.y + dy * easeExp((k + 0.5) / N, lam);

        // start from the cubic that interpolates the curve at 1/3 and 2/3, then refine with a pattern search
        const double f1 = a.y + dy * easeExp(1.0 / 3.0, lam), f2 = a.y + dy * easeExp(2.0 / 3.0, lam);
        const double r1 = 27.0 * f1 - 8.0 * a.y - b.y, r2 = 27.0 * f2 - a.y - 8.0 * b.y;
        double th[4] = { 1.0 / 3.0, std::clamp(((2.0 * r1 - r2) / 18.0 - a.y) / dy, 0.0, 1.0),
                         1.0 / 3.0, std::clamp((b.y - (2.0 * r2 - r1) / 18.0) / dy, 0.0, 1.0) };

        auto cost = [&](const double* t) {
            double err = 0.0;
            for (int k = 0; k < N; ++k)
            {
                const double x = a.x + span * (k + 0.5) / N;
                const double y = bezierY(a.x, a.y, a.x + span * t[0], a.y + dy * t[1],
                                         b.x - span * t[2], b.y - dy * t[3], b.x, b.y, x);
                err += (y - target[k]) * (y - target[k]);
            }
            return err;
        };

        double best = cost(th), step = 0.25;
        for (int iter = 0; iter < 400 && step > 1e-3; ++iter)
        {
            bool improved = false;
            for (int p = 0; p < 4; ++p)
                for (double dir : { 1.0, -1.0 })
                {
                    double trial[4] = { th[0], th[1], th[2], th[3] };
                    trial[p] = std::clamp(th[p] + dir * step, 0.0, 1.0);
                    const double c = cost(trial);
                    if (c < best - 1e-12) { best = c; for (int q = 0; q < 4; ++q) th[q] = trial[q]; improved = true; }
                }
            if (!improved) step *= 0.5;
        }

        a.outDx = (float) (span * th[0]);
        a.outDy = (float) (dy * th[1]);
        b.inDx = (float) (-span * th[2]);
        b.inDy = (float) (-dy * th[3]);
        a.bend = 0.f;
        a.bendX = 0.5f;
    }

    // Bezier handles -> one simple-mode bend (segments without handles are left alone)
    void convertSegmentToBend(size_t i)
    {
        auto& a = points[i];
        auto& b = points[i + 1];
        if (!segmentHasHandles(i)) return;

        const double span = b.x - a.x, dy = b.y - a.y;
        const auto o = outHandle(i);
        const auto n = inHandle(i + 1);
        a.outDx = a.outDy = 0.f; // handles are consumed by this conversion (o / n already hold their values)
        b.inDx = b.inDy = 0.f;
        a.bendX = 0.5f;
        if (span < 1e-4 || std::fabs(dy) < 1e-4) { a.bend = 0.f; return; }

        constexpr int N = 48;
        double actual[N];
        for (int k = 0; k < N; ++k)
        {
            const double x = a.x + span * (k + 0.5) / N;
            actual[k] = (bezierY(a.x, a.y, o.first, o.second, n.first, n.second, b.x, b.y, x) - a.y) / dy;
            actual[k] = std::clamp(actual[k], 0.0, 1.0);
        }
        auto cost = [&](double lam) {
            double err = 0.0;
            for (int k = 0; k < N; ++k) { const double d = easeExp((k + 0.5) / N, lam) - actual[k]; err += d * d; }
            return err;
        };

        // coarse scan over sharpness (both signs), then refine between the neighbours of the best one
        const double grid[] = { 0.0, 0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 12.0, 20.0, 35.0, 60.0, 100.0, 200.0, maxLambda };
        constexpr int G = (int) (sizeof(grid) / sizeof(grid[0]));
        double values[2 * G - 1];
        for (int g = 0; g < G; ++g) { values[G - 1 + g] = grid[g]; values[G - 1 - g] = -grid[g]; }
        int bestIdx = 0;
        for (int v = 1; v < 2 * G - 1; ++v) if (cost(values[v]) < cost(values[bestIdx])) bestIdx = v;
        double lo = values[std::max(0, bestIdx - 1)], hi = values[std::min(2 * G - 2, bestIdx + 1)];
        for (int k = 0; k < 60; ++k)
        {
            const double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
            if (cost(m1) < cost(m2)) hi = m2; else lo = m1;
        }
        const double lam = 0.5 * (lo + hi);
        a.bend = std::fabs(lam) < 0.05 ? 0.f : (float) std::clamp(lam / maxLambda, -1.0, 1.0);
    }

    void convertToBezier() { for (size_t i = 0; i + 1 < points.size(); ++i) convertSegmentToHandles(i); }
    void convertToSimple() { for (size_t i = 0; i + 1 < points.size(); ++i) convertSegmentToBend(i); }

    // Like convertToSimple(), but a segment that one bend cannot follow (an S-shape, like half a sine) is first
    // split in two with de Casteljau (the curve itself does not change), so the result stays close to the original
    // shape instead of flattening to a line. Used for presets and pasted curves; the new points are ordinary points.
    Envelope simplifiedCopy(float tolerance = 0.02f) const
    {
        Envelope cur = *this;
        for (int pass = 0; pass < 6; ++pass)
        {
            Envelope trial = cur;
            trial.convertToSimple();
            int worst = -1;
            float worstErr = tolerance;
            for (size_t i = 0; i + 1 < trial.points.size(); ++i)
            {
                const float x0 = trial.points[i].x, x1 = trial.points[i + 1].x;
                if (x1 - x0 < 0.04f) continue;
                float err = 0.f;
                for (int k = 1; k < 24; ++k)
                {
                    const float x = x0 + (x1 - x0) * (float) k / 24.f;
                    err = std::max(err, std::fabs(trial.evaluate(x) - evaluate(x)));
                }
                if (err > worstErr) { worstErr = err; worst = (int) i; }
            }
            if (worst < 0 || cur.points.size() >= maxPoints) return trial; // never split past the limit
            cur.splitSegment((size_t) worst);
        }
        Envelope last = cur;
        last.convertToSimple();
        return last;
    }

    // Split segment i at its middle without changing the curve (de Casteljau on the segment's Bezier).
    void splitSegment(size_t i)
    {
        const EnvPoint a = points[i], b = points[i + 1];
        if (std::fabs(a.bend) > 1e-4f) return; // simple-mode segment: nothing to split
        const auto o = outHandle(i), n = inHandle(i + 1);
        auto mid = [](std::pair<float, float> p, std::pair<float, float> q) { return std::pair<float, float>{ 0.5f * (p.first + q.first), 0.5f * (p.second + q.second) }; };
        const std::pair<float, float> p0{ a.x, a.y }, p3{ b.x, b.y };
        const auto m01 = mid(p0, o), m12 = mid(o, n), m23 = mid(n, p3);
        const auto m012 = mid(m01, m12), m123 = mid(m12, m23);
        const auto m = mid(m012, m123);
        EnvPoint np;
        np.x = m.first; np.y = m.second;
        np.inDx = m012.first - m.first;  np.inDy = m012.second - m.second;
        np.outDx = m123.first - m.first; np.outDy = m123.second - m.second;
        points[i].outDx = m01.first - a.x;  points[i].outDy = m01.second - a.y;
        points[i + 1].inDx = m23.first - b.x; points[i + 1].inDy = m23.second - b.y;
        points.insert(points.begin() + (long) i + 1, np);
    }

    static double bezierY(double ax, double ay, double p1x, double p1y, double p2x, double p2y, double bx, double by, double x)
    {
        auto bz = [](double a, double b, double c, double d, double t) {
            const double u = 1.0 - t;
            return u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d;
        };
        double lo = 0.0, hi = 1.0, t = 0.5;
        for (int k = 0; k < 30; ++k) { t = 0.5 * (lo + hi); if (bz(ax, p1x, p2x, bx, t) < x) lo = t; else hi = t; }
        return bz(ay, p1y, p2y, by, t);
    }

    static float bezier(float a, float b, float c, float d, float t)
    {
        const float u = 1.f - t;
        return u * u * u * a + 3.f * u * u * t * b + 3.f * u * t * t * c + t * t * t * d;
    }

    float evaluate(float phase) const
    {
        if (points.empty()) return 1.f;
        if (points.size() == 1) return points[0].y;

        phase -= std::floor(phase);
        size_t i = 0;
        while (i + 2 < points.size() && phase >= points[i + 1].x) ++i;

        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (b.x - a.x <= 1e-6f) return b.y;

        const auto o = outHandle(i);
        const auto n = inHandle(i + 1);

        // x(t) is monotonic because both handle x's stay inside the segment, so bisect for t.
        float lo = 0.f, hi = 1.f, t = 0.5f;
        for (int k = 0; k < 24; ++k)
        {
            t = 0.5f * (lo + hi);
            if (bezier(a.x, o.first, n.first, b.x, t) < phase) lo = t; else hi = t;
        }
        // Bisection never lands exactly on t = 0, and with a very sharp bend even t = 1e-7 is already almost
        // at the top, so the first sample of a segment would come out wrong. Pin it to the true start value.
        if (phase <= a.x) t = 0.f;

        // Snap values within float rounding of 0 or 1 to exactly 0 / 1, so a flat curve at 100%
        // is a true unity gain (bit-exact pass-through) rather than 0.99999994.
        float v = bezier(a.y, o.second, n.second, b.y, t);
        const float dy = b.y - a.y;
        if (std::fabs(a.bend) > 1e-4f && std::fabs(dy) > 1e-6f)
        {
            const float u = std::clamp((v - a.y) / dy, 0.f, 1.f);
            v = a.y + dy * (float) easeExp((double) u, (double) a.bend * maxLambda);
        }
        v = std::clamp(v, 0.f, 1.f);
        return v > 0.999999f ? 1.f : (v < 1e-6f ? 0.f : v);
    }

    // Writes tableSize + 1 values (last one == value at phase 1) for cheap interpolated lookup.
    void bake(float* out) const
    {
        for (int i = 0; i <= tableSize; ++i)
            out[i] = (i == tableSize) ? points.back().y   // phase 1.0 is the curve's end point: the loop's seam stays continuous
                                      : evaluate((float) i / (float) tableSize);
    }

    // Keep the invariants the editor and evaluator rely on.
    void normalize()
    {
        if (points.size() < 2) resetToPump();
        // NaN / Inf first (pasted text or damaged data): they slip through clamps, and NaN even breaks the sort
        for (auto& p : points)
        {
            auto fix = [](float& v, float fallback) { if (! std::isfinite(v)) v = fallback; };
            fix(p.x, 0.f); fix(p.y, 1.f); // 1 = no ducking
            for (float* h : { &p.inDx, &p.inDy, &p.outDx, &p.outDy, &p.bend }) fix(*h, 0.f);
            fix(p.bendX, 0.5f);
        }
        std::stable_sort(points.begin(), points.end(), [](auto& l, auto& r) { return l.x < r.x; });
        for (auto& p : points)
        {
            p.x = std::clamp(p.x, 0.f, 1.f);
            p.y = std::clamp(p.y, 0.f, 1.f);
            p.inDx = std::clamp(p.inDx, -1.f, 0.f);
            p.outDx = std::clamp(p.outDx, 0.f, 1.f);
            p.inDy = std::clamp(p.inDy, -1.f, 1.f);
            p.outDy = std::clamp(p.outDy, -1.f, 1.f);
            p.bend = std::clamp(p.bend, -1.f, 1.f);
            p.bendX = std::clamp(p.bendX, 0.05f, 0.95f);
        }
        points.front().x = 0.f;
        points.back().x = 1.f;
        points.back().y = points.front().y; // the loop's start and end share one height, so the seam never clicks
        points.front().inDx = points.front().inDy = 0.f;
        points.back().outDx = points.back().outDy = 0.f;
    }

    std::string serialize() const
    {
        std::ostringstream s;
        s.precision(9); // enough digits for floats to survive a save / load exactly
        for (auto& p : points)
            s << p.x << ',' << p.y << ',' << p.inDx << ',' << p.inDy << ',' << p.outDx << ',' << p.outDy << ',' << p.bend << ',' << p.bendX << ';';
        return s.str();
    }

    // Older saves load too: fewer than 6 fields = straight segments, fewer than 8 = no simple-mode bend.
    // `fallback` is returned if the text holds more than loadCap points.
    static Envelope deserialize(const std::string& str, const Envelope& fallback = Envelope())
    {
        Envelope e;
        e.points.clear();
        std::istringstream s(str);
        std::string item;
        while (std::getline(s, item, ';'))
        {
            EnvPoint p;
            int n = std::sscanf(item.c_str(), "%f,%f,%f,%f,%f,%f,%f,%f", &p.x, &p.y, &p.inDx, &p.inDy, &p.outDx, &p.outDy, &p.bend, &p.bendX);
            if (n >= 2)
            {
                if (n < 6) p.inDx = p.inDy = p.outDx = p.outDy = 0.f;
                if (n < 8) { p.bend = 0.f; p.bendX = 0.5f; } // saves from before the simple-mode bend
                if (e.points.size() >= loadCap) return fallback; // stop reading: this is not a real curve
                e.points.push_back(p);
            }
        }
        e.normalize();
        return e;
    }
};
