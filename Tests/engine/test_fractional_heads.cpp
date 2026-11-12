// Fractional heads in every engine (PLUGIN-PLAN 1.8, 4 M3 "fractional heads", part B), as prototype-v4/tests/
// test-engine.js "fractional heads" tests it: a 6-beat C3 note whose Heads lane steps 2 -> 2.5 -> 3 (a third and
// then a fourth head fading in), the 6-partial C3 tone, Spread 0.25. The largest sample-to-sample step around each
// change (0.98-1.35 s and 1.98-2.35 s) over the largest steady step (0.4-0.95 s and 2.4-2.95 s) stays below 1.5 in
// Tape, Voice and Cloud (the M3 row), and in Lock too. Negative control: one sample nudged by twice the steady
// step in a change window reads above 1.5, so the measure sees a click. Group "engine".

#include "engine/GrainRig.h"
#include "engine/VoiceRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000;

    std::int64_t at (double secs) { return static_cast<std::int64_t> (std::llround (secs * kSr)); }

    struct Steps
    {
        double steady = 0, change = 0;
    };

    Steps steps (const std::vector<float>& x)
    {
        Steps s;
        s.steady = std::max (rig::maxStep (x, at (0.4), at (0.95)), rig::maxStep (x, at (2.4), at (2.95)));
        s.change = std::max (rig::maxStep (x, at (0.98), at (1.35)), rig::maxStep (x, at (1.98), at (2.35)));
        return s;
    }
}

SL_TEST ("engine", fractionalHeadsNoClick)
{
    section ("fractional heads: Heads 2 -> 2.5 -> 3 on a C3 note in Tape, Voice, Cloud and Lock: largest step at the changes / steady "
             "largest step < 1.5");

    struct Eng
    {
        const char* name;
        core::Engine engine;
        bool lock;
        double proto;   // the prototype's ratio (node)
    };
    const Eng engines[] = { { "Tape", core::Engine::tape, false, 0.8606 },
                            { "Voice", core::Engine::voice, false, 0.9998 },
                            { "Cloud", core::Engine::cloud, false, 0.7635 },
                            { "Lock", core::Engine::cloud, true, 0.9672 } };
    const std::vector<float> src = signals::tone (signals::midiHz (60), 4, kSr, signals::ToneOpts { 6, 0, 0 });
    std::vector<float> lastRender;
    Steps lastSteps;
    for (const Eng& x : engines)
    {
        auto vOf = [] (double H) { return 0.5 + (H - 1) / 14; };
        rig::RawNote n = grains::rawNote ("fh", 60, 0, 6, x.engine);
        n.lock = x.lock;
        rig::LaneSpec& heads = n.lanes[static_cast<std::size_t> (core::LaneId::heads)];
        heads.set = true;
        heads.fn = [vOf] (double u) { return u < 1.0 / 3 ? vOf (2) : u < 2.0 / 3 ? vOf (2.5) : vOf (3); };
        core::Inst inst = grains::instWith (x.engine);
        inst.lock = x.lock;
        inst.spread = 0.25;
        grains::Play play (kSr, inst, grains::mono (src, kSr));
        play.snapshot (rig::rawSnapshot ({ n }, 4, false));
        play.play (120, 0, false);
        play.render (at (2.95));
        const Steps s = steps (play.L);
        const double r = s.change / s.steady;
        check (r < 1.5 && s.steady > 0.01, fmt ("%s: %.4f (< 1.5; the prototype %.4f): at the changes %.5f, steady %.5f", x.name, r, x.proto, s.change,
                                                s.steady));
        lastRender = play.L;
        lastSteps = s;
    }
    // A no-op is not a pass: a click of twice the steady step inside a change window is seen.
    std::vector<float> clicked = lastRender;
    const auto c = static_cast<std::size_t> (at (1.1));
    clicked[c] = static_cast<float> (clicked[c] + 2.0 * lastSteps.steady);
    const Steps sc = steps (clicked);
    check (sc.change / sc.steady > 1.5, fmt ("negative control: one sample nudged by twice the steady step reads %.2f (> 1.5)", sc.change / sc.steady));
}
