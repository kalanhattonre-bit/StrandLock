#pragma once

// The toast (PLUGIN-PLAN 4 M4, 7.6; shell.html .toast, sl-ui.js toast): a short message in a dark rounded box that
// goes away after a few seconds. MainView places it just under the roll's ruler (on the pane when the roll is
// hidden) and gives it up to three lines. It never takes a click. Message thread only.

#include "Theme.h"

namespace sl::ui
{
    class Toast final : public juce::Component, private juce::Timer
    {
    public:
        explicit Toast (const Theme&);

        /** Shows message for ms milliseconds (at least half a second). */
        void show (const juce::String& message, int ms);

        /** The width the message needs on one line, with the box's padding. */
        int preferredWidth() const;

        const juce::String& getText() const noexcept { return text; }

        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;

        const Theme& theme;
        juce::String text;
    };
}
