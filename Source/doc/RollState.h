#pragma once

// The roll's state beside the clip (PLUGIN-PLAN 7.6, M4): the selected notes and the primary one, the mode and the
// Sample sub-lane, and the node the inspector shows. ClipDocument holds it (its selection rules are there); this
// header is plain C++ so the roll's pure parts (RollGeometry, RollGestures) can read it.

#include "core/Lanes.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace sl::doc
{
    /** The node the inspector shows: the one last clicked or dragged in a lane mode (sl-ui.js activeNode). */
    struct ActiveNode
    {
        std::string id;
        core::LaneId lane = core::LaneId::pitch;
        int k = 0;
        int len = 0;    // the lane's node count when it was picked: when that count changes the node is let go
    };

    struct RollState
    {
        std::vector<std::string> selected;   // in the order they were added (the prototype's Set), no repeats
        std::string primary;                 // empty = none
        int mode = 1;                        // 1 Build, 2..7 one lane each, 8 Sample
        core::LaneId subLane = core::LaneId::travel;   // mode 8's lane: Travel, Heads, Grain or Refresh
        std::optional<ActiveNode> activeNode;

        bool isSelected (const std::string& id) const noexcept
        {
            return std::find (selected.begin(), selected.end(), id) != selected.end();
        }

        /** The lane a lane mode edits (mode 8: the sub-lane); nothing in Build (sl-ui.js activeLane). */
        std::optional<core::LaneId> activeLane() const noexcept
        {
            if (mode <= 1)
                return std::nullopt;
            if (mode >= 8)
                return subLane;
            return core::kModes[mode - 1].lanes[0];
        }
    };
}
