// The controls and their parameters (PLUGIN-PLAN 7.4 decision 1): no control is bound through JUCE's
// ParameterAttachment or the APVTS attachments. A control writes its parameter on the message thread inside a
// host gesture, and the editor's 30 Hz timer moves each control to its parameter's value when that changed,
// never while the user is changing it. (alloc/hostChangesWithEditorOpen checks the other half: the host moving
// parameters with the editor open allocates nothing.)

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParamIds.h"
#include "params/ParameterLayout.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "ui/tabs/SampleTab.h"
#include "ui/tabs/TabPanel.h"

#include <cmath>
#include <memory>

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

    struct ParamWatch final : juce::AudioProcessorParameter::Listener
    {
        explicit ParamWatch (juce::AudioProcessorParameter& p) : param (p) { param.addListener (this); }
        ~ParamWatch() override { param.removeListener (this); }
        void parameterValueChanged (int, float) override { ++changes; }
        void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends) += 1; }

        juce::AudioProcessorParameter& param;
        int begins = 0, ends = 0, changes = 0;
    };

    /** The host moves a parameter to a plain value, the way automation does (its normalised value). */
    void hostSets (juce::RangedAudioParameter& p, float plain)
    {
        p.setValueNotifyingHost (p.convertTo0to1 (plain));
    }

    template <class T>
    T* findControl (MainView& view, sl::params::P id)
    {
        for (auto* c : view.getSyncedControls())
            if (auto* t = dynamic_cast<T*> (c))
                if (t->getParameter().getParameterIndex() == sl::params::index (id))
                    return t;
        return nullptr;
    }
}

SL_TEST ("ui", controlsFollowTheTimerNotAttachments)
{
    section ("Controls: a host change shows on the 30 Hz timer, not at once (no attachment); never during a drag");

    StrandLockProcessor proc;
    resetUserSettings (proc);
    StrandLockEditor editor (proc);
    auto& view = editor.getView();

    // Every control bound to a parameter is in the timer's list: the four tabs' (M5's Slides tab: Auto slide and Slide
    // time) and the strip's At the end. The Sample tab's Grain knob (M6) shows three parameters (Voice, Cloud and Lock
    // grain, as the engine asks), so the timer has two controls fewer than bound parameters.
    int bound = 0;
    for (int tab : { MainView::macrosTab, MainView::sampleTab, MainView::soundTab, MainView::slidesTab })
        if (auto* panel = view.getTabPanel (tab))
            bound += panel->getBoundIds().size();
    check (bound == 13 + 25 + 9 + 2 && (int) view.getSyncedControls().size() == bound - 2 + 1,
           fmt ("the timer moves %d controls: the tabs' (%d parameters) and the strip's At the end", (int) view.getSyncedControls().size(), bound));
    check (view.syncControls() == 0, "with nothing changed, the timer moves nothing");

    // A knob: the host's change waits for the timer, then shows (an attachment would have moved it at once).
    auto& speed = view.getSampleTab().getKnob (ui::rules::KnobId::speed);
    auto& speedParam = proc.getParam (params::P::speed);
    hostSets (speedParam, 4.0f);
    const juce::String readBefore = speed.readout();
    const int moved = view.syncControls();
    check (readBefore == "1x" && speed.readout() == "4x" && moved == 1,
           fmt ("host Speed 4x: the knob still reads \"%s\" until the timer, then \"%s\" (%d control moved)", readBefore.toRawUTF8(),
                speed.readout().toRawUTF8(), moved));

    // While the user drags the knob, the timer leaves it alone; when the drag ends it catches up.
    {
        ParamWatch w (speedParam);
        speed.press (300.0);
        speed.dragTo (300.0 + 40.0, false);   // down: slower
        const float during = params::plainValueOf (speedParam);
        const auto readDuring = speed.readout();
        hostSets (speedParam, 8.0f);
        const int movedDuring = view.syncControls();
        const auto shownDuring = speed.readout();
        speed.release();
        const int movedAfter = view.syncControls();
        check (during < 4.0f && movedDuring == 0 && shownDuring == readDuring && movedAfter == 1 && speed.readout() == "8x" && w.begins == 1
                   && w.ends == 1,
               fmt ("a drag down writes its value in one gesture (%d / %d, %.4g); the host's 8x waits until the drag ends (%d, then %d moved)",
                    w.begins, w.ends, (double) during, movedDuring, movedAfter));
    }

    // A value typed into the knob is one whole gesture.
    {
        ParamWatch w (speedParam);
        speed.type ("3");
        check (juce::exactlyEqual (params::plainValueOf (speedParam), 3.0f) && w.begins == 1 && w.ends == 1,
               fmt ("typed: 3x in one gesture (%d / %d)", w.begins, w.ends));
    }

    // A switch, a row of segments and a drop-down: the same rule.
    auto* tape = findControl<ui::ParamToggle> (view, params::P::tape);
    auto* engineSeg = findControl<ui::SegmentedControl> (view, params::P::engine);
    auto* jump = findControl<ui::ChoiceBox> (view, params::P::jump);
    check (tape != nullptr && engineSeg != nullptr && jump != nullptr, "the Tape switch, the Engine segments, the Jump every menu");
    if (tape == nullptr || engineSeg == nullptr || jump == nullptr)
        return;

    hostSets (proc.getParam (params::P::tape), 1.0f);
    hostSets (proc.getParam (params::P::engine), 0.0f);
    hostSets (proc.getParam (params::P::jump), 3.0f);
    const bool waited = ! tape->getToggleState() && engineSeg->getSelected() == 1 && jump->getBox().getSelectedItemIndex() != 3;
    const int three = view.syncControls();
    check (waited && three == 3 && tape->getToggleState() && engineSeg->getSelected() == 0 && jump->getBox().getSelectedItemIndex() == 3,
           fmt ("host changes to Tape, Engine and Jump every wait for the timer, then all three show (%d moved)", three));

    {
        ParamWatch wt (proc.getParam (params::P::tape)), we (proc.getParam (params::P::engine)), wj (proc.getParam (params::P::jump));
        tape->setToggleState (false, juce::sendNotificationSync);   // what a click does (triggerClick would go through the message loop)
        engineSeg->choose (2);
        jump->getBox().setSelectedItemIndex (5, juce::sendNotificationSync);
        check (juce::exactlyEqual (params::plainValueOf (proc.getParam (params::P::tape)), 0.0f) && wt.begins == 1 && wt.ends == 1
                   && juce::exactlyEqual (params::plainValueOf (proc.getParam (params::P::engine)), 2.0f) && we.begins == 1 && we.ends == 1
                   && juce::exactlyEqual (params::plainValueOf (proc.getParam (params::P::jump)), 5.0f) && wj.begins == 1 && wj.ends == 1,
               fmt ("the user's switch, segment and menu each write their parameter in one gesture (%d %d %d)", wt.begins, we.begins, wj.begins));
        check (view.syncControls() == 0, "and the timer then has nothing to move");
    }
    resetUserSettings (proc);
}
