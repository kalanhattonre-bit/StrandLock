#pragma once

// The hint bar at the bottom of the window (PLUGIN-PLAN 4 M4, 7.6 R3; shell.html #hintBar, sl-ui.js modeHint and
// updateHint): one line of help for the mode on the left, and "Try this:" on the right. A hint too long for its
// line is cut with an ellipsis; while the mouse is on it, the whole hint shows over the bar, growing upwards
// (shell.html #hintHover: the prototype's short windows clamp the hint the same way). The hover never takes a
// click and never changes the window's layout. Message thread only.

#include "Theme.h"

#include "core/Lanes.h"

#include <optional>

namespace sl::ui
{
    /** sl-ui.js modeHint: the line for mode 1..8 (lane: the mode's lane, mode 8's sub-lane), with Freehand on or
        Slide into armed. Box selection, the Nodes switch and the lane buttons' menus are M5's, so their parts of
        the prototype's lines are left out until then; a lane mode's line says the right-click menu holds the
        shapes, which it does in M4. */
    juce::String modeHint (int mode, std::optional<core::LaneId> lane, bool freehand, bool linkArmed);

    class HintBar final : public juce::Component
    {
    public:
        explicit HintBar (const Theme&);

        void setHint (const juce::String& hint);
        const juce::String& getHint() const noexcept { return hint; }

        /** The right-hand text after "Try this: ". */
        void setTryThis (const juce::String& text);

        /** True when the hint does not fit on its line (it is cut, and the mouse shows all of it). */
        bool isHintCut() const;

        /** The whole hint over the bar (MainView adds it as its own child, above the bar). */
        juce::Component& getHover() noexcept { return hover; }

        /** Shows or hides the whole hint over the bar (the mouse does this; also for the tests). */
        void showHover (bool show);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        static constexpr int padX = 8;

    private:
        class Hover final : public juce::Component
        {
        public:
            explicit Hover (const Theme& t) : theme (t) { setInterceptsMouseClicks (false, false); }
            void paint (juce::Graphics&) override;
            juce::String text;

        private:
            const Theme& theme;
        };

        juce::Rectangle<int> hintArea() const;
        juce::Rectangle<int> tryArea() const;

        const Theme& theme;
        juce::String hint, tryThis;
        Hover hover;
    };
}
