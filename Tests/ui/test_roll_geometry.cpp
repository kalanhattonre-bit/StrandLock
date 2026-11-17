// The roll's geometry against Tests/fixtures/roll/geometry.json (PLUGIN-PLAN 4 M4, 7.6 R1): rows and beats, each
// note's drawn line (its pitch, its glide and its vibrato squiggle), where its nodes, segment diamonds and slide
// handles sit, the inverse of a lane's y, and what a grid of points hits. tools/fixtures/roll.js recorded every value
// from sl-ui.js's own functions. Values within 1e-9 px; the Float32 line within 2 ulps; ids and indices exact.

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "doc/RollState.h"
#include "model/ModelFixture.h"
#include "model/Presets.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/roll/RollGeometry.h"

#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::roll::RollGeometry;

    constexpr double kTol = 1e-9;

    struct Ctx
    {
        std::map<std::string, model::Clip> clips;
        std::map<std::string, model::ClipPtr> shared;
        std::map<std::string, core::Inst> insts;

        model::ClipPtr clip (const std::string& name)
        {
            if (auto it = shared.find (name); it != shared.end())
                return it->second;
            model::ClipPtr c;
            if (name.rfind ("preset:", 0) == 0)
            {
                if (auto p = model::loadClipPreset (name.substr (7)))
                    c = std::make_shared<const model::Clip> (*p);
            }
            else if (auto it2 = clips.find (name); it2 != clips.end())
            {
                c = std::make_shared<const model::Clip> (it2->second);
            }
            shared[name] = c;
            return c;
        }
    };

    std::string num (double v) { return core::jsNumberToString (v); }

    bool close (double got, const core::JsonValue& want, std::string& why, const std::string& what, bool& exact)
    {
        const double w = fixture::num (want);
        if (! fixture::same (got, w))
            exact = false;
        if (fixture::withinAbs (got, w, kTol))
            return true;
        if (why.empty())
            why = what + ": got " + num (got) + ", want " + num (w);
        return false;
    }

    bool closePoint (double x, double y, const core::JsonValue& want, std::string& why, const std::string& what, bool& exact)
    {
        bool ok = close (x, want[0], why, what + " x", exact);
        ok = close (y, want[1], why, what + " y", exact) && ok;
        return ok;
    }

    bool closeF32 (float got, const core::JsonValue& want, std::string& why, const std::string& what, bool& exact)
    {
        const float w = (float) fixture::num (want);
        const auto u = fixture::ulps (got, w);
        if (u != 0)
            exact = false;
        if (u <= 2)
            return true;
        if (why.empty())
            why = what + ": got " + num ((double) got) + ", want " + num ((double) w);
        return false;
    }

    RollGeometry geometryFor (Ctx& ctx, const core::JsonValue& in)
    {
        RollGeometry geo;
        geo.setInst (ctx.insts[in["inst"].asString()]);
        geo.setBpm (fixture::num (in["bpm"]));
        geo.setZoom (fixture::num (in["BW"]), fixture::num (in["RH"]));
        geo.setClip (ctx.clip (in["clip"].asString()));
        return geo;
    }

    bool noteCase (Ctx& ctx, const core::JsonValue& in, const core::JsonValue& out, std::string& why, bool& exact)
    {
        RollGeometry geo = geometryFor (ctx, in);
        const model::Note* n = geo.getClip() != nullptr ? modelfx::findNote (*geo.getClip(), in["id"].asString()) : nullptr;
        if (n == nullptr)
        {
            why = "no note " + in["id"].asString();
            return false;
        }
        const auto& G = geo.geom (*n);
        bool ok = true;
        if (G.K != (int) fixture::num (out["K"]))
        {
            why = "K " + std::to_string (G.K) + " want " + num (fixture::num (out["K"]));
            return false;
        }
        for (std::size_t i = 0; i < out["base"].size(); ++i)
        {
            const auto idx = (std::size_t) fixture::num (out["base"][i][0]);
            ok = closeF32 (G.base[idx], out["base"][i][1], why, "base[" + std::to_string (idx) + "]", exact) && ok;
            ok = closeF32 (G.vib[idx], out["vib"][i][1], why, "vib[" + std::to_string (idx) + "]", exact) && ok;
        }
        ok = close (G.smin, out["smin"], why, "smin", exact) && ok;
        ok = close (G.smax, out["smax"], why, "smax", exact) && ok;
        if (out["glide"].isNull() != ! G.glide.has_value())
        {
            why = "glide presence differs";
            return false;
        }
        if (G.glide)
        {
            ok = close (G.glide->d, out["glide"]["d"], why, "glide d", exact) && ok;
            ok = close (G.glide->len, out["glide"]["len"], why, "glide len", exact) && ok;
            ok = close (G.glide->curve, out["glide"]["curve"], why, "glide curve", exact) && ok;
        }
        for (std::size_t i = 0; i < out["baseYAt"].size(); ++i)
            ok = close (geo.baseYAt (*n, fixture::num (out["baseYAt"][i][0])), out["baseYAt"][i][1], why, "baseYAt", exact) && ok;
        for (const char* which : { "box", "boxRibbon" })
        {
            const auto b = geo.noteBox (*n, std::string (which) == "boxRibbon");
            const auto& w = out[which];
            ok = close (b.x0, w["x0"], why, std::string (which) + " x0", exact) && ok;
            ok = close (b.x1, w["x1"], why, std::string (which) + " x1", exact) && ok;
            ok = close (b.top, w["top"], why, std::string (which) + " top", exact) && ok;
            ok = close (b.bottom, w["bottom"], why, std::string (which) + " bottom", exact) && ok;
        }
        const auto gh = geo.glideHandles (*n);
        if (out["glideHandles"].isNull() != ! gh.has_value())
        {
            if (why.empty())
                why = "glide handles presence differs";
            return false;
        }
        if (gh)
        {
            ok = closePoint (gh->start.x, gh->start.y, out["glideHandles"]["start"], why, "glide start", exact) && ok;
            ok = closePoint (gh->mid.x, gh->mid.y, out["glideHandles"]["mid"], why, "glide mid", exact) && ok;
            ok = ok && gh->glide.toId == out["glideHandles"]["g"]["toId"].asString() && gh->glide.linked == out["glideHandles"]["g"]["linked"].asBool();
        }
        const auto& lanes = out["lanes"];
        for (std::size_t li = 0; li < lanes.size(); ++li)
        {
            const auto lane = core::findLane (lanes.keyAt (li));
            const auto& L = lanes.valueAt (li);
            if (! lane)
            {
                why = "unknown lane " + lanes.keyAt (li);
                return false;
            }
            const std::string ln = lanes.keyAt (li);
            if (L["nodes"].size() != model::noteLane (*n, *lane).size())
            {
                why = ln + " node count";
                return false;
            }
            for (std::size_t k = 0; k < L["nodes"].size(); ++k)
            {
                const auto p = geo.nodeXY (*n, *lane, (int) k);
                ok = closePoint (p.x, p.y, L["nodes"][k], why, ln + " node " + std::to_string (k), exact) && ok;
            }
            for (std::size_t k = 0; k < L["segs"].size(); ++k)
            {
                const auto s = geo.segXY (*n, *lane, (int) k);
                if (L["segs"][k].isNull() != ! s.has_value())
                {
                    if (why.empty())
                        why = ln + " seg " + std::to_string (k) + " presence differs";
                    ok = false;
                    continue;
                }
                if (s)
                    ok = closePoint (s->x, s->y, L["segs"][k], why, ln + " seg " + std::to_string (k), exact) && ok;
            }
            for (std::size_t i = 0; i < L["valueY"].size(); ++i)
            {
                const auto& r = L["valueY"][i];
                ok = close (geo.valueY (*n, *lane, fixture::num (r[0]), fixture::num (r[1])), r[2], why, ln + " valueY", exact) && ok;
            }
            for (std::size_t i = 0; i < L["laneValue"].size(); ++i)
            {
                const auto& r = L["laneValue"][i];
                ok = close (geo.laneValue (*n, *lane, fixture::num (r[0]), fixture::num (r[1])), r[2], why, ln + " laneValue", exact) && ok;
            }
        }
        return ok;
    }

    bool hitCase (Ctx& ctx, const core::JsonValue& in, const core::JsonValue& out, std::string& why)
    {
        RollGeometry geo = geometryFor (ctx, in);
        const auto clip = geo.getClip();
        doc::RollState st;
        st.mode = (int) fixture::num (in["mode"]);
        st.selected = modelfx::stringsFrom (in["sel"]);
        st.primary = in["primary"].isString() ? in["primary"].asString() : (st.selected.empty() ? std::string() : st.selected.front());
        const auto lane = st.activeLane();
        const auto& pts = in["points"];
        const model::Note* P = nullptr;
        if (! st.primary.empty())
            P = modelfx::findNote (*clip, st.primary);
        int bad = 0;
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const double x = fixture::num (pts[i][0]), y = fixture::num (pts[i][1]);
            const auto h = geo.hitNote (*clip, st, x, y);
            const auto& wantNote = out["note"][i];
            bool ok = wantNote.isNull() ? h == nullptr : (h != nullptr && h->id == wantNote.asString());
            if (ok && h != nullptr)
                ok = geo.onNoteLine (*h, x, y) == out["line"][i].asBool();
            if (lane)
            {
                const auto hn = geo.hitNode (*clip, st, x, y, *lane);
                const auto& wantNode = out["node"][i];
                ok = ok && (wantNode.isNull() ? ! hn.has_value()
                                              : (hn && hn->note->id == wantNode[0].asString() && hn->k == (int) fixture::num (wantNode[1])));
                const int seg = P != nullptr ? geo.hitSeg (*P, x, y, *lane) : -1;
                ok = ok && seg == (int) fixture::num (out["seg"][i]);
            }
            if (! ok)
            {
                if (bad++ == 0)
                    why = "point (" + num (x) + ", " + num (y) + "): note " + (h ? h->id : std::string ("none")) + ", want " + core::toJson (wantNote);
            }
        }
        if (bad > 1)
            why += " (" + std::to_string (bad) + " points differ)";
        return bad == 0;
    }
}

SL_TEST ("ui", rollGeometryMatchesFixture)
{
    section ("Roll geometry: rows, note lines, nodes, diamonds, slide handles and hit tests equal sl-ui.js (roll/geometry.json)");

    core::JsonValue doc;
    if (! fixture::load ("roll/geometry.json", doc))
        return;
    Ctx ctx;
    if (! modelfx::clipsFrom (doc, ctx.clips))
        return;
    for (std::size_t i = 0; i < doc["insts"].size(); ++i)
        ctx.insts[doc["insts"].keyAt (i)] = core::normalizeInst (core::instFromJson (doc["insts"].valueAt (i)));

    const auto& cases = doc["cases"];
    modelfx::FileTally tally ("roll/geometry.json", cases.size());
    int rows = 0, notes = 0, hits = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& c = cases[i];
        const std::string fn = c["fn"].asString();
        const auto& in = c["input"];
        const auto& out = c["output"];
        std::string why;
        bool exact = true, ok = false;
        if (fn == "rows")
        {
            RollGeometry geo;
            geo.setZoom (fixture::num (in["BW"]), fixture::num (in["RH"]));
            ok = close (geo.laneHeight(), out["laneH"], why, "laneH", exact);
            for (std::size_t k = 0; k < out["cy"].size(); ++k)
                ok = close (geo.cy (fixture::num (out["cy"][k][0])), out["cy"][k][1], why, "cy", exact) && ok;
            for (std::size_t k = 0; k < out["pitchAt"].size(); ++k)
            {
                const int p = geo.pitchAt (fixture::num (out["pitchAt"][k][0]));
                if (p != (int) fixture::num (out["pitchAt"][k][1]))
                {
                    ok = false;
                    if (why.empty())
                        why = "pitchAt " + num (fixture::num (out["pitchAt"][k][0])) + " = " + std::to_string (p);
                }
            }
            ++rows;
        }
        else if (fn == "note")
        {
            ok = noteCase (ctx, in, out, why, exact);
            ++notes;
        }
        else if (fn == "hit")
        {
            ok = hitCase (ctx, in, out, why);
            ++hits;
        }
        else
        {
            why = "unknown fn " + fn;
        }
        if (ok)
            tally.pass (exact);
        else
            tally.fail (fn + " " + core::toJson (in).substr (0, 160) + ": " + why);
    }
    tally.finish();
    note (fmt ("rows %d, notes %d, hit grids %d", rows, notes, hits));
    check (rows > 0 && notes > 0 && hits > 0, "the file has row, note and hit cases");
}

SL_TEST ("ui", rollGeometryJsHypot)
{
    section ("Roll geometry: distances are V8's Math.hypot, so a hit at the edge of its size decides as the prototype does");

    // Values from node (Math.hypot): 3-4-5, a tiny and a huge pair, a pair where sqrt (a*a + b*b) differs by an ulp.
    check (fixture::same (ui::roll::jsHypot (3.0, 4.0), 5.0) && fixture::same (ui::roll::jsHypot (-3.0, 4.0), 5.0), "hypot (3, 4) = 5, signs ignored");
    check (fixture::same (ui::roll::jsHypot (0.0, 0.0), 0.0) && fixture::same (ui::roll::jsHypot (0.0, -7.5), 7.5), "hypot with a 0 is the other's magnitude");
    check (fixture::same (ui::roll::jsHypot (1e200, 1e200), 1.414213562373095e200), "no overflow: hypot (1e200, 1e200) = 1.414213562373095e200");
    check (fixture::same (ui::roll::jsHypot (1e-200, 1e-200), 1.414213562373095e-200), "no underflow: hypot (1e-200, 1e-200) = 1.414213562373095e-200");
    // two pairs where sqrt (a * a + b * b) is one ulp from what the prototype measures
    check (fixture::same (ui::roll::jsHypot (7.178459167480469, 6.924934387207031), 9.97421637454827)
               && fixture::same (ui::roll::jsHypot (-11.642063856124878, 11.502940654754639), 16.36628530048576),
           "hypot (7.178459167480469, 6.924934387207031) = 9.97421637454827 and hypot (-11.642063856124878, 11.502940654754639) = 16.36628530048576 (sqrt of the sum of squares: ...268 and ...764)");
    check (std::isinf (ui::roll::jsHypot (std::nan (""), INFINITY)) && std::isnan (ui::roll::jsHypot (std::nan (""), 1.0)),
           "an infinity wins over NaN; otherwise NaN");
}
