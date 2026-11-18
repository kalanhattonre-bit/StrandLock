// The live roll (PLUGIN-PLAN 4 M4, 7.6 R3): a real editor driven through the roll canvas's own mouse handlers, as
// Windows delivers a mouse to it. A double-click on empty space makes a note, a drag moves it, its end dot makes it
// longer; in Pitch mode a double-click on the line adds a node, a drag moves it (the readout and the inspector say
// where), the diamond bends a segment, a double-click on the node deletes it; Freehand paints Volume. Every finished
// gesture is one undo step and a drag sends its steps to the engine without one: walking back with the Undo button
// lands on the clip before each gesture, and Redo walks forward again. Also: the right-click menu, the selection
// row and the roll's switches, the keys (only with Keys on), the inspector's typed values (T6), the hint bar's texts
// for every mode (sl-ui.js modeHint), and pictures of the live roll for review.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "model/Clip.h"
#include "model/History.h"
#include "model/NoteSettings.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/HintBar.h"
#include "ui/Theme.h"
#include "ui/roll/InspectorText.h"
#include "ui/roll/RollView.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::ThemeId;
    using ui::roll::MenuItem;
    using ui::roll::RollView;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    /** The mouse on the roll's canvas, in roll pixels, with the event times a test chooses. */
    struct Pointer
    {
        juce::Component& canvas;
        const ui::roll::RollGeometry& geo;

        juce::Point<float> at (double rollX, double rollY) const
        {
            const auto v = geo.toView (rollX, rollY);
            return { (float) v.x, (float) v.y };
        }

        juce::MouseEvent event (juce::Point<float> p, juce::ModifierKeys mods, double timeMs) const
        {
            const auto time = juce::Time ((juce::int64) timeMs);
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, juce::MouseInputSource::defaultPressure,
                                     juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                     juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &canvas, &canvas, time, p, time, 1, false);
        }

        void down (double x, double y, double t, int mods = 0) const
        {
            canvas.mouseDown (event (at (x, y), juce::ModifierKeys (mods | juce::ModifierKeys::leftButtonModifier), t));
        }

        void drag (double x, double y, double t, int mods = 0) const
        {
            canvas.mouseDrag (event (at (x, y), juce::ModifierKeys (mods | juce::ModifierKeys::leftButtonModifier), t));
        }

        void up (double x, double y, double t) const { canvas.mouseUp (event (at (x, y), juce::ModifierKeys(), t)); }

        void click (double x, double y, double t, int mods = 0) const
        {
            down (x, y, t, mods);
            up (x, y, t + 40.0);
        }

        void rightClick (double x, double y, double t) const
        {
            canvas.mouseDown (event (at (x, y), juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), t));
            canvas.mouseUp (event (at (x, y), juce::ModifierKeys(), t + 40.0));
        }
    };

    std::string label (const StrandLockProcessor& p)
    {
        return p.undoLabel().value_or ("(none)");
    }

    model::NotePtr noteIn (const model::ClipPtr& c, const std::string& id)
    {
        for (const auto& n : c->notes)
            if (n->id == id)
                return n;
        return nullptr;
    }

    bool near (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

    std::string str (const juce::String& s) { return s.toStdString(); }

    /** A clip of the notes given as (id, pitch, start, length). */
    model::Clip clipOf (std::initializer_list<std::tuple<const char*, double, double, double>> notes)
    {
        model::Clip c = model::newClip (4.0);
        for (const auto& [id, p, s, l] : notes)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (p, s, l, id)));
        return c;
    }

    /** A processor and an editor at 1100 x 680, holding clip (one undo step called "Start"), the Grid at 1/16, no
        menu windows, and the rows around pitch in view. */
    struct LiveRig
    {
        StrandLockProcessor proc;
        std::unique_ptr<StrandLockEditor> editor;

        explicit LiveRig (const model::Clip& clip, double pitch = 62.0)
        {
            resetUserSettings (proc);
            editor = std::make_unique<StrandLockEditor> (proc);
            proc.editClip ("Start", clip);
            roll().setMenusShown (false);
            roll().setGrid (0.25);
            roll().setScroll (0.0, geo().cy (pitch) - geo().viewHeight() / 2.0);
            editor->refreshNow();
        }

        ~LiveRig() { resetUserSettings (proc); }

        ui::MainView& view() { return editor->getView(); }
        RollView& roll() { return editor->getView().getRoll(); }
        const ui::roll::RollGeometry& geo() { return roll().getGeometry(); }
        doc::ClipDocument& doc() { return proc.getDocument(); }
        Pointer mouse() { return Pointer { roll().getCanvas(), roll().getGeometry() }; }
    };
}

//==================================================================================================
SL_TEST ("ui", rollLiveGestures)
{
    section ("Live roll: create, move, resize, node add / drag / bend / delete and Freehand through the canvas's mouse "
             "handlers; one undo step each; Undo and Redo walk the history");

    LiveRig rig (model::newClip (4.0));
    auto& view = rig.view();
    auto& doc = rig.doc();
    const auto& geo = rig.geo();
    const auto mouse = rig.mouse();
    const double bw = geo.beatWidth(), rh = geo.rowHeight();

    std::vector<model::ClipPtr> clips { doc.current() };   // the clip after each gesture, from the start
    std::vector<std::string> labels { label (rig.proc) };
    const auto stepDone = [&] { clips.push_back (doc.current()); labels.push_back (label (rig.proc)); };

    // 1. a double-click on empty space (two presses 100 ms apart): one note at the grid step, max (1 beat, grid) long
    const double x1 = 1.05 * bw, y1 = geo.cy (60.0);
    mouse.click (x1, y1, 10000.0);
    const bool firstClickNothing = doc.current()->notes.empty() && label (rig.proc) == "Start";
    mouse.click (x1, y1, 10100.0);
    const auto made = doc.current();
    const auto n1 = made->notes.empty() ? nullptr : made->notes.front();
    const std::string id = n1 != nullptr ? n1->id : std::string();
    check (firstClickNothing && made->notes.size() == 1 && n1 != nullptr && juce::exactlyEqual (n1->p, 60.0) && juce::exactlyEqual (n1->s, 1.0)
               && juce::exactlyEqual (n1->l, 1.0) && label (rig.proc) == "Add note",
           fmt ("a double-click on empty space at beat 1.05, C3 makes one note: p %.0f, start %.3f, length %.3f (undo step \"%s\"; the first "
                "click alone made nothing)", n1 ? n1->p : -1.0, n1 ? n1->s : -1.0, n1 ? n1->l : -1.0, label (rig.proc).c_str()));
    stepDone();
    auto& insp = view.getInspector();
    check (view.getSelectionText() == "1 note selected" && view.getDuplicateButton().isEnabled() && insp.showsNote()
               && insp.getStart().getValue() == "1.2.1" && insp.getLength().getValue() == "1" && insp.getPitch().getValue() == "C3"
               && insp.getVelocity().getValue() == "102",
           fmt ("the new note is selected (\"%s\") and the inspector shows Start %s, Length %s, Pitch %s, Velocity %s",
                str (view.getSelectionText()).c_str(), str (insp.getStart().getValue()).c_str(), str (insp.getLength().getValue()).c_str(),
                str (insp.getPitch().getValue()).c_str(), str (insp.getVelocity().getValue()).c_str()));

    // 2. a drag from the middle of the note, 2 beats later and 3 rows up: it moves with the grid; the drag's steps
    // reach the document (and the engine) with no undo step until the release
    {
        const auto n = noteIn (doc.current(), id);
        const double x = (n->s + n->l / 2.0) * bw, y = geo.baseYAt (*n, 0.5);
        mouse.down (x, y, 12000.0);
        mouse.drag (x + 1.0 * bw, y - 1.0 * rh, 12050.0);
        const auto mid = noteIn (doc.current(), id);
        const bool liveNoStep = mid != nullptr && juce::exactlyEqual (mid->s, 2.0) && juce::exactlyEqual (mid->p, 61.0) && label (rig.proc) == "Add note";
        const bool readout = rig.roll().getGestures().readout().has_value();
        mouse.drag (x + 2.0 * bw, y - 3.0 * rh, 12100.0);
        mouse.up (x + 2.0 * bw, y - 3.0 * rh, 12150.0);
        const auto moved = noteIn (doc.current(), id);
        check (liveNoStep && readout && moved != nullptr && juce::exactlyEqual (moved->s, 3.0) && juce::exactlyEqual (moved->p, 63.0)
                   && label (rig.proc) == "Move notes" && ! rig.roll().getGestures().readout().has_value(),
               fmt ("a drag 2 beats later and 3 rows up moves the note to beat %.2f, D#3 (%.0f), \"%s\"; mid-drag it was already at beat 2 "
                    "with no new undo step (%s) and a readout showed", moved ? moved->s : -1.0, moved ? moved->p : -1.0,
                    label (rig.proc).c_str(), liveNoStep ? "yes" : "NO"));
        stepDone();
    }

    // 3. its end dot dragged a beat to the right: a beat longer
    {
        const auto n = noteIn (doc.current(), id);
        const auto dot = geo.endDot (*n);
        mouse.down (dot.x, dot.y, 14000.0);
        mouse.drag (dot.x + 0.5 * bw, dot.y, 14050.0);
        mouse.drag (dot.x + bw, dot.y, 14100.0);
        mouse.up (dot.x + bw, dot.y, 14150.0);
        const auto longer = noteIn (doc.current(), id);
        check (longer != nullptr && juce::exactlyEqual (longer->l, 2.0) && juce::exactlyEqual (longer->s, 3.0) && juce::exactlyEqual (longer->p, 63.0)
                   && label (rig.proc) == "Change length" && insp.getLength().getValue() == "2",
               fmt ("dragging the end dot a beat right makes it %.3f beats long (\"%s\"; the inspector says %s)", longer ? longer->l : -1.0,
                    label (rig.proc).c_str(), str (insp.getLength().getValue()).c_str()));
        stepDone();
    }

    // 4. Pitch mode: a double-click on the line, a quarter into the note, adds a node there (on the line: +0)
    view.getModeButton (2).onClick();
    {
        const auto n = noteIn (doc.current(), id);
        const double x = (n->s + 0.25 * n->l) * bw, y = geo.baseYAt (*n, 0.25);
        mouse.click (x, y, 16000.0);
        const bool firstNothing = label (rig.proc) == "Change length";
        mouse.click (x, y, 16100.0);
        const auto withNode = noteIn (doc.current(), id);
        const auto& nodes = model::noteLane (*withNode, core::LaneId::pitch);
        const auto st = doc.rollState();
        check (firstNothing && nodes.size() == 3 && near (nodes[1].t, 0.25, 1e-9) && near (nodes[1].v, 0.5, 1e-9) && label (rig.proc) == "Add node"
                   && st.activeNode.has_value() && st.activeNode->k == 1 && insp.showsNode() && insp.getNodeTime().getValue() == "0.5"
                   && insp.getNodeCaption() == "Node (Pitch)",
               fmt ("Pitch mode: a double-click on the line adds node 1 at t %.3f, v %.3f (\"%s\"); the inspector shows %s at %s beats in, %s",
                    nodes.size() > 1 ? nodes[1].t : -1.0, nodes.size() > 1 ? nodes[1].v : -1.0, label (rig.proc).c_str(),
                    str (insp.getNodeCaption()).c_str(), str (insp.getNodeTime().getValue()).c_str(), str (insp.getNodeValue().getValue()).c_str()));
        stepDone();
    }

    // 5. the node dragged 2 rows up: it snaps to whole semitones (+2); the readout and the inspector say so live
    {
        const auto n = noteIn (doc.current(), id);
        const auto p = geo.nodeXY (*n, core::LaneId::pitch, 1);
        mouse.down (p.x, p.y, 18000.0);
        mouse.drag (p.x, p.y - 1.2 * rh, 18050.0);
        mouse.drag (p.x, p.y - 2.1 * rh, 18100.0);
        const auto shown = rig.roll().getGestures().readout();
        const auto liveValue = insp.getNodeValue().getValue();
        mouse.up (p.x, p.y - 2.1 * rh, 18150.0);
        const auto after = noteIn (doc.current(), id);
        const auto& nodes = model::noteLane (*after, core::LaneId::pitch);
        check (nodes.size() == 3 && near (nodes[1].v, 0.5 + 2.0 / 24.0, 1e-12) && near (nodes[1].t, 0.25, 1e-9) && label (rig.proc) == "Move node"
                   && shown.has_value() && juce::String (shown->text).contains ("2 semitones") && liveValue.contains ("2 semitones"),
               fmt ("dragging the node up 2.1 rows lands on +2 semitones (v %.6f, \"%s\"); the readout said \"%s\", the inspector \"%s\"",
                    nodes.size() > 1 ? nodes[1].v : -1.0, label (rig.proc).c_str(), shown ? shown->text.c_str() : "(none)", str (liveValue).c_str()));
        stepDone();
    }

    // 6. the diamond of the segment before it dragged down 30 px: the segment bends (it fills in)
    {
        const auto n = noteIn (doc.current(), id);
        const auto d = geo.segXY (*n, core::LaneId::pitch, 0);
        bool bent = false;
        if (d)
        {
            mouse.down (d->x, d->y, 20000.0);
            mouse.drag (d->x, d->y + 15.0, 20050.0);
            mouse.drag (d->x, d->y + 30.0, 20100.0);
            mouse.up (d->x, d->y + 30.0, 20150.0);
            const auto& nodes = model::noteLane (*noteIn (doc.current(), id), core::LaneId::pitch);
            bent = nodes[0].shape == model::NodeShape::bend && near (nodes[0].bend, 0.5, 1e-12);
        }
        check (d.has_value() && bent && label (rig.proc) == "Bend segment",
               fmt ("dragging segment 0's diamond 30 px down bends it to 50 %% (\"%s\")", label (rig.proc).c_str()));
        stepDone();
    }

    // 7. a double-click on the node deletes it (the first click alone only picks it)
    {
        const auto n = noteIn (doc.current(), id);
        const auto p = geo.nodeXY (*n, core::LaneId::pitch, 1);
        mouse.click (p.x, p.y, 22000.0);
        const bool firstNothing = label (rig.proc) == "Bend segment";
        mouse.click (p.x, p.y, 22100.0);
        const auto& nodes = model::noteLane (*noteIn (doc.current(), id), core::LaneId::pitch);
        check (firstNothing && nodes.size() == 2 && label (rig.proc) == "Delete node",
               fmt ("a double-click on the node deletes it: %d nodes left (\"%s\")", (int) nodes.size(), label (rig.proc).c_str()));
        stepDone();
    }

    // 8. Volume with Freehand: a drag across the note paints the lane, which becomes smooth nodes on release
    view.getModeButton (3).onClick();
    view.getFreehandButton().onClick();
    {
        const auto n = noteIn (doc.current(), id);
        const bool lit = view.getFreehandButton().getToggleState() && view.getHintText().startsWith ("FREEHAND (Volume)");
        const double xa = n->s * bw, xb = (n->s + n->l) * bw;
        const auto yAt = [&] (double t, double v) { return geo.valueY (*n, core::LaneId::amp, t, v); };
        mouse.down (xa, yAt (0.0, 0.2), 24000.0);
        for (int i = 1; i <= 12; ++i)
        {
            const double t = (double) i / 12.0;
            mouse.drag (xa + (xb - xa) * t, yAt (t, 0.2 + 0.7 * t), 24000.0 + 10.0 * i);
        }
        mouse.up (xb, yAt (1.0, 0.9), 24200.0);
        const auto& amp = model::noteLane (*noteIn (doc.current(), id), core::LaneId::amp);
        bool smooth = amp.size() >= 2;
        for (const auto& q : amp)
            smooth = smooth && q.shape == model::NodeShape::smooth;
        const auto& pitchNodes = model::noteLane (*noteIn (doc.current(), id), core::LaneId::pitch);
        check (lit && smooth && near (amp.front().v, 0.2, 0.02) && near (amp.back().v, 0.9, 0.02) && amp.size() <= 6 && pitchNodes.size() == 2
                   && label (rig.proc) == "Draw lane by hand",
               fmt ("Freehand on Volume (lit, hint \"%s\"): a ramp painted from 0.2 to 0.9 becomes %d smooth nodes, %.3f .. %.3f (\"%s\"); Pitch "
                    "is left alone", str (view.getHintText().substring (0, 18)).c_str(), (int) amp.size(), amp.front().v, amp.back().v,
                    label (rig.proc).c_str()));
        stepDone();
    }
    view.getFreehandButton().onClick();

    // a click on the note that moves nothing adds no undo step: with a Redo waiting, a new step would clear it
    view.getUndoButton().onClick();
    {
        view.setMode (1);
        const auto n = noteIn (doc.current(), id);
        const double x = (n->s + n->l / 2.0) * bw, y = geo.baseYAt (*n, 0.5);
        const auto before = doc.current();
        mouse.click (x, y, 30000.0);
        check (rig.proc.canRedo() && doc.current() == before && label (rig.proc) == "Delete node",
               "a click on the note that moves nothing pushes no undo step (Redo is still there)");
        view.getRedoButton().onClick();
    }

    // Undo walks back through every gesture, each to the clip before it; Redo walks forward again
    const int steps = (int) clips.size() - 1;
    int backOk = 0, forwardOk = 0;
    std::string firstBad;
    for (int i = steps; i >= 1; --i)
    {
        const auto want = "Undo: " + labels[(std::size_t) i];
        view.getUndoButton().onClick();
        const bool ok = model::sameClip (*doc.current(), *clips[(std::size_t) i - 1]) && str (view.getToastText()) == want;
        backOk += ok ? 1 : 0;
        if (! ok && firstBad.empty())
            firstBad = fmt ("undo %d: toast \"%s\"", i, str (view.getToastText()).c_str());
    }
    const bool undoGreyedAtStart = label (rig.proc) == "Start";
    for (int i = 1; i <= steps; ++i)
    {
        view.getRedoButton().onClick();
        const bool ok = model::sameClip (*doc.current(), *clips[(std::size_t) i]) && str (view.getToastText()) == "Redo: " + labels[(std::size_t) i];
        forwardOk += ok ? 1 : 0;
        if (! ok && firstBad.empty())
            firstBad = fmt ("redo %d: toast \"%s\"", i, str (view.getToastText()).c_str());
    }
    check (steps == 8 && backOk == steps && forwardOk == steps && undoGreyedAtStart && ! view.getRedoButton().isEnabled()
               && view.getUndoButton().isEnabled(),
           fmt ("%d gestures, %d undo steps: Undo lands on the clip before each one (%d of %d), Redo on the clip after it (%d of %d)%s%s",
                steps, steps, backOk, steps, forwardOk, steps, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
}

SL_TEST ("ui", rollLiveSlideInto)
{
    section ("Live roll: Shift+click a second note = slide into it; the Slide into button waits for the next click");

    LiveRig rig (clipOf ({ { "a", 60.0, 0.0, 1.0 }, { "b", 62.0, 2.0, 1.0 }, { "c", 64.0, 4.0, 1.0 } }));
    auto& view = rig.view();
    auto& doc = rig.doc();
    const auto& geo = rig.geo();
    const auto mouse = rig.mouse();
    const auto mid = [&] (const char* id)
    {
        const auto n = noteIn (doc.current(), id);
        return std::pair<double, double> ((n->s + n->l / 2.0) * geo.beatWidth(), geo.baseYAt (*n, 0.5));
    };

    auto [ax, ay] = mid ("a");
    mouse.click (ax, ay, 10000.0);
    auto [bx, by] = mid ("b");
    mouse.click (bx, by, 11000.0, juce::ModifierKeys::shiftModifier);
    const auto a = noteIn (doc.current(), "a");
    check (a->glideTo == "b" && label (rig.proc) == "Slide into" && str (view.getToastText()) == "C3 now slides into D3"
               && doc.rollState().primary == "b",
           fmt ("click C3, Shift+click D3: C3 slides into D3 (glideTo \"%s\", \"%s\", toast \"%s\"); D3 is selected", a->glideTo.c_str(),
                label (rig.proc).c_str(), str (view.getToastText()).c_str()));

    // the button: armed (lit, the hint says so), then the next note clicked is the one it slides into
    view.getSlideIntoButton().onClick();
    const bool armed = view.getSlideIntoButton().getToggleState() && view.getHintText().startsWith ("SLIDE INTO");
    auto [cx, cyy] = mid ("c");
    mouse.click (cx, cyy, 12000.0);
    const auto b = noteIn (doc.current(), "b");
    check (armed && b->glideTo == "c" && ! view.getSlideIntoButton().getToggleState() && view.getHintText().startsWith ("BUILD (1)"),
           fmt ("Slide into... lights and the hint says SLIDE INTO; clicking E3 makes D3 slide into it (glideTo \"%s\") and lets go",
                b->glideTo.c_str()));
}

SL_TEST ("ui", rollLiveMenu)
{
    section ("Live roll: the right-click menu (a right press selects the note there); its items act as one undo step each");

    LiveRig rig (clipOf ({ { "a", 60.0, 0.0, 1.0 }, { "b", 62.0, 2.0, 1.0 } }));
    auto& view = rig.view();
    auto& roll = rig.roll();
    auto& doc = rig.doc();
    const auto& geo = rig.geo();
    const auto mouse = rig.mouse();
    const double bw = geo.beatWidth();

    const auto find = [&roll] (const std::string& text) -> std::optional<MenuItem>
    {
        for (const auto& m : roll.getMenu())
        {
            if (m.label == text)
                return m;
            for (const auto& r : m.row)
                if (r.label == text)
                    return r;
        }
        return std::nullopt;
    };

    // Build, on a note: it is selected, and the menu lists the Build items and the note's
    const auto b = noteIn (doc.current(), "b");
    mouse.rightClick ((b->s + 0.5) * bw, geo.baseYAt (*b, 0.5), 10000.0);
    const auto& menu = roll.getMenu();
    const bool built = ! menu.empty() && menu.front().kind == MenuItem::Kind::header && menu.front().label == "Build"
                       && find ("Slide into the next note I click") && find ("Remove slide links") && find ("Jump, no slide")
                       && find ("Copy lanes") && find ("Paste lanes") && find ("Paste lanes")->disabled && find ("Duplicate") && find ("Delete")
                       && ! find ("New note here");
    check (built && doc.rollState().primary == "b" && view.getSelectionText() == "1 note selected",
           fmt ("a right press on D3 selects it and opens the menu: %d items, Build's three, then Copy lanes, Paste lanes (greyed: nothing "
                "copied), Duplicate, Delete", (int) menu.size()));
    if (const auto del = find ("Delete"))
        roll.chooseMenuItem (*del);
    check (noteIn (doc.current(), "b") == nullptr && label (rig.proc) == "Delete note" && roll.getMenu().empty(),
           fmt ("Delete removes it, one undo step (\"%s\")", label (rig.proc).c_str()));

    // on empty space: New note here
    mouse.rightClick (6.3 * bw, geo.cy (65.0), 11000.0);
    const auto here = find ("New note here");
    if (here)
        roll.chooseMenuItem (*here);
    const auto c = doc.current();
    bool added = false;
    for (const auto& n : c->notes)
        added = added || (n->id != "a" && juce::exactlyEqual (n->s, 6.25) && juce::exactlyEqual (n->p, 65.0) && juce::exactlyEqual (n->l, 1.0));
    check (here.has_value() && added && label (rig.proc) == "Add note",
           fmt ("on empty space, New note here adds a beat-long note at the grid step (beat 6.25, F3): \"%s\"", label (rig.proc).c_str()));

    // Volume: the shapes for the lane, with Each note / Across; Across keeps the menu open, lit
    view.setMode (3);
    const auto a = noteIn (doc.current(), "a");
    mouse.rightClick ((a->s + 0.5) * bw, geo.baseYAt (*a, 0.5), 12000.0);
    const bool shapes = ! roll.getMenu().empty() && roll.getMenu().front().label == "Volume shape" && roll.getMenu().front().colour == core::LaneId::amp;
    const auto across = find ("Across");
    if (across)
        roll.chooseMenuItem (*across);
    const auto acrossNow = find ("Across"), eachNow = find ("Each note");
    const bool kept = acrossNow.has_value() && acrossNow->on && eachNow.has_value() && ! eachNow->on && rig.proc.getUiState().scope == 1;
    std::optional<MenuItem> shape;
    for (const auto& m : roll.getMenu())
        if (m.kind == MenuItem::Kind::item && m.action >= 0 && m.label != "Copy lanes" && m.label != "Paste lanes" && m.label != "Duplicate"
            && m.label != "Delete" && ! shape)
            shape = m;
    const auto ampBefore = model::noteLane (*noteIn (doc.current(), "a"), core::LaneId::amp);
    if (shape)
        roll.chooseMenuItem (*shape);
    const auto& ampAfter = model::noteLane (*noteIn (doc.current(), "a"), core::LaneId::amp);
    const bool changedLane = ampAfter.size() != ampBefore.size() || ! near (ampAfter.front().v, ampBefore.front().v, 1e-12)
                             || ! near (ampAfter.back().v, ampBefore.back().v, 1e-12);
    check (shapes && kept && shape.has_value() && changedLane && label (rig.proc) == shape->label + " (Volume)",
           fmt ("Volume mode: \"Volume shape\" in the lane's colour; Across stays open and lit (UIST scope %d); \"%s\" changes Volume (\"%s\")",
                (int) rig.proc.getUiState().scope, shape ? shape->label.c_str() : "(none)", label (rig.proc).c_str()));
}

SL_TEST ("ui", rollLiveSelectionRow)
{
    section ("Live roll: the selection row (how many notes, Select all, Duplicate, Delete, Clear all twice) and the switches "
             "(Freehand, Multi, the Grid)");

    LiveRig rig (clipOf ({ { "a", 60.0, 0.0, 1.0 }, { "b", 62.0, 2.0, 1.0 }, { "c", 64.0, 4.0, 1.0 } }));
    auto& view = rig.view();
    auto& doc = rig.doc();
    const auto& geo = rig.geo();
    const auto mouse = rig.mouse();
    const double bw = geo.beatWidth();

    check (view.getSelectionText() == "No notes selected" && ! view.getDuplicateButton().isEnabled() && ! view.getDeleteButton().isEnabled()
               && view.getSelectAllButton().isEnabled() && ! view.getInspector().showsNote()
               && view.getInspector().getEmptyText().startsWith ("Select a note"),
           "nothing selected: \"No notes selected\", Duplicate and Delete greyed, the inspector says to select a note");

    view.getSelectAllButton().onClick();
    check (view.getSelectionText() == "3 notes selected" && view.getDuplicateButton().isEnabled() && view.getDeleteButton().isEnabled(),
           fmt ("Select all: \"%s\", Duplicate and Delete work", str (view.getSelectionText()).c_str()));

    doc.setSelection ({ "a", "b" }, std::string ("a"));
    view.syncRollControls();
    view.getDuplicateButton().onClick();
    const auto dup = doc.current();
    check (dup->notes.size() == 5 && label (rig.proc) == "Duplicate" && doc.rollState().selected.size() == 2 && ! doc.rollState().isSelected ("a"),
           fmt ("Duplicate copies the two selected notes after them (%d notes), one undo step, the copies selected", (int) dup->notes.size()));
    view.getDeleteButton().onClick();
    check (doc.current()->notes.size() == 3 && label (rig.proc) == "Delete notes" && view.getSelectionText() == "No notes selected",
           fmt ("Delete removes them (\"%s\")", label (rig.proc).c_str()));

    // Clear all: the first click asks for a second
    view.getClearAllButton().onClick();
    const bool asks = view.getClearAllButton().getButtonText() == "Click again to clear" && doc.current()->notes.size() == 3;
    view.getClearAllButton().onClick();
    check (asks && doc.current()->notes.empty() && label (rig.proc) == "Clear all" && view.getClearAllButton().getButtonText() == "Clear all",
           "Clear all: the first click says \"Click again to clear\", the second removes every note (one undo step)");
    view.getUndoButton().onClick();

    // Freehand: refused in Build with the prototype's toast; on in a lane mode
    view.getFreehandButton().onClick();
    const bool refused = ! view.getFreehandButton().getToggleState() && view.getToastText() == "Freehand draws a lane: pick a lane mode (2 to 8) first";
    view.setMode (4);
    view.getFreehandButton().onClick();
    const bool on = view.getFreehandButton().getToggleState() && rig.roll().getGestures().settings().free;
    view.setMode (1);
    check (refused && on && ! view.getFreehandButton().getToggleState(),
           "Freehand: refused in Build (toast), lit in Vibrato, off again when Build is picked");

    // Multi: a click adds to the selection, as Ctrl+click does
    view.getMultiButton().onClick();
    const auto a = noteIn (doc.current(), "a"), b = noteIn (doc.current(), "b");
    mouse.click ((a->s + 0.5) * bw, geo.baseYAt (*a, 0.5), 10000.0);
    mouse.click ((b->s + 0.5) * bw, geo.baseYAt (*b, 0.5), 11000.0);
    check (view.getMultiButton().getToggleState() && doc.rollState().selected.size() == 2 && view.getSelectionText() == "2 notes selected",
           fmt ("Multi lit: two clicks select two notes (\"%s\")", str (view.getSelectionText()).c_str()));
    view.getMultiButton().onClick();

    // the Grid: 1/8 makes a drag move in eighths (a drag of 0.3 beat moves half a beat), and the project keeps it
    auto& grid = view.getGridBox();
    grid.setSelectedId (2, juce::sendNotificationSync);
    const auto c = noteIn (doc.current(), "c");
    const double x = (c->s + 0.5) * bw, y = geo.baseYAt (*c, 0.5);
    mouse.down (x, y, 20000.0);
    mouse.drag (x + 0.3 * bw, y, 20050.0);
    mouse.up (x + 0.3 * bw, y, 20100.0);
    const auto moved = noteIn (doc.current(), "c");
    check (juce::exactlyEqual (rig.roll().getGrid(), 0.5) && juce::exactlyEqual (rig.proc.getUiState().gridBeats, 0.5f)
               && juce::exactlyEqual (moved->s, 4.5),
           fmt ("Grid 1/8: a drag of 0.3 beat moves the note half a beat (to %.3f); UIST gridBeats %.3f", moved->s,
                (double) rig.proc.getUiState().gridBeats));
}

SL_TEST ("ui", rollLiveKeys)
{
    section ("Live roll: the shortcuts reach the roll through the editor only with Keys on; Space always goes to Cubase");

    LiveRig rig (clipOf ({ { "a", 60.0, 0.0, 1.0 }, { "b", 62.0, 2.0, 1.0 } }));
    auto& editor = *rig.editor;
    auto& view = rig.view();
    auto& doc = rig.doc();
    const auto key = [] (int code, int mods = 0) { return juce::KeyPress (code, juce::ModifierKeys (mods), 0); };
    using M = juce::ModifierKeys;

    const auto clip0 = doc.current();
    const bool offA = editor.keyPressed (key ('A', M::commandModifier));
    const bool off3 = editor.keyPressed (key ('3'));
    const bool offDelete = editor.keyPressed (key (juce::KeyPress::deleteKey));
    check (! offA && ! off3 && ! offDelete && view.getMode() == 1 && doc.rollState().selected.empty() && doc.current() == clip0,
           "Keys off: Ctrl+A, 3 and Delete go on to Cubase and change nothing");

    editor.setKeysOn (true);
    const bool on3 = editor.keyPressed (key ('3'));
    const bool mode3 = view.getMode() == 3 && view.getModeButton (3).getToggleState() && view.getHintText().startsWith ("VOLUME (3)");
    editor.keyPressed (key ('1'));
    editor.keyPressed (key ('A', M::commandModifier));
    const bool all = view.getSelectionText() == "2 notes selected";
    editor.keyPressed (key (juce::KeyPress::deleteKey));
    const bool deleted = doc.current()->notes.empty() && label (rig.proc) == "Delete notes";
    editor.keyPressed (key ('Z', M::commandModifier));
    const bool undone = model::sameClip (*doc.current(), *clip0) && view.getToastText() == "Undo: Delete notes";
    editor.keyPressed (key ('Z', M::commandModifier | M::shiftModifier));
    const bool redone = doc.current()->notes.empty() && view.getToastText() == "Redo: Delete notes";
    editor.keyPressed (key ('Y', M::commandModifier));
    const bool nothing = view.getToastText() == "Nothing to redo";
    const bool space = editor.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey, M(), ' '));
    check (on3 && mode3 && all && deleted && undone && redone && nothing && ! space,
           "Keys on: 3 picks Volume, 1 Build, Ctrl+A selects both notes, Delete removes them (one step), Ctrl+Z brings them back "
           "(\"Undo: Delete notes\"), Ctrl+Shift+Z deletes them again, Ctrl+Y says \"Nothing to redo\"; Space still goes to Cubase");

    editor.keyPressed (key ('Z', M::commandModifier));
    doc.setSelection ({ "a" }, std::string ("a"));
    view.syncRollControls();
    editor.keyPressed (key ('B'));
    const bool armed = rig.roll().getGestures().isLinkArmed() && view.getSlideIntoButton().getToggleState();
    editor.keyPressed (key (juce::KeyPress::escapeKey));
    check (armed && doc.rollState().selected.empty() && view.getSelectionText() == "No notes selected",
           "B arms Slide into (the button lights); Escape clears the selection");
    editor.setKeysOn (false);
}

SL_TEST ("ui", rollLiveInspector)
{
    section ("Live roll: the inspector (T6) shows the selected note and types new values through R1's parsers, one undo step each");

    LiveRig rig (clipOf ({ { "a", 60.0, 0.0, 1.0 }, { "b", 64.0, 2.0, 1.0 } }));
    auto& view = rig.view();
    auto& doc = rig.doc();
    auto& insp = view.getInspector();
    doc.setSelection ({ "a" }, std::string ("a"));
    view.syncRollControls();
    check (insp.showsNote() && ! insp.showsNode() && insp.getStart().getValue() == "1.1.1" && insp.getLength().getValue() == "1"
               && insp.getPitch().getValue() == "C3" && insp.getVelocity().getValue() == "102",
           "C3 selected: Start 1.1.1, Length 1, Pitch C3, Velocity 102 (0.8 x 127)");

    insp.getStart().type ("1.2.3");
    const auto a1 = noteIn (doc.current(), "a");
    insp.getLength().type ("1/8");
    const auto a2 = noteIn (doc.current(), "a");
    insp.getPitch().type ("D3");
    const auto a3 = noteIn (doc.current(), "a");
    const std::string pitchStep = label (rig.proc);
    insp.getVelocity().type ("64");
    const auto a4 = noteIn (doc.current(), "a");
    check (juce::exactlyEqual (a1->s, 1.5) && juce::exactlyEqual (a2->l, 0.5) && juce::exactlyEqual (a3->p, 62.0) && pitchStep == "Type pitch"
               && near (a4->vel, 64.0 / 127.0, 1e-15) && label (rig.proc) == "Type velocity" && insp.getStart().getValue() == "1.2.3"
               && insp.getPitch().getValue() == "D3",
           fmt ("typed 1.2.3, 1/8, D3 and 64: start %.3f, length %.3f, pitch %.0f, velocity %.4f; each one undo step (\"%s\")", a1->s, a2->l,
                a3->p, a4->vel, label (rig.proc).c_str()));

    // a value that cannot be read: the toast says what to type, nothing changes
    const auto before = doc.current();
    insp.getStart().type ("soon");
    const bool refused = doc.current() == before && label (rig.proc) == "Type velocity" && insp.getStart().getValue() == "1.2.3"
                         && view.getToastText() == "Type a position like 2.1.3 (bar.beat.sixteenth)";
    insp.getVelocity().type ("64");
    check (refused && doc.current() == before && label (rig.proc) == "Type velocity",
           "\"soon\" in Start: the toast says how to type a position and the note stays; the value it shows typed again changes nothing");

    // two notes selected: Pitch moves both by the same amount, Velocity sets both
    doc.setSelection ({ "a", "b" }, std::string ("a"));
    view.syncRollControls();
    insp.getPitch().type ("E3");
    insp.getVelocity().type ("127");
    const auto a5 = noteIn (doc.current(), "a"), b5 = noteIn (doc.current(), "b");
    check (juce::exactlyEqual (a5->p, 64.0) && juce::exactlyEqual (b5->p, 66.0) && juce::exactlyEqual (a5->vel, 1.0) && juce::exactlyEqual (b5->vel, 1.0),
           fmt ("two notes: E3 moves both up 2 (to %.0f and %.0f), 127 sets both to full velocity", a5->p, b5->p));

    // the node: Pitch mode, a node in the middle of the note
    model::NodeList nodes = model::noteLane (*a5, core::LaneId::pitch);
    nodes.insert (nodes.begin() + 1, model::mkNode (0.5, 0.5, model::NodeShape::smooth, 0.0));
    rig.proc.editClip ("Three nodes", model::setLaneNodes (*doc.current(), "a", core::LaneId::pitch, nodes));
    view.setMode (2);
    doc.setSelection ({ "a" }, std::string ("a"));
    doc.setActiveNode (doc::ActiveNode { "a", core::LaneId::pitch, 1, 0 });
    view.syncRollControls();
    const bool shows = insp.showsNode() && insp.getNodeCaption() == "Node (Pitch)" && insp.getNodeTime().getValue() == "0.25"
                       && insp.getNodeTime().isEnabled();
    insp.getNodeValue().type ("+3");
    const auto& n6 = model::noteLane (*noteIn (doc.current(), "a"), core::LaneId::pitch);
    const bool value = near (n6[1].v, 0.5 + 3.0 / 24.0, 1e-12) && label (rig.proc) == "Type node value";
    insp.getNodeTime().type ("0.375");
    const auto& n7 = model::noteLane (*noteIn (doc.current(), "a"), core::LaneId::pitch);
    const bool time = near (n7[1].t, 0.75, 1e-12) && label (rig.proc) == "Type node time" && insp.getNodeTime().getValue() == "0.375";
    const auto toastBefore = view.getToastText();
    insp.getNodeValue().type ("loud");
    const bool bad = label (rig.proc) == "Type node time" && view.getToastText().isNotEmpty() && view.getToastText() != toastBefore;
    doc.setActiveNode (doc::ActiveNode { "a", core::LaneId::pitch, 0, 0 });
    view.syncRollControls();
    const bool endNode = insp.getNodeTime().getValue() == "start" && ! insp.getNodeTime().isEnabled();
    check (shows && value && time && bad && endNode,
           fmt ("Node (Pitch) at 0.25 beats in: +3 makes it %.4f (+3 semitones), 0.375 moves it to t %.3f; \"loud\" says why in the "
                "toast (\"%s\"); the first node shows \"start\", greyed", n6[1].v, n7[1].t, str (view.getToastText()).c_str()));
}

SL_TEST ("ui", rollLiveVelocityParse)
{
    section ("Inspector: the Velocity box reads a number as the prototype's parseFloat (clean (text)), rounded, 1 to 127");

    struct Case
    {
        const char* text;
        int want;   // 0 = not a number
    };
    // from node: parseFloat (clean (t)), clamp (Math.round (x), 1, 127)
    const Case cases[] = { { "102", 102 }, { "90.6", 91 }, { "300", 127 }, { "-5", 1 }, { "abc", 0 }, { "1e2", 100 }, { " 64 ", 64 },
                           { ".5", 1 }, { "Infinity", 0 }, { "12abc", 12 }, { "+90", 90 }, { "0", 1 }, { "127.4", 127 }, { "1,5", 2 },
                           { "5.", 5 }, { "-0", 1 }, { "", 0 }, { "64 velocity", 64 }, { "0x10", 1 }, { "1e400", 0 } };
    int right = 0;
    std::string firstBad;
    for (const auto& c : cases)
    {
        const auto v = ui::roll::parseVelocity (c.text);
        const bool ok = c.want == 0 ? ! v.has_value() : (v.has_value() && *v == c.want);
        right += ok ? 1 : 0;
        if (! ok && firstBad.empty())
            firstBad = fmt ("\"%s\" gave %d", c.text, v.value_or (0));
    }
    const int count = (int) (sizeof (cases) / sizeof (cases[0]));
    check (right == count, fmt ("%d of %d typed velocities read as the prototype reads them%s%s", right, count, firstBad.empty() ? "" : "; first other: ",
                                firstBad.c_str()));
}

SL_TEST ("ui", rollLiveHints)
{
    section ("Hint bar: sl-ui.js modeHint's text for every mode, Freehand and Slide into (less M5's box select and lane menus)");

    struct Case
    {
        int mode;
        std::optional<core::LaneId> lane;
        bool freehand, link;
        const char* text;   // UTF-8, from sl-ui.js modeHint run in node (tools: scratch hints.js)
    };
    const Case cases[] = {
        { 1, std::nullopt, false, false,
          "BUILD (1): double-click empty space = new note \xc2\xb7 drag a note to move it \xc2\xb7 drag its end dots sideways (length) or up/down (bend) \xc2\xb7 \xe2\x97\x86 slide handles \xc2\xb7 Shift+click a 2nd note = slide into it \xc2\xb7 right-click = menu" },
        { 2, core::LaneId::pitch, false, false,
          "PITCH (2): notes are locked, only Pitch changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note \xc2\xb7 nodes snap to semitones (hold Shift for free) \xc2\xb7 \xe2\x97\x86 slide handles" },
        { 3, core::LaneId::amp, false, false,
          "VOLUME (3): notes are locked, only Volume changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note" },
        { 4, core::LaneId::vib, false, false,
          "VIBRATO (4): notes are locked, only Vibrato changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note" },
        { 5, core::LaneId::tone, false, false,
          "TONE (5): notes are locked, only Tone changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note" },
        { 6, core::LaneId::formant, false, false,
          "FORMANT (6): notes are locked, only Formant changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note" },
        { 7, core::LaneId::hollow, false, false,
          "HOLLOW (7): notes are locked, only Hollow changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note" },
        { 8, core::LaneId::travel, false, false,
          "SAMPLE (8) \xc2\xb7 TRAVEL: notes are locked, only Travel changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note \xc2\xb7 Travel: above the middle line plays forwards, below plays backwards, on the line stops (1/8x to 8x). Path notes: the line is where in the Region the note is." },
        { 8, core::LaneId::heads, false, false,
          "SAMPLE (8) \xc2\xb7 HEADS: notes are locked, only Heads changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note \xc2\xb7 Heads: raise the line for more playheads (1 to 8), lower it for fewer. Flat = the Heads knob." },
        { 8, core::LaneId::grain, false, false,
          "SAMPLE (8) \xc2\xb7 GRAIN: notes are locked, only Grain changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note \xc2\xb7 Grain: raise the line for longer grains, lower it for shorter ones. Flat = the Grain knob." },
        { 8, core::LaneId::refresh, false, false,
          "SAMPLE (8) \xc2\xb7 REFRESH: notes are locked, only Refresh changes \xc2\xb7 click a note to select it \xc2\xb7 drag \xe2\x97\x8f nodes \xc2\xb7 double-click the line = add a node \xc2\xb7 double-click a node = delete \xc2\xb7 drag the small \xe2\x97\x87 in a segment = curve it (it fills in), double-click a filled \xe2\x97\x86 = straighten \xc2\xb7 right-click = shapes for every selected note \xc2\xb7 Refresh: raise the line for faster fresh grabs, lower it for slower; the very bottom = Smooth. The line works once Refresh on the Sample tab is Free or Synced (it looks dim until then)." },
        { 3, core::LaneId::amp, true, false, "FREEHAND (Volume): paint across the selected note; it turns into nodes when you let go." },
        { 8, core::LaneId::grain, true, false, "FREEHAND (Grain): paint across the selected note; it turns into nodes when you let go." },
        { 1, std::nullopt, false, true, "SLIDE INTO: click the note the selected note should slide into." },
        { 2, core::LaneId::pitch, false, true, "SLIDE INTO: click the note the selected note should slide into." },
    };
    int right = 0;
    std::string firstBad;
    for (const auto& c : cases)
    {
        const auto got = ui::modeHint (c.mode, c.lane, c.freehand, c.link);
        const bool ok = got == juce::String::fromUTF8 (c.text);
        right += ok ? 1 : 0;
        if (! ok && firstBad.empty())
            firstBad = fmt ("mode %d: \"%s\"", c.mode, got.toRawUTF8());
    }
    const int count = (int) (sizeof (cases) / sizeof (cases[0]));
    check (right == count, fmt ("%d of %d hints equal the prototype's%s%s", right, count, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));

    // the editor's hint bar follows the mode, Freehand and Slide into; a hint too long for the bar shows whole on hover
    LiveRig rig (clipOf ({ { "a", 60.0, 0.0, 1.0 } }));
    auto& view = rig.view();
    int shown = 0;
    for (int m = 1; m <= 8; ++m)
    {
        view.setMode (m, 1);
        const auto st = rig.doc().rollState();
        shown += view.getHintText() == ui::modeHint (m, st.activeLane(), false, false) ? 1 : 0;
    }
    view.setMode (1);
    auto& bar = view.getHintBar();
    const bool cut = bar.isHintCut();
    bar.showHover (true);
    const auto hover = bar.getHover().getBounds();
    const bool whole = bar.getHover().isVisible() && hover.getBottom() == bar.getBottom() + 1 && hover.getHeight() > bar.getHeight();
    bar.showHover (false);
    check (shown == 8 && cut && whole && ! bar.getHover().isVisible(),
           fmt ("the bar shows each mode's hint (%d of 8); Build's is cut at 1100 px and shows whole over the bar on hover (%d px tall, "
                "growing up from the bar's bottom)", shown, hover.getHeight()));
}

//==================================================================================================
// Pictures of the live roll for review (the Release runner uploads them; the Debug runner draws them with JUCE's
// assertions live): what a gesture leaves on screen in the rows around the roll.

namespace
{
    struct LiveShot
    {
        ThemeId theme = ThemeId::dark;
        int width = 1100, height = 680;
        int mode = 1;
        bool node = false;        // Pitch: a node added on the selected note (the inspector's node part)
        bool hover = false;       // the hint bar's whole hint over it
        bool freehand = false;    // Volume with Freehand on
        bool folded = true;
    };

    juce::Image shootLive (const LiveShot& s)
    {
        StrandLockProcessor proc;
        proc.waitForSounds();
        auto& settings = proc.getUserSettings();
        settings.setTheme (s.theme);
        settings.setUiScalePercent (100);
        settings.setKeysDefault (false);
        juce::Image image;
        {
            StrandLockEditor editor (proc);
            editor.setLogicalSize (s.width, s.height);
            auto& view = editor.getView();
            auto& roll = view.getRoll();
            roll.setMenusShown (false);
            view.setFolded (s.folded);
            editor.refreshNow();
            const auto& geo = roll.getGeometry();
            const Pointer mouse { roll.getCanvas(), geo };
            const double bw = geo.beatWidth();

            // the demo's lead note (sd-25, G4) dragged a beat later and a row down, then let go: it is selected
            roll.setScroll (0.0, geo.cy (77.0) - geo.viewHeight() / 2.0);
            if (const auto n = noteIn (proc.getDocument().current(), "sd-25"))
            {
                const double x = (n->s + 0.3 * n->l) * bw, y = geo.baseYAt (*n, 0.3);
                mouse.down (x, y, 10000.0);
                mouse.drag (x + bw, y + geo.rowHeight(), 10050.0);
                mouse.up (x + bw, y + geo.rowHeight(), 10100.0);
            }
            if (s.mode != 1)
                view.setMode (s.mode);
            if (s.node)
                if (const auto n = noteIn (proc.getDocument().current(), "sd-25"))
                {
                    // a click on its second pitch node picks it: the inspector shows that node
                    const auto p = geo.nodeXY (*n, core::LaneId::pitch, 1);
                    mouse.click (p.x, p.y, 20000.0);
                }
            if (s.freehand)
                view.getFreehandButton().onClick();
            editor.refreshNow();
            if (s.hover)
                view.getHintBar().showHover (true);
            image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        }
        resetUserSettings (proc);
        return image;
    }
}

SL_SNAPSHOT (live_build_moved_dark)     { return shootLive ({ ThemeId::dark, 1100, 680, 1 }); }
SL_SNAPSHOT (live_build_moved_light)    { return shootLive ({ ThemeId::light, 1100, 680, 1 }); }
SL_SNAPSHOT (live_pitch_node_dark)      { return shootLive ({ ThemeId::dark, 1100, 680, 2, true }); }
SL_SNAPSHOT (live_pitch_node_light)     { return shootLive ({ ThemeId::light, 1100, 680, 2, true }); }
SL_SNAPSHOT (live_hint_hover_dark)      { return shootLive ({ ThemeId::dark, 1100, 680, 3, false, true }); }
SL_SNAPSHOT (live_freehand_narrow_dark) { return shootLive ({ ThemeId::dark, 900, 560, 3, false, false, true }); }
SL_SNAPSHOT (live_node_narrow_light)    { return shootLive ({ ThemeId::light, 900, 560, 2, true }); }
SL_SNAPSHOT (live_tab_open_dark)        { return shootLive ({ ThemeId::dark, 1100, 680, 1, false, false, false, false }); }
