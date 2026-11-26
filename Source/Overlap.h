#pragma once
// Where the sidechain kick and the plugin's output compete: the stretches of the cycle where both are loud. The graph
// hatches them. Pure C++ (no JUCE) so it can be unit-tested anywhere.
//
// "Loud" is judged on each signal's loudness outline, not on its waveform. A slice of the cycle is far shorter than one
// swing of a bass note or a kick, so comparing the waveforms slice by slice only marked the moments where two peaks
// happened to line up: thin stripes, or nothing at all with busy material. The outline is the same one "curve from kick"
// reads (gaps between swings filled in, corners taken off). It is worked out here with sliding windows whose cost does
// not grow with their width, and without allocating, because this runs on every animation frame.
#include <algorithm>
#include <cmath>

namespace overlap
{
    constexpr int bins = 512;          // slices per cycle, as the scope records them
    constexpr float threshold = 0.4f;  // both must be above this share of their own peak
    constexpr float silence = 0.01f;   // -40 dB: below this there is nothing to compare
    constexpr int shortest = 4;        // a marked stretch shorter than this many slices is left out
    constexpr int maxRadius = bins / 16;

    // The widest swing to bridge is about 13 ms to each side (as in "curve from kick")
    inline int radiusFor(double cycleSeconds)
    {
        if (! (cycleSeconds > 0.0)) return 1;
        return (int) std::clamp(std::lround(0.013 / (cycleSeconds / bins)), 1L, (long) maxRadius);
    }

    namespace detail
    {
        // The maximum (or minimum) over +-radius slices around every slice, the cycle wrapping round. Van Herk / Gil-Werman:
        // three passes whatever the radius.
        template <class Op>
        inline void slide(const float* in, float* out, int radius, Op op)
        {
            constexpr int longest = bins + 2 * maxRadius;
            float wrapped[longest], ahead[longest], behind[longest];
            const int width = 2 * radius + 1, length = bins + 2 * radius;
            for (int i = 0; i < length; ++i) wrapped[i] = in[(i - radius) & (bins - 1)];
            for (int i = 0; i < length; ++i) ahead[i] = i % width == 0 ? wrapped[i] : op(ahead[i - 1], wrapped[i]);
            for (int i = length - 1; i >= 0; --i)
                behind[i] = (i == length - 1 || i % width == width - 1) ? wrapped[i] : op(behind[i + 1], wrapped[i]);
            for (int i = 0; i < bins; ++i) out[i] = op(behind[i], ahead[i + width - 1]);
        }
    }

    // The loudness outline of one cycle of peaks: gaps between the swings filled in, then the corners taken off
    inline void outline(const float* peaks, float* out, int radius)
    {
        static_assert((bins & (bins - 1)) == 0, "the wrap-around uses a bit mask");
        radius = std::clamp(radius, 1, maxRadius);
        float grown[bins], closed[bins];
        detail::slide(peaks, grown, radius, [](float a, float b) { return a > b ? a : b; });
        detail::slide(grown, closed, radius, [](float a, float b) { return a < b ? a : b; });
        const int r = std::max(1, radius / 3);
        double sum = 0.0;
        for (int k = -r; k <= r; ++k) sum += closed[k & (bins - 1)];
        for (int i = 0; i < bins; ++i)
        {
            out[i] = (float) (sum / (double) (2 * r + 1));
            sum += (double) closed[(i + r + 1) & (bins - 1)] - (double) closed[(i - r) & (bins - 1)];
        }
    }

    // `kick` and `sound`: the loudest |sample| in each slice of the cycle, the sound as it comes in. `gain`: what the plugin
    // does to it there. Each side is measured against its own peak, so send levels don't matter; the sound is judged after
    // the ducking, so a spot where the curve really ducks under the kick is not marked. Returns false if nothing is marked.
    inline bool mark(const float* kick, const float* sound, const float* gain, double cycleSeconds, bool* marked)
    {
        std::fill(marked, marked + bins, false);
        const int radius = radiusFor(cycleSeconds);
        float k[bins], s[bins];
        outline(kick, k, radius);
        outline(sound, s, radius);
        float kickPeak = 0.f, outPeak = 0.f;
        for (int i = 0; i < bins; ++i)
        {
            s[i] *= gain[i]; // after the outline, so the curve's edges stay where they are
            kickPeak = std::max(kickPeak, k[i]);
            outPeak = std::max(outPeak, s[i]);
        }
        if (! (kickPeak >= silence && outPeak >= silence)) return false;

        bool any = false;
        int from = -1;
        for (int i = 0; i <= bins; ++i)
        {
            const bool both = i < bins && std::min(k[i] / kickPeak, s[i] / outPeak) > threshold;
            if (both && from < 0) from = i;
            if (! both && from >= 0)
            {
                if (i - from >= shortest) { std::fill(marked + from, marked + i, true); any = true; }
                from = -1;
            }
        }
        return any;
    }
}
