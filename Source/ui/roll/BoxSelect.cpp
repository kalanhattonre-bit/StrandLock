#include "BoxSelect.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace sl::ui::roll
{
    SelectBox SelectBox::between (double ax, double ay, double bx, double by) noexcept
    {
        return { std::min (ax, bx), std::min (ay, by), std::max (ax, bx), std::max (ay, by) };
    }

    std::vector<std::string> notesInBox (const RollGeometry& geo, const model::Clip& clip, bool withRibbon, const SelectBox& box)
    {
        // sl-ui.js applyBox: bx.x1 >= x0 && bx.x0 <= x1 && bx.bottom >= y0 && bx.top <= y1 (a NaN edge meets nothing)
        std::vector<std::string> hits;
        for (const auto& n : clip.notes)
        {
            const NoteBox b = geo.noteBox (*n, withRibbon);
            if (b.x1 >= box.x0 && b.x0 <= box.x1 && b.bottom >= box.y0 && b.top <= box.y1)
                hits.push_back (n->id);
        }
        return hits;
    }

    std::vector<std::string> combineNotes (const std::vector<std::string>& base, const std::vector<std::string>& hits, bool shift, bool ctrl)
    {
        if (! shift && ! ctrl)
            return hits;
        std::vector<std::string> out;
        std::unordered_set<std::string> in;
        for (const auto& id : base)
            if (in.insert (id).second)
                out.push_back (id);
        for (const auto& id : hits)
        {
            if (in.count (id) == 0)
            {
                in.insert (id);
                out.push_back (id);
            }
            else if (! shift)   // Ctrl: a note the box meets again is taken out
            {
                in.erase (id);
                out.erase (std::remove (out.begin(), out.end(), id), out.end());
            }
        }
        return out;
    }

    std::vector<doc::NodePick> nodesInBox (const RollGeometry& geo, const model::Clip& clip, const doc::RollState& state, core::LaneId lane,
                                           const SelectBox& box)
    {
        std::vector<doc::NodePick> hits;
        for (const auto& n : RollGeometry::selectedNotes (clip, state))
        {
            doc::NodePick pk;
            pk.id = n->id;
            const int count = (int) model::noteLane (*n, lane).size();
            for (int k = 0; k < count; ++k)
            {
                const Point q = geo.nodeXY (*n, lane, k);
                if (q.x >= box.x0 && q.x <= box.x1 && q.y >= box.y0 && q.y <= box.y1)
                    pk.ks.push_back (k);
            }
            if (! pk.ks.empty())
                hits.push_back (std::move (pk));
        }
        return hits;
    }

    std::vector<doc::NodePick> combineNodes (const std::vector<doc::NodePick>& base, const std::vector<doc::NodePick>& hits, bool shift, bool ctrl)
    {
        std::vector<doc::NodePick> out;
        if (shift || ctrl)
            out = base;
        for (const auto& h : hits)
        {
            auto at = std::find_if (out.begin(), out.end(), [&h] (const doc::NodePick& p) { return p.id == h.id; });
            doc::NodePick cur;
            if (at != out.end())
                cur = *at;
            else
                cur.id = h.id;
            for (const int k : h.ks)
            {
                const auto has = std::find (cur.ks.begin(), cur.ks.end(), k);
                if (ctrl && has != cur.ks.end())
                    cur.ks.erase (has);
                else if (has == cur.ks.end())
                    cur.ks.push_back (k);
            }
            if (! cur.ks.empty())
            {
                if (at != out.end())
                    *at = std::move (cur);   // a JavaScript Map's set keeps the key's place
                else
                    out.push_back (std::move (cur));
            }
            else if (at != out.end())
            {
                out.erase (at);
            }
        }
        return out;
    }
}
