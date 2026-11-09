#include "Glides.h"

#include "Num.h"
#include "core/JsCompat.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <unordered_set>

namespace sl::model
{
    void GlideMap::set (const std::string& sourceId, Glide glide)
    {
        const auto it = index.find (sourceId);
        if (it != index.end())
        {
            entries[it->second].second = std::move (glide);
            return;
        }
        index.emplace (sourceId, entries.size());
        entries.emplace_back (sourceId, std::move (glide));
    }

    const Glide* GlideMap::find (const std::string& sourceId) const noexcept
    {
        const auto it = index.find (sourceId);
        return it != index.end() ? &entries[it->second].second : nullptr;
    }

    double startSemis (const Note& n)
    {
        return (noteLane (n, core::LaneId::pitch).front().v - 0.5) * 24.0;
    }

    double endSemis (const Note& n)
    {
        return (noteLane (n, core::LaneId::pitch).back().v - 0.5) * 24.0;
    }

    double glideD (const Note& n, const Note& m)
    {
        return clamp ((m.p + startSemis (m)) - (n.p + endSemis (n)), -48.0, 48.0);
    }

    namespace
    {
        int keyOf (double d) noexcept
        {
            if (d == 0.0 || std::isnan (d))
                return 0;
            return d < 0.0 ? -1 : 1;
        }

        // (a, b) => a.p - b.p || timeCmp (a, b)
        bool byP (const Note* a, const Note* b) noexcept
        {
            const int k = keyOf (a->p - b->p);
            return (k != 0 ? k : timeCmp (*a, *b)) < 0;
        }

        // n.gc || 0
        double curveOf (double gc) noexcept
        {
            return gc != 0.0 && ! std::isnan (gc) ? gc : 0.0;
        }

        bool contains (const std::vector<const Note*>& list, const Note* n) noexcept
        {
            return std::find (list.begin(), list.end(), n) != list.end();
        }

        struct Info
        {
            const Note* n = nullptr;
            double end = 0.0, s0 = 0.0;
            std::vector<const Note*> group;
        };

        std::unordered_map<std::string, const Note*> notesById (const Clip& clip)
        {
            // new Map (notes.map (n => [n.id, n])): a later note with the same id wins
            std::unordered_map<std::string, const Note*> byId;
            for (const NotePtr& n : clip.notes)
                byId[n->id] = n.get();
            return byId;
        }
    }

    GlideMap computeGlides (const Clip& clip, const core::Inst& inst)
    {
        const core::Inst I = core::normalizeInst (inst);
        const auto byId = notesById (clip);
        GlideMap out;
        auto glen = [&I] (const Note& n) { return jsMax (0.0, n.glide.has_value() ? *n.glide : I.glideTime); };

        for (const NotePtr& n : clip.notes)
        {
            if (n->glideTo.empty())
                continue;
            const auto it = byId.find (n->glideTo);
            if (it == byId.end() || it->second == n.get())
                continue;
            const Note& m = *it->second;
            out.set (n->id, Glide { glideD (*n, m), jsMin (glen (*n), n->l * 0.95), curveOf (n->gc), m.id, true });
        }
        if (I.glideMode == core::GlideMode::off)
            return out;

        const double tol = 0.13, reach = I.glideMode == core::GlideMode::always ? 4.0 : tol;
        // a note an explicit link already leads into is not an auto target (one voice per target)
        std::unordered_set<std::string> linkedTo;
        for (const auto& e : out)
            linkedTo.insert (e.second.toId);

        // 1. every auto note's candidate group: the earliest following notes (within 0.05 beat) inside +-12 st
        std::vector<Info> info;
        for (const NotePtr& np : clip.notes)
        {
            const Note* n = np.get();
            if (out.contains (n->id))
                continue;
            const double end = n->s + n->l, hi = end + reach;
            std::vector<const Note*> cand;
            for (const NotePtr& mp : clip.notes)
            {
                const Note* m = mp.get();
                if (m != n && linkedTo.count (m->id) == 0 && m->s > n->s + 1e-3 && m->s >= end - tol && m->s <= hi
                    && std::abs (m->p - n->p) <= 12.0)
                    cand.push_back (m);
            }
            if (cand.empty())
                continue;
            double s0 = cand[0]->s;
            for (const Note* m : cand)
                s0 = jsMin (s0, m->s);
            Info a;
            a.n = n;
            a.end = end;
            a.s0 = s0;
            for (const Note* m : cand)
                if (m->s <= s0 + 0.05)
                    a.group.push_back (m);
            std::stable_sort (a.group.begin(), a.group.end(), byP);
            info.push_back (std::move (a));
        }

        // 2. voice leading by rank among peers: notes that end together, lead into the same next chord and share
        //    at least one candidate
        std::vector<std::size_t> root (info.size());
        std::iota (root.begin(), root.end(), std::size_t { 0 });
        auto find = [&root] (std::size_t i)
        {
            while (root[i] != i)
                i = root[i] = root[root[i]];
            return i;
        };
        for (std::size_t i = 0; i < info.size(); ++i)
        {
            for (std::size_t j = i + 1; j < info.size(); ++j)
            {
                const Info& a = info[i];
                const Info& b = info[j];
                if (std::abs (a.end - b.end) < 0.05 && std::abs (a.s0 - b.s0) < 0.05
                    && std::any_of (a.group.begin(), a.group.end(), [&b] (const Note* m) { return contains (b.group, m); }))
                {
                    const std::size_t ri = find (i);   // root[find (i)] = find (j), in JavaScript's order
                    const std::size_t rj = find (j);
                    root[ri] = rj;
                }
            }
        }

        // clusters, in the order their first member appears in info (a Map keyed by the root)
        std::vector<std::vector<const Info*>> clusters;
        std::unordered_map<std::size_t, std::size_t> clusterOf;
        for (std::size_t i = 0; i < info.size(); ++i)
        {
            const std::size_t r = find (i);
            const auto it = clusterOf.find (r);
            if (it == clusterOf.end())
            {
                clusterOf.emplace (r, clusters.size());
                clusters.push_back ({ &info[i] });
            }
            else
            {
                clusters[it->second].push_back (&info[i]);
            }
        }

        for (auto& peers : clusters)
        {
            std::stable_sort (peers.begin(), peers.end(), [] (const Info* a, const Info* b) { return byP (a->n, b->n); });
            // [...new Set ([].concat (...peers.map (a => a.group)))].sort (byP)
            std::vector<const Note*> all;
            for (const Info* a : peers)
                for (const Note* m : a->group)
                    if (! contains (all, m))
                        all.push_back (m);
            std::stable_sort (all.begin(), all.end(), byP);

            for (std::size_t r = 0; r < peers.size(); ++r)
            {
                const Info& a = *peers[r];
                const Note& n = *a.n;
                auto nearest = [&a] (double want)
                {
                    const Note* x = a.group[0];
                    for (std::size_t k = 1; k < a.group.size(); ++k)
                    {
                        const Note* y = a.group[k];
                        if (std::abs (y->p - want) < std::abs (x->p - want))
                            x = y;
                    }
                    return x;
                };
                const Note* tgt = nullptr;
                if (peers.size() > 1 && all.size() > 1)
                {
                    const double at = core::jsRound (static_cast<double> (r) * static_cast<double> (all.size() - 1)
                                                     / static_cast<double> (peers.size() - 1));
                    const Note* ranked = all[static_cast<std::size_t> (at)];
                    tgt = contains (a.group, ranked) ? ranked : nearest (ranked->p);
                }
                else
                {
                    tgt = nearest (n.p);
                }
                out.set (n.id, Glide { glideD (n, *tgt), jsMin (glen (n), n.l * 0.95), curveOf (n.gc), tgt->id, false });
            }
        }
        return out;
    }

    Chains buildChains (const Clip& clip, const GlideMap& glides)
    {
        const auto byId = notesById (clip);

        struct In
        {
            const Note* n;
            const Glide* g;
        };
        // incoming: a Map keyed by the target id, in the order the targets first appear
        std::vector<std::pair<std::string, std::vector<In>>> incoming;
        std::unordered_map<std::string, std::size_t> incomingAt;
        for (const auto& [sid, g] : glides)
        {
            const auto ni = byId.find (sid);
            const auto mi = byId.find (g.toId);
            if (ni == byId.end() || mi == byId.end())
                continue;
            const Note* n = ni->second;
            const Note* m = mi->second;
            if (m == n || m->sample.restart || ! (m->s > n->s))
                continue;
            const auto it = incomingAt.find (m->id);
            if (it == incomingAt.end())
            {
                incomingAt.emplace (m->id, incoming.size());
                incoming.emplace_back (m->id, std::vector<In> { In { n, &g } });
            }
            else
            {
                incoming[it->second].second.push_back (In { n, &g });
            }
        }

        std::unordered_map<std::string, std::string> next, prev;
        for (auto& [mid, list] : incoming)
        {
            // (b.g.linked - a.g.linked) || Math.abs (a.g.d) - Math.abs (b.g.d) || timeCmp (a.n, b.n)
            std::stable_sort (list.begin(), list.end(), [] (const In& a, const In& b)
            {
                int k = keyOf (static_cast<double> (b.g->linked) - static_cast<double> (a.g->linked));
                if (k == 0)
                    k = keyOf (std::abs (a.g->d) - std::abs (b.g->d));
                if (k == 0)
                    k = timeCmp (*a.n, *b.n);
                return k < 0;
            });
            const std::string& src = list[0].n->id;
            next[src] = mid;
            prev[mid] = src;
        }

        Chains chains;
        std::unordered_set<std::string> seen;
        auto walkFrom = [&] (const std::string& head)
        {
            std::vector<std::string> ch;
            std::optional<std::string> id = head;
            while (id.has_value() && ! id->empty() && seen.count (*id) == 0)
            {
                seen.insert (*id);
                ch.push_back (*id);
                const auto it = next.find (*id);
                id = it != next.end() ? std::optional<std::string> (it->second) : std::nullopt;
            }
            return ch;
        };
        const std::vector<NotePtr> ordered = timeOrder (clip.notes);
        for (const NotePtr& n : ordered)
            if (prev.count (n->id) == 0 && seen.count (n->id) == 0)
                chains.push_back (walkFrom (n->id));
        for (const NotePtr& n : ordered)   // cycle guard
            if (seen.count (n->id) == 0)
                chains.push_back (walkFrom (n->id));
        return chains;
    }

    Chains buildChains (const Clip& clip)
    {
        return buildChains (clip, computeGlides (clip, core::defaultInst()));
    }
}
