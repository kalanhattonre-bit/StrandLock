#pragma once

// The roll's state beside the clip (PLUGIN-PLAN 7.6, M4; 7.7, M5): the selected notes and the primary one, the mode
// and the Sample sub-lane, the node the inspector shows, and the nodes picked with the Nodes switch (M5). ClipDocument
// holds it (its selection rules are there); this header is plain C++ so the roll's pure parts (RollGeometry,
// RollGestures, BoxSelect) can read it.

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

    /** The nodes picked on one note with the Nodes switch (sl-ui.js app.nodeSel and app.nodeLen): their indexes on the
        lane the mode shows, in the order they were picked, and that lane's node count when they were picked. The
        picks are indexes, so when the count changes they would name other nodes, and they are let go. */
    struct NodePick
    {
        std::string id;
        std::vector<int> ks;   // no repeats
        int len = -1;          // -1: the note or the lane was missing (let go at the next clip version)

        bool has (int k) const noexcept { return std::find (ks.begin(), ks.end(), k) != ks.end(); }
    };

    struct RollState
    {
        std::vector<std::string> selected;   // in the order they were added (the prototype's Set), no repeats
        std::string primary;                 // empty = none
        int mode = 1;                        // 1 Build, 2..7 one lane each, 8 Sample
        core::LaneId subLane = core::LaneId::travel;   // mode 8's lane: Travel, Heads, Grain or Refresh
        std::optional<ActiveNode> activeNode;
        std::vector<NodePick> picks;         // the picked nodes per note, in the order the notes were picked (a Map)

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

        /** The picks on one note, or null. */
        const NodePick* picksOf (const std::string& id) const noexcept
        {
            for (const auto& p : picks)
                if (p.id == id)
                    return &p;
            return nullptr;
        }

        /** Every picked node, on every note. */
        int pickCount() const noexcept
        {
            int n = 0;
            for (const auto& p : picks)
                n += (int) p.ks.size();
            return n;
        }
    };
}
