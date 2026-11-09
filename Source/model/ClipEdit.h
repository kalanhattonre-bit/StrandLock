#pragma once

// Copy-on-write editing (sl-model.js idSet, selected, editNotes; PLUGIN-PLAN 1.2). An edit returns a new
// Clip: every note it acts on is a fresh copy, every other note is the same NotePtr as in the input.

#include "Clip.h"

#include <array>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sl::model
{
    struct SamplePatch;   // NoteSettings.h

    /** The notes an operation acts on: every note (the prototype's null), or a list of ids (a single id
        string is a list of one). Ids that are not in the clip are ignored. */
    class Ids
    {
    public:
        /** Every note. */
        static Ids all() { return Ids(); }

        Ids (std::vector<std::string> list) : everyNote (false), ids (std::move (list)) {}
        Ids (std::initializer_list<std::string> list) : everyNote (false), ids (list) {}
        Ids (std::string one) : everyNote (false), ids { std::move (one) } {}
        Ids (const char* one) : everyNote (false), ids { std::string (one) } {}

        bool isAll() const noexcept { return everyNote; }
        const std::vector<std::string>& list() const noexcept { return ids; }

    private:
        Ids() = default;

        bool everyNote = true;
        std::vector<std::string> ids;
    };

    using IdSet = std::unordered_set<std::string>;

    /** SLModel.idSet: the ids as a set (every id of the clip for Ids::all ()). */
    IdSet idSet (const Clip& clip, const Ids& ids);

    /** SLModel.selected: the clip's notes whose ids are in ids, in clip order. */
    std::vector<NotePtr> selected (const Clip& clip, const Ids& ids);

    /** The clip with the same settings and these notes. */
    Clip withNotes (const Clip& clip, std::vector<NotePtr> notes);

    /** SLModel.editNotes: fn (copy, original) edits a fresh copy (cloneNote) of every note in ids; the
        result holds that copy in the note's place, and shares every other note with the input. Every note
        in ids is copied, even when fn changes nothing (the prototype does the same). */
    template <class Fn>
    Clip editNotes (const Clip& clip, const Ids& ids, Fn&& fn)
    {
        const IdSet set = idSet (clip, ids);
        std::vector<NotePtr> notes;
        notes.reserve (clip.notes.size());
        for (const NotePtr& n : clip.notes)
        {
            if (set.count (n->id) == 0)
            {
                notes.push_back (n);
                continue;
            }
            auto copy = std::make_shared<Note> (cloneNote (*n));
            fn (*copy, *n);
            notes.push_back (std::move (copy));
        }
        return withNotes (clip, std::move (notes));
    }

    //==============================================================================================
    // Build-mode editing (sl-model.js uniqueId, addNote, removeNotes, updateNotes, duplicateNotes), the
    // prototype's Copy / Paste lanes, and a note paste. The edits that add notes refuse a clip past the note
    // cap (PLUGIN-PLAN 3: one table of limits), as Chop refuses past its 1024.

    /** What an edit that can be refused returns (chop, addNote, duplicateNotes, pasteNotes). */
    struct EditResult
    {
        Clip clip;                       // the new clip; when refused, the input clip (every note shared)
        bool refused = false;
        double count = 0.0;              // how many notes the clip would hold (Chop: as the prototype counts it)
        std::vector<std::string> ids;    // the notes the edit made, in order (addNote: one; Chop: none)
    };

    /** SLModel.uniqueId: 'n' + base 36 of hash32 (idHash (base), k | 0, salt), the salt counting up from 0
        until the id is not taken. The id is added to taken. */
    std::string uniqueId (IdSet& taken, const std::string& base, double k);

    /** SLModel.addNote: a new note (mkNote) with the id uniqueId (taken, 'add:' + count + ':' + p + ':' + s +
        ':' + l, count), the patch applied (applyNotePatch) when given, at the end of the clip. Refused when the
        clip already holds maxNotes notes (maxNotes is at most kMaxClipNotes). */
    EditResult addNote (const Clip& clip, double p, double s, double l, const SamplePatch* patch = nullptr,
                        int maxNotes = kMaxClipNotes);

    /** SLModel.removeNotes: the notes without those in ids; a note that linked to a removed note becomes a
        copy with no link. */
    Clip removeNotes (const Clip& clip, const Ids& ids);

    /** updateNotes' patch: the fields that are set change. glide: an empty inner value is null (follow the
        instrument); glideTo: an empty string is null. */
    struct NotePatch
    {
        std::optional<double> p, s, l, vel;
        std::optional<std::optional<double>> glide;
        std::optional<double> gc;
        std::optional<std::string> glideTo;
    };

    /** SLModel.updateNotes with a patch of values: the fields set, then p clamped 0..127, s at least 0, l at
        least 1e-3, vel 0..1 (a non-finite value takes its default: 60, 0, 1, 0.8). glide, gc and glideTo are
        written as given, as the prototype does. (The prototype's function form is editNotes.) */
    Clip updateNotes (const Clip& clip, const Ids& ids, const NotePatch& patch);

    /** SLModel.duplicateNotes: copies of the notes in ids (in time order), moved by offset beats (default:
        the selection's own length, s1 - s0), starts at least 0, ids uniqueId (taken, 'dup:' + id + ':' +
        offset, k); a link inside the group points at the copy, a link out of it is dropped. The copies go at
        the end of the clip. Nothing selected: the clip and no ids. Refused when the clip would hold more than
        maxNotes notes (maxNotes is at most kMaxClipNotes). */
    EditResult duplicateNotes (const Clip& clip, const Ids& ids, std::optional<double> offset = std::nullopt,
                               int maxNotes = kMaxClipNotes);

    /** Paste notes (not in the prototype, which only pastes lanes): copies of notes (in time order), moved so
        the earliest starts at beat `at` (starts at least 0), with ids uniqueId (taken, 'paste:' + id + ':' +
        at, k) and links kept inside the group as duplicateNotes keeps them. Refused when the clip would hold
        more than maxNotes notes (maxNotes is at most kMaxClipNotes). */
    EditResult pasteNotes (const Clip& clip, const std::vector<NotePtr>& notes, double at, int maxNotes = kMaxClipNotes);

    /** The prototype's Copy lanes (sl-ui.js copyLanes): one note's drawing and generator on each lane of the
        mask, in registry order. */
    struct LaneClipboard
    {
        std::array<bool, core::kNumLanes> has {};
        std::array<NodeList, core::kNumLanes> c;
        std::array<std::optional<Gen>, core::kNumLanes> gen;
    };

    LaneClipboard copyLanes (const Note& from, const std::array<bool, core::kNumLanes>& mask);

    /** The prototype's Paste lanes (sl-ui.js pasteLanes, through updateNotes' function form): every note in ids
        takes the clipboard's nodes and generator on each lane it holds. Node t values are fractions of the
        note, so the lanes stretch to each note's length. Never adds a note. */
    Clip pasteLanes (const Clip& clip, const Ids& ids, const LaneClipboard& clipboard);
}
