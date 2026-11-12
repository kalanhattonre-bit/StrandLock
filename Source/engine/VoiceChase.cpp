// sl-engine.js Voice.chase, ffAdvance and ffChain, with the plugin's bounded cost (PLUGIN-PLAN 1.8, 8.2 #22).
//
// The JS chase replays every 32-sample tick from the chain's first note to the chase point in one go (up to 2^17
// ticks per voice). Here the far past is coarse: a tick that lies more than 4096 ticks before the chase point is
// replayed once per 16 (one tick, then ffAdvance (512); a head steered along a Path moves one tick of it, see
// ffAdvanceCoarse), and the last 4096 ticks are replayed one by one, exactly as the JS. A chase of at most 4096
// ticks (every fixture case) is therefore the JS chase, bit for bit. The engine may also spread one chase over
// several blocks (chaseBegin, chaseRun with a tick budget, chaseEnd); the voice is silent until chaseEnd. Block
// sizes never change the result: the steps depend on samples only.

#include "engine/Voice.h"

#include <limits>

namespace sl::engine
{
    std::int64_t Voice::chaseBegin (std::int64_t kNow) noexcept
    {
        std::int64_t kk = ctx->clock->kOf (U);
        const std::int64_t maxSpan = static_cast<std::int64_t> (kChaseMaxTicks) * kCtrl;
        if (kNow - kk > maxSpan)
            kk = kNow - maxSpan;   // very long notes: the far past is approximate
        tick (kk, true);
        chaseK = kk;
        chasing = true;
        return 1;
    }

    std::int64_t Voice::chaseRun (std::int64_t kNow, std::int64_t budget) noexcept
    {
        constexpr std::int64_t coarseStep = static_cast<std::int64_t> (kChaseCoarseEvery) * kCtrl;
        constexpr std::int64_t fineSpan = static_cast<std::int64_t> (kChaseCoarseAfter) * kCtrl;
        std::int64_t kk = chaseK, ticks = 0;
        while (kk + kCtrl <= kNow && ticks < budget)
        {
            // coarse while, after this step, more than 4096 ticks still lie before the chase point
            if (kNow - (kk + coarseStep) > fineSpan)
            {
                ffAdvanceCoarse (static_cast<double> (coarseStep));
                kk += coarseStep;
            }
            else
            {
                ffAdvance (kCtrl);
                kk += kCtrl;
            }
            ffChain (kk);
            tick (kk, true);
            ++ticks;
        }
        chaseK = kk;
        return ticks;
    }

    void Voice::chaseEnd (std::int64_t kNow) noexcept
    {
        ffAdvance (static_cast<double> (kNow - chaseK));
        chaseFinishState();
    }

    void Voice::chaseFinishState() noexcept
    {
        first = true;
        vt = 0;
        ph = 0;
        aStep = 1 / (kChaseFadeSeconds * ctx->sr);   // chase fade-in 10 ms
        for (Head& h : heads)
        {
            h.clearGrains();
            h.active = false;
        }
        chasing = false;
    }

    std::int64_t Voice::chase (std::int64_t kNow) noexcept
    {
        std::int64_t ticks = chaseBegin (kNow);
        ticks += chaseRun (kNow, std::numeric_limits<std::int64_t>::max());
        chaseEnd (kNow);
        return ticks;
    }

    void Voice::ffAdvance (double n) noexcept
    {
        for (Head& h : heads)
        {
            h.raw += h.spd * n;
            h.off += h.dOff * n;
        }
    }

    void Voice::ffAdvanceCoarse (double n) noexcept
    {
        // A Path head's speed is (target - raw) / 32: one tick at it lands on the target. 512 samples at it would
        // overshoot by 15 times the distance, so each coarse step multiplied the error by -15 and the position ran
        // to infinity and NaN far into a long Path note (M3 review R3-GP-1). The head now moves one tick and sits
        // on that tick's target until the next coarse tick steers it again. The last 4096 ticks (fine, as the JS)
        // then land it where the JS chase does: steering forgets where the head came from.
        const double steps = pathSteer ? static_cast<double> (kCtrl) : n;
        for (Head& h : heads)
        {
            h.raw += h.spd * steps;
            h.off += h.dOff * n;
        }
    }

    void Voice::ffChain (std::int64_t kk) noexcept
    {
        const Clock& C = *ctx->clock;
        const double u = C.uAt (kk);
        for (int guard = 0; guard < 64; ++guard)
        {
            const core::NoteSnap* nx = chainNext();
            if (nx == nullptr || ! (nx->s > note->s))
                return;
            const double Un = nx->s + static_cast<double> (m) * C.Lb;
            if (Un > u)
                return;
            switchTo (nx, Un);
        }
    }
}
