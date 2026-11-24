// M8 W1's controls on screen (PLUGIN-PLAN 7.10 decisions 7 and 8):
// - the Slides tab's "Hold over the loop end", the clip's tieAcrossLoop (F3): lit for a new clip; a click is one undo
//   step that changes that one clip setting and nothing else, and goes to the engine; Undo and Redo bring it back;
// - the Sound tab's Vibrato sync, beside Vibrato speed in the knob row: Free and the 21 rates, a pick is one host gesture
//   and one undo step ("Vibrato sync 1/8"); while a rate sets the speed the knob fades and the readout says the rate in Hz
//   at the host's tempo; the list reads "1 bar" as "4 beats" when the clip's bars hold another number of beats; the
//   knob's own change is one undo step "Vibrato speed", as the prototype's commit;
// - pictures: the Sound tab synced and the Slides tab with the switch off (dark and light).

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "model/Clip.h"
#include "model/ClipCodec.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/MainView.h"
#include "ui/Theme.h"
#include "ui/roll/RollView.h"
#include "ui/tabs/SlidesTab.h"
#include "ui/tabs/SoundTab.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;
    using ui::MainView;

    struct EditorRig
    {
        StrandLockProcessor proc;
        std::unique_ptr<StrandLockEditor> editor;

        explicit EditorRig (ui::ThemeId theme = ui::ThemeId::dark, int width = 1100, int height = 680)
        {
            proc.waitForSounds();
            auto& s = proc.getUserSettings();
            s.setTheme (theme);
            s.setUiScalePercent (100);
            s.setKeysDefault (false);
            editor = std::make_unique<StrandLockEditor> (proc);
            editor->setLogicalSize (width, height);
            view().getRoll().setMenusShown (false);
            editor->refreshNow();
        }

        ~EditorRig()
        {
            editor.reset();
            proc.getUserSettings().setTheme (state::Theme::dark);
        }

        EditorRig (const EditorRig&) = delete;
        EditorRig& operator= (const EditorRig&) = delete;

        MainView& view() { return editor->getView(); }
        model::ClipPtr clip() { return proc.getDocument().current(); }
        std::uint64_t submits() { return proc.getSnapshotPublisher().stats().submitted; }
        juce::Image picture() { return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f); }
    };

    struct ParamWatch final : juce::AudioProcessorParameter::Listener
    {
        explicit ParamWatch (juce::AudioProcessorParameter& p) : param (p) { param.addListener (this); }
        ~ParamWatch() override { param.removeListener (this); }
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends) += 1; }

        juce::AudioProcessorParameter& param;
        int begins = 0, ends = 0;
    };

    float plainOf (StrandLockProcessor& proc, params::P p) { return params::plainValueOf (proc.getParam (p)); }

    /** The clip with one note, for the pictures. */
    model::Clip oneNoteClip()
    {
        model::Clip c = model::newClip (4);
        c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (60, 2, 4, "n1")));
        return c;
    }

    juce::Image slidesHoldOff (ui::ThemeId theme)
    {
        EditorRig e (theme);
        e.proc.editClip ("Start", oneNoteClip());
        e.view().openTab (MainView::slidesTab);
        e.editor->refreshNow();
        e.view().getSlidesTab().getHoldButton().onClick();
        e.editor->refreshNow();
        return e.picture();
    }

    juce::Image soundSynced (ui::ThemeId theme)
    {
        EditorRig e (theme);
        e.view().openTab (MainView::soundTab);
        e.editor->refreshNow();
        e.view().getSoundTab().getVibratoSync().getChoice().getBox().setSelectedItemIndex (10, juce::sendNotificationSync);   // 1/8
        e.editor->refreshNow();
        return e.picture();
    }
}

SL_TEST ("ui", holdOverLoopEndSwitch)
{
    section ("The Slides tab's \"Hold over the loop end\" (F3): the clip's tieAcrossLoop, lit for a new clip, one undo step a click");

    EditorRig e;
    e.proc.editClip ("Start", oneNoteClip());
    auto& view = e.view();
    view.openTab (MainView::slidesTab);
    e.editor->refreshNow();
    auto& tab = view.getSlidesTab();
    auto& hold = tab.getHoldButton();
    const auto before = e.clip();
    const bool litAtStart = before->tieAcrossLoop && hold.getToggleState();
    // after Jump, no slide in the flow: to its right on its row, or first on the next row when the window is narrow
    const auto& jump = tab.getJumpButton();
    const bool afterJump = (hold.getY() == jump.getY() && hold.getX() > jump.getRight()) || hold.getY() > jump.getBottom();
    const bool onScreen = hold.isVisible() && hold.getRight() <= tab.getWidth() && hold.getBottom() <= tab.getHeight() && afterJump;
    check (litAtStart && onScreen, fmt ("a new clip ties across the loop and the switch is lit, after Jump, no slide (x %d, y %d; Jump %d, %d)",
                                        hold.getX(), hold.getY(), jump.getRight(), jump.getY()));

    const auto submits0 = e.submits();
    hold.onClick();
    e.editor->refreshNow();
    const auto off = e.clip();
    model::Clip expected = *before;
    expected.tieAcrossLoop = false;
    check (! off->tieAcrossLoop && ! hold.getToggleState() && e.proc.undoLabel().value_or ("") == "Hold over the loop end off"
               && model::encodeClip (*off) == model::encodeClip (expected) && e.submits() > submits0,
           fmt ("a click turns it off: one undo step \"%s\", only that clip setting changed (the bytes of the clip with it off), and the "
                "clip went to the engine (%llu submitted)", e.proc.undoLabel().value_or ("").c_str(), (unsigned long long) (e.submits() - submits0)));

    view.getRoll().undo();
    e.editor->refreshNow();
    const bool backOn = e.clip()->tieAcrossLoop && hold.getToggleState() && e.proc.undoLabel().value_or ("") == "Start";
    view.getRoll().redo();
    e.editor->refreshNow();
    check (backOn && ! e.clip()->tieAcrossLoop && ! hold.getToggleState(), "Undo puts it back on (the switch lit again), Redo off again");

    hold.onClick();
    e.editor->refreshNow();
    check (e.clip()->tieAcrossLoop && hold.getToggleState() && e.proc.undoLabel().value_or ("") == "Hold over the loop end on",
           "a second click turns it on: one undo step \"Hold over the loop end on\"");
}

SL_TEST ("ui", vibratoSyncBesideSpeed)
{
    section ("The Sound tab's Vibrato sync beside Vibrato speed: one pick, one gesture, one undo step; the knob fades while synced; "
             "the readout; \"4 beats\" when a bar holds another number of beats");

    EditorRig e;
    auto& view = e.view();
    view.openTab (MainView::soundTab);
    e.editor->refreshNow();
    auto& tab = view.getSoundTab();
    auto& speed = tab.getVibratoSpeed();
    auto& sync = tab.getVibratoSync();
    auto& box = sync.getChoice().getBox();

    const bool besideIt = sync.isVisible() && sync.getY() == speed.getY() && sync.getHeight() == speed.getHeight() && sync.getX() > speed.getRight()
                       && sync.getX() - speed.getRight() <= 12;
    check (besideIt, fmt ("Vibrato sync sits in the knob row right after Vibrato speed (speed %d..%d, sync from %d, y %d and %d)", speed.getX(),
                          speed.getRight(), sync.getX(), speed.getY(), sync.getY()));
    check (box.getNumItems() == 22 && box.getItemText (0) == "Free" && box.getItemText (1) == "1 bar" && box.getItemText (10) == "1/8"
               && box.getItemText (21) == "1/64 dotted" && box.getSelectedItemIndex() == 0 && juce::exactlyEqual (speed.getAlpha(), 1.0f)
               && sync.getReadout() == "5.5 Hz, the knob",
           fmt ("the list: Free and the 21 rates (\"%s\" .. \"%s\"), Free picked, the knob lit, the readout \"%s\"", box.getItemText (1).toRawUTF8(),
                box.getItemText (21).toRawUTF8(), sync.getReadout().toRawUTF8()));

    ParamWatch w (e.proc.getParam (params::P::vibSync));
    box.setSelectedItemIndex (10, juce::sendNotificationSync);
    e.editor->refreshNow();
    check (juce::exactlyEqual (plainOf (e.proc, params::P::vibSync), 10.0f) && w.begins == 1 && w.ends == 1
               && e.proc.undoLabel().value_or ("") == "Vibrato sync 1/8" && speed.getAlpha() < 1.0f && sync.getReadout() == "4.00 Hz at 120 bpm",
           fmt ("1/8 picked: the parameter 10 in one gesture (%d / %d), one undo step \"%s\", the speed knob faded (%.2f), \"%s\"", w.begins, w.ends,
                e.proc.undoLabel().value_or ("").c_str(), (double) speed.getAlpha(), sync.getReadout().toRawUTF8()));

    tab.refresh (90.0);
    check (sync.getReadout() == "3.00 Hz at 90 bpm", fmt ("the host at 90 bpm: \"%s\"", sync.getReadout().toRawUTF8()));

    view.getRoll().undo();
    e.editor->refreshNow();
    check (juce::exactlyEqual (plainOf (e.proc, params::P::vibSync), 0.0f) && box.getSelectedItemIndex() == 0 && juce::exactlyEqual (speed.getAlpha(), 1.0f),
           "Undo: Free again, the knob lit");

    // the knob's own change is one undo step when it is let go (sl-ui.js commit ('Vibrato speed'))
    params::setPlainValue (e.proc.getParam (params::P::vibRate), params::index (params::P::vibRate), 7.0f);
    if (speed.onGestureEnd)
        speed.onGestureEnd();
    check (e.proc.undoLabel().value_or ("") == "Vibrato speed", fmt ("the Vibrato speed knob let go: one undo step \"%s\"", e.proc.undoLabel().value_or ("").c_str()));

    // a clip in 3/4: "1 bar" reads "4 beats" (rates are quarter-note beats); back in 4/4, "1 bar"
    model::Clip threeFour = model::newClip (4);
    threeFour.beatsPerBar = 3;
    e.proc.editClip ("3/4", threeFour);
    e.editor->refreshNow();
    const bool fourBeats = box.getItemText (1) == "4 beats" && box.getItemText (2) == "4 beats triplet" && box.getItemText (3) == "4 beats dotted"
                        && box.getItemText (10) == "1/8" && box.getNumItems() == 22;
    model::Clip fourFour = model::newClip (4);
    e.proc.editClip ("4/4", fourFour);
    e.editor->refreshNow();
    check (fourBeats && box.getItemText (1) == "1 bar" && box.getItemText (3) == "1 bar dotted",
           "in 3/4 the list reads \"4 beats\", \"4 beats triplet\", \"4 beats dotted\"; in 4/4 \"1 bar\" again");
}

SL_SNAPSHOT (w1_slides_hold_off_dark_100)   { return slidesHoldOff (ui::ThemeId::dark); }
SL_SNAPSHOT (w1_slides_hold_off_light_100)  { return slidesHoldOff (ui::ThemeId::light); }
SL_SNAPSHOT (w1_sound_synced_dark_100)      { return soundSynced (ui::ThemeId::dark); }
SL_SNAPSHOT (w1_sound_synced_light_100)     { return soundSynced (ui::ThemeId::light); }
