// The roll's gestures at model level (PLUGIN-PLAN 4 M4 "gestures", 7.6 decisions 1 and 2).
//
// rollGesturesMatchPrototype replays every case of Tests/fixtures/roll/gestures.json, which tools/fixtures/roll.js
// recorded from sl-ui.js itself (its rollDown, rollMove, rollUp and context menu driven with made-up events): after
// every press, move, release and menu pick, the C++ clip, note order, undo steps and their labels, selection,
// primary note, inspector node, readout, toast and Slide-into arming must be the prototype's (values within 1e-9).
// The other tests check the table's rows directly: a double-click on empty space makes one note of max (1 beat,
// grid) at the snapped position, a drag moves with grid and scale snap, an end dot bends only after 0.4 row, and every
// finished gesture is exactly one undo step (none for a click that changes nothing).

#include <juce_audio_processors/juce_audio_processors.h>

#include "RollRig.h"

#include "PluginProcessor.h"

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/JsonLite.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "core/Seeds.h"
#include "model/History.h"
#include "model/ModelFixture.h"
#include "model/Normalize.h"
#include "model/Presets.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "ui/roll/RollGeometry.h"
#include "ui/roll/RollGestures.h"

#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::roll::MenuItem;
    using ui::roll::Mods;

    constexpr double kTol = 1e-9;
    constexpr double kPageClock = 1e6;   // the fixture page's Date.now () at its start

    const std::string kLinkEmoji = "\xF0\x9F\x94\x97 ";   // U+1F517 and a space: the prototype's menu label starts with it

    std::string withoutEmoji (std::string s)
    {
        if (s.rfind (kLinkEmoji, 0) == 0)
            s.erase (0, kLinkEmoji.size());
        return s;
    }

    /** A deep merge of JSON objects (mergeInst's rule for the nested settings). */
    core::JsonValue merged (const core::JsonValue& base, const core::JsonValue& patch)
    {
        if (! base.isObject() || ! patch.isObject())
            return patch;
        core::JsonValue out = base;
        for (std::size_t i = 0; i < patch.size(); ++i)
        {
            const auto& key = patch.keyAt (i);
            const auto* now = out.find (key);
            out.set (key, now != nullptr ? merged (*now, patch.valueAt (i)) : patch.valueAt (i));
        }
        return out;
    }

    std::string shown (const MenuItem& m)
    {
        switch (m.kind)
        {
            case MenuItem::Kind::separator: return "-";
            case MenuItem::Kind::header:    return "#" + m.label;
            case MenuItem::Kind::row:
            {
                std::string s = "[";
                for (std::size_t i = 0; i < m.row.size(); ++i)
                    s += (i ? "|" : "") + std::string (m.row[i].on ? "*" : "") + m.row[i].label;
                return s + "]";
            }
            case MenuItem::Kind::item:      return (m.disabled ? "~" : "") + m.label;
        }
        return "?";
    }

    /** Applies a fixture note patch ({ field: value }, c and gen per lane, null = flat / none) to an expected note. */
    bool applyPatch (model::Note& n, const core::JsonValue& set, std::string& why)
    {
        for (std::size_t i = 0; i < set.size(); ++i)
        {
            const std::string& k = set.keyAt (i);
            const auto& v = set.valueAt (i);
            if (k == "p") n.p = fixture::num (v);
            else if (k == "s") n.s = fixture::num (v);
            else if (k == "l") n.l = fixture::num (v);
            else if (k == "vel") n.vel = fixture::num (v);
            else if (k == "gc") n.gc = fixture::num (v);
            else if (k == "glide")
            {
                if (v.isNull())
                    n.glide.reset();
                else
                    n.glide = fixture::num (v);
            }
            else if (k == "glideTo") n.glideTo = v.isString() ? v.asString() : std::string();
            else if (k == "sample") n.sample = model::sampleFromJson (v);
            else if (k == "c" || k == "gen")
            {
                for (std::size_t j = 0; j < v.size(); ++j)
                {
                    const auto lane = core::findLane (v.keyAt (j));
                    if (! lane)
                    {
                        why = "unknown lane " + v.keyAt (j);
                        return false;
                    }
                    const auto& lv = v.valueAt (j);
                    if (k == "c")
                    {
                        if (lv.isNull())
                            n.lane (*lane) = model::flat (core::laneDef (*lane));
                        else if (! modelfx::nodesFrom (lv, n.lane (*lane), why))
                            return false;
                    }
                    else
                    {
                        n.genOf (*lane) = lv.isNull() ? std::nullopt : model::genFromJson (lv);
                    }
                }
            }
            else
            {
                why = "unknown note field " + k;
                return false;
            }
        }
        return true;
    }

    struct Expected
    {
        std::vector<std::string> order;
        std::map<std::string, model::Note> notes;
        int hist = 0;
        std::optional<std::string> label;
        std::vector<std::string> sel;
        std::optional<std::string> primary;
        core::JsonValue active;
        std::optional<std::string> readout, toast;
        bool linkArm = false;
    };

    std::optional<std::string> optString (const core::JsonValue& v)
    {
        if (v.isString())
            return v.asString();
        return std::nullopt;
    }

    std::string text (const std::optional<std::string>& s) { return s ? "\"" + *s + "\"" : std::string ("null"); }

    std::string list (const std::vector<std::string>& v)
    {
        std::string s = "[";
        for (std::size_t i = 0; i < v.size(); ++i)
            s += (i ? "," : "") + v[i];
        return s + "]";
    }

    /** Compares the rig with the expected state; the first difference goes to why. */
    bool sameState (RollRig& rig, const Expected& e, bool compareNotes, bool& exact, std::string& why)
    {
        const auto clip = rig.doc.current();
        const auto st = rig.doc.rollState();
        if (compareNotes)
        {
            std::vector<std::string> order;
            for (const auto& n : clip->notes)
                order.push_back (n->id);
            if (order != e.order)
            {
                why = "note order " + list (order) + ", want " + list (e.order);
                return false;
            }
            for (const auto& n : clip->notes)
            {
                const auto it = e.notes.find (n->id);
                if (it == e.notes.end())
                {
                    why = "unexpected note " + n->id;
                    return false;
                }
                std::string w;
                if (! modelfx::notesClose (*n, it->second, kTol, kTol, exact, w))
                {
                    why = "note " + n->id + ": " + w;
                    return false;
                }
            }
        }
        if (rig.steps != e.hist)
        {
            why = "undo steps " + std::to_string (rig.steps) + ", want " + std::to_string (e.hist);
            return false;
        }
        const auto label = rig.steps > 0 ? rig.history.undoLabel() : std::nullopt;
        if (label != e.label)
        {
            why = "undo label " + text (label) + ", want " + text (e.label);
            return false;
        }
        std::vector<std::string> sel;
        for (const auto& n : ui::roll::RollGeometry::selectedNotes (*clip, st))
            sel.push_back (n->id);
        if (sel != e.sel)
        {
            why = "selection " + list (sel) + ", want " + list (e.sel);
            return false;
        }
        const std::optional<std::string> primary = st.primary.empty() ? std::nullopt : std::optional<std::string> (st.primary);
        if (primary != e.primary)
        {
            why = "primary " + text (primary) + ", want " + text (e.primary);
            return false;
        }
        const bool wantActive = e.active.isArray();
        if (wantActive != st.activeNode.has_value()
            || (wantActive && (st.activeNode->id != e.active[0].asString() || core::laneInfo (st.activeNode->lane).code != e.active[1].asString()
                               || st.activeNode->k != (int) fixture::num (e.active[2]))))
        {
            why = "inspector node " + (st.activeNode ? st.activeNode->id + " " + core::laneInfo (st.activeNode->lane).code + " " + std::to_string (st.activeNode->k)
                                                     : std::string ("none"))
                + ", want " + core::toJson (e.active);
            return false;
        }
        const auto& r = rig.g->readout();
        const std::optional<std::string> readout = r ? std::optional<std::string> (r->text) : std::nullopt;
        if (readout != e.readout)
        {
            why = "readout " + text (readout) + ", want " + text (e.readout);
            return false;
        }
        if (rig.g->isLinkArmed() != e.linkArm)
        {
            why = std::string ("Slide into armed ") + (rig.g->isLinkArmed() ? "on" : "off");
            return false;
        }
        return true;
    }

    /** Runs one fixture case; false with why at the first step that differs. */
    bool runCase (const core::JsonValue& c, const std::map<std::string, model::Clip>& clips, const core::JsonValue& initInst, int& stepsRun,
                  bool& exact, std::string& why)
    {
        const auto& in = c["input"];
        const auto& setup = in["setup"];
        const std::string clipName = in["clip"].asString();
        model::Clip clip;
        if (clipName.rfind ("preset:", 0) == 0)
        {
            const auto p = model::loadClipPreset (clipName.substr (7));
            if (! p)
            {
                why = "no preset " + clipName;
                return false;
            }
            clip = *p;
        }
        else if (const auto it = clips.find (clipName); it != clips.end())
        {
            clip = it->second;
        }
        else
        {
            why = "no clip " + clipName;
            return false;
        }

        RollRig rig (clip);
        auto& g = rig.gestures();
        auto& s = g.settings();
        s.inst = core::normalizeInst (core::instFromJson (setup.contains ("inst") ? merged (initInst, setup["inst"]) : initInst));
        s.bpm = fixture::num (setup["bpm"]);
        const int mode = (int) fixture::num (setup["mode"]);
        g.setMode (mode);
        if (mode == 8)   // the page's setSubLane, which puts it in mode 8 (the case names the sub-lane only then)
            if (const auto lane = core::findLane (setup["subLane"].asString()))
                g.setSubLane (*lane);
        s.snap = fixture::num (setup["snap"]);
        s.free = setup["free"].asBool();
        s.multi = setup["multi"].asBool();
        const auto sel = modelfx::stringsFrom (setup["sel"]);
        if (! sel.empty())
            rig.doc.setSelection (sel, optString (setup["primary"]));
        g.sync();

        // the start
        Expected e;
        const auto& start = c["start"];
        e.order = modelfx::stringsFrom (start["order"]);
        for (const auto& n : rig.doc.current()->notes)
            e.notes.emplace (n->id, model::cloneNote (*n));
        e.sel = modelfx::stringsFrom (start["sel"]);
        e.primary = optString (start["primary"]);
        e.active = start["active"];
        e.linkArm = start["linkArm"].asBool();
        if (! sameState (rig, e, true, exact, why))
        {
            why = "start: " + why;
            return false;
        }

        const auto& steps = c["steps"];
        for (std::size_t i = 0; i < steps.size(); ++i)
        {
            const auto& step = steps[i];
            const std::string what = step["do"].asString();
            const double x = fixture::num (step["x"]), y = fixture::num (step["y"]);
            Mods mods;
            mods.shift = step["mods"]["shiftKey"].asBool();
            mods.ctrl = step["mods"]["ctrlKey"].asBool() || step["mods"]["metaKey"].asBool();
            const double now = kPageClock + fixture::num (step["t"]);
            rig.toasts.clear();
            const auto& after = step["after"];
            std::string where = "step " + std::to_string (i) + " (" + what + (what == "up" ? "" : " " + core::jsNumberToString (x) + ", " + core::jsNumberToString (y)) + ")";

            if (what == "down")
                g.pointerDown (x, y, mods, now);
            else if (what == "move")
                g.pointerMove (x, y, mods);
            else if (what == "up")
                g.pointerUp();
            else if (what == "menu")
            {
                const auto items = g.contextMenu (x, y, now);
                const auto want = modelfx::stringsFrom (after["menu"]);
                std::vector<std::string> got;
                for (const auto& m : items)
                    got.push_back (shown (m));
                std::vector<std::string> wantPlain;
                for (const auto& w : want)
                    wantPlain.push_back (withoutEmoji (w));
                if (got != wantPlain)
                {
                    why = where + ": the menu " + list (got) + ", want " + list (wantPlain);
                    return false;
                }
                if (step["pick"].isString())
                {
                    const std::string pick = withoutEmoji (step["pick"].asString());
                    const MenuItem* hit = nullptr;
                    for (const auto& m : items)
                        if (m.kind == MenuItem::Kind::item && m.label == pick)
                            hit = &m;
                    if (hit == nullptr)
                    {
                        why = where + ": no menu item " + pick;
                        return false;
                    }
                    const MenuItem chosen = *hit;
                    g.runMenuItem (chosen);
                }
                where += " pick " + text (optString (step["pick"]));
            }
            else
            {
                why = where + ": unknown step";
                return false;
            }

            // what the step changed
            const bool notesRecorded = ! (after.contains ("notes") && ! after["notes"].asBool());
            if (const auto* changed = after.find ("changed"))
            {
                for (std::size_t k = 0; k < changed->size(); ++k)
                {
                    const auto& ch = (*changed)[k];
                    const std::string id = ch["id"].asString();
                    if (ch.contains ("note"))
                    {
                        model::Note n;
                        if (! modelfx::noteFrom (ch["note"], n, why))
                            return false;
                        e.notes[id] = n;
                    }
                    else if (! applyPatch (e.notes[id], ch["set"], why))
                    {
                        return false;
                    }
                }
            }
            if (const auto* removed = after.find ("removed"))
                for (std::size_t k = 0; k < removed->size(); ++k)
                    e.notes.erase ((*removed)[k].asString());
            if (after.contains ("order")) e.order = modelfx::stringsFrom (after["order"]);
            if (after.contains ("hist")) e.hist = (int) fixture::num (after["hist"]);
            if (after.contains ("label")) e.label = optString (after["label"]);
            if (after.contains ("sel")) e.sel = modelfx::stringsFrom (after["sel"]);
            if (after.contains ("primary")) e.primary = optString (after["primary"]);
            if (after.contains ("active")) e.active = after["active"];
            if (after.contains ("readout")) e.readout = optString (after["readout"]);
            if (after.contains ("toast")) e.toast = optString (after["toast"]);
            if (after.contains ("linkArm")) e.linkArm = after["linkArm"].asBool();

            if (! sameState (rig, e, notesRecorded, exact, why))
            {
                why = where + ": " + why;
                return false;
            }
            const auto toast = rig.lastToast();
            if (toast != e.toast)
            {
                why = where + ": toast " + text (toast) + ", want " + text (e.toast);
                return false;
            }
            ++stepsRun;
        }
        return true;
    }

    //==============================================================================================
    model::Clip strandDemo()
    {
        return *model::loadClipPreset ("strand-demo");
    }

    /** Eight notes 1.5 beats long, half a beat apart (no glides) and six rows apart: a press in the middle of a note's
        line hits that note and nothing else, and its end dots are 48 px from the middle at 64 px a beat. */
    model::Clip spreadClip()
    {
        model::Clip c = model::newClip (4.0);
        for (int i = 0; i < 8; ++i)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (40.0 + 6.0 * i, 2.0 * i, 1.5, "d" + std::to_string (i))));
        return c;
    }

    /** A seeded value 0..1 (the fixtures' rng: rand01 (hash32 (seed, i))). */
    struct Rng
    {
        std::uint32_t seed;
        std::int32_t i = 0;
        double operator()() { return core::rand01 (core::hash32 ((std::int32_t) seed, i++)); }
    };
}

SL_TEST ("ui", rollGesturesMatchPrototype)
{
    section ("Roll gestures: every press, move, release and menu pick gives what sl-ui.js gives (roll/gestures.json)");

    core::JsonValue doc;
    if (! fixture::load ("roll/gestures.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    const auto& initInst = doc["insts"]["init"];
    const auto& cases = doc["cases"];
    modelfx::FileTally tally ("roll/gestures.json", cases.size());
    int stepsRun = 0;
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        std::string why;
        bool exact = true;
        if (runCase (cases[i], clips, initInst, stepsRun, exact, why))
            tally.pass (exact);
        else
            tally.fail (cases[i]["name"].asString() + ": " + why);
    }
    tally.finish();
    note (fmt ("%d steps replayed", stepsRun));
    check (stepsRun > 500, fmt ("the replay ran the steps of every case (%d, more than 500)", stepsRun));
}

SL_TEST ("ui", rollDoubleClickMakesOneNote)
{
    section ("Roll gestures: a double-click on empty space makes one note of max (1 beat, grid) at the snapped position, one undo step");

    Rng rnd { 4401 };
    int made = 0, right = 0, steps = 0;
    std::string firstBad;
    const double grids[] = { 0.0, 0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
    for (int trial = 0; trial < 160; ++trial)
    {
        RollRig rig (model::newClip (4.0));
        auto& g = rig.gestures();
        const double grid = grids[trial % 8];
        g.settings().snap = grid;
        g.settings().inst.scaleLock = trial % 3 == 0;
        g.settings().inst.key = trial % 12;
        g.settings().inst.scale = (std::uint8_t) (1 + trial % 7);
        g.sync();
        const auto& geo = g.geometry();
        const double x = rnd() * 15.9 * geo.beatWidth(), y = (2.0 + rnd() * 68.0) * geo.rowHeight();
        g.pointerDown (x, y, {}, 1000.0);
        g.pointerUp();
        g.pointerDown (x, y, {}, 1100.0);
        g.pointerUp();
        const auto clip = rig.doc.current();
        if (clip->notes.size() != 1)
        {
            if (firstBad.empty())
                firstBad = fmt ("trial %d: %zu notes", trial, clip->notes.size());
            continue;
        }
        ++made;
        steps += rig.steps;
        const auto& n = *clip->notes.front();
        const double b = x / geo.beatWidth();
        const double wantS = grid > 0.0 ? std::floor (b / grid) * grid : b;
        const double wantL = std::min (std::max (1.0, grid > 0.0 ? grid : 0.125), 16.0 - wantS);
        const double row = (double) geo.pitchAt (y);
        const double wantP = g.settings().inst.scaleLock ? core::snapScale (row, g.settings().inst.key, g.settings().inst.scale) : row;
        if (fixture::same (n.s, wantS) && fixture::same (n.l, wantL) && fixture::same (n.p, wantP) && rig.steps == 1 && rig.labels.back() == "Add note"
            && rig.doc.rollState().primary == n.id)
            ++right;
        else if (firstBad.empty())
            firstBad = fmt ("trial %d (grid %g): s %g l %g p %g, want %g %g %g; %d steps", trial, grid, n.s, n.l, n.p, wantS, wantL, wantP, rig.steps);
    }
    note (fmt ("%d of 160 double-clicks made one note, %d at the right place and length, %d undo steps%s%s", made, right, steps,
               firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (made == 160 && right == 160, "every double-click made exactly one note at the snapped start, max (1 beat, grid) long, on its row "
                                        "(scale-snapped when the scale lock is on), selected, with one undo step \"Add note\"");

    // two slow clicks, two far apart, and a lane mode make none
    RollRig rig (model::newClip (4.0));
    auto& g = rig.gestures();
    g.pointerDown (100, 300, {}, 1000.0); g.pointerUp();
    g.pointerDown (100, 300, {}, 1400.0); g.pointerUp();
    g.pointerDown (130, 300, {}, 1500.0); g.pointerUp();
    const bool noneSlowOrFar = rig.doc.current()->notes.empty() && rig.steps == 0;
    g.setMode (3);
    g.pointerDown (100, 300, {}, 3000.0); g.pointerUp();
    g.pointerDown (100, 300, {}, 3100.0); g.pointerUp();
    check (noneSlowOrFar && rig.doc.current()->notes.empty() && rig.steps == 0 && rig.lastToast() == std::string ("Notes are locked in this mode: press 1 Build to add notes"),
           "two clicks 400 ms apart or 30 px apart make no note, and neither does a double-click in a lane mode (a toast says why); 0 undo steps");
}

SL_TEST ("ui", rollDragMovesWithGridAndScale)
{
    section ("Roll gestures: a drag moves the note with grid snap of the move and scale snap of the pitch, one undo step");

    const model::Clip spread = spreadClip();

    Rng rnd { 4402 };
    int right = 0, total = 0, movedOnes = 0;
    std::string firstBad;
    for (int trial = 0; trial < 160; ++trial)
    {
        const double grid = trial % 4 == 0 ? 0.0 : trial % 4 == 1 ? 0.25 : trial % 4 == 2 ? 0.5 : 1.0;
        RollRig rig (spread);
        auto& g = rig.gestures();
        g.settings().snap = grid;
        g.settings().inst.glideMode = core::GlideMode::off;
        g.settings().inst.scaleLock = trial % 2 == 0;
        g.settings().inst.key = (trial * 5) % 12;
        g.settings().inst.scale = (std::uint8_t) (1 + trial % 7);
        const auto clip0 = rig.doc.current();
        const auto& n = *clip0->notes[(std::size_t) (trial % 8)];
        rig.doc.setSelection ({ n.id }, n.id);
        g.sync();
        const auto& geo = g.geometry();
        const double x = (n.s + n.l * 0.5) * geo.beatWidth(), y = geo.baseYAt (n, 0.5);
        const double dx = (rnd() - 0.5) * 400.0, dy = (rnd() - 0.5) * 120.0;
        g.pointerDown (x, y, {}, 10000.0 + trial * 1000.0);
        g.pointerMove (x + dx * 0.5, y + dy * 0.5, {});
        g.pointerMove (x + dx, y + dy, {});
        g.pointerUp();
        ++total;
        const model::Note* m = modelfx::findNote (*rig.doc.current(), n.id);
        const double b0 = x / geo.beatWidth(), b = (x + dx) / geo.beatWidth();
        const double db = grid > 0.0 ? core::jsRound ((b - b0) / grid) * grid : b - b0;
        const int dp = geo.pitchAt (y + dy) - geo.pitchAt (y);
        // the drag starts after 5 px sideways or a row, at either move
        const bool moved = std::abs (dx * 0.5) > 5.0 || geo.pitchAt (y + dy * 0.5) != geo.pitchAt (y) || std::abs (dx) > 5.0 || dp != 0;
        double wantP = moved ? n.p + dp : n.p;
        if (moved && g.settings().inst.scaleLock)
            wantP = core::snapScale (wantP, g.settings().inst.key, g.settings().inst.scale);
        wantP = moved ? std::min (96.0, std::max (24.0, wantP)) : wantP;
        const double wantS = moved ? std::min (std::max (0.0, n.s + db), std::max (0.0, 16.0 - n.l)) : n.s;
        const bool changed = ! fixture::same (wantP, n.p) || ! fixture::same (wantS, n.s);
        movedOnes += changed ? 1 : 0;
        const bool ok = m != nullptr && fixture::same (m->p, wantP) && fixture::withinAbs (m->s, wantS, 1e-12) && rig.steps == (changed ? 1 : 0)
                        && (! changed || rig.labels.back() == "Move notes");
        if (ok)
            ++right;
        else if (firstBad.empty())
            firstBad = fmt ("trial %d (grid %g, lock %d): p %g s %g, want %g %g; %d steps", trial, grid, (int) g.settings().inst.scaleLock,
                            m ? m->p : -1.0, m ? m->s : -1.0, wantP, wantS, rig.steps);
    }
    note (fmt ("%d of %d drags as computed (%d moved the note)%s%s", right, total, movedOnes, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (right == total && movedOnes >= 100, "every drag moved the note by the grid-snapped beats and the scale-snapped rows (clamped to the clip "
                                               "and the roll), one undo step \"Move notes\" when it moved, none when it came to rest where it started");
}

SL_TEST ("ui", rollEndDotDeadZone)
{
    section ("Roll gestures: an end dot changes the end pitch only after 0.4 row, then by whole semitones (Shift: free)");

    int ok = 0, total = 0;
    std::string firstBad;
    for (const double rh : { 8.0, 14.0, 21.0, 30.0 })
    {
        for (int k = 0; k <= 30; ++k)
        {
            RollRig rig (spreadClip());
            auto& g = rig.gestures();
            g.geometry().setZoom (64.0, rh);
            g.settings().inst.glideMode = core::GlideMode::off;
            const std::string id = "d" + std::to_string (k % 8);
            rig.doc.setSelection ({ id }, id);
            g.sync();
            const auto c0 = rig.doc.current();
            const model::Note& n = *modelfx::findNote (*c0, id);
            const auto dot = g.geometry().endDot (n);
            const double dy = -(double) k * rh * 0.05;   // 0 .. 1.5 rows up
            const bool shift = k % 5 == 4;
            g.pointerDown (dot.x, dot.y, {}, 5000.0 + k * 1000.0);
            g.pointerMove (dot.x, dot.y + dy, { shift, false });
            const auto nowNote = modelfx::findNote (*rig.doc.current(), id);
            const double endV = model::noteLane (*nowNote, core::LaneId::pitch).back().v;
            const double v0 = model::noteLane (n, core::LaneId::pitch).back().v;
            g.pointerUp();
            ++total;
            double want = v0;
            const double y1 = dot.y + dy;   // the same arithmetic as the gesture's, so the edge at exactly 0.4 row decides alike
            if (! (std::abs (y1 - dot.y) < rh * 0.4))
            {
                double semi = (v0 - 0.5) * 24.0 + (dot.y - y1) / rh;
                if (! shift)
                    semi = core::jsRound (semi);
                semi = std::min (12.0, std::max (-12.0, semi));
                want = 0.5 + semi / 24.0;
            }
            if (fixture::withinAbs (endV, want, 1e-12))
                ++ok;
            else if (firstBad.empty())
                firstBad = fmt ("row %g, %g px up: end %g, want %g", rh, -dy, endV, want);
        }
    }
    note (fmt ("%d of %d end-dot drags as computed%s%s", ok, total, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (ok == total, "below 0.4 row the end pitch stays put; from 0.4 row it follows the pointer in whole semitones (any amount with Shift), "
                        "at most 12 semitones");
}

SL_TEST ("ui", rollOneUndoStepPerGesture)
{
    section ("Roll gestures: every finished gesture is exactly one undo step when it changed the clip, none when it did not");

    // 600 seeded gestures in Build on Strand Demo: presses on notes, their lines, bands, end dots, slide handles and
    // empty space, single and double, with Shift or Ctrl, 0 to 3 moves. After each release the undo steps it added
    // must be 1 when the clip changed (by content) and 0 when it did not.
    Rng rnd { 4403 };
    RollRig rig (strandDemo());
    auto& g = rig.gestures();
    g.sync();
    const auto& geo = g.geometry();
    int gestures = 0, changedOnes = 0, unchangedOnes = 0, bad = 0;
    std::string firstBad;
    double clock = 1e6;
    for (int i = 0; i < 600; ++i)
    {
        const auto before = rig.doc.current();
        const auto P = before->notes.empty() ? nullptr : before->notes[(std::size_t) (rnd() * (double) before->notes.size())];
        double x = 0.0, y = 0.0;
        const double kind = rnd();
        if (P != nullptr && kind < 0.3)
        {
            const double t = rnd();
            x = (P->s + P->l * t) * geo.beatWidth();
            y = geo.baseYAt (*P, t) - rnd() * 30.0 + 5.0;
        }
        else if (P != nullptr && kind < 0.5)
        {
            rig.doc.setSelection ({ P->id }, P->id);
            const auto dot = rnd() < 0.5 ? geo.endDot (*P) : geo.startDot (*P);
            x = dot.x;
            y = dot.y;
        }
        else if (P != nullptr && kind < 0.6)
        {
            rig.doc.setSelection ({ P->id }, P->id);
            if (const auto gh = geo.glideHandles (*P))
            {
                x = rnd() < 0.5 ? gh->start.x : gh->mid.x;
                y = rnd() < 0.5 ? gh->start.y : gh->mid.y;
            }
        }
        else
        {
            x = rnd() * 17.0 * geo.beatWidth();
            y = 200.0 + rnd() * 700.0;
        }
        Mods mods;
        const double m = rnd();
        mods.shift = m < 0.1;
        mods.ctrl = m >= 0.1 && m < 0.18;
        if (rnd() < 0.2)   // the first click of a double-click: a gesture of its own
        {
            const auto b0 = rig.doc.current();
            const int s0 = rig.steps;
            g.pointerDown (x, y, mods, clock);
            g.pointerUp();
            const bool changed = ! model::sameClip (*rig.doc.current(), *b0);
            ++gestures;
            (changed ? changedOnes : unchangedOnes) += 1;
            if (rig.steps - s0 != (changed ? 1 : 0))
            {
                ++bad;
                if (firstBad.empty())
                    firstBad = fmt ("gesture %d (first click at %g, %g): %d steps, clip %s", i, x, y, rig.steps - s0, changed ? "changed" : "unchanged");
            }
            clock += 90.0;
        }
        const auto b1 = rig.doc.current();
        const int s1 = rig.steps;
        g.pointerDown (x, y, mods, clock);
        const int moves = (int) (rnd() * 4.0);
        double mx = x, my = y;
        for (int k = 0; k < moves; ++k)
        {
            mx += (rnd() - 0.5) * 140.0;
            my += (rnd() - 0.5) * 70.0;
            g.pointerMove (mx, my, { rnd() < 0.2, false });
        }
        g.pointerUp();
        const bool changed = ! model::sameClip (*rig.doc.current(), *b1);
        ++gestures;
        (changed ? changedOnes : unchangedOnes) += 1;
        if (rig.steps - s1 != (changed ? 1 : 0))
        {
            ++bad;
            if (firstBad.empty())
                firstBad = fmt ("gesture %d at (%g, %g), %d moves: %d steps, clip %s", i, x, y, moves, rig.steps - s1, changed ? "changed" : "unchanged");
        }
        clock += 700.0;
    }
    note (fmt ("%d gestures: %d changed the clip, %d did not; %d undo steps in all%s%s", gestures, changedOnes, unchangedOnes, rig.steps,
               firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (bad == 0, fmt ("each of the %d gestures added 1 undo step when it changed the clip and 0 when it did not (%d did otherwise)", gestures, bad));
    check (changedOnes >= 150 && unchangedOnes >= 150, "the sequence holds both kinds: at least 150 gestures that changed the clip and 150 that did not");

    // a click on a note that moves nothing: 0 steps; the same press dragged a row up: 1 step "Move notes"; Undo puts it back
    RollRig r2 (spreadClip());
    auto& g2 = r2.gestures();
    g2.settings().inst.glideMode = core::GlideMode::off;
    g2.sync();
    const auto c0 = r2.doc.current();
    const auto& n = *c0->notes[3];
    const double x = (n.s + n.l * 0.4) * g2.geometry().beatWidth(), y = g2.geometry().baseYAt (n, 0.4);
    g2.pointerDown (x, y, {}, 1e6);
    g2.pointerMove (x + 2.0, y, {});
    g2.pointerUp();
    const bool clickNothing = r2.steps == 0 && r2.doc.current() == c0 && r2.doc.rollState().primary == n.id;
    g2.pointerDown (x, y, {}, 1e6 + 1000.0);
    g2.pointerMove (x, y - g2.geometry().rowHeight(), {});
    g2.pointerUp();
    const bool oneStep = r2.steps == 1 && r2.labels.back() == "Move notes" && fixture::same (modelfx::findNote (*r2.doc.current(), n.id)->p, n.p + 1.0);
    r2.undo();
    check (clickNothing && oneStep && model::sameClip (*r2.doc.current(), *c0),
           "a click on a note that moves 2 px sideways selects it with 0 undo steps; dragged a row up it is 1 step \"Move notes\", and Undo puts it back");
}

SL_TEST ("ui", rollDragPublishesWithoutHistory)
{
    section ("Roll gestures in the plugin: a drag's steps reach the engine while it moves, and its release is one undo step");

    // decision 2 through the real processor: RollHost's commit is pushUndoStep, the document publishes every version
    StrandLockProcessor proc;
    auto out = proc.editClip ("Spread notes", spreadClip());
    proc.waitForSnapshot();
    ui::roll::RollGestures g ({ proc.getDocument(), [&proc] (const std::string& l) { return proc.pushUndoStep (l, {}); }, {}, {} });
    g.settings().inst.glideMode = core::GlideMode::off;
    proc.getDocument().setSelection ({ "d3" }, std::string ("d3"));
    g.sync();
    const auto start = proc.getDocument().current();
    const model::Note& n = *modelfx::findNote (*start, "d3");
    const auto& geo = g.geometry();
    const double x = (n.s + n.l * 0.5) * geo.beatWidth(), y = geo.baseYAt (n, 0.5);

    g.pointerDown (x, y, {}, 1e6);
    int published = 0, rowsUp = 0;
    bool historyStill = true;
    for (int k = 1; k <= 3; ++k)
    {
        const auto v0 = proc.getDocument().version();
        g.pointerMove (x, y - k * geo.rowHeight(), {});
        const auto v1 = proc.getDocument().version();
        const bool reached = proc.waitForSnapshot() && proc.getSnapshotPublisher().stats().publishedVersion == v1;
        published += v1 == v0 + 1 && reached ? 1 : 0;
        rowsUp += fixture::same (modelfx::findNote (*proc.getDocument().current(), "d3")->p, n.p + k) ? 1 : 0;
        historyStill = historyStill && proc.undoLabel() == std::optional<std::string> ("Spread notes");
    }
    g.pointerUp();
    const bool oneStep = proc.undoLabel() == std::optional<std::string> ("Move notes");
    proc.undo();
    const bool undone = model::sameClip (*proc.getDocument().current(), *start) && proc.undoLabel() == std::optional<std::string> ("Spread notes");
    check (out.accepted && published == 3 && rowsUp == 3,
           fmt ("each of the 3 moves made a new clip version, a row higher, that the publisher compiled and handed to the audio thread (%d, %d)", published, rowsUp));
    check (historyStill, "no undo step while the drag moves");
    check (oneStep && undone, "the release is one undo step \"Move notes\"; Undo puts the note back and leaves \"Spread notes\" next");
}
