#pragma once

// Copy-on-write editing (sl-model.js idSet, selected, editNotes; PLUGIN-PLAN 1.2). An edit returns a new
// Clip: every note it acts on is a fresh copy, every other note is the same NotePtr as in the input.

#include "Clip.h"

#include <initializer_list>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sl::model
{
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
}
