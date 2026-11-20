// Box select (PLUGIN-PLAN 4 M5 "box select", 7.7 S1; idea Y1; sl-ui.js applyBox) through the roll's gestures, with no
// mouse: seeded boxes over a 64-note clip, dragged from empty space in every direction, in Build and in lane modes.
// After every box (and at a move in its middle, since a box selects as it moves) the selection is exactly the notes
// whose rectangle (RollGeometry::noteBox, with the lane band outside Build) meets the box, edges included, in clip
// order: a plain box replaces the selection, Shift adds to what was selected when the box started, Ctrl toggles it.
// The primary note stays while the box keeps it, else the first note selected is the primary. A box changes no note
// and pushes no undo step.
//
// The Nodes switch: in a lane mode with notes selected, a box picks exactly the nodes of the selected notes inside it
// (Shift adds, Ctrl toggles), a press on one of several picked nodes drags them all together as one undo step "Move
// nodes", and the picks go with another mode, with Escape and with Undo; a node added or deleted on a picked note moves
// them along. Pictures of a box in progress and of picked nodes, for review.

#include <juce_audio_processors/juce_audio_processors.h>

#include "RollRig.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Lanes.h"
#include "core/Seeds.h"
#include "model/ClipCodec.h"
#include "model/Clip.h"
#include "model/LaneFormat.h"
#include "model/Node.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/HintBar.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/BoxSelect.h"
#include "ui/roll/Inspector.h"
#include "ui/roll/InspectorText.h"
#include "ui/roll/RollGeometry.h"
#include "ui/roll/RollGestures.h"
#include "ui/roll/RollView.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::ThemeId;
    using ui::roll::Mods;
    using ui::roll::RollGeometry;

    struct Rng
    {
        std::uint32_t seed;
        std::int32_t i = 0;
        double operator()() { return core::rand01 (core::hash32 ((std::int32_t) seed, i++)); }
        int below (int n) { return (int) ((*this)() * n) % n; }
    };

    /** 64 seeded notes over 8 bars (starts on sixteenths, pitches 44 to 84, lengths a sixteenth to 2 beats), each with
        2 to 6 nodes on Volume and on Tone, so a lane mode has nodes to pick. Ids "bx-0" .. "bx-63". */
    model::Clip clip64 (std::uint32_t seed)
    {
        Rng r { seed };
        model::Clip c = model::newClip (8.0);
        for (int i = 0; i < 64; ++i)
        {
            const double l = 0.25 * (double) (1 + r.below (8));
            const double s = std::min (32.0 - l, 0.25 * (double) r.below (128));
            const double p = (double) (44 + r.below (41));
            model::Note n = model::mkNote (p, s, l, "bx-" + std::to_string (i));
            for (const auto lane : { core::LaneId::amp, core::LaneId::tone })
            {
                const int count = 2 + r.below (5);
                model::NodeList nodes;
                for (int k = 0; k < count; ++k)
                    nodes.push_back (model::mkNode ((double) k / (double) (count - 1), 0.1 + 0.8 * r(), model::NodeShape::linear));
                n.lane (lane) = model::normalizeNodes (nodes, core::laneDef (lane));
            }
            c.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
        }
        return c;
    }

    std::string joined (const std::vector<std::string>& ids)
    {
        std::string s;
        for (const auto& id : ids)
            s += (s.empty() ? "" : ",") + id;
        return s;
    }

    bool contains (const std::vector<std::string>& v, const std::string& id)
    {
        return std::find (v.begin(), v.end(), id) != v.end();
    }

    const model::Note* noteIn (const model::Clip& c, const std::string& id)
    {
        for (const auto& n : c.notes)
            if (n->id == id)
                return n.get();
        return nullptr;
    }

    /** The notes whose rectangle meets the box, edges included, in clip order (the M5 table's rule). */
    std::vector<std::string> expectedHits (const RollGeometry& geo, const model::Clip& c, bool ribbon, double x0, double y0, double x1, double y1)
    {
        const double lx = std::min (x0, x1), hx = std::max (x0, x1), ly = std::min (y0, y1), hy = std::max (y0, y1);
        std::vector<std::string> out;
        for (const auto& n : c.notes)
        {
            const auto b = geo.noteBox (*n, ribbon);
            if (b.x1 >= lx && b.x0 <= hx && b.bottom >= ly && b.top <= hy)
                out.push_back (n->id);
        }
        return out;
    }

    /** True where a press hits nothing a gesture takes before a box: no note, no node or segment diamond of the active
        lane, no end or start dot of a selected note, no slide handle. */
    bool emptyAt (RollRig& rig, double x, double y)
    {
        auto& g = rig.gestures();
        g.sync();
        const auto& geo = g.geometry();
        const auto c = rig.doc.current();
        const auto st = rig.doc.rollState();
        if (x < 0.0 || y < 0.0 || geo.hitNote (*c, st, x, y) != nullptr)
            return false;
        if (const auto L = st.activeLane())
        {
            if (geo.hitNode (*c, st, x, y, *L))
                return false;
            if (const auto* P = noteIn (*c, st.primary))
                if (geo.hitSeg (*P, x, y, *L) >= 0)
                    return false;
        }
        for (const auto& n : RollGeometry::selectedNotes (*c, st))
        {
            const auto e = geo.endDot (*n), s = geo.startDot (*n);
            if (std::hypot (x - e.x, y - e.y) < 14.0 || std::hypot (x - s.x, y - s.y) < 14.0)
                return false;
            if (const auto gh = geo.glideHandles (*n))
                if (std::hypot (x - gh->start.x, y - gh->start.y) < 14.0 || std::hypot (x - gh->mid.x, y - gh->mid.y) < 14.0)
                    return false;
        }
        return true;
    }

    /** A seeded point of empty space over the notes' part of the roll. */
    bool emptyPoint (RollRig& rig, Rng& r, double& x, double& y)
    {
        const auto& geo = rig.gestures().geometry();
        for (int tries = 0; tries < 400; ++tries)
        {
            x = r() * 32.0 * geo.beatWidth();
            y = geo.cy (88.0) + r() * (geo.cy (40.0) - geo.cy (88.0));
            if (emptyAt (rig, x, y))
                return true;
        }
        return false;
    }

    /** The picks as text: "id:k,k;id:k". */
    std::string picksText (const std::vector<doc::NodePick>& p)
    {
        std::string s;
        for (const auto& pk : p)
        {
            s += (s.empty() ? "" : ";") + pk.id + ":";
            for (std::size_t i = 0; i < pk.ks.size(); ++i)
                s += (i ? "," : "") + std::to_string (pk.ks[i]);
        }
        return s;
    }

    /** The nodes of lane inside the box, per selected note in time order (the M5 table's rule for the Nodes switch). */
    std::vector<doc::NodePick> expectedNodes (const RollGeometry& geo, const model::Clip& c, const doc::RollState& st, core::LaneId lane,
                                              double x0, double y0, double x1, double y1)
    {
        const double lx = std::min (x0, x1), hx = std::max (x0, x1), ly = std::min (y0, y1), hy = std::max (y0, y1);
        std::vector<doc::NodePick> out;
        for (const auto& n : RollGeometry::selectedNotes (c, st))
        {
            doc::NodePick pk;
            pk.id = n->id;
            const auto& nd = model::noteLane (*n, lane);
            for (int k = 0; k < (int) nd.size(); ++k)
            {
                const auto q = geo.nodeXY (*n, lane, k);
                if (q.x >= lx && q.x <= hx && q.y >= ly && q.y <= hy)
                    pk.ks.push_back (k);
            }
            if (! pk.ks.empty())
                out.push_back (pk);
        }
        return out;
    }

    bool samePicks (const std::vector<doc::NodePick>& a, const std::vector<doc::NodePick>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].id != b[i].id || a[i].ks != b[i].ks)
                return false;
        return true;
    }

    /** A note of the clip with 4 or 5 nodes on lane and at least 1.75 beats long, so a press between two nodes is more
        than a node's hit size from both (null: none). */
    const model::Note* roomyNote (const model::Clip& c, core::LaneId lane)
    {
        for (const auto& n : c.notes)
        {
            const int count = (int) model::noteLane (*n, lane).size();
            if (count >= 4 && count <= 5 && n->l >= 1.75)
                return n.get();
        }
        return nullptr;
    }
}

//==================================================================================================
SL_TEST ("ui", boxSelectSeededBoxes)
{
    section ("Box select: seeded boxes over a 64-note clip select exactly the notes whose rectangle meets the box (Build and "
             "lane modes, every direction); Shift adds, Ctrl toggles; the primary note; no undo step");

    const model::Clip clip = clip64 (6401);
    int boxes = 0, right = 0, middleRight = 0, nonEmpty = 0, partial = 0, steps = 0, clipChanged = 0;
    std::string firstBad;
    for (const int mode : { 1, 3, 5, 8 })
    {
        RollRig rig (clip);
        auto& g = rig.gestures();
        g.setMode (mode);
        const auto bytes0 = model::encodeClip (*rig.doc.current());
        Rng r { (std::uint32_t) (7700 + mode) };
        double clock = 0.0;
        for (int b = 0; b < 40; ++b)
        {
            double x0 = 0.0, y0 = 0.0;
            if (! emptyPoint (rig, r, x0, y0))
                continue;
            // the far corner: up to 8 beats and 30 rows away, in any direction
            const auto& geo = g.geometry();
            const double x1 = x0 + (r() - 0.5) * 16.0 * geo.beatWidth(), y1 = y0 + (r() - 0.5) * 60.0 * geo.rowHeight();
            const double xm = x0 + (x1 - x0) * 0.5, ym = y0 + (y1 - y0) * 0.5;
            const int kind = b % 4;   // 0, 1 plain; 2 Shift; 3 Ctrl
            const Mods mods { kind == 2, kind == 3 };
            const auto before = rig.doc.rollState();

            clock += 1000.0;   // a second apart: never a double-click
            g.pointerDown (x0, y0, mods, clock);
            g.pointerMove (xm, ym, mods);
            const auto middle = rig.doc.rollState();
            g.pointerMove (x1, y1, mods);
            g.pointerUp();
            const auto after = rig.doc.rollState();

            // the expected selection, written out: plain = the hits; Shift = the old selection, then the new hits;
            // Ctrl = the old selection without the hits it held, then the hits it did not hold. The primary note
            // stays while each step keeps it, else the first selected one takes over.
            auto expect = [&before, kind] (const std::vector<std::string>& h)
            {
                if (kind < 2)
                    return h;
                std::vector<std::string> out;
                for (const auto& id : before.selected)
                    if (kind == 2 || ! contains (h, id))
                        out.push_back (id);
                for (const auto& id : h)
                    if (! contains (before.selected, id))
                        out.push_back (id);
                return out;
            };
            auto primaryAfter = [] (const std::string& was, const std::vector<std::string>& sel)
            {
                return ! was.empty() && contains (sel, was) ? was : sel.empty() ? std::string() : sel.front();
            };
            const auto wantMid = expect (expectedHits (geo, *rig.doc.current(), mode != 1, x0, y0, xm, ym));
            const auto hits = expectedHits (geo, *rig.doc.current(), mode != 1, x0, y0, x1, y1);
            const auto want = expect (hits);
            const std::string wantPrimary = primaryAfter (primaryAfter (before.primary, wantMid), want);
            ++boxes;
            const bool ok = after.selected == want && after.primary == wantPrimary;
            right += ok ? 1 : 0;
            middleRight += middle.selected == wantMid ? 1 : 0;
            nonEmpty += hits.empty() ? 0 : 1;
            partial += ! hits.empty() && hits.size() < clip.notes.size() ? 1 : 0;
            if (! ok && firstBad.empty())
                firstBad = fmt ("mode %d box %d (%s): got [%s] primary %s, want [%s] primary %s", mode, b, kind == 2 ? "Shift" : kind == 3 ? "Ctrl" : "plain",
                                joined (after.selected).c_str(), after.primary.c_str(), joined (want).c_str(), wantPrimary.c_str());
        }
        steps += rig.steps;
        clipChanged += model::encodeClip (*rig.doc.current()) == bytes0 ? 0 : 1;
    }
    note (fmt ("%d boxes in Build, Volume, Tone and Sample (Travel): %d met notes, %d met some but not all%s%s", boxes, nonEmpty, partial,
               firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (boxes >= 150 && right == boxes, fmt ("%d of %d boxes select exactly the notes whose rectangle meets them, with the primary kept or the first "
                                                 "one (plain replaces, Shift adds, Ctrl toggles)", right, boxes));
    check (middleRight == boxes, fmt ("%d of %d boxes select as they move: half way through the drag the selection is the half-way box's", middleRight, boxes));
    check (nonEmpty >= boxes / 2 && partial >= boxes / 3, fmt ("the boxes are not trivial: %d met notes, %d met some but not all", nonEmpty, partial));
    check (steps == 0 && clipChanged == 0, fmt ("box select changes no note (%d clips changed) and pushes no undo step (%d)", clipChanged, steps));
}

SL_TEST ("ui", boxSelectRules)
{
    section ("Box select: the rules around the box (a click clears, a small move is a click, a Shift band press, BoxSelect's helpers)");

    const model::Clip clip = clip64 (6402);
    RollRig rig (clip);
    auto& g = rig.gestures();
    const auto& geo = g.geometry();
    Rng r { 991 };
    double clock = 0.0, x = 0.0, y = 0.0;
    const bool found = emptyPoint (rig, r, x, y);

    // a click on empty space (a press that moved under 4 px) clears the selection; with Shift it keeps it
    rig.doc.setSelection ({ "bx-1", "bx-2" }, std::string ("bx-2"));
    clock += 1000.0;
    g.pointerDown (x, y, {}, clock);
    g.pointerMove (x + 3.0, y, {});
    const bool noBoxYet = ! g.box().has_value();
    g.pointerUp();
    const bool cleared = rig.doc.rollState().selected.empty();
    rig.doc.setSelection ({ "bx-1", "bx-2" }, std::string ("bx-2"));
    clock += 1000.0;
    g.pointerDown (x, y, { true, false }, clock);
    g.pointerUp();
    const bool keptWithShift = rig.doc.rollState().selected.size() == 2;
    check (found && noBoxYet && cleared && keptWithShift,
           "a press on empty space that moves 3 px draws no box and clears the selection as a click; with Shift the selection stays");

    // the box the view draws: its corners in order, whichever way it was dragged
    clock += 1000.0;
    g.pointerDown (x, y, {}, clock);
    g.pointerMove (x - 50.0, y - 30.0, {});
    const auto b = g.box();
    const bool drawn = b.has_value() && juce::exactlyEqual (b->x0, x - 50.0) && juce::exactlyEqual (b->y0, y - 30.0)
                       && juce::exactlyEqual (b->x1, x) && juce::exactlyEqual (b->y1, y) && ! g.isNodeBox();
    g.pointerUp();
    check (drawn, "a box dragged up and to the left is drawn from its top left corner to its bottom right one");

    // with Shift, a press on the faint band above a note's line starts a box (a drag) instead of a slide
    rig.doc.setSelection ({ "bx-5" }, std::string ("bx-5"));
    const auto c = rig.doc.current();
    const model::Note* band = nullptr;
    double bx = 0.0, by = 0.0;
    for (const auto& n : c->notes)
    {
        bx = (n->s + n->l * 0.5) * geo.beatWidth();
        by = geo.baseYAt (*n, 0.5) - geo.rowHeight() * 1.5;
        const auto h = geo.hitNote (*c, rig.doc.rollState(), bx, by);
        if (h == n && ! geo.onNoteLine (*n, bx, by) && n->id != "bx-5")
        {
            band = n.get();
            break;
        }
    }
    const auto before = rig.doc.rollState().selected;
    clock += 1000.0;
    g.pointerDown (bx, by, { true, false }, clock);
    g.pointerMove (bx + 40.0, by + 40.0, {});
    g.pointerMove (bx + 90.0, by + 70.0, {});
    g.pointerUp();
    auto want = before;
    for (const auto& id : expectedHits (geo, *rig.doc.current(), false, bx, by, bx + 90.0, by + 70.0))
        if (! contains (want, id))
            want.push_back (id);
    bool linked = false;
    for (const auto& n : rig.doc.current()->notes)
        linked = linked || ! n->glideTo.empty();
    check (band != nullptr && rig.doc.rollState().selected == want && ! linked && rig.steps == 0,
           fmt ("a Shift drag from the band above %s's line adds the notes its box meets ([%s]) and links nothing", band != nullptr ? band->id.c_str() : "?",
                joined (rig.doc.rollState().selected).c_str()));

    // BoxSelect's helpers: the box between corners, and the combining rules
    const auto sb = ui::roll::SelectBox::between (10.0, 40.0, -5.0, 2.0);
    const std::vector<std::string> ab { "a", "b" }, bc { "b", "c" };
    const auto plain = ui::roll::combineNotes (ab, bc, false, false);
    const auto shift = ui::roll::combineNotes (ab, bc, true, false);
    const auto ctrl = ui::roll::combineNotes (ab, bc, false, true);
    const auto both = ui::roll::combineNotes (ab, bc, true, true);
    check (juce::exactlyEqual (sb.x0, -5.0) && juce::exactlyEqual (sb.y0, 2.0) && juce::exactlyEqual (sb.x1, 10.0) && juce::exactlyEqual (sb.y1, 40.0)
               && plain == std::vector<std::string> { "b", "c" } && shift == std::vector<std::string> { "a", "b", "c" }
               && ctrl == std::vector<std::string> { "a", "c" } && both == std::vector<std::string> { "a", "b", "c" },
           "combineNotes: plain [b,c]; Shift [a,b,c]; Ctrl [a,c] (b toggled out); Shift and Ctrl together add, as the prototype tests Shift first");
    std::vector<doc::NodePick> base (1);
    base[0].id = "a";
    base[0].ks = { 1, 3 };
    std::vector<doc::NodePick> hit (2);
    hit[0].id = "b";
    hit[0].ks = { 0 };
    hit[1].id = "a";
    hit[1].ks = { 3, 4 };
    const auto nPlain = ui::roll::combineNodes (base, hit, false, false);
    const auto nShift = ui::roll::combineNodes (base, hit, true, false);
    const auto nCtrl = ui::roll::combineNodes (base, hit, false, true);
    check (picksText (nPlain) == "b:0;a:3,4" && picksText (nShift) == "a:1,3,4;b:0" && picksText (nCtrl) == "a:1,4;b:0",
           fmt ("combineNodes: plain \"%s\", Shift \"%s\", Ctrl \"%s\" (a note keeps its place, a new one goes last)", picksText (nPlain).c_str(),
                picksText (nShift).c_str(), picksText (nCtrl).c_str()));
}

//==================================================================================================
SL_TEST ("ui", boxSelectNodesSwitch)
{
    section ("Box select, Nodes switch: a box picks exactly the nodes of the selected notes inside it (Shift adds, Ctrl toggles); a drag on "
             "a picked node moves them all (one step \"Move nodes\")");

    const model::Clip clip = clip64 (6403);
    int boxes = 0, right = 0, picked = 0;
    std::string firstBad;
    for (const int mode : { 3, 5 })
    {
        RollRig rig (clip);
        auto& g = rig.gestures();
        g.setMode (mode);
        const auto lane = rig.doc.rollState().activeLane().value_or (core::LaneId::amp);
        // in Build the switch is refused; in a lane mode it switches on
        g.setMode (1);
        g.toggleNodes();
        const bool refused = ! g.settings().nodes && rig.lastToast() == std::optional<std::string> ("Nodes works in a lane mode (2 to 8)");
        g.setMode (mode);
        g.toggleNodes();
        check (refused && g.settings().nodes, fmt ("mode %d: the Nodes switch is refused in Build (a toast) and switches on in a lane mode", mode));

        Rng r { (std::uint32_t) (9100 + mode) };
        double clock = 0.0;
        for (int b = 0; b < 30; ++b)
        {
            // a seeded selection of up to 12 notes now and then, then a box from empty space aimed at a selected note
            if (b % 5 == 0)
            {
                std::vector<std::string> sel;
                const int k = 6 + r.below (7);
                for (int i = 0; i < k; ++i)
                    sel.push_back ("bx-" + std::to_string (r.below (64)));
                rig.doc.setSelection (sel, std::nullopt);
            }
            double x0 = 0.0, y0 = 0.0;
            if (! emptyPoint (rig, r, x0, y0))
                continue;
            const auto& geo = g.geometry();
            const auto st = rig.doc.rollState();
            const auto sel = RollGeometry::selectedNotes (*rig.doc.current(), st);
            double x1 = x0 + 100.0, y1 = y0 + 100.0;
            if (! sel.empty())
            {
                const auto& n = sel[(std::size_t) r.below ((int) sel.size())];
                x1 = (n->s + n->l * (0.2 + 0.8 * r())) * geo.beatWidth();
                y1 = geo.baseYAt (*n, 0.5) - geo.laneHeight() * r();
            }
            const int kind = b % 3;   // plain, Shift, Ctrl
            const Mods mods { kind == 1, kind == 2 };
            clock += 1000.0;
            g.pointerDown (x0, y0, mods, clock);
            const bool nodeBox = g.isNodeBox();
            g.pointerMove ((x0 + x1) * 0.5, (y0 + y1) * 0.5, mods);
            g.pointerMove (x1, y1, mods);
            g.pointerUp();
            const auto hits = expectedNodes (geo, *rig.doc.current(), st, lane, x0, y0, x1, y1);
            const auto want = ui::roll::combineNodes (st.picks, hits, kind == 1, kind == 2);
            const auto got = rig.doc.rollState().picks;
            ++boxes;
            const bool ok = nodeBox == ! st.selected.empty() && samePicks (got, want) && rig.doc.rollState().selected == st.selected;
            right += ok ? 1 : 0;
            picked += hits.empty() ? 0 : 1;
            if (! ok && firstBad.empty())
                firstBad = fmt ("mode %d box %d: got \"%s\", want \"%s\" (hits \"%s\")", mode, b, picksText (got).c_str(), picksText (want).c_str(),
                                picksText (hits).c_str());
        }
        check (rig.steps == 0, fmt ("mode %d: picking nodes pushes no undo step (%d)", mode, rig.steps));
    }
    note (fmt ("%d node boxes, %d caught nodes%s%s", boxes, picked, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (boxes >= 50 && right == boxes, fmt ("%d of %d boxes pick exactly the selected notes' nodes inside them (the selection stays)", right, boxes));
    check (picked >= boxes / 2, fmt ("the boxes caught nodes %d times of %d", picked, boxes));

    // a drag on one of several picked nodes moves every picked node by the same amount (one undo step)
    RollRig rig (clip);
    auto& g = rig.gestures();
    g.setMode (3);
    g.toggleNodes();
    const auto lane = core::LaneId::amp;
    rig.doc.setSelection ({ "bx-3", "bx-4" }, std::string ("bx-3"));
    std::vector<doc::NodePick> picks (2);
    picks[0].id = "bx-3";
    picks[0].ks = { 0, 1 };
    picks[1].id = "bx-4";
    picks[1].ks = { 1 };
    rig.doc.setNodePicks (picks);
    const auto before = rig.doc.current();
    const auto& geo = g.geometry();
    g.sync();
    const auto p = geo.nodeXY (*noteIn (*before, "bx-3"), lane, 1);
    const double dy = geo.laneHeight() * 0.25;
    g.pointerDown (p.x, p.y, {}, 50000.0);
    const bool groupDrag = g.isDragging() && ! g.isNodeBox();
    g.pointerMove (p.x, p.y - dy * 0.5, {});
    g.pointerMove (p.x, p.y - dy, {});
    const bool readout = g.readout().has_value();
    g.pointerUp();
    const auto after = rig.doc.current();
    int movedRight = 0, others = 0;
    for (std::size_t i = 0; i < after->notes.size(); ++i)
    {
        const auto& a = *after->notes[i];
        const auto& na = model::noteLane (a, lane);
        const auto& no = model::noteLane (*before->notes[i], lane);
        for (std::size_t k = 0; k < na.size() && k < no.size(); ++k)
        {
            const bool isPicked = (a.id == "bx-3" && k <= 1) || (a.id == "bx-4" && k == 1);
            const double want = isPicked ? std::clamp (no[k].v + 0.25, 0.0, 1.0) : no[k].v;
            if (std::abs (na[k].v - want) < 1e-9 && juce::exactlyEqual (na[k].t, no[k].t))
                movedRight += isPicked ? 1 : 0;
            else
                ++others;
        }
    }
    check (groupDrag && movedRight == 3 && others == 0 && rig.steps == 1 && rig.labels.back() == "Move nodes" && readout,
           fmt ("a drag of a quarter of the lane's height on one of 3 picked nodes moves all 3 up by 0.25 (%d right), no other node (%d), one undo "
                "step \"%s\", with the readout", movedRight, others, rig.labels.empty() ? "" : rig.labels.back().c_str()));

    // the picks go with Escape, another mode and Undo
    rig.doc.setNodePicks (picks);
    const bool kept = rig.doc.rollState().pickCount() == 3;
    g.clearSelection();
    const bool escape = rig.doc.rollState().picks.empty();
    rig.doc.setSelection ({ "bx-3", "bx-4" }, std::string ("bx-3"));
    rig.doc.setNodePicks (picks);
    g.setMode (5);
    const bool modeGoes = rig.doc.rollState().picks.empty();
    g.setMode (3);
    rig.doc.setNodePicks (picks);
    rig.undo();
    const bool undoGoes = rig.doc.rollState().picks.empty();
    check (kept && escape && modeGoes && undoGoes, "the picks go with Escape, with another mode and with Undo");

    // a node added or deleted on a picked note moves its picks along, so they stay on the same nodes
    const model::Note* m = roomyNote (*rig.doc.current(), lane);
    bool addMoved = false, deleteMoved = false;
    std::string addText, deleteText;
    if (m != nullptr)
    {
        const std::string id = m->id;
        rig.doc.setSelection ({ id }, id);
        std::vector<doc::NodePick> one (1);
        one[0].id = id;
        one[0].ks = { 0, 1 };
        rig.doc.setNodePicks (one);
        g.sync();
        // a Shift double-click on the note's line between nodes 0 and 1 (Shift keeps the picks through the first click)
        const auto& nd = model::noteLane (*m, lane);
        const int countBefore = (int) nd.size();
        const double t = (nd[0].t + nd[1].t) * 0.5;
        const double ax = (m->s + m->l * t) * geo.beatWidth(), ay = geo.baseYAt (*m, t);
        g.pointerDown (ax, ay, { true, false }, 90000.0);
        g.pointerUp();
        g.pointerDown (ax, ay, { true, false }, 90100.0);
        g.pointerUp();
        const auto afterAdd = rig.doc.rollState();
        const auto* moved = afterAdd.picksOf (id);
        const model::Note* m2 = noteIn (*rig.doc.current(), id);
        const int countAfter = m2 != nullptr ? (int) model::noteLane (*m2, lane).size() : -1;
        addMoved = countAfter == countBefore + 1 && moved != nullptr && moved->ks == std::vector<int> { 0, 2 };
        addText = fmt ("%d nodes, now %d, picks \"%s\"", countBefore, countAfter, moved != nullptr ? picksText ({ *moved }).c_str() : "");

        // a double-click on node 2 (not picked) deletes it: the pick on node 3 moves to 2
        if (m2 != nullptr && countAfter >= 4)
        {
            one[0].ks = { 0, 3 };
            rig.doc.setNodePicks (one);
            g.sync();
            const auto q = geo.nodeXY (*m2, lane, 2);
            g.pointerDown (q.x, q.y, {}, 95000.0);
            g.pointerUp();
            g.pointerDown (q.x, q.y, {}, 95100.0);
            g.pointerUp();
            const auto afterDelete = rig.doc.rollState();
            const auto* moved2 = afterDelete.picksOf (id);
            const model::Note* m3 = noteIn (*rig.doc.current(), id);
            const int countDeleted = m3 != nullptr ? (int) model::noteLane (*m3, lane).size() : -1;
            deleteMoved = countDeleted == countAfter - 1 && moved2 != nullptr && moved2->ks == std::vector<int> { 0, 2 }
                          && ! rig.labels.empty() && rig.labels.back() == "Delete node";
            deleteText = fmt ("%d nodes, picks \"%s\"", countDeleted, moved2 != nullptr ? picksText ({ *moved2 }).c_str() : "");
        }
    }
    check (m != nullptr && addMoved && deleteMoved,
           fmt ("a node added between the picked nodes 0 and 1 moves the second pick to 2 (%s); node 2 deleted moves a pick on 3 to 2 (%s)",
                addText.c_str(), deleteText.c_str()));
}

//==================================================================================================
SL_TEST ("ui", boxSelectInTheEditor)
{
    section ("Box select in the editor: a drag on the canvas selects the notes the box meets; the Nodes button and key, and the hints");

    StrandLockProcessor proc;
    {
        auto& s = proc.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }
    {
        StrandLockEditor editor (proc);
        auto& view = editor.getView();
        auto& roll = view.getRoll();
        roll.setMenusShown (false);
        view.setFolded (true);   // the tabs folded: only the selection bar's line moves the roll
        proc.editClip ("Start", clip64 (6404));
        editor.refreshNow();

        const auto& geo = roll.getGeometry();
        auto& canvas = roll.getCanvas();
        const auto event = [&canvas] (double vx, double vy, int mods, double t)
        {
            const auto time = juce::Time ((juce::int64) t);
            const juce::Point<float> at ((float) vx, (float) vy);
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (mods),
                                     juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                     juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                     juce::MouseInputSource::defaultTiltY, &canvas, &canvas, time, at, time, 1, false);
        };
        // the view's top row is 88, above every note (the highest is 84 with its lane band), so the box starts on empty
        // space; the notes from about 78 to 84 fill the rest of the view
        roll.setScroll (0.0, geo.cy (88.0) - geo.rowHeight() / 2.0);
        editor.refreshNow();
        const double vx0 = 2.0, vy0 = 2.0, vx1 = (double) canvas.getWidth() - 3.0, vy1 = (double) canvas.getHeight() - 3.0;
        const auto r0 = geo.toRoll (vx0, vy0), r1 = geo.toRoll (vx1, vy1);
        const bool startEmpty = geo.hitNote (*proc.getDocument().current(), proc.getDocument().rollState(), r0.x, r0.y) == nullptr;
        const int top0 = roll.getY();
        const double scroll0 = geo.scrollY();
        canvas.mouseDown (event (vx0, vy0, juce::ModifierKeys::leftButtonModifier, 100000.0));
        canvas.mouseDrag (event (vx1, vy1, juce::ModifierKeys::leftButtonModifier, 100050.0));
        const bool boxShown = roll.getPaintState().box.has_value();
        // the box selected notes: the selection bar's line came in above the roll, and the roll scrolled by as much,
        // so its notes stayed where they were under the mouse
        const int grew = roll.getY() - top0;
        const double scrolled = geo.scrollY() - scroll0;
        canvas.mouseUp (event (vx1, vy1, 0, 100100.0));
        const auto want = expectedHits (geo, *proc.getDocument().current(), false, r0.x, r0.y, r1.x, r1.y);
        const auto got = proc.getDocument().rollState().selected;
        const juce::String text = view.getSelectionText();
        check (startEmpty && boxShown && want.size() >= 2 && got == want && text == juce::String ((int) want.size()) + " notes selected",
               fmt ("a drag across the canvas draws the box and selects the %d notes it meets (\"%s\")", (int) want.size(), text.toRawUTF8()));
        check (grew == ui::roll::SelectionBar::lineHeight && juce::exactlyEqual (scrolled, (double) grew) && view.getSelectionBar().isVisible()
                   && ! view.getSelectionBar().isIdleShown(),
               fmt ("the selection bar's line came in with the selection (the roll moved down %d px) and the roll scrolled %.0f px, so the notes "
                    "stayed still under the box", grew, scrolled));

        // a click on empty space clears the selection: the line goes once nothing holds the roll (here at once)
        double cx = vx0, cy = vy0;
        for (int i = 0; i < 30; ++i)
        {
            const auto p = geo.toRoll (vx0 + 7.0 * i, vy0 + 1.0 * (i % 5));
            if (geo.hitNote (*proc.getDocument().current(), proc.getDocument().rollState(), p.x, p.y) == nullptr)
            {
                cx = vx0 + 7.0 * i;
                cy = vy0 + 1.0 * (i % 5);
                break;
            }
        }
        canvas.mouseDown (event (cx, cy, juce::ModifierKeys::leftButtonModifier, 102000.0));
        canvas.mouseUp (event (cx, cy, 0, 102040.0));
        check (proc.getDocument().rollState().selected.empty() && roll.getY() == top0 && view.getSelectionBar().getHeight() == 0,
               fmt ("a click on empty space clears the selection and the bar's line goes (the roll back at %d, the bar %d px)", roll.getY(),
                    view.getSelectionBar().getHeight()));
        // select them again for what follows
        proc.getDocument().setSelection (want, std::nullopt);
        roll.edited();

        // the Nodes button: greyed in Build, lit in a lane mode, and the hint says what to do; N with Keys on switches it
        const bool greyInBuild = ! view.getNodesButton().isEnabled();
        view.setMode (3);
        view.getNodesButton().onClick();
        const bool lit = view.getNodesButton().isEnabled() && view.getNodesButton().getToggleState() && roll.getGestures().settings().nodes;
        const bool hint = view.getHintText().startsWith ("VOLUME (3)") && view.getHintText().contains ("NODES: drag a box around nodes");
        (void) view.handleKey (juce::KeyPress ('N', juce::ModifierKeys(), 0), true);
        view.keyReleased();
        const bool keyOff = ! roll.getGestures().settings().nodes && ! view.getNodesButton().getToggleState();
        (void) view.handleKey (juce::KeyPress ('N', juce::ModifierKeys(), 0), false);
        const bool keysOffNothing = ! roll.getGestures().settings().nodes;
        check (greyInBuild && lit && hint && keyOff && keysOffNothing,
               fmt ("the Nodes button is greyed in Build, lights in Volume (hint \"%s...\"); N with Keys on switches it off, with Keys off does nothing",
                    view.getHintText().substring (0, 40).toRawUTF8()));
    }

    // the hints: Build's line has box select again (the prototype's words), and the Nodes line
    const auto build = ui::modeHint (1, std::nullopt, false, false);
    const auto nodes = ui::modeHint (8, core::LaneId::heads, false, false, true);
    check (build.contains ("drag on empty space = box select (Shift adds, Ctrl toggles)")
               && nodes == juce::String::fromUTF8 ("SAMPLE (8) \xc2\xb7 HEADS \xc2\xb7 NODES: drag a box around nodes of the selected notes, then drag one "
                                                   "of them to move them together (Shift adds, Ctrl toggles)."),
           "the hints: Build says drag on empty space = box select; the Nodes switch's line is the prototype's");
}

SL_TEST ("ui", boxSelectNodesInspector)
{
    section ("The Nodes switch and the inspector (sl-ui-panels.js activeNodeCtx with a group): two or more picked nodes show as one value box "
             "that types them all (one undo step), the time box goes; one picked node shows as in M4");

    StrandLockProcessor proc;
    {
        auto& s = proc.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }
    {
        StrandLockEditor editor (proc);
        auto& view = editor.getView();
        auto& doc = proc.getDocument();
        auto& insp = view.getInspector();
        view.getRoll().setMenusShown (false);
        view.setFolded (true);

        // two notes with three Volume nodes each
        model::Clip clip = model::newClip (4.0);
        for (int i = 0; i < 2; ++i)
        {
            auto n = model::mkNote (60.0 + 4.0 * i, 2.0 * i, 2.0, "nv-" + std::to_string (i));
            n.lane (core::LaneId::amp) = model::NodeList { model::mkNode (0.0, 0.8, model::NodeShape::smooth, 0.0),
                                                           model::mkNode (0.5, 0.6, model::NodeShape::smooth, 0.0),
                                                           model::mkNode (1.0, 0.8, model::NodeShape::smooth, 0.0) };
            clip.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
        }
        proc.editClip ("Start", clip);
        const auto start = doc.current();
        view.setMode (3);
        doc.setSelection ({ "nv-0", "nv-1" }, std::string ("nv-0"));
        doc.setNodePicks ({ doc::NodePick { "nv-0", { 1 }, -1 }, doc::NodePick { "nv-1", { 0, 2 }, -1 } });
        view.syncRollControls();
        const bool group = insp.showsNode() && insp.getNodeCaption() == "3 nodes (Volume)" && ! insp.getNodeTime().isVisible()
                           && insp.getNodeValue().isVisible();

        // one value for the three: each picked node gets it, the others keep theirs; one undo step
        insp.getNodeValue().type ("-6 dB");
        const auto want = ui::roll::laneValueFromText (core::LaneId::amp, "-6 dB", model::formatCtx (proc.readInst(), 120.0, nullptr));
        const auto now = doc.current();
        auto nodesOf = [] (const model::ClipPtr& c, const std::string& id) -> model::NodeList
        {
            for (const auto& n : c->notes)
                if (n->id == id)
                    return model::noteLane (*n, core::LaneId::amp);
            return {};
        };
        const auto a = nodesOf (now, "nv-0"), b = nodesOf (now, "nv-1");
        const bool typed = want.ok && a.size() == 3 && b.size() == 3 && juce::exactlyEqual (a[1].v, want.v) && juce::exactlyEqual (b[0].v, want.v)
                           && juce::exactlyEqual (b[2].v, want.v) && juce::exactlyEqual (a[0].v, 0.8) && juce::exactlyEqual (a[2].v, 0.8)
                           && juce::exactlyEqual (b[1].v, 0.6) && proc.undoLabel().value_or ("") == "Type value for 3 nodes";
        proc.undo();
        const bool oneStep = model::encodeClip (*doc.current()) == model::encodeClip (*start) && proc.undoLabel().value_or ("") == "Start";
        check (group && typed && oneStep,
               fmt ("three picked Volume nodes on two notes: the inspector shows \"%s\" with no time box (%d); \"-6 dB\" sets all three to %.4f and "
                    "no other (%d), one undo step \"Type value for 3 nodes\" (%d)",
                    insp.getNodeCaption().toRawUTF8(), (int) group, want.v, (int) typed, (int) oneStep));

        // a text it cannot read: the toast says why, nothing changes
        doc.setSelection ({ "nv-0", "nv-1" }, std::string ("nv-0"));
        doc.setNodePicks ({ doc::NodePick { "nv-0", { 1 }, -1 }, doc::NodePick { "nv-1", { 0, 2 }, -1 } });
        view.syncRollControls();
        const auto before = doc.current();
        insp.getNodeValue().type ("loud");
        const bool refused = doc.current() == before && view.isToastShown() && view.getToastText().isNotEmpty();
        // one picked node: the single node, with its time box
        doc.setNodePicks ({ doc::NodePick { "nv-1", { 2 }, -1 } });
        doc.setActiveNode (doc::ActiveNode { "nv-1", core::LaneId::amp, 2, 0 });
        view.syncRollControls();
        const bool single = insp.getNodeCaption() == "Node (Volume)" && insp.getNodeTime().isVisible() && insp.getNodeTime().getValue() == "end";
        check (refused && single, fmt ("\"loud\" toasts \"%s\" and changes nothing (%d); one picked node shows \"%s\" with its time box (%d)",
                                       view.getToastText().toRawUTF8(), (int) refused, insp.getNodeCaption().toRawUTF8(), (int) single));
    }
}

//==================================================================================================
// Pictures for review (the Release runner uploads them): a box being dragged in Build, and nodes picked in Volume.

namespace
{
    juce::Image shootBox (ThemeId theme, bool nodes)
    {
        StrandLockProcessor proc;
        proc.waitForSounds();
        auto& settings = proc.getUserSettings();
        settings.setTheme (theme);
        settings.setUiScalePercent (100);
        settings.setKeysDefault (false);
        juce::Image image;
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (1100, 680);
            auto& view = editor.getView();
            auto& roll = view.getRoll();
            roll.setMenusShown (false);
            view.setFolded (true);
            editor.refreshNow();
            auto& g = roll.getGestures();
            const auto& geo = roll.getGeometry();
            roll.setScroll (0.0, geo.cy (66.0) - geo.viewHeight() / 2.0);
            if (nodes)
            {
                // eight notes selected (at most 16 show their nodes), Volume, the Nodes switch on
                view.setMode (3);
                proc.getDocument().setSelection ({ "sd-1", "sd-2", "sd-3", "sd-4", "sd-5", "sd-6", "sd-7", "sd-8" }, std::string ("sd-1"));
                roll.edited();
                view.getNodesButton().onClick();
            }
            // a box from the first empty point near the top left of the view, down and right across the notes (not let
            // go in Build: it shows as it is dragged)
            const auto c = proc.getDocument().current();
            const auto st = proc.getDocument().rollState();
            double x0 = geo.scrollX() + 30.0, y0 = geo.scrollY() + 10.0;
            for (int step = 0; step < 40; ++step)
            {
                const double x = geo.scrollX() + 30.0 + 13.0 * step, y = geo.scrollY() + 10.0 + 3.0 * step;
                const bool onNode = st.activeLane().has_value() && geo.hitNode (*c, st, x, y, *st.activeLane()).has_value();
                if (geo.hitNote (*c, st, x, y) == nullptr && ! onNode)
                {
                    x0 = x;
                    y0 = y;
                    break;
                }
            }
            g.pointerDown (x0, y0, {}, 10000.0);
            g.pointerMove (x0 + (nodes ? 2.6 : 4.2) * geo.beatWidth(), y0 + (nodes ? 0.9 : 0.6) * geo.viewHeight(), {});
            if (nodes)
                g.pointerUp();
            roll.edited();
            editor.refreshNow();
            image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
            if (g.isDragging())
                g.pointerUp();
        }
        settings.setTheme (state::Theme::dark);
        return image;
    }
}

SL_SNAPSHOT (m5_box_select_dark)   { return shootBox (ThemeId::dark, false); }
SL_SNAPSHOT (m5_box_select_light)  { return shootBox (ThemeId::light, false); }
SL_SNAPSHOT (m5_nodes_picked_dark) { return shootBox (ThemeId::dark, true); }
