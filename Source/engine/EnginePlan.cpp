// sl-engine.js Engine.plan and measure (PLUGIN-PLAN 1.8, D3): the budget planner, ported whole. Every
// filter (...).sort (...) of the JS is a fixed array sorted with rt::stableInsertionSort and the JS comparator;
// the wait list holds (voice, head) pairs, so it is sized kVoicePool x kMaxHeads = 512 (8.2 #32).
//
// Budget: at most B = max (16, floor (192 x 48000 / sr)) load units per sample. Over budget, the top
// (fractional first) heads of the oldest and quietest voices fade out over 20 ms; the newest note never thins.
// Steady heads are planned to floor (0.75 B), so the 20 ms fade tails fit under B. The units: Tape 2 per head
// (est; the measured peak is 1, or 2 during a jump or stutter crossfade), Voice and Cloud the active grains of a
// head (est min (8, ceil (len / P) + 1) for Voice and min (5, ceil (len / hop) + 1) for Cloud, set by Voice::tick;
// runGrains measures its most grains in the stretch), Lock its taps (est 2 max (nT, nA, nB while crossfading), set
// by Voice::tick; runLock measures N, or N + N' during a re-grab crossfade). The grain engines only change what the
// heads estimate and measure, not this planner.
//
// The plugin's one change: a voice still in a bounded chase (Voice::chasing) is silent and waiting. The planner
// passes it over (as if it were not in the list), so its heads get no grant; once it has caught up it starts like
// a new voice (its first tick grants its own heads) and the planner runs.

#include "engine/Engine.h"

#include "rt/StableSort.h"

#include <cmath>

namespace sl::engine
{
    namespace
    {
        inline bool planned (const Voice& v) noexcept { return ! v.done && ! v.chasing; }

        inline void silenceHead (Head& h) noexcept
        {
            h.on = false;
            h.active = false;
            h.grant = 0;
            h.g0L = h.g1L = h.g0R = h.g1R = 0;
            h.dL = h.dR = 0;
            h.clearGrains();
        }

        inline int quiet (const Voice& v) noexcept { return v.level * v.env < kQuietLevel ? 1 : 0; }

        inline int bySerial (std::uint64_t a, std::uint64_t b) noexcept { return a < b ? -1 : a > b ? 1 : 0; }
    }

    void Engine::plan() noexcept
    {
        const int B = budget;
        const double soft = std::floor (B * kSoftBudget);
        Voice* newest = nullptr;
        for (int i = 0; i < numVoices; ++i)
        {
            Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (planned (v) && ! v.released && (newest == nullptr || v.serial > newest->serial))
                newest = &v;
        }
        if (newest == nullptr)
            for (int i = 0; i < numVoices; ++i)
            {
                Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                if (planned (v) && (newest == nullptr || v.serial > newest->serial))
                    newest = &v;
            }

        int steady = 0, fading = 0, thinned = 0;
        for (int i = 0; i < numVoices; ++i)
        {
            const Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (! planned (v))
                continue;
            for (const Head& h : v.heads)
            {
                if (h.want && h.allowed)
                    steady += h.est;
                else if (h.on)
                    fading += h.grant;
                if (h.want && ! h.allowed)
                    ++thinned;
            }
        }

        auto listPlanned = [this] (bool skipNewest, const Voice* nw) {
            int n = 0;
            for (int i = 0; i < numVoices; ++i)
            {
                const std::uint8_t slot = order[static_cast<std::size_t> (i)];
                const Voice& v = voiceAt (slot);
                if (planned (v) && ! (skipNewest && &v == nw))
                    scratch[static_cast<std::size_t> (n++)] = slot;
            }
            return n;
        };

        if (thinned > 0)
        {   // room again: bring thinned heads back, newest voices first (with hysteresis)
            const int n = listPlanned (false, nullptr);
            rt::stableInsertionSortCmp (scratch.begin(), scratch.begin() + n, [this] (std::uint8_t a, std::uint8_t b) {
                return bySerial (voiceAt (b).serial, voiceAt (a).serial);   // (a, b) => b.serial - a.serial
            });
            for (int q = 0; q < n; ++q)
                for (Head& h : voiceAt (scratch[static_cast<std::size_t> (q)]).heads)
                    if (h.want && ! h.allowed && steady + h.est <= soft * 0.9)
                    {
                        h.allowed = true;
                        steady += h.est;
                        if (h.on)
                            fading -= h.grant;
                    }
        }

        if (steady > soft)
        {
            // released voices first, then silent ones (Volume lane or envelope at -60 dB or less), then the oldest
            const int n = listPlanned (true, newest);
            rt::stableInsertionSortCmp (scratch.begin(), scratch.begin() + n, [this] (std::uint8_t ia, std::uint8_t ib) {
                const Voice& a = voiceAt (ia);
                const Voice& b = voiceAt (ib);
                const int r = (b.released ? 1 : 0) - (a.released ? 1 : 0);
                if (r != 0)
                    return r;
                const int qd = quiet (b) - quiet (a);
                if (qd != 0)
                    return qd;
                return bySerial (a.serial, b.serial);
            });
            // a head that has not sounded yet (its voice starts at this very sample, e.g. the other notes of a chord)
            // goes silent at once: it has no tail to fade
            auto thin = [&steady, &fading] (Head& h, const Voice& v) {
                h.allowed = false;
                steady -= h.est;
                if (! h.on)
                    return;
                if (v.vt == 0)
                    silenceHead (h);
                else
                    fading += h.grant;
            };
            bool stop = false;
            for (int q = 0; q < n && ! stop; ++q)
            {
                Voice& v = voiceAt (scratch[static_cast<std::size_t> (q)]);
                for (int i = kMaxHeads - 1; i >= 1; --i)
                {
                    if (steady <= soft)
                    {
                        stop = true;
                        break;
                    }
                    Head& h = v.heads[static_cast<std::size_t> (i)];
                    if (h.want && h.allowed)
                        thin (h, v);
                }
            }
            for (int q = 0; q < n; ++q)
            {
                if (steady <= soft)
                    break;
                Voice& v = voiceAt (scratch[static_cast<std::size_t> (q)]);
                Head& h = v.heads[0];
                if (h.want && h.allowed)
                    thin (h, v);
            }
            if (steady > B && newest != nullptr)
                for (int i = kMaxHeads - 1; i >= 1 && steady > B; --i)
                {
                    Head& h = newest->heads[static_cast<std::size_t> (i)];
                    if (h.want && h.allowed)
                        thin (h, *newest);
                }
        }

        // Grants. A head that is already sounding keeps its share. A head that has not sounded yet (a new voice's
        // heads, a head the Heads lane adds, a thinned head coming back) waits until the 20 ms fade tails leave
        // room, newest voice first, so thinning always fades and never cuts. A waiting head comes in within 20 ms
        // with its own 20 ms fade-in; it is not thinned. Head 1 of a voice that has not started never waits.
        int load = fading;
        int w = 0;
        for (int i = 0; i < numVoices; ++i)
        {
            const std::uint8_t slot = order[static_cast<std::size_t> (i)];
            Voice& v = voiceAt (slot);
            if (! planned (v))
                continue;
            for (Head& h : v.heads)
            {
                if (! (h.want && h.allowed))
                    continue;
                if (h.on && h.grant > 0 && v.vt > 0)
                {
                    h.grant = h.est;
                    load += h.est;
                }
                else
                {
                    waitList[static_cast<std::size_t> (w++)] = static_cast<std::uint16_t> (slot * kMaxHeads + h.i);
                }
            }
        }
        if (w > 1)
            rt::stableInsertionSortCmp (waitList.begin(), waitList.begin() + w, [this] (std::uint16_t a, std::uint16_t b) {
                // (a, b) => b[1].serial - a[1].serial || a[0].i - b[0].i
                const int s = bySerial (voiceAt (b / kMaxHeads).serial, voiceAt (a / kMaxHeads).serial);
                if (s != 0)
                    return s;
                return (a % kMaxHeads) - (b % kMaxHeads);
            });
        for (int q = 0; q < w; ++q)
        {
            const std::uint16_t e = waitList[static_cast<std::size_t> (q)];
            Voice& v = voiceAt (e / kMaxHeads);
            Head& h = v.heads[static_cast<std::size_t> (e % kMaxHeads)];
            if (load + h.est <= B || (h.i == 0 && v.vt == 0))
            {
                h.grant = h.est;
                load += h.est;
            }
            else if (h.grant > 0 || h.on)
            {
                h.grant = 0;
                if (v.vt == 0)
                    silenceHead (h);
            }
        }

        if (load > B)
        {   // last resort (only reachable when new voices' first heads alone do not fit): cut the oldest tails
            const int n = listPlanned (false, nullptr);
            rt::stableInsertionSortCmp (scratch.begin(), scratch.begin() + n, [this] (std::uint8_t ia, std::uint8_t ib) {
                const Voice& a = voiceAt (ia);
                const Voice& b = voiceAt (ib);
                const int r = (b.released ? 1 : 0) - (a.released ? 1 : 0);
                if (r != 0)
                    return r;
                return bySerial (a.serial, b.serial);
            });
            bool stop = false;
            for (int q = 0; q < n && ! stop; ++q)
            {
                Voice& v = voiceAt (scratch[static_cast<std::size_t> (q)]);
                for (int i = kMaxHeads - 1; i >= 0; --i)
                {
                    if (load <= B)
                    {
                        stop = true;
                        break;
                    }
                    Head& h = v.heads[static_cast<std::size_t> (i)];
                    if (h.on && ! (h.want && h.allowed))
                    {
                        load -= h.grant;
                        silenceHead (h);
                        ++st.cuts;
                    }
                }
            }
        }

        int th = 0;
        for (int i = 0; i < numVoices; ++i)
        {
            const Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (planned (v))
                for (const Head& h : v.heads)
                    if (h.want && ! h.allowed)
                        ++th;
        }
        st.thinnedHeads = th;
    }

    // Measured load: the sum of each head's peak load inside the stretch just rendered (no planner action happens
    // inside a stretch), an upper bound of the true per-sample sum of active grains plus Lock taps.
    void Engine::measure() noexcept
    {
        int load = 0;
        for (int i = 0; i < numVoices; ++i)
            for (Head& h : voiceAt (order[static_cast<std::size_t> (i)]).heads)
            {
                load += h.segPeak;
                h.segPeak = 0;
            }
        st.grains = load;
        if (load > st.maxGrains)
            st.maxGrains = load;
    }
}
