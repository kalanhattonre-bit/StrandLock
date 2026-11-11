// Lock in tune (PLUGIN-PLAN 4, M3 part L: "Lock tuning"), as prototype-v4/tests/test-engine.js measures it
// ("Cloud + Lock: in tune from noise and from a chord"):
//  - from noise (the tests' noise, seed 5) and from a chord (C3 E3 G3), live keys C2, C3 and C4 on Cloud with the
//    Lock switch at 48 kHz, Refresh Smooth, Free 120 ms and Free 400 ms: f0 by periodF0 (Lock repeats exactly every
//    period, so the period estimator reads it) over the end of a 2 s render, within +-3 cents. The JS gates Smooth
//    and 120 ms; the M3 table adds 400 ms (in the prototype within 0.6 cents).
//  - harmonic lines from noise: the harmonicity (mean power on the lines k f, k = 1 .. 8, over the gaps) of Lock on
//    noise at C3 with each Refresh is more than 10 dB above plain Cloud's on the same noise (the JS gate: plain
//    Cloud has no lines, about 0 dB).
// Group "engine".

#include "engine/GrainRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000;

    struct Refresh
    {
        core::RefreshMode mode;
        double ms;
        const char* name;
    };
    const Refresh kRefreshes[] = { { core::RefreshMode::smooth, 120, "Smooth" },
                                   { core::RefreshMode::free, 120, "120 ms" },
                                   { core::RefreshMode::free, 400, "400 ms" } };

    /** The tests' noise (6 s, seed 5) and chord (C3 E3 G3, 6 s), made once. */
    const std::vector<float>& noiseSource()
    {
        static const std::vector<float> made = signals::noise (6, kSr, 5);
        return made;
    }
    const std::vector<float>& chordSource()
    {
        static const std::vector<float> made = signals::chord ({ 60, 64, 67 }, 6, kSr);
        return made;
    }

    /** A live key p for 2 s on Cloud (Lock on or off) with a Refresh, as the JS mkEngine + noteOn + render. */
    std::vector<float> renderKey (const std::vector<float>& src, int p, bool lock, const Refresh& refresh, double pitch = 0)
    {
        core::Inst inst = grains::instWith (core::Engine::cloud);
        inst.lock = lock;
        inst.refresh.mode = refresh.mode;
        inst.refresh.ms = refresh.ms;
        inst.pitch = pitch;
        grains::Play play (kSr, inst, grains::mono (src, kSr));
        play.e->noteOn (p, 1);
        play.render (static_cast<std::int64_t> (2.0 * kSr));
        return play.L;
    }
}

SL_TEST ("engine", lockTuning)
{
    section ("Lock tuning: from noise and from a chord, C2 / C3 / C4, Refresh Smooth / 120 ms / 400 ms, within +-3 cents (periodF0)");

    const int keys[] = { 48, 60, 72 };
    const char* names[] = { "C2", "C3", "C4" };
    int ok = 0, total = 0;
    double worst = 0;
    for (int s = 0; s < 2; ++s)
    {
        const std::vector<float>& src = s == 0 ? noiseSource() : chordSource();
        const char* srcName = s == 0 ? "noise" : "chord C3-E3-G3";
        for (int i = 0; i < 3; ++i)
            for (const Refresh& refresh : kRefreshes)
            {
                const std::vector<float> L = renderKey (src, keys[i], true, refresh);
                const double fExp = signals::midiHz (keys[i]);
                const double c = grains::cents (grains::periodF0 (L, kSr, fExp), fExp);
                ++total;
                ok += std::abs (c) <= 3.0 ? 1 : 0;
                worst = std::max (worst, std::abs (c));
                note (fmt ("Lock from %s, %s, Refresh %s: %+.4f cents", srcName, names[i], refresh.name, c));
            }
    }
    check (ok == total && total == 18, fmt ("Lock: all 18 within +-3 cents (%d of %d; the worst %.4f cents)", ok, total, worst));

    // A no-op is not a pass: Pitch +0.1 semitone (10 cents) on C3 from noise measures 10 cents off, outside the gate.
    const double c = grains::cents (grains::periodF0 (renderKey (noiseSource(), 60, true, kRefreshes[0], 0.1), kSr, signals::midiHz (60)),
                                    signals::midiHz (60));
    check (std::abs (c - 10) <= 3.0 && std::abs (c) > 3.0,
           fmt ("negative control: Lock C3 from noise with Pitch +0.1 st measures %+.3f cents (10 expected), outside +-3", c));
}

SL_TEST ("engine", lockHarmonicLines)
{
    section ("Lock makes a pitch from noise: harmonic lines more than 10 dB over the gaps, beyond plain Cloud's, with every Refresh");

    const double f = signals::midiHz (60);
    const double plain = grains::harmonicity (renderKey (noiseSource(), 60, false, kRefreshes[0]), kSr, f);
    double least = HUGE_VAL;
    std::string each;
    for (const Refresh& refresh : kRefreshes)
    {
        const double h = grains::harmonicity (renderKey (noiseSource(), 60, true, refresh), kSr, f);
        least = std::min (least, h);
        each += fmt ("%s%s %.1f dB", each.empty() ? "" : ", ", refresh.name, h);
    }
    check (least > plain + 10,
           fmt ("Lock on noise at C3: %s; plain Cloud %.1f dB (the gate: the worst Lock > plain Cloud + 10 dB)", each.c_str(), plain));
}
