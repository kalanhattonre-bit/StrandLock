// M7's chips and suggestion line on the strip (PLUGIN-PLAN 4 M7, 7.9 decision 2; sl-ui-sample.js renderChips, sl-ui.js
// updateStripText): a song's part chips each set the Region in one undo step (the one that is the Region lit), "Use the
// whole file" too, and a long file opens on the part pickPart chooses (U2) inside its load's own undo step; a new Region
// gets a root suggestion; a short file's suggestions (root, key, Loop cleanly, the engine) each do what they say, and
// none applies itself; a starter's one "Parts" chip sits beside the file line and its menu picks a part.

#include "plugin/SongRig.h"

#include "PluginEditor.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"
#include "ui/MainView.h"
#include "ui/SampleTabRules.h"
#include "ui/tabs/SoundTab.h"

#include <cmath>
#include <cstring>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    bool isRegion (const std::pair<double, double>& r, double a, double b)
    {
        return std::memcmp (&r.first, &a, sizeof (double)) == 0 && std::memcmp (&r.second, &b, sizeof (double)) == 0;
    }

    /** The chip showing text is on screen: made, visible, and inside the chips' own area (fix pass T7-1: a chip that did
        not fit the row used to be made and hidden, and the tests that read the chips' texts could not see it). */
    bool onScreen (const ui::PartChips& chips, const juce::String& text)
    {
        const auto* b = chips.chip (text);
        return b != nullptr && b->isVisible() && chips.isVisible() && chips.getWidth() > 0 && b->getX() >= 0 && b->getY() >= 0
            && b->getRight() <= chips.getWidth() && b->getBottom() <= chips.getHeight();
    }

    /** Clicks the chip showing text (its onClick, at once), as the mouse can: only one on screen. False when none is. */
    bool click (ui::PartChips& chips, const juce::String& text)
    {
        auto* b = chips.chip (text);
        if (b == nullptr || ! b->onClick || ! onScreen (chips, text))
            return false;
        b->onClick();
        return true;
    }
}

SL_TEST ("ui", partChipsSetTheRegion)
{
    section ("Part chips (D10, decision 2): a song opens on a suggested part (U2), each chip sets the Region in one undo step, "
             "Use the whole file too, and a new Region gets a root suggestion");

    const auto dir = songs::tempFolder ("chips");
    const auto file = dir.getChildFile ("minute song.wav");
    check (songs::writeMono16 (file, songs::minuteSong (22050.0), 22050.0), "a one-minute song (held note, quiet, beat, hit, chord) written at test time");

    StrandLockProcessor p;
    resetUserSettings (p);
    p.waitForSounds();
    StrandLockEditor editor (p);
    auto& view = editor.getView();
    auto& strip = view.getStrip();
    const auto stepsBefore = p.historySize();
    strip.loadFile (file);
    p.waitForSounds (30000);
    editor.refreshNow();

    const auto& parts = p.getSuggestions().analysis.parts;
    check (parts.size() >= 3, fmt ("the worker suggested %d parts", (int) parts.size()));
    if (parts.size() < 2)
        return;

    // U2: the Region is the part pickPart chooses, in the load's own undo step.
    std::vector<std::optional<ui::rules::Part>> list;
    for (const auto& q : parts)
        list.push_back (ui::rules::Part { analysis::partKindName (q.kind), q.label, q.start, q.end, q.tempoFound, q.bpm });
    const auto chosen = ui::rules::pickPart (&list);
    const auto r0 = p.currentRegion();
    check (chosen.has_value() && isRegion (r0, chosen->start, chosen->end) && p.historySize() == stepsBefore + 1
               && p.undoLabel().value_or ("") == "Load minute song.wav",
           fmt ("U2: the song opens on the suggested part \"%s\" (%.4f .. %.4f), inside the one undo step \"%s\"", chosen.has_value() ? chosen->label.c_str() : "",
                r0.first, r0.second, p.undoLabel().value_or ("").c_str()));

    // The chips: the suggestion line first, then Use the whole file and one chip per part, on rows of their own. Every
    // part's chip is on screen in the default 1100 x 680 window (fix pass UI7-1 and T7-1: the last one, the loop with its
    // tempo, was made but hidden for want of room on one row).
    auto& chips = strip.getChips();
    const auto texts = chips.texts();
    bool allParts = true;
    juce::StringArray hidden;
    for (const auto& q : parts)
    {
        const auto label = juce::String::fromUTF8 (q.label.c_str());
        allParts = allParts && texts.contains (label);
        if (! onScreen (chips, label))
            hidden.add (label);
    }
    check (strip.hasChipRow() && texts.contains ("Use the whole file") && allParts,
           fmt ("the chips (on their own rows): %s", texts.joinIntoString (" | ").toRawUTF8()));
    check (hidden.isEmpty() && onScreen (chips, "Use the whole file") && editor.getWidth() >= 1100 && strip.chipRowsFor (strip.getWidth()) >= 1,
           fmt ("every part chip is on screen in a %d x %d window, on %d row(s) of chips%s%s", editor.getWidth(), editor.getHeight(),
                strip.chipRowsFor (strip.getWidth()), hidden.isEmpty() ? "" : "; NOT on screen: ", hidden.joinIntoString (" | ").toRawUTF8()));
    auto* lit = chips.chip (juce::String::fromUTF8 (chosen->label.c_str()));
    check (lit != nullptr && lit->getToggleState(), "the chip of the part that is the Region is lit");

    // Another part's chip: the Region in one undo step.
    const auto other = parts[0].label == chosen->label ? parts[1] : parts[0];
    const auto steps1 = p.historySize();
    const bool clickedOther = click (chips, juce::String::fromUTF8 (other.label.c_str()));
    editor.refreshNow();
    check (clickedOther && isRegion (p.currentRegion(), other.start, other.end) && p.historySize() == steps1 + 1
               && p.undoLabel().value_or ("") == "Region: " + other.label,
           fmt ("a click on \"%s\" sets the Region to it (the exact edges) as one undo step \"%s\"", other.label.c_str(), p.undoLabel().value_or ("").c_str()));
    check (chips.chip (juce::String::fromUTF8 (other.label.c_str())) != nullptr && chips.chip (juce::String::fromUTF8 (other.label.c_str()))->getToggleState(),
           "and its chip is lit now");

    // The new Region gets a root suggestion when it has a pitch (sl-ui.js suggestRoot), after its moment. The Big hit has
    // one (fix pass T7-2: the check passed with no suggestion at all, so a Region that never asked would have passed too).
    p.waitForAnalysis (30000);
    editor.refreshNow();
    const auto& sug = p.getSuggestions();
    note (fmt ("after the part chip: %s", sug.root.has_value() ? fmt ("a root suggestion MIDI %d %+d cents for %.4f .. %.4f", sug.root->root, sug.root->cents,
                                                                           sug.root->regionStart, sug.root->regionEnd).c_str()
                                                               : "no root suggestion (the part has no single pitch)"));
    check (other.kind == analysis::PartKind::hit && sug.root.has_value() && p.showsRootSuggestion(),
           fmt ("the Region on \"%s\" (a hit, which has a pitch) gets a root suggestion, shown for that Region", other.label.c_str()));

    // Use the whole file: one undo step; then Undo goes back to the part.
    const auto steps2 = p.historySize();
    const bool clickedWhole = click (chips, "Use the whole file");
    editor.refreshNow();
    check (clickedWhole && isRegion (p.currentRegion(), 0.0, 1.0) && p.historySize() == steps2 + 1 && chips.chip ("Use the whole file") == nullptr,
           "Use the whole file: the Region is 0 .. 1 (one undo step) and its chip goes");
    p.undo();
    editor.refreshNow();
    check (isRegion (p.currentRegion(), other.start, other.end), "Undo puts the part back");

    // A root suggestion belongs to the Region it was heard in (fix pass T7-2): back on the hit it comes again, and another
    // part's chip hides it at once, before anything is heard in the new Region.
    p.waitForAnalysis (30000);
    editor.refreshNow();
    const bool again = p.getSuggestions().root.has_value() && p.showsRootSuggestion();
    const auto elsewhere = parts[0].label == other.label ? parts[1] : parts[0];
    const bool movedAway = click (chips, juce::String::fromUTF8 (elsewhere.label.c_str()));
    editor.refreshNow();
    bool rootChip = false;
    for (const auto& t : chips.texts())
        rootChip = rootChip || (t.startsWith ("Use ") && t.endsWith (" as the root"));
    check (again && movedAway && isRegion (p.currentRegion(), elsewhere.start, elsewhere.end) && ! p.showsRootSuggestion()
               && ! p.getSuggestions().root.has_value() && ! rootChip,
           fmt ("the hit's suggestion shows again on the hit (%s); a click on \"%s\" moves the Region and hides it (and its chip) at once",
                again ? "yes" : "NO", elsewhere.label.c_str()));

    // A chip's hover draws its box on the waveform and says what it is.
    chips.showHover (other);
    check (chips.hoveredPart().has_value() && view.getHintBar().getHelp().contains (juce::String::fromUTF8 (other.label.c_str())),
           fmt ("hovering a part chip says what it is: \"%s\"", view.getHintBar().getHelp().toRawUTF8()));
    chips.showHover (std::nullopt);
    dir.deleteRecursively();
}

SL_TEST ("ui", suggestionLineDoesWhatItSays)
{
    section ("The suggestion line (decision 2): root (no undo step), key (one step), Loop cleanly (one step), the engine (one step); "
             "none applies itself");

    const auto dir = songs::tempFolder ("suggestions");
    const auto tone = dir.getChildFile ("tone.wav");
    check (songs::writeMono16 (tone, signals::tone (signals::midiHz (45.23), 2.0, 44100.0), 44100.0), "a 2 s tone at A1 +23 cents");

    StrandLockProcessor p;
    resetUserSettings (p);
    p.waitForSounds();
    StrandLockEditor editor (p);
    auto& strip = editor.getView().getStrip();
    strip.loadFile (tone);
    p.waitForSounds (30000);
    editor.refreshNow();
    auto& chips = strip.getChips();
    const auto texts = chips.texts();
    check (texts.contains ("Use A1 as the root") && texts.contains ("Use key A major") && texts.contains ("Loop cleanly")
               && texts.contains ("Voice suits this sound (on)") && juce::exactlyEqual (songs::plainOf (p, params::P::rootNote), 60.0f),
           fmt ("a short tone: %s (the root is still C3)", texts.joinIntoString (" | ").toRawUTF8()));
    check (strip.headerText().contains ("key A major"), fmt ("the file line: \"%s\"", strip.headerText().toRawUTF8()));

    const auto steps0 = p.historySize();
    const bool rootClicked = click (chips, "Use A1 as the root");
    editor.refreshNow();
    check (rootClicked && juce::exactlyEqual (songs::plainOf (p, params::P::rootNote), 45.0f) && juce::exactlyEqual (songs::plainOf (p, params::P::rootCents), 23.0f)
               && p.historySize() == steps0 && chips.chip ("Use A1 as the root") == nullptr,
           "Use A1 as the root: root A1, +23 cents, no undo step, and the chip goes");

    const bool keyClicked = click (chips, "Use key A major");
    editor.refreshNow();
    check (keyClicked && p.historySize() == steps0 + 1 && p.getInstState().key == 9 && chips.chip ("Use key A major") == nullptr
               && editor.getView().getSoundTab().getKeyBox().getText() == "A",
           "Use key A major: one undo step, the chip goes, the Sound tab's Key shows A");

    const auto before = p.currentRegion();
    const bool trimClicked = click (chips, "Loop cleanly");
    editor.refreshNow();
    const auto after = p.currentRegion();
    check (trimClicked && p.historySize() == steps0 + 2 && after.second < before.second && after.second > 0.9
               && p.undoLabel().value_or ("") == "Trim the Region",
           fmt ("Loop cleanly pulls the Region end in to %.4f (one undo step \"Trim the Region\")", after.second));

    const bool noteClicked = click (chips, "Voice suits this sound (on)");
    editor.refreshNow();
    check (noteClicked && chips.chip ("Voice suits this sound (on)") == nullptr && p.historySize() == steps0 + 2,
           "the engine note (Voice is on already) hides on a click and changes nothing");

    // Noise: no pitch, so Cloud is suggested; the button switches the engine as one undo step.
    const auto noise = dir.getChildFile ("noise.wav");
    songs::writeMono16 (noise, signals::noise (2.0, 44100.0, 3.0, 0.5), 44100.0);
    strip.loadFile (noise);
    p.waitForSounds (30000);
    editor.refreshNow();
    const auto steps1 = p.historySize();
    check (chips.chip ("Try Cloud") != nullptr && chips.chip ("Use key E minor") == nullptr && strip.headerText().endsWith ("no clear pitch"),
           fmt ("white noise: \"Try Cloud\", no key offered, the file line says no clear pitch (%s)", chips.texts().joinIntoString (" | ").toRawUTF8()));
    click (chips, "Try Cloud");
    editor.refreshNow();
    check (p.readInst().engine == core::Engine::cloud && p.historySize() == steps1 + 1 && p.undoLabel().value_or ("") == "Engine: Cloud (suggested)"
               && chips.chip ("Try Cloud") == nullptr,
           fmt ("Try Cloud: the engine is Cloud, one undo step \"%s\", and the chip goes", p.undoLabel().value_or ("").c_str()));
    dir.deleteRecursively();
}

SL_TEST ("ui", starterPartsChip)
{
    section ("A starter's suggestions: one \"Parts\" chip beside the file line; its menu sets the Region to a part in one undo step");

    StrandLockProcessor p;
    resetUserSettings (p);
    p.waitForSounds();
    p.waitForAnalysis();   // the starter's parts, made right after it was handed on
    StrandLockEditor editor (p);
    editor.refreshNow();
    auto& strip = editor.getView().getStrip();
    auto& chips = strip.getChips();
    const auto& parts = p.getSuggestions().analysis.parts;
    check (! parts.empty() && chips.texts() == juce::StringArray ("Parts") && ! strip.hasChipRow()
               && chips.getBounds().getY() < strip.getWaveArea().getY(),
           fmt ("the Saw that opens up: %d parts behind one \"Parts\" chip, beside the file line (no row of its own)", (int) parts.size()));
    if (parts.empty())
        return;
    const auto steps = p.historySize();
    chips.choosePart (0);
    editor.refreshNow();
    check (isRegion (p.currentRegion(), parts[0].start, parts[0].end) && p.historySize() == steps + 1
               && chips.texts() == juce::StringArray (juce::String::fromUTF8 (parts[0].label.c_str())),
           fmt ("its menu's first part sets the Region (one undo step), and the chip reads \"%s\"", chips.texts()[0].toRawUTF8()));
}
