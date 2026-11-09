#include "Music.h"

#include "JsCompat.h"

#include <cmath>

namespace sl::core
{
    int findScale (std::string_view name) noexcept
    {
        for (int i = 0; i < kNumScales; ++i)
            if (name == kScales[i].name)
                return i;
        return -1;
    }

    std::string noteName (double p)
    {
        const double r = jsRound (p);
        if (! (std::fabs (r) < 1e15))
            return {};

        // NAMES[((r % 12) + 12) % 12] + (floor (r / 12) - 2), in doubles as JavaScript computes it.
        const double pitchClass = std::fmod (std::fmod (r, 12.0) + 12.0, 12.0);
        const double octave = std::floor (r / 12.0) - 2.0;
        return std::string (kNoteNames[static_cast<int> (pitchClass)]) + std::to_string (static_cast<long long> (octave));
    }

    bool inScale (double p, int key, int scale) noexcept
    {
        const ScaleInfo& s = kScales[scale >= 0 && scale < kNumScales ? scale : 0];
        const double r = jsRound (p) - static_cast<double> (key);
        if (! std::isfinite (r))
            return false;

        const double pitchClass = std::fmod (std::fmod (r, 12.0) + 12.0, 12.0);
        for (int i = 0; i < s.numSteps; ++i)
            if (static_cast<double> (s.steps[i]) == pitchClass)
                return true;
        return false;
    }

    double snapScale (double p, int key, int scale) noexcept
    {
        if (inScale (p, key, scale))
            return p;
        for (int d = 1; d < 7; ++d)
        {
            if (inScale (p - d, key, scale))
                return p - d;
            if (inScale (p + d, key, scale))
                return p + d;
        }
        return p;
    }
}
