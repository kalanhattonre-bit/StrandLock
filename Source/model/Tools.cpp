#include "Tools.h"

#include "Num.h"
#include "core/JsCompat.h"
#include "core/Seeds.h"
#include "core/Travel.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sl::model
{
    namespace
    {
        /** One key of a JavaScript comparator chain `x || next`: a difference that is 0 or NaN goes on to the
            next key. */
        int keyOf (double d) noexcept
        {
            if (d == 0.0 || std::isnan (d))
                return 0;
            return d < 0.0 ? -1 : 1;
        }

        std::vector<std::string> idsOf (const std::vector<NotePtr>& notes)
        {
            std::vector<std::string> out;
            out.reserve (notes.size());
            for (const NotePtr& n : notes)
                out.push_back (n->id);
            return out;
        }

        // laneEdit (clip, ids, lane, fn): n.c[lane] = fn (n.c[lane], n) on a copy of every note in ids
        template <class Fn>
        Clip laneEdit (const Clip& clip, const Ids& ids, core::LaneId lane, Fn fn)
        {
            return editNotes (clip, ids, [&] (Note& n, const Note&) { n.lane (lane) = fn (n.lane (lane), n); });
        }
    }

    //==============================================================================================
    Clip connect (const Clip& clip, const Ids& ids, ConnectMode mode, double seed)
    {
        const IdSet set = idSet (clip, ids);
        const std::vector<NotePtr> sel = timeOrder (selected (clip, ids));
        const std::int32_t sd = core::toInt32 (num (seed, 1.0));

        // existing links among the selection are replaced; links from outside the selection still count
        std::unordered_set<std::string> taken;
        for (const NotePtr& n : clip.notes)
            if (! n->glideTo.empty() && ! (set.count (n->id) != 0 && set.count (n->glideTo) != 0))
                taken.insert (n->glideTo);

        std::unordered_map<std::string, std::string> link;
        for (const NotePtr& np : sel)
        {
            const Note& n = *np;
            std::vector<const Note*> cand;
            for (const NotePtr& m : sel)
                if (m.get() != &n && m->s > n.s && m->s >= n.s + n.l - 0.13 && std::abs (m->p - n.p) <= 24.0)
                    cand.push_back (m.get());
            if (cand.empty())
                continue;
            double s0 = cand[0]->s;
            for (const Note* m : cand)
                s0 = jsMin (s0, m->s);
            std::vector<const Note*> group;
            for (const Note* m : cand)
                if (m->s <= s0 + 0.05)
                    group.push_back (m);

            if (mode == ConnectMode::random)
            {
                const std::uint32_t nh = core::idHash (std::string_view (n.id));
                std::unordered_map<const Note*, double> key;
                for (const Note* m : group)
                    key[m] = core::rand01 (core::hash32 (nh, core::idHash (std::string_view (m->id)), sd));
                std::stable_sort (group.begin(), group.end(), [&] (const Note* a, const Note* b)
                {
                    const int k = keyOf (key.at (a) - key.at (b));
                    return (k != 0 ? k : timeCmp (*a, *b)) < 0;
                });
            }
            else if (mode == ConnectMode::furthest)
            {
                std::stable_sort (group.begin(), group.end(), [&] (const Note* a, const Note* b)
                {
                    const int k = keyOf (std::abs (b->p - n.p) - std::abs (a->p - n.p));
                    return (k != 0 ? k : timeCmp (*a, *b)) < 0;
                });
            }
            else
            {
                std::stable_sort (group.begin(), group.end(), [&] (const Note* a, const Note* b)
                {
                    const int k = keyOf (std::abs (a->p - n.p) - std::abs (b->p - n.p));
                    return (k != 0 ? k : timeCmp (*a, *b)) < 0;
                });
            }

            const Note* tgt = nullptr;
            for (const Note* m : group)
            {
                if (taken.count (m->id) == 0)
                {
                    tgt = m;
                    break;
                }
            }
            if (tgt == nullptr)
                continue;
            taken.insert (tgt->id);
            link[n.id] = tgt->id;
        }

        return editNotes (clip, Ids (idsOf (sel)), [&] (Note& n, const Note&)
        {
            const auto it = link.find (n.id);
            if (it != link.end())
                n.glideTo = it->second;
            else if (! n.glideTo.empty() && set.count (n.glideTo) != 0)
                n.glideTo.clear();
        });
    }

    Clip disconnect (const Clip& clip, const Ids& ids)
    {
        const IdSet set = idSet (clip, ids);
        // [...set].filter (id => clip.notes.some (n => n.id === id && n.glideTo && set.has (n.glideTo)))
        std::vector<std::string> linked;
        for (const NotePtr& n : clip.notes)
            if (set.count (n->id) != 0 && ! n->glideTo.empty() && set.count (n->glideTo) != 0)
                linked.push_back (n->id);
        return editNotes (clip, Ids (std::move (linked)), [] (Note& n, const Note&) { n.glideTo.clear(); });
    }

    //==============================================================================================
    Clip walk (const Clip& clip, const Ids& ids, WalkField field, double from, double to)
    {
        const std::vector<NotePtr> sel = timeOrder (selected (clip, ids));
        const std::size_t N = sel.size();
        std::unordered_map<std::string, double> val;
        for (std::size_t i = 0; i < N; ++i)
            val[sel[i]->id] = from + (to - from) * (N > 1 ? static_cast<double> (i) / static_cast<double> (N - 1) : 0.0);

        return editNotes (clip, Ids (idsOf (sel)), [&] (Note& n, const Note&)
        {
            const double v = clamp (val.at (n.id), 0.0, 1.0);
            switch (field.kind)
            {
                case WalkField::Kind::start:
                    n.sample.start.mode = StartMode::region;
                    n.sample.start.pos = v;
                    break;
                case WalkField::Kind::vel:
                    n.vel = v;
                    break;
                case WalkField::Kind::lane:
                    n.lane (field.lane) = flat (v);
                    break;
            }
        });
    }

    EditResult chop (const Clip& clip, const Ids& ids, core::RateId rate, ChopStart stepStart, int maxNotes)
    {
        return chop (clip, ids, core::rateBeats (rate), stepStart, maxNotes);
    }

    EditResult chop (const Clip& clip, const Ids& ids, double stepBeats, ChopStart stepStart, int maxNotes)
    {
        const double maxN = static_cast<double> (std::min (maxNotes, kMaxClipNotes));
        const double step = core::rateBeatsOr (stepBeats);
        const std::vector<NotePtr> sel = selected (clip, ids);
        IdSet set;
        for (const NotePtr& n : sel)
            set.insert (n->id);
        auto pieces = [step] (const Note& n) { return jsMax (1.0, std::ceil (n.l / step - 1e-6)); };

        double total = static_cast<double> (clip.notes.size()) - static_cast<double> (sel.size());
        for (const NotePtr& n : sel)
            total += pieces (*n);

        EditResult r;
        r.count = total;
        if (total > maxN)
        {
            r.clip = clip;
            r.refused = true;
            return r;
        }

        IdSet taken;
        for (const NotePtr& n : clip.notes)
            taken.insert (n->id);

        std::vector<NotePtr> out;
        for (const NotePtr& np : clip.notes)
        {
            if (set.count (np->id) == 0)
            {
                out.push_back (np);
                continue;
            }
            const Note& n = *np;
            const double K = pieces (n);
            const double dStart = stepStart.spread ? 1.0 / K : num (stepStart.step, 0.0);
            for (double k = 0.0; k < K; k += 1.0)
            {
                const double s = n.s + k * step;
                const double e = k == K - 1.0 ? n.s + n.l : jsMin (n.s + n.l, s + step);
                if (e - s < 1e-6)
                    continue;
                auto c = std::make_shared<Note> (cloneNote (n));
                if (k > 0.0)
                {
                    c->id = uniqueId (taken, "chop:" + n.id, k);
                    // a 'Different' random line keeps the original note's values (Y7: random stays until Re-roll)
                    for (auto& g : c->gen)
                        if (g.has_value() && g->perNote == PerNote::different && g->key.empty())
                            g->key = n.id;
                }
                c->s = s;
                c->l = e - s;
                const double u0 = (s - n.s) / n.l, u1 = (e - n.s) / n.l;
                for (int L = 0; L < core::kNumLanes; ++L)
                {
                    const auto lane = static_cast<core::LaneId> (L);
                    c->lane (lane) = sliceNodes (noteLane (n, lane), u0, u1);
                }
                c->glideTo = k == K - 1.0 ? n.glideTo : std::string();
                if (k > 0.0)
                    c->sample.restart = true;
                if (dStart != 0.0 && ! std::isnan (dStart))
                {
                    const StartSpec& st = n.sample.start;
                    const double pos = st.pos + k * dStart;
                    c->sample.start = st;
                    c->sample.start.mode = StartMode::region;
                    c->sample.start.pos = pos - std::floor (pos + 1e-12);
                }
                out.push_back (std::move (c));
            }
        }
        r.clip = withNotes (clip, std::move (out));
        return r;
    }

    //==============================================================================================
    Clip reverseLane (const Clip& clip, const Ids& ids, core::LaneId lane)
    {
        return laneEdit (clip, ids, lane, [] (const NodeList& nd, Note&) { return reverseNodes (nd); });
    }

    Clip flipLane (const Clip& clip, const Ids& ids, core::LaneId lane)
    {
        return laneEdit (clip, ids, lane, [lane] (const NodeList& nd, Note& n)
        {
            auto& g = n.genOf (lane);
            if (g.has_value())
                g->invert = ! g->invert;
            return flipNodes (nd);
        });
    }

    Clip scaleLane (const Clip& clip, const Ids& ids, core::LaneId lane, double factor)
    {
        const double f = jsMax (0.0, num (factor, 1.0));
        const double def = core::laneDef (lane);
        return laneEdit (clip, ids, lane, [&] (const NodeList& nd, Note& n)
        {
            auto& g = n.genOf (lane);
            if (g.has_value())
            {
                g->d0 = clamp (g->d0 * f, 0.0, 1.0);
                g->d1 = clamp (g->d1 * f, 0.0, 1.0);
                g->amount = clamp (g->amount * f, 0.0, 1.0);
            }
            if (lane == core::LaneId::travel && n.sample.travel != TravelKind::path)
            {
                NodeList out;
                out.reserve (nd.size());
                for (const Node& q : nd)
                {
                    const double s = core::travelSpeed (core::f32 (q.v));
                    const double v = s > 0.0 ? clamp (0.75 + (q.v - 0.75) * f, core::kVFwdMin, 1.0)
                                   : s < 0.0 ? clamp (0.25 + (q.v - 0.25) * f, 0.0, core::kVBackMax)
                                             : q.v;
                    out.push_back (mkNode (q.t, v, q.shape, q.bend));
                }
                return out;
            }
            return scaleNodes (nd, def, f);
        });
    }

    //==============================================================================================
    std::vector<std::string> selectSimilar (const Clip& clip, const std::string& refId, SimilarKind kind, const Ids& ids)
    {
        const Note* ref = nullptr;
        for (const NotePtr& n : clip.notes)
        {
            if (n->id == refId)
            {
                ref = n.get();
                break;
            }
        }
        const std::vector<NotePtr> all = timeOrder (clip.notes);
        std::vector<std::string> out;

        switch (kind)
        {
            case SimilarKind::pitch:
                if (ref != nullptr)
                    for (const NotePtr& n : all)
                        if (n->p == ref->p)
                            out.push_back (n->id);
                return out;

            case SimilarKind::length:
                if (ref != nullptr)
                    for (const NotePtr& n : all)
                        if (std::abs (n->l - ref->l) < 1e-6)
                            out.push_back (n->id);
                return out;

            case SimilarKind::bar:
            {
                if (ref == nullptr)
                    return out;
                // BEATS_PER_BAR in the prototype (always 4); the clip's own bar length here
                const double bpb = clip.beatsPerBar >= 1 ? static_cast<double> (clip.beatsPerBar) : 4.0;
                const double b = std::floor (ref->s / bpb + 1e-9);
                for (const NotePtr& n : all)
                    if (std::floor (n->s / bpb + 1e-9) == b)
                        out.push_back (n->id);
                return out;
            }

            case SimilarKind::everyOther:
            {
                // ids == null || !idSet (clip, ids).size ? null : idSet (clip, ids)
                const IdSet set = idSet (clip, ids);
                const bool everyNote = ids.isAll() || set.empty();
                std::vector<const Note*> list;
                for (const NotePtr& n : all)
                    if (everyNote || set.count (n->id) != 0)
                        list.push_back (n.get());
                std::size_t par = 0;
                for (std::size_t i = 0; i < list.size(); ++i)
                {
                    if (list[i]->id == refId)
                    {
                        par = i % 2;
                        break;
                    }
                }
                for (std::size_t i = 0; i < list.size(); ++i)
                    if (i % 2 == par)
                        out.push_back (list[i]->id);
                return out;
            }

            case SimilarKind::invert:
            {
                // ids == null ? new Set () : idSet (clip, ids)
                const IdSet set = ids.isAll() ? IdSet() : idSet (clip, ids);
                for (const NotePtr& n : all)
                    if (set.count (n->id) == 0)
                        out.push_back (n->id);
                return out;
            }
        }
        return out;
    }

    //==============================================================================================
    Clip humanize (const Clip& clip, const Ids& ids, const HumanizeAmounts& amounts, double seed)
    {
        const std::int32_t sd = core::toInt32 (num (seed, 1.0));
        const double time = jsMax (0.0, num (amounts.time, 0.0));
        const double vel = jsMax (0.0, num (amounts.vel, 0.0));

        return editNotes (clip, ids, [&] (Note& n, const Note&)
        {
            const std::uint32_t h = core::idHash (std::string_view (n.id));
            auto r = [h, sd] (int k) { return core::rand01 (core::hash32 (h, k, sd)) * 2.0 - 1.0; };
            if (time != 0.0)
                n.s = jsMax (0.0, n.s + r (1) * time);
            if (vel != 0.0)
                n.vel = clamp (n.vel + r (2) * vel, 0.05, 1.0);
            for (int L = 0; L < core::kNumLanes; ++L)
            {
                const double amt = jsMax (0.0, num (amounts.lanes[static_cast<std::size_t> (L)], 0.0));
                if (amt == 0.0)
                    continue;
                const double off = r (10 + L) * amt;
                for (Node& q : n.c[static_cast<std::size_t> (L)])
                    q.v = clamp (q.v + off, 0.0, 1.0);
            }
        });
    }

    Clip strum (const Clip& clip, const Ids& ids, core::RateId rate, StrumDir dir)
    {
        const double step = core::rateBeats (rate);
        const std::vector<NotePtr> sel = timeOrder (selected (clip, ids));
        std::unordered_map<std::string, double> shift;
        for (std::size_t i = 0; i < sel.size();)
        {
            const double s0 = sel[i]->s;
            std::size_t j = i;
            while (j < sel.size() && sel[j]->s <= s0 + 0.05)
                ++j;
            if (j == i)
                j = i + 1;   // a NaN start (never in a normalized clip) would loop for ever in the prototype
            std::vector<const Note*> grp;
            for (std::size_t q = i; q < j; ++q)
                grp.push_back (sel[q].get());
            std::stable_sort (grp.begin(), grp.end(), [dir] (const Note* a, const Note* b)
            {
                const int k = keyOf (dir == StrumDir::down ? b->p - a->p : a->p - b->p);
                return (k != 0 ? k : timeCmp (*a, *b)) < 0;
            });
            for (std::size_t k = 0; k < grp.size(); ++k)
                shift[grp[k]->id] = static_cast<double> (k) * step;
            i = j;
        }

        return editNotes (clip, Ids (idsOf (sel)), [&] (Note& n, const Note&)
        {
            const auto it = shift.find (n.id);
            const double d = it != shift.end() ? it->second : 0.0;
            if (d == 0.0 || std::isnan (d))
                return;
            const double end = n.s + n.l;
            n.s = jsMax (n.s, jsMin (n.s + d, end - jsMin (kMinLen, n.l)));
            n.l = end - n.s;
        });
    }

    Clip legato (const Clip& clip, const Ids& ids)
    {
        const std::vector<NotePtr> sel = timeOrder (selected (clip, ids));
        std::vector<double> chordStart;
        std::unordered_map<std::string, std::size_t> chordOf;
        for (const NotePtr& n : sel)
        {
            if (chordStart.empty() || n->s > chordStart.back() + 0.05)
                chordStart.push_back (n->s);
            chordOf[n->id] = chordStart.size() - 1;
        }

        return editNotes (clip, Ids (idsOf (sel)), [&] (Note& n, const Note&)
        {
            const std::size_t next = chordOf.at (n.id) + 1;
            if (next < chordStart.size())
                n.l = jsMax (kMinLen, chordStart[next] - n.s);
        });
    }
}
