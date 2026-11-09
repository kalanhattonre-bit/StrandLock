#include "ClipEdit.h"

namespace sl::model
{
    IdSet idSet (const Clip& clip, const Ids& ids)
    {
        IdSet set;
        if (ids.isAll())
        {
            for (const NotePtr& n : clip.notes)
                set.insert (n->id);
            return set;
        }
        for (const std::string& id : ids.list())
            set.insert (id);
        return set;
    }

    std::vector<NotePtr> selected (const Clip& clip, const Ids& ids)
    {
        const IdSet set = idSet (clip, ids);
        std::vector<NotePtr> out;
        for (const NotePtr& n : clip.notes)
            if (set.count (n->id) != 0)
                out.push_back (n);
        return out;
    }

    Clip withNotes (const Clip& clip, std::vector<NotePtr> notes)
    {
        Clip out;
        out.bars = clip.bars;
        out.beatsPerBar = clip.beatsPerBar;
        out.offsetBeats = clip.offsetBeats;
        out.loop = clip.loop;
        out.tieAcrossLoop = clip.tieAcrossLoop;
        out.extras = clip.extras;
        out.notes = std::move (notes);
        return out;
    }
}
