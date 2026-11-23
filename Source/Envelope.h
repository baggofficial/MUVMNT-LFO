#pragma once
// Pure C++ (no JUCE) so it can be unit-tested anywhere.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

struct EnvPoint
{
    float x = 0.f;      // phase position 0..1
    float y = 0.f;      // value 0..1
    float curve = 0.f;  // -1..1, bends the segment that STARTS at this point
};

class Envelope
{
public:
    static constexpr int tableSize = 2048;

    std::vector<EnvPoint> points;

    Envelope() { resetToPump(); }

    void resetToPump() { points = { { 0.f, 0.f, 0.4f }, { 0.6f, 1.f, 0.f }, { 1.f, 1.f, 0.f } }; }

    // curve > 0: fast start, slow end. curve < 0: slow start, fast end.
    static float shape(float t, float c)
    {
        if (std::fabs(c) < 1e-4f) return t;
        return std::pow(t, std::pow(2.f, -c * 3.f));
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
        const float span = b.x - a.x;
        if (span <= 1e-6f) return b.y;

        const float t = std::clamp((phase - a.x) / span, 0.f, 1.f);
        return a.y + (b.y - a.y) * shape(t, a.curve);
    }

    // Writes tableSize + 1 values (last one == value at phase 1) for cheap interpolated lookup.
    void bake(float* out) const
    {
        for (int i = 0; i <= tableSize; ++i)
            out[i] = (i == tableSize) ? evaluate(0.999999f) : evaluate((float) i / (float) tableSize);
    }

    // Keep the invariants the editor and evaluator rely on.
    void normalize()
    {
        if (points.size() < 2) resetToPump();
        std::stable_sort(points.begin(), points.end(), [](auto& l, auto& r) { return l.x < r.x; });
        for (auto& p : points)
        {
            p.x = std::clamp(p.x, 0.f, 1.f);
            p.y = std::clamp(p.y, 0.f, 1.f);
            p.curve = std::clamp(p.curve, -1.f, 1.f);
        }
        points.front().x = 0.f;
        points.back().x = 1.f;
    }

    std::string serialize() const
    {
        std::ostringstream s;
        for (auto& p : points) s << p.x << ',' << p.y << ',' << p.curve << ';';
        return s.str();
    }

    static Envelope deserialize(const std::string& str)
    {
        Envelope e;
        e.points.clear();
        std::istringstream s(str);
        std::string item;
        while (std::getline(s, item, ';'))
        {
            EnvPoint p;
            if (std::sscanf(item.c_str(), "%f,%f,%f", &p.x, &p.y, &p.curve) == 3)
                e.points.push_back(p);
        }
        e.normalize();
        return e;
    }
};
