#pragma once

// Box select (PLUGIN-PLAN 4 M5, 7.7 S1; idea Y1; sl-ui.js applyBox): what a box dragged on the roll selects. A drag on
// empty space draws a box in every mode; once it has moved 4 px, every move selects the notes whose box it meets.
// A plain box replaces the selection, Shift adds to the selection the box started with, Ctrl toggles it. With the
// Nodes switch on (a lane mode, notes selected), the box picks the nodes of the selected notes that lie inside it
// instead, with the same keys.
//
// RollGestures holds the drag (it is part of its state machine, so the tests need no mouse) and calls these pure
// functions on every move; the roll draws the box (RollPainter). Roll pixels throughout (nothing scrolled).

#include "RollGeometry.h"

#include "core/Lanes.h"
#include "doc/RollState.h"
#include "model/Clip.h"

#include <string>
#include <vector>

namespace sl::ui::roll
{
    /** A box on the roll in roll pixels, its corners in order (x0 <= x1, y0 <= y1). */
    struct SelectBox
    {
        double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;

        /** The box between two corners given in any order (sl-ui.js applyBox's min and max). */
        static SelectBox between (double ax, double ay, double bx, double by) noexcept;
    };

    /** The notes whose box (RollGeometry::noteBox, with its lane band when withRibbon: every mode but Build) meets the
        box, edges included, in clip order. */
    std::vector<std::string> notesInBox (const RollGeometry& geo, const model::Clip& clip, bool withRibbon, const SelectBox& box);

    /** sl-ui.js applyBox for notes, from base (the selection when the box started) and the notes in the box: Shift =
        base, then each hit not in it; else Ctrl = base without the hits it held, then the hits it did not hold; else
        the hits alone. (Shift wins when both are held, as the prototype tests it first.) */
    std::vector<std::string> combineNotes (const std::vector<std::string>& base, const std::vector<std::string>& hits, bool shift, bool ctrl);

    /** The nodes of lane inside the box (edges included), for each selected note in time order
        (RollGeometry::selectedNotes); a note with none inside is left out. */
    std::vector<doc::NodePick> nodesInBox (const RollGeometry& geo, const model::Clip& clip, const doc::RollState& state, core::LaneId lane,
                                           const SelectBox& box);

    /** sl-ui.js applyBox for nodes, from base (the picks when the box started) and the nodes in the box: with Shift or
        Ctrl it starts from base, else from nothing; each hit node is added, or with Ctrl taken out when it is already
        picked; a note left with no picks goes. Notes keep their place in the order; a new one goes at the end. */
    std::vector<doc::NodePick> combineNodes (const std::vector<doc::NodePick>& base, const std::vector<doc::NodePick>& hits, bool shift, bool ctrl);
}
