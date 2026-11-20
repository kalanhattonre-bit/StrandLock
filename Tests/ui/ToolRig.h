#pragma once

// The selection bar's tools with no window, for the M5 tests: a RollRig (a ClipDocument, an EditHistory and the
// gestures) and the bar's SelectionTools on the same document and history. It counts the undo steps (with their
// labels), keeps the toasts (with their times), counts every new clip version the document makes (each is what the
// processor hands to the SnapshotPublisher), and draws the ghosts into a GhostPreview whose previews it counts.

#include "RollRig.h"

#include "core/Lanes.h"
#include "core/Rates.h"
#include "model/Clip.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "model/Generators.h"
#include "ui/roll/GhostPreview.h"
#include "ui/roll/SelectionBar.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
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

    /** The ids in time order (what the bar acts on: RollGestures::selectionInTimeOrder), for the direct model calls. */
    inline std::vector<std::string> inTimeOrder (const model::Clip& c, const std::vector<std::string>& sel)
    {
        const std::unordered_set<std::string> set (sel.begin(), sel.end());
        std::vector<model::NotePtr> notes;
        for (const auto& n : c.notes)
            if (set.count (n->id) != 0)
                notes.push_back (n);
        std::vector<std::string> out;
        for (const auto& n : model::timeOrder (notes))
            out.push_back (n->id);
        return out;
    }

    /** sl-ui-panels.js putGen, computed another way: setGen puts a copy of the generator on every note, then the notes
        that had a generator on the lane get their own seed and key back (and their invert when it was the same kind). */
    inline model::Clip expectedPutGen (const model::Clip& c, const std::vector<std::string>& ids, core::LaneId lane, const model::Gen& g)
    {
        model::Clip out = model::setGen (c, ids, lane, g);
        const std::unordered_set<std::string> sel (ids.begin(), ids.end());
        std::map<std::string, model::Gen> old;
        for (const auto& n : c.notes)
            if (sel.count (n->id) != 0 && n->genOf (lane))
                old[n->id] = *n->genOf (lane);
        if (old.empty())
            return out;
        std::vector<std::string> had;
        for (const auto& entry : old)
            had.push_back (entry.first);
        return model::editNotes (out, had, [&old, lane, &g] (model::Note& n, const model::Note&)
        {
            const auto& o = old.at (n.id);
            auto& now = *n.genOf (lane);
            now.seed = o.seed;
            now.key = o.key;
            now.invert = o.kind == g.kind ? o.invert : false;
        });
    }

    /** The drawers' generators as they start (sl-ui-panels.js S.pat and S.rnd), through makeGen. */
    inline model::Gen startPattern (model::GenShape shape = model::GenShape::squiggle)
    {
        model::GenPatch p;
        p.shape = shape;
        p.rate = core::RateId::r1_16T;
        p.d0 = 0.3;
        p.d1 = 0.3;
        p.warp = 0.0;
        p.lockToBeat = true;
        p.perNote = model::PerNote::same;
        return model::makeGen (model::GenKind::pattern, p);
    }

    inline model::Gen startRandom()
    {
        model::GenPatch p;
        p.rate = core::RateId::r1_16T;
        p.amount = 0.5;
        p.smooth = 0.0;
        p.perNote = model::PerNote::same;
        p.lockToBeat = true;
        return model::makeGen (model::GenKind::random, p);
    }

    /** A selected note has a generator on the lane. */
    inline bool hasGen (const model::Clip& c, const std::vector<std::string>& ids, core::LaneId lane)
    {
        const std::unordered_set<std::string> sel (ids.begin(), ids.end());
        for (const auto& n : c.notes)
            if (sel.count (n->id) != 0 && n->genOf (lane))
                return true;
        return false;
    }
}
