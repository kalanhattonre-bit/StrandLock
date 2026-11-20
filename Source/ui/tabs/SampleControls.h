#pragma once

// The Sample and Macros tabs' own controls (PLUGIN-PLAN 4 M6, 7.8 T1; sl-ui-sample.js knob (), toggleLock, rerollBtn):
//
// - SampleKnob: the prototype's knob. Drag up or down (170 px cross the whole knob, Shift = fine); the value lands where
//   the knob's q function puts it (rules::dragTo) and is written at once inside one host gesture, and the release is one
//   undo step. A double-click puts the default back; a click on the value opens a box to type one, which is kept as
//   typed (decision 2: Lock Grain 4.3 stays 4.3). It shows the exact value it set itself while the parameter still holds
//   that value's float (plan 1.6 #12), so its readout is the prototype's. A knob with nothing to do now is faded (.42, .6
//   under the mouse); the Grain knob in Tape with no grain notes is off ("—", a press only says why).
// - LockToggle: Lock lights only with Cloud; on another engine a click switches to Cloud with Lock in one undo step.
//
// They write their parameters on the message thread through ParamBinding (no attachment, plan 7.4 decision 1) and the
// editor's 30 Hz timer moves them (ParamSynced). Each finished gesture is one undo step through TabActions::commit
// (decision 1). None of them takes the keyboard but the value box while it is typed in (as the inspector's boxes).

#include "params/ParamIds.h"
#include "ui/Controls.h"
#include "ui/SampleTabRules.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sl::ui
{
    /** What the tabs' controls reach outside themselves. */
    struct TabActions
    {
        /** One undo step called label for the parameters named (StrandLockProcessor::pushUndoStep). */
        std::function<bool (const std::string& label, const std::vector<params::P>& changed)> commit;

        /** A control's text in the hint line while the mouse is on it (empty: none). */
        std::function<void (const juce::String&)> hint;

        /** A toast for ms milliseconds. */
        std::function<void (const juce::String&, int ms)> toast;
    };

    /** Calls changed (true / false) when the mouse comes onto a component or any of its children, and when it leaves
        them all (sl-ui-sample.js helpOn's mouseenter / mouseleave). */
    class HoverWatch final : private juce::MouseListener
    {
    public:
        HoverWatch (juce::Component& c, std::function<void (bool)> changed);
        ~HoverWatch() override;

        bool isOver() const noexcept { return over; }

    private:
        void mouseEnter (const juce::MouseEvent&) override { update(); }
        void mouseExit (const juce::MouseEvent&) override { update(); }
        void update();

        juce::Component::SafePointer<juce::Component> target;
        std::function<void (bool)> onChange;
        bool over = false;
    };

    //==============================================================================================
    class SampleKnob final : public juce::Component, public juce::SettableTooltipClient, public ParamSynced
    {
    public:
        SampleKnob (const Theme&, juce::AudioProcessorValueTreeState&, rules::KnobId, TabActions&);
        ~SampleKnob() override;

        /** The knob as the settings make it now (the tab, on every sync): its spec (the Grain knob's follows the engine)
            and its fade state (none: no rule). */
        void setSpec (const rules::KnobSpec& spec, const rules::Fade* fade);
        const rules::KnobSpec& getSpec() const noexcept { return spec; }
        rules::KnobId getId() const noexcept { return id; }

        bool syncToParameter() override;

        /** The value it shows: the exact value it set itself while its parameter still holds that value's float, else
            the parameter's own. */
        double value() const;
        juce::String readout() const;
        bool isFaded() const noexcept { return faded; }
        bool isOff() const noexcept { return spec.off; }

        /** The hint line's text while the mouse is on it, and its tooltip (sl-ui-sample.js helpOn and title). */
        juce::String hintText() const;
        juce::String titleText() const;

        //==========================================================================================
        // Gestures, as the mouse makes them (the tests call them too). y is in the knob's own pixels, growing downwards:
        // moving up raises the value.

        void press (double y);
        void dragTo (double y, bool fine);
        void release();
        void doubleClick();

        /** Types text into the value box as Kalan does: a click on the value, the text, Enter. False when no box opens
            (a macro or offset, or a knob that is off). */
        bool type (const juce::String& text);

        /** Opens the value box (a click on the value) and leaves it open. */
        void beginTyping();
        bool isTyping() const;
        juce::TextEditor* getTextEditor() const;

        /** The dial's area (presses there drag) and the value's (a click there types). */
        juce::Rectangle<int> dialBounds() const;
        juce::Rectangle<int> valueBounds() const;

        /** Called after a gesture changed the value (the tab re-reads the settings at once). */
        std::function<void()> onChange;

        int preferredWidth() const;
        static constexpr int preferredHeight = 72;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void lookAndFeelChanged() override;

    private:
        class ValueLabel;

        ParamBinding& binding() const;
        void write (double v, bool inGesture);
        void typed (const juce::String& text);
        void refreshLook();
        void hoverChanged (bool over);

        const Theme& theme;
        juce::AudioProcessorValueTreeState& state;
        const rules::KnobId id;
        TabActions& actions;
        rules::KnobSpec spec;
        std::optional<rules::Fade> fade;
        std::map<int, std::unique_ptr<ParamBinding>> bindings;   // by parameter index: the Grain knob writes three

        double exact = 0.0;      // the value the knob set last, and the parameter it set it on
        int exactParam = -1;
        double shown = 0.0;      // the value drawn now
        bool faded = false, hovering = false;
        std::optional<rules::KnobDrag> drag;
        std::unique_ptr<ValueLabel> label;
        std::unique_ptr<HoverWatch> hover;
    };

    //==============================================================================================
    /** The Lock switch (sl-ui-sample.js toggleLock): lit with Cloud and Lock on; with another engine a click switches to
        Cloud with Lock (both parameters, one undo step "Engine: Cloud with Lock", and a toast). */
    class LockToggle final : public FlatButton, public ParamSynced
    {
    public:
        LockToggle (const Theme&, juce::AudioProcessorValueTreeState&, TabActions&);

        bool syncToParameter() override;
        juce::RangedAudioParameter& getParameter() noexcept { return lock.parameter(); }

        /** What a click does (the tests call it as a click). */
        void toggle();

    protected:
        using FlatButton::clicked;
        void clicked() override { toggle(); }

    private:
        TabActions& actions;
        ParamBinding lock, engine;
    };
}
