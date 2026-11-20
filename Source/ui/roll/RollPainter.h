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
//
// M6 (plan 7.8 T2, decision 5): after each strand, the note's decorations (sl-ui-panels.js decorate, drawn in every
// mode with the prototype's alphas): its thumbnail above the line and its chevrons under it (NoteDecor), the dot of a
// note with settings of its own, and the warning where its slide goes into a note that starts well inside it.

#include "NoteDecor.h"
#include "RollGeometry.h"
#include "RollGestures.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "doc/RollState.h"
#include "model/Clip.h"
#include "ui/Theme.h"

#include <cstdint>
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
        doc::RollState roll;                     // the selection, the primary note, the mode, the inspector's node,
                                                 // the picked nodes (M5)
        core::Inst inst = core::defaultInst();   // the key and scale (rows), Refresh's mode (dims its ribbon)
        bool free = false;                       // Freehand: no slide diamonds, no segment diamonds
        bool nodes = false;                      // the Nodes switch: with nodes picked, no segment diamonds (M5)
        double snap = 0.25;                      // the Grid in beats (0 = Free: the grid shows sixteenths)
        model::ClipPtr ghost;                    // a preview of another clip (M5's hover ghosts); null = none
        const RollGeometry* ghostGeo = nullptr;  // the ghost's own geometry (its glides), with the same zoom
        NoteDecor* decor = nullptr;              // the notes' chevrons and thumbnails (M6); null = none drawn
        std::uint64_t decorKey = 0;              // NoteDecor::key of the instrument: what every note's decorations read
        std::optional<Readout> readout;          // the live readout beside the pointer (roll pixels)
        std::optional<RollGestures::Box> box;    // a box being dragged (roll pixels): it selects notes or picks nodes

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
            Travel middle line, the note line with its squiggle, the start dot and the end dot; then its decorations. */
        void paintNote (juce::Graphics&, const model::Note&, bool selected) const;

        /** What a note's decorations draw (sl-ui-panels.js decorate, M6 decision 5), in roll pixels, in the lane-sample
            colour unless it says: the thumbnail's columns in the wave colour (none without a sound, or on a note under
            12 px), the chevrons under the line (the Mirror ones at half strength), two bars where it stands still, a
            square where it falls silent (At the end: Stop), Scatter's jumps (at 0.85 of the strength), the dot of a note
            with settings of its own, and the warning where its slide goes into a note that starts well inside it. */
        struct Decorations
        {
            struct Chevron
            {
                double x = 0.0, y = 0.0;
                int dir = 1;          // 1 points right (forwards), -1 left
                double scale = 1.0;
            };

            float thumbAlpha = 0.0f, strength = 0.0f;
            std::vector<juce::Rectangle<float>> columns;
            std::vector<Chevron> chevrons, mirrors;
            std::vector<juce::Rectangle<float>> stops, jumps;
            std::optional<juce::Rectangle<float>> ownDot;
            std::optional<juce::Point<double>> warning;   // where the target starts, on this note's line
        };

        Decorations decorations (const model::Note& n, bool selected) const;

        /** Where a note's drawing can reach (roll pixels): its line, ribbons and dots, plus room for the line's
            mitred joins and the antialiased edge, and its decorations under the line. */
        juce::Rectangle<double> noteBounds (const model::Note& n) const { return noteBoundsIn (*s.geo, n); }
        static juce::Rectangle<double> noteBoundsIn (const RollGeometry&, const model::Note&);

        /** What is drawn over the notes, in sl-ui.js's order: the slide links, the handles, the ghost, the box. */
        void paintOverlay (juce::Graphics&) const;

        /** A slide link the ghost would make or take away (sl-ui-panels.js decorate, "the ghost's slide links"): a note
            whose ghost slides into another note than it does now draws a dashed line from where it ends now to the start
            of the note the ghost slides it into, with a dot there; a note whose ghost slides nowhere draws a cross half
            way along the link it has now. In roll pixels. */
        struct GhostLink
        {
            double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
            bool removed = false;   // a cross half way from (x1, y1) to (x2, y2), not a line and a dot
        };

        /** The ghost's slide links, in the clip's note order (none without a ghost). */
        std::vector<GhostLink> ghostLinks() const;

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

        /** The padding under the line's lowest point: the decorations reach 15.5 px below the line (a Mirror chevron
            under a chevron), plus the antialiased edge. */
        static constexpr double decorPad = 18.0;

    private:
        void paintDecorations (juce::Graphics&, const model::Note&, bool selected) const;
        void paintLinks (juce::Graphics&) const;
        void paintHandles (juce::Graphics&) const;
        void paintGhost (juce::Graphics&) const;
        void paintBox (juce::Graphics&) const;

        const PaintState& s;
    };
}
