// The roll's geometry against Tests/fixtures/roll/geometry.json (PLUGIN-PLAN 4 M4, 7.6 R1): rows and beats, each
// note's drawn line (its pitch, its glide and its vibrato squiggle), where its nodes, segment diamonds and slide
// handles sit, the inverse of a lane's y, and what a grid of points hits. tools/fixtures/roll.js recorded every value
// from sl-ui.js's own functions. Values within 1e-9 px; the Float32 line within 2 ulps; ids and indices exact.
// The view part (the roll's size, the scroll, the zoom buttons, the wheel zoom, the first view, UIST's view state)
// is checked against values node computed with sl-ui.js's own lines over a browser-like scroller.

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
#include <utility>

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
            // the prototype's clips never tie across the loop (F3 off: the parity setting), so the glides are its own
            model::ClipPtr c;
            if (name.rfind ("preset:", 0) == 0)
            {
                if (auto p = model::loadClipPreset (name.substr (7)))
                {
                    p->tieAcrossLoop = false;
                    c = std::make_shared<const model::Clip> (*p);
                }
            }
            else if (auto it2 = clips.find (name); it2 != clips.end())
            {
                model::Clip parity = it2->second;
                parity.tieAcrossLoop = false;
                c = std::make_shared<const model::Clip> (std::move (parity));
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

SL_TEST ("ui", rollViewScrollAndZoom)
{
    section ("Roll geometry: the view's scroll and zoom follow sl-ui.js (sizeSpacer, the zoom buttons, Ctrl + wheel, the first view)");

    // Each row's values come from sl-ui.js's own lines (sizeSpacer, zoom (h, v), the Ctrl + wheel listener and the
    // first view in init) run in node over a browser-like scroller that keeps its scroll in 0 .. content - view.
    // Strand Demo (4 bars, 26 notes, median pitch 67) in a 1018 x 486 view.
    struct Want
    {
        const char* what;
        double bw, rh, sx, sy, cw, ch;
    };
    RollGeometry geo;
    const auto demo = model::loadClipPreset ("strand-demo");
    if (! demo)
    {
        check (false, "the Strand Demo preset loads");
        return;
    }
    geo.setClip (std::make_shared<const model::Clip> (*demo));
    geo.setViewSize (1018.0, 486.0);

    int rowsRun = 0, bad = 0;
    std::string firstBad;
    auto expect = [&] (const Want& w)
    {
        ++rowsRun;
        const double got[] = { geo.beatWidth(), geo.rowHeight(), geo.scrollX(), geo.scrollY(), geo.contentWidth(), geo.contentHeight() };
        const double want[] = { w.bw, w.rh, w.sx, w.sy, w.cw, w.ch };
        const char* names[] = { "beat width", "row height", "scroll x", "scroll y", "roll width", "roll height" };
        for (int i = 0; i < 6; ++i)
        {
            if (fixture::withinAbs (got[i], want[i], kTol))
                continue;
            if (bad++ == 0)
                firstBad = std::string (w.what) + ": " + names[i] + " " + num (got[i]) + ", want " + num (want[i]);
            break;
        }
    };

    expect ({ "1018 x 486", 64, 14, 0, 0, 1084, 1022 });
    geo.centreOnNotes();
    expect ({ "the first view", 64, 14, 0, 100, 1084, 1022 });
    geo.zoomBy (1.3, 1.0);
    expect ({ "zoom across x 1.3", 83.2, 14, 152.70000000000005, 100, 1392, 1022 });
    geo.zoomBy (1.0, 1.25);
    expect ({ "zoom rows x 1.25 (17.5 rounds to 18)", 83.2, 18, 152.70000000000005, 198, 1392, 1314 });
    geo.zoomBy (1.0, 1.25);
    expect ({ "zoom rows x 1.25 again", 83.2, 23, 152.70000000000005, 320.5, 1392, 1679 });
    geo.zoomBy (1.0, 0.8);
    expect ({ "zoom rows x 0.8", 83.2, 18, 152.70000000000005, 198, 1392, 1314 });
    for (int i = 0; i < 8; ++i)
        geo.zoomBy (1.0 / 1.3, 1.0);
    expect ({ "zoom across / 1.3, 8 times (to 12 px a beat)", 12, 18, 0, 198, 252, 1314 });
    for (int i = 0; i < 6; ++i)
        geo.zoomBy (1.0, 0.8);
    expect ({ "zoom rows x 0.8, 6 times (to 8 px a row)", 12, 8, 0, 0, 252, 584 });
    for (int i = 0; i < 30; ++i)
        geo.zoomBy (1.3, 1.25);
    expect ({ "zoom both up 30 times (to 400 and 30)", 400, 30, 3006.0897138599566, 668.25, 6460, 2190 });
    geo.setScroll (2500.0, 700.0);
    expect ({ "scroll to 2500, 700", 400, 30, 2500, 700, 6460, 2190 });
    geo.wheelZoom (true, 300.0);
    expect ({ "wheel in at x 300 (already 400)", 400, 30, 2500, 700, 6460, 2190 });
    geo.wheelZoom (false, 300.0);
    expect ({ "wheel out at x 300", 347.82608695652175, 30, 2134.782608695652, 700, 5626, 2190 });
    geo.wheelZoom (false, 900.0);
    expect ({ "wheel out at x 900", 302.4574669187146, 30, 1738.9413988657848, 700, 4900, 2190 });
    geo.setScroll (1e9, -5.0);
    expect ({ "scroll to 1e9, -5 (kept inside the roll)", 302.4574669187146, 30, 3882, 0, 4900, 2190 });
    model::Clip two = *demo;
    two.bars = 2;
    geo.setClip (std::make_shared<const model::Clip> (two));
    expect ({ "the clip cut to 2 bars (the scroll follows the narrower roll)", 302.4574669187146, 30, 1462, 0, 2480, 2190 });
    model::Clip none = two;
    none.notes.clear();
    none.bars = 8;
    geo.setClip (std::make_shared<const model::Clip> (none));
    geo.setZoom (64.0, 14.0);
    geo.setScroll (0.0, 0.0);
    geo.centreOnNotes();
    expect ({ "the first view of 8 empty bars (row 66 in the middle)", 64, 14, 0, 184, 2108, 1022 });

    note (fmt ("%d view states compared%s%s", rowsRun, firstBad.empty() ? "" : "; first difference: ", firstBad.c_str()));
    check (bad == 0, fmt ("the zoom, the scroll and the roll's size equal sl-ui.js at every step within 1e-9 px (%d of %d differ)", bad, rowsRun));

    // view <-> roll pixels, and the ruler's beat (sl-ui.js: (clientX - left + scrollLeft) / BW)
    geo.setScroll (300.0, 120.0);
    const auto r = geo.toRoll (10.0, 20.0);
    const auto v = geo.toView (r.x, r.y);
    check (fixture::same (r.x, 310.0) && fixture::same (r.y, 140.0) && fixture::same (v.x, 10.0) && fixture::same (v.y, 20.0)
               && fixture::same (geo.beatAt (geo.toRoll (128.0, 0.0).x), (128.0 + 300.0) / 64.0)
               && geo.pitchAt (geo.toRoll (0.0, 20.0).y) == 96 - (int) std::floor (140.0 / 14.0),
           "a view point is the roll point less the scroll, and back; the beat and the row under it follow");

    // UIST's view state: zoom factors and the scroll in beats and rows, through float32 as the chunk stores them
    geo.setZoom (83.2, 18.0);
    geo.setScroll (500.0, 400.0);
    const auto s = geo.viewState();
    check (fixture::withinAbs (s.zoomH, 1.3, 1e-12) && fixture::withinAbs (s.zoomV, 18.0 / 14.0, 1e-12)
               && fixture::withinAbs (s.scrollBeat, 500.0 / 83.2, 1e-12) && fixture::withinAbs (s.scrollRow, 400.0 / 18.0, 1e-12),
           fmt ("the view state is the zoom over 64 px a beat and 14 px a row, the scroll in beats and rows (%s, %s, %s, %s)",
                num (s.zoomH).c_str(), num (s.zoomV).c_str(), num (s.scrollBeat).c_str(), num (s.scrollRow).c_str()));
    RollGeometry back;
    back.setClip (geo.getClip());
    back.setViewSize (1018.0, 486.0);
    RollGeometry::ViewState stored;
    stored.zoomH = (double) (float) s.zoomH;
    stored.zoomV = (double) (float) s.zoomV;
    stored.scrollBeat = s.scrollBeat;
    stored.scrollRow = (double) (float) s.scrollRow;
    back.setViewState (stored);
    check (fixture::same (back.rowHeight(), 18.0) && fixture::withinAbs (back.beatWidth(), 83.2, 1e-5)
               && fixture::withinAbs (back.scrollX(), 500.0, 1e-3) && fixture::withinAbs (back.scrollY(), 400.0, 1e-3),
           fmt ("a stored view state comes back: 18 px rows exactly (float32 gives %s before rounding), 83.2 px beats, the scroll within 0.001 px",
                num (14.0 * stored.zoomV).c_str()));
    RollGeometry::ViewState junk;
    junk.zoomH = std::nan ("");
    junk.zoomV = INFINITY;
    junk.scrollBeat = std::nan ("");
    junk.scrollRow = -INFINITY;
    back.setViewState (junk);
    back.setViewSize (std::nan (""), -3.0);
    check (fixture::same (back.beatWidth(), 64.0) && fixture::same (back.rowHeight(), 14.0) && fixture::same (back.scrollX(), 0.0)
               && fixture::same (back.scrollY(), 0.0) && fixture::same (back.viewWidth(), 0.0) && fixture::same (back.viewHeight(), 0.0),
           "a view state or size that is not a number gives the default zoom, no scroll and an empty view");
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
