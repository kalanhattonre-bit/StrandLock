#pragma once

// Reading the sample (sl-engine.js readAbs and the inline fast paths of runTape, runGrains and runLock).
//
// readAbs (S, x) is the 4-point Hermite read at a fractional frame x, both channels at the same position, with
// every index guarded. The fast paths skip the guards on frames 1 .. n-3, where they are not needed; they use
// hermite () below, the same expression in the same order, so on those frames they give readAbs's value bit for
// bit (test_seams checks it at random positions).
//
// Two storage formats (SampleData.h): floats, and the 16-bit samples of a long file (M7). A 16-bit sample s is
// read as the double s / 32768 (sampleValue below), the exact value a float holding s / 32768 has, so every read
// of a 16-bit sound gives the same bits as the same read of a float sound with those values (test_long_file).
// The format is chosen once per call (readAbs) or once per run (PlainStretch, Region.h), never per sample tap.

#include "engine/SampleData.h"

#include <cmath>
#include <cstdint>

namespace sl::engine
{
    /** One stored sample as the double the engine computes with: a float as it is, a 16-bit sample times 2^-15. */
    inline double sampleValue (float v) noexcept { return v; }
    inline double sampleValue (std::int16_t v) noexcept { return static_cast<double> (v) * kInt16Scale; }

    /** The 4-point Hermite of p[i - 1 .. i + 2] at fraction f, as sl-engine.js writes it. The four samples are read
        as doubles first, so every operation is a double operation, as in JavaScript. */
    template <class T>
    inline double hermite (const T* p, std::int64_t i, double f) noexcept
    {
        const double a = sampleValue (p[i - 1]), b = sampleValue (p[i]), c = sampleValue (p[i + 1]), e = sampleValue (p[i + 2]);
        return (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
    }

    /** readAbs over one storage format's two channel pointers (R == L when mono). */
    template <class T>
    inline void readAbsOf (const T* L, const T* R, std::int64_t n, bool stereo, double x, double& l, double& r) noexcept
    {
        if (! (x > -1e12 && x < 1e12))
            x = 0;
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
        double a = sampleValue (L[i0]), b = sampleValue (L[i1]), c = sampleValue (L[i2]), e = sampleValue (L[i3]);
        l = (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
        if (stereo)
        {
            a = sampleValue (R[i0]);
            b = sampleValue (R[i1]);
            c = sampleValue (R[i2]);
            e = sampleValue (R[i3]);
            r = (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
        }
        else
        {
            r = l;
        }
    }

    /** readAbs (S, x): l and r get the read at frame x (fractional, unbounded: beyond +-1e12 it reads frame 0). */
    inline void readAbs (const SampleData& s, double x, double& l, double& r) noexcept
    {
        if (s.format == SampleFormat::i16)
            readAbsOf (s.L16, s.R16, s.n, s.stereo, x, l, r);
        else
            readAbsOf (s.L, s.R, s.n, s.stereo, x, l, r);
    }
}
