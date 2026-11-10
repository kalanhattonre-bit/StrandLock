#include "SnapshotPublisher.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace sl::doc
{
    namespace
    {
        bool sameBits (double a, double b) noexcept { return std::memcmp (&a, &b, sizeof a) == 0; }

        /** No generator and every node the same value: the compiler's table holds one value repeated, which it
            stores as 2 floats. (A lane with no nodes reads flat at its default.) */
        bool surelyFlat (const model::Note& n, std::size_t lane) noexcept
        {
            if (n.gen[lane].has_value())
                return false;
            const model::NodeList& nd = n.c[lane];
            for (std::size_t i = 1; i < nd.size(); ++i)
                if (! sameBits (nd[i].v, nd[0].v))
                    return false;
            return true;
        }

        /** compileSnapshot's table length for a note of length l at res: laneTable's max (2, ceil (l x res) + 1),
            capped at kMaxTableLength, on the length normalizeNote leaves (at least 1e-3; 1 when not finite). */
        double tableFloats (double l, int res) noexcept
        {
            const double len = std::isfinite (l) ? std::max (1e-3, l) : 1.0;
            const double n = std::ceil (len * static_cast<double> (res)) + 1.0;
            return std::min (std::isfinite (n) ? std::max (2.0, n) : 2.0, static_cast<double> (model::kMaxTableLength));
        }
    }

    //==============================================================================================
    std::size_t SnapshotSizer::bytesAt (const model::Clip& clip, int res)
    {
        std::unordered_map<std::uint64_t, NoteInfo> now;
        now.reserve (clip.notes.size());
        double floats = 0.0;
        for (const auto& np : clip.notes)
        {
            if (np == nullptr)
                continue;
            const std::uint64_t serial = np->serial.get();
            NoteInfo info;
            if (const auto it = notes.find (serial); it != notes.end())
            {
                info = it->second;
            }
            else
            {
                info.l = np->l;
                for (std::size_t lane = 0; lane < static_cast<std::size_t> (core::kNumLanes); ++lane)
                    info.fullLanes += surelyFlat (*np, lane) ? 0 : 1;
            }
            now[serial] = info;
            floats += info.fullLanes * tableFloats (info.l, res) + (core::kNumLanes - info.fullLanes) * 2.0;
        }
        notes = std::move (now);
        const double bytes = floats * static_cast<double> (sizeof (float));
        return bytes >= 1.8e19 ? static_cast<std::size_t> (-1) : static_cast<std::size_t> (bytes);
    }

    int SnapshotSizer::resolutionFor (const model::Clip& clip, std::size_t* bytes)
    {
        std::size_t b = 0;
        for (const int res : kSnapshotResolutions)
        {
            b = bytesAt (clip, res);
            if (b <= kSnapshotBudgetBytes)
            {
                if (bytes != nullptr)
                    *bytes = b;
                return res;
            }
        }
        if (bytes != nullptr)
            *bytes = b;
        return 0;
    }

    std::size_t snapshotTableBytes (const core::Snapshot& snapshot) noexcept
    {
        std::size_t total = 0;
        for (const auto& t : snapshot.tables)
            if (t != nullptr)
                for (const auto& lane : t->lanes)
                    total += lane.size() * sizeof (float);
        return total;
    }

    //==============================================================================================
    SnapshotPublisher::SnapshotPublisher (rt::VersionExchange<core::Snapshot>& out)
        : juce::Thread ("StrandLock snapshots"), exchange (out)
    {
        startThread();
    }

    SnapshotPublisher::~SnapshotPublisher()
    {
        stopThread (10000);
    }

    void SnapshotPublisher::submit (model::ClipPtr clip, std::uint64_t version)
    {
        if (clip == nullptr)
            return;
        {
            const std::lock_guard<std::mutex> hold (lock);
            ++st.submitted;
            if (version < newestSubmitted)
            {
                // Older than one already handed over (a state load and an edit on two threads can hand their
                // versions over in the wrong order): it would replace the newer one (fix pass RT2-4).
                ++st.stale;
                return;
            }
            newestSubmitted = version;
            if (hasPending)
                ++st.skipped;   // the thread never took the one waiting: only the newest is compiled
            pendingClip = std::move (clip);
            pendingVersion = version;
            hasPending = true;
        }
        notify();
    }

    bool SnapshotPublisher::waitForVersion (std::uint64_t version, int timeoutMs)
    {
        std::unique_lock<std::mutex> hold (lock);
        return published.wait_for (hold, std::chrono::milliseconds (std::max (0, timeoutMs)),
                                   [this, version] { return st.publishedVersion >= version; });
    }

    SnapshotPublisher::Stats SnapshotPublisher::stats() const
    {
        const std::lock_guard<std::mutex> hold (lock);
        return st;
    }

    void SnapshotPublisher::setHeld (bool nowHeld)
    {
        {
            const std::lock_guard<std::mutex> hold (lock);
            held = nowHeld;
        }
        notify();
    }

    void SnapshotPublisher::run()
    {
        while (! threadShouldExit())
        {
            model::ClipPtr clip;
            std::uint64_t version = 0;
            {
                const std::lock_guard<std::mutex> hold (lock);
                if (hasPending && ! held)
                {
                    clip = std::move (pendingClip);
                    pendingClip = nullptr;
                    version = pendingVersion;
                    hasPending = false;
                }
            }
            if (clip == nullptr)
            {
                wait (-1.0);
                continue;
            }
            try
            {
                compileAndPublish (clip, version);
            }
            catch (...)
            {
                // Out of memory, most likely (up to 256 MiB of lane tables). The thread must live on, and the
                // waits for this version must end (fix pass RT2-3).
                publishFailed (*clip, version);
            }
        }
    }

    void SnapshotPublisher::failNextCompile()
    {
        failNext.store (true);
    }

    void SnapshotPublisher::publishFailed (const model::Clip& clip, std::uint64_t version)
    {
        // What a clip too detailed to play gets: its settings and no notes, so its notes are silent, not stale.
        cache.clear();
        bool silent = false;
        try
        {
            model::Clip empty;
            empty.bars = clip.bars;
            empty.beatsPerBar = clip.beatsPerBar;
            empty.offsetBeats = clip.offsetBeats;
            empty.loop = clip.loop;
            empty.tieAcrossLoop = clip.tieAcrossLoop;
            auto snap = model::compileSnapshot (empty, 8);
            if (snap != nullptr)
            {
                snap->clipVersion = version;
                exchange.publish (std::move (snap));
                exchange.collect();
                silent = true;
            }
        }
        catch (...)
        {
            // still no memory: the snapshot before plays on
        }
        {
            const std::lock_guard<std::mutex> hold (lock);
            ++st.failed;
            st.publishedVersion = std::max (st.publishedVersion, version);
            st.lastRes = 0;
            st.lastTableBytes = 0;
            st.tooDetailed = silent;
            st.cachedNotes = 0;
        }
        published.notify_all();
    }

    void SnapshotPublisher::compileAndPublish (const model::ClipPtr& clip, std::uint64_t version)
    {
        const auto t0 = std::chrono::steady_clock::now();
        if (failNext.exchange (false))
            throw std::bad_alloc();

        int res = sizer.resolutionFor (*clip);
        std::unique_ptr<core::Snapshot> snap;
        std::size_t bytes = 0;
        while (res != 0)
        {
            snap = model::compileSnapshot (*clip, res, &cache, &cache);
            bytes = snap != nullptr ? snapshotTableBytes (*snap) : 0;
            if (snap == nullptr || bytes <= kSnapshotBudgetBytes)
                break;
            // Over the budget after all (the estimate is an upper bound but for floating-point corner cases):
            // one step coarser, and nothing at all past res 8.
            const auto* at = std::find (std::begin (kSnapshotResolutions), std::end (kSnapshotResolutions), res);
            res = at + 1 < std::end (kSnapshotResolutions) ? *(at + 1) : 0;
            snap.reset();
        }

        const bool tooDetailed = res == 0;
        if (tooDetailed)
        {
            // The clip's settings and no notes: silence rather than a stale clip.
            model::Clip empty;
            empty.bars = clip->bars;
            empty.beatsPerBar = clip->beatsPerBar;
            empty.offsetBeats = clip->offsetBeats;
            empty.loop = clip->loop;
            empty.tieAcrossLoop = clip->tieAcrossLoop;
            snap = model::compileSnapshot (empty, 8);
            cache.clear();
            bytes = 0;
        }

        if (snap != nullptr)
        {
            snap->clipVersion = version;
            exchange.publish (std::move (snap));
            exchange.collect();
        }

        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        {
            const std::lock_guard<std::mutex> hold (lock);
            ++st.compiled;
            st.publishedVersion = std::max (st.publishedVersion, version);
            st.lastRes = res;
            st.lastTableBytes = bytes;
            st.lastCompileMs = ms;
            st.tooDetailed = tooDetailed;
            st.cachedNotes = cache.size();
        }
        published.notify_all();
    }
}
