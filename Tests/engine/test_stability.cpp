// Stability and the engine's bookkeeping (PLUGIN-PLAN 4, M2 "stability"; 1.2, 1.3, 1.8): Strand Demo with 8 heads Tape
// and 16 live chords keeps every health counter at 0 and the output in bounds; the health counters do count (a
// source too loud to be real, a source with NaN); 300 simultaneous starts keep the 256 earliest and start the rest
// at the next segment (stats ().deferredStarts) while the 64-voice pool cuts its oldest stealing voices; a deleted
// note's voice keeps its snapshot's epoch until it is done, an edited note's voice follows at once. Group "engine".

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "engine/Engine.h"
#include "model/Clip.h"
#include "model/Node.h"
#include "model/Presets.h"
#include "rt/VersionExchange.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    engine::Transport playAt (double beat)
    {
        engine::Transport t;
        t.playing = true;
        t.bpm = 120;
        t.beat = beat;
        return t;
    }

    double rmsOf (const std::vector<float>& x)
    {
        double s = 0;
        for (const float v : x)
            s += static_cast<double> (v) * v;
        return x.empty() ? 0 : std::sqrt (s / static_cast<double> (x.size()));
    }

    double peakOf (const std::vector<float>& x)
    {
        double p = 0;
        for (const float v : x)
            p = std::max (p, std::abs (static_cast<double> (v)));
        return p;
    }

    model::Clip clipWith (int bars, const std::vector<model::Note>& notes)
    {
        model::Clip c = model::newClip (bars);
        for (const model::Note& n : notes)
            c.notes.push_back (std::make_shared<const model::Note> (n));
        return c;
    }
}

SL_TEST ("engine", stabilityStrandDemoChords)
{
    section ("stability: Strand Demo with 8 heads Tape and 16 live chords, 10 s: no non-finite sample, no filter reset, output in bounds");

    const sounds::StarterSound open = sounds::makeStarterSound (sounds::StarterId::sawopen, 1);
    auto sample = engine::makeSampleData (open.left, open.right, open.sampleRate, "sawopen");
    auto snap = render::compile (*model::loadClipPreset ("strand-demo"));
    core::Inst inst = model::applySoundPreset (core::defaultInst(), "init")->inst;
    inst.engine = core::Engine::tape;
    inst.heads = 8;
    auto e = std::make_unique<engine::Engine>();
    e->prepare (48000);
    e->setInst (inst);
    e->setSample (sample.get(), 1);
    e->setSnapshot (snap.get(), 1);
    render::Script s;
    s.at (0, [] (engine::Engine& en) { en.setTransport (playAt (0)); });
    const int chord[3] = { 0, 4, 7 };
    for (int c = 0; c < 16; ++c)
    {
        const int root = 48 + (c * 5) % 12;
        const std::int64_t at = 6000 + c * 28000;
        for (const int iv : chord)
        {
            s.at (at, [root, iv] (engine::Engine& en) { en.noteOn (root + iv, 0.9); });
            s.at (at + 24000, [root, iv] (engine::Engine& en) { en.noteOff (root + iv); });
        }
    }
    std::vector<float> L, R;
    s.run (*e, 480000, render::fixedBlocks (256), L, R);
    const engine::Stats st = e->stats();
    const double rms = std::max (rmsOf (L), rmsOf (R)), peak = std::max (peakOf (L), peakOf (R));
    check (st.nonFinite == 0 && st.filterResets == 0, fmt ("nonFinite = %lld, filterResets = %lld", static_cast<long long> (st.nonFinite),
                                                           static_cast<long long> (st.filterResets)));
    check (st.preLimiterPeak > 0.05 && rms > 0.01 && peak <= 0.98,
           fmt ("pre-limiter peak %.3f > 0.05, output rms %.4f > 0.01, peak %.4f <= 0.98", st.preLimiterPeak, rms, peak));
    note (fmt ("at the end: %d heads held back by the budget; %lld steals, %lld cuts, the most load %d of %d", st.thinnedHeads,
               static_cast<long long> (st.steals), static_cast<long long> (st.cuts), st.maxGrains, st.budget));
}

SL_TEST ("engine", healthCountersCount)
{
    section ("the health counters count: a source far too loud trips the output guard, a NaN in the source resets the filters");

    for (const bool withNan : { false, true })
    {
        std::vector<float> x (48000, withNan ? 0.25f : 1.0e30f);
        if (withNan)
            for (std::size_t i = 1000; i < 2000; ++i)
                x[i] = std::numeric_limits<float>::quiet_NaN();
        auto sample = render::monoSample (x, 48000);
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        auto e = std::make_unique<engine::Engine>();
        e->prepare (48000);
        e->setInst (inst);
        e->setSample (sample.get(), 1);
        e->noteOn (60, 1.0);
        std::vector<float> L (4800), R (4800);
        e->process (L.data(), R.data(), 4800);
        const engine::Stats st = e->stats();
        bool finite = true;
        for (std::size_t i = 0; i < L.size(); ++i)
            finite = finite && std::isfinite (L[i]) && std::isfinite (R[i]) && std::abs (L[i]) <= 0.98f;
        if (withNan)
            check (st.filterResets > 0 && finite, fmt ("NaN in the source: %lld filter resets, the output stays finite",
                                                       static_cast<long long> (st.filterResets)));
        else
            check (st.nonFinite > 0 && finite, fmt ("a 1e30 source: the guard replaced %lld samples, the output stays finite and within 0.98",
                                                    static_cast<long long> (st.nonFinite)));
        e->resetStats();
        check (e->stats().nonFinite == 0 && e->stats().filterResets == 0, "resetStats clears them");
    }
}

SL_TEST ("engine", deferredStartsAndPoolCuts)
{
    section ("300 notes starting together: 256 start, 44 wait for the next segment (deferredStarts); the 64-voice pool cuts its oldest stealing voices");

    std::vector<model::Note> notes;
    for (int i = 0; i < 300; ++i)
        notes.push_back (model::mkNote (36 + (i % 48), 0, 1, "d" + std::to_string (1000 + i)));
    auto snap = render::compile (clipWith (4, notes));
    auto sample = render::monoSample (signals::dc (2.0, 48000, 0.01), 48000);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    inst.polyphony = 32;
    auto e = std::make_unique<engine::Engine>();
    e->prepare (48000);
    e->setInst (inst);
    e->setSample (sample.get(), 1);
    e->setSnapshot (snap.get(), 1);
    e->setTransport (playAt (0));
    std::vector<float> L (32), R (32);
    e->process (L.data(), R.data(), 32);
    const engine::Stats a = e->stats();
    e->process (L.data(), R.data(), 32);
    const engine::Stats b = e->stats();
    check (a.deferredStarts == 44 && b.deferredStarts == 44, fmt ("44 starts deferred (%lld)", static_cast<long long> (a.deferredStarts)));
    // polyphony 32: every start after the 32nd steals one voice; the pool holds 64: every start after the 64th cuts one
    check (a.steals == 256 - 32 && a.cuts == 256 - 64 && a.voices == 64,
           fmt ("after the first segment: 256 started (steals %lld, cuts %lld, %d voices)", static_cast<long long> (a.steals),
                static_cast<long long> (a.cuts), a.voices));
    check (b.steals == 300 - 32 && b.cuts == 300 - 64 && b.voices == 64,
           fmt ("after the second: all 300 started (steals %lld, cuts %lld, %d voices)", static_cast<long long> (b.steals),
                static_cast<long long> (b.cuts), b.voices));
    std::vector<float> rest (4800), restR (4800);
    e->process (rest.data(), restR.data(), 4800);
    check (e->stats().voices == 32, fmt ("once the 5 ms steal tails have ended, 32 voices sound (%d)", e->stats().voices));
}

SL_TEST ("engine", editsFollowAndEpochs)
{
    section ("snapshots while playing: a deleted note's voice keeps its epoch until it is done; an edited note's voice follows at once");

    model::Note keep = model::mkNote (60, 0, 4, "keep"), gone = model::mkNote (67, 0, 4, "gone");
    model::Note quieter = keep;
    quieter.lane (core::LaneId::amp) = model::NodeList { { 0.0, 0.4, model::NodeShape::linear, 0.0 }, { 1.0, 0.4, model::NodeShape::linear, 0.0 } };
    auto snapA = render::compile (clipWith (4, { keep, gone }));
    auto snapB = render::compile (clipWith (4, { keep }));
    auto snapC = render::compile (clipWith (4, { quieter }));
    auto sample = render::monoSample (signals::dc (4.0, 48000, 0.25), 48000);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    auto e = std::make_unique<engine::Engine>();
    e->prepare (48000);
    e->setInst (inst);
    e->setSample (sample.get(), 1);
    e->setSnapshot (snapA.get(), 5);
    e->setTransport (playAt (0));
    std::vector<float> L (4800), R (4800);
    e->process (L.data(), R.data(), 4800);
    check (e->stats().voices == 2 && e->minEpochInUse (engine::VersionKind::snapshot) == 5, "two notes sound on snapshot 5");

    e->setSnapshot (snapB.get(), 6);
    const std::uint64_t held = e->minEpochInUse (engine::VersionKind::snapshot);
    e->process (L.data(), R.data(), 4800);
    const std::uint64_t later = e->minEpochInUse (engine::VersionKind::snapshot);
    check (held == 5 && later == 6 && e->stats().voices == 1,
           fmt ("deleting a note: its voice holds epoch 5 while it releases (%llu), then only 6 is read (%llu), 1 voice (%d)",
                static_cast<unsigned long long> (held), static_cast<unsigned long long> (later), e->stats().voices));

    const double before = L[4799];
    e->setSnapshot (snapC.get(), 7);
    std::vector<float> L2 (128), R2 (128);
    e->process (L2.data(), R2.data(), 128);
    const double after = L2[127];
    const double want = (0.4 * 0.4) / (0.8 * 0.8);
    check (e->minEpochInUse (engine::VersionKind::snapshot) == 7 && e->stats().voices == 1 && std::abs (after / before - want) <= 0.01,
           fmt ("a Volume edit is heard within two ticks on the same voice: level ratio %.4f (expected %.4f), epoch 7", after / before, want));
    e->setSnapshot (nullptr, 0);
    e->process (L.data(), R.data(), 4800);
    check (e->minEpochInUse (engine::VersionKind::snapshot) == rt::kNoEpoch && e->stats().voices == 0,
           "no snapshot: the last voice releases, and once it is done no snapshot epoch is read");
}
