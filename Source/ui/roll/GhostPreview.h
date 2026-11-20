#pragma once

// Hover ghosts (PLUGIN-PLAN 4 M5 "hover ghosts (U6)", 7.7 S2 and decision 2; sl-ui-panels.js ghost, clearGhost,
// ghostOn and hintOn, sl-ui.js setGhost and its menu's hover): while the mouse is on a shape, a pattern, a tool, a
// menu item or a recipe, the roll draws what a click would do, dashed, over the notes it would change.
//
// The would-be clip is made off to the side by the same model call the click makes, and handed to the roll to draw
// (RollView::setGhost). It never reaches the ClipDocument: a ghost pushes no undo step and publishes no snapshot.
//
// - GhostPreview: which preview is drawn and who owns it (a button, the open drawer, the open menu), as the
//   prototype's ghostOwner. A click, an edit or a live slider move takes the preview away; so does the mouse leaving
//   its owner. No JUCE in it, so the tests count what it drew.
// - HoverHooks: the mouse hooks of the controls that preview, and the hint bar's text while the mouse is on one (the
//   control's tooltip, as the prototype's hintOn shows a button's title there).
//
// Message thread only.

#include "model/Clip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace sl::ui::roll
{
    class GhostPreview
    {
    public:
        /** What draws a preview (RollView::setGhost); null: none. */
        using Draw = std::function<void (model::ClipPtr)>;

        /** What makes a would-be clip: nothing draws no preview. */
        using Make = std::function<std::optional<model::Clip>()>;

        explicit GhostPreview (Draw draw);

        GhostPreview (const GhostPreview&) = delete;
        GhostPreview& operator= (const GhostPreview&) = delete;

        /** sl-ui-panels.js ghost (fn, owner): the clip make returns is drawn, owned by owner; nothing draws none (and
            takes away any preview drawn now). */
        void hover (const void* owner, const Make& make);

        /** The same with the clip in hand. */
        void show (const void* owner, std::optional<model::Clip> clip);

        /** The mouse left owner: its preview goes; another owner's stays. */
        void leave (const void* owner);

        /** clearGhost: whatever preview is drawn goes (a click, an edit, a live slider move). */
        void clear();

        bool isShown() const noexcept { return shown != nullptr; }
        const void* getOwner() const noexcept { return owner; }
        const model::ClipPtr& getClip() const noexcept { return shown; }

        /** How many previews it has drawn (the tests). */
        int count() const noexcept { return drawn; }

        /** The open drawer's own previews: a Pattern or Random slider before Apply, Walk's and Humanize's before
            their first click (sl-ui-panels.js ghost (..., D.drawer)). They stay until the mouse leaves the drawer. */
        static const void* drawerOwner() noexcept;

        /** The open menu's item under the mouse. */
        static const void* menuOwner() noexcept;

    private:
        Draw draw;
        model::ClipPtr shown;
        const void* owner = nullptr;
        int drawn = 0;
    };

    //==============================================================================================
    /** The mouse hooks of the controls that preview (sl-ui-panels.js ghostOn and hintOn): on entering a control its
        preview is drawn and its text goes to the hint bar; on leaving, both go; a press takes the preview away first
        (sl-ui-panels.js button: clearGhost, then the click). Each hook is a juce::MouseListener on its control; one
        that outlives its control is left alone, and the hooks of a control that goes take its preview and hint with
        them (a drawer rebuilt under the mouse). */
    class HoverHooks
    {
    public:
        using Hint = std::function<void (const juce::String&)>;

        /** ghosts may be null (no previews); hint shows a text in the hint bar (empty: the mode's own hint). */
        HoverHooks (GhostPreview* ghosts, Hint hint);
        ~HoverHooks();

        HoverHooks (const HoverHooks&) = delete;
        HoverHooks& operator= (const HoverHooks&) = delete;

        /** Hooks c: make (may be empty) is its preview; its hint is text when given, else c's tooltip. */
        void add (juce::Component& c, GhostPreview::Make make, std::function<juce::String()> text = {});

        /** As the mouse does (the tests call these too): on c, off c, a press on c. False when c has no hook. */
        bool enter (const juce::Component& c);
        bool exit (const juce::Component& c);
        bool press (const juce::Component& c);

        /** The hooked controls (in the order added). */
        std::vector<juce::Component*> controls() const;

    private:
        struct Hook;

        Hook* find (const juce::Component& c) const noexcept;
        void entered (Hook&);
        void exited (Hook&);
        void pressed (Hook&);

        GhostPreview* ghosts;
        Hint hint;
        std::vector<std::unique_ptr<Hook>> hooks;
        const void* hintOwner = nullptr;
    };
}
