// CPU (PLUGIN-PLAN 4, M2 "CPU (Tape)"), timed by the plan's timing-gate rule: a figure and its target are printed,
// and the test fails only when slower than real time, or more than 5 times its target. Gated on Windows only (the
// Linux job prints the figures). Group "cpu" (kept out of the sanitizer job, whose times mean nothing).
//  - 16 voices x 8 heads Tape at 96 kHz in 64-sample blocks: target >= 8x real time, gate > 1x;
//  - a 4096-note clip at 96 kHz in 64-sample blocks, before any voice sounds (only scanStarts works): target < 0.5 %
//    of a core, gate at 5 times that.

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/TestRegistry.h"

#include "core/Inst.h"
#include "engine/Engine.h"
#include "model/Clip.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <chrono>
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
