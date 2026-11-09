#include "Snapshot.h"

#include "Rates.h"

#include <cmath>
#include <limits>

namespace sl::core
{
    namespace
    {
        // Math.max / Math.min: NaN when either is NaN, and +0 above -0.
        double jsMax (double a, double b) noexcept
        {
            if (std::isnan (a) || std::isnan (b))
                return std::numeric_limits<double>::quiet_NaN();
            if (a == 0.0 && b == 0.0)
                return std::signbit (a) && std::signbit (b) ? -0.0 : 0.0;
            return a > b ? a : b;
        }

        double jsMin (double a, double b) noexcept
        {
            if (std::isnan (a) || std::isnan (b))
                return std::numeric_limits<double>::quiet_NaN();
            if (a == 0.0 && b == 0.0)
                return std::signbit (a) || std::signbit (b) ? -0.0 : 0.0;
            return a < b ? a : b;
        }

        bool flag (std::int8_t marker, bool instValue) noexcept
        {
            return marker < 0 ? instValue : marker != 0;
        }
    }

    ResolvedNote resolve (const NoteSnap& x, const Inst& inst) noexcept
    {
        ResolvedNote r;
        r.engine = x.engine < 0 ? inst.engine : static_cast<Engine> (x.engine);
        r.lock = r.engine == Engine::cloud && flag (x.lock, inst.lock);
        r.motion = x.motion < 0 ? inst.motion : static_cast<Motion> (x.motion);
        r.tape = flag (x.tape, inst.tape);
        r.rev = flag (x.rev, inst.rev);
        r.jumpBeats = std::isnan (x.jumpBeats) ? rateBeats (inst.jump) : x.jumpBeats;

        int mode = static_cast<int> (inst.glideMode);
        if (mode < 0 || mode > 2)
            mode = static_cast<int> (GlideMode::legato);
        const GlideSnap& g = x.glides[mode];
        r.hasGlide = g.present;
        if (g.present)
        {
            r.glideD = g.d;
            r.glideLen = jsMin (jsMax (0.0, std::isnan (x.glide) ? inst.glideTime : x.glide), x.l * 0.95);
            r.glideCurve = g.curve;
            r.glideTo = g.toIndex;
        }
        r.chain = x.chains[mode];
        return r;
    }
}
