#pragma once
// "Curve from kick": turns one LFO cycle of the sidechain signal into an editable starting curve.
// Two styles:
//   safe    - 3 to 6 points: fully ducked while the kick is strong, then one smooth rise. Leaves the kick plenty of room.
//   precise - up to 16 points, and it looks at both signals: at every moment it ducks your sound only as far as needed for
//             the kick and your sound to fit together. Where your sound is quiet, or the kick has faded enough, it is left
//             alone. Both are measured against their own peaks (so routing levels do not matter), i.e. as if they sit
//             equally loud in the mix: their peaks added together stay at about that level.
// Pure C++ (no JUCE) so it can be unit-tested anywhere. It runs once per button press, never on the audio thread.
//
// The input is the loudest absolute sample in each slice of the cycle (the same slices the scope draws). From that:
//   1. a loudness outline: gaps between the swings of the waveform are filled in, so a low kick reads as one shape
//   2. safe: duck = how loud the outline is, relative to its own peak (fully ducked from 70%, untouched below 10%), and
//      never less than anything later in the hit; one Bezier segment is fitted to the way it fades
//      precise: curve = (1 - kick outline) / your sound's outline, at most 1 (the lowest 5% of the kick counts as
//      silence, and its loudest moment is always fully ducked), followed with as few points as will stay close to it
//   3. as in the factory shapes, a short drop back down just before the next hit
#include <algorithm>
#include <cmath>
#include <vector>
#include "Envelope.h"

namespace kickfit
{
    enum class Style { safe, precise };

    enum class Status
    {
        ok,
        noSidechain,   // nothing is connected to the sidechain input
        notReady,      // a whole cycle has not been recorded at the current rate yet
        quiet,         // silence
        bandQuiet,     // multiband: the kick has next to nothing in this band
        noGaps,        // it never fades far enough to have a shape (a bass line, noise, a very long tail)
        severalHits,   // more than one hit in a cycle
        crossesLoop    // the hit starts late in the cycle and is still ringing when it starts again
    };

    struct Result
    {
        Status status = Status::quiet;
        int hits = 0;
        bool soundHeard = false;   // precise: the sound being ducked was audible, so the curve takes it into account
        Envelope curve;   // only meaningful when status is ok
    };

    constexpr float silence = 0.01f;          // -40 dB: anything below this is not a kick
    constexpr float duckFrom = 0.10f;         // outline (share of its peak) where ducking starts ...
    constexpr float duckFully = 0.70f;        // ... and where the curve is all the way down
    constexpr float preciseFloor = 0.05f;     // precise: outline below this share of the peak counts as silence
    constexpr int preciseKnots = 10;          // precise: the most points it adds inside the fade
    constexpr float preciseClose = 0.015f;    // ... and it stops adding once the curve is this close everywhere
    constexpr float onBeatEarly = 0.08f;      // a hit this close to the loop point (before it) ...
    constexpr float onBeatLate = 0.06f;       // ... or this close after it counts as "on the beat"

    namespace detail
    {
        // circular running maximum / minimum / average over +-radius slices
        template <class Op>
        std::vector<float> window(const std::vector<float>& in, int radius, Op op)
        {
            const int n = (int) in.size();
            std::vector<float> out((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                float v = in[(size_t) i];
                for (int k = -radius; k <= radius; ++k)
                    if (k != 0) v = op(v, in[(size_t) (((i + k) % n + n) % n)]);
                out[(size_t) i] = v;
            }
            return out;
        }

        // Fills the gaps between a waveform's swings (down to about 20 Hz with a 13 ms radius), then takes the corners off
        inline std::vector<float> outline(const std::vector<float>& raw, int radius)
        {
            const int n = (int) raw.size();
            auto filled = window(raw, radius, [](float a, float b) { return std::max(a, b); });
            filled = window(filled, radius, [](float a, float b) { return std::min(a, b); });
            const int r = std::max(1, radius / 3);
            std::vector<float> smooth((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                float sum = 0.f;
                for (int k = -r; k <= r; ++k) sum += filled[(size_t) (((i + k) % n + n) % n)];
                smooth[(size_t) i] = sum / (float) (2 * r + 1);
            }
            return smooth;
        }

        inline float cubic(float a, float b, float c, float d, float t)
        {
            const float u = 1.f - t;
            return u * u * u * a + 3.f * u * u * t * b + 3.f * u * t * t * c + t * t * t * d;
        }

        struct Handles { float x1, y1, x2, y2; };   // a unit Bezier from (0, 0) to (1, 1)

        // The handles whose curve follows `target` (evenly spaced over the segment) most closely
        inline Handles fit(const std::vector<float>& target)
        {
            constexpr int samples = 48, steps = 64;
            float want[samples + 1];
            const int last = (int) target.size() - 1;
            for (int i = 0; i <= samples; ++i)
            {
                const float f = (float) i / samples * (float) last;
                const int a = (int) f, b = std::min(last, a + 1);
                want[i] = target[(size_t) a] + (target[(size_t) b] - target[(size_t) a]) * (f - (float) a);
            }
            const float xs[] = { 0.f, 0.15f, 0.3f, 0.45f, 0.6f, 0.8f };
            const float ys[] = { 0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f };
            Handles best { 1.f / 3.f, 1.f / 3.f, 2.f / 3.f, 2.f / 3.f };
            float bestError = 1e9f;
            for (float x1 : xs) for (float y1 : ys) for (float dx2 : xs) for (float y2 : ys)
            {
                const float x2 = 1.f - dx2;
                float error = 0.f, px = 0.f, py = 0.f;
                int next = 0;
                for (int s = 1; s <= steps && error < bestError; ++s)
                {
                    const float t = (float) s / steps;
                    const float x = cubic(0.f, x1, x2, 1.f, t), y = cubic(0.f, y1, y2, 1.f, t);
                    while (next <= samples && (float) next / samples <= x + 1e-6f)
                    {
                        const float at = (float) next / samples;
                        const float v = x - px > 1e-6f ? py + (y - py) * (at - px) / (x - px) : y;
                        error += (v - want[next]) * (v - want[next]);
                        ++next;
                    }
                    px = x; py = y;
                }
                if (error < bestError) { bestError = error; best = { x1, y1, x2, y2 }; }
            }
            return best;
        }

        inline EnvPoint at(float x, float y) { EnvPoint p; p.x = x; p.y = y; return p; }

        // Precise: a smooth curve through a few points of `target` (one value per slice, from 0 up to 1), adding a point
        // wherever it is furthest off. Tangents are chosen so that it never overshoots between two points.
        inline std::vector<EnvPoint> follow(const std::vector<float>& target, int firstSlice, int slices)
        {
            const int last = (int) target.size() - 1;
            const int apart = std::max(3, slices / 50);   // points no closer than 2% of the cycle: no clusters
            std::vector<int> knots { 0, last };
            std::vector<float> slope;   // per slice, at each knot
            auto tangents = [&] {
                const size_t count = knots.size();
                std::vector<float> d(count - 1);
                for (size_t i = 0; i + 1 < count; ++i)
                    d[i] = (target[(size_t) knots[i + 1]] - target[(size_t) knots[i]]) / (float) (knots[i + 1] - knots[i]);
                slope.assign(count, 0.f);
                slope[0] = d[0];        // leaves the ducked stretch at once; arrives at full level flat
                for (size_t i = 1; i + 1 < count; ++i)
                    if (d[i - 1] * d[i] > 0.f) slope[i] = 2.f * d[i - 1] * d[i] / (d[i - 1] + d[i]);
            };
            auto valueAt = [&](size_t seg, int i) {
                const int a = knots[seg], b = knots[seg + 1];
                const float w = (float) (b - a), t = (float) (i - a) / w;
                const float y0 = target[(size_t) a], y1 = target[(size_t) b];
                return cubic(y0, y0 + slope[seg] * w / 3.f, y1 - slope[seg + 1] * w / 3.f, y1, t);
            };
            for (;;)
            {
                tangents();
                if ((int) knots.size() >= preciseKnots + 2) break;
                float worst = preciseClose;
                int where = -1;
                size_t seg = 0;
                for (size_t s = 0; s + 1 < knots.size(); ++s)
                    for (int i = knots[s] + apart; i <= knots[s + 1] - apart; ++i)
                    {
                        const float error = std::fabs(valueAt(s, i) - target[(size_t) i]);
                        if (error > worst) { worst = error; where = i; seg = s; }
                    }
                if (where < 0) break;
                knots.insert(knots.begin() + (long) seg + 1, where);
            }

            std::vector<EnvPoint> points;
            for (int i : knots) points.push_back(at((float) (firstSlice + i) / (float) slices, std::clamp(target[(size_t) i], 0.f, 1.f)));
            for (size_t s = 0; s + 1 < knots.size(); ++s)
            {
                const float third = (float) (knots[s + 1] - knots[s]) / 3.f, dx = third / (float) slices;
                points[s].outDx = dx;
                points[s].outDy = std::clamp(slope[s] * third, -points[s].y, 1.f - points[s].y);
                points[s + 1].inDx = -dx;
                points[s + 1].inDy = std::clamp(-slope[s + 1] * third, -points[s + 1].y, 1.f - points[s + 1].y);
            }
            return points;
        }
    }

    // `peaks`: the loudest |sample| of the kick in each of the cycle's slices. `cycleSeconds`: how long the cycle lasts.
    // `sound` (used by precise only): the same for the signal being ducked, over the same cycle. Without it, or when it is
    // silent, precise assumes the sound is at full level all the way through.
    inline Result analyse(const std::vector<float>& peaks, double cycleSeconds, Style style = Style::safe,
                          const std::vector<float>& sound = {})
    {
        Result res;
        const int n = (int) peaks.size();
        if (n < 64 || ! (cycleSeconds > 0.0)) return res;

        std::vector<float> raw((size_t) n);
        for (int i = 0; i < n; ++i) { const float v = std::fabs(peaks[(size_t) i]); raw[(size_t) i] = std::isfinite(v) ? v : 0.f; }

        // 1. loudness outline
        const double sliceSeconds = cycleSeconds / n;
        const int radius = std::clamp((int) std::lround(0.013 / sliceSeconds), 1, n / 16);
        auto outline = detail::outline(raw, radius);
        const float peak = *std::max_element(outline.begin(), outline.end());
        if (peak < silence) return res;
        for (auto& v : outline) v /= peak;

        // how many separate hits: rises above half the peak, each after having fallen below a fifth of it
        int quietAt = -1;
        for (int i = 0; i < n && quietAt < 0; ++i) if (outline[(size_t) i] < 0.2f) quietAt = i;
        if (quietAt < 0) { res.status = Status::noGaps; return res; }
        int rise = -1;
        bool armed = true;
        for (int k = 1; k <= n; ++k)
        {
            const int i = (quietAt + k) % n;
            const float v = outline[(size_t) i];
            if (armed && v >= 0.5f) { ++res.hits; armed = false; if (rise < 0) rise = i; }
            else if (v < 0.2f) armed = true;
        }
        if (res.hits > 1) { res.status = Status::severalHits; return res; }
        if (rise < 0) return res;

        int start = rise; // walk back to where the hit begins
        for (int k = 0; k < n && outline[(size_t) ((start - 1 + n) % n)] > 0.15f; ++k) start = (start - 1 + n) % n;
        const float onset = (float) start / (float) n;
        const bool early = onset >= 1.f - onBeatEarly;
        const bool onBeat = early || onset <= onBeatLate;

        std::vector<float> duck((size_t) n);
        for (int i = 0; i < n; ++i) duck[(size_t) i] = std::clamp((outline[(size_t) i] - duckFrom) / (duckFully - duckFrom), 0.f, 1.f);
        if (! onBeat && duck[0] > 0.5f && duck[(size_t) n - 1] > 0.5f) { res.status = Status::crossesLoop; return res; }

        // 2. the stretch of the cycle that belongs to this hit's fade
        const float dropLength = std::clamp((float) (0.012 / cycleSeconds), 0.015f, 0.035f);
        const float dropAt = early ? std::min(1.f - dropLength, onset - dropLength) : 1.f - dropLength;
        const int first = onBeat ? 0 : start;
        const int end = onBeat ? std::clamp((int) (dropAt * (float) n), 8, n) : n;

        // from the back: each slice is ducked at least as much as anything after it, so the fade only ever rises
        std::vector<float> held((size_t) n, 0.f);
        float running = 0.f;
        for (int i = end - 1; i >= first; --i) { running = std::max(running, duck[(size_t) i]); held[(size_t) i] = running; }

        int holdEnd = first;
        for (int i = first; i < end; ++i) if (held[(size_t) i] >= 0.9f) holdEnd = i;
        int back = end - 1;
        for (int i = holdEnd + 1; i < end; ++i) if (held[(size_t) i] <= 0.05f) { back = i; break; }
        if (holdEnd >= end - n / 20) { res.status = Status::noGaps; return res; }
        back = std::max(back, holdEnd + 3);

        // 3. the fade: from the end of the fully ducked stretch ({ x, 0 }) back to full level ({ x, 1 })
        std::vector<EnvPoint> fadePoints;
        if (style == Style::safe)
        {
            std::vector<float> fade;
            for (int i = holdEnd; i <= back; ++i) fade.push_back(1.f - held[(size_t) std::min(i, end - 1)]);
            fade.front() = 0.f;
            fade.back() = 1.f;
            const auto h = detail::fit(fade);

            const float xHold = (float) holdEnd / (float) n;
            const float xBack = std::min((float) back / (float) n, onBeat ? dropAt : 1.f);
            const float span = xBack - xHold;
            auto from = detail::at(xHold, 0.f), top = detail::at(xBack, 1.f);
            from.outDx = std::max(h.x1 * span, span * 0.001f);
            from.outDy = h.y1;
            top.inDx = std::min((h.x2 - 1.f) * span, -span * 0.001f);
            top.inDy = h.y2 - 1.f;
            fadePoints = { from, top };
        }
        else
        {
            // down until the kick's loudest moment is over, then exactly what it leaves free
            int loudest = first;
            for (int i = first; i < end; ++i) if (outline[(size_t) i] >= 0.95f) loudest = i;
            // how loud the sound being ducked is, against its own peak
            std::vector<float> busy((size_t) n, 1.f);
            if ((int) sound.size() == n)
            {
                std::vector<float> level((size_t) n);
                for (int i = 0; i < n; ++i) { const float v = std::fabs(sound[(size_t) i]); level[(size_t) i] = std::isfinite(v) ? v : 0.f; }
                level = detail::outline(level, radius);
                const float top = *std::max_element(level.begin(), level.end());
                res.soundHeard = top >= silence;
                if (res.soundHeard)
                    for (int i = 0; i < n; ++i) busy[(size_t) i] = level[(size_t) i] / top;
            }
            std::vector<float> room((size_t) n, 1.f);
            const int soften = std::max(1, radius / 2);
            for (int i = first; i < end; ++i)
            {
                float sum = 0.f;
                int count = 0;
                for (int j = std::max(first, i - soften); j <= std::min(end - 1, i + soften); ++j) { sum += outline[(size_t) j]; ++count; }
                const float spare = 1.f - std::clamp((sum / (float) count - preciseFloor) / (1.f - preciseFloor), 0.f, 1.f);
                // what the kick leaves free, against what the sound needs there
                room[(size_t) i] = spare >= 1.f ? 1.f : std::clamp(spare / std::max(busy[(size_t) i], 0.001f), 0.f, 1.f);
            }
            loudest = std::min(loudest, end - 6);
            int full = end - 1;   // the first slice from which it stays free
            while (full - 1 > loudest + 4 && room[(size_t) (full - 1)] >= 0.97f) --full;
            std::vector<float> target(room.begin() + loudest, room.begin() + full + 1);
            target.front() = 0.f;
            target.back() = 1.f;
            fadePoints = detail::follow(target, loudest, n);
            if (onBeat) fadePoints.back().x = std::min(fadePoints.back().x, dropAt);
        }

        // ... and the rest of the cycle around it
        std::vector<EnvPoint> pts;
        if (! onBeat)
        {
            // full level until just before the hit, then straight down onto it
            pts.push_back(detail::at(0.f, 1.f));
            pts.push_back(detail::at(std::max(0.005f, onset - dropLength), 1.f));
            pts.push_back(detail::at(onset, 0.f));
        }
        else pts.push_back(detail::at(0.f, 0.f));

        if (fadePoints.front().x - pts.back().x > 0.01f) pts.push_back(fadePoints.front());
        else { pts.back().outDx = fadePoints.front().outDx; pts.back().outDy = fadePoints.front().outDy; }
        pts.insert(pts.end(), fadePoints.begin() + 1, fadePoints.end());

        if (onBeat)
        {
            // stay up, then drop for the next hit (a hit that comes early gets its drop early too)
            if (dropAt - pts.back().x > 0.012f) pts.push_back(detail::at(dropAt, 1.f));
            else pts.back().x = std::max(pts.back().x, dropAt);
            if (early) pts.push_back(detail::at(onset, 0.f));
            pts.push_back(detail::at(1.f, 0.f));
        }
        else if (1.f - pts.back().x > 0.005f) pts.push_back(detail::at(1.f, 1.f));
        else pts.back().x = 1.f;

        res.curve.points = std::move(pts);
        res.curve.normalize();
        res.status = Status::ok;
        return res;
    }
}
