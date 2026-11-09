#include "NoteSettings.h"

#include "Num.h"
#include "core/JsCompat.h"

#include <cmath>
#include <utility>

namespace sl::model
{
    Note& applyNotePatch (Note& n, const SamplePatch& patch)
    {
        // The sample fields: each valid value is written; null (an empty inner value) follows the instrument.
        if (patch.motion.has_value())
            n.sample.motion = *patch.motion;
        if (patch.travel.has_value())
            n.sample.travel = *patch.travel;
        if (patch.tape.has_value())
            n.sample.tape = *patch.tape;
        if (patch.rev.has_value())
            n.sample.rev = *patch.rev;
        if (patch.jump.has_value())
            n.sample.jump = *patch.jump;
        if (patch.engine.has_value())
            n.sample.engine = *patch.engine;
        if (patch.lock.has_value())
            n.sample.lock = *patch.lock;
        if (patch.restart.has_value())
            n.sample.restart = *patch.restart;

        if (patch.start.has_value())
        {
            StartSpec st = n.sample.start;
            const StartPatch& p = *patch.start;
            if (p.mode.has_value())
                st.mode = *p.mode;
            if (p.pos.has_value() && std::isfinite (*p.pos))
                st.pos = clamp (*p.pos, 0.0, 1.0);
            if (p.range.has_value() && std::isfinite (*p.range))
                st.range = clamp (*p.range, 0.0, 1.0);
            if (p.seed.has_value() && std::isfinite (*p.seed))
                st.seed = core::toInt32 (std::trunc (*p.seed));
            n.sample.start = st;
        }

        if (patch.vel.has_value() && std::isfinite (*patch.vel))
            n.vel = clamp (*patch.vel, 0.0, 1.0);
        if (patch.glide.has_value())
        {
            const std::optional<double>& g = *patch.glide;
            if (! g.has_value())
                n.glide.reset();
            else if (std::isfinite (*g))
                n.glide = jsMax (0.0, *g);
        }
        if (patch.gc.has_value() && std::isfinite (*patch.gc))
            n.gc = clamp (*patch.gc, -1.0, 1.0);
        return n;
    }

    Clip setNoteSample (const Clip& clip, const Ids& ids, const SamplePatch& patch)
    {
        return editNotes (clip, ids, [&] (Note& n, const Note&) { applyNotePatch (n, patch); });
    }

    namespace
    {
        // const common = get => { const v0 = get (sel[0]); return sel.every (n => get (n) === v0) ? v0 : 'mixed'; }
        template <class T, class Get>
        Mixed<T> common (const std::vector<NotePtr>& sel, Get get)
        {
            Mixed<T> out;
            out.value = get (*sel[0]);
            for (const NotePtr& n : sel)
                if (! (get (*n) == out.value))
                    out.mixed = true;
            return out;
        }
    }

    std::optional<MixedSettings> getMixed (const Clip& clip, const Ids& ids)
    {
        const std::vector<NotePtr> sel = selected (clip, ids);
        if (sel.empty())
            return std::nullopt;

        MixedSettings out;
        out.count = static_cast<int> (sel.size());
        out.motion = common<std::optional<core::Motion>> (sel, [] (const Note& n) { return n.sample.motion; });
        out.travel = common<TravelKind> (sel, [] (const Note& n) { return n.sample.travel; });
        out.tape = common<std::optional<bool>> (sel, [] (const Note& n) { return n.sample.tape; });
        out.rev = common<std::optional<bool>> (sel, [] (const Note& n) { return n.sample.rev; });
        out.jump = common<std::optional<core::RateId>> (sel, [] (const Note& n) { return n.sample.jump; });
        out.engine = common<std::optional<core::Engine>> (sel, [] (const Note& n) { return n.sample.engine; });
        out.lock = common<std::optional<bool>> (sel, [] (const Note& n) { return n.sample.lock; });
        out.restart = common<bool> (sel, [] (const Note& n) { return n.sample.restart; });
        out.startMode = common<StartMode> (sel, [] (const Note& n) { return n.sample.start.mode; });
        out.startPos = common<double> (sel, [] (const Note& n) { return n.sample.start.pos; });
        out.startRange = common<double> (sel, [] (const Note& n) { return n.sample.start.range; });
        out.vel = common<double> (sel, [] (const Note& n) { return n.vel; });
        out.glide = common<std::optional<double>> (sel, [] (const Note& n) { return n.glide; });
        out.gc = common<double> (sel, [] (const Note& n) { return n.gc; });
        return out;
    }

    Clip setLaneNodes (const Clip& clip, const std::string& id, core::LaneId lane, const NodeList& nodes)
    {
        // Object.assign ({}, n, { c }): a plain copy of the note with that one lane replaced (no cloneNote, so
        // nothing else is touched, not even an empty lane).
        const NodeList nd = normalizeNodes (nodes, core::laneDef (lane));
        std::vector<NotePtr> notes;
        notes.reserve (clip.notes.size());
        for (const NotePtr& n : clip.notes)
        {
            if (n->id != id)
            {
                notes.push_back (n);
                continue;
            }
            auto copy = std::make_shared<Note> (*n);
            copy->lane (lane) = nd;
            notes.push_back (std::move (copy));
        }
        return withNotes (clip, std::move (notes));
    }

    Clip setLaneNodesMany (const Clip& clip, core::LaneId lane, const std::map<std::string, NodeList>& nodesById)
    {
        std::vector<NotePtr> notes;
        notes.reserve (clip.notes.size());
        for (const NotePtr& n : clip.notes)
        {
            const auto it = nodesById.find (n->id);
            if (it == nodesById.end())
            {
                notes.push_back (n);
                continue;
            }
            auto copy = std::make_shared<Note> (*n);
            copy->lane (lane) = normalizeNodes (it->second, core::laneDef (lane));
            notes.push_back (std::move (copy));
        }
        return withNotes (clip, std::move (notes));
    }
}
