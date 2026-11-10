#pragma once

// The Region a voice plays (sl-engine.js readReg, seamRho, grabPos, mapPos; PLUGIN-PLAN 1.9). Positions are
// frames from the Region start (a head's raw + off), unbounded.
//
// Loop: the period is exactly L, the Region length, so a held note or a beat loop never drifts against the
// transport. The seam crossfades X frames with material just outside the Region (equal-power fades with
// GrainLock's correlation correction 1 / sqrt (1 + 2 rho gi go)):
//  - post (preferred, when the file goes on after the Region end): every pass after the first starts with the
//    Region start fading in while the file's own continuation past the Region end fades out. The first pass
//    plays the true attack.
//  - pre (when only material before the Region start exists): every pass ends by fading from the Region's last
//    X frames into the X frames that lead into the Region start, landing exactly on frame 0.
//  - none (the Region is the whole file, or there is no room on either side): a plain wrap.
// Hold and Stop clamp to the Region; Bounce folds (mapPos).

#include "engine/SampleData.h"
#include "engine/SourceRead.h"

#include <cmath>
#include <cstdint>

namespace sl::engine
{
    /** The JS voice's `reg`: the Region in frames of the source, recomputed at every tick (Voice::tick). */
    struct RegionState
    {
        double rs = 0;          // the Region start frame (a whole number)
        double L = 64;          // the Region length in frames (a whole number, at least 64)
        double X = 0;           // the seam fade length in frames (may be fractional), 0 = a plain wrap
        double Lp = 64;         // the loop period (= L)
        double invX = 0;        // 1 / X, or 0
        bool loop = true;       // At the end = Loop (and not Bounce)
        bool fold = false;      // Bounce
        bool stop = false;      // At the end = Stop (and not Bounce)
        bool post = true;       // the seam fade reads after the Region end (true) or before its start (false)
        double rho = 0;         // the two seam sides' correlation, 0..1 (seamRho)

        // What rho was computed for (it is computed again only when one of these changes).
        double cRs = -1, cL = -1, cX = -1;
        bool cPost = false;
        const SampleData* cSrc = nullptr;
    };

    /** readReg (S, G, x): l and r get the Region read at x (frames from the Region start, unbounded). */
    void readReg (const SampleData& s, const RegionState& g, double x, double& l, double& r) noexcept;

    /** seamRho (S, G): the correlation of the two sides of the seam fade (both channels summed), 0..1. */
    double seamRho (const SampleData& s, const RegionState& g) noexcept;

    /** mapPos (G, x): where a head really is (0..L frames): Bounce folds, Loop wraps (as readReg), Hold and Stop
        clamp. */
    inline double mapPos (const RegionState& g, double x) noexcept
    {
        if (g.fold)
        {
            const double P2 = 2 * g.L;
            const double m = x - P2 * std::floor (x / P2);
            return m < g.L ? m : P2 - m;
        }
        if (g.loop)
        {
            const double v = x - g.L * std::floor (x / g.L);
            return v < g.L ? v : v - g.L;
        }
        return x < 0 ? 0 : x > g.L - 1 ? g.L - 1 : x;
    }

    /** grabPos (G, x): where a grain or a Lock capture reads from. In loop mode the head's own unwrapped position
        (reads that cross a pass boundary then go through readReg's seam fade); otherwise mapPos. */
    inline double grabPos (const RegionState& g, double x) noexcept
    {
        return g.loop ? x : mapPos (g, x);
    }

    /** The inline fast path of runTape, runGrains and runLock: the plain stretch of any loop pass (or of a Hold,
        Stop or folded Region), frames 1 .. n-3 of the source. Set up once per run (the JS consts before each
        run's loop); read () gives readReg's value bit for bit wherever it returns true, and the caller falls
        back to readReg wherever it returns false. */
    struct PlainStretch
    {
        const float* SL = nullptr;
        const float* SR = nullptr;
        bool mono = true;
        bool loop = true;
        double rs = 0, L = 64, plainEnd = 0, lo = 0, xLater = 0;

        PlainStretch (const SampleData& s, const RegionState& g) noexcept
            : SL (s.L), SR (s.R), mono (! s.stereo), loop (g.loop), rs (g.rs), L (g.L)
        {
            // plainEnd = Math.min(G.loop ? (G.post ? G.L : G.L - G.X) : G.L - 1, S.n - 3 - rs), _lo = rs >= 1 ? 0 : 1 - rs
            const double a = g.loop ? (g.post ? g.L : g.L - g.X) : g.L - 1;
            const double b = static_cast<double> (s.n) - 3 - rs;
            plainEnd = std::isnan (a) || std::isnan (b) ? std::nan ("") : (a < b ? a : b);
            lo = rs >= 1 ? 0 : 1 - rs;
            xLater = g.post ? g.X : 0;
        }

        /** True, with the read in a and b, when x lies on the plain stretch. */
        bool read (double x, double& a, double& b) const noexcept
        {
            double xv = x;
            if (loop && xv >= L)
            {
                xv -= L * std::floor (xv / L);
                if (xv < xLater)
                    xv = -1e9;
            }
            if (! (xv >= lo && xv < plainEnd))
                return false;
            const double xa = rs + xv;
            const auto i = static_cast<std::int64_t> (xa);   // xa | 0: xa >= 1 here, so a floor
            const double f = xa - static_cast<double> (i);
            a = hermite (SL, i, f);
            b = mono ? a : hermite (SR, i, f);
            return true;
        }
    };
}
