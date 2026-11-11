// Refresh, Skip and Spray as the grain engines use them (PLUGIN-PLAN 4, M3 part G), as prototype-v4/tests/
// test-engine.js tests them on Cloud (20 ms grains) and the ramp source (L / R encodes the read position):
//  - Refresh Free 100 ms at 4x: inside each period the read position moves < 0.002 (the grains read the last
//    grab while the head moves on); each refresh grabs where the head is now, 0.05 +- 0.01 further at 4x.
//  - Skip 0.5: 3 to 10 of the 13 refreshes grab; two renders are identical; Re-roll (seed + 1) changes them.
//  - Spray 0.25: the read position's spread around the head (median |deviation|) is more than 4 times the
//    unsprayed one plus 0.002, and at most 0.04; two renders identical; seed + 1 changes it.
// Group "engine".

#include "engine/GrainRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"

#include <cmath>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000;

    struct RefreshRun
    {
        std::vector<float> L;
        std::vector<double> mids, within;
    };

    /** A live C3 on Cloud (20 ms grains) at 4x with this Refresh, 1.6 s: the read position 35 ms and 95 ms into
        each 100 ms period from the note start. */
    RefreshRun refreshRun (core::RefreshMode mode, double skip, std::int32_t seed)
    {
        core::Inst inst = grains::instWith (core::Engine::cloud);
        inst.grainCloud = 20;
        inst.speed = 4;
        inst.refresh.mode = mode;
        inst.refresh.ms = 100;
        inst.refresh.skip = skip;
        inst.seed = seed;
        grains::Play play (kSr, inst, grains::ramp (8, kSr));
        play.e->noteOn (60, 1);
        play.render (static_cast<std::int64_t> (1.6 * kSr));
        RefreshRun r;
        for (int k = 1; k < 15; ++k)
        {
            const double a = play.decodeAt (static_cast<std::int64_t> (std::round ((k * 0.1 + 0.035) * kSr)), 24);
            const double b = play.decodeAt (static_cast<std::int64_t> (std::round ((k * 0.1 + 0.095) * kSr)), 24);
            r.mids.push_back (a);
            r.within.push_back (std::abs (b - a));
        }
        r.L = play.L;
        return r;
    }

    /** How many of the 13 period-to-period moves are a new grab (> 0.02). */
    int grabs (const RefreshRun& r)
    {
        int n = 0;
        for (std::size_t i = 1; i < r.mids.size(); ++i)
            n += r.mids[i] - r.mids[i - 1] > 0.02 ? 1 : 0;
        return n;
    }
}

SL_TEST ("engine", cloudRefreshHolds)
{
    section ("Cloud Refresh Free 100 ms at 4x: the grab holds inside each period and each refresh grabs where the head is now");

    const RefreshRun hold = refreshRun (core::RefreshMode::free, 0, 1);
    const RefreshRun smooth = refreshRun (core::RefreshMode::smooth, 0, 1);
    const grains::Stats wHold = grains::stats (hold.within), wSmooth = grains::stats (smooth.within);
    check (wHold.n == 14 && wHold.max < 0.002,
           fmt ("Refresh 100 ms: the read position moves at most %.5f inside a period (< 0.002; Smooth moves %.4f, the contrast)", wHold.max, wSmooth.med));
    check (wSmooth.med > 0.02, fmt ("negative control: with Refresh Smooth the read position moves %.4f inside a period (> 0.02)", wSmooth.med));
    std::vector<double> steps;
    for (std::size_t i = 1; i < hold.mids.size(); ++i)
        steps.push_back (hold.mids[i] - hold.mids[i - 1]);
    const grains::Stats s = grains::stats (steps);
    check (s.n == 13 && s.min > 0.04 && s.max < 0.06,
           fmt ("Refresh 100 ms: each refresh grabs %.4f .. %.4f further (0.05 +- 0.01 at 4x)", s.min, s.max));
}

SL_TEST ("engine", cloudRefreshSkip)
{
    section ("Cloud Refresh Skip 0.5: about half the refreshes grab; seeded (repeatable, Re-roll changes it)");

    const RefreshRun sk = refreshRun (core::RefreshMode::free, 0.5, 1);
    const RefreshRun sk2 = refreshRun (core::RefreshMode::free, 0.5, 1);
    const RefreshRun sk3 = refreshRun (core::RefreshMode::free, 0.5, 2);
    check (grabs (sk) >= 3 && grabs (sk) <= 10, fmt ("Skip 0.5: %d of 13 refreshes grab (3 .. 10)", grabs (sk)));
    check (grains::largestDifference (sk.L, sk2.L) == 0.0 && ! sk.L.empty(), "Skip: two renders are identical");
    check (grains::largestDifference (sk.L, sk3.L) > 0.0,
           fmt ("Skip: Re-roll (seed + 1) changes which refreshes are skipped (%d grab with seed 2)", grabs (sk3)));
}

SL_TEST ("engine", cloudSpray)
{
    section ("Cloud Spray 0.25: seeded scatter of each grain around the head (spray x min (1 s, Region / 2) = 0.031 of the file)");

    struct Dev
    {
        std::vector<float> L;
        grains::Stats s;
    };
    auto dev = [] (double spray, std::int32_t seed) {
        core::Inst inst = grains::instWith (core::Engine::cloud);
        inst.grainCloud = 20;
        inst.spray = spray;
        inst.seed = seed;
        grains::Play play (kSr, inst, grains::ramp (8, kSr));
        const double rn = static_cast<double> (play.sample->n);
        engine::NoteOnOpts opts;
        opts.startPos = 0.25;
        play.e->noteOn (60, 1, opts);
        play.render (static_cast<std::int64_t> (1.0 * kSr));
        std::vector<double> d;
        for (double t = 0.1; t < 0.95; t += 0.005)
        {
            const auto i = static_cast<std::int64_t> (std::round (t * kSr));
            d.push_back (std::abs (play.decodeAt (i, 4) - (0.25 + static_cast<double> (i) / rn)));
        }
        Dev r;
        r.L = play.L;
        r.s = grains::stats (d);
        return r;
    };
    const Dev a = dev (0, 1), b = dev (0.25, 1), b2 = dev (0.25, 1), c = dev (0.25, 2);
    check (b.s.med > 4 * a.s.med + 0.002 && b.s.max < 0.04,
           fmt ("Spray 0.25 scatters the read position: median |deviation| %.5f against %.5f unsprayed (> 4 x + 0.002), the most %.4f (< 0.04)",
                b.s.med, a.s.med, b.s.max));
    check (grains::largestDifference (b.L, b2.L) == 0.0 && ! b.L.empty(), "Spray: two renders are identical");
    check (grains::largestDifference (b.L, c.L) > 0.0, "Spray: seed + 1 changes it");
}
