// CPU (PLUGIN-PLAN 4, M2 "CPU (Tape)"), timed by the plan's timing-gate rule: a figure and its target are printed,
// and the test fails only when slower than real time, or more than 5 times its target. Gated on Windows only (the
// Linux job prints the figures). Group "cpu" (kept out of the sanitizer job, whose times mean nothing).
//  - 16 voices x 8 heads Tape at 96 kHz in 64-sample blocks: target >= 8x real time, gate > 1x;
//  - a 4096-note clip at 96 kHz in 64-sample blocks, before any voice sounds (only scanStarts works): target < 0.5 %
//    of a core, gate at 5 times that.
// M3 part B adds the grain engines at 96 kHz and a typical load per engine at 48 kHz (at the end of the file).

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"
#include "model/Clip.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

#if defined(_WIN32)
    constexpr bool kGateTimes = true;
#else
    constexpr bool kGateTimes = false;
#endif

    double secondsSince (std::chrono::steady_clock::time_point t0)
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    }
}

SL_TEST ("cpu", engineTapeCpu)
{
    section ("CPU: 16 voices x 8 heads Tape at 96 kHz, 64-sample blocks (target >= 8x real time, gate > 1x on Windows)");

    const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
    auto sample = engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw");
    auto e = std::make_unique<engine::Engine>();
    e->prepare (96000);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    inst.heads = 8;
    inst.spread = 0.5;
    inst.drift = 0.3;
    inst.polyphony = 16;
    e->setInst (inst);
    e->setSample (sample.get(), 1);
    for (int i = 0; i < 16; ++i)
        e->noteOn (40 + 2 * i, 0.8);
    const double audioSeconds = 10;
    const auto n = static_cast<std::int64_t> (audioSeconds * 96000);
    std::vector<float> L (64), R (64);
    double best = 1e300;
    int voices = 0, thinned = 0;
    for (int run = 0; run < 2; ++run)
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (std::int64_t k = 0; k < n; k += 64)
            e->process (L.data(), R.data(), 64);
        best = std::min (best, secondsSince (t0));
        voices = e->stats().voices;
        thinned = e->stats().thinnedHeads;
    }
    const double x = audioSeconds / best;
    note (fmt ("%.1f x real time (target >= 8x): %g s of audio in %.3f s; %d voices, %d heads held back by the 96 kHz budget", x, audioSeconds, best,
               voices, thinned));
    check (voices == 16, fmt ("16 voices sound (%d)", voices));
    if (kGateTimes)
        check (x > 1.0, fmt ("faster than real time (%.1f x)", x));
    else
        note ("not gated on this platform (the plan gates CPU on Windows)");
}

SL_TEST ("cpu", scanStartsCpu)
{
    section ("CPU: scanStarts on a 4096-note clip at 96 kHz, 64-sample blocks, before any voice sounds (target < 0.5 % of a core)");

    model::Clip clip = model::newClip (64);   // 256 beats
    for (int i = 0; i < 4096; ++i)
        clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (36 + (i % 48), 64 + i * (192.0 / 4096), 0.04, "s" + std::to_string (i))));
    auto snap = render::compile (clip);
    const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
    auto sample = engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw");
    auto e = std::make_unique<engine::Engine>();
    e->prepare (96000);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    e->setInst (inst);
    e->setSample (sample.get(), 1);
    e->setSnapshot (snap.get(), 1);
    engine::Transport t;
    t.playing = true;
    t.bpm = 120;
    t.beat = 0;
    e->setTransport (t);
    const double audioSeconds = 30;   // 60 beats: the first note is at beat 64
    const auto n = static_cast<std::int64_t> (audioSeconds * 96000);
    std::vector<float> L (64), R (64);
    const auto t0 = std::chrono::steady_clock::now();
    for (std::int64_t k = 0; k < n; k += 64)
        e->process (L.data(), R.data(), 64);
    const double took = secondsSince (t0);
    const double percent = took / audioSeconds * 100;
    note (fmt ("%.3f %% of a core (target < 0.5 %%): %g s of audio in %.1f ms with %zu notes", percent, audioSeconds, took * 1000, snap->notes.size()));
    check (e->stats().voices == 0 && snap->notes.size() == 4096, "no voice sounded, and the clip holds 4096 notes");
    if (kGateTimes)
        check (percent < 2.5, fmt ("within 5 times the target (%.3f %% < 2.5 %%)", percent));
    else
        note ("not gated on this platform (the plan gates CPU on Windows)");
}

// M3 part B (PLUGIN-PLAN 4, M3 "CPU at 96 kHz"): the grain engines, timed by the same rule. The source is the
// prototype speed test's (test-engine.js "speed": the 12-partial C3 tone plus noise 0.3), Refresh Free 80 ms,
// Spread 0.3, each figure the best of 3 runs on a fresh engine.
//  - at 96 kHz, 64-sample blocks: Cloud 16 voices x 8 heads with 140 ms grains, Lock 16 x 8 with N = 12, and Voice
//    16 x 8 around C5: targets >= 2.0x real time, printed; gate > 1x on Windows;
//  - at 48 kHz, 64-sample blocks: 8 voices x 2 heads in Tape, Voice, Cloud and Lock: targets >= 20x, printed; gate
//    > 1x on Windows.
// The voice counts are gated everywhere (a count, not a time).

namespace
{
    std::vector<float> speedSource()
    {
        const std::vector<float> t = signals::tone (signals::midiHz (60), 6, 48000, signals::ToneOpts { 12, 0, 0 });
        const std::vector<float> nz = signals::noise (6, 48000, 2, 0.3);
        std::vector<float> mix (t.size());
        for (std::size_t i = 0; i < mix.size(); ++i)
            mix[i] = static_cast<float> (static_cast<double> (t[i]) + static_cast<double> (nz[i]));
        return mix;
    }

    struct Bench
    {
        double x = 0;       // the best of the runs, in x real time
        int voices = 0;     // voices sounding at the end
        int maxLoad = 0, budget = 0, thinned = 0;
    };

    /** voices keys (first, first + step, ...) held on a fresh engine per run; secs of audio in 64-sample blocks. */
    Bench bench (const core::Inst& inst, double sr, int voices, int firstKey, int step, double secs, int runs)
    {
        static const std::vector<float> mix = speedSource();
        auto sample = engine::makeSampleData (mix, {}, 48000, "speed");
        Bench b;
        double best = 1e300;
        std::vector<float> L (64), R (64);
        const auto n = static_cast<std::int64_t> (std::llround (secs * sr));
        for (int run = 0; run < runs; ++run)
        {
            auto e = std::make_unique<engine::Engine>();
            e->prepare (sr);
            e->setInst (inst);
            e->setSample (sample.get(), 1);
            for (int i = 0; i < voices; ++i)
                e->noteOn (firstKey + step * i, 1);
            const auto t0 = std::chrono::steady_clock::now();
            for (std::int64_t k = 0; k < n; k += 64)
                e->process (L.data(), R.data(), 64);
            best = std::min (best, secondsSince (t0));
            const engine::Stats s = e->stats();
            b.voices = s.voices;
            b.maxLoad = s.maxGrains;
            b.budget = s.budget;
            b.thinned = s.thinnedHeads;
        }
        b.x = secs / best;
        return b;
    }

    core::Inst benchInst (core::Engine engine, bool lock, double heads)
    {
        core::Inst inst = core::defaultInst();
        inst.engine = engine;
        inst.lock = lock;
        inst.heads = heads;
        inst.spread = 0.3;
        inst.refresh.mode = core::RefreshMode::free;
        inst.refresh.ms = 80;
        inst.polyphony = 16;
        return inst;
    }
}

SL_TEST ("cpu", grainEnginesCpu96k)
{
    section ("CPU: the grain engines at 96 kHz, 16 voices x 8 heads, 64-sample blocks, best of 3 (targets >= 2.0x real time, gate > 1x on "
             "Windows)");

    struct Row
    {
        const char* name;
        core::Engine engine;
        bool lock;
        int firstKey, step;
    };
    const Row rows[] = { { "Cloud, 140 ms grains", core::Engine::cloud, false, 40, 3 },
                         { "Lock, N = 12", core::Engine::cloud, true, 40, 3 },
                         { "Voice around C5", core::Engine::voice, false, 76, 1 } };
    for (const Row& r : rows)
    {
        core::Inst inst = benchInst (r.engine, r.lock, 8);
        inst.grainCloud = 140;
        inst.grainLock = 12;
        const Bench b = bench (inst, 96000, 16, r.firstKey, r.step, 4, 3);
        note (fmt ("%s: %.2f x real time (target >= 2.0x); %d voices, the most load %d of %d, %d heads held back by the budget", r.name, b.x, b.voices,
                   b.maxLoad, b.budget, b.thinned));
        check (b.voices == 16, fmt ("%s: 16 voices sound (%d)", r.name, b.voices));
        if (kGateTimes)
            check (b.x > 1.0, fmt ("%s: faster than real time (%.2f x)", r.name, b.x));
    }
    if (! kGateTimes)
        note ("not gated on this platform (the plan gates CPU on Windows)");
}

SL_TEST ("cpu", everyEngineCpu48kTypical)
{
    section ("CPU: a typical load, 8 voices x 2 heads per engine at 48 kHz, 64-sample blocks, best of 3 (targets >= 20x real time, gate > 1x "
             "on Windows)");

    struct Row
    {
        const char* name;
        core::Engine engine;
        bool lock;
    };
    const Row rows[] = { { "Tape", core::Engine::tape, false },
                         { "Voice", core::Engine::voice, false },
                         { "Cloud", core::Engine::cloud, false },
                         { "Lock", core::Engine::cloud, true } };
    for (const Row& r : rows)
    {
        const Bench b = bench (benchInst (r.engine, r.lock, 2), 48000, 8, 40, 3, 6, 3);
        note (fmt ("%s: %.1f x real time (target >= 20x); %d voices, the most load %d of %d", r.name, b.x, b.voices, b.maxLoad, b.budget));
        check (b.voices == 8, fmt ("%s: 8 voices sound (%d)", r.name, b.voices));
        if (kGateTimes)
            check (b.x > 1.0, fmt ("%s: faster than real time (%.1f x)", r.name, b.x));
    }
    if (! kGateTimes)
        note ("not gated on this platform (the plan gates CPU on Windows)");
}
