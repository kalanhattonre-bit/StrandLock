// The lane-mode lock through the roll's gestures (PLUGIN-PLAN 4 M4 "lane-mode lock", PROMPT 14.5): in a lane mode the
// notes are locked and only that lane changes. 500 seeded gestures in each lane mode (2 to 7, and Sample mode 8 across
// its four sub-lanes): presses on nodes, segment diamonds, slide handles, note lines, bands and empty space, single and
// double, with Shift, Ctrl, Multi and Freehand, 0 to 3 moves, and now and then a shape from the right-click menu. After
// every one: 0 changes to any note's start, length, pitch, velocity, sample settings or any other lane (nor to its
// slide outside Pitch mode, where the slide handles live), no note added or removed, and exactly one undo step when the
// clip changed, none when it did not.

#include "RollRig.h"

#include "core/Lanes.h"
#include "core/Seeds.h"
#include "model/History.h"
#include "model/ModelFixture.h"
#include "model/Presets.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/roll/RollGeometry.h"
#include "ui/roll/RollGestures.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::roll::MenuItem;
    using ui::roll::Mods;

    struct Rng
    {
        std::uint32_t seed;
        std::int32_t i = 0;
        double operator()() { return core::rand01 (core::hash32 ((std::int32_t) seed, i++)); }
        int below (int n) { return (int) ((*this)() * n) % n; }
    };

    bool sameSample (const model::NoteSample& a, const model::NoteSample& b)
    {
        return a.motion == b.motion && a.travel == b.travel && a.start.mode == b.start.mode && fixture::same (a.start.pos, b.start.pos)
            && fixture::same (a.start.range, b.start.range) && a.start.seed == b.start.seed && a.tape == b.tape && a.rev == b.rev
            && a.jump == b.jump && a.engine == b.engine && a.lock == b.lock && a.restart == b.restart;
    }

    /** What changed besides the active lane (empty = nothing); laneChanged says whether the active lane did. */
    std::string lockBroken (const model::Clip& before, const model::Clip& after, core::LaneId lane, bool pitchMode, bool& laneChanged)
    {
        laneChanged = false;
        if (before.notes.size() != after.notes.size())
            return fmt ("the clip went from %zu to %zu notes", before.notes.size(), after.notes.size());
        if (before.bars != after.bars)
            return "the bars changed";
        for (std::size_t i = 0; i < before.notes.size(); ++i)
        {
            const auto& a = *before.notes[i];
            const auto& b = *after.notes[i];
            if (a.id != b.id)
                return "note " + std::to_string (i) + " is now " + b.id + " (was " + a.id + ")";
            if (! fixture::same (a.s, b.s) || ! fixture::same (a.l, b.l) || ! fixture::same (a.p, b.p))
                return "note " + a.id + " moved: start, length or pitch";
            if (! fixture::same (a.vel, b.vel) || ! sameSample (a.sample, b.sample))
                return "note " + a.id + ": velocity or sample settings";
            if (! pitchMode && (a.glide.has_value() != b.glide.has_value() || (a.glide && ! fixture::same (*a.glide, *b.glide))
                                || ! fixture::same (a.gc, b.gc) || a.glideTo != b.glideTo))
                return "note " + a.id + ": its slide changed outside Pitch mode";
            for (int li = 0; li < core::kNumLanes; ++li)
            {
                const auto l = static_cast<core::LaneId> (li);
                const bool same = model::sameNodes (model::noteLane (a, l), model::noteLane (b, l))
                               && (a.genOf (l).has_value() == b.genOf (l).has_value() && (! a.genOf (l) || model::sameGen (*a.genOf (l), *b.genOf (l))));
                if (l == lane)
                    laneChanged = laneChanged || ! same;
                else if (! same)
                    return "note " + a.id + ": lane " + core::laneInfo (l).code + " changed";
            }
        }
        return {};
    }

    model::Clip strandDemo() { return *model::loadClipPreset ("strand-demo"); }
}

SL_TEST ("ui", laneModesLockTheNotes)
{
    section ("Lane-mode lock: 500 seeded gestures in each lane mode change only that lane; one undo step per change");

    for (int mode = 2; mode <= 8; ++mode)
    {
        Rng rnd { (std::uint32_t) (7700 + mode) };
        RollRig rig (strandDemo());
        auto& g = rig.gestures();
        g.setMode (mode);
        g.sync();
        const auto& geo = g.geometry();
        int broken = 0, laneChanges = 0, unchanged = 0, badSteps = 0, menuPicks = 0;
        std::string firstBroken, firstBadStep;
        double clock = 1e6;

        // one gesture: checks the lock and the undo step against the clip before it
        auto finish = [&] (const model::ClipPtr& before, int stepsBefore, const std::string& what)
        {
            const auto after = rig.doc.current();
            const auto lane = *rig.doc.rollState().activeLane();
            bool laneChanged = false;
            const std::string why = lockBroken (*before, *after, lane, mode == 2, laneChanged);
            if (! why.empty())
            {
                if (broken++ == 0)
                    firstBroken = what + ": " + why;
            }
            const bool changed = ! model::sameClip (*after, *before);
            laneChanges += laneChanged ? 1 : 0;
            unchanged += changed ? 0 : 1;
            if (rig.steps - stepsBefore != (changed ? 1 : 0))
            {
                if (badSteps++ == 0)
                    firstBadStep = fmt ("%s: %d undo steps, the clip %s", what.c_str(), rig.steps - stepsBefore, changed ? "changed" : "did not change");
            }
        };

        for (int i = 0; i < 500; ++i)
        {
            if (mode == 8 && i % 125 == 0)
                g.setSubLane (core::kSampleLanes[i / 125]);
            const auto lane = *rig.doc.rollState().activeLane();
            const auto clip = rig.doc.current();

            // now and then a new selection (1 to 4 notes) and the switches
            if (rnd() < 0.3 || rig.doc.rollState().selected.empty())
            {
                std::vector<std::string> ids;
                const int count = 1 + rnd.below (4);
                for (int k = 0; k < count; ++k)
                    ids.push_back (clip->notes[(std::size_t) rnd.below ((int) clip->notes.size())]->id);
                rig.doc.setSelection (ids, ids[(std::size_t) rnd.below ((int) ids.size())]);
            }
            if (rnd() < 0.08)
                g.toggleFreehand();
            g.settings().multi = rnd() < 0.08;
            g.sync();

            const auto st = rig.doc.rollState();
            const model::Note* P = modelfx::findNote (*clip, st.primary);
            double x = rnd() * 16.0 * geo.beatWidth(), y = 200.0 + rnd() * 700.0;
            const double kind = rnd();
            if (P != nullptr && kind < 0.35)
            {
                const auto& nd = model::noteLane (*P, lane);
                const auto p = geo.nodeXY (*P, lane, rnd.below ((int) nd.size()));
                x = p.x + (rnd() - 0.5) * 8.0;
                y = p.y + (rnd() - 0.5) * 8.0;
            }
            else if (P != nullptr && kind < 0.5)
            {
                const auto& nd = model::noteLane (*P, lane);
                if (const auto s = geo.segXY (*P, lane, rnd.below ((int) nd.size() - 1)))
                {
                    x = s->x;
                    y = s->y;
                }
            }
            else if (P != nullptr && kind < 0.58)
            {
                if (const auto gh = geo.glideHandles (*P))
                {
                    const auto& h = rnd() < 0.5 ? gh->start : gh->mid;
                    x = h.x;
                    y = h.y;
                }
            }
            else if (kind < 0.85)
            {
                const auto& n = *clip->notes[(std::size_t) rnd.below ((int) clip->notes.size())];
                const double t = rnd();
                x = (n.s + n.l * t) * geo.beatWidth();
                y = geo.baseYAt (n, t) - rnd() * 40.0 + 6.0;
            }
            Mods mods;
            const double m = rnd();
            mods.shift = m < 0.12;
            mods.ctrl = m >= 0.12 && m < 0.2;

            if (rnd() < 0.08)
            {
                // the right-click menu: a shape (or Each note / Across) on the selected notes
                const auto before = rig.doc.current();
                const int s0 = rig.steps;
                auto items = g.contextMenu (x, y, clock);
                std::vector<MenuItem> shapes;
                bool afterHeader = false;
                for (const auto& it : items)
                {
                    if (it.kind == MenuItem::Kind::separator)
                        afterHeader = false;
                    if (it.kind == MenuItem::Kind::header)
                        afterHeader = true;
                    if (afterHeader && it.kind == MenuItem::Kind::item)
                        shapes.push_back (it);
                    if (it.kind == MenuItem::Kind::row)
                        for (const auto& r : it.row)
                            shapes.push_back (r);
                }
                if (! shapes.empty())
                {
                    g.runMenuItem (shapes[(std::size_t) rnd.below ((int) shapes.size())]);
                    ++menuPicks;
                }
                finish (before, s0, fmt ("mode %d gesture %d (menu at %g, %g)", mode, i, x, y));
                clock += 700.0;
                continue;
            }

            if (rnd() < 0.2)   // the first click of a double-click
            {
                const auto before = rig.doc.current();
                const int s0 = rig.steps;
                g.pointerDown (x, y, mods, clock);
                g.pointerUp();
                finish (before, s0, fmt ("mode %d gesture %d (first click at %g, %g)", mode, i, x, y));
                clock += 90.0;
            }
            const auto before = rig.doc.current();
            const int s0 = rig.steps;
            g.pointerDown (x, y, mods, clock);
            const int moves = rnd.below (4);
            double mx = x, my = y;
            for (int k = 0; k < moves; ++k)
            {
                mx += (rnd() - 0.5) * 120.0;
                my += (rnd() - 0.5) * 60.0;
                g.pointerMove (mx, my, { rnd() < 0.2, false });
            }
            g.pointerUp();
            finish (before, s0, fmt ("mode %d gesture %d (at %g, %g, %d moves)", mode, i, x, y, moves));
            clock += 700.0;
        }

        note (fmt ("mode %d: %d gestures changed the lane, %d changed nothing, %d menu picks, %d undo steps%s%s%s%s", mode, laneChanges, unchanged,
                   menuPicks, rig.steps, firstBroken.empty() ? "" : "; first lock break: ", firstBroken.c_str(),
                   firstBadStep.empty() ? "" : "; first step miscount: ", firstBadStep.c_str()));
        check (broken == 0, fmt ("mode %d (%s): 0 changes to a note's start, length, pitch, settings or any other lane in 500 gestures (%d)", mode,
                                 core::kModes[mode - 1].name, broken));
        check (badSteps == 0, fmt ("mode %d: every gesture that changed the lane is 1 undo step, every other 0 (%d miscounted)", mode, badSteps));
        check (laneChanges >= 60 && unchanged >= 60, fmt ("mode %d: the gestures did change the lane (%d times) and also left it alone (%d)", mode,
                                                         laneChanges, unchanged));
    }
}
