#pragma once

// sl-model.js's small number helpers, exactly as JavaScript evaluates them. Internal to the model.

#include <cmath>

namespace sl::model
{
    /** SLModel's clamp (v, a, b): v < a ? a : v > b ? b : v (NaN passes through, unlike std::clamp's contract). */
    constexpr double clamp (double v, double a, double b) noexcept { return v < a ? a : v > b ? b : v; }

    /** SLModel's num (x, d): x when it is a finite number, else d. */
    inline double num (double x, double d) noexcept { return std::isfinite (x) ? x : d; }

    /** Math.max (a, b) / Math.min (a, b): NaN when either is NaN. */
    inline double jsMax (double a, double b) noexcept
    {
        if (std::isnan (a) || std::isnan (b))
            return std::nan ("");
        return a > b ? a : b;
    }

    inline double jsMin (double a, double b) noexcept
    {
        if (std::isnan (a) || std::isnan (b))
            return std::nan ("");
        return a < b ? a : b;
    }

    /** Number.isInteger (x). */
    inline bool isInteger (double x) noexcept { return std::isfinite (x) && std::floor (x) == x; }
}
