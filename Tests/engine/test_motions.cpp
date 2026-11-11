// The Motions and Scatter on the grain engines (PLUGIN-PLAN 4, M3 part G), as prototype-v4/tests/test-engine.js
// tests them. Positions are read from the audio of the ramp source (L / R encodes the read position, decodeAt) or
// from the display's head positions.
//  - Backward: Tape at the root reads 1:1, so its output correlates with the reversed source, r > 0.95 (Forward
//    |r| < 0.1). Voice and Cloud grains play forwards inside each grain, so for them Backward is checked as the
//    read position falling at 1x (within 0.003 over 0.5 s).
//  - Freeze: Voice and Cloud hold the read position (spread < 0.001 over 1.2 s); Tape plays Freeze as Forward.
//  - Bounce: Voice, Cloud and Tape stay inside a 0.2 .. 0.3 Region and turn 4 to 6 times in 2 s.
//  - Mirror: with 4 heads, Voice and Cloud heads move +1x -1x +1x -1x, each within 0.003.
//  - Scatter: 1/8 at 87 bpm (Tape as the prototype, and Voice and Cloud): one jump per grid line, each 0 <= error
//    < 32 samples after its line; two renders identical; Re-roll moves >= 80 % of the targets; the largest step
//    around a jump < 0.05.
// Group "engine".

#include "engine/GrainRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000;

    const char* nameOf (core::Engine e)
    {
        return e == core::Engine::tape ? "Tape" : e == core::Engine::voice ? "Voice" : "Cloud";
    }

    /** A clip note played from beat 0 at bpm, loop off (the JS tests' mkSnap ([N]) and setTransport). */
    grains::Play clipPlay (std::unique_ptr<engine::SampleData> sample, const core::Inst& inst, const rig::RawNote& n, double bpm = 120)
    {
        grains::Play play (kSr, inst, std::move (sample));
        play.snapshot (rig::rawSnapshot ({ n }, 4, false));
        play.play (bpm, 0, false);
        return play;
    }
}

SL_TEST ("engine", motionBackward)
{
    section ("Backward: Tape correlates with the reversed source (r > 0.95, Forward |r| < 0.1); Voice and Cloud read backwards at 1x");

    const std::vector<float> nz = signals::smoothNoise (4, kSr, 9);
    const auto n = static_cast<std::int64_t> (nz.size());
    auto corrRev = [&] (core::Motion motion, int& bestLag) {
        rig::RawNote N = grains::rawNote ("b", 60, 0, 3, core::Engine::tape);
        N.motion = motion;
        N.start.pos = 0.5;
        grains::Play play = clipPlay (grains::mono (nz, kSr), grains::instWith (core::Engine::tape), N);
        play.render (static_cast<std::int64_t> (kSr));
        const std::int64_t k0 = 4800, len = 24000;
        double best = -1;
        bestLag = 0;
        std::vector<float> b (static_cast<std::size_t> (len));
        for (int lag = -6; lag <= 6; ++lag)
        {
            for (std::int64_t j = 0; j < len; ++j)
                b[static_cast<std::size_t> (j)] = nz[static_cast<std::size_t> (std::max<std::int64_t> (0, static_cast<std::int64_t> (std::round (static_cast<double> (n) / 2.0)) - k0 - j + lag))];
            const double r = grains::corr (play.L.data() + k0, b.data(), static_cast<std::size_t> (len));
            if (r > best)
            {
                best = r;
                bestLag = lag;
            }
        }
        return best;
    };
    int lagB = 0, lagF = 0;
    const double bw = corrRev (core::Motion::backward, lagB), fw = corrRev (core::Motion::forward, lagF);
    check (bw > 0.95, fmt ("Tape Backward correlates with the reversed source: r = %.4f at lag %d (> 0.95)", bw, lagB));
    check (std::abs (fw) < 0.1, fmt ("Tape Forward does not (the contrast): r = %.4f (|r| < 0.1)", fw));

    for (const core::Engine eng : { core::Engine::voice, core::Engine::cloud })
    {
        auto sample = grains::ramp (4, kSr);
        const double rn = static_cast<double> (sample->n);
        rig::RawNote N = grains::rawNote ("v", 60, 0, 3, eng);
        N.motion = core::Motion::backward;
        N.start.pos = 0.5;
        grains::Play play = clipPlay (std::move (sample), grains::instWith (eng), N);
        play.render (static_cast<std::int64_t> (kSr));
        const double d = play.decodeAt (36000, 200) - play.decodeAt (12000, 200), expected = -24000 / rn;
        check (std::abs (d - expected) < 0.003, fmt ("%s Backward: the read position falls %.5f in 0.5 s (%.5f +- 0.003: -1x)", nameOf (eng), d, expected));
    }
}

SL_TEST ("engine", motionFreeze)
{
    section ("Freeze: Voice and Cloud hold the read position (spread < 0.001); Tape plays Freeze as Forward");

    for (const core::Engine eng : { core::Engine::voice, core::Engine::cloud, core::Engine::tape })
    {
        auto sample = grains::ramp (4, kSr);
        const double rn = static_cast<double> (sample->n);
        auto spreadOf = [&] (core::Motion motion, double& moved) {
            core::Inst inst = grains::instWith (eng);
            inst.motion = motion;
            inst.grainCloud = 30;
            grains::Play play (kSr, inst, grains::ramp (4, kSr));
            engine::NoteOnOpts opts;
            opts.startPos = 0.5;
            play.e->noteOn (60, 1, opts);
            play.render (static_cast<std::int64_t> (1.5 * kSr));
            double lo = HUGE_VAL, hi = -HUGE_VAL;
            double first = 0, last = 0;
            for (const double t : { 0.2, 0.6, 1.0, 1.4 })
            {
                const double p = play.decodeAt (static_cast<std::int64_t> (std::round (t * kSr)), 200);
                lo = std::min (lo, p);
                hi = std::max (hi, p);
                if (t < 0.3)
                    first = p;
                last = p;
            }
            moved = last - first;
            return hi - lo;
        };
        double moved = 0, movedForward = 0;
        const double spread = spreadOf (core::Motion::freeze, moved);
        const double forward = spreadOf (core::Motion::forward, movedForward);
        if (eng == core::Engine::tape)
            check (std::abs (moved - 1.2 * kSr / rn) < 0.003, fmt ("Tape plays Freeze as Forward: it moves %.5f in 1.2 s (%.5f, 1x)", moved, 1.2 * kSr / rn));
        else
            check (spread < 0.001 && forward > 0.1,
                   fmt ("%s Freeze holds the read position: spread %.2e (< 0.001; Forward, the contrast, spreads %.4f)", nameOf (eng), spread, forward));
    }
}

SL_TEST ("engine", motionBounce)
{
    section ("Bounce: inside a 0.2 .. 0.3 Region, 4 to 6 turns in 2 s (Voice, Cloud, Tape)");

    for (const core::Engine eng : { core::Engine::voice, core::Engine::cloud, core::Engine::tape })
    {
        core::Inst inst = grains::instWith (eng);
        inst.motion = core::Motion::bounce;
        inst.region.start = 0.2;
        inst.region.end = 0.3;
        grains::Play play (kSr, inst, grains::ramp (4, kSr));
        play.e->noteOn (60, 1);
        play.render (static_cast<std::int64_t> (2.0 * kSr));
        std::vector<double> pos;
        for (double t = 0.05; t < 2.0; t += 0.01)
            pos.push_back (play.decodeAt (static_cast<std::int64_t> (std::round (t * kSr)), 60));
        int turns = 0, dir = 0;
        for (std::size_t i = 1; i < pos.size(); ++i)
        {
            const double dd = pos[i] - pos[i - 1];
            const int d = dd > 0 ? 1 : dd < 0 ? -1 : 0;
            if (d != 0 && dir != 0 && d != dir)
                ++turns;
            if (d != 0)
                dir = d;
        }
        const double lo = *std::min_element (pos.begin(), pos.end()), hi = *std::max_element (pos.begin(), pos.end());
        check (lo > 0.195 && hi < 0.305, fmt ("%s: Bounce stays inside the Region (%.4f .. %.4f; 0.2 .. 0.3)", nameOf (eng), lo, hi));
        check (turns >= 4 && turns <= 6, fmt ("%s: Bounce turns at each end (%d turns in 2 s; 4 .. 6)", nameOf (eng), turns));
    }
}

SL_TEST ("engine", motionMirror)
{
    section ("Mirror: 4 heads move +1x -1x +1x -1x (Voice, Cloud), each within 0.003");

    for (const core::Engine eng : { core::Engine::voice, core::Engine::cloud })
    {
        core::Inst inst = grains::instWith (eng);
        inst.motion = core::Motion::mirror;
        inst.heads = 4;
        inst.spread = 0.5;
        grains::Play play (kSr, inst, grains::ramp (4, kSr));
        const double rn = static_cast<double> (play.sample->n);
        play.e->noteOn (60, 1);
        play.render (static_cast<std::int64_t> (0.3 * kSr));
        const engine::DisplayFrame& f0 = play.display();
        double before[4] = { 0, 0, 0, 0 };
        const bool four0 = f0.numVoices == 1 && f0.voices[0].numHeads == 4;
        for (int i = 0; four0 && i < 4; ++i)
            before[i] = f0.voices[0].heads[i].pos;
        play.render (static_cast<std::int64_t> (0.1 * kSr));
        const engine::DisplayFrame& f1 = play.display();
        const bool four1 = f1.numVoices == 1 && f1.voices[0].numHeads == 4;
        bool ok = four0 && four1;
        std::string moves;
        const double want[4] = { 1, -1, 1, -1 };
        for (int i = 0; four1 && i < 4; ++i)
        {
            const double x = (f1.voices[0].heads[i].pos - before[i]) * rn / (0.1 * kSr);
            ok = ok && std::abs (x - want[i]) <= 0.003 && f1.voices[0].heads[i].dir == static_cast<int> (want[i]);
            moves += fmt ("%+.4fx ", x);
        }
        check (ok, fmt ("%s Mirror: heads 1 and 3 forward, 2 and 4 backward: %s(+1 -1 +1 -1, within 0.003)", nameOf (eng), moves.c_str()));
    }
}

SL_TEST ("engine", scatterOnTheGrid)
{
    section ("Scatter at 1/8, 87 bpm (Tape, Voice, Cloud): one jump per grid line 0 .. 32 samples after it, repeatable, Re-roll moves them, no click");

    const double bpm = 87, jb = 0.5;
    const auto beatSamples = [bpm] (double beat) { return beat * 60 / bpm * kSr; };
    std::vector<double> grid;
    for (int j = 1; j * jb < 7.9; ++j)
        grid.push_back (beatSamples (j * jb));
    for (const core::Engine eng : { core::Engine::tape, core::Engine::voice, core::Engine::cloud })
    {
        struct Jump
        {
            std::int64_t k;
            double to;
        };
        struct Run
        {
            std::vector<float> L;
            std::vector<Jump> jumps;
        };
        auto run = [&] (std::int32_t seed) {
            core::Inst inst = grains::instWith (eng);
            inst.seed = seed;
            rig::RawNote N = grains::rawNote ("sc", 60, 0, 8, eng);
            N.motion = core::Motion::scatter;
            N.jump = jb;
            grains::Play play = clipPlay (grains::ramp (6, kSr), inst, N, bpm);
            std::vector<double> pos;
            play.render (static_cast<std::int64_t> (std::round (beatSamples (7.9))), 32, [&] (std::int64_t) {
                const engine::DisplayFrame& f = play.display();
                pos.push_back (f.numVoices > 0 && f.voices[0].numHeads > 0 ? static_cast<double> (f.voices[0].heads[0].pos) : std::nan (""));
            });
            Run r;
            r.L = play.L;
            // a loop wrap moves the head back by the whole Region (G.Lp / n, the whole file here: 1): not a jump
            for (std::size_t m = 1; m < pos.size(); ++m)
            {
                const double d = pos[m] - pos[m - 1];
                if (std::abs (d) > 0.005 && std::abs (d + 1) > 0.01)
                    r.jumps.push_back ({ static_cast<std::int64_t> (32 * m), pos[m] });
            }
            return r;
        };
        const Run a = run (1), b = run (1), c = run (2);
        std::vector<double> errs;
        for (const Jump& J : a.jumps)
        {
            double best = HUGE_VAL;
            for (const double g : grid)
            {
                const double d = static_cast<double> (J.k) - g;
                if (std::abs (d) < std::abs (best))
                    best = d;
            }
            errs.push_back (best);
        }
        const grains::Stats es = grains::stats (errs);
        int moved = 0;
        for (std::size_t i = 0; i < a.jumps.size(); ++i)
            if (i < c.jumps.size() && std::abs (c.jumps[i].to - a.jumps[i].to) > 0.01)
                ++moved;
        double step = 0;
        for (const Jump& J : a.jumps)
            step = std::max (step, rig::maxStep (a.L, J.k - 64, J.k + 400));
        const char* name = nameOf (eng);
        check (a.jumps.size() == grid.size() && grid.size() == 15,
               fmt ("%s: one jump per grid line (%zu jumps, %zu grid lines of 1/8 at 87 bpm)", name, a.jumps.size(), grid.size()));
        check (es.n > 0 && es.min >= 0 && es.max < 32, fmt ("%s: the jumps land 0 .. 32 samples after their grid line (%.1f .. %.1f)", name, es.min, es.max));
        check (grains::largestDifference (a.L, b.L) == 0.0 && ! a.L.empty(), fmt ("%s: two renders are identical", name));
        check (moved >= static_cast<int> (a.jumps.size() * 8 / 10) && ! a.jumps.empty(),
               fmt ("%s: Re-roll (seed + 1) moves %d of %zu jump targets (>= 80 %%)", name, moved, a.jumps.size()));
        check (step < 0.05, fmt ("%s: the largest step around a jump %.4f (< 0.05)", name, step));
    }
}
