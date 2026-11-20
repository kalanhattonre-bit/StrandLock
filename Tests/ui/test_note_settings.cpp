// The Note settings tab in the real editor (PLUGIN-PLAN 4 M6 "Note settings", 7.8 T2; S7; sl-ui-panels.js
// buildNoteSettings and syncNoteSettings):
//
// - "mixed" shows exactly when the selected notes differ: 240 seeded selections of a clip whose notes hold every kind of
//   own setting, every group against model::getMixed (its tag, the lit segment, the list, the sliders' "mixed");
// - every change is one model call and one undo step: each control gives the bytes model::setNoteSample gives on the
//   same clip, with the prototype's label, and one Undo brings back the clip before; the sliders change the notes while
//   they move with no undo step and are one step on release;
// - Mirror on notes with one head also sets their Heads lane to 2 heads in that step (and says so); Path on a flat Travel
//   line makes it play through the Region (Fit);
// - the first line, the faded groups and their reasons, and the tab asleep with nothing selected;
// and pictures.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Rates.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/Node.h"
#include "model/NoteSettings.h"
#include "model/Shapes.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/Drawers.h"
#include "ui/roll/RollView.h"
#include "ui/tabs/NoteSettingsTab.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;
    using ui::NoteSettingsTab;
    using ui::ThemeId;
    using Group = NoteSettingsTab::Group;
    using Bytes = std::vector<std::uint8_t>;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    /** Twelve notes a beat apart, each holding its own mix of settings: every field takes two or three values across
        them, so a selection is mixed in some fields and shared in others. */
    model::Clip settingsClip()
    {
        model::Clip c = model::newClip (4.0);
        for (int i = 0; i < 12; ++i)
        {
            auto n = model::mkNote (60.0 + (double) (i % 5), (double) i, 0.9, "ns-" + std::to_string (i));
            auto& s = n.sample;
            if (i % 3 == 1)
                s.engine = core::Engine::cloud;
            else if (i % 3 == 2)
                s.engine = core::Engine::tape;
            if (i % 4 == 1)
                s.lock = true;
            else if (i % 4 == 3)
                s.lock = false;
            if (i % 5 != 0)
                s.motion = (core::Motion) (i % 6);
            s.travel = i % 4 == 2 ? model::TravelKind::path : model::TravelKind::speed;
            s.start.mode = (model::StartMode) (i % 3 == 0 ? 0 : i % 7 == 1 ? 3 : i % 2 == 0 ? 1 : 2);
            s.start.pos = i % 2 == 0 ? 0.25 : 0.5;
            s.start.range = i % 3 == 0 ? 0.25 : 0.6;
            if (i % 3 == 1)
                s.tape = i % 2 == 0;
            if (i % 4 == 0)
                s.rev = true;
            if (i % 6 == 5)
                s.jump = core::RateId::r1_16;
            else if (i % 6 == 2)
                s.jump = core::RateId::r1_8T;
            s.restart = i % 5 == 3;
            n.vel = i % 4 == 0 ? 0.8 : 0.5;
            c.notes.push_back (std::make_shared<const model::Note> (std::move (n)));
        }
        return c;
    }

    /** An editor on a processor holding a clip, with the Note settings tab open. */
    struct NsRig
    {
        StrandLockProcessor proc;
        std::unique_ptr<StrandLockEditor> editor;

        explicit NsRig (const model::Clip& clip, ThemeId theme = ThemeId::dark, int width = 1100, int height = 900)
        {
            proc.waitForSounds();
            auto& s = proc.getUserSettings();
            s.setTheme (theme);
            s.setUiScalePercent (100);
            s.setKeysDefault (false);
            editor = std::make_unique<StrandLockEditor> (proc);
            editor->setLogicalSize (width, height);
            view().getRoll().setMenusShown (false);
            proc.editClip ("Start", clip);
            view().openTab (MainView::noteTab);
            select ({}, {});
        }

        ~NsRig()
        {
            editor.reset();
            resetUserSettings (proc);
        }

        NsRig (const NsRig&) = delete;
        NsRig& operator= (const NsRig&) = delete;

        MainView& view() { return editor->getView(); }
        NoteSettingsTab& tab() { return view().getNoteSettingsTab(); }

        void select (const std::vector<std::string>& ids, const std::string& primary)
        {
            proc.getDocument().setSelection (ids, primary);
            view().getRoll().edited();
            editor->refreshNow();
        }

        model::ClipPtr clip() { return proc.getDocument().current(); }
        Bytes bytes() { return model::encodeClip (*clip()); }

        /** The selection in time order (what the tab acts on). */
        std::vector<std::string> ids() { return view().getRoll().getTools().ids(); }
    };

    /** A small seeded generator (the same numbers on every platform). */
    struct Lcg
    {
        std::uint32_t s;
        std::uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
        int below (int n) { return (int) (next() % (std::uint32_t) n); }
    };

    /** What a segment should light for a field: -1 mixed, 0 Follow (null), else 1 + the value. */
    template <class T>
    int litFor (const model::Mixed<std::optional<T>>& m)
    {
        return m.mixed ? -1 : m.value ? 1 + (int) *m.value : 0;
    }

    int litFor (const model::Mixed<std::optional<bool>>& m)
    {
        return m.mixed ? -1 : ! m.value ? 0 : *m.value ? 1 : 2;
    }
}

//==================================================================================================
SL_TEST ("ui", noteSettingsShowsMixed)
{
    section ("Note settings: \"mixed\" shows exactly when the selected notes differ, every group against model::getMixed (M6)");

    const auto clip = settingsClip();
    NsRig rig (clip);
    auto& tab = rig.tab();
    check (tab.isAsleep() && tab.getHeadText().startsWith ("Select notes on the roll"),
           fmt ("nothing selected: the tab says how to use it and its controls sleep (\"%s\")", tab.getHeadText().substring (0, 40).toRawUTF8()));

    Lcg rng { 7u };
    int runs = 0, right = 0;
    int mixedSeen[NoteSettingsTab::numGroups] = {}, sharedSeen[NoteSettingsTab::numGroups] = {};
    std::string firstBad;
    for (int run = 0; run < 240; ++run)
    {
        // 1 to 6 notes of the twelve
        std::vector<std::string> ids;
        const int k = 1 + rng.below (6);
        while ((int) ids.size() < k)
        {
            const auto id = "ns-" + std::to_string (rng.below (12));
            if (std::find (ids.begin(), ids.end(), id) == ids.end())
                ids.push_back (id);
        }
        rig.select (ids, ids.front());
        const auto mx = model::getMixed (*rig.clip(), ids);
        if (! mx)
            continue;
        ++runs;

        struct Want
        {
            Group g;
            bool mixed;
            int lit;
        };
        const Want segs[] = {
            { Group::engine, mx->engine.mixed, litFor (mx->engine) },
            { Group::lock, mx->lock.mixed, litFor (mx->lock) },
            { Group::motion, mx->motion.mixed, litFor (mx->motion) },
            { Group::travel, mx->travel.mixed, mx->travel.mixed ? -1 : (int) mx->travel.value },
            { Group::start, mx->startMode.mixed, mx->startMode.mixed ? -1 : (int) mx->startMode.value },
            { Group::tape, mx->tape.mixed, litFor (mx->tape) },
            { Group::rev, mx->rev.mixed, litFor (mx->rev) },
        };
        bool ok = ! tab.isAsleep();
        for (const auto& w : segs)
        {
            const bool tagRight = tab.isMixed (w.g) == w.mixed;
            const bool litRight = tab.getSegments (w.g).getSelected() == w.lit;
            ok = ok && tagRight && litRight;
            (w.mixed ? mixedSeen : sharedSeen)[(int) w.g] += 1;
            if (! (tagRight && litRight) && firstBad.empty())
                firstBad = fmt ("run %d group %d: tag %d (want %d), lit %d (want %d)", run, (int) w.g, (int) tab.isMixed (w.g), (int) w.mixed,
                                tab.getSegments (w.g).getSelected(), w.lit);
        }
        // Jump every: the list's "mixed", or the shared rate (Follow: 1)
        const int jumpWant = mx->jump.mixed ? NoteSettingsTab::jumpMixedId : mx->jump.value ? 2 + (int) *mx->jump.value : 1;
        const bool jumpRight = tab.isMixed (Group::jump) == mx->jump.mixed && tab.getJump().getSelectedId() == jumpWant;
        (mx->jump.mixed ? mixedSeen : sharedSeen)[(int) Group::jump] += 1;
        // Restart sample here: lit when every note restarts, "(some)" when some do
        const bool restartRight = tab.getRestart().getToggleState() == (! mx->restart.mixed && mx->restart.value)
                               && tab.getRestart().getButtonText().endsWith ("(some)") == mx->restart.mixed;
        (mx->restart.mixed ? mixedSeen : sharedSeen)[(int) Group::restart] += 1;
        // the sliders read "mixed" while their value differs
        const bool slidersRight = (tab.getSpot().getValueText() == "mixed") == mx->startPos.mixed
                               && (tab.getRange().getValueText() == "mixed") == mx->startRange.mixed
                               && (tab.getVelocity().getValueText() == "mixed") == mx->vel.mixed;
        (mx->vel.mixed ? mixedSeen : sharedSeen)[(int) Group::velocity] += 1;
        ok = ok && jumpRight && restartRight && slidersRight;
        if (! ok && firstBad.empty())
            firstBad = fmt ("run %d: jump %d restart %d sliders %d", run, (int) jumpRight, (int) restartRight, (int) slidersRight);
        right += ok ? 1 : 0;
    }
    check (runs == 240 && right == runs, fmt ("%d of %d selections show \"mixed\" exactly where model::getMixed says the notes differ, and the "
                                              "shared value lit elsewhere%s%s", right, runs, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    std::string seen;
    for (int g = 0; g < NoteSettingsTab::numGroups; ++g)
        seen += fmt ("%s%d/%d", g == 0 ? "" : " ", mixedSeen[g], sharedSeen[g]);
    note ("mixed / shared selections per group (engine lock motion travel start tape rev jump restart velocity): " + seen);
    int groupsBoth = 0;
    for (int g = 0; g < NoteSettingsTab::numGroups; ++g)
        groupsBoth += mixedSeen[g] > 0 && sharedSeen[g] > 0 ? 1 : 0;
    check (groupsBoth == NoteSettingsTab::numGroups, fmt ("every group was seen both mixed and shared (%d of %d)", groupsBoth, NoteSettingsTab::numGroups));

    // the first line (every note of this clip has a setting of its own; Mirror's test sees notes that follow the tab)
    rig.select ({ "ns-4" }, "ns-4");
    const auto head1 = rig.tab().getHeadText();
    rig.select ({ "ns-1", "ns-2", "ns-4" }, "ns-1");
    const auto head2 = rig.tab().getHeadText();
    const juce::String dot = juce::String (" ") + juce::String::charToString ((juce::juce_wchar) 0x00b7) + " ";
    check (head1 == "1 note selected" + dot + "1 has its own settings (a dot on the roll marks them)"
               && head2 == "3 notes selected" + dot + "3 have their own settings (a dot on the roll marks them)",
           fmt ("the first line: \"%s\" and \"%s\"", head1.toRawUTF8(), head2.toRawUTF8()));
    rig.select ({}, {});
    check (rig.tab().isAsleep() && ! rig.tab().getClear().isEnabled(), "nothing selected again: asleep, Follow the Sample tab for all greyed");
}

//==================================================================================================
SL_TEST ("ui", noteSettingsOneUndoStepEach)
{
    section ("Note settings: every change is one model call (the bytes of model::setNoteSample) and one undo step with the prototype's label (M6)");

    const auto clip = settingsClip();
    NsRig rig (clip);
    auto& tab = rig.tab();
    const std::vector<std::string> sel { "ns-3", "ns-4", "ns-5" };   // every field mixed or partly set: each click changes something
    rig.select (sel, "ns-4");

    int runs = 0, right = 0;
    std::string firstBad;
    // One action through the tab against the direct model call; then Undo, so each starts from the same clip.
    const auto expect = [&] (const std::string& what, const std::function<void()>& act, const std::function<model::Clip (const model::Clip&)>& direct,
                             const std::string& label)
    {
        ++runs;
        const auto before = rig.clip();
        const auto bytes0 = model::encodeClip (*before);
        const auto label0 = rig.proc.undoLabel();
        act();
        const auto want = model::encodeClip (direct (*before));
        const bool same = rig.bytes() == want, labelled = rig.proc.undoLabel().value_or ("") == label;
        rig.proc.undo();
        rig.view().getRoll().edited();
        rig.editor->refreshNow();
        const bool back = rig.bytes() == bytes0 && rig.proc.undoLabel() == label0;
        if (same && labelled && back)
            ++right;
        else if (firstBad.empty())
            firstBad = fmt ("%s: bytes %s, label \"%s\" (want \"%s\"), one Undo back %s", what.c_str(), same ? "right" : "WRONG",
                            rig.proc.undoLabel().value_or ("").c_str(), label.c_str(), back ? "yes" : "NO");
    };
    const auto ids = rig.ids();
    const auto patched = [ids] (const model::SamplePatch& p) { return [ids, p] (const model::Clip& c) { return model::setNoteSample (c, ids, p); }; };
    const char* const engineWords[] = { "follow", "Tape", "Voice", "Cloud" };
    for (int i = 0; i < 4; ++i)
    {
        model::SamplePatch p;
        if (i == 0)
            p.engine.emplace();
        else
            p.engine.emplace ((core::Engine) (i - 1));
        expect (fmt ("Engine %d", i), [&tab, i] { tab.getSegments (Group::engine).getButton (i).onClick(); }, patched (p),
                std::string ("Note engine: ") + engineWords[i]);
    }
    const char* const triWords[] = { "follow", "on", "off" };
    for (int i = 0; i < 3; ++i)
    {
        model::SamplePatch lockP, tapeP, revP;
        const auto tri = [i] (model::Choice<bool>& c) { if (i == 0) c.emplace(); else c.emplace (i == 1); };
        tri (lockP.lock);
        tri (tapeP.tape);
        tri (revP.rev);
        expect (fmt ("Lock %d", i), [&tab, i] { tab.getSegments (Group::lock).getButton (i).onClick(); }, patched (lockP), std::string ("Note lock: ") + triWords[i]);
        expect (fmt ("Tape %d", i), [&tab, i] { tab.getSegments (Group::tape).getButton (i).onClick(); }, patched (tapeP), std::string ("Note tape: ") + triWords[i]);
        expect (fmt ("Rev %d", i), [&tab, i] { tab.getSegments (Group::rev).getButton (i).onClick(); }, patched (revP),
                std::string ("Each grain backwards: ") + triWords[i]);
    }
    const char* const motionWords[] = { "follow", "Forward", "Backward", "Bounce", "Freeze", "Scatter" };
    const int motionButtons[] = { 0, 1, 2, 3, 5, 6 };   // Mirror (4) has its own test below
    for (int j = 0; j < 6; ++j)
    {
        const int i = motionButtons[j];
        model::SamplePatch p;
        if (i == 0)
            p.motion.emplace();
        else
            p.motion.emplace ((core::Motion) (i - 1));
        expect (fmt ("Motion %d", i), [&tab, i] { tab.getSegments (Group::motion).getButton (i).onClick(); }, patched (p),
                std::string ("Note motion: ") + motionWords[j]);
    }
    const char* const startWords[] = { "a spot in the part", "carry on", "with the song", "random" };
    for (int i = 0; i < 4; ++i)
    {
        model::SamplePatch p;
        model::StartPatch sp;
        sp.mode = (model::StartMode) i;
        p.start = sp;
        expect (fmt ("Start %d", i), [&tab, i] { tab.getSegments (Group::start).getButton (i).onClick(); }, patched (p),
                std::string ("Start: ") + startWords[i]);
    }
    {
        model::SamplePatch p;
        p.jump.emplace (core::RateId::r1_16T);
        expect ("Jump every 1/16 triplet", [&tab] { tab.getJump().setSelectedId (2 + (int) core::RateId::r1_16T, juce::sendNotificationSync); }, patched (p),
                "Jump every 1/16 triplet");
        model::SamplePatch f;
        f.jump.emplace();
        // the list shows "mixed" for this selection, so Follow is a change
        expect ("Jump every Follow", [&tab] { tab.getJump().setSelectedId (1, juce::sendNotificationSync); }, patched (f), "Jump every : follow");
    }
    {
        // Restart sample here: some of the notes restart, so the click makes every one restart
        model::SamplePatch p;
        p.restart = true;
        expect ("Restart", [&tab] { tab.getRestart().onClick(); }, patched (p), "Restart sample here: on");
        model::SamplePatch c;
        c.engine.emplace();
        c.lock.emplace();
        c.motion.emplace();
        c.tape.emplace();
        c.rev.emplace();
        c.jump.emplace();
        expect ("Follow the Sample tab for all", [&tab] { tab.getClear().onClick(); }, patched (c), "Note settings: follow the Sample tab");
    }
    {
        // Re-roll: every selected note with a Random start gets the next seed
        rig.select ({ "ns-8", "ns-1" }, "ns-1");   // both start at random
        const auto ids2 = rig.ids();
        expect ("Re-roll", [&tab] { tab.getReroll().onClick(); },
                [ids2] (const model::Clip& c)
                {
                    return model::editNotes (c, ids2, [] (model::Note& n, const model::Note&)
                    {
                        if (n.sample.start.mode == model::StartMode::random)
                            n.sample.start.seed = n.sample.start.seed + 1;
                    });
                },
                "Re-roll random start");
    }
    check (runs > 0 && right == runs, fmt ("%d of %d clicks gave the bytes of the direct model call, one undo step with the prototype's label, "
                                           "and one Undo back%s%s", right, runs, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));

    // The sliders: each move changes the notes with no undo step; the release is one step.
    {
        rig.select (sel, "ns-4");
        auto& vel = tab.getVelocity();
        const auto ids3 = rig.ids();
        const auto before = rig.clip();
        const auto label0 = rig.proc.undoLabel();
        int moves = 0, live = 0;
        double last = 0.0;
        for (const float x : { 90.0f, 120.0f, 150.0f })   // the slider's own pixels: past its caption, along its track
        {
            vel.dragTo (x);
            ++moves;
            model::SamplePatch p;
            p.vel = vel.getValue() / 127.0;
            live += rig.bytes() == model::encodeClip (model::setNoteSample (*before, ids3, p)) && rig.proc.undoLabel() == label0 ? 1 : 0;
            last = vel.getValue();
        }
        vel.release();
        model::SamplePatch p;
        p.vel = last / 127.0;
        const bool released = rig.bytes() == model::encodeClip (model::setNoteSample (*before, ids3, p)) && rig.proc.undoLabel().value_or ("") == "Velocity";
        rig.proc.undo();
        const bool back = model::encodeClip (*rig.clip()) == model::encodeClip (*before);
        check (live == moves && released && back,
               fmt ("Velocity: %d of %d moves changed the notes live with no undo step; the release is one step \"Velocity\" (velocity %g); "
                    "one Undo back: %s", live, moves, last, back ? "yes" : "no"));
    }
}

//==================================================================================================
SL_TEST ("ui", noteSettingsMirrorAndPath)
{
    section ("Note settings: Mirror on one-head notes also sets 2 heads in that step; Path on a flat Travel line plays through the Region (M6)");

    model::Clip c = model::newClip (4.0);
    for (int i = 0; i < 4; ++i)
        c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60.0 + i, (double) i, 1.0, "m" + std::to_string (i))));
    {
        // m3 already reaches 3 heads on its own Heads line: Mirror leaves it alone
        auto n = model::cloneNote (*c.notes[3]);
        n.lane (core::LaneId::heads) = model::flat (0.65);
        c.notes[3] = std::make_shared<const model::Note> (std::move (n));
    }
    NsRig rig (c);
    auto& tab = rig.tab();
    rig.select ({ "m0", "m1", "m2", "m3" }, "m0");
    const juce::String dot = juce::String (" ") + juce::String::charToString ((juce::juce_wchar) 0x00b7) + " ";
    check (tab.getHeadText() == "4 notes selected" + dot + "following the Sample tab" && ! tab.getClear().isEnabled(),
           fmt ("notes with no settings of their own: \"%s\", Follow the Sample tab for all greyed", tab.getHeadText().toRawUTF8()));
    const auto before = rig.clip();
    tab.getSegments (Group::motion).getButton (1 + (int) core::Motion::mirror).onClick();
    const auto after = rig.clip();
    int twoHeads = 0, left = 0;
    for (const auto& n : after->notes)
    {
        const auto& line = n->lane (core::LaneId::heads);
        const bool two = line.size() == 2 && juce::exactlyEqual (line[0].v, 0.5 + 1.0 / 14.0) && juce::exactlyEqual (line[1].v, 0.5 + 1.0 / 14.0);
        if (n->id == "m3")
            left += juce::exactlyEqual (line[0].v, 0.65) ? 1 : 0;
        else
            twoHeads += two ? 1 : 0;
    }
    check (twoHeads == 3 && left == 1 && rig.proc.undoLabel().value_or ("") == "Note motion: Mirror"
               && rig.view().getToastText() == "Mirror needs 2 heads: Heads set to 2 on 3 notes",
           fmt ("Mirror: 3 one-head notes got a flat Heads line at 2 heads, the one with 3 heads kept its line; one step \"%s\"; \"%s\"",
                rig.proc.undoLabel().value_or ("").c_str(), rig.view().getToastText().toRawUTF8()));
    rig.proc.undo();
    rig.view().getRoll().edited();
    check (model::encodeClip (*rig.clip()) == model::encodeClip (*before), "one Undo takes the Motion and the Heads lines back together");

    // Path: the flat Travel lines become Fit (1x through the Region); the toast says so
    rig.editor->refreshNow();
    const auto ids = rig.ids();
    const auto clip0 = rig.clip();
    tab.getSegments (Group::travel).getButton (1).onClick();
    model::SamplePatch p;
    p.travel = model::TravelKind::path;
    auto want = model::setNoteSample (*clip0, ids, p);
    want = *model::applyShape (want, ids, core::LaneId::travel, model::ShapeKey::fit, model::ShapeScope::each,
                               rig.view().getRoll().getTools().shapeOpts());
    check (rig.bytes() == model::encodeClip (want) && rig.proc.undoLabel().value_or ("") == "Travel: Path"
               && rig.view().getToastText()
                      == "Path: the Travel line now plays through the Region at 1x (Fit). Flat = hold still, a step = jump.",
           fmt ("Path: the bytes of setNoteSample then Fit on the flat lines, one step \"%s\", \"%s\"", rig.proc.undoLabel().value_or ("").c_str(),
                rig.view().getToastText().toRawUTF8()));
}

//==================================================================================================
SL_TEST ("ui", noteSettingsFadesSayWhy)
{
    section ("Note settings: a group that does nothing for the selected notes is faded and says why (M6)");

    const auto clip = settingsClip();
    NsRig rig (clip);
    auto& tab = rig.tab();

    // ns-2, ns-5, ns-8: Tape notes (their own engine), no Scatter among them
    rig.select ({ "ns-2", "ns-5", "ns-8" }, "ns-2");
    const bool tapeFaded = tab.isFaded (Group::tape) && tab.isFaded (Group::rev) && tab.isFaded (Group::lock) && tab.isFaded (Group::jump);
    const bool reasons = tab.getFadeReason (Group::tape) == "The Tape engine always ties pitch to speed"
                      && tab.getFadeReason (Group::rev) == "The Tape engine has no grains"
                      && tab.getFadeReason (Group::lock) == "Lock only works in the Cloud engine"
                      && tab.getFadeReason (Group::jump) == "Jump every only matters with Motion Scatter";
    const auto& freeze = tab.getSegments (Group::motion).getButton (1 + (int) core::Motion::freeze);
    const auto& path = tab.getSegments (Group::travel).getButton (1);
    check (tapeFaded && reasons && freeze.getAlpha() < 0.6f && tab.titleOf (freeze) == "Tape cannot freeze: it plays Forward"
               && path.getAlpha() < 0.6f && tab.titleOf (path) == "Tape plays Path as Speed at 1x",
           "Tape notes: Tape, Each grain backwards, Lock (no Cloud) and Jump every (no Scatter) faded with the prototype's reasons; "
           "Freeze and Path faded and say why");

    // ns-1 (Cloud, Backward) and ns-11 (Tape, Scatter): not all Tape, a Cloud note, a Scatter note
    rig.select ({ "ns-1", "ns-11" }, "ns-1");
    const bool open = ! tab.isFaded (Group::lock) && ! tab.isFaded (Group::jump) && ! tab.isFaded (Group::tape) && ! tab.isFaded (Group::rev)
                   && tab.getSegments (Group::motion).getButton (1 + (int) core::Motion::freeze).getAlpha() > 0.99f;
    check (open, "a Cloud note and a Tape note on Scatter: Lock, Jump every, Tape and Each grain backwards are not faded, and Freeze is plain");
}

//==================================================================================================
// Pictures for review: the tab with a mixed selection (dark) and with one note (light).

namespace
{
    juce::Image shootNs (ThemeId theme, bool mixed)
    {
        juce::Image image;
        {
            NsRig rig (settingsClip(), theme, 1100, 900);
            if (mixed)
                rig.select ({ "ns-3", "ns-4", "ns-5" }, "ns-4");
            else
                rig.select ({ "ns-8" }, "ns-8");
            image = rig.editor->createComponentSnapshot (rig.editor->getLocalBounds(), true, 1.0f);
        }
        return image;
    }
}

SL_SNAPSHOT (m6_note_settings_dark)  { return shootNs (ThemeId::dark, true); }
SL_SNAPSHOT (m6_note_settings_light) { return shootNs (ThemeId::light, false); }
