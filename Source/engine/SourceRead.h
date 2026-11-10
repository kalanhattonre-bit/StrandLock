#pragma once

// Reading the sample (sl-engine.js readAbs and the inline fast paths of runTape, runGrains and runLock).
//
// readAbs (S, x) is the 4-point Hermite read at a fractional frame x, both channels at the same position, with
// every index guarded. The fast paths skip the guards on frames 1 .. n-3, where they are not needed; they use
// hermite () below, the same expression in the same order, so on those frames they give readAbs's value bit for
// bit (test_seams checks it at random positions).
//
// S is a template parameter (a SampleData, or M7's 16-bit storage later): it needs L, R (R == L when mono),
// n and stereo.

#include <cmath>
#include <cstdint>

namespace sl::engine
{
    /** The 4-point Hermite of p[i - 1 .. i + 2] at fraction f, as sl-engine.js writes it. The four samples are read
        as doubles first, so every operation is a double operation, as in JavaScript. */
    inline double hermite (const float* p, std::int64_t i, double f) noexcept
    {
        const double a = p[i - 1], b = p[i], c = p[i + 1], e = p[i + 2];
        return (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
    }

    /** readAbs (S, x): l and r get the read at frame x (fractional, unbounded: beyond +-1e12 it reads frame 0). */
    template <class S>
    inline void readAbs (const S& s, double x, double& l, double& r) noexcept
    {
        if (! (x > -1e12 && x < 1e12))
            x = 0;
        const std::int64_t n = s.n;
        const double fl = std::floor (x);
        const double f = x - fl;
        const auto i = static_cast<std::int64_t> (fl);
        std::int64_t i0, i1, i2, i3;
        if (i >= 1 && i < n - 2)
        {
            i0 = i - 1;
            i1 = i;
            i2 = i + 1;
            i3 = i + 2;
        }
        else
        {
            const std::int64_t m = n - 1;
            i0 = i - 1 < 0 ? 0 : i - 1 > m ? m : i - 1;
            i1 = i < 0 ? 0 : i > m ? m : i;
            i2 = i + 1 < 0 ? 0 : i + 1 > m ? m : i + 1;
            i3 = i + 2 < 0 ? 0 : i + 2 > m ? m : i + 2;
        }
        const float* L = s.L;
        double a = L[i0], b = L[i1], c = L[i2], e = L[i3];
        l = (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
        if (s.stereo)
        {
            const float* R = s.R;
            a = R[i0];
            b = R[i1];
            c = R[i2];
            e = R[i3];
            r = (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
        }
        else
        {
            r = l;
        }
    }
}
