#pragma once

// The lane buttons' menus (PLUGIN-PLAN 4 M5 "the lane-button menu arrows (Y2-Y4)", 7.7 S2; sl-ui-panels.js
// openLaneMenu and hookLaneArrows): the small arrow beside each lane button on the mode bar opens a menu for that lane
// on the selected notes. From the top: "Formant on 4 notes" in the lane's colour, Each note or Across, the lane's
// shapes, then the drawer's pattern ("Pattern: squiggle 1/16 triplet") and Pattern settings..., the drawer's random
// ("Random 1/16 triplet") and Random settings... (they open that drawer for the lane), Re-roll the random, Remove
// pattern / random and Turn into nodes (greyed while no selected note has a pattern or random on the lane).
//
// The roll shows the lane first. A click is one model call and one undo step (7.7 decision 1), through the selection
// bar's tools; the mouse on an item draws its ghost (decision 2). The Sample button's arrow opens the menu of the
// Sample lane the roll shows. Message thread only.

#include "GhostPreview.h"
#include "RollGestures.h"
#include "SelectionBar.h"

#include "core/Lanes.h"

#include <functional>
#include <string>
#include <vector>

namespace sl::ui::roll
{
    class RollView;

    /** A lane menu's items, what each one does and what each one would make (its ghost). */
    struct LaneMenu
    {
        std::vector<MenuItem> items;
        std::vector<std::function<void()>> actions;   // MenuItem::action indexes them
        std::vector<GhostPreview::Make> hovers;        // MenuItem::hover indexes them
    };

    /** The menu of a lane for the notes in ids (sl-ui-panels.js openLaneMenu's items). reopen opens it again in its
        place (Each note / Across keep it open); setScope changes the scope (the roll keeps it in UIST); openSettings
        opens the Pattern or Random drawer (Pattern settings..., Random settings...). */
    LaneMenu laneMenu (SelectionTools& tools, core::LaneId lane, const std::vector<std::string>& ids, std::function<void()> reopen,
                       std::function<void (model::ShapeScope)> setScope, std::function<void (ToolSettings::Drawer)> openSettings);

    /** Opens a lane's menu under a lane button's arrow, 4 px below it: the roll shows the lane; with nothing selected
        a toast says to select notes first. openSettings opens a drawer of the selection bar. */
    void openLaneMenu (RollView& roll, core::LaneId lane, juce::Component& arrow, std::function<void (ToolSettings::Drawer)> openSettings);

    /** The toast with nothing selected. */
    inline constexpr const char* kLaneMenuNothingSelected = "Select notes first: click one, or drag a box on empty space";
}
