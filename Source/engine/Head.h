#pragma once

// sl-engine.js Head: one playhead of a voice (PLUGIN-PLAN 1.8, 1.9). Its position is raw + off frames from the
// Region start: raw moves at spd frames per sample, off is the Spread offset (slewed, moving dOff per sample).
// It holds its gains (slew-limited, per tick), its budget fields (want, allowed, est, grant, peaks), its grain
// slots (Voice and Cloud, M3) and its two Lock captures (M3). A plain data class, as in the JS: the voice and
// the engine's planner read and write its fields directly.

#include <array>
#include <cstdint>

namespace sl::engine
{
    class Head
    {
    public:
        /** The JS constructor: head i with every field at its start value. */
        void init (int index) noexcept
        {
            *this = Head();
            i = index;
        }

        /** clearGrains (): no grains, prime the scheduler again, end any Tape crossfade and Lock re-grab fade. */
        void clearGrains() noexcept
        {
            gc = 0;
            needPrime = true;
            xLeft = 0;
            xf = -1;
        }

        int i = 0;

        // Position and motion.
        double raw = 0, off = 0, dOff = 0, spd = 0, scat = 0;

        // Gains: the tick's start (g0) and end (g1) values per channel, and their per-sample steps.
        double g0L = 0, g0R = 0, g1L = 0, g1R = 0, dL = 0, dR = 0;
        double stopG = 1;     // At the end = Stop: fades to 0 over 20 ms outside the Region
        double level = 0;     // frac / sqrt (H)
        int dir = 0;          // the display arrow: 1, -1 or 0
        double spray = 0;     // frames
        bool smooth = true;   // Refresh Smooth this tick

        // The budget (Engine::plan).
        bool want = false, allowed = true, active = false, on = false, rendered = false;
        int est = 2, grant = 0, peak = 0, segPeak = 0;

        // Grain parameters (Voice, Cloud, Lock; M3).
        double P = 100, len = 100, step = 1, norm = 1, hop = 33, PR = 100, dth = 0.01;
        int nT = 4;

        // Grain slots (Float64Array (8) in the JS).
        std::array<double, 8> gT {}, gX {}, gS {}, gN {}, gA {};
        int gc = 0;
        double nextT = 0;
        std::int64_t gIdx = 0;   // a seed input (hash32 wraps it like JavaScript's `| 0`)
        bool needPrime = true;

        // Tape's 5 ms crossfade at a Scatter jump or a Refresh stutter: the old position keeps reading.
        double xRaw = 0;
        int xLeft = 0, xLen = 1;
        double grabRaw = 0;

        // Lock captures (M3) and the grab position.
        double grab = 0, capA = 0, capB = 0;
        int nA = 4, nB = 4;
        double th = 0, xf = -1;
        int periods = 0;

        // Seeded per voice (its chain's first note): speed drift -1..1 and the tiny start offset (seconds).
        double drift = 0, tiny = 0;
        bool regrab = false;
        double rho = 0;
    };
}
