#pragma once

// The engine's beat clock (sl-engine.js uAt, loopBeat, songBeat, kOf, effLen and the fields they read;
// PLUGIN-PLAN 1.2: "Engine private, int64 sample counters, double beats"). The Engine owns one and changes it in
// setTransport and setSnapshot; every voice reads it through its VoiceContext (Voice.h).
//
// Playing: the unwrapped song beat at sample k is anchorBeat + (k - anchorK) db. Stopped: a free clock runs from
// freeBeat0 at freeK0 (live Scatter and synced Refresh while Cubase is stopped).

#include "core/Snapshot.h"
#include "engine/EngineConfig.h"
#include "engine/EngineMath.h"

#include <cmath>
#include <cstdint>

namespace sl::engine
{
    struct Clock
    {
        bool playing = false;
        bool loop = true;
        double bpm = 120;
        double db = 2.0 / kDefaultSampleRate;   // beats per sample: bpm / 60 / sr
        double anchorBeat = 0;                  // unwrapped song beat at sample anchorK (playing)
        std::int64_t anchorK = 0;
        double freeBeat0 = 0;                   // the free-running beat clock while stopped
        std::int64_t freeK0 = 0;
        double Lb = 16;                         // the clip length in beats: max (1, bars) x max (1, beatsPerBar)
        bool tie = false;                       // F3 (M8): the snapshot's tieAcrossLoop (setSnapshot)

        /** F3: notes hold on and slide over the loop end while the clip loops with tieAcrossLoop on. */
        bool ties() const noexcept { return loop && tie; }

        /** uAt (k): the unwrapped song beat at sample k. */
        double uAt (std::int64_t k) const noexcept { return anchorBeat + static_cast<double> (k - anchorK) * db; }

        /** loopBeat (u): the beat inside the clip. C++ change (8.2 #4): a negative beat is never wrapped into the
            clip, so nothing plays before bar 1 (the prototype floor-wraps it with loop on). */
        double loopBeat (double u) const noexcept
        {
            if (! loop || u < 0)
                return u;
            return u - Lb * std::floor (u / Lb);
        }

        /** songBeat (k): the clip's beat while playing, the free clock's while stopped. */
        double songBeat (std::int64_t k) const noexcept
        {
            return playing ? loopBeat (uAt (k)) : freeBeat0 + static_cast<double> (k - freeK0) * db;
        }

        /** kOf (U): the first sample at or after unwrapped beat U (a pure function of the anchor, so block sizes
            never change it). */
        std::int64_t kOf (double U) const noexcept
        {
            return anchorK + toInt64 (std::ceil ((U - anchorBeat) / db - 1e-7));
        }

        /** effLen (X): the length a note plays; with loop on it is cut at the clip end (the prototype). F3 (M8): with
            the tie on, a note that starts inside the clip holds on over the loop end into the next pass for its whole
            length, at most one loop, so it has ended when its own next start comes round. */
        double effLen (const core::NoteSnap& X) const noexcept
        {
            if (tie && loop && X.s >= 0 && X.s < Lb)
                return jsMin (X.l, Lb);
            return loop ? jsMin (X.l, Lb - X.s) : X.l;
        }
    };
}
