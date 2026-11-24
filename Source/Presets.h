#pragma once
// Factory curve presets. Pure C++ (no JUCE) so they can be unit-tested anywhere.
//
// Every preset is one LFO cycle: the shape is applied to whichever curve you are editing (the single curve, or
// the selected band), and the Rate knob decides how long a cycle is. "x4" / "x8" presets repeat the shape inside
// the cycle, so one bar at 1/1 gives four or eight pumps or gate steps. All presets obey the plugin's rules: the
// first and last point share one height (no click at the loop seam), and the curve only ever ducks (0..1).
#include <algorithm>
#include <string>
#include <vector>
#include "Envelope.h"

struct CurvePreset
{
    std::string group, name;
    Envelope env;
};

namespace presets
{
    // Repeat a one-cycle shape n times inside the cycle. The first and last point of the shape share one height,
    // so the copies join seamlessly (the junction point keeps the previous copy's in handle and the next one's out handle).
    inline Envelope tile(const Envelope& base, int n)
    {
        Envelope out;
        out.points.clear();
        const float fn = (float) n;
        for (int k = 0; k < n; ++k)
            for (size_t i = 0; i < base.points.size(); ++i)
            {
                EnvPoint p = base.points[i];
                p.x = ((float) k + p.x) / fn;
                p.inDx /= fn;
                p.outDx /= fn;
                if (k > 0 && i == 0) // junction with the previous copy: it already has its in handle, add this out handle
                {
                    out.points.back().outDx = p.outDx;
                    out.points.back().outDy = p.outDy;
                    out.points.back().bend = p.bend;
                    out.points.back().bendX = p.bendX;
                    continue;
                }
                out.points.push_back(p);
            }
        out.normalize();
        return out;
    }

    inline Envelope make(std::vector<EnvPoint> pts)
    {
        Envelope e;
        e.points = std::move(pts);
        e.normalize();
        return e;
    }

    inline std::vector<CurvePreset> factory()
    {
        std::vector<CurvePreset> list;
        auto add = [&](const char* group, const char* name, Envelope e) { list.push_back({ group, name, std::move(e) }); };

        // ---- Sidechain: ducked on the beat, then back up. ({ x, y, inDx, inDy, outDx, outDy })
        Envelope pump;                                    // the classic house pump (also the plugin's default)
        add("Sidechain", "House pump", pump);

        // Gentle: only dips to 35% and the dip is centred on the loop seam, so every edge is smooth.
        add("Sidechain", "Soft duck", make({ { 0.00f, 0.35f, 0.f, 0.f, 0.10f, 0.f },
                                             { 0.30f, 1.00f, -0.10f, 0.f, 0.f, 0.f },
                                             { 0.70f, 1.00f, 0.f, 0.f, 0.10f, 0.f },
                                             { 1.00f, 0.35f, -0.10f, 0.f, 0.f, 0.f } }));

        // Hard: full duck, held solid for a moment, then a straight steep ramp back and a sharp drop at the end.
        add("Sidechain", "Hard duck", make({ { 0.00f, 0.f }, { 0.08f, 0.f }, { 0.32f, 1.f }, { 0.97f, 1.f }, { 1.00f, 0.f } }));

        // Long release: drops, then climbs back slowly, easing into full level late in the cycle.
        add("Sidechain", "Long release", make({ { 0.00f, 0.f, 0.f, 0.f, 0.12f, 0.60f },
                                                { 0.90f, 1.f, -0.45f, 0.f, 0.f, 0.f },
                                                { 0.97f, 1.f, 0.f, 0.f, 0.f, 0.f },
                                                { 1.00f, 0.f, 0.f, 0.f, 0.f, 0.f } }));

        add("Sidechain", "Pump x2", tile(pump, 2));

        // ---- Kick shapes: the usual "rise, hold, drop" and "drop, hold, return" volume shapes of kick-sidechain tools.
        add("Kick shapes", "Fast rise", make({ { 0.00f, 0.f, 0.f, 0.f, 0.08f, 0.f },
                                               { 0.22f, 1.f, -0.08f, 0.f, 0.f, 0.f },
                                               { 0.98f, 1.f }, { 1.00f, 0.f } }));
        add("Kick shapes", "Smooth rise", make({ { 0.00f, 0.f, 0.f, 0.f, 0.14f, 0.f },
                                                 { 0.38f, 1.f, -0.14f, 0.f, 0.f, 0.f },
                                                 { 0.98f, 1.f }, { 1.00f, 0.f } }));
        add("Kick shapes", "Slow rise", make({ { 0.00f, 0.f, 0.f, 0.f, 0.25f, 0.f },
                                               { 0.65f, 1.f, -0.25f, 0.f, 0.f, 0.f },
                                               { 0.98f, 1.f }, { 1.00f, 0.f } }));
        add("Kick shapes", "Rounded pump", make({ { 0.00f, 0.f, 0.f, 0.f, 0.10f, 0.f },
                                                  { 0.30f, 1.f, -0.10f, 0.f, 0.f, 0.f },
                                                  { 0.55f, 1.f, 0.f, 0.f, 0.20f, 0.f },
                                                  { 1.00f, 0.f, -0.20f, 0.f, 0.f, 0.f } }));
        add("Kick shapes", "Hump", make({ { 0.00f, 0.f, 0.f, 0.f, 0.f, 0.f },
                                          { 0.25f, 0.f, 0.f, 0.f, 0.15f, 0.f },
                                          { 0.62f, 1.f, -0.15f, 0.f, 0.15f, 0.f },
                                          { 1.00f, 0.f, -0.22f, 0.f, 0.f, 0.f } }));
        add("Kick shapes", "Late rise", make({ { 0.00f, 1.f }, { 0.03f, 0.f },
                                               { 0.55f, 0.f, 0.f, 0.f, 0.20f, 0.f },
                                               { 1.00f, 1.f, -0.20f, 0.f, 0.f, 0.f } }));
        add("Kick shapes", "Dip & snap", make({ { 0.00f, 1.f, 0.f, 0.f, 0.15f, 0.f },
                                                { 0.40f, 0.f, -0.15f, 0.f, 0.f, 0.f },
                                                { 0.97f, 0.f }, { 1.00f, 1.f } }));
        add("Kick shapes", "Ramp dip", make({ { 0.00f, 1.f }, { 0.45f, 0.f }, { 0.97f, 0.f }, { 1.00f, 1.f } }));
        add("Kick shapes", "Double bump", make({ { 0.00f, 0.f, 0.f, 0.f, 0.06f, 0.f },
                                                 { 0.18f, 0.7f, -0.06f, 0.f, 0.06f, 0.f },
                                                 { 0.34f, 0.3f, -0.06f, 0.f, 0.20f, 0.f },
                                                 { 0.95f, 1.f, -0.30f, 0.f, 0.f, 0.f },
                                                 { 1.00f, 0.f } }));

        // ---- Shapes: tremolo and ramps.
        add("Shapes", "Sine", make({ { 0.0f, 1.f, 0.f, 0.f, 0.18f, 0.f },
                                     { 0.5f, 0.f, -0.18f, 0.f, 0.18f, 0.f },
                                     { 1.0f, 1.f, -0.18f, 0.f, 0.f, 0.f } }));
        add("Shapes", "Triangle", make({ { 0.0f, 1.f }, { 0.5f, 0.f }, { 1.0f, 1.f } }));
        add("Shapes", "Saw down", make({ { 0.000f, 1.f }, { 0.998f, 0.f }, { 1.000f, 1.f } }));
        add("Shapes", "Saw up", make({ { 0.000f, 0.f }, { 0.998f, 1.f }, { 1.000f, 0.f } }));
        add("Shapes", "Flat (no ducking)", make({ { 0.f, 1.f }, { 1.f, 1.f } }));
        return list;
    }

    // A one-line text form for the clipboard and the preset files.
    inline std::string toText(const Envelope& e) { return "FreeLFOCurve1:" + e.serialize(); }

    // Parses toText() output; false if the text is not a curve from this plugin.
    inline bool fromText(const std::string& text, Envelope& out)
    {
        const std::string prefix = "FreeLFOCurve1:";
        if (text.compare(0, prefix.size(), prefix) != 0) return false;
        const std::string payload = text.substr(prefix.size());
        if (std::count(payload.begin(), payload.end(), ';') < 2) return false;   // a curve has at least two points
        const Envelope e = Envelope::deserialize(payload);
        if (e.points.size() < 2) return false;
        out = e;
        return true;
    }
}
