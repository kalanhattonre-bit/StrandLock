#include "SelectionBar.h"

#include "Drawers.h"
#include "InspectorText.h"
#include "RollPainter.h"
#include "RollView.h"

#include "core/JsCompat.h"
#include "core/Travel.h"
#include "model/NoteSettings.h"
#include "model/Recipes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace sl::ui::roll
{
    namespace
    {
        const model::Note* noteIn (const model::Clip& c, const std::string& id) noexcept
        {
            for (const auto& n : c.notes)
                if (n->id == id)
                    return n.get();
            return nullptr;
        }

        /** sl-ui-panels.js sameIds: the same ids, in any order. */
        bool sameIds (const std::vector<std::string>& a, const std::vector<std::string>& b)
        {
            if (a.size() != b.size())
                return false;
            const std::unordered_set<std::string> s (a.begin(), a.end());
            for (const auto& id : b)
                if (s.count (id) == 0)
                    return false;
            return true;
        }

        std::string laneName (core::LaneId l) { return core::laneInfo (l).name; }

        const char* connectName (model::ConnectMode m) noexcept
        {
            switch (m)
            {
                case model::ConnectMode::nearest:  return "nearest";
                case model::ConnectMode::furthest: return "furthest";
                case model::ConnectMode::random:   return "random";
            }
            return "nearest";
        }
    }

    //==============================================================================================
    int linksAmong (const model::Clip& clip, const std::vector<std::string>& ids)
    {
        const std::unordered_set<std::string> set (ids.begin(), ids.end());
        int n = 0;
        for (const auto& q : clip.notes)
            if (set.count (q->id) != 0 && ! q->glideTo.empty() && set.count (q->glideTo) != 0)
                ++n;
        return n;
    }

    int insideLinks (const model::Clip& clip, const std::vector<std::string>& ids)
    {
        // a link into a note that starts after the source starts and more than 0.13 beat before it ends
        const std::unordered_set<std::string> set (ids.begin(), ids.end());
        int n = 0;
        for (const auto& q : clip.notes)
        {
            if (set.count (q->id) == 0 || q->glideTo.empty())
                continue;
            const model::Note* m = noteIn (clip, q->glideTo);
            if (m != nullptr && m->s > q->s + 1e-6 && m->s < q->s + q->l - 0.13)
                ++n;
        }
        return n;
    }

    std::pair<double, double> walkDefaults (model::WalkField f)
    {
        // sl-ui-panels.js WALK_DEFAULTS and walkDefaults
        if (f.kind == model::WalkField::Kind::start)
            return { 0.0, 0.9 };
        if (f.kind == model::WalkField::Kind::vel)
            return { 0.4, 1.0 };
        switch (f.lane)
        {
            case core::LaneId::pitch:   return { 0.5, 1.0 };
            case core::LaneId::amp:     return { 0.35, 0.9 };
            case core::LaneId::vib:     return { 0.0, 0.5 };
            case core::LaneId::tone:    return { 0.35, 1.0 };
            case core::LaneId::formant: return { 0.3, 0.7 };
            case core::LaneId::hollow:  return { 0.0, 0.6 };
            case core::LaneId::travel:  return { core::travelSpeedToV (1.0), core::travelSpeedToV (2.0) };
            case core::LaneId::heads:   return { 0.5, 0.79 };
            case core::LaneId::grain:   return { 0.3, 0.7 };
            case core::LaneId::refresh: return { 0.3, 0.7 };
        }
        return { 0.2, 0.8 };
    }

    bool sameField (model::WalkField a, model::WalkField b) noexcept
    {
        return a.kind == b.kind && (a.kind != model::WalkField::Kind::lane || a.lane == b.lane);
    }

    std::string walkName (model::WalkField f)
    {
        if (f.kind == model::WalkField::Kind::start)
            return "Start";
        if (f.kind == model::WalkField::Kind::vel)
            return "Velocity";
        return laneName (f.lane);
    }

    std::string nNotes (int k)
    {
        return std::to_string (k) + (k == 1 ? " note" : " notes");
    }

    const char* genShapeName (model::GenShape s) noexcept
    {
        // sl-ui-panels.js GEN_NAMES
        switch (s)
        {
            case model::GenShape::squiggle:     return "Squiggle";
            case model::GenShape::triangle:     return "Triangle";
            case model::GenShape::sawup:        return "Saw up";
            case model::GenShape::sawdown:      return "Saw down";
            case model::GenShape::square:       return "Square";
            case model::GenShape::randsteps:    return "Random steps";
            case model::GenShape::smoothrandom: return "Smooth random";
        }
        return "Squiggle";
    }

    const char* shapeTitle (model::ShapeKey k) noexcept
    {
        // sl-ui-panels.js SHAPE_TITLES
        switch (k)
        {
            case model::ShapeKey::up:              return "Rises from the start of the note to the end";
            case model::ShapeKey::down:            return "Falls from the start to the end";
            case model::ShapeKey::swell:           return "Rises to the middle, then falls";
            case model::ShapeKey::dip:             return "Falls to the middle, then rises";
            case model::ShapeKey::squiggle:        return "A few smooth waves";
            case model::ShapeKey::random:          return "A few random steps (each click makes a new one)";
            case model::ShapeKey::decay:           return "Starts high and falls away quickly";
            case model::ShapeKey::reset:           return "Back to the flat starting line (a pattern or random on top stays)";
            case model::ShapeKey::tapestop:        return "Slows down to a stop, like a tape machine switched off";
            case model::ShapeKey::tapestart:       return "Starts stopped and speeds up to normal";
            case model::ShapeKey::reversehalf:     return "Plays forwards, then backwards from the middle";
            case model::ShapeKey::rewind:          return "Plays, then rewinds fast and stops";
            case model::ShapeKey::playthrough:     return "Plays the Region from its start to its end across the note";
            case model::ShapeKey::playthroughback: return "Plays the Region from its end to its start";
            case model::ShapeKey::fit:             return "Plays through the Region at exactly normal speed (1x) at this tempo";
        }
        return "";
    }

    std::string depthText (core::LaneId lane, double v)
    {
        // sl-ui-panels.js depthText: on Pitch a depth of 1 swings 2 semitones each way
        if (lane == core::LaneId::pitch)
            return plainUnits ("\xc2\xb1" + fmtNum (v * 2.0, 2) + " st");
        return core::jsNumberToString (core::jsRound (v * 100.0)) + "%";
    }

    std::string warpText (double v)
    {
        return std::abs (v) < 0.03 ? "even" : v > 0.0 ? "speeds up" : "slows down";
    }

    std::string smoothText (double v)
    {
        return v < 0.02 ? "steps" : v > 0.98 ? "smooth" : core::jsNumberToString (core::jsRound (v * 100.0)) + "%";
    }

    bool isRandomShape (model::GenShape s) noexcept
    {
        return s == model::GenShape::randsteps || s == model::GenShape::smoothrandom;
    }

    //==============================================================================================
    SelectionTools::SelectionTools (ToolHost h, RollGestures& g)
        : host (std::move (h)), gestures (g)
    {
        // sl-ui-panels.js init: a lane mode's lane is the Lane
        const auto st = host.doc.rollState();
        const auto L = st.activeLane();
        if (L)
            set.lane = *L;
        lastMode = st.mode;
        lastLane = L ? core::laneIndex (*L) : -1;
    }

    core::LaneId SelectionTools::lane() const
    {
        const auto L = host.doc.rollState().activeLane();
        return L ? *L : set.lane;
    }

    std::vector<std::string> SelectionTools::ids() const
    {
        return gestures.selectionInTimeOrder();
    }

    model::ClipPtr SelectionTools::clip() const
    {
        return host.doc.current();
    }

    model::NotePtr SelectionTools::primaryNote() const
    {
        const auto st = host.doc.rollState();
        if (st.primary.empty())
            return nullptr;
        const auto c = host.doc.current();
        for (const auto& n : c->notes)
            if (n->id == st.primary)
                return n;
        return nullptr;
    }

    void SelectionTools::toast (const std::string& text, int ms) const
    {
        if (host.toast)
            host.toast (text, ms);
    }

    void SelectionTools::showLane (core::LaneId L)
    {
        // sl-ui-panels.js showLane: a Sample lane shows mode 8 with that sub-lane, any other lane its own mode
        if (onShowLane)
        {
            onShowLane (L);
            return;
        }
        const auto st = host.doc.rollState();
        const auto& info = core::laneInfo (L);
        if (info.sub)
        {
            if (st.mode != 8 || st.subLane != L)
                gestures.setSubLane (L);
            return;
        }
        if (st.mode != info.mode)
            gestures.setMode (info.mode);
    }

    void SelectionTools::pickLane (core::LaneId L)
    {
        set.lane = L;
        showLane (L);
    }

    void SelectionTools::openDrawer (ToolSettings::Drawer d)
    {
        // sl-ui-panels.js openDrawer: the lane drawers show their lane on the roll, and read the primary note's
        // generator into their settings
        using D = ToolSettings::Drawer;
        set.drawer = d;
        if (d == D::shapes || d == D::pattern || d == D::random)
        {
            showLane (lane());
            loadGenValues();
        }
    }

    void SelectionTools::toggleDrawer (ToolSettings::Drawer d)
    {
        openDrawer (set.drawer == d ? ToolSettings::Drawer::none : d);
    }

    void SelectionTools::toggleTool (ToolSettings::Tool t)
    {
        set.tool = set.tool == t ? ToolSettings::Tool::none : t;
    }

    void SelectionTools::sync()
    {
        const auto st = host.doc.rollState();
        const auto L = st.activeLane();
        const int laneCode = L ? core::laneIndex (*L) : -1;
        if (st.mode != lastMode || laneCode != lastLane)
        {
            // sl-ui-panels.js A.on ('mode'): the lane mode's lane becomes the Lane, and a live session ends
            lastMode = st.mode;
            lastLane = laneCode;
            if (L)
                set.lane = *L;
            endLive();
        }
        if (live)
        {
            if (! sameIds (live->ids, ids()))
                endLive();                          // A.on ('selection'): another selection ends the session
            else if (host.doc.current() != live->last)
                live.reset();                       // A.on ('clip'): Undo, a drag on the roll: the session is gone
        }

        // another clip, selection, primary note or lane: the drawers' settings read the primary note's generator
        // again (sl-ui-panels.js loadGenValues on 'selection', 'clip' and 'mode')
        const auto cur = host.doc.current();
        const int ln = core::laneIndex (lane());
        if (cur != seenClip || st.selected != seenSelection || st.primary != seenPrimary || ln != seenLane)
        {
            seenClip = cur;
            seenSelection = st.selected;
            seenPrimary = st.primary;
            seenLane = ln;
            loadGenValues();
        }
    }

    //==============================================================================================
    // Editing (sl-ui-panels.js act, endLive, liveRun).

    bool SelectionTools::act (model::Clip proposed, const std::string& label)
    {
        model::EditResult r;
        r.clip = std::move (proposed);
        return act (std::move (r), label);
    }

    bool SelectionTools::act (model::EditResult result, const std::string& label)
    {
        // one model call, one undo step (7.7 decision 1); a cap refuses it with the clip unchanged and a toast
        endLive();
        if (ghosts != nullptr)
            ghosts->clear();
        const auto out = host.doc.apply (std::move (result));
        gestures.sync();
        if (out.refused)
        {
            toast (out.message, 4000);
            return false;
        }
        if (host.commit)
            host.commit (label);
        return true;
    }

    void SelectionTools::endLive()
    {
        if (live && live->dirty && host.doc.current() == live->last && host.commit)
            host.commit (live->label);
        live.reset();
    }

    bool SelectionTools::isLive (ToolSettings::Tool tool) const noexcept
    {
        if (! live)
            return false;
        return (tool == ToolSettings::Tool::walk && live->key == "walk") || (tool == ToolSettings::Tool::humanize && live->key == "hum");
    }

    bool SelectionTools::isLiveGen() const noexcept
    {
        return live && live->key.rfind ("gen:", 0) == 0;
    }

    void SelectionTools::ghost (std::optional<model::Clip> clip) const
    {
        // a slider before its session: what it would do, drawn, owned by the drawer (sl-ui-panels.js ghost (...,
        // D.drawer)); it goes when the mouse leaves the drawer
        if (ghosts != nullptr)
            ghosts->show (GhostPreview::drawerOwner(), std::move (clip));
    }

    void SelectionTools::liveRun (const std::string& key, const std::string& label, const std::function<model::Clip (const model::Clip&)>& make, bool final)
    {
        // one slider session: every move rebuilds the result from the same base clip (shown and played, no undo
        // step); the release (final) is one undo step. A click starts a session and is one step too.
        const auto cur = ids();
        if (! (live && live->key == key && sameIds (live->ids, cur) && host.doc.current() == live->last))
        {
            endLive();
            Live l;
            l.key = key;
            l.base = host.doc.current();
            l.ids = cur;
            l.label = label;
            live = std::move (l);
        }
        model::Clip next = make (*live->base);
        live->label = label;
        if (ghosts != nullptr)
            ghosts->clear();
        const auto out = host.doc.apply (std::move (next));
        gestures.sync();
        if (out.refused)
        {
            toast (out.message, 4000);
            return;
        }
        live->last = host.doc.current();
        if (final)
        {
            live->dirty = false;
            if (host.commit)
                host.commit (label);
        }
        else
        {
            live->dirty = true;
        }
    }

    //==============================================================================================
    // Connect (Y5).

    void SelectionTools::doConnect (model::ConnectMode mode, const std::string& label)
    {
        const auto sel = ids();
        if (sel.empty())
            return;
        model::Clip next = model::connect (*host.doc.current(), sel, mode, set.connectSeed);
        const int made = linksAmong (next, sel);
        if (! act (std::move (next), label))
            return;
        toast (made > 0 ? std::to_string (made) + (made == 1 ? " slide" : " slides") + " among the selected notes"
                        : std::string ("No slides made: a note slides into one that starts as it ends (up to 2 octaves away)"),
               3200);
    }

    void SelectionTools::connect (model::ConnectMode mode)
    {
        doConnect (mode, std::string ("Connect: ") + connectName (mode));
    }

    void SelectionTools::connectReroll()
    {
        if (ids().empty())
            return;
        set.connectSeed += 1.0;
        doConnect (model::ConnectMode::random, "Connect: random (re-rolled)");
    }

    void SelectionTools::disconnect()
    {
        const auto sel = ids();
        if (sel.empty())
            return;
        act (model::disconnect (*host.doc.current(), sel), "Disconnect");
    }

    //==============================================================================================
    // Tools (T1 to T5).

    model::Clip SelectionTools::walkClip (const model::Clip& base) const
    {
        return model::walk (base, ids(), set.walk.field, set.walk.from, set.walk.to);
    }

    void SelectionTools::walk (bool final, bool click)
    {
        if (ids().empty())
            return;
        // without a session a slider move draws what Walk would do (sl-ui-panels.js walkRun)
        if (click || isLive (ToolSettings::Tool::walk))
            liveRun ("walk", "Walk " + walkName (set.walk.field), [this] (const model::Clip& base) { return walkClip (base); }, final);
        else
            ghost (walkClip (*host.doc.current()));
    }

    void SelectionTools::setWalkField (model::WalkField field)
    {
        set.walk.field = field;
        const auto d = walkDefaults (field);
        set.walk.from = d.first;
        set.walk.to = d.second;
        if (isLive (ToolSettings::Tool::walk))
            endLive();
    }

    model::EditResult SelectionTools::chopPreview() const
    {
        return model::chop (*host.doc.current(), ids(), set.chop.rate, set.chop.step ? model::ChopStart::evenly() : model::ChopStart::by (0.0),
                            model::kMaxChopNotes);
    }

    void SelectionTools::chop()
    {
        const auto before = host.doc.current();
        const auto sel = ids();
        if (sel.empty())
            return;
        auto r = model::chop (*before, sel, set.chop.rate, set.chop.step ? model::ChopStart::evenly() : model::ChopStart::by (0.0),
                              model::kMaxChopNotes);
        if (r.refused)
        {
            toast ("That would make " + core::jsNumberToString (r.count) + " notes; the limit is " + std::to_string (model::kMaxChopNotes)
                       + ". Pick a slower rate or fewer notes.",
                   4000);
            return;
        }
        // the pieces made join the selection
        std::unordered_set<std::string> old;
        for (const auto& n : before->notes)
            old.insert (n->id);
        std::vector<std::string> made;
        for (const auto& n : r.clip.notes)
            if (old.count (n->id) == 0)
                made.push_back (n->id);
        const std::string primary = host.doc.rollState().primary;
        if (! act (std::move (r), std::string ("Chop ") + core::rateLabel (set.chop.rate)))
            return;
        auto all = sel;
        all.insert (all.end(), made.begin(), made.end());
        host.doc.setSelection (std::move (all), primary);
        toast ("Chopped into " + nNotes ((int) (sel.size() + made.size())));
    }

    void SelectionTools::laneTool (const std::string& name, const std::function<model::Clip (const model::Clip&, core::LaneId)>& make)
    {
        if (ids().empty())
            return;
        const auto L = lane();
        if (host.doc.rollState().activeLane() != L)
            showLane (L);
        act (make (*host.doc.current(), L), name + " (" + laneName (L) + ")");
    }

    void SelectionTools::reverse()
    {
        laneTool ("Reverse", [this] (const model::Clip& c, core::LaneId L) { return model::reverseLane (c, ids(), L); });
    }

    void SelectionTools::flip()
    {
        laneTool ("Flip", [this] (const model::Clip& c, core::LaneId L) { return model::flipLane (c, ids(), L); });
    }

    void SelectionTools::more()
    {
        laneTool ("More", [this] (const model::Clip& c, core::LaneId L) { return model::scaleLane (c, ids(), L, 2.0); });
    }

    void SelectionTools::less()
    {
        laneTool ("Less", [this] (const model::Clip& c, core::LaneId L) { return model::scaleLane (c, ids(), L, 0.5); });
    }

    void SelectionTools::selectSimilar (model::SimilarKind kind)
    {
        // the selection only (no undo step), like the primary note; the primary stays when it is among them
        const auto P = primaryNote();
        const std::string pid = P != nullptr ? P->id : std::string();
        const auto res = model::selectSimilar (*host.doc.current(), pid, kind, ids());
        const bool keep = ! pid.empty() && std::find (res.begin(), res.end(), pid) != res.end();
        host.doc.setSelection (res, keep ? pid : res.empty() ? std::string() : res.front());
        toast (! res.empty() ? nNotes ((int) res.size()) + " selected" : std::string ("No notes match"));
    }

    model::Clip SelectionTools::humanizeClip (const model::Clip& base) const
    {
        model::HumanizeAmounts a;
        a.time = set.hum.time;
        a.vel = set.hum.vel;
        a.volumeAndTone (set.hum.lanes);
        return model::humanize (base, ids(), a, set.hum.seed);
    }

    void SelectionTools::humRun (bool final, bool click, const std::string& label)
    {
        if (ids().empty())
            return;
        if (click || isLive (ToolSettings::Tool::humanize))
            liveRun ("hum", label, [this] (const model::Clip& base) { return humanizeClip (base); }, final);
        else
            ghost (humanizeClip (*host.doc.current()));
    }

    void SelectionTools::humanize (bool final, bool click)
    {
        humRun (final, click, "Humanize");
    }

    void SelectionTools::humanizeReroll()
    {
        if (ids().empty())
            return;
        set.hum.seed += 1.0;
        humRun (true, true, "Humanize: re-roll");
    }

    void SelectionTools::strum (model::StrumDir dir)
    {
        const auto sel = ids();
        if (sel.empty())
            return;
        act (model::strum (*host.doc.current(), sel, set.strumRate, dir), dir == model::StrumDir::up ? "Strum up" : "Strum down");
    }

    void SelectionTools::legato()
    {
        const auto before = host.doc.current();
        const auto sel = ids();
        if (sel.empty())
            return;
        model::Clip after = model::legato (*before, sel);
        bool stretched = false;
        for (const auto& n : after.notes)
        {
            const model::Note* o = noteIn (*before, n->id);
            if (o == nullptr || std::abs (o->l - n->l) > 1e-9)
            {
                stretched = true;
                break;
            }
        }
        if (! stretched)
        {
            // a note stretches to the next selected note (a chord to the next chord): one chord alone has nowhere to go
            double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
            int found = 0;
            for (const auto& id : sel)
                if (const model::Note* o = noteIn (*before, id))
                {
                    lo = std::min (lo, o->s);
                    hi = std::max (hi, o->s);
                    ++found;
                }
            const bool oneChord = found == 0 || hi - lo <= 0.05;
            toast (oneChord ? "No later selected note to stretch to: select the notes that follow as well" : "Already touching: nothing to stretch", 3500);
            return;
        }
        if (act (std::move (after), "Legato"))
            toast ("Each note now reaches the next one: try Connect \xe2\x96\xb8 Nearest");
    }

    //==============================================================================================
    // Shapes (Y2).

    std::optional<model::TravelKind> SelectionTools::travelKind() const
    {
        // sl-ui-panels.js travelKind: getMixed (...).travel, Speed with nothing selected
        const auto mx = model::getMixed (*host.doc.current(), ids());
        if (! mx)
            return model::TravelKind::speed;
        if (mx->travel.mixed)
            return std::nullopt;
        return mx->travel.value;
    }

    std::vector<model::ShapeKey> SelectionTools::shapeKeys (core::LaneId L) const
    {
        // sl-ui-panels.js shapeKeys: on Travel the notes' kind's shapes, both kinds' when they are mixed
        if (L != core::LaneId::travel)
            return model::shapeKeysFor (L);
        const auto kind = travelKind();
        if (kind)
            return model::shapeKeysFor (L, *kind);
        auto keys = model::shapeKeysFor (L, model::TravelKind::speed);
        for (const auto k : model::shapeKeysFor (L, model::TravelKind::path))
            if (model::shapeKeyKind (k) == model::TravelKind::path)
                keys.push_back (k);
        return keys;
    }

    model::ShapeOpts SelectionTools::shapeOpts() const
    {
        // sl-ui-panels.js shapeOpts: { seed: S.shapeSeed, bpm, sampleSeconds, inst }
        model::ShapeOpts o;
        o.seed = set.shapeSeed;
        const auto& rs = gestures.settings();
        o.bpm = rs.bpm;
        o.sampleSeconds = rs.sampleSeconds;
        o.inst = rs.inst;
        return o;
    }

    model::ShapeScope SelectionTools::scope() const noexcept
    {
        return gestures.settings().scope;
    }

    void SelectionTools::setScope (model::ShapeScope s) noexcept
    {
        gestures.settings().scope = s;
    }

    std::optional<model::Clip> SelectionTools::shapeClip (model::ShapeKey key, core::LaneId L, const std::vector<std::string>& sel) const
    {
        if (sel.empty())
            return std::nullopt;
        return model::applyShape (*host.doc.current(), sel, L, key, scope(), shapeOpts());
    }

    void SelectionTools::applyShape (model::ShapeKey key)
    {
        // sl-ui-panels.js DRAWERS.shapes' click: the roll shows the lane, one step, and a Random moves the seed on
        const auto sel = ids();
        if (sel.empty())
            return;
        const auto L = lane();
        if (host.doc.rollState().activeLane() != L)
            showLane (L);
        if (auto r = shapeClip (key, L, sel))
            act (std::move (*r), std::string (model::shapeName (key)) + " (" + laneName (L) + ")");
        if (key == model::ShapeKey::random)
            set.shapeSeed += 1.0;
    }

    //==============================================================================================
    // Pattern (Y3) and Random (Y4).

    model::Gen SelectionTools::drawerGen (model::GenKind kind, std::optional<model::GenShape> shape) const
    {
        // sl-ui-panels.js genOf: { kind } with the drawer's settings (and a chip's shape), through makeGen
        model::GenPatch p;
        if (kind == model::GenKind::pattern)
        {
            p.shape = shape.value_or (set.pat.shape);
            p.rate = set.pat.rate;
            p.d0 = set.pat.d0;
            p.d1 = set.pat.d1;
            p.warp = set.pat.warp;
            p.lockToBeat = set.pat.lockToBeat;
            p.perNote = set.pat.perNote;
        }
        else
        {
            if (shape)
                p.shape = *shape;
            p.rate = set.rnd.rate;
            p.amount = set.rnd.amount;
            p.smooth = set.rnd.smooth;
            p.perNote = set.rnd.perNote;
            p.lockToBeat = set.rnd.lockToBeat;
        }
        return model::makeGen (kind, p);
    }

    model::Clip SelectionTools::putGen (const model::Clip& base, const std::vector<std::string>& sel, core::LaneId L, model::GenKind kind,
                                        std::optional<model::GenShape> shape) const
    {
        // sl-ui-panels.js putGen: each note keeps its own seed and key (and its invert when it had the same kind)
        const model::Gen g = drawerGen (kind, shape);
        return model::editNotes (base, sel, [&g, L] (model::Note& n, const model::Note&)
        {
            model::Gen next = g;
            if (const auto& old = n.genOf (L))
            {
                next.seed = old->seed;
                next.key = old->key;
                next.invert = old->kind == g.kind ? old->invert : false;
            }
            n.genOf (L) = model::normalizeGen (std::move (next));
        });
    }

    std::optional<model::Gen> SelectionTools::primaryGen (core::LaneId L) const
    {
        const auto P = primaryNote();
        if (P == nullptr || ! P->genOf (L))
            return std::nullopt;
        return P->genOf (L);
    }

    bool SelectionTools::anyGen (core::LaneId L, bool randomish) const
    {
        // sl-ui-panels.js anyGen (L, isRandomish): a selected note's generator on the lane (a random one, or a
        // pattern of a random shape)
        const auto c = host.doc.current();
        const auto order = gestures.selectionInTimeOrder();
        const std::unordered_set<std::string> sel (order.begin(), order.end());
        for (const auto& n : c->notes)
        {
            if (sel.count (n->id) == 0)
                continue;
            const auto& g = n->genOf (L);
            if (g && (! randomish || g->kind == model::GenKind::random || isRandomShape (g->shape)))
                return true;
        }
        return false;
    }

    bool SelectionTools::isEditing (model::GenKind kind) const
    {
        const auto g = primaryGen (lane());
        return g.has_value() && g->kind == kind;
    }

    void SelectionTools::genChanged (model::GenKind kind, bool final, const std::string& what)
    {
        // sl-ui-panels.js genChanged: live while the primary note has this generator, else a ghost until Apply
        const auto sel = ids();
        if (sel.empty())
            return;
        const auto L = lane();
        if (isEditing (kind))
        {
            const std::string word = kind == model::GenKind::pattern ? "Pattern" : "Random";
            liveRun (std::string ("gen:") + model::code (kind) + ":" + core::laneInfo (L).code, word + (what.empty() ? "" : " " + what) + " (" + laneName (L) + ")",
                     [this, L, kind] (const model::Clip& base) { return putGen (base, ids(), L, kind); }, final);
        }
        else
        {
            ghost (putGen (*host.doc.current(), sel, L, kind));
        }
    }

    void SelectionTools::applyGen (model::GenKind kind)
    {
        // sl-ui-panels.js applyGen: the lane shown, one step; the controls change it live from then on
        if (ids().empty())
            return;
        const auto L = lane();
        if (host.doc.rollState().activeLane() != L)
            showLane (L);
        const std::string word = kind == model::GenKind::pattern ? "Pattern" : "Random";
        liveRun (std::string ("gen:") + model::code (kind) + ":" + core::laneInfo (L).code, word + " (" + laneName (L) + ")",
                 [this, L, kind] (const model::Clip& base) { return putGen (base, ids(), L, kind); }, true);
    }

    void SelectionTools::rerollGen()
    {
        const auto sel = ids();
        if (sel.empty())
            return;
        const auto L = lane();
        act (model::reroll (*host.doc.current(), sel, L), "Re-roll (" + laneName (L) + ")");
    }

    void SelectionTools::removeGen()
    {
        const auto sel = ids();
        if (sel.empty())
            return;
        const auto L = lane();
        act (model::clearGen (*host.doc.current(), sel, L), "Remove pattern (" + laneName (L) + ")");
    }

    void SelectionTools::bake()
    {
        // sl-ui-panels.js bake: Turn into nodes, or a toast when there is nothing to turn
        const auto sel = ids();
        if (sel.empty())
            return;
        const auto L = lane();
        if (! anyGen (L))
        {
            toast ("No pattern or random on " + laneName (L) + " for the selected notes");
            return;
        }
        if (host.doc.rollState().activeLane() != L)
            showLane (L);
        act (model::bakeGen (*host.doc.current(), sel, L), "Turn into nodes (" + laneName (L) + ")");
    }

    void SelectionTools::loadGenValues()
    {
        // sl-ui-panels.js loadGenValues: the primary note's generator on the lane is what the drawer shows
        const auto g = primaryGen (lane());
        if (! g)
            return;
        if (g->kind == model::GenKind::pattern)
        {
            set.pat.shape = g->shape;
            set.pat.rate = g->rate;
            set.pat.d0 = g->d0;
            set.pat.d1 = g->d1;
            set.pat.warp = g->warp;
            set.pat.lockToBeat = g->lockToBeat;
            set.pat.perNote = g->perNote;
        }
        else
        {
            set.rnd.rate = g->rate;
            set.rnd.amount = g->amount;
            set.rnd.smooth = g->smooth;
            set.rnd.perNote = g->perNote;
            set.rnd.lockToBeat = g->lockToBeat;
        }
    }

    //==============================================================================================
    // Ghosts (sl-ui-panels.js ghost functions): the same model calls as the clicks, made off to the side.

    std::optional<model::Clip> SelectionTools::previewShape (model::ShapeKey key) const
    {
        return shapeClip (key, lane(), ids());
    }

    std::optional<model::Clip> SelectionTools::previewGen (model::GenKind kind, std::optional<model::GenShape> shape) const
    {
        const auto sel = ids();
        if (sel.empty())
            return std::nullopt;
        return putGen (*host.doc.current(), sel, lane(), kind, shape);
    }

    std::optional<model::Clip> SelectionTools::previewReroll() const
    {
        const auto sel = ids();
        if (sel.empty())
            return std::nullopt;
        return model::reroll (*host.doc.current(), sel, lane());
    }

    std::optional<model::Clip> SelectionTools::previewRemove() const
    {
        const auto sel = ids();
        if (sel.empty() || ! anyGen (lane()))
            return std::nullopt;
        return model::clearGen (*host.doc.current(), sel, lane());
    }

    std::optional<model::Clip> SelectionTools::previewBake() const
    {
        const auto sel = ids();
        if (sel.empty() || ! anyGen (lane()))
            return std::nullopt;
        return model::bakeGen (*host.doc.current(), sel, lane());
    }

    std::optional<model::Clip> SelectionTools::previewConnect (model::ConnectMode mode, bool reroll) const
    {
        const auto sel = ids();
        if (sel.empty())
            return std::nullopt;
        return model::connect (*host.doc.current(), sel, mode, reroll ? set.connectSeed + 1.0 : set.connectSeed);
    }

    std::optional<model::Clip> SelectionTools::previewDisconnect() const
    {
        const auto sel = ids();
        const auto c = host.doc.current();
        if (sel.empty() || linksAmong (*c, sel) == 0)
            return std::nullopt;
        return model::disconnect (*c, sel);
    }

    std::optional<model::Clip> SelectionTools::previewWalk() const
    {
        if (ids().empty())
            return std::nullopt;
        return walkClip (*host.doc.current());
    }

    std::optional<model::Clip> SelectionTools::previewChop() const
    {
        if (ids().empty())
            return std::nullopt;
        auto r = chopPreview();
        if (r.refused)
            return std::nullopt;
        return std::move (r.clip);
    }

    std::optional<model::Clip> SelectionTools::previewHumanize() const
    {
        if (ids().empty())
            return std::nullopt;
        return humanizeClip (*host.doc.current());
    }

    std::optional<model::Clip> SelectionTools::previewStrum (model::StrumDir dir) const
    {
        const auto sel = ids();
        if (sel.empty())
            return std::nullopt;
        return model::strum (*host.doc.current(), sel, set.strumRate, dir);
    }

    std::optional<model::Clip> SelectionTools::previewLegato() const
    {
        const auto sel = ids();
        if (sel.empty())
            return std::nullopt;
        return model::legato (*host.doc.current(), sel);
    }

    std::optional<model::Clip> SelectionTools::previewLaneTool (int which) const
    {
        const auto sel = ids();
        if (sel.empty())
            return std::nullopt;
        const auto clipNow = host.doc.current();
        const auto& c = *clipNow;
        const auto L = lane();
        switch (which)
        {
            case 0:  return model::reverseLane (c, sel, L);
            case 1:  return model::flipLane (c, sel, L);
            case 2:  return model::scaleLane (c, sel, L, 2.0);
            default: return model::scaleLane (c, sel, L, 0.5);
        }
    }

    std::optional<model::Clip> SelectionTools::previewRecipe (int n) const
    {
        // sl-ui-sample.js recipe hover: applyRecipe (clip, inst, n, the selection or every note).clip
        const auto sel = ids();
        auto r = model::applyRecipe (*host.doc.current(), gestures.settings().inst, n, sel.empty() ? model::Ids::all() : model::Ids (sel));
        if (! r)
            return std::nullopt;
        return std::move (r->clip);
    }

    //==============================================================================================
    /** The bar's Lane: a list of the ten lanes with a 2 px border in the lane's colour (.slp-lanesel). */
    class SelectionBar::LaneBox final : public juce::ComboBox
    {
    public:
        explicit LaneBox (const Theme& t) : juce::ComboBox ("Lane"), theme (t)
        {
            for (int i = 0; i < core::kNumLanes; ++i)
            {
                const auto& info = core::kLanes[i];
                addItem ((info.sub ? juce::String ("Sample: ") : juce::String()) + info.name, i + 1);
            }
            setTooltip ("The lane that Shapes, Pattern, Random, Turn into nodes, Reverse, Flip, More and Less change");
        }

        void showLane (core::LaneId l)
        {
            if (l == lane && getSelectedId() == core::laneIndex (l) + 1)
                return;
            lane = l;
            setSelectedId (core::laneIndex (l) + 1, juce::dontSendNotification);
            repaint();
        }

        core::LaneId picked() const
        {
            return static_cast<core::LaneId> (juce::jlimit (0, core::kNumLanes - 1, getSelectedId() - 1));
        }

        void paint (juce::Graphics& g) override
        {
            juce::ComboBox::paint (g);
            juce::Path edge;
            edge.addRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), Theme::corner);
            g.setColour (RollPainter::laneColour (theme.colours(), lane).withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));
            g.strokePath (edge, juce::PathStrokeType (2.0f));
        }

    private:
        const Theme& theme;
        core::LaneId lane = core::LaneId::tone;
    };

    namespace
    {
        const char* const kDrawerNames[] = { "Shapes", "Pattern", "Random", "Connect", "Tools" };
        const ToolSettings::Drawer kDrawers[] = { ToolSettings::Drawer::shapes, ToolSettings::Drawer::pattern, ToolSettings::Drawer::random,
                                                  ToolSettings::Drawer::connect, ToolSettings::Drawer::tools };
        const char* const kDrawerTips[] = {
            "One-click shapes for the lane on every selected note: Ramp, Swell, Tape stop...",
            "A repeating shape at a musical rate (1 bar to 1/64), riding on top of the drawing",
            "Random movement that lands on a musical grid, and stays the same until you Re-roll",
            "Join the selected notes with slides: Nearest, Furthest or Random",
            "Walk, Chop, Reverse, Flip, More, Less, Select similar, Humanize, Strum and Legato",
        };
        constexpr int kLaneBoxWidth = 150, kButtonHeight = 24, kGap = 4;

        bool isLaneDrawer (ToolSettings::Drawer d) noexcept
        {
            return d == ToolSettings::Drawer::shapes || d == ToolSettings::Drawer::pattern || d == ToolSettings::Drawer::random;
        }
    }

    /** The mouse leaving the open drawer's box (not one of its controls for another): its own preview goes. */
    class SelectionBar::DrawerWatch final : public juce::MouseListener
    {
    public:
        explicit DrawerWatch (SelectionBar& b) : bar (b) { bar.drawerView.addMouseListener (this, true); }
        ~DrawerWatch() override { bar.drawerView.removeMouseListener (this); }

        void mouseExit (const juce::MouseEvent& e) override
        {
            if (! bar.drawerView.getScreenBounds().contains (e.getScreenPosition()))
                bar.drawerLeft();
        }

    private:
        SelectionBar& bar;
    };

    SelectionBar::SelectionBar (const Theme& t, RollView& rv)
        : theme (t),
          roll (rv),
          idleText ("Select notes (click one, or drag a box around several) to use the selection tools."),
          laneLabel (t, "Lane", Caption::Style::label)
    {
        laneBox = std::make_unique<LaneBox> (t);
        laneBox->onChange = [this]
        {
            // sl-ui-panels.js laneSel: the Lane, and the roll shows its mode
            getTools().pickLane (laneBox->picked());
            roll.edited();
        };
        addChildComponent (*laneBox);
        addChildComponent (laneLabel);

        for (int i = 0; i < 5; ++i)
        {
            auto b = std::make_unique<ArrowButton> (t, kDrawerNames[i]);
            b->setTooltip (kDrawerTips[i]);
            const auto d = kDrawers[i];
            b->onClick = [this, d] { openDrawer (d); };
            addChildComponent (*b);
            drawerButtons.push_back (std::move (b));
        }
        bakeButton = std::make_unique<FlatButton> (t, "Turn into nodes");
        bakeButton->setTooltip ("Turn the pattern or random on this lane into ordinary nodes you can move by hand");
        bakeButton->onClick = [this]
        {
            getTools().bake();
            roll.edited();
        };
        addChildComponent (*bakeButton);

        // the buttons' texts in the hint bar while the mouse is on them, and Turn into nodes' ghost
        hooks = std::make_unique<HoverHooks> (&roll.getGhosts(), [this] (const juce::String& text) { roll.showHint (text); });
        for (auto& b : drawerButtons)
            hooks->add (*b, {});
        hooks->add (*bakeButton, [this] { return getTools().previewBake(); });
        lineGap = std::make_unique<LineSeparator> (t);
        addChildComponent (*lineGap);

        drawerView.setScrollBarsShown (true, false);
        drawerView.setScrollBarThickness (drawerScrollBar);
        addChildComponent (drawerView);
        drawerWatch = std::make_unique<DrawerWatch> (*this);
        sync();
    }

    SelectionBar::~SelectionBar()
    {
        drawerWatch.reset();
        hooks.reset();
        drawerView.setViewedComponent (nullptr, false);
        drawer.reset();
    }

    SelectionTools& SelectionBar::getTools() noexcept
    {
        return roll.getTools();
    }

    FlatButton& SelectionBar::getDrawerButton (ToolSettings::Drawer d) noexcept
    {
        for (int i = 0; i < 5; ++i)
            if (kDrawers[i] == d)
                return *drawerButtons[(std::size_t) i];
        return *drawerButtons.back();
    }

    juce::ComboBox& SelectionBar::getLaneBox() noexcept
    {
        return *laneBox;
    }

    void SelectionBar::openDrawer (ToolSettings::Drawer d)
    {
        getTools().toggleDrawer (d);
        rebuildDrawer();
        sync();
    }

    void SelectionBar::showDrawer (ToolSettings::Drawer d)
    {
        getTools().openDrawer (d);
        rebuildDrawer();
        sync();
    }

    void SelectionBar::drawerLeft()
    {
        roll.getGhosts().leave (GhostPreview::drawerOwner());
    }

    int SelectionBar::drawerKey() const
    {
        // a lane drawer is built for its lane (its captions, its depth's units), and Shapes on Travel for the notes'
        // kind (its shapes); another lane or kind builds it again, as the prototype's renderDrawer does
        auto& tools = roll.getTools();
        const auto d = tools.settings().drawer;
        if (! isLaneDrawer (d))
            return -1;
        const auto L = tools.lane();
        int kind = 0;
        if (d == ToolSettings::Drawer::shapes && L == core::LaneId::travel)
        {
            const auto k = tools.travelKind();
            kind = ! k ? 3 : *k == model::TravelKind::path ? 2 : 1;
        }
        return core::laneIndex (L) * 4 + kind;
    }

    void SelectionBar::rebuildDrawer()
    {
        // only from the bar's own buttons and sync: never while one of the drawer's own controls is calling out
        auto& tools = getTools();
        drawerView.setViewedComponent (nullptr, false);
        drawer.reset();
        shownDrawer = tools.settings().drawer;
        shownKey = drawerKey();
        if (shownDrawer == ToolSettings::Drawer::shapes)
            drawer = std::make_unique<ShapesDrawer> (theme, tools, roll);
        else if (shownDrawer == ToolSettings::Drawer::pattern)
            drawer = std::make_unique<GenDrawer> (theme, tools, roll, model::GenKind::pattern);
        else if (shownDrawer == ToolSettings::Drawer::random)
            drawer = std::make_unique<GenDrawer> (theme, tools, roll, model::GenKind::random);
        else if (shownDrawer == ToolSettings::Drawer::connect)
            drawer = std::make_unique<ConnectDrawer> (theme, tools, roll);
        else if (shownDrawer == ToolSettings::Drawer::tools)
            drawer = std::make_unique<ToolsDrawer> (theme, tools, roll);
        if (drawer != nullptr)
        {
            drawer->onHeightChange = [this] { drawerHeightChanged(); };
            drawerView.setViewedComponent (drawer.get(), false);
            noKeyboardFocus (*drawer);
            noKeyboardFocus (drawerView);
        }
        drawerView.setVisible (drawer != nullptr);
        drawerHeightChanged();
    }

    bool SelectionBar::Seen::operator== (const Seen& o) const noexcept
    {
        return clip == o.clip && ids == o.ids && lane == o.lane && juce::exactlyEqual (bpm, o.bpm) && juce::exactlyEqual (seconds, o.seconds)
            && juce::exactlyEqual (regionStart, o.regionStart) && juce::exactlyEqual (regionEnd, o.regionEnd) && scope == o.scope;
    }

    void SelectionBar::sync()
    {
        auto& tools = getTools();
        tools.sync();
        const auto ids = tools.ids();
        const bool none = ids.empty();
        const auto& st = tools.settings();

        // nothing selected and no drawer open: the bar takes no room (sl-ui-panels.js paintBar hides it); while the
        // roll holds its room still (MainView) the line says how to select
        const bool nowIdle = none && st.drawer == ToolSettings::Drawer::none;
        bool changed = false;
        if (nowIdle != idle)
        {
            idle = nowIdle;
            laneBox->setVisible (! idle);
            laneLabel.setVisible (! idle);
            for (auto& b : drawerButtons)
                b->setVisible (! idle);
            bakeButton->setVisible (! idle);
            lineGap->setVisible (! idle);
            repaint();
            changed = true;
        }
        laneBox->showLane (tools.lane());
        bakeButton->setEnabled (tools.anyGen (tools.lane()));   // sl-ui-panels.js paintBar
        for (int i = 0; i < 5; ++i)
        {
            const bool open = st.drawer == kDrawers[i];
            drawerButtons[(std::size_t) i]->setToggleState (open, juce::dontSendNotification);
            drawerButtons[(std::size_t) i]->setOpen (open);
        }
        bool rebuilt = false;
        if (st.drawer != shownDrawer || (isLaneDrawer (st.drawer) && drawerKey() != shownKey))
        {
            rebuildDrawer();
            rebuilt = true;
        }
        if (drawer != nullptr)
        {
            // the drawer follows the clip, the selection, the lane, the tempo and the sound; the editor's timer calls
            // this too, so it is synced again only when one of them changed
            const auto& rs = tools.rollSettings();
            Seen now { tools.clip(), ids, core::laneIndex (tools.lane()), rs.bpm, rs.sampleSeconds, rs.inst.region.start, rs.inst.region.end,
                       static_cast<int> (rs.scope) };
            if (rebuilt || ! (now == seen))
            {
                seen = std::move (now);
                // with nothing selected an open drawer stays where it is, faded and asleep (it wakes with the next
                // selection), so clearing the selection never moves the roll
                drawer->setAlpha (none ? 0.45f : 1.0f);
                drawer->setInterceptsMouseClicks (! none, ! none);
                drawer->sync();
                changed = true;
            }
        }
        if (changed)
            drawerHeightChanged();
    }

    int SelectionBar::heightFor (int width) const
    {
        if (drawer == nullptr)
            return idle ? 0 : lineHeight;
        const int inner = juce::jmax (40, width - 2 * drawerPadX);
        const int box = juce::jmin (drawerMax, drawer->heightFor (inner) + 2 * drawerPadY);
        return lineHeight + box + 4;
    }

    void SelectionBar::setDrawerMax (int px)
    {
        drawerMax = juce::jlimit (drawerMinHeight, drawerMaxHeight, px);
    }

    void SelectionBar::drawerHeightChanged()
    {
        const int want = heightFor (getWidth());
        if (want != lastWantedHeight)
        {
            lastWantedHeight = want;
            if (onHeightChange)
                onHeightChange();
        }
        resized();
    }

    void SelectionBar::resized()
    {
        // the line: LANE [list] Shapes Pattern Random Turn into nodes | Connect Tools (sl-ui-panels.js buildBar)
        const int cy = lineHeight / 2;
        int x = 0;
        auto place = [&x, cy] (juce::Component& c, int w, int h)
        {
            c.setBounds (x, cy - h / 2, w, h);
            x += w + kGap;
        };
        place (laneLabel, laneLabel.preferredWidth(), 18);
        place (*laneBox, kLaneBoxWidth, 26);
        for (int i = 0; i < 3; ++i)
            place (*drawerButtons[(std::size_t) i], drawerButtons[(std::size_t) i]->widthWithArrow(), kButtonHeight);
        place (*bakeButton, bakeButton->preferredWidth() + 2, kButtonHeight);
        place (*lineGap, 5, kButtonHeight);
        for (int i = 3; i < 5; ++i)
            place (*drawerButtons[(std::size_t) i], drawerButtons[(std::size_t) i]->widthWithArrow(), kButtonHeight);

        // the open drawer's box under the line (.slp-drawer: 7 px by 8 px of padding), scrolling when it is taller
        // than the most it may take
        drawerArea = {};
        if (drawer == nullptr)
            return;
        const int w = getWidth();
        const int inner = juce::jmax (40, w - 2 * drawerPadX);
        const int content = drawer->heightFor (inner);
        const int box = juce::jmin (drawerMax, content + 2 * drawerPadY);
        drawerArea = { 0, lineHeight, w, box };
        const auto view = drawerArea.reduced (drawerPadX, drawerPadY);
        drawerView.setBounds (view);
        const bool scrolls = content > view.getHeight();
        const int cw = scrolls ? view.getWidth() - drawerView.getScrollBarThickness() : view.getWidth();
        drawer->setSize (cw, drawer->heightFor (cw));
    }

    void SelectionBar::paint (juce::Graphics& g)
    {
        if (idle)
        {
            g.setColour (theme (Tok::dim));
            g.setFont (Theme::px (12.0f));
            g.drawText (idleText, juce::Rectangle<int> (0, 0, getWidth(), lineHeight), juce::Justification::centredLeft, true);
        }
        if (! drawerArea.isEmpty())
        {
            // .slp-drawer: panel2, a line around it, an 8 px corner
            juce::Path box;
            box.addRoundedRectangle (drawerArea.toFloat().reduced (0.5f), 8.0f);
            g.setColour (theme (Tok::panel2));
            g.fillPath (box);
            g.setColour (theme (Tok::line));
            g.strokePath (box, juce::PathStrokeType (1.0f));
        }
    }
}
