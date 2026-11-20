#pragma once

// The roll's gestures (PLUGIN-PLAN 4 M4, 7.6 decision 1): a pure state machine. It takes pointer presses, moves and
// releases, double-clicks (two presses within 320 ms and 22 px on the same kind of thing, as sl-ui.js isDouble),
// modifiers and the right-click menu, in roll pixels (RollGeometry's: nothing scrolled), and turns them into model
// edits through the M1 functions (addNote, updateNotes, setLaneNodes, applyShape ...). It never draws and never sees a
// mouse: the view (R3) translates JUCE events into these calls, and the tests call them directly.
//
// What it changes, it changes through a RollHost: the ClipDocument (the clip, the selection, the mode, the
// inspector's node) and one undo step per finished gesture (decision 2). While a drag moves, every step is shown and
// sent to the engine through ClipDocument::apply with no undo step; when the gesture ends, commit (label) pushes one,
// and a gesture that changed nothing pushes none (a drag that came back to where it started is put back exactly).
//
// The behaviour is sl-ui.js rollDown, rollMove, rollUp and openContextMenu, read line by line: the order of the hit
// tests (slide handles, nodes, segment diamonds, end dots, Freehand, notes, empty space), the snap rules, the dead
// zones (a note moves after 5 px or a row; a node after 3 px; an end dot bends after 0.4 row; a box after 4 px) and
// the readouts in real units (model::formatLane through InspectorText). Message thread only.
//
// Box select (M5 S1, BoxSelect.h): a drag on empty space draws a box that selects the notes it meets (Shift adds,
// Ctrl toggles); with the Nodes switch on it picks the nodes of the selected notes inside it, and a drag on one picked
// node moves every picked node together ("Move nodes"). A click on empty space clears the selection, as in the
// prototype.
//
// The right-click menu's shapes have hover ghosts (M5 S2): menuItemGhost makes the clip a shape would make, which the
// view draws and never applies. The lane-button menus are LaneMenu's. Not here: Sample mode's thumbnails (M6).
//
// Copy notes and Paste notes (M5 S3, the Copy / Paste tab) sit beside Copy lanes and Paste lanes: a paste and a
// Duplicate are one model call and one undo step each, refused past 4096 notes with the clip unchanged.

#include "BoxSelect.h"
#include "RollGeometry.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "doc/ClipDocument.h"
#include "doc/RollState.h"
#include "model/ClipEdit.h"
#include "model/Clip.h"
#include "model/LaneFormat.h"
#include "model/Shapes.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sl::ui::roll
{
    /** The pointer's modifier keys: shift, and ctrl = Ctrl (Windows) or Cmd (the prototype's ctrlKey || metaKey). */
    struct Mods
    {
        bool shift = false;
        bool ctrl = false;
    };

    /** Where the gestures write: the document, and the undo history (the processor in the plugin; a ClipDocument and
        an EditHistory in the tests). */
    struct RollHost
    {
        doc::ClipDocument& doc;

        /** One undo step called label for the document as it is now (StrandLockProcessor::pushUndoStep (label, {})).
            Returns false when nothing changed since the last step, so it adds none. */
        std::function<bool (const std::string& label)> commit;

        /** Plays a pitch briefly (a new note, a note moved to another row). May be empty. */
        std::function<void (int pitch)> audition;

        /** A short message for the toast. May be empty. */
        std::function<void (const std::string& text)> toast;
    };

    /** What the gestures read besides the document. */
    struct RollSettings
    {
        double snap = 0.25;          // the grid in beats; 0 = Free
        bool free = false;           // Freehand: a drag paints the active lane of a note
        bool multi = false;          // Multi: a click adds to the selection (as Ctrl)
        core::Inst inst = core::defaultInst();   // scale lock, key and scale; the glides; the readouts
        double bpm = 120.0;          // the readouts and the vibrato's squiggle
        double sampleSeconds = 6.0;  // the sound's length, for the Fit shape
        model::ShapeScope scope = model::ShapeScope::each;   // the menu's shapes: Each note or Across
        bool nodes = false;          // the Nodes switch (M5): in a lane mode a box picks the selected notes' nodes
    };

    /** The live readout beside the pointer while a drag changes something (sl-ui.js showReadout). */
    struct Readout
    {
        double x = 0.0, y = 0.0;     // roll pixels of the pointer
        std::string text;            // "Length 1.5 beats", "+3 semitones", "C3 · bar 2 beat 1" ...
    };

    /** A right-click menu entry, as data (sl-ui.js openContextMenu's items). */
    struct MenuItem
    {
        enum class Kind { item, header, separator, row };

        Kind kind = Kind::item;
        std::string label;
        bool on = false;             // a row button that is lit (Each note / Across)
        bool disabled = false;       // Paste lanes with nothing copied
        bool keepOpen = false;       // clicking it changes a setting and opens the menu again
        std::optional<core::LaneId> colour;   // a header in a lane's colour
        std::vector<MenuItem> row;   // Kind::row: its buttons, side by side
        int action = -1;             // what runMenuItem does (an index into the open menu's actions)
        int hover = -1;              // its ghost while the mouse is on it (an index into the open menu's hovers), or none
    };

    class RollGestures
    {
    public:
        explicit RollGestures (RollHost host);

        RollGestures (const RollGestures&) = delete;
        RollGestures& operator= (const RollGestures&) = delete;

        //==========================================================================================
        // Settings and the geometry the view draws with (the same one the gestures hit-test with).

        RollSettings& settings() noexcept { return set; }
        const RollSettings& settings() const noexcept { return set; }

        RollGeometry& geometry() noexcept { return geo; }
        const RollGeometry& geometry() const noexcept { return geo; }

        /** The current clip in the geometry (and the instrument, tempo of the settings). Every event calls it. */
        void sync();

        //==========================================================================================
        // Pointer events in roll pixels. timeMs is a clock in milliseconds (juce::Time::getMillisecondCounterHiRes
        // in the view); only differences matter, except for the menu's Random seed. Right and middle buttons never
        // reach pointerDown: a right press is contextMenu.

        void pointerDown (double x, double y, Mods mods, double timeMs);
        void pointerMove (double x, double y, Mods mods);
        void pointerUp();

        /** True while a press is held (a drag in progress, or a click not yet let go). */
        bool isDragging() const noexcept { return drag.has_value(); }

        /** A box being dragged (after 4 px): its corners in order, for the view to draw. Every move selects what it
            meets (BoxSelect.h), or with the Nodes switch picks the nodes inside it. */
        using Box = SelectBox;
        std::optional<Box> box() const;

        /** True while the box being dragged picks nodes (the Nodes switch) rather than notes. */
        bool isNodeBox() const noexcept;

        /** The readout to show, or nothing (hidden). */
        const std::optional<Readout>& readout() const noexcept { return shown; }

        //==========================================================================================
        // The right-click menu: a right press at (x, y) selects the note there first (or makes it the primary one),
        // then lists what applies. runMenuItem does an item's action; for a keepOpen item it returns the menu
        // opened again (Each note / Across), else nothing.

        std::vector<MenuItem> contextMenu (double x, double y, double timeMs);
        std::optional<std::vector<MenuItem>> runMenuItem (const MenuItem& item);

        /** What an item of the open menu would make while the mouse is on it (sl-ui.js shapeItems' hover: the shape on
            the selected notes), never applied; nothing for an item without a ghost. */
        std::optional<model::Clip> menuItemGhost (const MenuItem& item) const;

        //==========================================================================================
        // Commands (the menu, the keys, the buttons). Each finished one is one undo step, none when it changes nothing.

        /** Mode 1..8, with the prototype's side effects: Build turns Freehand off; Slide into is disarmed outside Build
            and Pitch. */
        void setMode (int mode);
        void setSubLane (core::LaneId lane);

        /** Freehand on or off (refused in Build, with a toast). */
        void toggleFreehand();

        /** Slide into: the next note clicked is the one the primary note slides into (Build or Pitch only). */
        void armLink();
        bool isLinkArmed() const noexcept { return linkArm; }

        /** The Nodes switch (sl-ui.js toggleNodes): refused in Build with a toast; each switch lets go of the picked
            nodes, and switched on with nothing selected it says to select notes first. */
        void toggleNodes();

        void duplicateSelection();
        void deleteSelection();
        void selectAll();
        void clearSelection();   // Escape: the selection and the picked nodes go

        void copyLanes();
        void pasteLanes();
        bool hasLaneClipboard() const noexcept { return clipboard.has_value(); }

        /** What Copy lanes holds: the lanes it copied (nothing until something is copied). */
        const std::optional<model::LaneClipboard>& laneClipboard() const noexcept { return clipboard; }

        /** The lanes Copy lanes copies (every lane by default; the Copy / Paste tab, M5, changes it and keeps it in
            UIST). */
        std::array<bool, core::kNumLanes> copyMask;

        /** Copy notes (the Copy / Paste tab, M5 S3; the prototype pastes only lanes): the selected notes as they are
            now, or a toast with nothing selected. */
        void copyNotes();

        /** Paste notes: copies of the copied notes, the earliest where the selected notes end (with nothing selected,
            where the copied notes ended), through model::pasteNotes (links inside the group kept). As Duplicate,
            copies that would start at or past the clip's end are left out and the rest end at it; the pasted notes are
            selected; one undo step "Paste notes". Past 4096 notes it is refused with a toast and the clip unchanged
            (7.7 decision 5). */
        void pasteNotes();

        /** The same with the earliest copy at a beat of its own (the right-click menu's Paste notes here). */
        void pasteNotesAt (double beat);

        /** What Copy notes holds (empty until something is copied). */
        const std::vector<model::NotePtr>& noteClipboard() const noexcept { return copiedNotes; }

        /** The selected notes' ids in time order (sl-ui.js getSelection): what every selection tool acts on. */
        std::vector<std::string> selectionInTimeOrder() const;

    private:
        model::ClipPtr clip() const { return host.doc.current(); }
        doc::RollState roll() const { return host.doc.rollState(); }
        model::NotePtr noteById (const std::string& id) const;
        model::NotePtr primaryNote() const;

        double total() const;
        double minLen() const noexcept { return set.snap > 0.0 ? set.snap : 0.125; }
        double snapR (double b) const;
        double scaleSnap (double p) const;
        model::LaneCtx formatCtx (const model::Note* n) const;

        bool isDouble (double x, double y, int what, double now);
        void show (model::Clip c);
        void show (model::EditResult r);
        bool setClipLabelled (model::Clip c, const std::string& label);
        void toast (const std::string& text) const;
        void audition (double p) const;
        void showReadout (double x, double y, std::string text);

        bool pickNote (const model::Note& h, bool add);
        void linkNotes (const model::Note& P, const model::Note& h);
        void bandClick (const std::string& id, bool link, bool add);
        void addNodeAt (const model::Note& n, core::LaneId lane, double x, double y, bool onLine);
        void applyBox();
        void moveNodes (double x, double y, bool shift);
        void paint (double x, double y);
        std::optional<model::NodeList> bendEndNodes (const model::Note& n, bool start, double y, double y0, double v0, bool shift) const;

        std::vector<MenuItem> openMenu (double x, double y, const std::string& noteId);
        std::vector<MenuItem> shapeItems (core::LaneId lane, const std::vector<std::string>& ids, const std::string& header);

        RollHost host;
        RollSettings set;
        RollGeometry geo;

        // The gesture in progress (sl-ui.js drag).
        struct Drag
        {
            enum class Type { gstart, gmid, node, seg, end, start, free, move, box, nodes };
            Type t = Type::box;
            std::string id;
            core::LaneId lane = core::LaneId::pitch;
            int k = 0;                    // the node or segment; -1: a node a full lane kept no place for (moves nothing)
            model::NodeList nodes0;
            model::ClipPtr clip0;
            std::string label;
            bool force = false, moved = false;
            double x0 = 0.0, y0 = 0.0, gx = 0.0, gy = 0.0;
            bool hasDeadZone = false;     // a node press (not one made by a double-click) moves after 3 px
            double v0 = 0.0, endBeat = 0.0, c0 = 0.0, d = 0.0, b0 = 0.0;
            double x1 = 0.0, y1 = 0.0;
            bool shift = false, ctrl = false;
            std::optional<std::string> clickId;   // a Shift or Ctrl press on a note's band: the click it was
            bool clickLink = false, clickAdd = false;
            bool nodeBox = false;                 // box: it picks nodes (the Nodes switch), not notes
            std::optional<std::string> clickNote; // a node box started on a selected note: a click makes it the primary
            std::vector<std::string> base;        // box: the selection when it started
            std::vector<doc::NodePick> baseNodes; // box: the picked nodes when it started; nodes: the nodes moved
            std::vector<std::string> ids;         // move: the notes moved, and where each started
            std::vector<std::pair<double, double>> orig;   // (s, p) per id
            int p0 = 0;
            double lastP = 0.0;
            std::string hid;
            std::vector<double> arr;              // Freehand: 97 values across the note
            int li = -1;
            double lv = 0.0;
        };
        std::optional<Drag> drag;

        struct Tap
        {
            double t = 0.0, x = 0.0, y = 0.0;
            int what = -1;
        };
        Tap lastTap;

        bool linkArm = false;
        std::optional<Readout> shown;
        std::optional<model::LaneClipboard> clipboard;
        std::vector<model::NotePtr> copiedNotes;   // Copy notes (immutable versions: nothing copies them twice)

        /** Duplicate's and Paste notes' second half (sl-ui.js duplicateSel): the copies a model call made, less those
            that start at or past the clip's end, the rest cut to end at it, as one undo step called label. Returns the
            copies kept (in order), or nothing when the document refused it (with its toast). */
        std::optional<std::vector<std::string>> placeCopies (const model::EditResult& made, const std::string& label);

        // The open menu's actions (MenuItem::action indexes them), its ghosts (MenuItem::hover) and where it opened.
        std::vector<std::function<void()>> menuActions;
        std::vector<bool> menuReopens;
        std::vector<std::function<std::optional<model::Clip>()>> menuHovers;
        double menuX = 0.0, menuY = 0.0, menuTime = 0.0;
        std::string menuNote;
    };
}
