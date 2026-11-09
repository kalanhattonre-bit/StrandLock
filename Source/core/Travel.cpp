#include "Travel.h"

#include <cmath>

namespace sl::core
{
    namespace
    {
        // Math.min (8, x): NaN stays NaN (std::fmin would give 8).
        double minEight (double x) noexcept { return std::isnan (x) ? x : (x < 8.0 ? x : 8.0); }
    }

    double travelSpeed (double v) noexcept
    {
        if (v >= 0.48 - kF32Eps && v <= 0.52 + kF32Eps)
            return 0.0;
        if (v > 0.52)
            return minEight (std::pow (2.0, ((v - 0.75) / 0.23) * 3.0));
        return -minEight (std::pow (2.0, ((0.25 - v) / 0.23) * 3.0));
    }

    double travelSpeedToV (double x) noexcept
    {
        if (x == 0.0 || ! std::isfinite (x))   // JavaScript's !x is also true for NaN
            return 0.5;

        double a = std::fabs (x);
        a = a < 0.125 ? 0.125 : a > 8.0 ? 8.0 : a;
        const double o = (std::log2 (a) / 3.0) * 0.23;
        if (x > 0.0)
        {
            const double v = 0.75 + o;
            return v < kVFwdMin ? kVFwdMin : v;      // Math.max (V_FWD_MIN, 0.75 + o)
        }
        const double v = 0.25 - o;
        return v > kVBackMax ? kVBackMax : v;        // Math.min (V_BACK_MAX, 0.25 - o)
    }
}
