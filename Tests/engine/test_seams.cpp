// Reading the sample and the Region (PLUGIN-PLAN 1.2, 1.9; M2's "loop seam" row). readAbs against the inline fast
// path at random positions in every kind of Region (bit for bit), and the loop seam on one Tape voice driven
// directly (the Engine comes in M2 part 2): with a 10 ms crossfade the largest step in the seam zones is less than
// 1.5 times the largest steady step, and a ramp source's 0.85 -> 0.05 seam has no step of 0.01 or more. The
// prototype's own numbers for the same per-voice renders (scratch script e1/ref.js, the JS Voice driven the same
// way) are compared too, so a port error shows even where the plan's criterion still holds. Group "engine".

#include "engine/VoiceRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Region.h"
#include "engine/SourceRead.h"
#include "engine/Voice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    bool sameBits (double a, double b) { return a == b || (std::isnan (a) && std::isnan (b)); }

    engine::RegionState region (double rs, double L, double X, bool post, bool loop, bool fold, double rho)
    {
        engine::RegionState g;
        g.rs = rs;
        g.L = L;
        g.X = X;
        g.Lp = L;
        g.invX = X > 0 ? 1 / X : 0;
        g.post = post;
        g.loop = loop;
        g.fold = fold;
        g.stop = false;
        g.rho = rho;
        return g;
    }

    struct FastTally
    {
        int fast = 0, slow = 0, mismatches = 0;
        std::string first;
    };

    // Every x through what runTape does: fold (Bounce), then the fast path where it applies, else readReg; the
    // fast result must equal readReg's bit for bit.
    void probe (const engine::SampleData& S, const engine::RegionState& G, double x0, FastTally& t)
    {
        const double x = G.fold ? engine::mapPos (G, x0) : x0;
        const engine::PlainStretch plain (S, G);
        double a = 0, b = 0, ra = 0, rb = 0;
        engine::readReg (S, G, x, ra, rb);
        if (plain.read (x, a, b))
        {
            ++t.fast;
            if (! sameBits (a, ra) || ! sameBits (b, rb))
            {
                if (t.mismatches == 0)
                    t.first = fmt ("x %.17g: fast %.17g %.17g, readReg %.17g %.17g", x, a, b, ra, rb);
                ++t.mismatches;
            }
        }
        else
        {
            ++t.slow;
        }
    }

    double seamRatioOf (const engine::Voice& v, const std::vector<float>& L, int& zonesOut)
    {
        const engine::RegionState& G = v.reg;
        const double Lr = G.L, X = G.X;
        std::vector<std::pair<std::int64_t, std::int64_t>> zones;
        for (int k = 1; k * Lr < static_cast<double> (L.size()); ++k)
        {
            const double a = G.post ? k * Lr : k * Lr - X;
            zones.emplace_back (static_cast<std::int64_t> (std::floor (a)) - 60, static_cast<std::int64_t> (std::ceil (a + X)) + 60);
        }
        double seam = 0, steady = 0;
        for (const auto& z : zones)
            seam = std::max (seam, rig::maxStep (L, z.first, z.second));
        auto inZone = [&zones] (std::int64_t i) {
            for (const auto& z : zones)
                if (i >= z.first && i < z.second)
                    return true;
            return false;
        };
        for (std::int64_t i = 4800; i < static_cast<std::int64_t> (L.size()); ++i)
            if (! inZone (i) && ! inZone (i - 1))
                steady = std::max (steady, std::abs (static_cast<double> (L[static_cast<std::size_t> (i)]) - L[static_cast<std::size_t> (i - 1)]));
        zonesOut = static_cast<int> (zones.size());
        return seam / steady;
    }
}

SL_TEST ("engine", readAbsFastPaths)
{
    section ("readAbs and the inline fast path: bit for bit at random positions, every kind of Region");

    // Short sources, so the file's own edges (frames 0 and n-3 .. n-1) are inside or next to every Region.
    signals::Lcg r (12345);
    const std::int64_t n = 3000;
    std::vector<float> mono (static_cast<std::size_t> (n)), left (static_cast<std::size_t> (n)), right (static_cast<std::size_t> (n));
    for (std::size_t i = 0; i < mono.size(); ++i)
    {
        mono[i] = static_cast<float> (r() * 2 - 1);
        left[i] = static_cast<float> (r() * 2 - 1);
        right[i] = static_cast<float> (r() * 2 - 1);
    }
    const auto sMono = engine::makeSampleData (mono, {}, 48000.0, "mono");
    const auto sStereo = engine::makeSampleData (left, right, 48000.0, "stereo");
    check (sMono != nullptr && ! sMono->stereo && sStereo != nullptr && sStereo->stereo, "a mono and a stereo source of 3000 frames");

    // The Hermite itself: readAbs's interior branch and the fast path's hermite () are the same expression.
    int interior = 0, interiorBad = 0;
    for (int q = 0; q < 20000; ++q)
    {
        const double x = 1 + r() * static_cast<double> (n - 4);
        double l = 0, rr = 0;
        engine::readAbs (*sStereo, x, l, rr);
        const auto i = static_cast<std::int64_t> (std::floor (x));
        const double f = x - std::floor (x);
        ++interior;
        if (! sameBits (l, engine::hermite (sStereo->L, i, f)) || ! sameBits (rr, engine::hermite (sStereo->R, i, f)))
            ++interiorBad;
    }
    check (interiorBad == 0, fmt ("hermite () equals readAbs on frames 1 .. n-3 (%d positions, %d differ)", interior, interiorBad));

    struct Config
    {
        const char* name;
        engine::RegionState g;
    };
    const Config configs[] = {
        { "loop, post seam", region (1000, 1200, 100.5, true, true, false, 0.3) },
        { "loop, pre seam", region (1500, 1500, 96, false, true, false, 0.7) },
        { "loop, plain wrap (whole file)", region (0, static_cast<double> (n), 0, true, true, false, 0) },
        { "loop from frame 0, post seam", region (0, 1000, 64, true, true, false, 0.1) },
        { "Hold / Stop (clamped)", region (700, 900, 0, true, false, false, 0) },
        { "Hold to the file end", region (2000, 1000, 0, true, false, false, 0) },
        { "Bounce (folded)", region (300, 640, 0, true, false, true, 0) },
    };
    int allFast = 0, allSlow = 0;
    for (const auto& c : configs)
    {
        for (const engine::SampleData* s : { sMono.get(), sStereo.get() })
        {
            FastTally t;
            const double L = c.g.L;
            for (int q = 0; q < 20000; ++q)
                probe (*s, c.g, -3 * L + r() * 8 * L, t);
            // the edges: every pass boundary, the seam zones and the file's last frames, with small offsets
            for (int k = -2; k <= 4; ++k)
                for (const double base : { k * L, k * L + c.g.X, k * L - c.g.X, k * L + L - 1, k * L + 1 })
                    for (const double d : { -1.5, -1.0, -0.5, -1e-9, 0.0, 1e-9, 0.25, 0.5, 1.0, 2.0 })
                        probe (*s, c.g, base + d, t);
            allFast += t.fast;
            allSlow += t.slow;
            check (t.mismatches == 0 && t.fast > 1000,
                   fmt ("%s, %s: the fast path equals readReg bit for bit (%d fast reads, %d through readReg, %d differ)%s", c.name,
                        s->stereo ? "stereo" : "mono", t.fast, t.slow, t.mismatches, t.first.empty() ? "" : (": " + t.first).c_str()));
        }
    }
    check (allSlow > 10000, fmt ("the probes also reached the guarded reads (seams, edges, outside the Region): %d", allSlow));

    // A no-op is not a pass: a fast path that forgets the post seam (reads the plain Region where readReg crossfades)
    // must be caught by the same comparison.
    {
        const engine::RegionState g = configs[0].g;
        engine::PlainStretch wrong (*sMono, g);
        wrong.xLater = 0;
        int caught = 0;
        for (int q = 0; q < 2000; ++q)
        {
            const double x = g.L + r() * g.X;   // inside the second pass's seam zone
            double a = 0, b = 0, ra = 0, rb = 0;
            engine::readReg (*sMono, g, x, ra, rb);
            if (wrong.read (x, a, b) && ! sameBits (a, ra))
                ++caught;
        }
        check (caught > 1900, fmt ("negative control: a fast path without the seam rule differs from readReg in the seam zone (%d of 2000)", caught));
    }
}

SL_TEST ("engine", loopSeamTape)
{
    section ("loop seam on one Tape voice: Region 0.1 .. 0.35 of a smooth 2-partial C3 tone, a live key");

    // test-engine.js 'loop seam': tone (midiHz (60), 2, SR, { partials: 2 }), inst { engine: tape, region: { start:
    // 0.1, end: 0.35, xfadeMs } }, noteOn (60, 1), 2.5 s in blocks of 128.
    signals::ToneOpts opts;
    opts.partials = 2;
    const std::vector<float> src = signals::tone (signals::midiHz (60), 2, 48000, opts);

    // The prototype's numbers for these renders (the JS Voice driven the same way, scratch script e1/ref.js).
    struct Want
    {
        double xfadeMs, ratio;
        const char* hash;
    };
    const Want wants[] = { { 10, 1.0337246672832128, "e4416215" }, { 0, 16.193937828473878, "535bc361" } };

    double ratios[2] = { 0, 0 };
    for (int w = 0; w < 2; ++w)
    {
        rig::VoiceRig rg (48000);
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        inst.region.start = 0.1;
        inst.region.end = 0.35;
        inst.region.xfadeMs = wants[w].xfadeMs;
        rg.setInst (inst);
        rg.setSample (rig::sampleOf (src, 48000));
        engine::Voice& v = rg.live (60, 1);
        const std::int64_t n = 120000;
        std::vector<float> L (static_cast<std::size_t> (n)), R (static_cast<std::size_t> (n));
        rg.render (v, L, R, 0, n, 128);
        int zones = 0;
        ratios[w] = seamRatioOf (v, L, zones);
        const std::string hash = signals::hex8 (rig::hashOf (L, R));
        note (fmt ("%g ms crossfade: Region %g frames, seam fade %g frames (%s), rho %.6f; seam / steady = %.6f over %d seams "
                   "(the prototype %.6f); hash %s (the prototype %s, %s)",
                   wants[w].xfadeMs, v.reg.L, v.reg.X, v.reg.post ? "post" : "pre", v.reg.rho, ratios[w], zones, wants[w].ratio, hash.c_str(),
                   wants[w].hash, hash == wants[w].hash ? "bit for bit" : "not bit for bit"));
        check (zones == 5 && v.reg.L == 23999 && v.reg.X == (wants[w].xfadeMs > 0 ? 480 : 0) && v.reg.post,
               fmt ("%g ms: the Region is 23999 frames with a %g-frame post seam, 5 seams in 2.5 s", wants[w].xfadeMs, wants[w].xfadeMs > 0 ? 480.0 : 0.0));
        check (std::abs (ratios[w] - wants[w].ratio) < 1e-4, fmt ("%g ms: the ratio is the prototype's within 1e-4 (%.9f, %.9f)", wants[w].xfadeMs,
                                                                   ratios[w], wants[w].ratio));
    }
    check (ratios[0] < 1.5, fmt ("Tape, 10 ms crossfade: largest step in the seam zones / largest steady step = %.4f (< 1.5)", ratios[0]));
    check (ratios[1] > 1.5, fmt ("negative control: with no crossfade the same measure is %.3f (> 1.5): it sees a hard wrap", ratios[1]));
}

SL_TEST ("engine", loopSeamRamp)
{
    section ("loop seam on one Tape voice: a ramp source's worst seam (0.85 -> 0.05), Region 0 .. 0.5");

    // test-engine.js: rampSource (2, SR), inst { engine: tape, region: { start: 0, end: 0.5, xfadeMs: 10 } }, noteOn
    // (60, 1), 1.5 s; largest step from sample 4800 on.
    const signals::Signal ramp = signals::rampSource (2, 48000);
    double steps[2] = { 0, 0 };
    const double wantSteps[2] = { 0.0012744665145874023, 0.20682281255722046 };
    for (int w = 0; w < 2; ++w)
    {
        rig::VoiceRig rg (48000);
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        inst.region.start = 0;
        inst.region.end = 0.5;
        inst.region.xfadeMs = w == 0 ? 10 : 0;
        rg.setInst (inst);
        rg.setSample (rig::sampleOf (ramp));
        engine::Voice& v = rg.live (60, 1);
        const std::int64_t n = 72000;
        std::vector<float> L (static_cast<std::size_t> (n)), R (static_cast<std::size_t> (n));
        rg.render (v, L, R, 0, n, 128);
        steps[w] = rig::maxStep (L, 4800, n);
        note (fmt ("%s: largest step %.9f (the prototype %.9f); seam fade %g frames %s; hash %s", w == 0 ? "10 ms" : "no crossfade", steps[w],
                   wantSteps[w], v.reg.X, v.reg.post ? "post" : "pre", signals::hex8 (rig::hashOf (L, R)).c_str()));
        check (std::abs (steps[w] - wantSteps[w]) < 1e-6, fmt ("%s: the largest step is the prototype's within 1e-6", w == 0 ? "10 ms" : "no crossfade"));
    }
    check (steps[0] < 0.01, fmt ("the ramp's 0.85 -> 0.05 seam crossfades: largest step %.5f (< 0.01)", steps[0]));
    check (steps[1] > 0.01, fmt ("negative control: without the crossfade the same measure is %.4f (> 0.01)", steps[1]));
}
