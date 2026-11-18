// Key routing (PLUGIN-PLAN 4 M4 "keys", 7.6 decision 3): with Keys off every key event returns false (it goes on to
// Cubase) and runs nothing; with Keys on Space is never consumed, and the shortcuts (Undo, Redo, Delete, Select all,
// Duplicate, the mode numbers 1 to 8, and the rest of the prototype's list) run only then.

#include <juce_gui_basics/juce_gui_basics.h>

#include "RollRig.h"

#include "model/ModelFixture.h"
#include "model/Presets.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/KeyRouter.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::KeyRouter;

    /** Counts every action a router runs. */
    struct Counter
    {
        std::map<std::string, int> runs;
        std::vector<int> modes;

        ui::KeyActions actions()
        {
            ui::KeyActions a;
            auto count = [this] (const char* name) { return [this, name] { ++runs[name]; }; };
            a.undo = count ("undo");
            a.redo = count ("redo");
            a.deleteSelection = count ("delete");
            a.selectAll = count ("select all");
            a.duplicate = count ("duplicate");
            a.copyLanes = count ("copy lanes");
            a.pasteLanes = count ("paste lanes");
            a.clearSelection = count ("clear");
            a.armLink = count ("slide into");
            a.setMode = [this] (int m) { modes.push_back (m); ++runs["mode"]; };
            return a;
        }

        int total() const
        {
            int n = 0;
            for (const auto& [name, count] : runs)
                n += count;
            return n;
        }
    };

    /** Every key a test presses, once each: the printable ASCII keys (both cases of the letters) and the special keys
        (Space is ASCII 32, so it is in the list once). */
    std::vector<int> allKeyCodes()
    {
        std::vector<int> codes;
        for (int c = 32; c < 127; ++c)
            codes.push_back (c);
        for (const int c : { juce::KeyPress::spaceKey, juce::KeyPress::escapeKey, juce::KeyPress::returnKey, juce::KeyPress::tabKey,
                             juce::KeyPress::deleteKey, juce::KeyPress::backspaceKey, juce::KeyPress::insertKey, juce::KeyPress::upKey,
                             juce::KeyPress::downKey, juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::pageUpKey,
                             juce::KeyPress::pageDownKey, juce::KeyPress::homeKey, juce::KeyPress::endKey, juce::KeyPress::F1Key,
                             juce::KeyPress::F2Key, juce::KeyPress::F5Key, juce::KeyPress::F12Key, juce::KeyPress::numberPad0,
                             juce::KeyPress::numberPad1, juce::KeyPress::numberPad2, juce::KeyPress::numberPad3, juce::KeyPress::numberPad4,
                             juce::KeyPress::numberPad5, juce::KeyPress::numberPad6, juce::KeyPress::numberPad7, juce::KeyPress::numberPad8,
                             juce::KeyPress::numberPad9, juce::KeyPress::numberPadAdd, juce::KeyPress::numberPadSubtract,
                             juce::KeyPress::numberPadDelete, juce::KeyPress::playKey, juce::KeyPress::stopKey })
            codes.push_back (c);
        std::sort (codes.begin(), codes.end());
        codes.erase (std::unique (codes.begin(), codes.end()), codes.end());
        return codes;
    }

    std::vector<juce::ModifierKeys> allMods()
    {
        using M = juce::ModifierKeys;
        return { M(), M (M::shiftModifier), M (M::commandModifier), M (M::ctrlModifier), M (M::altModifier),
                 M (M::commandModifier | M::shiftModifier), M (M::ctrlModifier | M::altModifier), M (M::shiftModifier | M::altModifier) };
    }

    juce::KeyPress key (int code, int mods = 0)
    {
        return juce::KeyPress (code, juce::ModifierKeys (mods), 0);
    }
}

SL_TEST ("ui", keysOffPassEverything)
{
    section ("Keys: with Keys off every key event returns false (it goes on to Cubase) and runs nothing");

    Counter counter;
    const KeyRouter router (counter.actions());
    int events = 0, kept = 0;
    for (const int code : allKeyCodes())
        for (const auto& mods : allMods())
        {
            ++events;
            kept += router.route (juce::KeyPress (code, mods, 0), false) ? 1 : 0;
        }
    note (fmt ("%d key events (%zu keys x %zu modifier sets)", events, allKeyCodes().size(), allMods().size()));
    check (events > 900 && kept == 0, fmt ("Keys off: %d of %d key events kept (0 expected)", kept, events));
    check (counter.total() == 0, fmt ("Keys off: no shortcut ran (%d ran)", counter.total()));
}

SL_TEST ("ui", keysOnNeverTakeSpace)
{
    section ("Keys: with Keys on Space is never consumed, with any modifier; every other key is kept");

    Counter counter;
    const KeyRouter router (counter.actions());
    int spaces = 0, spaceKept = 0, others = 0, otherKept = 0;
    for (const int code : allKeyCodes())
        for (const auto& mods : allMods())
        {
            const bool space = code == juce::KeyPress::spaceKey;
            const bool k = router.route (juce::KeyPress (code, mods, space ? (juce::juce_wchar) ' ' : 0), true);
            (space ? spaces : others) += 1;
            (space ? spaceKept : otherKept) += k ? 1 : 0;
        }
    const int ranBeforeSpace = counter.total();
    for (const auto& mods : allMods())
        (void) router.route (juce::KeyPress (juce::KeyPress::spaceKey, mods, ' '), true);
    check (spaces == (int) allMods().size() && spaceKept == 0, fmt ("Space with %d modifier sets: kept %d times (0 expected)", spaces, spaceKept));
    check (counter.total() == ranBeforeSpace, "Space runs no shortcut");
    check (otherKept == others, fmt ("every other key is kept with Keys on, as M0's rule says (%d of %d)", otherKept, others));
}

SL_TEST ("ui", keysShortcutsOnlyWithKeysOn)
{
    section ("Keys: each shortcut runs its action once with Keys on, and never with Keys off");

    struct Row
    {
        int code;
        int mods;
        const char* action;   // the Counter's name, or "mode N", or "" for none
        const char* what;
    };
    using M = juce::ModifierKeys;
    const Row rows[] = {
        { 'Z', M::commandModifier, "undo", "Ctrl+Z" },
        { 'z', M::commandModifier, "undo", "Ctrl+z (lower-case key code)" },
        { 'Z', M::commandModifier | M::shiftModifier, "redo", "Ctrl+Shift+Z" },
        { 'Y', M::commandModifier, "redo", "Ctrl+Y" },
        { juce::KeyPress::deleteKey, 0, "delete", "Delete" },
        { juce::KeyPress::backspaceKey, 0, "delete", "Backspace" },
        { 'A', M::commandModifier, "select all", "Ctrl+A" },
        { 'D', M::commandModifier, "duplicate", "Ctrl+D" },
        { 'C', M::commandModifier, "copy lanes", "Ctrl+C" },
        { 'V', M::commandModifier, "paste lanes", "Ctrl+V" },
        { juce::KeyPress::escapeKey, 0, "clear", "Escape" },
        { 'B', 0, "slide into", "B" },
        { '1', 0, "mode 1", "1" }, { '2', 0, "mode 2", "2" }, { '3', 0, "mode 3", "3" }, { '4', 0, "mode 4", "4" },
        { '5', 0, "mode 5", "5" }, { '6', 0, "mode 6", "6" }, { '7', 0, "mode 7", "7" }, { '8', 0, "mode 8", "8" },
        { juce::KeyPress::numberPad1, 0, "mode 1", "number pad 1" }, { juce::KeyPress::numberPad8, 0, "mode 8", "number pad 8" },
        { '9', 0, "", "9 (no mode 9)" }, { '0', 0, "", "0" }, { '3', M::shiftModifier, "", "Shift+3 (types a sign)" },
        { '3', M::altModifier, "", "Alt+3" }, { 'Z', 0, "", "Z alone (the live keyboard's octave key, M8)" },
        { 'Q', M::commandModifier, "", "Ctrl+Q" }, { 'A', 0, "", "A alone (a live note key, M8)" },
    };

    int right = 0;
    std::string firstBad;
    for (const auto& r : rows)
    {
        Counter on, off;
        const KeyRouter routerOn (on.actions()), routerOff (off.actions());
        const bool keptOn = routerOn.route (key (r.code, r.mods), true);
        const bool keptOff = routerOff.route (key (r.code, r.mods), false);
        const std::string want = r.action;
        bool ok = keptOn && ! keptOff && off.total() == 0;
        if (want.empty())
            ok = ok && on.total() == 0;
        else if (want.rfind ("mode ", 0) == 0)
            ok = ok && on.total() == 1 && on.modes.size() == 1 && on.modes[0] == std::stoi (want.substr (5));
        else
            ok = ok && on.total() == 1 && on.runs[want] == 1;
        if (ok)
            ++right;
        else if (firstBad.empty())
            firstBad = fmt ("%s: kept on %d, off %d; %d actions on, %d off", r.what, (int) keptOn, (int) keptOff, on.total(), off.total());
    }
    const int rowCount = (int) (sizeof (rows) / sizeof (rows[0]));
    note (fmt ("%d of %d keys as listed%s%s", right, rowCount, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (right == rowCount, "Undo, Redo, Delete, Select all, Duplicate, Copy and Paste lanes, Escape, Slide into and the modes 1 to 8 each run once "
                              "with Keys on and never with Keys off; other keys run nothing");
}

SL_TEST ("ui", keysDriveTheRoll)
{
    section ("Keys: the shortcuts wired to the roll change the document only with Keys on");

    RollRig rig (*model::loadClipPreset ("strand-demo"));
    auto& g = rig.gestures();
    ui::KeyActions a;
    a.undo = [&rig] { rig.undo(); };
    a.deleteSelection = [&g] { g.deleteSelection(); };
    a.selectAll = [&g] { g.selectAll(); };
    a.duplicate = [&g] { g.duplicateSelection(); };
    a.setMode = [&g] (int m) { g.setMode (m); };
    const KeyRouter router (a);
    using M = juce::ModifierKeys;
    const auto clip0 = rig.doc.current();

    for (const auto& k : { key ('3'), key ('A', M::commandModifier), key ('D', M::commandModifier), key (juce::KeyPress::deleteKey),
                           key ('Z', M::commandModifier) })
        (void) router.route (k, false);
    const auto stOff = rig.doc.rollState();
    check (stOff.mode == 1 && stOff.selected.empty() && rig.doc.current() == clip0 && rig.steps == 0,
           "Keys off: 3, Ctrl+A, Ctrl+D, Delete and Ctrl+Z leave the mode, the selection, the clip and the undo history alone");

    (void) router.route (key ('3'), true);
    const bool mode3 = rig.doc.rollState().mode == 3;
    (void) router.route (key ('1'), true);
    (void) router.route (key ('A', M::commandModifier), true);
    const bool all = rig.doc.rollState().selected.size() == clip0->notes.size();
    rig.doc.setSelection ({ "sd-1", "sd-2" }, std::string ("sd-1"));
    (void) router.route (key ('D', M::commandModifier), true);
    const bool duplicated = rig.doc.current()->notes.size() == clip0->notes.size() + 2 && rig.steps == 1 && rig.labels.back() == "Duplicate";
    (void) router.route (key (juce::KeyPress::deleteKey), true);
    const bool deleted = rig.doc.current()->notes.size() == clip0->notes.size() && rig.steps == 2 && rig.labels.back() == "Delete notes";
    (void) router.route (key ('Z', M::commandModifier), true);
    (void) router.route (key ('Z', M::commandModifier), true);
    const bool undone = model::sameClip (*rig.doc.current(), *clip0);
    check (mode3 && all && duplicated && deleted && undone,
           "Keys on: 3 picks Volume, Ctrl+A selects all 26 notes, Ctrl+D duplicates the two selected (1 undo step), Delete removes the copies "
           "(1 step), and Ctrl+Z twice is back to the start");
}

SL_TEST ("ui", keysHeldBTogglesOnce)
{
    section ("Keys: a held B toggles Slide into once, however often its key repeats; let go, the next B toggles again (fix pass F4)");

    // JUCE hands over every repeat of a held key as one more key press and does not say it is a repeat. sl-ui.js
    // ignores repeated keys (e.repeat); B is the one shortcut a repeat would change (it toggles).
    Counter counter;
    const KeyRouter router (counter.actions());
    bool allKept = true;
    for (int i = 0; i < 12; ++i)   // one press and 11 repeats
        allKept = router.route (key ('B'), true) && allKept;
    const int held = counter.runs["slide into"];
    router.keyReleased();
    allKept = router.route (key ('B'), true) && allKept;
    allKept = router.route (key ('B'), true) && allKept;
    const int again = counter.runs["slide into"];
    (void) router.route (key ('3'), true);   // another key goes down: B is no longer repeating
    allKept = router.route (key ('b'), true) && allKept;
    const int afterOther = counter.runs["slide into"];
    const bool off = ! router.route (key ('B'), false);
    check (held == 1 && again == 2 && afterOther == 3 && allKept && off && counter.runs["mode"] == 1,
           fmt ("B held through 11 repeats toggles once (%d); let go and pressed again, once more (%d); after another key, once more (%d); "
                "every B is kept with Keys on and none with Keys off", held, again, afterOther));
}
