#include "SnapshotCompiler.h"

#include "Generators.h"
#include "Glides.h"
#include "Normalize.h"

#include "core/Inst.h"
#include "core/JsCompat.h"
#include "core/Lanes.h"
#include "core/Rates.h"
#include "core/Seeds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace sl::model
{
    std::shared_ptr<const core::NoteTables> SnapshotCache::find (std::uint64_t serial, const std::string& id, int res) const
    {
        const auto it = entries.find (serial);
        if (it == entries.end() || it->second.res != res || it->second.id != id)
            return nullptr;
        return it->second.tables;
    }

    void SnapshotCache::put (std::uint64_t serial, const std::string& id, int res, std::shared_ptr<const core::NoteTables> tables)
    {
        entries[serial] = Entry { id, res, std::move (tables) };
    }

    namespace
    {
        bool allSameBits (const std::vector<float>& v) noexcept
        {
            for (std::size_t i = 1; i < v.size(); ++i)
                if (std::memcmp (&v[i], &v[0], sizeof (float)) != 0)
                    return false;
            return true;
        }

        std::int8_t marker (bool v) noexcept { return v ? 1 : 0; }

        template <class E>
        std::int8_t marker (const std::optional<E>& v) noexcept
        {
            return v.has_value() ? static_cast<std::int8_t> (*v) : core::kFollow;
        }

        std::int8_t marker (const std::optional<bool>& v) noexcept
        {
            return v.has_value() ? marker (*v) : core::kFollow;
        }

        bool sameBits (double a, double b) noexcept { return std::memcmp (&a, &b, sizeof a) == 0; }

        /** The lane is the untouched default: no generator, and no nodes (noteLane reads them as flat (def)) or
            exactly flat (def): t 0 and 1, v the default (same bits), both smooth. A table then depends on the
            lane and its length only (a smooth segment never reads bend). */
        bool defaultFlat (const Note& note, std::size_t lane) noexcept
        {
            if (note.gen[lane].has_value())
                return false;
            const NodeList& nd = note.c[lane];
            if (nd.empty())
                return true;
            const double def = core::kLanes[lane].def;
            return nd.size() == 2 && sameBits (nd[0].t, 0.0) && sameBits (nd[1].t, 1.0) && sameBits (nd[0].v, def) && sameBits (nd[1].v, def)
                && nd[0].shape == NodeShape::smooth && nd[1].shape == NodeShape::smooth;
        }

        /** The tables of default-flat lanes already made in this compile, by lane and length. Reusing one is the
            same function on the same input, so no value changes; it saves recomputing the 9 untouched lanes of
            most notes. */
        struct MemoTable
        {
            std::int32_t fullLength = 0;
            std::vector<float> values;
        };

        using FlatMemo = std::unordered_map<std::uint64_t, MemoTable>;

        std::shared_ptr<const core::NoteTables> tablesOf (const Note& note, int res, FlatMemo* memo)
        {
            // laneTable's length, max (2, ceil (l * res) + 1), capped
            const double N = std::min (tableLength (note.l, static_cast<double> (std::max (1, res))), static_cast<double> (kMaxTableLength));

            auto out = std::make_shared<core::NoteTables>();
            for (std::size_t i = 0; i < static_cast<std::size_t> (core::kNumLanes); ++i)
            {
                const bool memoable = memo != nullptr && defaultFlat (note, i);
                const std::uint64_t key = (static_cast<std::uint64_t> (i) << 32) | static_cast<std::uint32_t> (core::toInt32 (N));
                if (memoable)
                {
                    if (const auto it = memo->find (key); it != memo->end())
                    {
                        out->fullLength[i] = it->second.fullLength;
                        out->lanes[i] = it->second.values;
                        continue;
                    }
                }
                std::vector<float> table = laneTableOfLength (note, static_cast<core::LaneId> (i), N);
                out->fullLength[i] = static_cast<std::int32_t> (table.size());
                if (table.size() > 2 && allSameBits (table))
                    table.resize (2);
                table.shrink_to_fit();
                if (memoable)
                    memo->emplace (key, MemoTable { out->fullLength[i], table });
                out->lanes[i] = std::move (table);
            }
            return out;
        }
    }

    std::shared_ptr<const core::NoteTables> compileTables (const Note& note, int res)
    {
        return tablesOf (note, res, nullptr);
    }

    std::unique_ptr<core::Snapshot> compileSnapshot (const Clip& clip, int res, const SnapshotCache* reuse, SnapshotCache* remember)
    {
        res = std::max (8, res);
        const Clip c = normalizeClip (clip);   // the same notes, in the same order

        std::array<GlideMap, 3> glides;
        std::array<Chains, 3> chains;
        for (int m = 0; m < 3; ++m)
        {
            core::Inst inst = core::defaultInst();
            inst.glideMode = static_cast<core::GlideMode> (m);
            const auto mi = static_cast<std::size_t> (m);
            glides[mi] = computeGlides (c, inst);
            chains[mi] = buildChains (c, glides[mi]);
        }

        const std::vector<NotePtr> order = timeOrder (c.notes);
        const std::size_t count = order.size();
        std::unordered_map<std::string, std::int32_t> indexOf;
        indexOf.reserve (count);
        for (std::size_t k = 0; k < count; ++k)
            indexOf.emplace (order[k]->id, static_cast<std::int32_t> (k));
        std::unordered_map<const Note*, std::size_t> sourceOf;   // each normalized note -> its input note
        sourceOf.reserve (count);
        for (std::size_t i = 0; i < c.notes.size(); ++i)
            sourceOf.emplace (c.notes[i].get(), i);
        auto at = [&indexOf] (const std::string& id) -> std::int32_t
        {
            const auto it = indexOf.find (id);
            return it != indexOf.end() ? it->second : -1;
        };

        // chainOf for each mode: the chain's first note, the place in it, the neighbours
        std::array<std::vector<core::ChainSnap>, 3> chainOf;
        for (std::size_t m = 0; m < 3; ++m)
        {
            chainOf[m].assign (count, core::ChainSnap {});
            for (const auto& ch : chains[m])
                for (std::size_t i = 0; i < ch.size(); ++i)
                {
                    const std::int32_t k = at (ch[i]);
                    if (k < 0)
                        continue;
                    core::ChainSnap& cs = chainOf[m][static_cast<std::size_t> (k)];
                    cs.firstIndex = at (ch[0]);
                    cs.index = static_cast<std::int32_t> (i);
                    cs.prevIndex = i > 0 ? at (ch[i - 1]) : -1;
                    cs.nextIndex = i + 1 < ch.size() ? at (ch[i + 1]) : -1;
                }
        }

        auto snap = std::make_unique<core::Snapshot>();
        snap->bars = c.bars;
        snap->beatsPerBar = c.beatsPerBar;
        snap->res = res;
        snap->offsetBeats = c.offsetBeats;
        snap->loop = c.loop;
        snap->tieAcrossLoop = c.tieAcrossLoop;
        snap->notes.resize (count);
        snap->ids.reserve (count);
        snap->tables.reserve (count);
        snap->byHash.reserve (count);

        SnapshotCache next;
        FlatMemo memo;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (std::size_t k = 0; k < count; ++k)
        {
            const Note& n = *order[k];
            core::NoteSnap& x = snap->notes[k];
            x.idHash = core::idHash (std::string_view (n.id));
            x.idIndex = static_cast<std::int32_t> (k);
            x.p = n.p;
            x.s = n.s;
            x.l = n.l;
            x.vel = n.vel;

            const NoteSample& sm = n.sample;
            x.engine = marker (sm.engine);
            x.lock = marker (sm.lock);
            x.motion = marker (sm.motion);
            x.tape = marker (sm.tape);
            x.rev = marker (sm.rev);
            x.travel = static_cast<std::uint8_t> (sm.travel == TravelKind::path ? 1 : 0);
            x.jumpBeats = sm.jump.has_value() ? core::rateBeats (*sm.jump) : nan;
            x.restart = sm.restart;
            x.start.mode = static_cast<std::uint8_t> (sm.start.mode);
            x.start.pos = sm.start.pos;
            x.start.range = sm.start.range;
            x.start.seed = sm.start.seed;
            x.glide = n.glide.has_value() ? *n.glide : nan;

            for (std::size_t m = 0; m < 3; ++m)
            {
                if (const Glide* g = glides[m].find (n.id))
                {
                    x.glides[m].present = true;
                    x.glides[m].d = g->d;
                    x.glides[m].curve = g->curve;
                    x.glides[m].toIndex = at (g->toId);
                }
                x.chains[m] = chainOf[m][k];
            }

            const Note& source = *clip.notes[sourceOf.at (&n)];
            const std::uint64_t serial = source.serial.get();
            std::shared_ptr<const core::NoteTables> tables = reuse != nullptr ? reuse->find (serial, n.id, res) : nullptr;
            if (tables == nullptr)
                tables = tablesOf (n, res, &memo);
            for (int lane = 0; lane < core::kNumLanes; ++lane)
            {
                const auto li = static_cast<std::size_t> (lane);
                x.lanes[li] = core::LaneTab { tables->lanes[li].data(), static_cast<std::int32_t> (tables->lanes[li].size()) };
            }
            if (remember != nullptr)
                next.put (serial, n.id, res, tables);

            snap->ids.push_back (n.id);
            snap->tables.push_back (std::move (tables));
            snap->byHash.emplace_back (x.idHash, x.idIndex);
        }
        std::sort (snap->byHash.begin(), snap->byHash.end());

        if (remember != nullptr)
            *remember = std::move (next);
        return snap;
    }
}
