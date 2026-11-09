#include "ClipEdit.h"

#include "NoteSettings.h"
#include "Num.h"
#include "core/JsCompat.h"
#include "core/Seeds.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

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

    //==============================================================================================
    namespace
    {
        IdSet takenIds (const Clip& clip)
        {
            IdSet taken;
            for (const NotePtr& n : clip.notes)
                taken.insert (n->id);
            return taken;
        }

        int capOf (int maxNotes) noexcept
        {
            return std::min (maxNotes, kMaxClipNotes);
        }

        EditResult refusedEdit (const Clip& clip, double count)
        {
            EditResult r;
            r.clip = clip;
            r.refused = true;
            r.count = count;
            return r;
        }

        /** duplicateNotes' copies, shared with pasteNotes: src in time order, moved by off, ids made from
            prefix + id + suffix. */
        EditResult copyIn (const Clip& clip, const std::vector<NotePtr>& src, double off, const std::string& prefix,
                           const std::string& suffix, int maxNotes)
        {
            const double count = static_cast<double> (clip.notes.size() + src.size());
            if (count > static_cast<double> (capOf (maxNotes)))
                return refusedEdit (clip, count);

            IdSet taken = takenIds (clip);
            std::unordered_map<std::string, std::string> map;   // the prototype's Map: a later equal id overwrites
            for (std::size_t k = 0; k < src.size(); ++k)
                map[src[k]->id] = uniqueId (taken, prefix + src[k]->id + suffix, static_cast<double> (k));

            EditResult r;
            r.count = count;
            std::vector<NotePtr> notes = clip.notes;
            notes.reserve (clip.notes.size() + src.size());
            for (const NotePtr& n : src)
            {
                auto c = std::make_shared<Note> (cloneNote (*n));
                c->id = map.at (n->id);
                c->s = jsMax (0.0, n->s + off);
                const auto to = n->glideTo.empty() ? map.end() : map.find (n->glideTo);
                c->glideTo = to != map.end() ? to->second : std::string();
                r.ids.push_back (c->id);
                notes.push_back (std::move (c));
            }
            r.clip = withNotes (clip, std::move (notes));
            return r;
        }
    }

    std::string uniqueId (IdSet& taken, const std::string& base, double k)
    {
        const std::uint32_t h = core::idHash (std::string_view (base));
        const std::int32_t kk = core::toInt32 (k);
        for (std::uint32_t salt = 0;; ++salt)
        {
            std::string id = "n" + core::toBase36 (core::hash32 (h, kk, salt));
            if (taken.insert (id).second)
                return id;
        }
    }

    EditResult addNote (const Clip& clip, double p, double s, double l, const SamplePatch* patch, int maxNotes)
    {
        const double count = static_cast<double> (clip.notes.size());
        if (count + 1.0 > static_cast<double> (capOf (maxNotes)))
            return refusedEdit (clip, count + 1.0);

        IdSet taken = takenIds (clip);
        const std::string base = "add:" + core::jsNumberToString (count) + ":" + core::jsNumberToString (p) + ":"
                               + core::jsNumberToString (s) + ":" + core::jsNumberToString (l);
        const std::string id = uniqueId (taken, base, count);
        auto n = std::make_shared<Note> (mkNote (p, s, l, id));
        if (patch != nullptr)
            applyNotePatch (*n, *patch);

        std::vector<NotePtr> notes = clip.notes;
        notes.push_back (std::move (n));
        EditResult r;
        r.clip = withNotes (clip, std::move (notes));
        r.count = count + 1.0;
        r.ids.push_back (id);
        return r;
    }

    Clip removeNotes (const Clip& clip, const Ids& ids)
    {
        const IdSet set = idSet (clip, ids);
        std::vector<NotePtr> notes;
        for (const NotePtr& n : clip.notes)
        {
            if (set.count (n->id) != 0)
                continue;
            if (! n->glideTo.empty() && set.count (n->glideTo) != 0)
            {
                auto c = std::make_shared<Note> (cloneNote (*n));
                c->glideTo.clear();
                notes.push_back (std::move (c));
            }
            else
            {
                notes.push_back (n);
            }
        }
        return withNotes (clip, std::move (notes));
    }

    Clip updateNotes (const Clip& clip, const Ids& ids, const NotePatch& patch)
    {
        return editNotes (clip, ids, [&] (Note& n, const Note&)
        {
            if (patch.p.has_value())       n.p = *patch.p;
            if (patch.s.has_value())       n.s = *patch.s;
            if (patch.l.has_value())       n.l = *patch.l;
            if (patch.vel.has_value())     n.vel = *patch.vel;
            if (patch.glide.has_value())   n.glide = *patch.glide;
            if (patch.gc.has_value())      n.gc = *patch.gc;
            if (patch.glideTo.has_value()) n.glideTo = *patch.glideTo;
            n.p = clamp (num (n.p, 60.0), 0.0, 127.0);
            n.s = jsMax (0.0, num (n.s, 0.0));
            n.l = jsMax (1e-3, num (n.l, 1.0));
            n.vel = clamp (num (n.vel, 0.8), 0.0, 1.0);
        });
    }

    EditResult duplicateNotes (const Clip& clip, const Ids& ids, std::optional<double> offset, int maxNotes)
    {
        const std::vector<NotePtr> src = timeOrder (selected (clip, ids));
        if (src.empty())
        {
            EditResult r;
            r.clip = clip;
            r.count = static_cast<double> (clip.notes.size());
            return r;
        }
        // Math.min (...starts), Math.max (...ends)
        double s0 = src[0]->s, s1 = src[0]->s + src[0]->l;
        for (const NotePtr& n : src)
        {
            s0 = jsMin (s0, n->s);
            s1 = jsMax (s1, n->s + n->l);
        }
        const double off = offset.has_value() ? *offset : s1 - s0;
        return copyIn (clip, src, off, "dup:", ":" + core::jsNumberToString (off), maxNotes);
    }

    EditResult pasteNotes (const Clip& clip, const std::vector<NotePtr>& notes, double at, int maxNotes)
    {
        const std::vector<NotePtr> src = timeOrder (notes);
        if (src.empty())
        {
            EditResult r;
            r.clip = clip;
            r.count = static_cast<double> (clip.notes.size());
            return r;
        }
        double s0 = src[0]->s;
        for (const NotePtr& n : src)
            s0 = jsMin (s0, n->s);
        return copyIn (clip, src, at - s0, "paste:", ":" + core::jsNumberToString (at), maxNotes);
    }

    LaneClipboard copyLanes (const Note& from, const std::array<bool, core::kNumLanes>& mask)
    {
        LaneClipboard cb;
        for (std::size_t i = 0; i < mask.size(); ++i)
        {
            if (! mask[i])
                continue;
            cb.has[i] = true;
            cb.c[i] = noteLane (from, static_cast<core::LaneId> (i));
            cb.gen[i] = from.gen[i];
        }
        return cb;
    }

    Clip pasteLanes (const Clip& clip, const Ids& ids, const LaneClipboard& clipboard)
    {
        return editNotes (clip, ids, [&] (Note& n, const Note&)
        {
            for (std::size_t i = 0; i < clipboard.has.size(); ++i)
            {
                if (! clipboard.has[i])
                    continue;
                n.c[i] = clipboard.c[i];
                n.gen[i] = clipboard.gen[i];
            }
        });
    }
}
