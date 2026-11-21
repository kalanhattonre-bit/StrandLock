#include "ClipDocument.h"

#include "core/JsCompat.h"
#include "core/Lanes.h"
#include "core/Music.h"
#include "model/History.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_set>
#include <utility>

namespace sl::doc
{
    namespace
    {
        /** A mod-wheel target's lane index for a saved id: "off" is off, a lane id is that lane, anything else
            plays as normalizeInst's default for that wheel (plan 2.2: an unknown id is kept verbatim). */
        std::int8_t modTargetIndex (const std::string& id, core::LaneId fallback) noexcept
        {
            if (id == "off")
                return core::kModOff;
            if (const auto lane = core::findLane (id))
                return static_cast<std::int8_t> (*lane);
            return static_cast<std::int8_t> (fallback);
        }

        std::string printed (const char* pattern, double a, double b = 0.0)
        {
            char text[256];
            std::snprintf (text, sizeof (text), pattern, a, b);
            return text;
        }
    }

    bool sameNonParam (const NonParamState& a, const NonParamState& b)
    {
        // The SMPL analysis cache (M7) describes the file, not an edit: a cache that arrives later (a project's file
        // analysed after its load, a Detect) is no change of state, so it never makes or splits an undo step. So do the
        // "Keep the sound in the project" switch and the 20-50 MB answer (M7 V3): they say how the project holds the
        // sound, not what plays.
        auto withoutCache = [] (state::SampleRef r)
        {
            r.file.cache = {};
            r.file.keepInProject = 1;
            r.file.embedAnswer = 0;
            return r;
        };
        return state::encodeInst (a.inst) == state::encodeInst (b.inst)
            && state::encodeSmpl (withoutCache (a.sound)) == state::encodeSmpl (withoutCache (b.sound));
    }

    //==============================================================================================
    ClipDocument::ClipDocument (params::NonParamAtomics& a)
        : atomics (a)
    {
        clip = std::make_shared<const model::Clip> (model::newClip (4.0));
        bytes = sizer.measure (*clip);
        ver = 1;
        writeAtomics (np.inst);
    }

    model::ClipPtr ClipDocument::current() const
    {
        const std::lock_guard<std::mutex> hold (readLock);
        return clip;
    }

    std::uint64_t ClipDocument::version() const noexcept
    {
        const std::lock_guard<std::mutex> hold (readLock);
        return ver;
    }

    std::size_t ClipDocument::encodedBytes() const
    {
        const std::lock_guard<std::mutex> hold (readLock);
        return bytes;
    }

    NonParamState ClipDocument::nonParam() const
    {
        const std::lock_guard<std::mutex> hold (readLock);
        return np;
    }

    state::InstState ClipDocument::instState() const
    {
        const std::lock_guard<std::mutex> hold (readLock);
        return np.inst;
    }

    state::SampleRef ClipDocument::soundRef() const
    {
        const std::lock_guard<std::mutex> hold (readLock);
        return np.sound;
    }

    //==============================================================================================
    std::string ClipDocument::refusalText (const model::EditResult& r, bool tooDetailed, std::size_t tableBytes)
    {
        constexpr double mb = 1024.0 * 1024.0;
        if (tooDetailed)
            return printed ("That would make the clip too detailed to play: its lanes would need %.0f MB; the limit is %.0f MB.",
                           static_cast<double> (tableBytes) / mb, static_cast<double> (kSnapshotBudgetBytes) / mb);
        if (r.count > model::kMaxClipNotes)
            return printed ("That would make %.0f notes; the limit is %.0f.", r.count, static_cast<double> (model::kMaxClipNotes));
        if (r.bytes > model::kMaxClipBytes)
            return printed ("That would make the clip %.1f MB; the limit is %.0f MB.", static_cast<double> (r.bytes) / mb,
                           static_cast<double> (model::kMaxClipBytes) / mb);
        return printed ("That would make %.0f notes, more than this tool makes at once.", r.count);
    }

    EditOutcome ClipDocument::apply (model::Clip proposed)
    {
        model::EditResult r;
        r.clip = std::move (proposed);
        return apply (std::move (r));
    }

    EditOutcome ClipDocument::apply (model::EditResult proposed)
    {
        const std::lock_guard<std::mutex> edit (editLock);
        const model::ClipPtr before = current();

        EditOutcome out;
        if (proposed.refused)
        {
            out.refused = true;
            out.message = refusalText (proposed, false, 0);
            out.result = std::move (proposed);
            out.version = version();
            return out;
        }

        model::EditResult limited = model::limitEdit (*before, std::move (proposed), sizer);
        if (limited.refused)
        {
            out.refused = true;
            out.message = refusalText (limited, false, 0);
            out.result = std::move (limited);
            out.version = version();
            return out;
        }

        std::size_t tables = 0;
        if (snapshotSizer.resolutionFor (limited.clip, &tables) == 0)
        {
            out.refused = true;
            out.message = refusalText (limited, true, tables);
            limited.refused = true;
            limited.clip = *before;
            limited.ids.clear();
            out.result = std::move (limited);
            out.version = version();
            return out;
        }

        return accept (std::move (limited));
    }

    EditOutcome ClipDocument::accept (model::EditResult r)
    {
        EditOutcome out;
        out.accepted = true;
        const model::ClipPtr before = current();
        if (model::sameClip (r.clip, *before))
        {
            out.result = std::move (r);
            out.version = version();
            return out;   // nothing changed: no new version
        }

        auto next = std::make_shared<const model::Clip> (r.clip);
        std::uint64_t v = 0;
        {
            const std::lock_guard<std::mutex> hold (readLock);
            clip = next;
            bytes = r.bytes;
            v = ++ver;
            pruneLocked (*next);
        }
        out.result = std::move (r);
        out.version = v;
        if (onClipChanged)
            onClipChanged (next, v);
        return out;
    }

    void ClipDocument::replace (model::ClipPtr newClip)
    {
        if (newClip == nullptr)
            return;
        std::uint64_t v = 0;
        {
            const std::lock_guard<std::mutex> edit (editLock);
            const std::size_t size = sizer.measure (*newClip);
            (void) snapshotSizer.bytesAt (*newClip, kSnapshotResolutions[0]);   // keeps its note cache on this clip
            const std::lock_guard<std::mutex> hold (readLock);
            clip = newClip;
            bytes = size;
            v = ++ver;
            pruneLocked (*newClip);
            // a state load, Undo and Redo let go of the inspector's node and the picked nodes: they may name other
            // nodes in this clip (sl-ui.js applyState)
            roll.activeNode.reset();
            roll.picks.clear();
        }
        if (onClipChanged)
            onClipChanged (newClip, v);
    }

    void ClipDocument::setNonParam (const NonParamState& s)
    {
        const std::lock_guard<std::mutex> edit (editLock);
        {
            const std::lock_guard<std::mutex> hold (readLock);
            np = s;
        }
        writeAtomics (s.inst);
    }

    void ClipDocument::setInstState (const state::InstState& s)
    {
        const std::lock_guard<std::mutex> edit (editLock);
        {
            const std::lock_guard<std::mutex> hold (readLock);
            np.inst = s;
        }
        writeAtomics (s);
    }

    void ClipDocument::setSoundRef (const state::SampleRef& r)
    {
        const std::lock_guard<std::mutex> edit (editLock);
        const std::lock_guard<std::mutex> hold (readLock);
        np.sound = r;
    }

    //==============================================================================================
    // The roll's state.

    RollState ClipDocument::rollState() const
    {
        const std::lock_guard<std::mutex> hold (readLock);
        return roll;
    }

    void ClipDocument::selectLocked (const model::Clip& c, std::vector<std::string> ids, std::optional<std::string> primary)
    {
        const model::IdSet exists = model::idSet (c, model::Ids::all());
        std::vector<std::string> set;
        std::unordered_set<std::string> seen;
        for (auto& id : ids)
            if (exists.count (id) != 0 && seen.insert (id).second)
                set.push_back (std::move (id));
        std::string p = primary ? *primary : roll.primary;
        if (! p.empty() && exists.count (p) == 0)
            p.clear();
        if (! p.empty() && seen.count (p) == 0)
        {
            if (primary)
            {
                set.push_back (p);
                seen.insert (p);
            }
            else
            {
                p.clear();
            }
        }
        if (p.empty() && ! set.empty())
            p = set.front();
        roll.selected = std::move (set);
        roll.primary = std::move (p);
        if (roll.activeNode && seen.count (roll.activeNode->id) == 0)
            roll.activeNode.reset();
        // the picked nodes of notes that are no longer selected go (sl-ui.js setSel)
        roll.picks.erase (std::remove_if (roll.picks.begin(), roll.picks.end(),
                                          [&seen] (const NodePick& pk) { return seen.count (pk.id) == 0; }),
                          roll.picks.end());
    }

    int ClipDocument::laneCountLocked (const model::Clip& c, const std::string& id) const
    {
        // sl-ui.js: n && AL && n.c[AL] ? n.c[AL].length : -1
        const auto lane = roll.activeLane();
        if (! lane)
            return -1;
        for (const auto& q : c.notes)
            if (q->id == id)
                return (int) model::noteLane (*q, *lane).size();
        return -1;
    }

    void ClipDocument::pruneLocked (const model::Clip& c)
    {
        const model::IdSet exists = model::idSet (c, model::Ids::all());
        std::vector<std::string> keep;
        for (const auto& id : roll.selected)
            if (exists.count (id) != 0)
                keep.push_back (id);
        const bool primaryOk = ! roll.primary.empty() && exists.count (roll.primary) != 0;
        if (keep.size() != roll.selected.size() || (! roll.primary.empty() && ! primaryOk))
            selectLocked (c, std::move (keep), primaryOk ? roll.primary : std::string());
        if (roll.activeNode)
        {
            const auto& a = *roll.activeNode;
            const model::Note* n = nullptr;
            for (const auto& q : c.notes)
                if (q->id == a.id)
                {
                    n = q.get();
                    break;
                }
            const int len = n != nullptr ? (int) model::noteLane (*n, a.lane).size() : -1;
            if (n == nullptr || len != a.len || a.k >= len)
                roll.activeNode.reset();
        }
        // sl-ui.js validateNodes: a note's picks go when it is gone or its count on the shown lane changed
        roll.picks.erase (std::remove_if (roll.picks.begin(), roll.picks.end(),
                                          [this, &c] (const NodePick& pk)
                                          {
                                              const int len = laneCountLocked (c, pk.id);
                                              return len < 0 || len != pk.len;
                                          }),
                          roll.picks.end());
    }

    void ClipDocument::setSelection (std::vector<std::string> ids, std::optional<std::string> primary)
    {
        const std::lock_guard<std::mutex> hold (readLock);
        selectLocked (*clip, std::move (ids), std::move (primary));
    }

    void ClipDocument::setPrimary (const std::string& id)
    {
        const std::lock_guard<std::mutex> hold (readLock);
        if (! id.empty() && ! roll.isSelected (id))
        {
            auto ids = roll.selected;
            ids.push_back (id);
            selectLocked (*clip, std::move (ids), id);
        }
        else if (id != roll.primary)
        {
            roll.primary = id;
        }
    }

    void ClipDocument::setMode (double m)
    {
        const std::lock_guard<std::mutex> hold (readLock);
        const double r = std::isfinite (m) ? core::jsRound (m) : 1.0;
        const int before = roll.mode;
        roll.mode = (int) std::clamp (r, 1.0, 8.0);
        if (roll.activeNode && roll.activeNode->lane != roll.activeLane())
            roll.activeNode.reset();
        // node picks belong to the lane shown: they go when the mode changes (sl-ui.js setMode)
        if (roll.mode != before)
            roll.picks.clear();
    }

    void ClipDocument::setSubLane (core::LaneId lane)
    {
        if (std::find (std::begin (core::kSampleLanes), std::end (core::kSampleLanes), lane) == std::end (core::kSampleLanes))
            return;
        const std::lock_guard<std::mutex> hold (readLock);
        const bool changed = lane != roll.subLane || roll.mode != 8;
        roll.subLane = lane;
        roll.mode = 8;
        if (roll.activeNode && roll.activeNode->lane != roll.activeLane())
            roll.activeNode.reset();
        if (changed)
            roll.picks.clear();
    }

    void ClipDocument::setActiveNode (std::optional<ActiveNode> node)
    {
        const std::lock_guard<std::mutex> hold (readLock);
        if (node && node->len == 0)
            for (const auto& q : clip->notes)
                if (q->id == node->id)
                {
                    node->len = (int) model::noteLane (*q, node->lane).size();
                    break;
                }
        roll.activeNode = std::move (node);
    }

    void ClipDocument::setNodePicks (std::vector<NodePick> picks)
    {
        const std::lock_guard<std::mutex> hold (readLock);
        std::vector<NodePick> out;
        out.reserve (picks.size());
        for (auto& pk : picks)
        {
            if (pk.ks.empty())
                continue;
            bool repeat = false;
            for (const auto& o : out)
                repeat = repeat || o.id == pk.id;
            if (repeat)
                continue;
            pk.len = laneCountLocked (*clip, pk.id);
            out.push_back (std::move (pk));
        }
        roll.picks = std::move (out);
    }

    void ClipDocument::setNotePicks (const std::string& id, std::vector<int> ks, int len)
    {
        const std::lock_guard<std::mutex> hold (readLock);
        roll.picks.erase (std::remove_if (roll.picks.begin(), roll.picks.end(), [&id] (const NodePick& pk) { return pk.id == id; }),
                          roll.picks.end());
        if (ks.empty())
            return;
        NodePick pk;
        pk.id = id;
        pk.ks = std::move (ks);
        pk.len = len;
        roll.picks.push_back (std::move (pk));
    }

    //==============================================================================================
    void ClipDocument::writeAtomics (const state::InstState& s) noexcept
    {
        atomics.seed.store (s.seed);
        atomics.mod1Target.store (modTargetIndex (s.mod1Target, core::LaneId::vib));
        atomics.mod2Target.store (modTargetIndex (s.mod2Target, core::LaneId::tone));
        atomics.mod1Cc.store (static_cast<std::int8_t> (std::min<int> (s.mod1Cc, 127)));
        atomics.mod2Cc.store (static_cast<std::int8_t> (std::min<int> (s.mod2Cc, 127)));
        atomics.key.store (static_cast<std::int8_t> (std::min<int> (s.key, 11)));
        const int scale = core::findScale (s.scale);
        atomics.scale.store (static_cast<std::uint8_t> (scale < 0 ? 0 : scale));
        atomics.scaleLock.store (s.scaleLock != 0);
        atomics.regionStartExact.store (s.regionStartExact);
        atomics.regionEndExact.store (s.regionEndExact);
    }
}
