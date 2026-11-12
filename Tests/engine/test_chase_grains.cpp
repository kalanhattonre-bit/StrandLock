// The chase on the grain engines (PLUGIN-PLAN 4, M3: "chase, grain engines"): Play pressed at beat 1.37 inside a
// 4-beat Voice note, a 4-beat Cloud note (part G) and a 4-beat Lock note (Cloud with the Lock switch, part L), at
// 120 bpm on the 8 s ramp source. A chased grain voice starts its grains (or its Lock capture) on a new phase, so its
// samples differ from the straight render's (M2's Tape row compares samples; this one compares the playhead): head
// 1's position on the display 2432 samples after Play is within 0.002 of the straight render's at the same song
// sample, and the 10 ms chase fade-in gives rms (0 .. 2.5 ms) / rms (12.5 .. 15 ms) in 0.02 .. 0.3 (the JS
// criteria). Group "engine".
//
// chaseGrainsPathFarIn (M3 fix pass, review R3-GP-1): Play far into a 16-beat Path-kind note, where the bounded chase
// takes coarse steps (8.2 #22). A Path head steers to its target each tick, and the coarse step used to move it 512
// samples at that steering speed, so its position ran to infinity and NaN and the note played frame 0 of the file.

#include "engine/GrainRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"

#include <cmath>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000;
    constexpr std::int64_t kAfter = 2432;   // the playhead is compared this many samples after Play

    struct Probe
    {
        double pos = std::nan ("");   // head 1's display position
        double decoded = std::nan (""); // the read position the audio says (decodeAt over +-100 samples at 2400)
        double fade = 0;              // rms (0 .. 120) / rms (600 .. 720) after Play
        float first = 0;
        int voices = 0;
        int grains = 0;               // grains (or Lock captures) on the display
    };

    /** One 4-beat note from beat 0 on eng (lock: Cloud with the Lock switch, on the note and the instrument); Play
        at `beat`, `after` samples rendered. */
    Probe playFrom (core::Engine eng, bool lock, double beat, std::int64_t after)
    {
        core::Inst inst = grains::instWith (eng);
        inst.lock = lock;
        rig::RawNote n = grains::rawNote ("ch", 60, 0, 4, eng);
        n.lock = lock;
        grains::Play play (kSr, inst, grains::ramp (8, kSr));
        play.snapshot (rig::rawSnapshot ({ n }, 4, false));
        play.play (120, beat, false);
        play.render (after);
        Probe p;
        const engine::DisplayFrame& f = play.display();
        p.voices = f.numVoices;
        p.grains = f.numGrains;
        if (f.numVoices == 1 && f.voices[0].numHeads > 0 && f.voices[0].heads[0].index == 0)
            p.pos = f.voices[0].heads[0].pos;
        if (after >= 2600)
        {
            p.decoded = play.decodeAt (2400, 100);
            p.fade = rig::rms (play.L, 0, 120) / rig::rms (play.L, 600, 720);
            p.first = play.L[0];
        }
        return p;
    }

    struct PathProbe
    {
        double pos = std::nan ("");       // head 1's display position `at` samples after Play
        double decoded = std::nan ("");   // the read position the audio says at 2400 samples (+-100)
        std::int64_t chaseTicks = 0;      // stats ().chaseTicks
        int voices = 0;
    };

    /** One 16-beat Path-kind note (Travel 0.1 -> 0.9 of the Region) from beat 0 at sr, on eng (lock: Cloud with the
        Lock switch); Play at `beat`; the display read `at` samples after Play, `total` samples rendered in all. */
    PathProbe playPathFrom (double sr, core::Engine eng, bool lock, double beat, std::int64_t at, std::int64_t total)
    {
        core::Inst inst = grains::instWith (eng);
        inst.lock = lock;
        rig::RawNote n = grains::rawNote ("pk", 60, 0, 16, eng);
        n.lock = lock;
        n.travel = 1;   // Path
        rig::LaneSpec lane;
        lane.set = true;
        lane.fn = [] (double u) { return 0.1 + 0.8 * u; };
        n.lanes[static_cast<std::size_t> (core::LaneId::travel)] = lane;
        grains::Play play (sr, inst, grains::ramp (8, sr));
        play.snapshot (rig::rawSnapshot ({ n }, 5, false));
        play.play (120, beat, false);
        play.render (at);
        PathProbe p;
        const engine::DisplayFrame& f = play.display();
        p.voices = f.numVoices;
        if (f.numVoices == 1 && f.voices[0].numHeads > 0 && f.voices[0].heads[0].index == 0)
            p.pos = f.voices[0].heads[0].pos;
        if (total > at)
        {
            play.render (total - at);
            p.decoded = play.decodeAt (2400, 100);
        }
        p.chaseTicks = play.e->stats().chaseTicks;
        return p;
    }
}

SL_TEST ("engine", chaseGrainsAt137)
{
    section ("chase, grain engines: Play at beat 1.37 inside Voice, Cloud and Lock notes: playhead within 0.002 of the straight render, "
             "10 ms fade-in");

    const double beat = 1.37;
    const auto kPlay = static_cast<std::int64_t> (std::round (beat * 60 / 120.0 * kSr));   // 32880: beat 1.37 of the straight render
    struct Kind
    {
        core::Engine eng;
        bool lock;
        const char* name;
    };
    for (const Kind& kind : { Kind { core::Engine::voice, false, "Voice" }, Kind { core::Engine::cloud, false, "Cloud" },
                              Kind { core::Engine::cloud, true, "Lock" } })
    {
        const char* name = kind.name;
        const Probe chased = playFrom (kind.eng, kind.lock, beat, kAfter);
        const Probe chasedAudio = playFrom (kind.eng, kind.lock, beat, static_cast<std::int64_t> (0.2 * kSr));
        const Probe straight = playFrom (kind.eng, kind.lock, 0, kPlay + kAfter);
        const double d = chased.pos - straight.pos;
        check (chased.voices == 1 && std::abs (d) <= 0.002,
               fmt ("%s: the chased playhead %.6f, the straight render's %.6f at the same song sample (%+.2e; within 0.002)", name, chased.pos,
                    straight.pos, d));
        check (chasedAudio.fade > 0.02 && chasedAudio.fade < 0.3,
               fmt ("%s: the chase fades in over 10 ms: rms (0 .. 2.5 ms) / rms (12.5 .. 15 ms) = %.4f (0.02 .. 0.3)", name, chasedAudio.fade));
        note (fmt ("%s: first sample %.3e; the audio reads %.5f of the file at 2400 samples (the playhead there %.5f: grains that restart on a new "
                   "phase read a little ahead, as in the prototype); %d grains on the display",
                   name, static_cast<double> (chasedAudio.first), chasedAudio.decoded, (static_cast<double> (kPlay) + 2400) / (8 * kSr), chased.grains));
        if (kind.lock)
            check (chased.grains == 1, fmt ("Lock: the chased note plays Lock, one capture on the display for its one head (%d)", chased.grains));

        // A no-op is not a pass: a chase that left the heads at the note start would be this far from the straight
        // render (the playhead 2432 samples after the note's own start).
        const Probe fromStart = playFrom (kind.eng, kind.lock, 0, kAfter);
        check (std::abs (fromStart.pos - straight.pos) > 0.05,
               fmt ("negative control: %s heads that start at the note start instead are %.4f away (far outside 0.002)", name,
                    std::abs (fromStart.pos - straight.pos)));
    }
}

SL_TEST ("engine", chaseGrainsPathFarIn)
{
    section ("chase, grain engines, far into a Path-kind note (M3 review R3-GP-1): 16-beat Voice, Cloud and Lock notes with a Travel path "
             "0.1 -> 0.9, Play at beat 12 (48 kHz) and beat 6 (96 kHz): the chase takes coarse steps, head 1 is finite and within 0.002 of "
             "the straight render, and the audio reads within 0.01 of it");

    struct Rate
    {
        double sr;
        double beat;
        double protoPos;          // the prototype's head 1 at kAfter samples (its chase replays every tick)
        double protoDecoded[3];   // the prototype's audio read position at 2400 samples: Voice, Cloud, Lock
    };
    struct Kind
    {
        core::Engine eng;
        bool lock;
        const char* name;
    };
    const Kind kinds[3] = { Kind { core::Engine::voice, false, "Voice" }, Kind { core::Engine::cloud, false, "Cloud" },
                            Kind { core::Engine::cloud, true, "Lock" } };
    for (const Rate& rate : { Rate { 48000, 12, 0.70500, { 0.70480, 0.70708, 0.70530 } }, Rate { 96000, 6, 0.40250, { 0.40255, 0.40692, 0.40245 } } })
    {
        const auto kPlay = static_cast<std::int64_t> (std::round (rate.beat * 60 / 120.0 * rate.sr));   // 288000 at both rates
        const std::int64_t everyTick = kPlay / engine::kCtrl + 1;   // the ticks a chase that never takes coarse steps replays
        for (int k = 0; k < 3; ++k)
        {
            const Kind& kind = kinds[k];
            const PathProbe chased = playPathFrom (rate.sr, kind.eng, kind.lock, rate.beat, kAfter, static_cast<std::int64_t> (0.2 * rate.sr));
            const PathProbe straight = playPathFrom (rate.sr, kind.eng, kind.lock, 0, kPlay + kAfter, 0);
            const double d = chased.pos - straight.pos;
            check (chased.chaseTicks > 0 && chased.chaseTicks < everyTick,
                   fmt ("%s at %.0f kHz, Play at beat %.0f: the chase took coarse steps (%lld ticks replayed for %lld ticks of note)", kind.name,
                        rate.sr / 1000, rate.beat, static_cast<long long> (chased.chaseTicks), static_cast<long long> (everyTick)));
            check (chased.voices == 1 && std::isfinite (chased.pos) && std::isfinite (straight.pos) && std::abs (d) <= 0.002,
                   fmt ("%s at %.0f kHz: the chased playhead %.6f, the straight render's %.6f at the same song sample (%+.2e; within 0.002; the "
                        "prototype %.5f)",
                        kind.name, rate.sr / 1000, chased.pos, straight.pos, d, rate.protoPos));
            check (std::abs (chased.decoded - straight.pos) <= 0.01,
                   fmt ("%s at %.0f kHz: the audio reads %.5f of the file at 2400 samples, within 0.01 of the playhead %.5f (the prototype %.5f; "
                        "a runaway head reads frame 0)",
                        kind.name, rate.sr / 1000, chased.decoded, straight.pos, rate.protoDecoded[k]));
        }
    }
}
