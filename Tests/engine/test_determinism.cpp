// Determinism with static settings (PLUGIN-PLAN 4, M2; risks 4.5; 8.2 #21): 10 s of Strand Demo with a chase at
// beat 1.7, live keys at fixed sample positions, Scatter and Refresh Skip, and a transport jump, rendered with host
// blocks of 1, 37, 128, 700, 4096 and random 1..700 samples: bit-identical. The same scenario started after 0, 7
// and 1000 stopped samples (aligned to the transport start): bit-identical, because the control grid counts from
// the transport start. The scenario's rms is above 0.01 (a no-op is not a pass). The processor's setNonRealtime
// version is the integration step's. M3 part B adds the same rules with every engine playing (at the end of the
// file). Group "engine".

#include "engine/EngineRender.h"
#include "engine/GrainRig.h"
#include "engine/VoiceRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "core/Snapshot.h"
#include "engine/Engine.h"
#include "model/Presets.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
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

// M3 part B: the same rules with every engine playing.
//  - The M2 block-size scenario with the grain engines (prototype-v4/tests/test-engine.js "random block sizes"): a
//    Voice-to-Cloud chain with a glide, a Tape Scatter note, a Lock note with a moving Heads lane, a Cloud note
//    with Each grain backwards and a stepped Travel lane, a Voice Mirror note on a Path-kind Travel, Refresh Free
//    90 ms with Skip 0.3, Spray 0.1, Drift, Head tune detune, a 1-bar clip at 101 bpm on a stereo 44.1 kHz source;
//    plus a chase (Play at beat 0.3), a live key, and a transport jump. Blocks 1, 37, 700, 4096 and random 1..700
//    equal blocks of 128 bit for bit, and so do starts after 7 and 1000 stopped samples.
//  - An 8-head Voice chord high up (C4 .. G5), under budget pressure (heads thinned), started after 0, 7 and 1000
//    stopped samples: bit-identical (the control grid counts from the transport start, 8.2 #21).

namespace
{
    struct GrainScene
    {
        std::unique_ptr<engine::SampleData> sample;
        std::unique_ptr<core::Snapshot> snap;
        core::Inst inst;
    };

    rig::LaneSpec laneValue (double v)
    {
        rig::LaneSpec s;
        s.set = true;
        s.value = v;
        return s;
    }

    rig::LaneSpec laneCurve (std::function<double (double)> fn)
    {
        rig::LaneSpec s;
        s.set = true;
        s.fn = std::move (fn);
        return s;
    }

    rig::LaneSpec& laneOf (rig::RawNote& n, core::LaneId id) { return n.lanes[static_cast<std::size_t> (id)]; }

    const GrainScene& grainScene()
    {
        static GrainScene g = [] {
            GrainScene x;
            const double srcSr = 44100;
            const std::vector<float> t1 = signals::tone (signals::midiHz (60), 4, srcSr), nz = signals::noise (4, srcSr, 4, 0.2);
            std::vector<float> l (t1.size()), r (t1.size());
            for (std::size_t i = 0; i < t1.size(); ++i)
            {
                l[i] = static_cast<float> (static_cast<double> (t1[i]) + static_cast<double> (nz[i]));
                r[i] = static_cast<float> (static_cast<double> (t1[i]) - static_cast<double> (nz[i]));
            }
            x.sample = engine::makeSampleData (std::move (l), std::move (r), srcSr, "tone+noise");
            using core::LaneId;
            rig::RawNote a1 = grains::rawNote ("a1", 60, 0, 1.5, core::Engine::voice);
            a1.hasGlide = true;
            a1.glideD = 7;
            a1.glideLen = 0.4;
            a1.glideCurve = 0.3;
            a1.glideTo = "a2";
            laneOf (a1, LaneId::vib) = laneValue (0.3);
            rig::RawNote a2 = grains::rawNote ("a2", 67, 1.5, 2, core::Engine::cloud);   // the chain goes Voice -> Cloud
            rig::RawNote b = grains::rawNote ("b", 72, 0.25, 3, core::Engine::tape);
            b.motion = core::Motion::scatter;
            b.jump = 0.25;
            rig::RawNote c = grains::rawNote ("c", 48, 0.5, 2.5, core::Engine::cloud);
            c.lock = true;
            laneOf (c, LaneId::heads) = laneCurve ([] (double u) { return 0.5 + 0.2 * u; });
            rig::RawNote d = grains::rawNote ("d", 64, 1, 2.5, core::Engine::cloud);
            d.rev = true;
            laneOf (d, LaneId::travel) = laneCurve ([] (double u) { return static_cast<int> (std::floor (u * 8)) % 2 != 0 ? 0.25 : 0.75; });
            laneOf (d, LaneId::grain) = laneValue (0.3);
            rig::RawNote e = grains::rawNote ("e", 55, 2, 1.9, core::Engine::voice);
            e.motion = core::Motion::mirror;
            e.travel = 1;   // Path
            laneOf (e, LaneId::travel) = laneCurve ([] (double u) { return u; });
            laneOf (e, LaneId::heads) = laneValue (0.64);
            x.snap = rig::rawSnapshot ({ a1, a2, b, c, d, e }, 1, false);
            // chainNotes ([a1, a2]): one chain of the first two notes, in all three Auto Slide modes
            for (int mode = 0; mode < 3; ++mode)
            {
                x.snap->notes[0].chains[mode] = core::ChainSnap { 0, 0, -1, 1 };
                x.snap->notes[1].chains[mode] = core::ChainSnap { 0, 1, 0, -1 };
            }
            x.inst = grains::instWith (core::Engine::voice);
            x.inst.refresh.mode = core::RefreshMode::free;
            x.inst.refresh.ms = 90;
            x.inst.refresh.skip = 0.3;
            x.inst.spray = 0.1;
            x.inst.drift = 0.5;
            x.inst.headTune = core::HeadTune::detune;
            x.inst.spread = 0.4;
            x.inst.heads = 2;
            return x;
        }();
        return g;
    }

    engine::Transport playAt101 (double beat)
    {
        engine::Transport t;
        t.playing = true;
        t.bpm = 101;
        t.beat = beat;
        t.loop = true;
        return t;
    }

    void renderGrainScene (std::int64_t stopped, const std::function<int (std::int64_t)>& block, std::vector<float>& L, std::vector<float>& R,
                           double playBeat = 0.3)
    {
        const GrainScene& g = grainScene();
        auto e = std::make_unique<engine::Engine>();
        e->prepare (48000);
        e->setRampMs (0);
        e->setInst (g.inst);
        e->setSample (g.sample.get(), 1);
        e->setSnapshot (g.snap.get(), 1);
        render::Script script;
        const std::int64_t S = stopped;
        script.at (S, [playBeat] (engine::Engine& en) { en.setTransport (playAt101 (playBeat)); });   // Play inside a1 and b: a chase
        script.at (S + 3000, [] (engine::Engine& en) { en.noteOn (64, 0.9); });
        script.at (S + 60000, [] (engine::Engine& en) { en.noteOff (64); });
        script.at (S + 90000, [] (engine::Engine& en) { en.setTransport (playAt101 (1.2)); });   // a jump: release and chase
        std::vector<float> l, r;
        script.run (*e, S + 144000, block, l, r);
        L.assign (l.begin() + S, l.end());
        R.assign (r.begin() + S, r.end());
    }
}

SL_TEST ("engine", deterministicGrainEngines)
{
    section ("determinism with the grain engines: Voice -> Cloud chain, Lock, Cloud backwards, Tape Scatter, Voice Mirror on a path, Spray, "
             "Skip, a chase, a live key, a jump (3 s): every block size and start offset bit-identical");

    std::vector<float> L0, R0;
    renderGrainScene (0, render::fixedBlocks (128), L0, R0);
    const double rms = std::max (rmsOf (L0), rmsOf (R0));
    check (rms > 0.01, fmt ("the scenario makes sound (rms %.4f > 0.01)", rms));

    int same = 0;
    std::string differ;
    for (const int size : { 1, 37, 700, 4096 })
    {
        std::vector<float> L, R;
        renderGrainScene (0, render::fixedBlocks (size), L, R);
        if (L == L0 && R == R0)
            ++same;
        else
            differ += " " + std::to_string (size);
    }
    {
        auto r = std::make_shared<signals::Lcg> (99);
        std::vector<float> L, R;
        renderGrainScene (0, [r] (std::int64_t) { return 1 + static_cast<int> (std::floor ((*r)() * 700)); }, L, R);
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
        renderGrainScene (stopped, render::fixedBlocks (128), L, R);
        if (L == L0 && R == R0)
            ++aligned;
    }
    check (aligned == 2, fmt ("started after 7 and 1000 stopped samples, aligned to the transport start: bit-identical (%d of 2)", aligned));

    std::vector<float> Lc, Rc;
    renderGrainScene (0, render::fixedBlocks (128), Lc, Rc, 0.35);
    check (Lc != L0, "negative control: Play at beat 0.35 instead of 0.3 renders differently");
}

SL_TEST ("engine", deterministicUnderBudgetPressure)
{
    section ("determinism under budget pressure: an 8-head Voice chord C4 .. G5 (heads thinned) started after 0, 7 and 1000 stopped "
             "samples: bit-identical");

    auto sample = engine::makeSampleData (signals::tone (signals::midiHz (60), 4, 48000), {}, 48000, "tone");
    std::vector<rig::RawNote> chord;
    const double keys[] = { 72, 76, 79, 84, 88, 91 };
    for (int i = 0; i < 6; ++i)
        chord.push_back (grains::rawNote (("ch" + std::to_string (i)).c_str(), keys[i], 0, 8, core::Engine::voice));
    auto snap = rig::rawSnapshot (chord, 4, false);
    core::Inst inst = grains::instWith (core::Engine::voice);
    inst.heads = 8;
    inst.spread = 0.5;

    auto go = [&] (std::int64_t stopped, double beat, std::vector<float>& L, std::vector<float>& R, int& thinned) {
        auto e = std::make_unique<engine::Engine>();
        e->prepare (48000);
        e->setRampMs (0);
        e->setInst (inst);
        e->setSample (sample.get(), 1);
        e->setSnapshot (snap.get(), 1);
        render::Script script;
        script.at (stopped, [beat] (engine::Engine& en) {
            engine::Transport t;
            t.playing = true;
            t.bpm = 120;
            t.beat = beat;
            en.setTransport (t);
        });
        thinned = 0;
        std::vector<float> l, r;
        script.run (*e, stopped + 96000, render::fixedBlocks (128), l, r,
                    [&thinned] (engine::Engine& en, std::int64_t) { thinned = std::max (thinned, en.stats().thinnedHeads); });
        L.assign (l.begin() + stopped, l.end());
        R.assign (r.begin() + stopped, r.end());
    };

    std::vector<float> L0, R0;
    int thinned0 = 0;
    go (0, 0, L0, R0, thinned0);
    check (thinned0 > 0 && rmsOf (L0) > 0.01, fmt ("the chord is under budget pressure: %d heads held back (> 0), rms %.4f", thinned0, rmsOf (L0)));
    int aligned = 0;
    for (const std::int64_t stopped : { 7, 1000 })
    {
        std::vector<float> L, R;
        int th = 0;
        go (stopped, 0, L, R, th);
        aligned += L == L0 && R == R0 ? 1 : 0;
    }
    check (aligned == 2, fmt ("started after 7 and 1000 stopped samples: bit-identical to the start after 0 (%d of 2)", aligned));
    std::vector<float> Lc, Rc;
    int thc = 0;
    go (0, 0.01, Lc, Rc, thc);
    check (Lc != L0, "negative control: Play at beat 0.01 renders differently");
}
