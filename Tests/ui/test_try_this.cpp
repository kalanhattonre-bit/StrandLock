// "Try this" (PLUGIN-PLAN 4 M6, 7.8 T2 decision 6; U3; sl-ui-panels.js updateTry):
//
// - one suggestion at a time from the prototype's list: 75 cases (every lane mode; in Sample mode every Sample lane,
//   every engine and Cloud with Lock, every Motion before and after its tip is done; a primary note's own engine, lock
//   and motion) give the prototype's own text. The table below was made by running sl-ui-panels.js's own updateTry
//   (its MODE_TIPS .. THEN_TIPS, read from the file) in node over these cases;
// - it never covers the hint bar's mode line: at every window width the suggestion has at most half the bar and the
//   mode's line the rest, and a cut suggestion shows whole over the right half only;
// - in the editor the bar follows the mode, the Sample lane, the Motion and the primary note.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/Inst.h"
#include "core/Lanes.h"
#include "model/Clip.h"
#include "model/NoteSettings.h"
#include "params/ParamBridge.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/HintBar.h"
#include "ui/MainView.h"
#include "ui/Theme.h"

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    /** One case: the mode and Sample lane; the primary note's own engine, lock and motion (-1: none, follows the
        instrument; hasPrimary false: no primary note); the instrument's engine (0 Tape, 1 Voice, 2 Cloud), lock, motion
        (0 Forward .. 5 Scatter), heads, spray, Head tune (true: Detune, else Same), drift, Jump every (true: 1/8 triplet,
        else 1/8); and the prototype's text. */
    struct TryCase
    {
        int mode;
        const char* sub;
        int noteEngine, noteLock, noteMotion;
        bool hasPrimary;
        int engine;
        bool lock;
        int motion;
        double heads, spray;
        bool detune;
        double drift;
        bool jump18T;
        const char* text;
    };

    const TryCase kTryCases[] = {
        { 1, "travel", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "draw a chord, drag a box around it, then Tools \xe2" "\x96" "\xb8" " Strum \xe2" "\x96" "\xb8" " Strum up." },
        { 2, "travel", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "select a held note, choose Shapes \xe2" "\x96" "\xb8" " Ramp up, then drag the small \xe2" "\x97" "\x87" " to curve the rise." },
        { 3, "travel", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "Pattern \xe2" "\x96" "\xb8" " Square at 1/16 Triplet with both depths near 80%: an instant rhythmic gate." },
        { 4, "travel", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "Shapes \xe2" "\x96" "\xb8" " Ramp up, so the vibrato grows across each note like a singer's." },
        { 5, "travel", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "Pattern \xe2" "\x96" "\xb8" " Squiggle at 1/16 Triplet with Lock to the beat: a wah that grooves with the bar." },
        { 6, "travel", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "select a phrase, choose Across, then Shapes \xe2" "\x96" "\xb8" " Ramp down: one long aah to ooh over all the notes." },
        { 7, "travel", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "Shapes \xe2" "\x96" "\xb8" " Swell: each note goes hollow in the middle, then opens again." },
        { 8, "travel", -1, -1, -1, false, 0, false, 0, 1, 0, false, 0, false, "Forward: draw the Travel line below the middle on one note, and that note plays backwards." },
        { 8, "travel", -1, -1, -1, false, 0, false, 0, 4, 0.2, true, 0.3, true, "Forward: draw the Travel line below the middle on one note, and that note plays backwards." },
        { 8, "travel", -1, -1, -1, false, 0, false, 1, 1, 0, false, 0, false, "Backward: add Shapes \xe2" "\x96" "\xb8" " Swell on Volume, so each note fades in like reversed tape." },
        { 8, "travel", -1, -1, -1, false, 0, false, 1, 4, 0.2, true, 0.3, true, "Backward: add Shapes \xe2" "\x96" "\xb8" " Swell on Volume, so each note fades in like reversed tape." },
        { 8, "travel", -1, -1, -1, false, 0, false, 2, 1, 0, false, 0, false, "Bounce: raise Heads to 3 and add a little Drift; the heads meet and pass each other." },
        { 8, "travel", -1, -1, -1, false, 0, false, 2, 4, 0.2, true, 0.3, true, "Bounce: raise Heads to 3 and add a little Drift; the heads meet and pass each other." },
        { 8, "travel", -1, -1, -1, false, 0, false, 3, 1, 0, false, 0, false, "Mirror: give it 2 or more Heads; every other head runs backwards, so the sound breathes both ways." },
        { 8, "travel", -1, -1, -1, false, 0, false, 3, 4, 0.2, true, 0.3, true, "Mirror: give it 2 or more Heads; every other head runs backwards, so the sound breathes both ways." },
        { 8, "travel", -1, -1, -1, false, 0, false, 4, 1, 0, false, 0, false, "Freeze does nothing in Tape (it plays forwards): pick Voice or Cloud on the Sample tab, then raise Heads to 4." },
        { 8, "travel", -1, -1, -1, false, 0, false, 4, 4, 0.2, true, 0.3, true, "Freeze does nothing in Tape (it plays forwards): pick Voice or Cloud on the Sample tab, then raise Heads to 4." },
        { 8, "travel", -1, -1, -1, false, 0, false, 5, 1, 0, false, 0, false, "Scatter: set Jump every to 1/8 Triplet, then raise Heads to 3." },
        { 8, "travel", -1, -1, -1, false, 0, false, 5, 4, 0.2, true, 0.3, true, "Scatter: press Re-roll on the Sample tab for new jump spots (the rhythm of the notes stays)." },
        { 8, "travel", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "Forward: draw the Travel line below the middle on one note, and that note plays backwards." },
        { 8, "travel", -1, -1, -1, false, 1, false, 0, 4, 0.2, true, 0.3, true, "Forward: draw the Travel line below the middle on one note, and that note plays backwards." },
        { 8, "travel", -1, -1, -1, false, 1, false, 1, 1, 0, false, 0, false, "Backward: add Shapes \xe2" "\x96" "\xb8" " Swell on Volume, so each note fades in like reversed tape." },
        { 8, "travel", -1, -1, -1, false, 1, false, 1, 4, 0.2, true, 0.3, true, "Backward: add Shapes \xe2" "\x96" "\xb8" " Swell on Volume, so each note fades in like reversed tape." },
        { 8, "travel", -1, -1, -1, false, 1, false, 2, 1, 0, false, 0, false, "Bounce: raise Heads to 3 and add a little Drift; the heads meet and pass each other." },
        { 8, "travel", -1, -1, -1, false, 1, false, 2, 4, 0.2, true, 0.3, true, "Bounce: drag the Region smaller on the waveform, so the heads turn round more often." },
        { 8, "travel", -1, -1, -1, false, 1, false, 3, 1, 0, false, 0, false, "Mirror: give it 2 or more Heads; every other head runs backwards, so the sound breathes both ways." },
        { 8, "travel", -1, -1, -1, false, 1, false, 3, 4, 0.2, true, 0.3, true, "Mirror: raise Spread on the Sample tab, so the forward and backward heads read different spots." },
        { 8, "travel", -1, -1, -1, false, 1, false, 4, 1, 0, false, 0, false, "Freeze: raise Heads to 4 and set Head tune to Detune for a frozen choir (pick Cloud to add Spray too)." },
        { 8, "travel", -1, -1, -1, false, 1, false, 4, 4, 0.2, true, 0.3, true, "Freeze: select a few notes and click a different spot on the waveform for each: every note holds its own moment of the sound." },
        { 8, "travel", -1, -1, -1, false, 1, false, 5, 1, 0, false, 0, false, "Scatter: set Jump every to 1/8 Triplet, then Random on Grain." },
        { 8, "travel", -1, -1, -1, false, 1, false, 5, 4, 0.2, true, 0.3, true, "Scatter: press Re-roll on the Sample tab for new jump spots (the rhythm of the notes stays)." },
        { 8, "travel", -1, -1, -1, false, 2, false, 0, 1, 0, false, 0, false, "Forward: draw the Travel line below the middle on one note, and that note plays backwards." },
        { 8, "travel", -1, -1, -1, false, 2, false, 0, 4, 0.2, true, 0.3, true, "Forward: draw the Travel line below the middle on one note, and that note plays backwards." },
        { 8, "travel", -1, -1, -1, false, 2, false, 1, 1, 0, false, 0, false, "Backward: add Shapes \xe2" "\x96" "\xb8" " Swell on Volume, so each note fades in like reversed tape." },
        { 8, "travel", -1, -1, -1, false, 2, false, 1, 4, 0.2, true, 0.3, true, "Backward: add Shapes \xe2" "\x96" "\xb8" " Swell on Volume, so each note fades in like reversed tape." },
        { 8, "travel", -1, -1, -1, false, 2, false, 2, 1, 0, false, 0, false, "Bounce: raise Heads to 3 and add a little Drift; the heads meet and pass each other." },
        { 8, "travel", -1, -1, -1, false, 2, false, 2, 4, 0.2, true, 0.3, true, "Bounce: drag the Region smaller on the waveform, so the heads turn round more often." },
        { 8, "travel", -1, -1, -1, false, 2, false, 3, 1, 0, false, 0, false, "Mirror: give it 2 or more Heads; every other head runs backwards, so the sound breathes both ways." },
        { 8, "travel", -1, -1, -1, false, 2, false, 3, 4, 0.2, true, 0.3, true, "Mirror: raise Spread on the Sample tab, so the forward and backward heads read different spots." },
        { 8, "travel", -1, -1, -1, false, 2, false, 4, 1, 0, false, 0, false, "Freeze: raise Heads to 4, then add Spray." },
        { 8, "travel", -1, -1, -1, false, 2, false, 4, 4, 0.2, true, 0.3, true, "Freeze: select a few notes and click a different spot on the waveform for each: every note holds its own moment of the sound." },
        { 8, "travel", -1, -1, -1, false, 2, false, 5, 1, 0, false, 0, false, "Scatter: set Jump every to 1/8 Triplet, then Random on Grain." },
        { 8, "travel", -1, -1, -1, false, 2, false, 5, 4, 0.2, true, 0.3, true, "Scatter: press Re-roll on the Sample tab for new jump spots (the rhythm of the notes stays)." },
        { 8, "travel", -1, -1, -1, false, 2, true, 0, 1, 0, false, 0, false, "Forward: draw the Travel line below the middle on one note, and that note plays backwards." },
        { 8, "travel", -1, -1, -1, false, 2, true, 0, 4, 0.2, true, 0.3, true, "Forward: draw the Travel line below the middle on one note, and that note plays backwards." },
        { 8, "travel", -1, -1, -1, false, 2, true, 1, 1, 0, false, 0, false, "Backward: add Shapes \xe2" "\x96" "\xb8" " Swell on Volume, so each note fades in like reversed tape." },
        { 8, "travel", -1, -1, -1, false, 2, true, 1, 4, 0.2, true, 0.3, true, "Backward: add Shapes \xe2" "\x96" "\xb8" " Swell on Volume, so each note fades in like reversed tape." },
        { 8, "travel", -1, -1, -1, false, 2, true, 2, 1, 0, false, 0, false, "Bounce: raise Heads to 3 and add a little Drift; the heads meet and pass each other." },
        { 8, "travel", -1, -1, -1, false, 2, true, 2, 4, 0.2, true, 0.3, true, "Bounce: drag the Region smaller on the waveform, so the heads turn round more often." },
        { 8, "travel", -1, -1, -1, false, 2, true, 3, 1, 0, false, 0, false, "Mirror: give it 2 or more Heads; every other head runs backwards, so the sound breathes both ways." },
        { 8, "travel", -1, -1, -1, false, 2, true, 3, 4, 0.2, true, 0.3, true, "Mirror: raise Spread on the Sample tab, so the forward and backward heads read different spots." },
        { 8, "travel", -1, -1, -1, false, 2, true, 4, 1, 0, false, 0, false, "Freeze: raise Heads to 4, then add Spray." },
        { 8, "travel", -1, -1, -1, false, 2, true, 4, 4, 0.2, true, 0.3, true, "Freeze: select a few notes and click a different spot on the waveform for each: every note holds its own moment of the sound." },
        { 8, "travel", -1, -1, -1, false, 2, true, 5, 1, 0, false, 0, false, "Scatter: set Jump every to 1/8 Triplet, then Random on Grain." },
        { 8, "travel", -1, -1, -1, false, 2, true, 5, 4, 0.2, true, 0.3, true, "Scatter: press Re-roll on the Sample tab for new jump spots (the rhythm of the notes stays)." },
        { 8, "heads", -1, -1, -1, false, 0, false, 0, 1, 0, false, 0, false, "Shapes \xe2" "\x96" "\xb8" " Ramp up on Heads: each note blooms from one playhead to many." },
        { 8, "heads", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "Shapes \xe2" "\x96" "\xb8" " Ramp up on Heads: each note blooms from one playhead to many." },
        { 8, "heads", -1, -1, -1, false, 2, false, 0, 1, 0, false, 0, false, "Shapes \xe2" "\x96" "\xb8" " Ramp up on Heads: each note blooms from one playhead to many." },
        { 8, "heads", -1, -1, -1, false, 2, true, 0, 1, 0, false, 0, false, "Shapes \xe2" "\x96" "\xb8" " Ramp up on Heads: each note blooms from one playhead to many." },
        { 8, "grain", -1, -1, -1, false, 0, false, 0, 1, 0, false, 0, false, "Grain does nothing in Tape: pick Voice or Cloud on the Sample tab, then try Random at 1/16 on Grain." },
        { 8, "grain", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "Random at 1/16 with Smooth near 50% on Grain: grains that breathe." },
        { 8, "grain", -1, -1, -1, false, 2, false, 0, 1, 0, false, 0, false, "Random at 1/16 with Smooth near 50% on Grain: grains that breathe." },
        { 8, "grain", -1, -1, -1, false, 2, true, 0, 1, 0, false, 0, false, "Random at 1/16 with Smooth near 50% on Grain: grains that breathe." },
        { 8, "refresh", -1, -1, -1, false, 0, false, 0, 1, 0, false, 0, false, "set Refresh to Synced on the Sample tab, then Shapes \xe2" "\x96" "\xb8" " Ramp up here for a stutter that speeds up." },
        { 8, "refresh", -1, -1, -1, false, 1, false, 0, 1, 0, false, 0, false, "set Refresh to Synced on the Sample tab, then Shapes \xe2" "\x96" "\xb8" " Ramp up here for a stutter that speeds up." },
        { 8, "refresh", -1, -1, -1, false, 2, false, 0, 1, 0, false, 0, false, "set Refresh to Synced on the Sample tab, then Shapes \xe2" "\x96" "\xb8" " Ramp up here for a stutter that speeds up." },
        { 8, "refresh", -1, -1, -1, false, 2, true, 0, 1, 0, false, 0, false, "set Refresh to Synced on the Sample tab, then Shapes \xe2" "\x96" "\xb8" " Ramp up here for a stutter that speeds up." },
        { 8, "travel", 0, -1, 4, true, 2, false, 0, 1, 0, false, 0, false, "Freeze does nothing in Tape (it plays forwards): pick Voice or Cloud on the Sample tab, then raise Heads to 4." },
        { 8, "travel", -1, -1, 4, true, 1, false, 5, 1, 0, false, 0, false, "Freeze: raise Heads to 4 and set Head tune to Detune for a frozen choir (pick Cloud to add Spray too)." },
        { 8, "travel", 2, 1, 4, true, 1, false, 0, 4, 0.2, true, 0.3, true, "Freeze: select a few notes and click a different spot on the waveform for each: every note holds its own moment of the sound." },
        { 8, "travel", 2, 0, -1, true, 0, false, 4, 4, 0.2, true, 0.3, true, "Freeze: select a few notes and click a different spot on the waveform for each: every note holds its own moment of the sound." },
        { 8, "grain", 0, -1, -1, true, 1, false, 0, 1, 0, false, 0, false, "Grain does nothing in Tape: pick Voice or Cloud on the Sample tab, then try Random at 1/16 on Grain." },
        { 8, "grain", 1, -1, -1, true, 0, false, 0, 1, 0, false, 0, false, "Random at 1/16 with Smooth near 50% on Grain: grains that breathe." },
        { 8, "travel", -1, 1, 3, true, 2, false, 0, 2, 0, false, 0, false, "Mirror: raise Spread on the Sample tab, so the forward and backward heads read different spots." },
        { 8, "travel", 0, -1, 3, true, 2, false, 0, 2, 0, false, 0, false, "Mirror: give it 2 or more Heads; every other head runs backwards, so the sound breathes both ways." },
    };

    void resetUserSettings (StrandLockProcessor& p)
    {
        auto& s = p.getUserSettings();
        s.setTheme (state::Theme::dark);
        s.setUiScalePercent (100);
        s.setKeysDefault (false);
    }

    core::Inst instOf (const TryCase& c)
    {
        core::Inst I = core::defaultInst();
        I.engine = (core::Engine) c.engine;
        I.lock = c.lock;
        I.motion = (core::Motion) c.motion;
        I.heads = c.heads;
        I.spray = c.spray;
        I.headTune = c.detune ? core::HeadTune::detune : core::HeadTune::same;
        I.drift = c.drift;
        I.jump = c.jump18T ? core::RateId::r1_8T : core::RateId::r1_8;
        return I;
    }

    model::Note primaryOf (const TryCase& c)
    {
        auto n = model::mkNote (60.0, 0.0, 1.0, "p");
        if (c.noteEngine >= 0)
            n.sample.engine = (core::Engine) c.noteEngine;
        if (c.noteLock >= 0)
            n.sample.lock = c.noteLock == 1;
        if (c.noteMotion >= 0)
            n.sample.motion = (core::Motion) c.noteMotion;
        return n;
    }

    /** The prototype's text for a case with no primary note and the instrument's defaults but these (heads 1: its tip
        not done yet). */
    juce::String prototypeText (int mode, const char* sub, int engine, bool lock, int motion)
    {
        for (const auto& c : kTryCases)
            if (c.mode == mode && std::strcmp (c.sub, sub) == 0 && ! c.hasPrimary && c.engine == engine && c.lock == lock && c.motion == motion
                && c.heads < 1.5)
                return juce::String::fromUTF8 (c.text);
        return {};
    }
}

//==================================================================================================
SL_TEST ("ui", tryThisIsThePrototypes)
{
    section ("Try this: one suggestion at a time, the prototype's own, for the mode, the Sample lane, the engine and the Motion (M6)");

    int right = 0, total = 0;
    std::string firstBad;
    juce::StringArray distinct;
    for (const auto& c : kTryCases)
    {
        ++total;
        const auto lane = core::findLane (c.sub);
        const auto P = primaryOf (c);
        const auto got = ui::tryThis (c.mode, lane.value_or (core::LaneId::travel), c.hasPrimary ? &P : nullptr, instOf (c));
        const auto want = juce::String::fromUTF8 (c.text);
        if (got == want)
            ++right;
        else if (firstBad.empty())
            firstBad = fmt ("mode %d %s (engine %d, motion %d, primary %d): \"%s\", the prototype's \"%s\"", c.mode, c.sub, c.engine, c.motion,
                            (int) c.hasPrimary, got.toRawUTF8(), want.toRawUTF8());
        distinct.addIfNotAlreadyThere (want);
    }
    check (right == total && total == 75, fmt ("%d of %d cases give the prototype's suggestion%s%s", right, total, firstBad.empty() ? "" : "; first other: ",
                                               firstBad.c_str()));
    check (distinct.size() == 24, fmt ("the cases reach %d different suggestions (the prototype's list has 24 that these cases can show)", distinct.size()));
}

SL_TEST ("ui", tryThisNeverCoversTheModeLine)
{
    section ("Try this: never covers the hint bar's mode line; a suggestion cut short shows whole over the right half only (M6)");

    ui::Theme theme;
    juce::Component parent;
    ui::HintBar bar (theme);
    parent.setSize (2400, 400);
    parent.addAndMakeVisible (bar);
    parent.addChildComponent (bar.getHover());
    const auto longest = ui::modeHint (1, std::nullopt, false, false);
    int layouts = 0, apart = 0, cut = 0, rightHalf = 0;
    std::string firstBad;
    for (int w = 876; w <= 2376; w += 100)   // the bar of windows 900 to 2400 px wide
        for (const auto& c : kTryCases)
        {
            bar.setBounds (12, 300, w, 30);
            bar.setHint (longest);
            bar.setTryThis (juce::String::fromUTF8 (c.text));
            ++layouts;
            const auto h = bar.getHintArea(), t = bar.getTryArea();
            const bool ok = t.getWidth() > 0 && h.getRight() <= t.getX() && t.getWidth() <= w / 2 && h.getWidth() >= w / 2 - 2 * ui::HintBar::padX - 12;
            apart += ok ? 1 : 0;
            if (! ok && firstBad.empty())
                firstBad = fmt ("at %d px: the line %d .. %d, the suggestion %d .. %d", w, h.getX(), h.getRight(), t.getX(), t.getRight());
            if (bar.isTryCut())
            {
                ++cut;
                bar.showTryHover (true);
                const auto hb = bar.getHover().getBounds();
                rightHalf += bar.getHover().isVisible() && hb.getX() > bar.getX() + h.getRight() && hb.getRight() <= bar.getRight() + 1 ? 1 : 0;
                bar.showTryHover (false);
            }
        }
    check (apart == layouts, fmt ("%d of %d layouts keep the mode's line and the suggestion apart, the suggestion in at most half the bar%s%s", apart,
                                  layouts, firstBad.empty() ? "" : "; first other: ", firstBad.c_str()));
    check (cut > 0 && rightHalf == cut, fmt ("%d suggestions were cut short; %d of them show whole over the right half only, past the mode's line",
                                            cut, rightHalf));

    // the mode line's own hover covers the whole bar: the check above tells the two apart
    bar.setBounds (12, 300, 876, 30);
    bar.showHover (true);
    const auto whole = bar.getHover().getBounds();
    check (bar.isHintCut() && bar.getHover().isVisible() && whole.getX() <= bar.getX() && whole.getRight() >= bar.getRight(),
           "the mode line's own hover (a different thing) covers the whole bar");
    bar.showHover (false);
    check (! bar.getHover().isVisible(), "and goes when the mouse leaves");
}

SL_TEST ("ui", tryThisInTheEditor)
{
    section ("Try this: the editor's hint bar follows the mode, the Sample lane, the instrument and the primary note (M6)");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    proc.waitForSounds();
    {
        StrandLockEditor editor (proc);
        auto& view = editor.getView();
        const auto shown = [&view] { return view.getHintBar().getTryThis(); };

        view.setMode (3);
        editor.refreshNow();
        const auto volume = shown();
        view.setMode (1);
        editor.refreshNow();
        const auto build = shown();
        check (volume == prototypeText (3, "travel", 1, false, 0) && build == prototypeText (1, "travel", 1, false, 0) && volume != build,
               fmt ("Volume: \"%s\"; Build: \"%s\"", volume.toRawUTF8(), build.toRawUTF8()));

        // Sample mode on Travel, Freeze on Tape (the instrument's): Tape cannot freeze, so the tip says so
        core::InstPatch patch;
        patch.engine = core::Engine::tape;
        patch.motion = core::Motion::freeze;
        params::applyPatch (proc.getApvts(), patch);
        view.setMode (8, 0);
        editor.refreshNow();
        const auto tapeFreeze = shown();
        view.setMode (8, 2);
        editor.refreshNow();
        const auto tapeGrain = shown();
        check (tapeFreeze == prototypeText (8, "travel", 0, false, 4) && tapeGrain == prototypeText (8, "grain", 0, false, 0),
               fmt ("Freeze on Tape: \"%s\"; Grain on Tape: \"%s\"", tapeFreeze.toRawUTF8(), tapeGrain.toRawUTF8()));

        // the primary note plays Voice on its own: the Voice tip for Freeze
        const auto clip = proc.getDocument().current();
        const auto first = model::timeOrder (clip->notes).front()->id;
        model::SamplePatch own;
        own.engine.emplace (core::Engine::voice);
        proc.editClip ("Own engine", model::setNoteSample (*clip, std::vector<std::string> { first }, own));
        proc.getDocument().setSelection ({ first }, first);
        view.setMode (8, 0);
        view.getRoll().edited();
        editor.refreshNow();
        const auto voiceFreeze = shown();
        check (voiceFreeze == prototypeText (8, "travel", 1, false, 4) && voiceFreeze != tapeFreeze,
               fmt ("the primary note on Voice (the instrument on Tape): \"%s\"", voiceFreeze.toRawUTF8()));
    }
    resetUserSettings (proc);
}
