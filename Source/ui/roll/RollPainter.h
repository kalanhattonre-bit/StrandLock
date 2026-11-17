#pragma once

// The roll's drawing (PLUGIN-PLAN 4 M4, 7.6 R2): sl-ui.js drawRoll, ribbonPath, drawNote, drawLinks, drawHandles,
// drawGhost, the box and showReadout, read line by line. The rows in the key's colours, the grid, every note as a
// strand (its ribbons in the lane colours, its line with the vibrato squiggle, its start and end dots), the slide
// links, the handles of the active lane (nodes, segment diamonds, the slide diamonds of the primary note), a ghost
// preview, the box being dragged and the live readout. Every colour is a theme token.
//
// Everything is drawn in roll pixels (RollGeometry's: nothing scrolled); the caller moves the Graphics to the
// scroll. The readout alone is placed in view pixels, as the prototype places its readout box in the scroller.
//
// It also says where each piece can draw (noteBounds, overlayBounds), so the view repaints only what changed and
// the NoteLayerCache draws again only the notes that changed. Message thread only.

#include "RollGeometry.h"
#include "RollGestures.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "doc/RollState.h"
#include "model/Clip.h"
#include "ui/Theme.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sl::ui::roll
{
    /** Everything one picture of the roll is drawn from, besides the geometry's zoom and note lines. The view
        takes it once per refresh, so every paint until the next refresh draws the same state. */
    struct PaintState
    {
        const Palette* colours = nullptr;        // the theme's tokens (set again at every paint)
        const RollGeometry* geo = nullptr;       // the zoom, the rows, each note's line and lane tables, the glides
        model::ClipPtr clip;                     // the clip shown (the geometry's)
        doc::RollState roll;                     // the selection, the primary note, the mode, the inspector's node
        core::Inst inst = core::defaultInst();   // the key and scale (rows), Refresh's mode (dims its ribbon)
        bool free = false;                       // Freehand: no slide diamonds, no segment diamonds
        double snap = 0.25;                      // the Grid in beats (0 = Free: the grid shows sixteenths)
        model::ClipPtr ghost;                    // a preview of another clip (M5's hover ghosts); null = none
        const RollGeometry* ghostGeo = nullptr;  // the ghost's own geometry (its glides), with the same zoom
        std::optional<Readout> readout;          // the live readout beside the pointer (roll pixels)
        std::optional<RollGestures::Box> box;    // a box being dragged on empty space (roll pixels)

        /** Indexes the clip's notes by id and the selection (call it after setting clip and roll). */
        void prepare();

        bool isSelected (const std::string& id) const { return selected.count (id) != 0; }
        bool isPrimary (const std::string& id) const { return ! roll.primary.empty() && id == roll.primary; }
        model::NotePtr noteById (const std::string& id) const;

    private:
        std::unordered_map<std::string, model::NotePtr> byId;
        std::unordered_set<std::string> selected;
    };

    class RollPainter
    {
    public:
        explicit RollPainter (const PaintState& paintState) noexcept : s (paintState) {}

        /** The rows (the key's root, the notes outside the scale, the black keys, octave lines) and the grid
            (bars, beats, the Grid's steps) over the part of the roll visible shows. Only up to the clip's end. */
        void paintRows (juce::Graphics&, juce::Rectangle<double> visible) const;

        /** The notes visible shows, in drawing order: the others first, then the selected ones, then the primary
            one (the clip's order within each). */
        std::vector<model::NotePtr> visibleNotes (juce::Rectangle<double> visible) const;

        /** One note (sl-ui.js drawNote): the ribbons of the sound lanes (the active lane last and stronger), the
            Travel middle line, the note line with its squiggle, the start dot and the end dot. */
        void paintNote (juce::Graphics&, const model::Note&, bool selected) const;

        /** Where a note's drawing can reach (roll pixels): its line, ribbons and dots, plus room for the line's
            mitred joins and the antialiased edge. */
        juce::Rectangle<double> noteBounds (const model::Note& n) const { return noteBoundsIn (*s.geo, n); }
        static juce::Rectangle<double> noteBoundsIn (const RollGeometry&, const model::Note&);

        /** What is drawn over the notes, in sl-ui.js's order: the slide links, the handles, the ghost, the box. */
        void paintOverlay (juce::Graphics&) const;

        /** The readout box, with the Graphics at the view's origin (visible: the view in roll pixels). */
        void paintReadout (juce::Graphics&, juce::Rectangle<double> visible) const;

        /** Where the overlay and the readout draw (roll pixels): what the view repaints when they change. */
        std::vector<juce::Rectangle<double>> overlayBounds (juce::Rectangle<double> visible) const;

        /** The readout box in view pixels (showReadout: 14 px right of the pointer and 30 px up, kept inside). */
        std::optional<juce::Rectangle<double>> readoutBox (juce::Rectangle<double> visible) const;

        /** A lane's colour: its own token, or lane-sample for the four Sample lanes (sl-ui.js laneColor). */
        static Tok laneToken (core::LaneId) noexcept;
        static juce::Colour laneColour (const Palette&, core::LaneId) noexcept;

        /** The padding noteBounds adds around a note's line, ribbons and dots. */
        static constexpr double notePad = 14.0;

    private:
        void paintLinks (juce::Graphics&) const;
        void paintHandles (juce::Graphics&) const;
        void paintGhost (juce::Graphics&) const;
        void paintBox (juce::Graphics&) const;

        const PaintState& s;
    };
}
