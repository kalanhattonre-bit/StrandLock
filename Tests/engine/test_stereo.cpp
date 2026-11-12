// Stereo (PLUGIN-PLAN 4, M3 "stereo", part B), as prototype-v4/tests/test-engine.js "stereo" tests it, in every
// engine (Tape, Voice, Cloud, and Lock: Cloud with the Lock switch on):
//  - a mono source with Spread 0 and 3 heads gives L == R bit for bit (and is not silent);
//  - a stereo source (C3 left, E3 right) keeps L and R apart: |corr (L, R)| < 0.5;
//  - a mono source (noise, so the heads carry different material) with Spread 0.6 pans the heads apart: corr < 0.9;
//  - the mono sum of 8 heads with Mirror and Spread 0.6 loses at most 4.5 dB against the mean of the L and R power
//    (heads panned apart must not cancel when Cubase sums to mono).
// The prototype's figures (node) are printed next to the C++ ones. Group "engine".

#include "engine/GrainRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000;

    struct Eng
    {
        const char* name;
        core::Engine engine;
        bool lock;
    };
    const Eng kEngines[] = { { "Tape", core::Engine::tape, false },
                             { "Voice", core::Engine::voice, false },
                             { "Cloud", core::Engine::cloud, false },
                             { "Lock", core::Engine::cloud, true } };

    /** A live C3 for secs on this engine, heads, Spread and Motion, from the sample. */
    grains::Play playC3 (const Eng& x, double heads, double spread, core::Motion motion, std::unique_ptr<engine::SampleData> sample, double secs)
    {
        core::Inst inst = grains::instWith (x.engine);
        inst.lock = x.lock;
        inst.heads = heads;
        inst.spread = spread;
        inst.motion = motion;
        grains::Play play (kSr, inst, std::move (sample));
        play.e->noteOn (60, 1);
        play.render (static_cast<std::int64_t> (std::llround (secs * kSr)));
        return play;
    }

    double power (const float* x, std::size_t n)
    {
        double s = 0;
        for (std::size_t i = 0; i < n; ++i)
            s += static_cast<double> (x[i]) * x[i];
        return s / static_cast<double> (n);
    }

    double rmsOf (const std::vector<float>& x) { return std::sqrt (power (x.data(), x.size())); }

    /** 10 log10 (mean (P (L), P (R)) / P ((L + R) / 2)) over [from, end): how much the mono sum loses. */
    double monoSumLossDb (const std::vector<float>& L, const std::vector<float>& R, std::size_t from)
    {
        const std::size_t n = L.size() - from;
        std::vector<float> m (n);
        for (std::size_t i = 0; i < n; ++i)
            m[i] = 0.5f * (L[from + i] + R[from + i]);
        const double side = 0.5 * (power (L.data() + from, n) + power (R.data() + from, n));
        return 10 * std::log10 (side / std::max (1e-30, power (m.data(), n)));
    }
}

SL_TEST ("engine", stereoMonoSourceSpreadZero)
{
    section ("stereo: a mono source (the C3 tone), Spread 0, 3 heads: L == R bit for bit in every engine");

    for (const Eng& x : kEngines)
    {
        grains::Play p = playC3 (x, 3, 0, core::Motion::forward, grains::mono (signals::tone (signals::midiHz (60), 3, kSr), kSr), 0.5);
        const double d = grains::largestDifference (p.L, p.R), r = rmsOf (p.L);
        const bool same = p.L == p.R;   // every sample identical (bit for bit)
        check (same && r > 0.01, fmt ("%s: L == R bit for bit (%s; largest |L - R| %g), rms %.4f > 0.01 (the prototype: identical)", x.name,
                                      same ? "yes" : "no", d, r));
    }
}

SL_TEST ("engine", stereoSourceStaysApart)
{
    section ("stereo: a stereo source (C3 left, E3 right), Spread 0, 3 heads: L and R stay apart, |corr (L, R)| < 0.5");

    const double proto[] = { 0.0033, 0.0035, 0.0033, 0.0071 };
    int i = 0;
    for (const Eng& x : kEngines)
    {
        auto sample = engine::makeSampleData (signals::tone (signals::midiHz (60), 3, kSr),
                                              signals::tone (signals::midiHz (64), 3, kSr, signals::ToneOpts { 0, 0, 9 }), kSr, "stereo");
        grains::Play p = playC3 (x, 3, 0, core::Motion::forward, std::move (sample), 1);
        const std::size_t from = 4800, n = p.L.size() - from;
        const double r = grains::corr (p.L.data() + from, p.R.data() + from, n);
        const double d = grains::largestDifference (p.L, p.R);
        check (std::abs (r) < 0.5 && d > 0.05, fmt ("%s: corr (L, R) %.4f (|r| < 0.5; the prototype %.4f), largest |L - R| %.3f", x.name, r,
                                                    proto[i], d));
        ++i;
    }
}

SL_TEST ("engine", stereoSpreadPansHeads)
{
    section ("stereo: a mono source (noise), Spread 0.6, 3 heads: the heads pan apart, corr (L, R) < 0.9");

    const double proto[] = { 0.8413, 0.8388, 0.8413, 0.8261 };
    int i = 0;
    for (const Eng& x : kEngines)
    {
        grains::Play p = playC3 (x, 3, 0.6, core::Motion::forward, grains::mono (signals::noise (3, kSr, 31), kSr), 0.5);
        const std::size_t from = 4800, n = p.L.size() - from;
        const double r = grains::corr (p.L.data() + from, p.R.data() + from, n);
        const double d = grains::largestDifference (p.L, p.R);
        check (r < 0.9 && d > 0.05, fmt ("%s: corr (L, R) %.4f (< 0.9; the prototype %.4f), largest |L - R| %.3f (> 0.05)", x.name, r, proto[i], d));
        ++i;
    }
}

SL_TEST ("engine", stereoMirrorMonoSum)
{
    section ("stereo: the mono sum of 8 heads with Mirror and Spread 0.6 (noise) loses at most 4.5 dB against the mean of L and R power");

    const double proto[] = { 0.246, 0.227, 0.250, 0.235 };
    int i = 0;
    for (const Eng& x : kEngines)
    {
        grains::Play p = playC3 (x, 8, 0.6, core::Motion::mirror, grains::mono (signals::noise (3, kSr, 31), kSr), 1);
        const double loss = monoSumLossDb (p.L, p.R, 9600);
        const double r = grains::corr (p.L.data() + 9600, p.R.data() + 9600, p.L.size() - 9600);
        check (loss <= 4.5 && rmsOf (p.L) > 0.01,
               fmt ("%s: the mono sum loses %.3f dB (<= 4.5; the prototype %.3f), corr (L, R) %.4f", x.name, loss, proto[i], r));
        ++i;
    }
    // A no-op is not a pass: the measure sees a cancellation (R = -L loses everything; uncorrelated L and R lose 3 dB).
    const std::vector<float> a = signals::noise (1, kSr, 5), b = signals::noise (1, kSr, 6);
    std::vector<float> minus (a.size());
    for (std::size_t k = 0; k < a.size(); ++k)
        minus[k] = -a[k];
    const double cancel = monoSumLossDb (a, minus, 0), apart = monoSumLossDb (a, b, 0);
    check (cancel > 40 && std::abs (apart - 3.01) < 0.2,
           fmt ("negative control: R = -L loses %.0f dB (> 40), two different noises %.2f dB (about 3)", cancel, apart));
}
