#include "SelectionBar.h"

#include "Drawers.h"
#include "RollPainter.h"
#include "RollView.h"

#include "core/JsCompat.h"
#include "core/Travel.h"

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

    void SelectionTools::toggleDrawer (ToolSettings::Drawer d)
    {
        using D = ToolSettings::Drawer;
        set.drawer = set.drawer == d ? D::none : d;
        // the lane drawers show their lane on the roll (S2)
        if (set.drawer == D::shapes || set.drawer == D::pattern || set.drawer == D::random)
            showLane (lane());
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
        return (tool == ToolSettings::Tool::walk && live->key == LiveKey::walk) || (tool == ToolSettings::Tool::humanize && live->key == LiveKey::hum);
    }

    void SelectionTools::liveRun (LiveKey key, const std::string& label, const std::function<model::Clip (const model::Clip&)>& make, bool final)
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
        // without a session a slider only sets its value: S2's ghost shows what Walk would do
        if (click || isLive (ToolSettings::Tool::walk))
            liveRun (LiveKey::walk, "Walk " + walkName (set.walk.field), [this] (const model::Clip& base) { return walkClip (base); }, final);
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
            liveRun (LiveKey::hum, label, [this] (const model::Clip& base) { return humanizeClip (base); }, final);
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
        constexpr const char* kNotYet = "Shapes, Pattern, Random and Turn into nodes arrive in the next build";
    }

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
            if (i < 3)
                b->onClick = [this] { getTools().toast (kNotYet, 3000); };   // S2's drawers
            else
                b->onClick = [this, d] { openDrawer (d); };
            addChildComponent (*b);
            drawerButtons.push_back (std::move (b));
        }
        bakeButton = std::make_unique<FlatButton> (t, "Turn into nodes");
        bakeButton->setTooltip ("Turn the pattern or random on this lane into ordinary nodes you can move by hand");
        bakeButton->onClick = [this] { getTools().toast (kNotYet, 3000); };
        addChildComponent (*bakeButton);
        lineGap = std::make_unique<LineSeparator> (t);
        addChildComponent (*lineGap);

        drawerView.setScrollBarsShown (true, false);
        drawerView.setScrollBarThickness (8);
        addChildComponent (drawerView);
        sync();
    }

    SelectionBar::~SelectionBar()
    {
        drawerView.setViewedComponent (nullptr, false);
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

    void SelectionBar::rebuildDrawer()
    {
        // only from the bar's own buttons and sync: never while one of the drawer's own controls is calling out
        auto& tools = getTools();
        drawerView.setViewedComponent (nullptr, false);
        drawer.reset();
        shownDrawer = tools.settings().drawer;
        if (shownDrawer == ToolSettings::Drawer::connect)
            drawer = std::make_unique<ConnectDrawer> (theme, tools, roll);
        else if (shownDrawer == ToolSettings::Drawer::tools)
            drawer = std::make_unique<ToolsDrawer> (theme, tools, roll);
        if (drawer != nullptr)
        {
            drawer->onHeightChange = [this] { drawerHeightChanged(); };
            drawerView.setViewedComponent (drawer.get(), false);
        }
        drawerView.setVisible (drawer != nullptr);
        drawerHeightChanged();
    }

    bool SelectionBar::Seen::operator== (const Seen& o) const noexcept
    {
        return clip == o.clip && ids == o.ids && lane == o.lane && juce::exactlyEqual (bpm, o.bpm) && juce::exactlyEqual (seconds, o.seconds)
            && juce::exactlyEqual (regionStart, o.regionStart) && juce::exactlyEqual (regionEnd, o.regionEnd);
    }

    void SelectionBar::sync()
    {
        auto& tools = getTools();
        tools.sync();
        const auto ids = tools.ids();
        const bool none = ids.empty();
        const auto& st = tools.settings();

        // nothing selected and no drawer open: the line says how to select (sl-ui-panels.js paintBar hides the bar)
        const bool nowIdle = none && st.drawer == ToolSettings::Drawer::none;
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
        }
        laneBox->showLane (tools.lane());
        for (int i = 0; i < 5; ++i)
        {
            const bool open = st.drawer == kDrawers[i];
            drawerButtons[(std::size_t) i]->setToggleState (open, juce::dontSendNotification);
            drawerButtons[(std::size_t) i]->setOpen (open);
        }
        bool rebuilt = false;
        if (st.drawer != shownDrawer)
        {
            rebuildDrawer();
            rebuilt = true;
        }
        if (drawer == nullptr)
            return;
        // the drawer follows the clip, the selection, the lane, the tempo and the sound; the editor's timer calls this
        // too, so it is synced again only when one of them changed
        const auto& rs = tools.rollSettings();
        Seen now { tools.clip(), ids, core::laneIndex (tools.lane()), rs.bpm, rs.sampleSeconds, rs.inst.region.start, rs.inst.region.end };
        if (! rebuilt && now == seen)
            return;
        seen = std::move (now);
        // with nothing selected an open drawer stays where it is, faded and asleep (it wakes with the next selection),
        // so clearing the selection never moves the roll
        drawer->setAlpha (none ? 0.45f : 1.0f);
        drawer->setInterceptsMouseClicks (! none, ! none);
        drawer->sync();
        drawerHeightChanged();
    }

    int SelectionBar::heightFor (int width) const
    {
        if (drawer == nullptr)
            return lineHeight;
        const int inner = juce::jmax (40, width - 2 * drawerPadX);
        const int box = juce::jmin (drawerMaxHeight, drawer->heightFor (inner) + 2 * drawerPadY);
        return lineHeight + box + 4;
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
        const int box = juce::jmin (drawerMaxHeight, content + 2 * drawerPadY);
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
