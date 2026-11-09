#pragma once

// Undo (sl-model.js History; PLUGIN-PLAN 1.2, 1.3): a list of immutable versions with labels. The prototype
// stores each version as JSON text, one string per note, and shares the text of a note that did not change.
// Here a state holds shared pointers to immutable versions (a ClipPtr, or a struct of them), so an
// unchanged note is shared by pointer, and undo, redo and current hand back the stored state itself: nothing
// in it can be changed through it.

#include "Clip.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace sl::model
{
    /** History's default limit: 200 undo steps (201 versions). */
    inline constexpr int kHistoryLimit = 200;

    /** Two clips hold the same state: the same settings, extras and note count, and each note either the same
        object or the same content (sameContent) with the same extras. The prototype compares the JSON text. */
    bool sameClip (const Clip& a, const Clip& b) noexcept;

    /** sameClip on clip versions (two null pointers are the same; one null pointer is not). */
    struct SameClipPtr
    {
        bool operator() (const ClipPtr& a, const ClipPtr& b) const noexcept
        {
            if (a == b)
                return true;
            return a != nullptr && b != nullptr && sameClip (*a, *b);
        }
    };

    /** SLModel.History: push adds a version after the current one (dropping the redo list) unless it is the
        same state as the current one; past `limit` undo steps the oldest version is dropped. Same tells two
        states apart (equal_to by default). */
    template <class State, class Same = std::equal_to<State>>
    class VersionHistory
    {
    public:
        /** An empty history. A limit that is not at least 1 reads 1 (the prototype's max (1, limit | 0)). */
        explicit VersionHistory (int limit = kHistoryLimit, Same same = Same())
            : lim (std::max (1, limit)), sameState (std::move (same))
        {
        }

        /** A history whose first version is initial, labelled "start". */
        VersionHistory (State initial, int limit = kHistoryLimit, Same same = Same())
            : VersionHistory (limit, std::move (same))
        {
            push (std::move (initial), "start");
        }

        /** Adds state after the current version, labelled. Returns false (and changes nothing) when it is the
            same state as the current version. */
        bool push (State state, std::string label = {})
        {
            if (cur >= 0 && sameState (versions[static_cast<std::size_t> (cur)], state))
                return false;
            const auto keep = static_cast<std::ptrdiff_t> (cur + 1);
            versions.erase (versions.begin() + keep, versions.end());
            labels.erase (labels.begin() + keep, labels.end());
            versions.push_back (std::move (state));
            labels.push_back (std::move (label));
            while (versions.size() > static_cast<std::size_t> (lim) + 1)
            {
                versions.pop_front();
                labels.pop_front();
            }
            cur = static_cast<int> (versions.size()) - 1;
            return true;
        }

        bool canUndo() const noexcept { return cur > 0; }
        bool canRedo() const noexcept { return cur >= 0 && cur < static_cast<int> (versions.size()) - 1; }

        /** The label of the step undo would take back, or of the step redo would make again. */
        std::optional<std::string> undoLabel() const
        {
            return canUndo() ? std::optional<std::string> (labels[static_cast<std::size_t> (cur)]) : std::nullopt;
        }

        std::optional<std::string> redoLabel() const
        {
            return canRedo() ? std::optional<std::string> (labels[static_cast<std::size_t> (cur + 1)]) : std::nullopt;
        }

        /** Steps back and returns the version it reached, or nothing when there is nothing to undo. */
        std::optional<State> undo()
        {
            if (! canUndo())
                return std::nullopt;
            --cur;
            return versions[static_cast<std::size_t> (cur)];
        }

        /** Steps forward and returns the version it reached, or nothing when there is nothing to redo. */
        std::optional<State> redo()
        {
            if (! canRedo())
                return std::nullopt;
            ++cur;
            return versions[static_cast<std::size_t> (cur)];
        }

        /** The current version, or nothing in an empty history. */
        std::optional<State> current() const
        {
            return cur >= 0 ? std::optional<State> (versions[static_cast<std::size_t> (cur)]) : std::nullopt;
        }

        /** The number of versions held (at most limit + 1). */
        std::size_t size() const noexcept { return versions.size(); }
        int limit() const noexcept { return lim; }
        int position() const noexcept { return cur; }

        /** Forgets every version. */
        void clear()
        {
            versions.clear();
            labels.clear();
            cur = -1;
        }

        /** Forgets every version and starts again from state, labelled "start". */
        void clear (State state)
        {
            clear();
            push (std::move (state), "start");
        }

    private:
        int lim;
        Same sameState;
        std::deque<State> versions;
        std::deque<std::string> labels;
        int cur = -1;
    };

    /** The undo history of clip versions. */
    using ClipHistory = VersionHistory<ClipPtr, SameClipPtr>;
}
