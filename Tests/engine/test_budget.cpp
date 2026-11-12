// The budget (PLUGIN-PLAN 1.8 D3, 4 M3 "budget", M3 part B), written from what the prototype does
// (prototype-v4/tests/test-engine.js "budget thinning", test-engine-extra.js section M, test-integration.js J):
//  - the prototype's 16 notes x 8 heads layout (notes 0.05 beat apart at 120 bpm, the pad starter; Voice, Cloud and
//    Lock N = 12; 44.1, 48 and 96 kHz): the measured max load (stats ().maxGrains) never passes B (208, 192, 96);
//  - Voice, 8 heads, 6 live keys 80 ms apart at 48 kHz: the newest note keeps all 8 heads after every note-on, and
//    the sounding heads never decrease toward newer voices (the oldest thin first); and the heads that wait for the
//    fade tails come in within 20 ms (plan 1.8 "waiting heads");
//  - cuts = 0 where the prototype measured none: Lock 8 heads Grain 4 and Voice 8 heads, arpeggios of 6 notes
//    0.1 s apart, and Strand Demo with recipes 3, 6 and 8 at 8 heads;
//  - Lock N = 12 x 8 heads, the five budget-* cases of engine/renders.json: cuts, thinned heads, max load and B at
//    every stats row equal the prototype's, and the newest note sounds 8 / 8 / 4 heads at 44.1 / 48 / 96 kHz
//    (read from the display at the end, as the prototype shows it);
//  - and at that edge (thinning, waiting heads and cuts all running) the engine allocates nothing.
// Each scenario also checks that the budget was under pressure (heads thinned), so none passes by doing nothing.
// The prototype's own figures (computed in node from sl-engine.js) are gated exactly since the M3 fix pass (review
// R3-LB-1): max load, heads held back and cuts of every layout, arpeggio and recipe; the arpeggio's heads held back
// after each note-on, its waits and its heads per voice. The fix pass also adds rows for the rules no figure above
// reached (each one turns red when its rule is broken in the prototype, checked in node with a mutated planner):
//  - the thinned heads come back, newest voice first and only up to 0.9 x floor (0.75 B), when voices end;
//  - quietest first: a silent voice (Volume 0) thins before a louder older one, a faint one (-45 dB) does not;
//  - released first: a released voice thins before an older held one;
//  - two keys at one sample (a dyad) after an arpeggio: the wait list is newest first, and the last-resort cuts take
//    only tails, released voices first, then the oldest;
//  - Play inside a held Lock chord whose chase is spread over blocks: no cuts (review R3-LB-2).
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
#include "core/Seeds.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"
#include "model/Clip.h"
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
#include <string_view>
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
        int protoMaxLoad, protoThinned, protoCuts;   // the prototype's max load, thinned heads and cuts at the end (node)
    };

    /** The display's voices, oldest first: "r" for a released one, its sounding heads, "/", its heads that are on. */
    std::string headsText (const engine::DisplayFrame& f)
    {
        std::string s;
        for (int i = 0; i < f.numVoices; ++i)
        {
            if (i > 0)
                s += ",";
            s += std::string (f.voices[i].released ? "r" : "") + std::to_string (soundingHeads (f.voices[i])) + "/"
                 + std::to_string (static_cast<int> (f.voices[i].numHeads));
        }
        return s;
    }

    /** Renders n samples in 32-sample blocks and returns headsText every `every` samples, " | " between. */
    std::string probeHeads (grains::Play& play, std::int64_t n, std::int64_t every)
    {
        std::string out;
        std::int64_t done = 0;
        play.render (n, 32, [&play, &out, &done, every] (std::int64_t) {
            done += 32;
            if (done % every == 0)
                out += (out.empty() ? "" : " | ") + headsText (play.display());
        });
        return out;
    }

    /** One engine as the arpeggio rows play it: Voice, 8 heads, Spread 0.5, the C3 tone. */
    std::unique_ptr<grains::Play> voicePlay (double sr)
    {
        core::Inst inst = grains::instWith (core::Engine::voice);
        inst.heads = 8;
        inst.spread = 0.5;
        return std::make_unique<grains::Play> (sr, inst, grains::mono (signals::tone (signals::midiHz (60), 4, sr), sr));
    }
}

SL_TEST ("engine", budgetLayoutStaysUnderB)
{
    section ("budget: the prototype's 16 notes x 8 heads layout (0.05 beat apart, 120 bpm, the pad) on Voice, Cloud and Lock N = 12 at "
             "44.1, 48 and 96 kHz: the measured max load never passes B = 208 / 192 / 96");

    const Layout layouts[] = {
        { "Voice", core::Engine::voice, false, 44100, 178, 96, 0 }, { "Cloud", core::Engine::cloud, false, 44100, 141, 89, 0 },
        { "Lock", core::Engine::cloud, true, 44100, 147, 117, 5 },  { "Voice", core::Engine::voice, false, 48000, 162, 99, 0 },
        { "Cloud", core::Engine::cloud, false, 48000, 132, 92, 0 }, { "Lock", core::Engine::cloud, true, 48000, 120, 118, 10 },
        { "Voice", core::Engine::voice, false, 96000, 84, 110, 0 }, { "Cloud", core::Engine::cloud, false, 96000, 72, 110, 0 },
        { "Lock", core::Engine::cloud, true, 96000, 80, 120, 15 },
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
        check (mostThinned > 0, fmt ("%s at %.1f kHz: the budget was under pressure: %d heads held back at the most", x.name, kHz, mostThinned));
        check (s.maxGrains == x.protoMaxLoad && s.thinnedHeads == x.protoThinned && s.cuts == x.protoCuts,
               fmt ("%s at %.1f kHz: max load %d, %d heads held back at the end and %lld fade tails cut, the prototype's exactly (%d, %d, %d)", x.name,
                    kHz, s.maxGrains, s.thinnedHeads, static_cast<long long> (s.cuts), x.protoMaxLoad, x.protoThinned, x.protoCuts));
    }
}

SL_TEST ("engine", budgetNewestKeepsItsHeads)
{
    section ("budget: Voice, 8 heads, 6 live keys (C5 up) 80 ms apart at 48 kHz: the newest keeps all 8 heads after every note-on; "
             "the sounding heads never decrease toward newer voices; waiting heads come in within 20 ms; then the 3 newest keys let go: "
             "the thinned heads come back, newest voice first, up to 0.9 x floor (0.75 B)");

    const std::unique_ptr<grains::Play> owner = voicePlay (kSr);
    grains::Play& play = *owner;
    std::string newest, thinned, waits;
    bool allKept = true, allCameIn = true;
    int thinnedLast = 0;
    std::int64_t longestWait = 0;
    for (int i = 0; i < 6; ++i)
    {
        play.e->noteOn (84 + i, 1);
        // Waiting heads (plan 1.8): a head that has not sounded yet waits at most 20 ms for the fade tails to leave
        // room. The display is read every 32 samples (one control tick) until the newest shows all 8 heads.
        const auto from = static_cast<std::int64_t> (play.L.size());
        std::int64_t cameIn = -1;
        play.render (static_cast<std::int64_t> (std::llround (0.08 * kSr)), 32, [&play, &cameIn, from] (std::int64_t done) {
            if (cameIn >= 0)
                return;
            const engine::DisplayVoice* held = newestHeld (play.display());
            if (held != nullptr && held->numHeads == 8 && soundingHeads (*held) == 8)
                cameIn = done - from;
        });
        allCameIn = allCameIn && cameIn >= 0 && cameIn <= 960 + 32;
        longestWait = std::max (longestWait, cameIn);
        waits += " " + std::to_string (cameIn);
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
    check (allCameIn && longestWait > 32 && waits == " 32 32 32 448 448 448",
           "waiting heads come in within 20 ms: samples from each note-on until the newest sounds all 8 heads" + waits
               + " (each <= 960 + one 32-sample tick, and > 32 at least once, so some heads waited; the prototype's exactly: 32 32 32 448 448 448)");
    check (thinnedLast > 0 && thinned == " 0 0 6 14 22 30", "older notes thin when busy (8 heads x 8 grains at C5): heads held back after each note-on"
                                                                + thinned + " (the prototype's exactly: 0 0 6 14 22 30)");
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
    check (nonDecreasing && f.numVoices == 6 && per == " 1 1 1 1 6 8",
           "the oldest thin first: sounding heads per voice, oldest first," + per + " (non-decreasing; the prototype's exactly: 1 1 1 1 6 8)");
    const engine::Stats s = play.e->stats();
    check (s.maxGrains <= s.budget && s.maxGrains == 192 && s.cuts == 0,
           fmt ("max load %d <= B %d (the prototype's 192), %lld cuts (the prototype 0)", s.maxGrains, s.budget, static_cast<long long> (s.cuts)));

    // Bring-back (plan 1.8 "room again"): the 3 newest keys let go; once their 60 ms releases have ended, the 3 older
    // voices' thinned heads come back, newest voice first, while the steady load stays within 0.9 x floor (0.75 B)
    // (129.6 at 48 kHz): 1 + 7 + 8 heads of 8 grains is 128. The display every 100 ms, oldest voice first: sounding
    // heads / heads on. Broken in node, the rule gives: no bring-back 1/1,1/1,1/1; oldest first 8/8,7/7,1/1; no
    // hysteresis 2/2,8/8,8/8.
    for (const int p : { 87, 88, 89 })
        play.e->noteOff (p);
    const std::string back = probeHeads (play, 14400, 4800);
    const engine::Stats sb = play.e->stats();
    check (back == "1/1,7/7,8/8 | 1/1,7/7,8/8 | 1/1,7/7,8/8" && sb.thinnedHeads == 8 && sb.cuts == 0,
           fmt ("bring-back: 100, 200 and 300 ms after the 3 newest keys let go, the voices sound %s heads, %d held back, %lld cuts (the prototype's "
                "exactly: 1/1,7/7,8/8 three times, 8, 0)",
                back.c_str(), sb.thinnedHeads, static_cast<long long> (sb.cuts)));
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
    // the max load and heads held back at the end are the prototype's exactly (since the M3 fix pass)
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
        check (s.cuts == 0 && s.maxGrains <= s.budget && mostThinned > 0 && s.maxGrains == a.protoMaxLoad && s.thinnedHeads == a.protoThinned,
               fmt ("%s: %lld cuts (expected 0), max load %d <= B %d (the prototype's exactly: %d), heads held back %d (the prototype's exactly: "
                    "%d; > 0: the budget was under pressure)",
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
        check (s.cuts == 0 && s.maxGrains <= s.budget && mostThinned > 0 && s.maxGrains == r.protoMaxLoad && s.thinnedHeads == r.protoThinned,
               fmt ("Strand Demo, recipe %d, 8 heads%s: %lld cuts (expected 0), max load %d <= B %d (the prototype's exactly: %d), heads held "
                    "back %d (the prototype's exactly: %d)",
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

SL_TEST ("engine", budgetThinningOrder)
{
    section ("budget: the thinning and cut order of plan 1.8 (M3 review R3-LB-1): quietest first, released first, the wait list newest "
             "first, and the last-resort cuts released first, then the oldest; every figure the prototype's exactly");

    // Each display text below lists the voices oldest first: "r" for a released voice, its sounding heads, "/", its heads that
    // are on (a thinned head stays on while it fades). The "broken" figures are the prototype's with that one rule broken
    // (node, a mutated sl-engine.js plan ()), so each check turns red when its rule breaks.

    {   // quietest first (test-engine-extra.js M, with a faint voice added): Voice, 8 heads, clip notes 0.25 beat apart: loud,
        // faint (Volume 0.04: level x env 5.9e-3, over the -60 dB line 1e-3), silent (Volume 0), new. 4 x 8 heads of 8 grains
        // is 256 against floor (0.75 B) = 144: 14 heads must go. The silent voice gives its top heads first, then the oldest
        // audible one (loud), never the faint one. Broken: no quiet rule 1/1,1/1,8/8,8/8; a -40 dB line 8/8,1/1,1/1,8/8.
        std::vector<rig::RawNote> notes;
        const char* ids[4] = { "loud", "faint", "silent", "new" };
        const double keys[4] = { 84, 85, 83, 82 };
        for (int i = 0; i < 4; ++i)
            notes.push_back (grains::rawNote (ids[i], keys[i], i * 0.25, 6, core::Engine::voice));
        laneRef (notes[1], core::LaneId::amp) = laneConst (0.04);
        laneRef (notes[2], core::LaneId::amp) = laneConst (0);
        core::Inst inst = grains::instWith (core::Engine::voice);
        inst.heads = 8;
        inst.spread = 0.5;
        grains::Play play (kSr, inst, grains::mono (sineWave (261.6, 4, kSr, 1), kSr));
        play.snapshot (rig::rawSnapshot (notes, 4, false));
        play.play (120, 0, false);
        play.render (36000, 128);   // 1.5 beats
        const engine::DisplayFrame& f = play.display();
        bool inOrder = f.numVoices == 4;
        for (int i = 0; inOrder && i < 4; ++i)
            inOrder = f.voices[i].noteIdHash == core::idHash (std::string_view (ids[i]));
        const std::string got = headsText (f);
        const engine::Stats s = play.e->stats();
        check (inOrder && got == "1/1,8/8,1/1,8/8" && s.thinnedHeads == 14 && s.cuts == 0,
               fmt ("quietest first: loud, faint, silent, new sound %s heads, %d held back, %lld cuts (the prototype's exactly: 1/1,8/8,1/1,8/8, "
                    "14, 0)",
                    got.c_str(), s.thinnedHeads, static_cast<long long> (s.cuts)));
    }
    {   // released first: live keys C5 held, D5 50 ms later and let go at 100 ms, E5 at 110 ms. The released D5 gives up 6 heads
        // in its release; the older C5, held, keeps all 8. The display 10, 20 and 30 ms after E5. Broken (released voices not
        // first): 2/2,r8/8,8/8.
        const std::unique_ptr<grains::Play> owner = voicePlay (kSr);
        grains::Play& play = *owner;
        play.e->noteOn (84, 1);
        play.render (2400, 32);
        play.e->noteOn (86, 1);
        play.render (2400, 32);
        play.e->noteOff (86);
        play.render (480, 32);
        play.e->noteOn (88, 1);
        const std::string got = probeHeads (play, 1440, 480);
        const engine::Stats s = play.e->stats();
        check (got == "8/8,r2/2,8/8 | 8/8,r2/2,8/8 | 8/8,r2/2,8/8" && s.thinnedHeads == 6 && s.cuts == 0,
               fmt ("released first: 10, 20 and 30 ms after the third key the voices sound %s heads, %d held back, %lld cuts (the prototype's "
                    "exactly: 8/8,r2/2,8/8 three times, 6, 0)",
                    got.c_str(), s.thinnedHeads, static_cast<long long> (s.cuts)));
    }
    {   // a dyad after an arpeggio: Voice, 8 heads, C5 C#5 D5 D#5 80 ms apart, then E5 and F5 at one sample. The older voices'
        // thinned heads still fade, so the dyad's heads wait: the newest (F5) first, then E5 (the wait list newest first), and
        // the new voices' first heads push the load past B, so 2 tails are cut: tails only, oldest first. The display every
        // 160 samples. Broken: the wait list oldest first or unsorted ...,1/8,6/8,1/1,5/5 with 0 cuts; the cuts newest first
        // ...,1/8,6/6,1/1,7/7; cuts that take sounding heads too 0/0,0/0,...
        const std::unique_ptr<grains::Play> owner = voicePlay (kSr);
        grains::Play& play = *owner;
        for (const int p : { 84, 85, 86, 87 })
        {
            play.e->noteOn (p, 1);
            play.render (3840, 32);
        }
        play.e->noteOn (88, 1);
        play.e->noteOn (89, 1);
        const std::string got = probeHeads (play, 640, 160);
        const engine::Stats s = play.e->stats();
        check (got == "1/1,1/1,1/6,6/8,1/1,7/7 | 1/1,1/1,1/6,6/8,1/1,7/7 | 1/1,1/1,1/1,6/6,1/1,8/8 | 1/1,1/1,1/1,6/6,1/1,8/8" && s.cuts == 2,
               fmt ("a dyad after an arpeggio: every 160 samples the voices sound %s heads, %lld tails cut (the prototype's exactly: "
                    "1/1,1/1,1/6,6/8,1/1,7/7 twice, then 1/1,1/1,1/1,6/6,1/1,8/8 twice; 2 cuts)",
                    got.c_str(), static_cast<long long> (s.cuts)));
    }
    {   // the cut order, released first: 96 kHz (B = 96), Voice, 8 heads: C5 held, D5 50 ms later and let go 2 ms after, E5 1 ms
        // after that. E5's first head does not fit beside the tails, so one tail is cut: D5's (released), not C5's (older).
        // Broken (cuts by age only): 1/7,r0/4,...
        const std::unique_ptr<grains::Play> owner = voicePlay (96000);
        grains::Play& play = *owner;
        play.e->noteOn (84, 1);
        play.render (4800, 32);
        play.e->noteOn (86, 1);
        play.render (192, 32);
        play.e->noteOff (86);
        play.render (96, 32);
        play.e->noteOn (88, 1);
        const std::string got = probeHeads (play, 640, 160);
        const engine::Stats s = play.e->stats();
        check (got == "1/8,r0/3,1/1 | 1/8,r0/3,1/1 | 1/6,r0/3,1/1 | 1/1,r0/3,8/8" && s.cuts == 1,
               fmt ("cuts take released tails first: every 160 samples after the third key the voices sound %s heads, %lld cut (the prototype's "
                    "exactly: 1/8,r0/3,1/1 twice, 1/6,r0/3,1/1, 1/1,r0/3,8/8; 1 cut)",
                    got.c_str(), static_cast<long long> (s.cuts)));
    }
}

SL_TEST ("engine", budgetChaseInsideLockChord)
{
    section ("budget: Play inside a held Lock N = 12 x 8-head chord whose chase is spread over blocks (8.2 #22; M3 review R3-LB-2): no cuts, "
             "the load within B, and the chord's last note sounds 4 / 8 heads at 96 / 48 kHz at the end, as in the prototype");

    // The prototype chases every voice at the same sample; the plugin's bounded chase (4096 ticks at once, then 256 per 32
    // samples) starts the late voices one by one. Before the fix pass the planner passed the chasing voices over, so each late
    // voice became the newest when it caught up, the earlier ones had already sounded and faded instead of going silent, and
    // its first head pushed the load past B: 2, 6 and 4 cuts here (node, the C++ scheduler copied onto sl-engine.js). Now a
    // chasing voice counts as waiting: it can be the newest, and the others leave room for it from Play on.
    struct Chord
    {
        int sr;
        int notes;
        double beat;
        int protoThinned, protoMax, lastHeads;
        long long cutsBefore;
    };
    const Chord chords[] = { { 96000, 4, 1, 28, 96, 4, 2 }, { 96000, 8, 1, 60, 96, 4, 6 }, { 48000, 8, 2, 56, 192, 8, 4 } };
    for (const Chord& c : chords)
    {
        model::Clip clip = model::newClip (4);
        for (int i = 0; i < c.notes; ++i)
            clip.notes.push_back (std::make_shared<const model::Note> (model::mkNote (48 + 3 * i, 0, 8, "c" + std::to_string (i))));
        core::Inst inst = grains::instWith (core::Engine::cloud);
        inst.lock = true;
        inst.heads = 8;
        inst.grainLock = 12;
        inst.polyphony = 16;
        const auto sr = static_cast<double> (c.sr);
        grains::Play play (sr, inst, starterSample (sounds::StarterId::pad));
        play.snapshot (render::compile (clip));
        play.play (120, c.beat, true);
        play.render (static_cast<std::int64_t> (std::llround (0.5 * sr)), 128);
        const engine::Stats s = play.e->stats();
        const engine::DisplayVoice* nv = newestHeld (play.display());
        const bool last = nv != nullptr && nv->noteIdHash == core::idHash (std::string_view ("c" + std::to_string (c.notes - 1)));
        const int heads = nv != nullptr ? soundingHeads (*nv) : -1;
        const char* what = c.notes == 4 ? "4-note" : "8-note";
        check (s.chaseTicks > 4096 && s.thinnedHeads > 0,
               fmt ("%s chord at %.0f kHz, Play at beat %.0f: the chase was spread over blocks (%lld ticks, more than 4096 at once) and the "
                    "budget under pressure (%d heads held back; the prototype %d)",
                    what, sr / 1000, c.beat, static_cast<long long> (s.chaseTicks), s.thinnedHeads, c.protoThinned));
        check (s.cuts == 0 && s.maxGrains <= s.budget,
               fmt ("%s chord at %.0f kHz: %lld cuts (expected 0, as the prototype; %lld before the fix pass), max load %d <= B %d (the "
                    "prototype %d)",
                    what, sr / 1000, static_cast<long long> (s.cuts), c.cutsBefore, s.maxGrains, s.budget, c.protoMax));
        check (last && heads == c.lastHeads, fmt ("%s chord at %.0f kHz: the chord's last note is the newest and sounds %d heads at the end (the "
                                                  "prototype %d)",
                                                  what, sr / 1000, heads, c.lastHeads));
    }
}
