#pragma once

// The hint bar at the bottom of the window (PLUGIN-PLAN 4 M4, 7.6 R3; shell.html #hintBar, sl-ui.js modeHint and
// updateHint): one line of help for the mode on the left, and "Try this:" on the right. A hint too long for its
// line is cut with an ellipsis; while the mouse is on it, the whole hint shows over the bar, growing upwards
// (shell.html #hintHover: the prototype's short windows clamp the hint the same way). While the mouse is on a control
// of the selection bar, that control's text shows there instead (sl-ui.js setHint, M5 S2). The hover never takes a
// click and never changes the window's layout. Message thread only.
//
// "Try this" (U3, M6 plan 7.8 decision 6) is one suggestion at a time from the prototype's list (tryThis), at most half
// the bar, so it never covers the mode's line; a suggestion too long for its room shows whole while the mouse is on it,
// over the right half of the bar only, growing upwards.

#include "Theme.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "model/Clip.h"

#include <optional>

namespace sl::ui
{
    /** sl-ui.js modeHint: the line for mode 1..8 (lane: the mode's lane, mode 8's sub-lane), with Freehand on, Slide
        into armed or the Nodes switch on, in the prototype's words (a lane mode's line points at the lane button's menu,
        M5 S2). */
    juce::String modeHint (int mode, std::optional<core::LaneId> lane, bool freehand, bool linkArmed, bool nodes = false);

    /** sl-ui.js SUB_HINT: what a Sample lane's line does (the mode line in Sample mode, and the Sample chips' tooltips);
        empty for any other lane. */
    juce::String sampleLaneHint (core::LaneId lane);

    /** sl-ui-panels.js updateTry (U3, M6): the one suggestion for what the roll shows now, in the prototype's words: a tip
        for each lane mode (Build and 2 to 7); in Sample mode one for each Sample lane, and on Travel one for each Motion
        (the primary note's own, else the Sample tab's), which gives way to its next step once it is done; a tip that
        does nothing in the engine the primary note plays gives way to one that works there. primary may be null. */
    juce::String tryThis (int mode, core::LaneId subLane, const model::Note* primary, const core::Inst& inst);

    class HintBar final : public juce::Component
    {
    public:
        explicit HintBar (const Theme&);

        void setHint (const juce::String& hint);
        const juce::String& getHint() const noexcept { return hint; }

        /** The right-hand text after "Try this: ". */
        void setTryThis (const juce::String& text);
        const juce::String& getTryThis() const noexcept { return tryThis; }

        /** True when the suggestion does not fit its room (at most half the bar): the mouse on it shows all of it. */
        bool isTryCut() const;

        /** Shows or hides the whole suggestion over the right half of the bar (the mouse does this; also for the tests). */
        void showTryHover (bool show);

        /** Where the mode's line and the suggestion are drawn (this component's pixels). */
        juce::Rectangle<int> getHintArea() const { return hintArea(); }
        juce::Rectangle<int> getTryArea() const { return tryArea(); }

        /** True when the hint does not fit on its line (it is cut, and the mouse shows all of it). */
        bool isHintCut() const;

        /** The whole hint over the bar (MainView adds it as its own child, above the bar). */
        juce::Component& getHover() noexcept { return hover; }

        /** Shows or hides the whole hint over the bar (the mouse does this; also for the tests). */
        void showHover (bool show);

        /** A control's text while the mouse is on it (sl-ui.js setHint): shown over the bar the way the whole hint is,
            changing no size. Empty: none, the mode's own hint again. */
        void setHelp (const juce::String& text);
        const juce::String& getHelp() const noexcept { return help; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        static constexpr int padX = 8;

    private:
        class Hover final : public juce::Component
        {
        public:
            explicit Hover (const Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); }
            void paint (juce::Graphics&) override;
            juce::String prefix;   // "Try this: " in the accent, before a whole suggestion
            juce::String text;

        private:
            const Theme& theme;
        };

        juce::Rectangle<int> hintArea() const;
        juce::Rectangle<int> tryArea() const;
        void updateHover();

        const Theme& theme;
        juce::String hint, tryThis;
        juce::String help;           // a control's text while the mouse is on it
        bool hintShown = false;      // the mouse is on a hint that is cut: the whole hint shows
        bool tryShown = false;       // the mouse is on a suggestion that is cut: the whole suggestion shows
        Hover hover;
    };
}
