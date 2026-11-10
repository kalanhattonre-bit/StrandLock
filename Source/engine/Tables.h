#pragma once

// sl-engine.js's two tables (PLUGIN-PLAN 1.2): HANN, the Hann window, and FADE, the equal-power fade in. Both are
// Float32 arrays, as in the JS, and both are built once at static-init time (Tables.cpp), never on first use, so
// the audio thread never builds anything. hannTab and fadeIn read them with linear interpolation in double.
// engine/defs.json holds the prototype's tables (test_parity checks them).

#include "engine/EngineConfig.h"

namespace sl::engine
{
    namespace detail
    {
        struct TableData
        {
            float hann[kHannN + 2];   // HANN[i] = 0.5 - 0.5 cos (TAU min (1, i / HANN_N)), i = 0 .. HANN_N + 1
            float fade[kFadeN + 2];   // FADE[i] = sin (PI / 2 min (1, i / FADE_N)), i = 0 .. FADE_N + 1

            TableData() noexcept;
        };

        extern const TableData kTableData;
    }

    /** HANN: kHannN + 2 Float32 values. */
    inline const float* hannTable() noexcept { return detail::kTableData.hann; }

    /** FADE: kFadeN + 2 Float32 values. */
    inline const float* fadeTable() noexcept { return detail::kTableData.fade; }

    /** hannTab (x), x in 0..1: 0 at and outside the ends. */
    inline double hannTab (double x) noexcept
    {
        if (x <= 0 || x >= 1)
            return 0;
        const float* H = detail::kTableData.hann;
        const double f = x * kHannN;
        const int i = static_cast<int> (f);   // f | 0: f is in (0, HANN_N)
        return H[i] + (f - i) * (static_cast<double> (H[i + 1]) - H[i]);
    }

    /** fadeIn (w): the equal-power fade in, w clamped to 0..1. */
    inline double fadeIn (double w) noexcept
    {
        if (w <= 0)
            return 0;
        if (w >= 1)
            return 1;
        const float* F = detail::kTableData.fade;
        const double f = w * kFadeN;
        const int i = static_cast<int> (f);   // f | 0: f is in (0, FADE_N)
        return F[i] + (f - i) * (static_cast<double> (F[i + 1]) - F[i]);
    }
}
