#pragma once

// The document (PLUGIN-PLAN 1.3, 2.2, 3): the current clip as an immutable version, and the instrument state
// that is not a parameter. Every edit makes a new Clip version (the model's copy-on-write edits share every note
// they did not touch) and goes through model::limitEdit with the document's one ClipSizer, so StrandLock never
// accepts a clip it would refuse to load: at most 4096 notes and 32 MiB encoded (plan 3's limits table). A
// refused edit leaves the clip unchanged and names the cap for the toast, as the prototype's Chop does. The
// document also refuses an edit whose snapshot would not fit the publisher's memory budget even at its coarsest
// resolution (SnapshotPublisher.h): a clip it could not play.
//
// The non-parameter INST fields (seed, the wheel targets and CCs, key, scale, scale lock, the exact Region
// edges) live here with the sound the project names (DocState::np, plan 2.4), and the document writes the
// atomics the audio thread reads (ParamBridge's NonParamAtomics) whenever they change.
//
// Threads: edits from the message thread (and the host's state calls on whatever thread they come); every read
// from any thread. Two locks: edits are serialised with one, reads take a short one, and onClipChanged runs
// after both are released.

#include "RollState.h"
#include "SnapshotPublisher.h"

#include "core/Lanes.h"
#include "model/ClipCodec.h"
#include "model/ClipEdit.h"
#include "params/ParamBridge.h"
#include "state/StateChunks.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sl::doc
{
    /** What Undo keeps besides the clip and the parameters (plan 2.4's NonParamState): the INST chunk's fields
        and the sound the project names (SMPL). */
    struct NonParamState
    {
        state::InstState inst;
        state::SampleRef sound;
    };

    /** The same INST fields and the same SMPL record (compared through their encodings). */
    bool sameNonParam (const NonParamState& a, const NonParamState& b);

    /** What an edit did. */
    struct EditOutcome
    {
        bool accepted = false;      // the document holds a new clip version
        bool refused = false;       // a cap refused it: the clip is unchanged
        std::string message;        // the toast for a refusal, else empty
        model::EditResult result;   // the model's result: the clip now held, the ids made, the count, the bytes
        std::uint64_t version = 0;  // the document's clip version after the edit
    };

    class ClipDocument
    {
    public:
        /** An empty 4-bar clip, the default INST fields and the default sound (sawopen). */
        explicit ClipDocument (params::NonParamAtomics& atomics);

        ClipDocument (const ClipDocument&) = delete;
        ClipDocument& operator= (const ClipDocument&) = delete;

        //==========================================================================================
        // Any thread.

        model::ClipPtr current() const;

        /** Counts every change of the clip (1 after construction). */
        std::uint64_t version() const noexcept;

        /** The CLIP payload the current clip encodes to (the ClipSizer's figure). */
        std::size_t encodedBytes() const;

        NonParamState nonParam() const;
        state::InstState instState() const;
        state::SampleRef soundRef() const;

        //==========================================================================================
        // Edits (message thread; a state load on its own thread).

        /** A model edit's result (addNote, duplicateNotes, chop ...): accepted when the model did not refuse it
            and limitEdit and the snapshot budget let it through. A result equal to the current clip (sameClip) is
            accepted without a new version. */
        EditOutcome apply (model::EditResult proposed);

        /** A whole proposed clip (a tool that returns a clip). */
        EditOutcome apply (model::Clip proposed);

        /** Holds this clip as it is (a state load, Undo and Redo, a new instance): no caps, because the decoder
            already holds the same limits and history states were accepted once. A new version; the selection keeps the
            notes the clip still holds, and the inspector's node is let go. */
        void replace (model::ClipPtr clip);

        void setNonParam (const NonParamState& np);
        void setInstState (const state::InstState& s);
        void setSoundRef (const state::SampleRef& r);

        /** Called after every new clip version, with no lock held, on the thread that made it (the processor
            hands it to the SnapshotPublisher and drops its encoded CLIP payload). */
        std::function<void (const model::ClipPtr&, std::uint64_t version)> onClipChanged;

        //==========================================================================================
        // The roll's state (PLUGIN-PLAN 7.6: selection, mode, active lane). Read from any thread; changed on the
        // message thread. Every new clip version drops what it no longer holds: selected notes that are gone (the
        // primary then falls to the first one left), and the inspector's node when its note, lane or node count
        // changed (sl-ui.js pruneSelection and validateNodes).

        RollState rollState() const;

        /** sl-ui.js setSel (ids, primary): the ids the clip holds (repeats once), in this order. primary: nothing =
            keep the current primary (dropped when it is not among the ids), an empty string = none, an id = that
            note (added to the selection when missing). With no primary the first id is it. The inspector's node
            goes when its note is no longer selected. */
        void setSelection (std::vector<std::string> ids, std::optional<std::string> primary = std::nullopt);

        /** sl-ui.js setPrimary: a selected note becomes the primary one; a note not selected joins the selection
            first. An empty id clears the primary. */
        void setPrimary (const std::string& id);

        /** Mode 1..8 (rounded and clamped; 1 for a value that is not finite). The inspector's node goes when it is
            not on the new mode's lane. */
        void setMode (double mode);

        /** Mode 8 with this Sample sub-lane (Travel, Heads, Grain or Refresh; any other lane is ignored). */
        void setSubLane (core::LaneId lane);

        /** The inspector's node (nothing = none). Its node count is taken from the clip when len is 0. */
        void setActiveNode (std::optional<ActiveNode> node);

        /** sl-ui.js setNodePicks: the nodes picked with the Nodes switch (M5), per note in this order. Each note's
            count on the lane the mode shows is taken from the clip now (-1 when the note is missing or no lane mode is
            on: those picks go at the next clip version). A note with no picks is left out. Every new clip version lets
            go of a note's picks when its count on that lane changed (sl-ui.js validateNodes); a selection without the
            note, another mode or lane, a state load, Undo and Redo let go of them too. */
        void setNodePicks (std::vector<NodePick> picks);

        /** One note's picks after a gesture moved them itself (a node added or deleted on that note: the picks after
            it move one place, sl-ui.js addNodeAt and the double-click that deletes): len is the count they belong to.
            The note goes to the end of the order, as a JavaScript Map's set does after a delete. Empty ks: none. */
        void setNotePicks (const std::string& id, std::vector<int> ks, int len);

        //==========================================================================================
        /** The toast for a refused result: the note cap ("That would make 4100 notes; the limit is 4096."), the
            encoded size, or the snapshot budget. */
        static std::string refusalText (const model::EditResult& refused, bool tooDetailed, std::size_t tableBytes);

    private:
        EditOutcome accept (model::EditResult r);
        void writeAtomics (const state::InstState& s) noexcept;

        // With readLock held: the selection rules on clip c.
        void selectLocked (const model::Clip& c, std::vector<std::string> ids, std::optional<std::string> primary);
        void pruneLocked (const model::Clip& c);
        int laneCountLocked (const model::Clip& c, const std::string& id) const;   // the shown lane's node count, or -1

        params::NonParamAtomics& atomics;

        std::mutex editLock;          // serialises edits and the sizers
        model::ClipSizer sizer;
        SnapshotSizer snapshotSizer;

        mutable std::mutex readLock;  // guards the fields below for readers
        model::ClipPtr clip;
        std::uint64_t ver = 0;
        std::size_t bytes = 0;
        NonParamState np;
        RollState roll;
    };
}
