// Determinism with static settings (PLUGIN-PLAN 4, M2; risks 4.5; 8.2 #21): 10 s of Strand Demo with a chase at
// beat 1.7, live keys at fixed sample positions, Scatter and Refresh Skip, and a transport jump, rendered with host
// blocks of 1, 37, 128, 700, 4096 and random 1..700 samples: bit-identical. The same scenario started after 0, 7
// and 1000 stopped samples (aligned to the transport start): bit-identical, because the control grid counts from
// the transport start. The scenario's rms is above 0.01 (a no-op is not a pass). The processor's setNonRealtime
// version is the integration step's. Group "engine".

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"
#include "model/Presets.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr std::int64_t kLength = 480000;   // 10 s at 48 kHz

    struct Scene
    {
        std::unique_ptr<engine::SampleData> sample;
        std::unique_ptr<core::Snapshot> snap;
        core::Inst inst;
    };

    const Scene& scene()
    {
        static Scene s = [] {
            Scene x;
            const sounds::StarterSound open = sounds::makeStarterSound (sounds::StarterId::sawopen, 1);
            x.sample = engine::makeSampleData (open.left, open.right, open.sampleRate, "sawopen");
            x.snap = render::compile (*model::loadClipPreset ("strand-demo"));
            x.inst = model::applySoundPreset (core::defaultInst(), "init")->inst;
            x.inst.motion = core::Motion::scatter;
            x.inst.jump = core::RateId::r1_16;
            x.inst.refresh.mode = core::RefreshMode::free;
            x.inst.refresh.ms = 90;
            x.inst.refresh.skip = 0.3;
            x.inst.heads = 3;
            return x;
        }();
        return s;
    }

    engine::Transport playAt (double beat)
    {
        engine::Transport t;
        t.playing = true;
        t.bpm = 120;
        t.beat = beat;
        return t;
    }

    /** The scenario after `stopped` samples of a stopped transport, in host blocks given by block (k). */
    void renderScene (std::int64_t stopped, const std::function<int (std::int64_t)>& block, std::vector<float>& L, std::vector<float>& R)
    {
        const Scene& s = scene();
        auto e = std::make_unique<engine::Engine>();
        e->prepare (48000);
        e->setInst (s.inst);
        e->setSample (s.sample.get(), 1);
        e->setSnapshot (s.snap.get(), 1);
        render::Script script;
        const std::int64_t S = stopped;
        script.at (S, [] (engine::Engine& en) { en.setTransport (playAt (1.7)); });   // Play inside the clip: a chase
        script.at (S + 24000, [] (engine::Engine& en) { en.noteOn (48, 0.9); });
        script.at (S + 96000, [] (engine::Engine& en) { en.noteOff (48); });
        script.at (S + 130000, [] (engine::Engine& en) { en.noteOn (55, 0.7); });
        script.at (S + 131111, [] (engine::Engine& en) { en.noteOn (59, 0.6); });
        script.at (S + 200000, [] (engine::Engine& en) { en.setController (engine::Ctrl::mod1, 0.4); });
        script.at (S + 250000, [] (engine::Engine& en) { en.noteOff (55); });
        script.at (S + 260017, [] (engine::Engine& en) { en.noteOff (59); });
        script.at (S + 300000, [] (engine::Engine& en) { en.setTransport (playAt (9.3)); });   // a jump: release, chase
        script.at (S + 400000, [] (engine::Engine& en) { en.noteOn (62, 1.0); });
        std::vector<float> l, r;
        script.run (*e, S + kLength, block, l, r);
        L.assign (l.begin() + S, l.end());
        R.assign (r.begin() + S, r.end());
    }

    double rmsOf (const std::vector<float>& x)
    {
        double s = 0;
        for (const float v : x)
            s += static_cast<double> (v) * v;
        return std::sqrt (s / static_cast<double> (x.size()));
    }
}

SL_TEST ("engine", deterministicAcrossBlocksAndStarts)
{
    section ("determinism: Strand Demo + chase at 1.7 + live keys + Scatter + Skip + a jump, 10 s: every block size and start offset bit-identical");

    std::vector<float> L0, R0;
    renderScene (0, render::fixedBlocks (128), L0, R0);
    const double rms = std::max (rmsOf (L0), rmsOf (R0));
    check (rms > 0.01, fmt ("the scenario makes sound (rms %.4f > 0.01)", rms));

    int same = 0;
    std::string differ;
    for (const int size : { 1, 37, 700, 4096 })
    {
        std::vector<float> L, R;
        renderScene (0, render::fixedBlocks (size), L, R);
        if (L == L0 && R == R0)
            ++same;
        else
            differ += " " + std::to_string (size);
    }
    {
        // random host blocks 1..700 (the tests' lcg, seed 7)
        auto r = std::make_shared<signals::Lcg> (7);
        std::vector<float> L, R;
        renderScene (0, [r] (std::int64_t) { return 1 + static_cast<int> (std::floor ((*r)() * 700)); }, L, R);
        if (L == L0 && R == R0)
            ++same;
        else
            differ += " random";
    }
    check (same == 5, fmt ("blocks of 1, 37, 700, 4096 and random 1..700 equal blocks of 128 bit for bit (%d of 5%s%s)", same,
                           differ.empty() ? "" : "; differ:", differ.c_str()));

    int aligned = 0;
    for (const std::int64_t stopped : { 7, 1000 })
    {
        std::vector<float> L, R;
        renderScene (stopped, render::fixedBlocks (128), L, R);
        if (L == L0 && R == R0)
            ++aligned;
    }
    check (aligned == 2, fmt ("started after 7 and 1000 stopped samples, aligned to the transport start: bit-identical (%d of 2)", aligned));

    // A no-op is not a pass: a different scenario (Play at beat 1.75 instead of 1.7) differs.
    {
        const Scene& s = scene();
        auto e = std::make_unique<engine::Engine>();
        e->prepare (48000);
        e->setInst (s.inst);
        e->setSample (s.sample.get(), 1);
        e->setSnapshot (s.snap.get(), 1);
        render::Script script;
        script.at (0, [] (engine::Engine& en) { en.setTransport (playAt (1.75)); });
        std::vector<float> L, R;
        script.run (*e, 48000, render::fixedBlocks (128), L, R);
        const bool differs = ! std::equal (L.begin(), L.end(), L0.begin());
        check (differs, "negative control: Play at beat 1.75 renders differently from Play at 1.7");
    }
}
