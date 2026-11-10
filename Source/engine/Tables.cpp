#include "engine/Tables.h"

#include "engine/EngineMath.h"

#include <cmath>

namespace sl::engine::detail
{
    // sl-engine.js, line for line:
    //   for (let i = 0; i <= HANN_N + 1; i++) HANN[i] = 0.5 - 0.5 * Math.cos(TAU * Math.min(1, i / HANN_N));
    //   for (let i = 0; i <= FADE_N + 1; i++) FADE[i] = Math.sin(Math.PI / 2 * Math.min(1, i / FADE_N));
    TableData::TableData() noexcept
    {
        for (int i = 0; i <= kHannN + 1; ++i)
            hann[i] = static_cast<float> (0.5 - 0.5 * std::cos (kTau * jsMin (1.0, static_cast<double> (i) / kHannN)));
        for (int i = 0; i <= kFadeN + 1; ++i)
            fade[i] = static_cast<float> (std::sin (kPi / 2 * jsMin (1.0, static_cast<double> (i) / kFadeN)));
    }

    // Built during static initialisation, before main and long before any audio thread exists.
    const TableData kTableData;
}
