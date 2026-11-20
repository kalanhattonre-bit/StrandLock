#pragma once

// The selection bar's tools with no window, for the M5 tests: a RollRig (a ClipDocument, an EditHistory and the
// gestures) and the bar's SelectionTools on the same document and history. It counts the undo steps (with their
// labels), keeps the toasts (with their times), counts every new clip version the document makes (each is what the
// processor hands to the SnapshotPublisher), and draws the ghosts into a GhostPreview whose previews it counts.

#include "RollRig.h"

#include "model/Clip.h"
#include "model/ClipCodec.h"
#include "ui/roll/GhostPreview.h"
#include "ui/roll/SelectionBar.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sl::test
{
    struct ToolRig
    {
        RollRig rig;
        std::vector<std::pair<std::string, int>> toasts;
        int versions = 0;                // new clip versions since the start (each one would be published)
        int drawn = 0;                   // previews handed to the roll to draw
        model::ClipPtr drawnClip;        // what the roll would draw now (null: no ghost)
        ui::roll::GhostPreview ghosts;
        std::unique_ptr<ui::roll::SelectionTools> tools;

        explicit ToolRig (const model::Clip& clip)
            : rig (clip),
              ghosts ([this] (model::ClipPtr c)
              {
                  if (c != nullptr)
                      ++drawn;
                  drawnClip = std::move (c);
              })
        {
            rig.doc.onClipChanged = [this] (const model::ClipPtr&, std::uint64_t) { ++versions; };
            ui::roll::ToolHost host { rig.doc,
                                      [this] (const std::string& label)
                                      {
                                          const bool pushed = rig.history.push (label, rig.doc.current(), rig.doc.nonParam(), {});
                                          if (pushed)
                                          {
                                              ++rig.steps;
                                              rig.labels.push_back (label);
                                          }
                                          return pushed;
                                      },
                                      [this] (const std::string& text, int ms) { toasts.emplace_back (text, ms); } };
            tools = std::make_unique<ui::roll::SelectionTools> (std::move (host), rig.gestures());
            tools->setGhosts (&ghosts);
        }

        ToolRig (const ToolRig&) = delete;
        ToolRig& operator= (const ToolRig&) = delete;

        ~ToolRig()
        {
            rig.doc.onClipChanged = nullptr;
        }

        ui::roll::ToolSettings& settings() { return tools->settings(); }

        void select (const std::vector<std::string>& ids, const std::string& primary = {})
        {
            rig.doc.setSelection (ids, primary.empty() ? std::optional<std::string>() : std::optional<std::string> (primary));
            tools->sync();
        }

        /** The roll in Build with this Lane (the lane tools then act on it), or the lane's own mode. */
        void lane (core::LaneId l, bool showItsMode = false)
        {
            if (showItsMode)
            {
                const auto& info = core::laneInfo (l);
                if (info.sub)
                    rig.gestures().setSubLane (l);
                else
                    rig.gestures().setMode (info.mode);
            }
            else
            {
                rig.gestures().setMode (1);
            }
            tools->sync();
            settings().lane = l;
            tools->sync();
        }

        std::vector<std::uint8_t> bytes() const { return model::encodeClip (*rig.doc.current()); }
        std::string lastToast() const { return toasts.empty() ? std::string() : toasts.back().first; }
        std::string lastLabel() const { return rig.labels.empty() ? std::string() : rig.labels.back(); }
    };

    inline std::vector<std::string> allIdsOf (const model::Clip& c)
    {
        std::vector<std::string> ids;
        for (const auto& n : c.notes)
            ids.push_back (n->id);
        return ids;
    }
}
