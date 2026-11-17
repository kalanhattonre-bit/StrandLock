#pragma once

// Which keys StrandLock keeps (PLUGIN-PLAN 4 M4 "keys", 7.6 decision 3; sl-ui.js's keydown handler). With Keys off,
// every key goes on to Cubase: route () returns false and does nothing. With Keys on, StrandLock keeps the keys it
// gets (M0's rule) and runs its shortcuts, except Space, which always goes on to Cubase (play and stop), with or
// without a modifier.
//
// The shortcuts with Keys on: Ctrl+Z Undo, Ctrl+Shift+Z and Ctrl+Y Redo, Delete and Backspace delete the selected
// notes, Ctrl+A selects every note, Ctrl+D duplicates, Ctrl+C / Ctrl+V copy and paste lanes, 1 to 8 pick the mode (the
// number pad too), Escape clears the selection, B arms Slide into. (On a Mac, Cmd stands for Ctrl.) Alt with a key
// runs nothing. The prototype's A to K note keys and Z / X octave keys are the live keyboard (M8), not here.
//
// Message thread only. The editor's keyPressed hands every key to route ().

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>

namespace sl::ui
{
    /** What the shortcuts call. An empty function is a shortcut that does nothing yet (the key is still kept). */
    struct KeyActions
    {
        std::function<void()> undo, redo, deleteSelection, selectAll, duplicate, copyLanes, pasteLanes, clearSelection, armLink;
        std::function<void (int mode)> setMode;
    };

    class KeyRouter
    {
    public:
        KeyRouter() = default;
        explicit KeyRouter (KeyActions a) : actions (std::move (a)) {}

        KeyActions actions;

        /** True when StrandLock keeps the key (it ran a shortcut, or Keys is on); false when it goes on to Cubase. */
        bool route (const juce::KeyPress& key, bool keysOn) const;

        enum class Shortcut { none, undo, redo, deleteSelection, selectAll, duplicate, copyLanes, pasteLanes, clearSelection, armLink, mode };

        struct Hit
        {
            Shortcut what = Shortcut::none;
            int mode = 0;   // Shortcut::mode: 1 .. 8
        };

        /** The shortcut a key names when Keys is on (none for Space and for keys that are not shortcuts). */
        static Hit shortcutFor (const juce::KeyPress& key);
    };
}
