#pragma once

// The piano roll (PLUGIN-PLAN 4 M4, 7.6 R2; shell.html #editor, sl-ui.js "drawing"): the ruler along the top, the
// piano keys down the left, and the roll between them with its scroll bars: the rows and the grid, the notes as
// strands (the NoteLayerCache keeps them in an image), the handles, the readout and, while Cubase plays, the
// playhead (#FF7A00) from the engine's display frames. Four zoom buttons sit at the right end of the ruler
// (H-, H+, V-, V+, the prototype's top-bar buttons); Ctrl + the wheel zooms in time around the pointer, the wheel
// scrolls (Shift + the wheel sideways), and so do the scroll bars.
//
// The view owns the RollGestures (R1) and draws with its geometry, the same one the gestures hit-test with. It reads
// the document only in refresh (the editor's timer, and R3's gestures after each event): refresh takes the clip,
// the selection, the mode, the instrument and the tempo as one PaintState, compares it with the last one and
// repaints only what changed (the notes the cache says changed, the handles, the readout, the links). Every paint
// until the next refresh draws that same state.
//
// The first view centres on the notes (the prototype's first open) unless the project saved another one (UIST's
// zoom and scroll); every zoom or scroll is written back to UIST. Nothing here runs on the audio thread: the
// piano's auditions go through the CommandQueue, and edits through the document. Message thread only.
//
// The live roll (R3): the canvas's mouse handlers turn JUCE's events into the gestures' presses, moves and releases
// in roll pixels (a right press opens the right-click menu), with the event's own time so double-clicks are the
// prototype's 320 ms. The buttons and the keys call the commands below. After every event the view refreshes and
// calls onEdit, so MainView shows the new selection, the inspector, the hint and the Undo buttons.
//
// The selection tools (M5): the view also owns the selection bar's SelectionTools, writing through the same document
// and undo history as the gestures; the bar's controls call them, then edited (). The bar's Select similar list and the
// lane buttons' menus open in the same menu panel as the right-click menu (openMenuAt). The hover ghosts (GhostPreview)
// are the view's too: what a control or a menu item would do, drawn dashed, never applied.

#include "GhostPreview.h"
#include "NoteDecor.h"
#include "NoteLayerCache.h"
#include "PianoStrip.h"
#include "RollGeometry.h"
#include "RollGestures.h"
#include "RollPainter.h"
#include "Ruler.h"
#include "SelectionBar.h"

#include "engine/DisplayFrame.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class StrandLockProcessor;

namespace sl::ui::roll
{
    class RollView final : public juce::Component,
                           private juce::ScrollBar::Listener,
                           private juce::Timer
    {
    public:
        RollView (const Theme&, StrandLockProcessor&);
        ~RollView() override;

        //==========================================================================================
        // What the editor and MainView call.

        /** Reads the document, the instrument, the tempo and the gestures' readout and box, and repaints what
            changed. The editor's timer calls it; R3 calls it after every gesture event. */
        void refresh();

        /** The engine's newest display frame (null: none): the playhead while Cubase plays, and the keys played
            live lit on the piano. */
        void setDisplayFrame (const engine::DisplayFrame* frame);

        /** The host's tempo (nothing: 120 bpm), for the vibrato's squiggle and the readouts. */
        void setHostBpm (std::optional<double> bpm);

        /** Mode 1..8, and for mode 8 its sub-lane (0 Travel .. 3 Refresh; -1 keeps it). */
        void setMode (int mode, int subLaneIndex = -1);

        /** A preview of another clip (M5's hover ghosts): its notes that differ are drawn dashed. Null: none. It is made
            from the clip, the selection and the mode of now; once any of them changes (Undo, Redo, Escape, a state load,
            a key) the next refresh takes it away, as it no longer shows what a click would do. */
        void setGhost (model::ClipPtr ghostClip);

        /** The hover ghosts: which preview is drawn and who owns it (the selection bar's controls, the menus). */
        GhostPreview& getGhosts() noexcept { return ghosts; }

        /** A control's text for the hint bar while the mouse is on it (empty: the mode's own hint), through onHint. */
        void showHint (const juce::String& text);

        /** Where showHint goes (MainView's hint bar). */
        std::function<void (const juce::String&)> onHint;

        /** Each note or Across for every shape tool (the menus, the Shapes drawer), kept in UIST. */
        void setScope (model::ShapeScope scope);

        /** Where the roll's short messages go (MainView's toast). */
        std::function<void (const juce::String&)> onToast;

        /** The same for a message that asks for its own time on screen (the selection tools'), in milliseconds; when
            unset those go to onToast. */
        std::function<void (const juce::String&, int ms)> onToastFor;

        /** Called after the mouse, a menu item or a command changed something (the clip, the selection, the
            switches): MainView updates the selection row, the inspector, the hint and the Undo buttons. */
        std::function<void()> onEdit;

        /** Called when a press reaches the roll, before the gestures see it (MainView ends any typing). */
        std::function<void()> onPress;

        //==========================================================================================
        // The mouse on the roll (the canvas's handlers call these with the canvas's pixels).

        /** A left press: mods Shift and Ctrl, timeMs the event's time in milliseconds (two presses within 320 ms
            and 22 px are a double-click, as the prototype has it). */
        void press (juce::Point<float> at, juce::ModifierKeys mods, double timeMs);

        /** The mouse moved while pressed. */
        void dragTo (juce::Point<float> at, juce::ModifierKeys mods);

        /** The mouse let go (or the roll lost it): the gesture ends, one undo step when it changed the clip. */
        void release();

        /** A right press: the note there is selected, and the right-click menu opens. */
        void rightPress (juce::Point<float> at, double timeMs);

        //==========================================================================================
        // Commands: the buttons, the keys and the menu. Each one refreshes and calls onEdit.

        /** Undo and Redo of the processor, with the prototype's toasts ("Undo: Move notes", "Nothing to undo"). */
        void undo();
        void redo();

        void selectAll();
        void duplicate();
        void deleteSelection();
        void clearSelection();     // Escape
        void clearAll();           // every note goes, one undo step
        void copyLanes();
        void pasteLanes();
        void copyNotes();          // the Copy / Paste tab (M5 S3)
        void pasteNotes();
        void toggleFreehand();
        void armLink();            // Slide into...
        void setMulti (bool on);
        void toggleNodes();        // the Nodes switch (M5): a box picks the selected notes' nodes

        /** The selection bar's tools (M5), on the same document and undo history as the gestures. */
        SelectionTools& getTools() noexcept { return *tools; }

        /** After the selection bar changed something: the roll refreshes, then onEdit (as after a gesture). */
        void edited();

        /** The Grid in beats (1, 0.5, 0.25, 0.125; 0 = Free), kept in UIST. */
        void setGrid (double beats);
        double getGrid() const noexcept;

        //==========================================================================================
        // The right-click menu: a panel over the page (not a JUCE PopupMenu, which would hold every key while
        // open). A press anywhere else closes it; with Keys on, Escape does (MainView).

        /** The menu the last right press opened (its items), or empty. */
        const std::vector<MenuItem>& getMenu() const noexcept { return menuItems; }

        /** Runs an item of the open menu, as a click on it does. */
        void chooseMenuItem (const MenuItem& item);

        /** Opens a menu of the caller's own in the same panel (the selection bar's Select similar, sl-ui-panels.js
            A.openMenu): its top left at `at` in from's pixels; a click on an item runs actions[item.action] after the
            menu closes. */
        void openMenuAt (const juce::Component& from, juce::Point<int> at, std::vector<MenuItem> items,
                         std::vector<std::function<void()>> actions, std::vector<GhostPreview::Make> hovers = {});

        /** The mouse is on item index of the open menu (button: one of a row's buttons; -1: none, or no item): its
            ghost is drawn, or the menu's ghost goes (the menu panel calls it; public for the tests). */
        void menuHover (int index, int button = -1);

        /** False: a right press keeps the menu's items (getMenu) but shows no panel (the tests). */
        void setMenusShown (bool shouldShow) noexcept { menusShown = shouldShow; }

        /** Whether the menu's panel is showing. */
        bool isMenuOpen() const noexcept;

        /** Closes the menu (its items go too). */
        void closeMenu();

        /** Where the menu's panel opens: a component over the whole page (MainView); none = this view. */
        void setMenuParent (juce::Component* parent) noexcept;

        /** The menu's panel once a menu has been shown (the tests), else null. */
        juce::Component* getMenuPanel() noexcept;

        /** Where the open menu draws item `index` (button 0 or 1: one of a row's two buttons), in the panel's
            pixels (the tests). */
        juce::Rectangle<int> getMenuItemBounds (std::size_t index, int button = -1) const;

        /** What the menu does with a press anywhere in StrandLock's windows (it hears them through the desktop's
            mouse listener): one outside it closes it, except the press that opened it. Public for the tests. */
        void menuHeardPress (const juce::MouseEvent& e);

        //==========================================================================================
        // Zoom and scroll.

        /** The zoom buttons: beats times h (H- 1/1.3, H+ 1.3), rows times v (V- 0.8, V+ 1.25), about the centre. */
        void zoomBy (double h, double v);

        /** The scroll in roll pixels (kept inside the roll, whole pixels). */
        void setScroll (double x, double y);
        void scrollBy (double dx, double dy) { setScroll (getGeometry().scrollX() + dx, getGeometry().scrollY() + dy); }

        /** One step of the mouse wheel over the roll (deltaY as JUCE gives it; ctrl: zoom in time around viewX;
            shift: sideways). Over the piano keys the wheel only scrolls up and down. */
        void wheel (double deltaX, double deltaY, bool ctrl, bool shift, double viewX, bool overPiano);

        //==========================================================================================
        // For R3 and the tests.

        RollGestures& getGestures() noexcept { return *gestures; }
        const RollGeometry& getGeometry() const noexcept { return gestures->geometry(); }
        NoteLayerCache& getCache() noexcept { return cache; }
        const PaintState& getPaintState() const noexcept { return state; }
        NoteDecor& getDecor() noexcept { return decor; }
        juce::Component& getCanvas() noexcept;
        Ruler& getRuler() noexcept { return ruler; }
        PianoStrip& getPiano() noexcept { return piano; }
        juce::ScrollBar& getScrollBar (bool vertical) noexcept { return vertical ? vbar : hbar; }

        /** 0 H-, 1 H+, 2 V-, 3 V+. The view owns them; it places them at the right end of the ruler while they are its
            children. MainView takes them into the tab row (M5 S3, plan 7.7 decision 6: in the ruler they hid its last bar
            numbers), and the ruler then runs the whole width. */
        FlatButton& getZoomButton (int index) noexcept { return *zoomButtons[(std::size_t) juce::jlimit (0, 3, index)]; }

        /** The playhead in beats while Cubase plays. */
        std::optional<double> getPlayhead() const noexcept { return playhead; }

        /** What the note layer said changed at the last refresh. */
        const NoteLayerCache::Change& getLastChange() const noexcept { return lastChange; }

        /** Every area refresh, the playhead, the piano and the scroll asked to repaint since the last call (this
            component's pixels). */
        juce::RectangleList<int> takeRepaintLog();

        /** The whole roll drawn again at the next paint (every note too). */
        void invalidateAll();

        /** The canvas in this component's pixels (the roll between the ruler, the keys and the scroll bars). */
        juce::Rectangle<int> getCanvasArea() const noexcept;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        static constexpr int pianoWidth = 40, rulerHeight = 20, scrollBarThickness = 10;
        static constexpr int zoomButtonWidth = 27, zoomButtonHeight = 16;

        /** Roll pixels the wheel scrolls for JUCE's delta of one notch (0.234 on Windows: about 100 px, as a
            browser scrolls the prototype's roll). */
        static constexpr double wheelPixels = 427.0;

    private:
        class Canvas;
        class MenuPanel;

        /** Everything that makes refresh repaint the whole roll (and the ruler and the keys) when it changes. The
            notes, the selection, the handles, the ghost, the box and the readout repaint only where they changed. */
        struct ViewKey
        {
            double viewWidth = 0.0, viewHeight = 0.0, scrollX = 0.0, scrollY = 0.0, beatWidth = 0.0, rowHeight = 0.0;
            int mode = 0, lane = -1;
            double snap = 0.0;
            int key = -1, scale = -1, refreshMode = -1, glideMode = -1;
            double glideTime = 0.0, vibRate = 0.0, bpm = 0.0;
            int bars = 0, beatsPerBar = 0;
            std::uint64_t decor = 0;   // what the notes' decorations read (M6: NoteDecor::key)

            bool operator== (const ViewKey&) const noexcept;
            bool operator!= (const ViewKey& o) const noexcept { return ! (*this == o); }
        };

        RollHost makeHost();
        ToolHost makeToolHost();
        ViewKey viewKey() const;
        juce::Rectangle<double> visible() const;
        void paintCanvas (juce::Graphics&);
        void repaintAll();
        void repaintRoll (juce::Rectangle<double> rollArea);
        void repaintCanvasArea (juce::Rectangle<int> canvasArea);
        void applyFirstView();
        void roundScroll();
        void viewChanged();
        void saveView();
        void updateScrollBars();
        void audition (int pitch);
        void auditionOff (int pitch);
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void timerCallback() override;

        void changed();
        void toast (const juce::String& text);
        void showMenu (juce::Point<float> at, double timeMs);
        void saveScope();

        const Theme& theme;
        StrandLockProcessor& processor;
        std::unique_ptr<RollGestures> gestures;
        std::unique_ptr<SelectionTools> tools;
        std::unique_ptr<Canvas> canvas;
        Ruler ruler;
        PianoStrip piano;
        juce::ScrollBar hbar { false }, vbar { true };
        std::array<std::unique_ptr<FlatButton>, 4> zoomButtons;

        NoteLayerCache cache;
        NoteLayerCache::Change lastChange;
        NoteDecor decor;   // the notes' chevrons and thumbnails (M6): the sound's peak bins, each note's simulation
        PaintState state;
        model::ClipPtr ghost;
        std::unique_ptr<RollGeometry> ghostGeo;

        /** What the ghost was made from: the document's clip, selection and mode when it was set (review R5T-1). */
        struct GhostBase
        {
            model::ClipPtr clip;
            std::vector<std::string> selected;
            int mode = 0;
            core::LaneId subLane = core::LaneId::travel;
        };
        GhostBase ghostBase;
        bool ghostIsStale() const;
        GhostPreview ghosts { [this] (model::ClipPtr c) { setGhost (std::move (c)); } };
        ViewKey lastKey;
        bool haveKey = false;
        std::vector<juce::Rectangle<double>> overlay;   // roll pixels the overlay covered at the last refresh
        std::optional<double> hostBpm;
        std::optional<double> playhead;
        bool firstView = true;
        juce::RectangleList<int> repaintLog;

        std::vector<MenuItem> menuItems;   // the right-click menu open now
        bool menusShown = true;
        juce::Component::SafePointer<juce::Component> menuParent;
        bool ownMenu = false;                                // the open menu is a caller's (openMenuAt), not the gestures'
        std::vector<std::function<void()>> ownMenuActions;   // its items' actions
        std::vector<GhostPreview::Make> ownMenuHovers;       // and their ghosts

        // auditions in flight: a note played by a gesture stops 220 ms later (sl-ui.js audition)
        struct PendingOff
        {
            int pitch = 60;
            double due = 0.0;
        };
        std::vector<PendingOff> pendingOffs;

        std::unique_ptr<MenuPanel> menuPanel;   // last, so it goes first

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RollView)
    };
}
