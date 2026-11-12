// The budget (PLUGIN-PLAN 1.8 D3, 4 M3 "budget", M3 part B), written from what the prototype does
// (prototype-v4/tests/test-engine.js "budget thinning", test-engine-extra.js section M, test-integration.js J):
//  - the prototype's 16 notes x 8 heads layout (notes 0.05 beat apart at 120 bpm, the pad starter; Voice, Cloud and
//    Lock N = 12; 44.1, 48 and 96 kHz): the measured max load (stats ().maxGrains) never passes B (208, 192, 96);
//  - Voice, 8 heads, 6 live keys 80 ms apart at 48 kHz: the newest note keeps all 8 heads after every note-on, and
//    the sounding heads never decrease toward newer voices (the oldest thin first);
//  - cuts = 0 where the prototype measured none: Lock 8 heads Grain 4 and Voice 8 heads, arpeggios of 6 notes
//    0.1 s apart, and Strand Demo with recipes 3, 6 and 8 at 8 heads;
//  - Lock N = 12 x 8 heads, the five budget-* cases of engine/renders.json: cuts, thinned heads, max load and B at
//    every stats row equal the prototype's, and the newest note sounds 8 / 8 / 4 heads at 44.1 / 48 / 96 kHz
//    (read from the display at the end, as the prototype shows it);
//  - and at that edge (thinning, waiting heads and cuts all running) the engine allocates nothing.
// Each scenario also checks that the budget was under pressure (heads thinned), so none passes by doing nothing.
// The prototype's own figures are printed next to the C++ ones (computed in node from sl-engine.js).
// Group "engine" (the allocation row also in "alloc").

#include "engine/EngineRender.h"
#include "engine/GrainRig.h"
#include "engine/VoiceRig.h"
#include "model/ModelFixture.h"
#include "support/AllocCounter.h"
#include "support/Check.h"
#include "support/FixtureJson.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "model/ClipEdit.h"
#include "model/Presets.h"
#include "model/Recipes.h"
#include "sounds/StarterSounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000;
    constexpr double kPiB = 3.141592653589793;

    std::unique_ptr<engine::SampleData> starterSample (sounds::StarterId id)
    {
        const sounds::StarterSound s = sounds::makeStarterSound (id, 1);
        return engine::makeSampleData (s.left, s.right, s.sampleRate, "starter");
    }

    /** The JS sineWave (f, secs, sr, amp): amp sin (2 pi f i / sr), Float32. */
    std::vector<float> sineWave (double f, double secs, double sr, double amp)
    {
        const auto n = static_cast<std::size_t> (std::llround (secs * sr));
        std::vector<float> x (n);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = static_cast<float> (amp * std::sin (2 * kPiB * f * static_cast<double> (i) / sr));
        return x;
    }

    rig::LaneSpec laneConst (double v)
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

    rig::LaneSpec& laneRef (rig::RawNote& n, core::LaneId id) { return n.lanes[static_cast<std::size_t> (id)]; }

    /** How many heads of a display voice sound (on and not thinned). */
    int soundingHeads (const engine::DisplayVoice& v)
    {
        int n = 0;
        for (int h = 0; h < v.numHeads; ++h)
            n += v.heads[h].thinned ? 0 : 1;
        return n;
    }

    /** The newest voice that is not released (the last one in the engine's voice order), or nullptr. */
    const engine::DisplayVoice* newestHeld (const engine::DisplayFrame& f)
    {
        const engine::DisplayVoice* nv = nullptr;
        for (int i = 0; i < f.numVoices; ++i)
            if (! f.voices[i].released)
                nv = &f.voices[i];
        return nv;
    }

    struct Layout
    {
        const char* name;
        core::Engine engine;
        bool lock;
        int sr;
        int protoMaxLoad, protoThinned;   // the prototype's max load and thinned heads at the end (node)
    };
}

SL_TEST ("engine", budgetLayoutStaysUnderB)
{
    section ("budget: the prototype's 16 notes x 8 heads layout (0.05 beat apart, 120 bpm, the pad) on Voice, Cloud and Lock N = 12 at "
             "44.1, 48 and 96 kHz: the measured max load never passes B = 208 / 192 / 96");

    const Layout layouts[] = {
        { "Voice", core::Engine::voice, false, 44100, 178, 96 }, { "Cloud", core::Engine::cloud, false, 44100, 141, 89 },
        { "Lock", core::Engine::cloud, true, 44100, 147, 117 },  { "Voice", core::Engine::voice, false, 48000, 162, 99 },
        { "Cloud", core::Engine::cloud, false, 48000, 132, 92 }, { "Lock", core::Engine::cloud, true, 48000, 120, 118 },
        { "Voice", core::Engine::voice, false, 96000, 84, 110 }, { "Cloud", core::Engine::cloud, false, 96000, 72, 110 },
        { "Lock", core::Engine::cloud, true, 96000, 80, 120 },
    };
    const core::Motion motions[] = { core::Motion::forward, core::Motion::mirror, core::Motion::scatter, core::Motion::bounce };
    for (const Layout& x : layouts)
    {
        std::vector<rig::RawNote> notes;
        for (int i = 0; i < 16; ++i)
        {
            rig::RawNote n = grains::rawNote (("x" + std::to_string (i)).c_str(), 48 + ((i * 7) % 40), i * 0.05, 6, x.engine);
            n.lock = x.lock;
            n.motion = motions[i % 4];
            laneRef (n, core::LaneId::heads) = laneConst (1);
            laneRef (n, core::LaneId::grain) = laneConst (0.2 + (i % 3) * 0.3);
            const double phase = i;
            laneRef (n, core::LaneId::pitch) = laneCurve ([phase] (double u) { return 0.5 + 0.3 * std::sin (u * 20 + phase); });
            notes.push_back (n);
        }
        core::Inst inst = grains::instWith (x.engine);
        inst.lock = x.lock;
        inst.spread = 1;
        inst.spray = 0.3;
        inst.grainLock = 12;
        grains::Play play (static_cast<double> (x.sr), inst, starterSample (sounds::StarterId::pad));
        play.snapshot (rig::rawSnapshot (notes, 4, false));
        play.play (120, 0, true);
        int mostThinned = 0;
        play.render (static_cast<std::int64_t> (std::llround (0.85 * static_cast<double> (x.sr))), 128,
                     [&play, &mostThinned] (std::int64_t) { mostThinned = std::max (mostThinned, play.e->stats().thinnedHeads); });
        const engine::Stats s = play.e->stats();
        const int wantB = x.sr == 44100 ? 208 : x.sr == 48000 ? 192 : 96;
        const double kHz = x.sr / 1000.0;
        check (s.budget == wantB && s.maxGrains <= s.budget && s.voices == 16,
               fmt ("%s at %.1f kHz: max load %d <= B %d (B expected %d; the prototype's max load %d), 16 voices (%d)", x.name, kHz,
                    s.maxGrains, s.budget, wantB, x.protoMaxLoad, s.voices));
        check (mostThinned > 0, fmt ("%s at %.1f kHz: the budget was under pressure: %d heads held back at the end (the prototype %d), "
                                     "%lld fade tails cut",
                                     x.name, kHz, s.thinnedHeads, x.protoThinned, static_cast<long long> (s.cuts)));
    }
}

SL_TEST ("engine", budgetNewestKeepsItsHeads)
{
    section ("budget: Voice, 8 heads, 6 live keys (C5 up) 80 ms apart at 48 kHz: the newest keeps all 8 heads after every note-on; "
             "the sounding heads never decrease toward newer voices");

    core::Inst inst = grains::instWith (core::Engine::voice);
    inst.heads = 8;
    inst.spread = 0.5;
    grains::Play play (kSr, inst, grains::mono (signals::tone (signals::midiHz (60), 4, kSr), kSr));
    std::string newest, thinned;
    bool allKept = true;
    int thinnedLast = 0;
    for (int i = 0; i < 6; ++i)
    {
        play.e->noteOn (84 + i, 1);
        play.render (static_cast<std::int64_t> (std::llround (0.08 * kSr)));
        const engine::DisplayFrame& f = play.display();
        const engine::DisplayVoice* nv = newestHeld (f);
        bool kept = nv != nullptr && nv->numHeads == 8;
        if (nv != nullptr)
            for (int h = 0; h < nv->numHeads; ++h)
                kept = kept && ! nv->heads[h].thinned && nv->heads[h].level > 0.3f;
        allKept = allKept && kept;
        newest += kept ? " ok" : " THIN";
        thinnedLast = play.e->stats().thinnedHeads;
        thinned += " " + std::to_string (thinnedLast);
    }
    check (allKept, "the newest note keeps all 8 heads (on, not thinned, level > 0.3) after every note-on:" + newest);
    check (thinnedLast > 0, "older notes thin when busy (8 heads x 8 grains at C5): heads held back after each note-on" + thinned
                                + " (the prototype 0 0 6 14 22 30)");
    const engine::DisplayFrame& f = play.display();
    std::string per;
    bool nonDecreasing = true;
    int previous = -1;
    for (int i = 0; i < f.numVoices; ++i)
    {
        const int n = soundingHeads (f.voices[i]);
        per += " " + std::to_string (n);
        nonDecreasing = nonDecreasing && n >= previous;
        previous = n;
    }
    check (nonDecreasing && f.numVoices == 6,
           "the oldest thin first: sounding heads per voice, oldest first," + per + " (non-decreasing; the prototype 1 1 1 1 6 8)");
    const engine::Stats s = play.e->stats();
    check (s.maxGrains <= s.budget && s.cuts == 0,
           fmt ("max load %d <= B %d (the prototype 192), %lld cuts", s.maxGrains, s.budget, static_cast<long long> (s.cuts)));
}

SL_TEST ("engine", budgetNoCutsWhereThePrototypeHasNone)
{
    section ("budget: cuts = 0 where the prototype measured none: Lock 8 heads Grain 4 and Voice 8 heads, arpeggios of 6 notes 0.1 s "
             "apart; Strand Demo with recipes 3, 6 and 8 at 8 heads (the pad)");

    struct Arp
    {
        const char* name;
        core::Engine engine;
        bool lock;
        int keys[6];
        int protoMaxLoad, protoThinned;
    };
    const Arp arps[] = { { "Lock, 8 heads, Grain 4", core::Engine::cloud, true, { 48, 52, 55, 60, 64, 67 }, 144, 30 },
                         { "Voice, 8 heads, C4..C5", core::Engine::voice, false, { 72, 74, 76, 77, 79, 84 }, 162, 29 } };
    for (const Arp& a : arps)
    {
        core::Inst inst = grains::instWith (a.engine);
        inst.lock = a.lock;
        inst.heads = 8;
        inst.spread = 0.5;
        inst.outGain = -12;
        grains::Play play (kSr, inst, grains::mono (sineWave (261.6256, 4, kSr, 0.5), kSr));
        std::size_t idx = 0;
        int mostThinned = 0;
        // the JS hook: after the block that ends on a multiple of 4800 samples, the next note
        play.render (static_cast<std::int64_t> (std::llround (1.2 * kSr)), 32, [&] (std::int64_t i) {
            if (i % 4800 == 0 && idx < 6)
                play.e->noteOn (a.keys[idx++], 1);
            mostThinned = std::max (mostThinned, play.e->stats().thinnedHeads);
        });
        const engine::Stats s = play.e->stats();
        check (s.cuts == 0 && s.maxGrains <= s.budget && mostThinned > 0,
               fmt ("%s: %lld cuts (expected 0), max load %d <= B %d (the prototype %d), heads held back %d (the prototype %d; > 0: the "
                    "budget was under pressure)",
                    a.name, static_cast<long long> (s.cuts), s.maxGrains, s.budget, a.protoMaxLoad, s.thinnedHeads, a.protoThinned));
    }

    const auto demo = model::loadClipPreset ("strand-demo");
    struct Rec
    {
        int n;
        double grainLock;
        int protoMaxLoad, protoThinned;
    };
    const Rec recs[] = { { 3, 0, 113, 13 }, { 6, 0, 117, 4 }, { 8, 12, 192, 32 } };
    for (const Rec& r : recs)
    {
        const auto applied = model::applyRecipe (*demo, core::defaultInst(), r.n, model::Ids::all());
        if (! applied)
        {
            check (false, fmt ("recipe %d applies", r.n));
            continue;
        }
        core::Inst inst = applied->inst;
        inst.heads = 8;
        if (r.grainLock > 0)
            inst.grainLock = r.grainLock;
        inst.outGain = -12;
        grains::Play play (kSr, inst, starterSample (sounds::StarterId::pad));
        play.snapshot (render::compile (applied->clip));
        play.play (120, 0, true);
        int mostThinned = 0;
        play.render (static_cast<std::int64_t> (3 * kSr), 128,
                     [&play, &mostThinned] (std::int64_t) { mostThinned = std::max (mostThinned, play.e->stats().thinnedHeads); });
        const engine::Stats s = play.e->stats();
        check (s.cuts == 0 && s.maxGrains <= s.budget && mostThinned > 0,
               fmt ("Strand Demo, recipe %d, 8 heads%s: %lld cuts (expected 0), max load %d <= B %d (the prototype %d), heads held back %d "
                    "(the prototype %d)",
                    r.n, r.grainLock > 0 ? ", Grain 12" : "", static_cast<long long> (s.cuts), s.maxGrains, s.budget, r.protoMaxLoad,
                    s.thinnedHeads, r.protoThinned));
    }
}

SL_TEST ("engine", budgetLockFixtureStats)
{
    section ("budget: Lock N = 12 x 8 heads (the five budget-* cases of engine/renders.json): cuts, thinned heads, max load and B equal "
             "the prototype's at every stats row; the newest note sounds 8 / 8 / 4 heads at 44.1 / 48 / 96 kHz");

    core::JsonValue doc;
    if (! fixture::load ("engine/renders.json", doc))
        return;
    std::map<std::string, model::Clip> clips;
    if (! modelfx::clipsFrom (doc, clips))
        return;
    // The newest note's sounding heads at the end, from the prototype's display (node, the same cases with their display)
    const std::map<std::string, int> newestWanted = { { "budget-16x8-lock12-48k", 8 },
                                                      { "budget-16x8-lock12-96k", 4 },
                                                      { "budget-16x8-lock12-clip-44.1k", 8 },
                                                      { "budget-16x8-lock12-clip-48k", 8 },
                                                      { "budget-16x8-lock12-clip-96k", 4 } };
    const core::JsonValue& cases = doc["cases"];
    int ran = 0;
    for (std::size_t c = 0; c < cases.size(); ++c)
    {
        const core::JsonValue& k = cases[c];
        const std::string name = k["name"].asString();
        const auto want = newestWanted.find (name);
        if (want == newestWanted.end())
            continue;
        ++ran;
        render::Rendered r;
        std::string why;
        if (! render::renderFixtureCase (k, clips, r, why))
        {
            check (false, name + ": " + why);
            continue;
        }
        const core::JsonValue& rows = k["out"]["stats"];
        bool same = rows.size() == r.stats.size() && rows.size() > 0;
        std::string first;
        for (std::size_t i = 0; same && i < rows.size(); ++i)
            for (const std::size_t col : { std::size_t { 3 }, std::size_t { 4 }, std::size_t { 5 }, std::size_t { 6 } })
                if (static_cast<std::int64_t> (rows[i][col].asNumber()) != r.stats[i][col])
                {
                    same = false;
                    first = fmt (" (row %zu, column %zu: %lld against the prototype's %lld)", i, col, static_cast<long long> (r.stats[i][col]),
                                 static_cast<long long> (rows[i][col].asNumber()));
                    break;
                }
        const core::JsonValue& last = rows[rows.size() - 1];
        const auto protoCuts = static_cast<long long> (last[6].asNumber());
        const auto& mine = r.stats.empty() ? std::vector<std::int64_t> (8, -1) : r.stats.back();
        check (same && protoCuts > 0,
               fmt ("%s: at every one of %zu stats rows the thinned heads, max load, B and cuts equal the prototype's%s; at the end %lld cuts, "
                    "%lld heads held back, max load %lld of %lld (the prototype: %lld, %lld, %lld of %lld)",
                    name.c_str(), rows.size(), first.c_str(), static_cast<long long> (mine[6]), static_cast<long long> (mine[3]),
                    static_cast<long long> (mine[4]), static_cast<long long> (mine[5]), protoCuts, static_cast<long long> (last[3].asNumber()),
                    static_cast<long long> (last[4].asNumber()), static_cast<long long> (last[5].asNumber())));
        const engine::DisplayVoice* nv = r.display.empty() ? nullptr : newestHeld (r.display.back());
        const int heads = nv != nullptr ? soundingHeads (*nv) : -1;
        check (heads == want->second, fmt ("%s: the newest note sounds %d heads at the end (the prototype %d)", name.c_str(), heads, want->second));
    }
    check (ran == 5, fmt ("5 budget-* cases ran (%d)", ran));
}

SL_TEST ("engine", budgetEdgeNoAllocations)
{
    section (std::string ("budget at its edge allocates nothing: 16 Lock N = 12 keys x 8 heads at 48 and 96 kHz (thinning, waiting heads, "
                          "cuts) and the Voice arpeggio (")
             + alloc::coverage() + ")");

    auto sample = engine::makeSampleData (signals::tone (signals::midiHz (60), 4, kSr), {}, kSr, "tone");
    core::Inst lockInst = grains::instWith (core::Engine::cloud);
    lockInst.lock = true;
    lockInst.heads = 8;
    lockInst.grainLock = 12;
    lockInst.polyphony = 16;
    core::Inst voiceInst = grains::instWith (core::Engine::voice);
    voiceInst.heads = 8;
    voiceInst.spread = 0.5;
    std::vector<std::unique_ptr<engine::Engine>> engines;
    const double rates[] = { 48000, 96000, 48000 };
    for (int i = 0; i < 3; ++i)
    {
        engines.push_back (std::make_unique<engine::Engine>());
        engines.back()->prepare (rates[i]);
        engines.back()->setInst (i < 2 ? lockInst : voiceInst);
    }
    auto frame = std::make_unique<engine::DisplayFrame>();
    std::vector<float> L (256), R (256);
    long long cuts[3] = { 0, 0, 0 };
    int thinned[3] = { 0, 0, 0 };

    alloc::reset();
    {
        const alloc::Scope counting;
        for (int i = 0; i < 3; ++i)
        {
            engine::Engine& e = *engines[static_cast<std::size_t> (i)];
            e.setSample (sample.get(), 1);
            const int keys = i < 2 ? 16 : 6;
            const int apart = i < 2 ? 256 : 3840;
            for (int key = 0; key < keys; ++key)
            {
                e.noteOn (i < 2 ? 36 + 2 * key : 84 + key, 1);
                for (int done = 0; done < apart; done += 128)
                {
                    e.process (L.data(), R.data(), 128);
                    thinned[i] = std::max (thinned[i], e.stats().thinnedHeads);
                }
            }
            for (int b = 0; b < 200; ++b)
            {
                e.process (L.data(), R.data(), 256);
                if (b % 20 == 0)
                    e.fillDisplay (*frame);
            }
            e.allNotesOff();
            e.process (L.data(), R.data(), 256);
            cuts[i] = static_cast<long long> (e.stats().cuts);
        }
    }
    const alloc::Counts c = alloc::counts();
    check (c.news == 0 && c.deletes == 0, fmt ("0 allocations and 0 frees (%lld and %lld)", c.news, c.deletes));
    check (cuts[0] > 0 && cuts[1] > 0 && thinned[0] > 0 && thinned[1] > 0 && thinned[2] > 0,
           fmt ("and the planner's every path ran: Lock at 48 kHz %lld cuts and %d heads held back, at 96 kHz %lld and %d; the Voice "
                "arpeggio %d held back",
                cuts[0], thinned[0], cuts[1], thinned[1], thinned[2]));
}

SL_TEST ("alloc", budgetEdgeNoAllocationsAlloc)
{
    section ("the same budget-edge allocation row, in the allocation group (the Debug-CRT run)");
    auto sample = engine::makeSampleData (signals::tone (signals::midiHz (60), 2, kSr), {}, kSr, "tone");
    core::Inst inst = grains::instWith (core::Engine::cloud);
    inst.lock = true;
    inst.heads = 8;
    inst.grainLock = 12;
    inst.polyphony = 16;
    auto e = std::make_unique<engine::Engine>();
    e->prepare (96000);
    e->setInst (inst);
    std::vector<float> L (128), R (128);
    alloc::reset();
    {
        const alloc::Scope counting;
        e->setSample (sample.get(), 1);
        for (int key = 0; key < 16; ++key)
        {
            e->noteOn (36 + 2 * key, 1);
            e->process (L.data(), R.data(), 128);
            e->process (L.data(), R.data(), 128);
        }
        for (int b = 0; b < 400; ++b)
            e->process (L.data(), R.data(), 128);
    }
    const alloc::Counts c = alloc::counts();
    check (c.news == 0 && c.deletes == 0, fmt ("0 allocations and 0 frees (%lld and %lld)", c.news, c.deletes));
    check (e->stats().cuts > 0 && e->stats().thinnedHeads > 0,
           fmt ("with cuts (%lld) and thinned heads (%d)", static_cast<long long> (e->stats().cuts), e->stats().thinnedHeads));
}
