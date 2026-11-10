#pragma once

// sl-engine.js Biquad: the RBJ cookbook peak and low-pass, transposed direct form II, on a voice's double
// buffer. The coefficients are recomputed only when the settings' key changes. run () guards against a state
// that blew up (NaN or beyond 1e9): it resets the state, zeroes the bad samples and says so, and the voice
// counts it (Stats::filterResets, PLUGIN-PLAN 1.8), so a broken voice never passes as silence unnoticed.

#include "engine/EngineMath.h"

#include <cmath>
#include <limits>

namespace sl::engine
{
    class Biquad
    {
    public:
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double key = std::numeric_limits<double>::quiet_NaN();

        /** A new filter (the JS constructor): unity, no state, no key. */
        void init() noexcept { *this = Biquad(); }

        void reset() noexcept
        {
            z1 = 0;
            z2 = 0;
        }

        void setPeak (double sr, double f, double q, double db) noexcept
        {
            const double k = f * 1e3 + q * 7 + db * 13.7;
            if (k == key)
                return;
            key = k;
            const double A = std::pow (10.0, db / 40), w = kTau * clampJs (f, 10, sr * 0.45) / sr, cs = std::cos (w), al = std::sin (w) / (2 * q);
            const double a0 = 1 + al / A;
            b0 = (1 + al * A) / a0;
            b1 = (-2 * cs) / a0;
            b2 = (1 - al * A) / a0;
            a1 = (-2 * cs) / a0;
            a2 = (1 - al / A) / a0;
        }

        void setLow (double sr, double f, double q) noexcept
        {
            const double k = f * 1e3 + q * 7 - 99;
            if (k == key)
                return;
            key = k;
            const double w = kTau * clampJs (f, 10, sr * 0.45) / sr, cs = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
            b0 = (1 - cs) / 2 / a0;
            b1 = (1 - cs) / a0;
            b2 = (1 - cs) / 2 / a0;
            a1 = (-2 * cs) / a0;
            a2 = (1 - al) / a0;
        }

        /** Filters buf[0 .. n) in place. True when the NaN guard reset the state. */
        bool run (double* buf, int n) noexcept
        {
            double s1 = z1, s2 = z2;
            const double c0 = b0, c1 = b1, c2 = b2, d1 = a1, d2 = a2;
            for (int j = 0; j < n; ++j)
            {
                const double x = buf[j], y = c0 * x + s1;
                s1 = c1 * x - d1 * y + s2;
                s2 = c2 * x - d2 * y;
                buf[j] = y;
            }
            bool wasReset = false;
            if (! (s1 > -1e9 && s1 < 1e9 && s2 > -1e9 && s2 < 1e9))
            {
                s1 = 0;
                s2 = 0;
                for (int j = 0; j < n; ++j)
                    if (! (buf[j] > -1e9 && buf[j] < 1e9))
                        buf[j] = 0;
                wasReset = true;
            }
            z1 = s1;
            z2 = s2;
            return wasReset;
        }
    };
}
