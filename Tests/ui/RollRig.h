#pragma once

// A roll with no window for the gesture tests: a ClipDocument, an EditHistory (the processor's undo, without the
// processor), and RollGestures writing into both. commit counts the undo steps pushed; the toasts and auditions are
// kept in order.

#include "doc/ClipDocument.h"
#include "doc/EditHistory.h"
#include "model/Clip.h"
#include "params/ParamBridge.h"
#include "ui/roll/RollGestures.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sl::test
{
    struct RollRig
    {
        params::NonParamAtomics atomics;
        doc::ClipDocument doc { atomics };
        doc::EditHistory history;
        int steps = 0;                      // undo steps pushed since the start
        std::vector<std::string> labels;    // their labels
        std::vector<std::string> toasts;
        std::vector<int> auditions;
        std::unique_ptr<ui::roll::RollGestures> g;

        explicit RollRig (const model::Clip& clip)
        {
            doc.replace (std::make_shared<const model::Clip> (clip));
            doc::DocState start;
            start.clip = doc.current();
            start.np = doc.nonParam();
            history.reset (start);
            ui::roll::RollHost host { doc,
                                      [this] (const std::string& label)
                                      {
                                          const bool pushed = history.push (label, doc.current(), doc.nonParam(), {});
                                          if (pushed)
                                          {
                                              ++steps;
                                              labels.push_back (label);
                                          }
                                          return pushed;
                                      },
                                      [this] (int p) { auditions.push_back (p); },
                                      [this] (const std::string& t) { toasts.push_back (t); } };
            g = std::make_unique<ui::roll::RollGestures> (std::move (host));
        }

        RollRig (const RollRig&) = delete;
        RollRig& operator= (const RollRig&) = delete;

        ui::roll::RollGestures& gestures() noexcept { return *g; }

        /** The processor's undo, on the document alone. */
        bool undo()
        {
            const auto step = history.undo();
            if (! step)
                return false;
            doc.setNonParam (step->state.np);
            doc.replace (step->state.clip);
            return true;
        }

        std::optional<std::string> lastToast() const
        {
            if (toasts.empty())
                return std::nullopt;
            return toasts.back();
        }
    };
}
