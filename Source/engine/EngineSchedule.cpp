// sl-engine.js Engine.addVoice, scanStarts, startClip and doChase (PLUGIN-PLAN 1.2, 1.8, 1.10), with the plugin's
// changes:
//   - the 64-voice pool (8.2 #8): a new voice takes a free pool object; with all 64 busy the oldest stealing voice
//     is cut at once (stats ().cuts);
//   - scanStarts finds its candidates by binary search on the start-sorted notes (8.2 #29) and keeps them in a
//     fixed array of 256 (more in one segment start at the next segment's first sample: stats ().deferredStarts);
//     the starts and their order are the JS ones;
//   - the scan rules of a tempo re-anchor (8.2 #3): no start twice when the clock moves back, and the starts the
//     clock jumped over start at once when it moves forward;
//   - the bounded chase (8.2 #22): one chase moment runs up to kChaseTicksNow ticks at once, shared by its voices
//     in doChase order; a voice that needs more is silent and waits ("chasing"), replaying kChaseTicksPer32
//     ticks per 32 output samples on the control grid, and starts its 10 ms fade-in once it has caught up.
// A chase whose voices need at most 4096 ticks together is the JS chase bit for bit (every fixture case).

#include "engine/Engine.h"

#include "engine/EngineMath.h"

#include <algorithm>
#include <cmath>

namespace sl::engine
{
    namespace
    {
        /** The JS sort (a, b) => a.kS - b.kS || a.X.s - b.X.s || a.X.p - b.X.p, made total by the note order (the
            JS sort is stable and its input is in note order) and the pass. */
        template <class E>
        bool startBefore (const E& a, const E& b) noexcept
        {
            if (a.kKey != b.kKey)
                return a.kKey < b.kKey;
            if (a.s != b.s)
                return a.s < b.s;
            if (a.p != b.p)
                return a.p < b.p;
            if (a.index != b.index)
                return a.index < b.index;
            return a.m < b.m;
        }
    }

    //==============================================================================================
    // The pool and addVoice.

    Voice* Engine::allocVoice() noexcept
    {
        if (voicePool == nullptr)
            return nullptr;
        compactVoices();
        for (int s = 0; s < kVoicePool; ++s)
            if (! slotUsed[static_cast<std::size_t> (s)])
                return &voiceAt (s);
        // all 64 objects busy (8.2 #8): cut the oldest stealing voice at once (or, never expected, the oldest voice)
        int victim = -1;
        for (int pass = 0; pass < 2 && victim < 0; ++pass)
            for (int i = 0; i < numVoices; ++i)
            {
                const int slot = order[static_cast<std::size_t> (i)];
                const Voice& w = voiceAt (slot);
                if (pass == 0 && w.st != EnvStage::steal)
                    continue;
                if (victim < 0 || w.serial < voiceAt (victim).serial)
                    victim = slot;
            }
        if (victim < 0)
            return nullptr;
        Voice& v = voiceAt (victim);
        v.done = true;
        v.chasing = false;
        ++st.cuts;
        compactVoices();
        return &v;
    }

    void Engine::admitVoice (int slot) noexcept
    {
        // the steal half of the JS addVoice: above polyphony the oldest (released ones first) fades in 5 ms
        const double poly = smoother.current().polyphony;
        int count = 0;
        for (int i = 0; i < numVoices; ++i)
        {
            const Voice& w = voiceAt (order[static_cast<std::size_t> (i)]);
            if (! w.done && w.st != EnvStage::steal)
                ++count;
        }
        while (count >= poly)
        {
            Voice* victim = nullptr;
            for (int i = 0; i < numVoices; ++i)
            {
                Voice& w = voiceAt (order[static_cast<std::size_t> (i)]);
                if (w.done || w.st == EnvStage::steal)
                    continue;
                if (victim == nullptr || (w.released && ! victim->released) || (w.released == victim->released && w.serial < victim->serial))
                    victim = &w;
            }
            if (victim == nullptr)
                break;
            victim->steal();
            ++st.steals;
            --count;
            if (victim->chasing)
            {   // it never sounded: it ends here
                victim->chasing = false;
                victim->done = true;
            }
        }
        order[static_cast<std::size_t> (numVoices++)] = static_cast<std::uint8_t> (slot);
        slotUsed[static_cast<std::size_t> (slot)] = true;
        if (voiceAt (slot).chasing)
            anyChasing = true;
    }

    void Engine::addVoice (int slot, std::int64_t k0) noexcept
    {
        admitVoice (slot);
        // first control tick at its own start sample, so the planner sees its heads
        voiceAt (slot).startAt (k0);
        plan();
    }

    //==============================================================================================
    // scanStarts.

    bool Engine::suppressed (double U) const noexcept
    {
        // a start the clock before a re-anchor had already emitted (its kOf (U) under that clock < kR)
        for (int r = 0; r < numRules; ++r)
        {
            const ScanRule& rule = rules[static_cast<std::size_t> (r)];
            const std::int64_t kOld = rule.anchorK + toInt64 (std::ceil ((U - rule.anchorBeat) / rule.db - 1e-7));
            if (kOld < rule.kR)
                return true;
        }
        return false;
    }

    void Engine::offerStart (std::int32_t index, std::int64_t m, double U, std::int64_t k0, std::int64_t k1, std::int64_t lowK) noexcept
    {
        const core::NoteSnap& X = snap->notes[static_cast<std::size_t> (index)];
        StartEvent e;
        e.kKey = clock.kOf (U);
        e.U = U;
        e.s = X.s;
        e.p = X.p;
        e.m = m;
        e.index = index;
        if (e.kKey >= k0 && e.kKey < k1)
        {
            if (e.kKey < playStartK)
                return;
            e.kS = e.kKey;
        }
        else if (e.kKey < k0)
        {
            const bool deferred = deferActive && e.kKey >= lowK && startBefore (deferCut, e);
            const bool caughtUp = catchUpPending && U >= catchUpLowU;
            if (! deferred && ! caughtUp)
                return;
            e.kS = k0;   // starts at this segment's first sample
        }
        else
        {
            return;
        }
        if (numRules > 0 && suppressed (U))
            return;

        // keep the 256 earliest in order (a sorted insert; equal keys cannot occur)
        ++startCandidates;
        if (numStarts == kMaxStarts)
        {
            if (! startBefore (e, starts[static_cast<std::size_t> (kMaxStarts - 1)]))
                return;
            --numStarts;
        }
        int j = numStarts;
        while (j > 0 && startBefore (e, starts[static_cast<std::size_t> (j - 1)]))
        {
            starts[static_cast<std::size_t> (j)] = starts[static_cast<std::size_t> (j - 1)];
            --j;
        }
        starts[static_cast<std::size_t> (j)] = e;
        ++numStarts;
    }

    void Engine::collectStarts (double ua, double ub, std::int64_t k0, std::int64_t k1, std::int64_t lowK) noexcept
    {
        const auto& notes = snap->notes;
        const auto count = static_cast<std::int32_t> (notes.size());
        const double Lb = clock.Lb;
        const bool loop = clock.loop;

        // the JS tests for one note and pass, exactly as written
        auto tryPass = [&] (std::int32_t i, std::int64_t m) {
            const core::NoteSnap& X = notes[static_cast<std::size_t> (i)];
            if (loop && (X.s >= Lb || X.s < 0))
                return;
            std::int64_t mA = 0, mB = 0;
            if (loop)
            {
                mA = toInt64 (jsMax (0, std::floor ((ua - X.s) / Lb)));
                mB = toInt64 (std::floor ((ub - X.s) / Lb));
            }
            if (m < mA || m > mB)
                return;
            const double U = X.s + static_cast<double> (m) * Lb;
            if (U < ua || U > ub)
                return;
            offerStart (i, m, U, k0, k1, lowK);
        };

        const double mLoD = loop ? jsMax (0, std::floor ((ua - Lb) / Lb)) : 0;
        const double mHiD = loop ? std::floor (ub / Lb) : 0;
        if (! notesSearchable || ! (mHiD - mLoD <= 1024))
        {
            // the JS walk over every note (a raw snapshot that is not start-sorted, or a very wide window)
            for (std::int32_t i = 0; i < count; ++i)
            {
                const core::NoteSnap& X = notes[static_cast<std::size_t> (i)];
                if (! noteOk (X))
                    continue;
                if (loop && (X.s >= Lb || X.s < 0))
                    continue;
                std::int64_t mA = 0, mB = 0;
                if (loop)
                {
                    mA = toInt64 (jsMax (0, std::floor ((ua - X.s) / Lb)));
                    mB = toInt64 (std::floor ((ub - X.s) / Lb));
                }
                for (std::int64_t m = mA; m <= mB; ++m)
                    tryPass (i, m);
            }
            return;
        }

        // 8.2 #29: for each loop pass m in the window, the notes with s in [ua - m Lb, ub - m Lb] by binary search
        // (widened by a margin; every candidate then goes through the JS tests above)
        constexpr double margin = 1e-6;
        const auto mLo = toInt64 (mLoD), mHi = toInt64 (mHiD);
        for (std::int64_t m = mLo; m <= mHi; ++m)
        {
            const double base = static_cast<double> (m) * Lb;
            const double sLo = ua - base - margin, sHi = ub - base + margin;
            auto it = std::lower_bound (notes.begin(), notes.end(), sLo, [] (const core::NoteSnap& X, double s) { return X.s < s; });
            for (; it != notes.end() && it->s <= sHi; ++it)
                tryPass (static_cast<std::int32_t> (it - notes.begin()), m);
        }
    }

    void Engine::scanStarts (std::int64_t k0, std::int64_t k1) noexcept
    {
        numStarts = 0;
        startCandidates = 0;
        if (! clock.playing || noteCount == 0 || snap == nullptr)
        {
            deferActive = false;
            catchUpPending = false;
            return;
        }
        const double db = clock.db;
        double ua = clock.uAt (k0) - 2 * db;
        const double ub = clock.uAt (k1) + 2 * db;
        std::int64_t lowK = k0;
        if (deferActive)
        {
            lowK = deferCut.kKey;
            ua = jsMin (ua, clock.uAt (lowK) - 2 * db);
        }
        if (catchUpPending)
            ua = jsMin (ua, catchUpLowU);
        // rules the scan has passed can no longer suppress anything
        int w = 0;
        for (int r = 0; r < numRules; ++r)
        {
            const ScanRule& rule = rules[static_cast<std::size_t> (r)];
            if (! (ua > rule.horizonU + 2 * rule.db))
                rules[static_cast<std::size_t> (w++)] = rule;
        }
        numRules = w;

        collectStarts (ua, ub, k0, k1, lowK);

        if (startCandidates > kMaxStarts)
        {
            st.deferredStarts += startCandidates - kMaxStarts;
            deferActive = true;
            deferCut = starts[static_cast<std::size_t> (kMaxStarts - 1)];
        }
        else
        {
            deferActive = false;
        }
        catchUpPending = false;
    }

    //==============================================================================================
    // startClip.

    void Engine::startClip (const StartEvent& e) noexcept
    {
        const core::NoteSnap& X = snap->notes[static_cast<std::size_t> (e.index)];
        const core::ResolvedNote rn = core::resolve (X, smoother.current());
        const std::int32_t pid = X.restart ? -1 : rn.chain.prevIndex;
        auto playsNote = [this] (const Voice& v, std::int32_t index) {
            if (index < 0 || static_cast<std::size_t> (index) >= snap->notes.size())
                return false;
            const core::NoteSnap& target = snap->notes[static_cast<std::size_t> (index)];
            if (v.snap == snap)
                return v.note == &target;
            // a voice still on another version (never expected for one that is not released): compare the ids
            if (v.snap == nullptr || v.note == nullptr || v.note->idHash != target.idHash)
                return false;
            const auto a = static_cast<std::size_t> (v.note->idIndex), b = static_cast<std::size_t> (target.idIndex);
            return v.note->idIndex >= 0 && target.idIndex >= 0 && a < v.snap->ids.size() && b < snap->ids.size() && v.snap->ids[a] == snap->ids[b];
        };
        if (pid >= 0)
        {   // a chain plays on ONE voice: hand over, no new attack (D2)
            for (int i = 0; i < numVoices; ++i)
            {
                Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                if (v.kind != VoiceKind::clip || v.released || v.done || v.m != e.m)
                    continue;
                if (v.chasing)
                {
                    // a bounded chase replays this chain and hands over by itself when it reaches this note
                    if (v.snap == snap && core::resolve (*v.note, smoother.current()).chain.firstIndex == rn.chain.firstIndex)
                        return;
                    continue;
                }
                if (playsNote (v, pid))
                {
                    v.switchTo (&X, e.U);
                    return;
                }
            }
        }
        // a bounded chase that caught up at this very sample has already handed over to this note (never the case
        // in the JS, whose chase ends before the starts of its sample are scanned)
        if (pid >= 0)
            for (int i = 0; i < numVoices; ++i)
            {
                const Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                if (v.kind == VoiceKind::clip && ! v.released && ! v.done && ! v.chasing && v.m == e.m && playsNote (v, e.index))
                    return;
            }
        Voice* v = allocVoice();
        if (v == nullptr)
            return;
        v->initClip (ctx, source, snap, snapEpoch, e.index, e.U, e.m, e.kS, ++serial);
        addVoice (static_cast<int> (v - voicePool.get()), e.kS);
    }

    //==============================================================================================
    // Chase: starting the transport inside a note starts it part-way, at the right lane time and playhead position.

    void Engine::doChase (std::int64_t kNow) noexcept
    {
        if (! clock.playing || noteCount == 0 || snap == nullptr)
            return;
        const Clock& C = clock;
        const double u = C.uAt (kNow), Lb = C.Lb;
        if (u < 0)
            return;   // 8.2 #4: before bar 1 nothing is sounding
        const core::Inst& I = smoother.current();
        const auto& notes = snap->notes;
        const auto count = static_cast<std::int32_t> (notes.size());
        std::int64_t remaining = kChaseTicksNow;
        for (std::int32_t i = 0; i < count; ++i)
        {
            const core::NoteSnap& X = notes[static_cast<std::size_t> (i)];
            if (! noteOk (X))
                continue;
            const double eff = C.effLen (X);
            if (eff <= 0)
                continue;
            const double mD = C.loop ? std::floor ((u - X.s) / Lb) : 0;
            if (mD < 0)
                continue;
            const double U = X.s + mD * Lb;
            if (! (u < U + eff) || C.kOf (U) >= kNow)
                continue;
            std::int32_t first = i;
            double Uf = U;
            for (int g = 0; g < 256; ++g)
            {
                const core::NoteSnap& F = notes[static_cast<std::size_t> (first)];
                const std::int32_t pvIndex = core::resolve (F, I).chain.prevIndex;
                const core::NoteSnap* pv = snapshotNote (snap, pvIndex);
                if (pv == nullptr || ! (pv->s < F.s))
                    break;
                first = pvIndex;
                Uf = pv->s + mD * Lb;
            }
            std::int32_t cur = first;
            for (int g = 0; g < 256; ++g)
            {
                const core::NoteSnap& Cn = notes[static_cast<std::size_t> (cur)];
                const std::int32_t nxIndex = core::resolve (Cn, I).chain.nextIndex;
                const core::NoteSnap* nx = snapshotNote (snap, nxIndex);
                if (nx == nullptr || ! (nx->s > Cn.s) || nx->s + mD * Lb > u)
                    break;
                cur = nxIndex;
            }
            if (cur != i)
                continue;

            Voice* v = allocVoice();
            if (v == nullptr)
                return;
            const int slot = static_cast<int> (v - voicePool.get());
            v->initClip (ctx, source, snap, snapEpoch, first, Uf, toInt64 (mD), C.kOf (Uf), ++serial);
            if (remaining > 0)
            {
                std::int64_t ticks = v->chaseBegin (kNow);
                if (remaining - ticks > 0)
                    ticks += v->chaseRun (kNow, remaining - ticks);
                remaining -= ticks;
                st.chaseTicks += ticks;
                if (v->chaseCaughtUp (kNow))
                {   // the whole chase fitted: exactly the JS chase (kNow), ffChain (kNow), addVoice (v, kNow)
                    v->chaseEnd (kNow);
                    v->ffChain (kNow);
                    addVoice (slot, kNow);
                    continue;
                }
            }
            else
            {
                // no budget left at this moment: the chase begins on the control grid
                v->chasing = true;
                v->chaseK = kChaseNotBegun;
            }
            admitVoice (slot);
        }
    }

    void Engine::continueChases (std::int64_t kNow) noexcept
    {
        std::int64_t budget32 = kChaseTicksPer32;
        for (int i = 0; i < numVoices; ++i)
        {
            const int slot = order[static_cast<std::size_t> (i)];
            Voice& v = voiceAt (slot);
            if (! v.chasing || v.done)
                continue;
            if (v.chaseK == kChaseNotBegun)
            {
                if (budget32 <= 0)
                    continue;
                const std::int64_t t = v.chaseBegin (kNow);
                budget32 -= t;
                st.chaseTicks += t;
            }
            if (budget32 > 0)
            {
                const std::int64_t t = v.chaseRun (kNow, budget32);
                budget32 -= t;
                st.chaseTicks += t;
            }
            if (v.chaseCaughtUp (kNow))
                finishChase (slot, kNow);
        }
    }

    void Engine::finishChase (int slot, std::int64_t kNow) noexcept
    {
        // caught up with the current sample: the last part tick, the chain's hand-overs, then the voice starts here
        // with its 10 ms chase fade-in (it is already in the voice list, admitted at the chase moment)
        Voice& v = voiceAt (slot);
        v.chaseEnd (kNow);
        v.ffChain (kNow);
        v.startAt (kNow);
        plan();
    }
}
