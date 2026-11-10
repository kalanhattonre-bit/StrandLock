#pragma once

// Undo (PLUGIN-PLAN 2.4): model::VersionHistory over the whole document state,
// DocState { ClipPtr clip; NonParamState np; ParamValues pv; }. The clip and the non-parameter state are shared
// immutable versions, so an undo step costs only the notes it changed.
//
// Undo and automation: when an action is pushed, pv copies the previous state's values and overwrites only the
// parameters that action changed. Undo and Redo then write only the parameters whose pv differs between the
// two states, so a value Cubase automation moved in between is never put back by Undo. Bypass is the host's and
// never part of a state (it is kept at its default in pv).
//
// Message thread only.

#include "ClipDocument.h"

#include "model/History.h"
#include "params/ParamIds.h"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sl::doc
{
    /** Every parameter's plain value (the state's PARM values), in the frozen order. */
    using ParamValues = std::array<float, params::kNumParams>;

    struct DocState
    {
        model::ClipPtr clip;
        NonParamState np;
        ParamValues pv {};
    };

    /** The same clip (sameClip), the same non-parameter state, and every pv value the same bits. */
    struct SameDocState
    {
        bool operator() (const DocState& a, const DocState& b) const;
    };

    class EditHistory
    {
    public:
        explicit EditHistory (int limit = model::kHistoryLimit);

        /** Forgets everything and starts from state, labelled "start" (a new instance, a state load). */
        void reset (DocState state);

        /** Adds an action after the current state: its clip and non-parameter state, and the parameters it
            changed (index, new plain value); every other pv value is the previous state's. False (nothing added)
            when the result is the same state as the current one, or before any reset. */
        bool push (std::string label, model::ClipPtr clip, NonParamState np, const std::vector<std::pair<int, float>>& changedParams);

        /** A step to apply: the state reached, and the parameters to write (those whose pv differs between the
            state left and the state reached), with their values. */
        struct Step
        {
            DocState state;
            std::vector<std::pair<int, float>> params;
            std::string label;   // the action undone or redone
        };

        std::optional<Step> undo();
        std::optional<Step> redo();

        bool canUndo() const noexcept { return history.canUndo(); }
        bool canRedo() const noexcept { return history.canRedo(); }
        std::optional<std::string> undoLabel() const { return history.undoLabel(); }
        std::optional<std::string> redoLabel() const { return history.redoLabel(); }

        std::optional<DocState> current() const { return history.current(); }
        std::size_t size() const noexcept { return history.size(); }

    private:
        static std::vector<std::pair<int, float>> differences (const ParamValues& from, const ParamValues& to);

        model::VersionHistory<DocState, SameDocState> history;
    };
}
