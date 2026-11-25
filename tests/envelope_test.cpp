// Build & run:  clang++ -std=c++17 tests/envelope_test.cpp -o /tmp/envtest && /tmp/envtest
#include "../Source/Envelope.h"
#include "../Source/Presets.h"
#include "../Source/KickToCurve.h"
#include <functional>
#include <limits>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

int main()
{
    // Default: the classic house sidechain pump, 6 points
    Envelope e;
    assert(e.points.size() == 6);
    assert(near(e.points.front().y, e.points.back().y));              // seam without a click
    assert(near(e.evaluate(0.f), 0.f));                                // fully ducked at the start
    assert(near(e.evaluate(0.1f), 0.f, 5e-3f));                        // held down
    assert(near(e.evaluate(0.25f), 0.5f, 0.02f));                      // steep swell through the middle
    assert(near(e.evaluate(0.5f), 1.f, 2e-3f));
    assert(near(e.evaluate(0.8f), 1.f, 2e-3f));                        // stays at full level
    assert(e.evaluate(0.999f) < 0.9f);                                 // and drops at the very end
    {
        float prev = -1.f;
        for (int i = 0; i <= 500; ++i)                                 // rises monotonically up to the top
        {
            const float v = e.evaluate(i / 500.f * 0.5f);
            assert(v >= prev - 1e-4f);
            prev = v;
        }
    }
    assert(near(e.evaluate(1.25f), e.evaluate(0.25f)));                // wraps

    // Multiband defaults: the lows recover slowest, the highs fastest (shapes taken from the reference sketch)
    {
        Envelope lo, mi, hi;
        lo.resetToBandPump(0); mi.resetToBandPump(1); hi.resetToBandPump(2);
        for (auto* c : { &lo, &mi, &hi })
        {
            assert(c->points.size() == 6);
            assert(near(c->points.front().y, c->points.back().y));
            assert(near(c->evaluate(0.f), 0.f) && near(c->evaluate(0.5f), 1.f, 2e-3f) && near(c->evaluate(0.8f), 1.f, 2e-3f));
            assert(c->evaluate(0.999f) < 0.9f);
        }
        // heights read off the sketch: (band, x, expected y)
        struct Probe { const Envelope* e; float x, y; };
        for (auto p : { Probe { &lo, 0.25f, 0.10f }, Probe { &lo, 0.338f, 0.50f }, Probe { &lo, 0.398f, 0.90f },
                        Probe { &mi, 0.228f, 0.25f }, Probe { &mi, 0.270f, 0.50f }, Probe { &mi, 0.359f, 0.90f },
                        Probe { &hi, 0.132f, 0.25f }, Probe { &hi, 0.175f, 0.50f }, Probe { &hi, 0.278f, 0.90f } })
            assert(near(p.e->evaluate(p.x), p.y, 0.025f));
        for (float x : { 0.15f, 0.2f, 0.25f, 0.3f, 0.35f })       // at any moment: low <= mid <= high
            assert(lo.evaluate(x) <= mi.evaluate(x) + 1e-4f && mi.evaluate(x) <= hi.evaluate(x) + 1e-4f);
    }

    // The loop's first and last points always share one height
    {
        Envelope seam; seam.points = { { 0.f, 0.3f }, { 0.5f, 1.f }, { 1.f, 0.9f } };
        seam.normalize();
        assert(near(seam.points.back().y, seam.points.front().y));
    }

    // Zero handles = straight line
    e.points = { { 0.f, 0.f }, { 1.f, 1.f } };
    for (float x : { 0.1f, 0.25f, 0.5f, 0.9f }) assert(near(e.evaluate(x), x));

    // Out handle pulled up bows the curve above the line; pulled down, below it
    e.points[0].outDx = 0.3f; e.points[0].outDy = 0.6f;
    assert(e.evaluate(0.25f) > 0.25f + 0.05f);
    e.points[0].outDy = 0.f;
    assert(e.evaluate(0.25f) < 0.25f - 0.02f);

    // In handle pulled down bows below at the end; pulled up bows above
    e.points = { { 0.f, 0.f }, { 1.f, 1.f } };
    e.points[1].inDx = -0.3f; e.points[1].inDy = 0.f;
    assert(e.evaluate(0.75f) > 0.75f + 0.02f);

    // Always monotone in x (a function of phase) and inside 0..1, even with extreme handles
    e.points = { { 0.f, 0.f, 0.f, 0.f, 5.f, 1.f }, { 1.f, 1.f, -5.f, -1.f, 0.f, 0.f } };
    e.normalize();
    for (int i = 0; i <= 200; ++i)
    {
        float v = e.evaluate(i / 200.f * 0.9999f);
        assert(v >= 0.f && v <= 1.f);
    }

    // Easy-ease (flat tangents): slow start, slow end
    e.points = { { 0.f, 0.f, 0.f, 0.f, 0.33f, 0.f }, { 1.f, 1.f, -0.33f, 0.f, 0.f, 0.f } };
    assert(e.evaluate(0.1f) < 0.1f);
    assert(e.evaluate(0.9f) > 0.9f);
    assert(near(e.evaluate(0.5f), 0.5f));

    // Simple mode: one handle bends the segment (power curve) through the dragged point
    e.points = { { 0.f, 0.f }, { 1.f, 1.f } };
    auto hd = e.bendHandle(0);
    assert(near(hd.first, 0.5f) && near(hd.second, 0.5f));
    e.bendSegmentThrough(0, 0.5f, 0.7f);
    hd = e.bendHandle(0);
    assert(near(hd.first, 0.5f, 5e-3f) && near(hd.second, 0.7f, 5e-3f));
    e.bendSegmentThrough(0, 0.3f, 0.1f);              // off-centre, below the line
    hd = e.bendHandle(0);
    assert(near(hd.first, 0.3f, 5e-3f) && near(hd.second, 0.1f, 5e-3f));
    assert(e.points[0].bendX > 0.29f && e.points[0].bendX < 0.31f);

    // ...and it reaches far sharper curves than a cubic with handles inside the box can:
    // the sharpest such cubic (both handles on the corner) is y = 1 - (1 - x^(1/3))^3
    {
        Envelope sharp; sharp.points = { { 0.f, 0.f }, { 1.f, 1.f } };
        sharp.bendSegmentThrough(0, 0.05f, 0.999f);   // handle near the start, right up at the top
        const float x = 0.01f;
        const float cubicLimit = 1.f - std::pow(1.f - std::pow(x, 1.f / 3.f), 3.f);
        std::printf("at x=%.2f: bend curve %.3f, sharpest cubic %.3f\n", x, sharp.evaluate(x), cubicLimit);
        assert(sharp.evaluate(0.05f) > 0.99f);
        assert(sharp.evaluate(x) > cubicLimit + 0.15f);
        // the very first sample must be the point itself, not the top of the jump
        assert(sharp.evaluate(0.f) == 0.f);
        sharp.bendSegmentThrough(0, 0.9f, 0.01f);     // and the other way: nothing happens until the very end
        assert(sharp.evaluate(0.9f) < 0.02f);
        assert(sharp.evaluate(0.999f) > 0.9f);
    }

    // Smooth control: dragging the handle toward the top must not make the sharpness explode. Compare the
    // bend values (log-sharpness) needed for a handle at 90% height versus 99% height of the segment.
    {
        Envelope m; m.points = { { 0.f, 0.f }, { 1.f, 1.f } };
        m.bendSegmentThrough(0, 0.26f, 0.90f); const float b90 = m.points[0].bend;
        m.bendSegmentThrough(0, 0.26f, 0.99f); const float b99 = m.points[0].bend;
        m.bendSegmentThrough(0, 0.26f, 0.73f); const float b73 = m.points[0].bend;
        std::printf("handle at 26%% along: height 73%% -> bend %.3f, 90%% -> %.3f, 99%% -> %.3f\n", b73, b90, b99);
        assert(b73 < b90 && b90 < b99);
        assert(b99 / b90 < 3.f);                       // a power curve needed ~ 10x here
        // and every bend is a smooth rounded knee: the curve's rise is monotonic and stays inside 0..1
        float prev = 0.f;
        for (int i = 0; i <= 200; ++i) { const float v = m.evaluate(i / 200.f * 0.9999f); assert(v >= prev - 1e-6f && v <= 1.f); prev = v; }
    }
    // bending clears that segment's Bezier handles, and a falling segment bends the same way
    e.points = { { 0.f, 1.f, 0.f, 0.f, 0.3f, -0.4f }, { 1.f, 0.f, -0.3f, 0.2f, 0.f, 0.f } };
    e.bendSegmentThrough(0, 0.5f, 0.3f);
    assert(near(e.points[0].outDx, 0.f) && near(e.points[1].inDy, 0.f));
    hd = e.bendHandle(0);
    assert(near(hd.first, 0.5f, 5e-3f) && near(hd.second, 0.3f, 5e-3f));

    // Round trip + old saves (6 fields: no bend)
    {
        Envelope b2; b2.points = { { 0.f, 0.f }, { 0.6f, 1.f }, { 1.f, 0.f } };
        b2.bendSegmentThrough(0, 0.24f, 0.8f * 1.f);
        Envelope back = Envelope::deserialize(b2.serialize());
        assert(near(back.points[0].bend, b2.points[0].bend) && near(back.points[0].bendX, 0.4f, 1e-3f));
        assert(near(back.evaluate(0.24f), b2.evaluate(0.24f)));
        Envelope old6 = Envelope::deserialize("0,0,0,0,0.2,0.1;1,1,-0.2,0,0,0;");
        assert(near(old6.points[0].bend, 0.f) && near(old6.points[0].bendX, 0.5f));
    }

    // Switching editing style converts the curve so it stays editable and keeps its shape
    auto maxDiff = [](const Envelope& x, const Envelope& y) {
        float worst = 0.f;
        for (int i = 0; i <= 400; ++i) worst = std::max(worst, std::fabs(x.evaluate(i / 400.f * 0.9999f) - y.evaluate(i / 400.f * 0.9999f)));
        return worst;
    };
    {
        // simple bend -> Bezier handles, for gentle through to very sharp bends, rising and falling
        for (auto spec : { std::array<float, 4> { 0.26f, 0.73f, 0.f, 1.f }, { 0.5f, 0.7f, 0.f, 1.f }, { 0.3f, 0.1f, 0.f, 1.f },
                           { 0.26f, 0.99f, 0.f, 1.f }, { 0.5f, 0.3f, 1.f, 0.f }, { 0.8f, 0.05f, 0.f, 1.f } })
        {
            Envelope src; src.points = { { 0.f, spec[2] }, { 1.f, spec[3] } };
            src.bendSegmentThrough(0, spec[0], src.points[0].y + (src.points[1].y - src.points[0].y) * spec[1]);
            Envelope conv = src;
            conv.convertToBezier();
            assert(std::fabs(conv.points[0].bend) < 1e-6f);                    // the bend is consumed...
            assert(conv.segmentHasHandles(0));                                 // ...and handles take over
            const float err = maxDiff(src, conv);
            std::printf("bend->handles (handle %.2f, %.2f): max error %.3f\n", spec[0], spec[1], err);
            assert(err < (spec[1] > 0.95f || spec[1] < 0.06f ? 0.2f : 0.05f)); // sharp edges are limited by what a cubic can do
        }
        // Bezier handles -> one bend (shapes a single bend can express: ease-out, ease-out, ease-in)
        for (auto h : { std::array<float, 4> { 0.3f, 0.6f, -0.3f, 0.0f }, { 0.1f, 0.9f, -0.1f, 0.0f }, { 0.4f, 0.0f, -0.2f, -0.5f } })
        {
            Envelope src; src.points = { { 0.f, 0.f, 0.f, 0.f, h[0], h[1] }, { 1.f, 1.f, h[2], h[3], 0.f, 0.f } };
            Envelope conv = src;
            conv.convertToSimple();
            assert(!conv.segmentHasHandles(0));
            const float err = maxDiff(src, conv);
            std::printf("handles->bend (out %.1f,%.1f in %.1f,%.1f): max error %.3f, bend %.3f\n", h[0], h[1], h[2], h[3], err, conv.points[0].bend);
            assert(err < 0.08f);
            assert(std::fabs(conv.points[0].bend) > 0.f);
        }
        // an S-curve (easy-ease) has no single bend: it is approximated, never garbage
        Envelope sc; sc.points = { { 0.f, 0.f, 0.f, 0.f, 0.33f, 0.f }, { 1.f, 1.f, -0.33f, 0.f, 0.f, 0.f } };
        Envelope sc2 = sc; sc2.convertToSimple();
        std::printf("S-curve -> simple: max error %.3f\n", maxDiff(sc, sc2));
        assert(maxDiff(sc, sc2) < 0.2f);
        // segments that don't use the style being converted are left alone
        Envelope keep; keep.points = { { 0.f, 0.f, 0.f, 0.f, 0.2f, 0.5f }, { 0.5f, 1.f, -0.2f, 0.f, 0.f, 0.f }, { 1.f, 0.f } };
        Envelope keep2 = keep; keep2.convertToBezier();                       // nothing has a bend: unchanged
        assert(maxDiff(keep, keep2) < 1e-6f && near(keep2.points[0].outDx, 0.2f));
        // a full cycle (simple -> bezier -> simple) lands close to where it started
        Envelope cyc; cyc.points = { { 0.f, 0.f }, { 1.f, 1.f } };
        cyc.bendSegmentThrough(0, 0.4f, 0.8f);
        Envelope cyc2 = cyc; cyc2.convertToBezier(); cyc2.convertToSimple();
        std::printf("simple -> bezier -> simple: max error %.3f\n", maxDiff(cyc, cyc2));
        assert(maxDiff(cyc, cyc2) < 0.08f);
    }

    // Unset handles show a third of the way along the straight line (so every point, including the first
    // and last, has a handle to grab), and change nothing about the curve
    {
        Envelope v; v.points = { { 0.f, 0.f }, { 0.5f, 1.f }, { 1.f, 0.2f } };
        const auto o0 = v.outHandle(0), i1 = v.inHandle(1), o1 = v.outHandle(1), i2 = v.inHandle(2);
        assert(near(o0.first, 0.5f / 3.f) && near(o0.second, 1.f / 3.f));
        assert(near(i1.first, 0.5f - 0.5f / 3.f) && near(i1.second, 1.f - 1.f / 3.f));
        assert(near(o1.first, 0.5f + 0.5f / 3.f) && near(o1.second, 1.f - 0.8f / 3.f));
        assert(near(i2.first, 1.f - 0.5f / 3.f) && near(i2.second, 0.2f + 0.8f / 3.f));
        for (int i = 0; i <= 100; ++i)                       // still exactly the straight polyline
        {
            const float x = i / 100.f * 0.9999f;
            const float expect = x < 0.5f ? x * 2.f : 1.f - (x - 0.5f) * 1.6f;
            assert(near(v.evaluate(x), expect, 2e-3f));
        }
    }

    // Bake table
    std::vector<float> table(Envelope::tableSize + 1);
    e.points = { { 0.f, 0.f }, { 1.f, 1.f } };
    e.bake(table.data());
    assert(near(table[0], 0.f));
    assert(near(table[Envelope::tableSize / 2], 0.5f));
    assert(near(table[Envelope::tableSize], 1.f, 2e-3f));

    // Serialize round trip + normalize
    Envelope p;
    p.points = { { 0.1f, 0.2f, 0.f, 0.f, 0.2f, 0.4f }, { 0.5f, 1.f, -0.1f, 0.1f, 0.1f, -0.2f }, { 0.9f, 0.f } };
    Envelope q = Envelope::deserialize(p.serialize());
    assert(q.points.size() == 3);
    assert(near(q.points.front().x, 0.f) && near(q.points.back().x, 1.f));
    assert(near(q.points[0].outDy, 0.4f));
    assert(near(q.points[1].inDx, -0.1f));
    assert(near(q.points[2].outDx, 0.f));

    // Older saves (no handles) still load, as straight segments
    Envelope old = Envelope::deserialize("0,0,0.2,0.5;1,1,0,0.5;");
    assert(old.points.size() == 2 && near(old.points[0].outDx, 0.f));

    // Multiband presets: 4 with three bands and 4 with two, and every band's curve obeys the same rules
    {
        const auto list = presets::factoryMultiband();
        int three = 0, two = 0;
        for (const auto& pr : list)
        {
            (pr.three ? three : two) += 1;
            for (const Envelope* ep : { &pr.low, &pr.mid, &pr.high })
            {
                assert(ep->points.size() >= 2 && ep->points.size() <= 6);
                assert(near(ep->points.front().x, 0.f) && near(ep->points.back().x, 1.f));
                assert(near(ep->points.front().y, ep->points.back().y, 1e-6f));
                for (size_t i = 1; i < ep->points.size(); ++i) assert(ep->points[i].x > ep->points[i - 1].x);
                for (int i = 0; i < 4096; ++i)
                {
                    const float v = ep->evaluate((float) i / 4096.f);
                    assert(v >= -1e-4f && v <= 1.0001f);
                }
            }
        }
        assert(three == 4 && two == 4);
    }

    // Factory presets: every one must be a valid, click-free, duck-only loop
    {
        const auto list = presets::factory();
        assert(list.size() >= 8);
        for (const auto& pr : list)
        {
            const auto& ep = pr.env;
            assert(ep.points.size() >= 2);
            assert(near(ep.points.front().x, 0.f) && near(ep.points.back().x, 1.f));
            assert(near(ep.points.front().y, ep.points.back().y, 1e-6f));            // no click at the loop seam
            for (size_t i = 1; i < ep.points.size(); ++i) assert(ep.points[i].x > ep.points[i - 1].x);   // strictly increasing: no stacked points
            float lo = 2.f, hi = -1.f;
            for (int i = 0; i < 4096; ++i)
            {
                const float v = ep.evaluate((float) i / 4096.f);
                assert(v >= -1e-4f && v <= 1.0001f);                                  // gain can only duck
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
            if (pr.name != "Flat (no ducking)" && pr.name != "Soft duck") assert(lo < 0.05f && hi > 0.95f);            // uses the full range
            // clipboard / file text round trip
            Envelope back;
            assert(presets::fromText(presets::toText(ep), back));
            for (int i = 0; i <= 200; ++i) assert(near(back.evaluate(i / 200.f), ep.evaluate(i / 200.f), 1e-4f));
        }
        assert(!presets::fromText("hello", e));
        assert(!presets::fromText("FreeLFOCurve1:", e));
        // tiling: Pump x2 is two half-length copies of the pump
        Envelope x2;
        for (const auto& pr : list) if (pr.name == "Pump x2") x2 = pr.env;
        Envelope one;
        for (int i = 0; i < 200; ++i)
        {
            const float x = i / 200.f * 0.99f;
            assert(near(x2.evaluate(x * 0.5f), one.evaluate(x), 2e-3f));          // first copy
            assert(near(x2.evaluate(0.5f + x * 0.5f), one.evaluate(x), 2e-3f));   // second copy
        }
    }

    // ---- the point limit
    {
        auto many = [](int n) {   // n points, zig-zag, straight segments
            Envelope m; m.points.clear();
            for (int i = 0; i < n; ++i) m.points.push_back({ (float) i / (float) (n - 1), i % 2 == 0 ? 0.2f : 0.8f });
            m.normalize();
            return m;
        };
        assert(Envelope::maxPoints == 64 && Envelope::loadCap == 512);
        Envelope out;
        assert(presets::fromText(presets::toText(many(64)), out) && out.points.size() == 64);   // exactly at the limit: fine
        const size_t before = out.points.size();
        assert(!presets::fromText(presets::toText(many(65)), out) && out.points.size() == before); // refused whole, target untouched
        assert(presets::hasTooManyPoints(presets::toText(many(65))));
        assert(!presets::hasTooManyPoints(presets::toText(many(64))));
        assert(!presets::hasTooManyPoints("some other text;;;"));
        std::string unterminated = presets::toText(many(65)); unterminated.pop_back();            // last ';' missing
        assert(!presets::fromText(unterminated, out));
        // saved projects: more than 64 loads unchanged; more than 512 is treated as damaged
        for (int n : { 65, 200, 512 }) assert(Envelope::deserialize(many(n).serialize()).serialize() == many(n).serialize());
        Envelope fallback; fallback.resetToBandPump(2);
        assert(Envelope::deserialize(many(513).serialize(), fallback).serialize() == fallback.serialize());
        assert(Envelope::deserialize(many(20000).serialize()).serialize() == Envelope().serialize());
        // converting for Simple mode may add points, but never past the limit: S-shaped segments that all want splitting
        for (int n : { 10, 40, 63, 64 })
        {
            Envelope s = many(n);
            const float span = 1.f / (float) (n - 1);
            for (int i = 0; i + 1 < n; ++i) { s.points[(size_t) i].outDx = span; s.points[(size_t) i + 1].inDx = -span; }
            const Envelope simple = s.simplifiedCopy(0.0001f);
            assert(simple.points.size() >= (size_t) n && simple.points.size() <= Envelope::maxPoints);
        }
        for (const auto& pr : presets::factory())
        {
            assert(pr.env.points.size() <= Envelope::maxPoints);
            assert(pr.env.simplifiedCopy().points.size() <= Envelope::maxPoints);
        }
    }

    // ---- curve from kick: many shapes of sidechain signal, sliced the way the plugin records them
    {
        using kickfit::Status;
        const double beat = 60.0 / 130.0, twoPi = 6.283185307179586;
        auto slices = [](const std::function<double(double)>& f, double cycle) {
            std::vector<float> peaks(512, 0.f);
            const int total = (int) (cycle * 48000.0);
            for (int i = 0; i < total; ++i)
            {
                float& slice = peaks[(size_t) ((double) i / total * 512.0)];
                slice = std::max(slice, (float) std::fabs(f(i / 48000.0)));
            }
            return peaks;
        };
        auto kick = [twoPi](double t, double hz, double decay, double level) {
            return t < 0.0 ? 0.0 : level * std::sin(twoPi * (hz * t + 2.4 * (1.0 - std::exp(-t / 0.02)))) * std::exp(-t / decay);
        };
        auto run = [&](const std::function<double(double)>& f, double cycle) { return kickfit::analyse(slices(f, cycle), cycle); };
        auto usable = [](const kickfit::Result& r) {
            const auto& e = r.curve;
            if (r.status != Status::ok || e.points.size() < 3 || e.points.size() > 6) return false;
            if (e.points.front().x != 0.f || e.points.back().x != 1.f || e.points.front().y != e.points.back().y) return false;
            for (size_t i = 1; i < e.points.size(); ++i) if (!(e.points[i].x > e.points[i - 1].x)) return false;
            for (int i = 0; i < 4096; ++i) { const float v = e.evaluate((float) i / 4096.f); if (!(v >= 0.f && v <= 1.f)) return false; }
            return true;
        };
        // where the curve first gets back to half volume (as a share of the cycle)
        auto halfWay = [](const Envelope& e) { for (int i = 0; i < 1000; ++i) if (e.evaluate(i / 1000.f) >= 0.5f) return i / 1000.f; return 1.f; };

        // plain kicks, short to long: ducked on the hit, full again later, and a longer kick ducks for longer
        float last = 0.f;
        for (double decay : { 0.04, 0.07, 0.10, 0.18 })
        {
            const auto r = run([&](double t) { return kick(t, 50.0, decay, 0.9); }, beat);
            assert(usable(r));
            assert(r.curve.evaluate(0.01f) < 0.05f && r.curve.evaluate(0.90f) > 0.95f);
            assert(halfWay(r.curve) > last);
            last = halfWay(r.curve);
        }
        // the level of the sidechain does not matter, only its shape; a very low kick has no false gaps
        const auto loud = run([&](double t) { return kick(t, 50.0, 0.10, 0.9); }, beat);
        const auto faint = run([&](double t) { return kick(t, 50.0, 0.10, 0.03); }, beat);
        assert(usable(faint) && faint.curve.serialize() == loud.curve.serialize());
        assert(usable(run([&](double t) { return kick(t, 32.0, 0.12, 0.9); }, beat)));
        assert(usable(run([&](double t) { return std::max(-0.6, std::min(0.6, 4.0 * kick(t, 50.0, 0.09, 0.9))); }, beat))); // clipped
        // other rates, from 1/16 to half a bar, with one hit each
        for (double cycle : { beat / 4.0, beat / 2.0, beat, beat * 2.0 })
            assert(usable(run([&](double t) { return kick(t, 55.0, cycle * 0.15, 0.9); }, cycle)));
        // a hit a little late, and one a little early (its drop comes early too)
        assert(usable(run([&](double t) { return kick(t - 0.03 * beat, 50.0, 0.09, 0.9); }, beat)));
        {
            const auto r = run([&](double t) { return kick(t, 50.0, 0.08, 0.9) + kick(t - 0.95 * beat, 50.0, 0.08, 0.9); }, beat);
            assert(usable(r) && r.curve.evaluate(0.97f) < 0.05f && r.curve.evaluate(0.80f) > 0.95f);
        }
        // a kick in the middle of the cycle: full level around it, ducked on it
        {
            const auto r = run([&](double t) { return kick(t - 0.5 * beat, 50.0, 0.05, 0.9); }, beat);
            assert(usable(r) && r.curve.evaluate(0.2f) > 0.99f && r.curve.evaluate(0.52f) < 0.05f && r.curve.evaluate(0.95f) > 0.95f);
        }
        // things that are not one clean hit are refused
        assert(run([](double) { return 0.0; }, beat).status == Status::quiet);
        assert(run([&](double t) { return kick(t, 50.0, 0.10, 0.004); }, beat).status == Status::quiet);
        assert(run([&](double t) { return 0.7 * std::sin(twoPi * 55.0 * t); }, beat).status == Status::noGaps);
        assert(run([&](double t) { return kick(t, 40.0, 1.5, 0.9); }, beat).status == Status::noGaps);          // a long 808
        const auto four = run([&](double t) { double s = 0; for (int k = 0; k < 4; ++k) s += kick(t - k * beat, 50.0, 0.06, 0.9); return s; }, beat * 4.0);
        assert(four.status == Status::severalHits && four.hits == 4);
        assert(run([&](double t) { return kick(t - 0.8 * beat, 50.0, 0.12, 0.9) + kick(t + 0.2 * beat, 50.0, 0.12, 0.9); }, beat).status == Status::crossesLoop);
        // nonsense in, no crash out
        assert(kickfit::analyse({}, beat).status == Status::quiet);
        assert(kickfit::analyse(std::vector<float>(512, std::numeric_limits<float>::quiet_NaN()), beat).status == Status::quiet);
        assert(kickfit::analyse(std::vector<float>(512, 0.5f), 0.0).status == Status::quiet);
        // ---- precise: looks at the kick and at the sound being ducked
        {
            using kickfit::Style;
            auto usablePrecise = [&](const kickfit::Result& r) {
                const auto& e = r.curve;
                if (r.status != Status::ok || e.points.size() < 3 || e.points.size() > 16) return false;
                if (e.points.front().x != 0.f || e.points.back().x != 1.f || e.points.front().y != e.points.back().y) return false;
                for (size_t i = 1; i < e.points.size(); ++i) if (!(e.points[i].x > e.points[i - 1].x)) return false;
                for (int i = 0; i < 4096; ++i) { const float v = e.evaluate((float) i / 4096.f); if (!(v >= 0.f && v <= 1.f)) return false; }
                return true;
            };
            auto bass = [twoPi](double t) { return 0.7 * std::sin(twoPi * 55.0 * t); };
            const std::function<double(double)> sounds[] = {
                bass,
                [&](double t) { return bass(t) * std::min(1.0, 0.25 + t / (beat * 0.6)); },       // fading in
                [&](double t) { return bass(t) * std::exp(-std::fmod(t, beat / 4.0) / 0.05); },    // plucks
                [&](double t) { return t > beat * 0.5 ? bass(t) : 0.0; },                          // off the beat only
                [](double) { return 0.0; } };
            for (double decay : { 0.05, 0.09, 0.18 })
            {
                const auto kickPeaks = slices([&](double t) { return kick(t, 50.0, decay, 0.9); }, beat);
                const auto alone = kickfit::analyse(kickPeaks, beat, Style::precise);
                assert(usablePrecise(alone) && !alone.soundHeard);
                for (const auto& sound : sounds)
                {
                    const auto soundPeaks = slices(sound, beat);
                    const auto r = kickfit::analyse(kickPeaks, beat, Style::precise, soundPeaks);
                    assert(usablePrecise(r));
                    assert(r.curve.evaluate(0.004f) < 0.35f && r.curve.evaluate(0.93f) > 0.9f);   // down on the hit, back before the next
                    // kick and ducked sound, each measured against its own peak, fit together (a little over at worst)
                    const auto k = kickfit::detail::outline(kickPeaks, 14), x = kickfit::detail::outline(soundPeaks, 14);
                    const float kTop = *std::max_element(k.begin(), k.end()), xTop = std::max(1e-9f, *std::max_element(x.begin(), x.end()));
                    for (int i = 0; i < 512; ++i)
                        assert(k[(size_t) i] / kTop + r.curve.evaluate((i + 0.5f) / 512.f) * x[(size_t) i] / xTop < 1.3f);
                    // never ducks more than it would for a sound that is loud all the way (apart from fitting slack)
                    double mine = 0.0, worstCase = 0.0;
                    for (int i = 0; i < 512; ++i) { mine += r.curve.evaluate((i + 0.5f) / 512.f); worstCase += alone.curve.evaluate((i + 0.5f) / 512.f); }
                    assert(mine > worstCase - 512 * 0.04);
                }
            }
            // precise refuses exactly what safe refuses
            for (const auto& f : { std::function<double(double)>([](double) { return 0.0; }), std::function<double(double)>(bass),
                                   std::function<double(double)>([&](double t) { return kick(t, 40.0, 1.5, 0.9); }) })
                assert(kickfit::analyse(slices(f, beat), beat, Style::precise).status == run(f, beat).status);
        }
        // random junk must give either a refusal or a usable curve
        unsigned seed = 12345;
        auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.f; };
        for (int trial = 0; trial < 600; ++trial)
        {
            std::vector<float> peaks(512, 0.f);
            for (int hit = 0, count = 1 + trial % 3; hit < count; ++hit)
            {
                const int at = (int) (rnd() * 512.f), length = 2 + (int) (rnd() * rnd() * 500.f);
                const float level = 0.05f + rnd();
                for (int i = 0; i < length; ++i) { float& s = peaks[(size_t) ((at + i) % 512)]; s = std::max(s, level * (1.f - (float) i / length) * (0.5f + 0.5f * rnd())); }
            }
            const double cycle = 0.05 + 4.0 * rnd();
            const auto r = kickfit::analyse(peaks, cycle);
            assert(r.status != Status::ok || usable(r));
            std::vector<float> sound(512);
            for (auto& v : sound) v = trial % 5 == 0 ? 0.f : rnd() * rnd();
            const auto fine = kickfit::analyse(peaks, cycle, kickfit::Style::precise, sound);
            assert(fine.status == r.status);
            if (fine.status == Status::ok)
            {
                assert(fine.curve.points.size() >= 3 && fine.curve.points.size() <= 16 && fine.curve.points.front().y == fine.curve.points.back().y);
                for (size_t i = 1; i < fine.curve.points.size(); ++i) assert(fine.curve.points[i].x >= fine.curve.points[i - 1].x);
                for (int i = 0; i < 1024; ++i) { const float v = fine.curve.evaluate(i / 1024.f); assert(v >= 0.f && v <= 1.f); }
            }
        }
    }

    std::puts("envelope tests passed");
    return 0;
}
