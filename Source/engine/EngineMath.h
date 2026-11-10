#pragma once

// sl-engine.js's small number helpers, exactly as JavaScript evaluates them (PLUGIN-PLAN 1.6). The engine's
// own copies: the model keeps its own in model/Num.h.

#include <cmath>
#include <cstdint>

namespace sl::engine
{
    inline constexpr double kPi = 3.141592653589793;      // Math.PI
    inline constexpr double kTau = kPi * 2;               // TAU = Math.PI * 2
    inline constexpr double kSqrt2 = 1.4142135623730951;  // Math.SQRT2
    inline constexpr double kSqrt1_2 = 0.7071067811865476; // Math.SQRT1_2

    /** The JS clamp (v, a, b): v < a ? a : v > b ? b : v. NaN passes through (std::clamp's contract forbids it). */
    constexpr double clampJs (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

    /** The JS fin (x, d): x when it is a finite number, else d. */
    inline double fin (double x, double d) noexcept { return std::isfinite (x) ? x : d; }

    /** Math.max (a, b): NaN when either is NaN; +0 beats -0. */
    inline double jsMax (double a, double b) noexcept
    {
        if (std::isnan (a) || std::isnan (b))
            return std::nan ("");
        if (a == b)
            return std::signbit (a) ? b : a;
        return a > b ? a : b;
    }

    /** Math.min (a, b): NaN when either is NaN; -0 beats +0. */
    inline double jsMin (double a, double b) noexcept
    {
        if (std::isnan (a) || std::isnan (b))
            return std::nan ("");
        if (a == b)
            return std::signbit (a) ? a : b;
        return a < b ? a : b;
    }

    /** A double that holds a whole number (a floor, a ceil) as an int64, saturated far outside the range any
        sample counter reaches, and 0 for NaN. Where JS keeps such a number as a double, the C++ keeps an int64. */
    inline std::int64_t toInt64 (double x) noexcept
    {
        if (! (x == x))
            return 0;
        if (x >= 4.0e18)
            return static_cast<std::int64_t> (4.0e18);
        if (x <= -4.0e18)
            return -static_cast<std::int64_t> (4.0e18);
        return static_cast<std::int64_t> (x);
    }
}
