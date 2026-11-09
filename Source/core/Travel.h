#pragma once

// The Travel lane (BLUEPRINT 1.3): one copy for the model and the engine (PLUGIN-PLAN 1.2, 1.6 rule 3).
// Thresholds on lane values hold at Float32 precision, because the snapshot tables are Float32:
// Math.fround (0.48) = 0.4799999893 lies 1.1e-8 below the edge. kF32Eps (2e-8) is more than half a Float32
// step near 0.5 and less than a whole one, so a value drawn on an edge plays as the edge.

#include <bit>
#include <cstdint>

namespace sl::core
{
    inline constexpr double kF32Eps = 2e-8;

    namespace detail
    {
        /** The next Float32 value up (dir 1) or down (dir -1), by stepping the bit pattern as SLModel's
            f32Next does. */
        constexpr float f32Next (float f, int dir) noexcept
        {
            return std::bit_cast<float> (std::bit_cast<std::int32_t> (f) + dir);
        }

        constexpr double firstForwardOutsideBand() noexcept
        {
            float f = static_cast<float> (0.52 + kF32Eps);
            while (static_cast<double> (f) <= 0.52 + kF32Eps)
                f = f32Next (f, 1);
            return static_cast<double> (f);
        }

        constexpr double firstBackwardOutsideBand() noexcept
        {
            float f = static_cast<float> (0.48 - kF32Eps);
            while (static_cast<double> (f) >= 0.48 - kF32Eps)
                f = f32Next (f, -1);
            return static_cast<double> (f);
        }
    }

    /** The first Float32 values outside the dead band (they play +-1/8x within 4e-7): 0.5200000405 and
        0.4799999595. */
    inline constexpr double kVFwdMin = detail::firstForwardOutsideBand();
    inline constexpr double kVBackMax = detail::firstBackwardOutsideBand();

    /** Speed kind: lane value -> signed speed multiplier. 0.48 - 2e-8 .. 0.52 + 2e-8 (inclusive) is stopped;
        above, 2^(((v - 0.75) / 0.23) * 3) up to 8; below, minus 2^(((0.25 - v) / 0.23) * 3) down to -8. */
    double travelSpeed (double v) noexcept;

    /** The inverse of travelSpeed for |x| in 1/8 .. 8 (clamped); 0, NaN and the infinities give 0.5.
        +-1/8 land on kVFwdMin / kVBackMax, just outside the band, so they play. */
    double travelSpeedToV (double x) noexcept;

    /** The Refresh lane below 0.06 is Smooth (BLUEPRINT 1.2), at Float32 precision: v < 0.06 - 2e-8. */
    constexpr bool refreshSmooth (double v) noexcept { return v < 0.06 - kF32Eps; }
}
