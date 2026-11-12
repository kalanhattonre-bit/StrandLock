// Per-note engines and the engine switch inside a chain (PLUGIN-PLAN 4 M3 deliverable, part B):
//  - each note plays its own engine, whatever the instrument's (prototype-v4/tests/test-engine.js "per-note engine"):
//    with the instrument on Cloud, a Tape note at C5 moves the formant up an octave (centroid x 2.0 in the
//    prototype, > 1.7) and a Voice note at C5 keeps it (x 1.04, < 1.25); the display shows each engine;
//  - a chain Voice -> Cloud -> Tape plays on one voice: the engine follows each note (the display), the playhead
//    carries on across both joints, and the sound never stops (every 10 ms window keeps an rms above 0.05; the
//    prototype's quietest is 0.151).
// Group "engine".

#include "engine/GrainRig.h"
#include "engine/VoiceRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/DisplayFrame.h"
#include "engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    constexpr double kSr = 48000;

    const char* engineName (int kc)
    {
        static const char* names[] = { "tape", "voice", "cloud", "lock" };
        return kc >= 0 && kc < 4 ? names[kc] : "none";
    }
}

SL_TEST ("engine", perNoteEngines)
{
    section ("per-note engines: with the instrument on Cloud, a Tape note and a Voice note (both C5) each play their own engine");

    const std::vector<float> src = signals::formantTone (signals::midiHz (60), 6, kSr, 1000, 350);
    core::Inst inst = grains::instWith (core::Engine::cloud);   // the instrument's engine differs from both notes on purpose
    grains::Play play (kSr, inst, grains::mono (src, kSr));
    play.snapshot (rig::rawSnapshot ({ grains::rawNote ("tp", 72, 0, 3, core::Engine::tape), grains::rawNote ("vc", 72, 4, 3, core::Engine::voice) },
                                     4, false));
    play.play (120, 0, false);
    int engA = -1, engB = -1;
    play.render (static_cast<std::int64_t> (3.5 * kSr), 128, [&] (std::int64_t i) {
        if (engA < 0 && static_cast<double> (i) > 0.5 * kSr)
        {
            const engine::DisplayFrame& f = play.display();
            engA = f.numVoices > 0 ? f.voices[0].engine : 99;
        }
        if (engB < 0 && static_cast<double> (i) > 2.5 * kSr)
        {
            const engine::DisplayFrame& f = play.display();
            engB = f.numVoices > 0 ? f.voices[0].engine : 99;
        }
    });
    const double c0 = grains::centroid (src, kSr, 15, 0);
    const double cA = grains::centroid (play.L, kSr, 15, static_cast<std::int64_t> (std::llround (0.3 * kSr)));
    const double cB = grains::centroid (play.L, kSr, 15, static_cast<std::int64_t> (std::llround (2.3 * kSr)));
    check (engA == static_cast<int> (engine::Kc::tape) && cA / c0 > 1.7,
           fmt ("note 1 plays Tape (+12 st moves the formant up about 2x): engine %s, centroid x %.3f (> 1.7; the prototype tape, x 2.000)",
                engineName (engA), cA / c0));
    check (engB == static_cast<int> (engine::Kc::voice) && cB / c0 < 1.25,
           fmt ("note 2 plays Voice (+12 st keeps the formant): engine %s, centroid x %.3f (< 1.25; the prototype voice, x 1.037)", engineName (engB),
                cB / c0));
}

SL_TEST ("engine", chainSwitchesEngine)
{
    section ("a chain Voice -> Cloud -> Tape (2 beats each, C3) plays on one voice: the engine follows each note, the playhead carries on, "
             "no gap");

    const std::vector<float> src = signals::tone (signals::midiHz (60), 4, kSr);
    grains::Play play (kSr, grains::instWith (core::Engine::voice), grains::mono (src, kSr));
    play.snapshot (rig::rawSnapshot ({ grains::rawNote ("v1", 60, 0, 2, core::Engine::voice), grains::rawNote ("c2", 60, 2, 2, core::Engine::cloud),
                                       grains::rawNote ("t3", 60, 4, 2, core::Engine::tape) },
                                     4, true));
    play.play (120, 0, false);
    int maxVoices = 0;
    std::string engines;
    double pos[4] = { -1, -1, -1, -1 };
    const std::int64_t engineAt[] = { 24064, 72064, 120064 }, posAt[] = { 47872, 48128, 95872, 96128 };
    play.render (static_cast<std::int64_t> (3 * kSr), 128, [&] (std::int64_t i) {
        maxVoices = std::max (maxVoices, play.e->stats().voices);
        for (const std::int64_t k : engineAt)
            if (i == k)
            {
                const engine::DisplayFrame& f = play.display();
                engines += std::string (engines.empty() ? "" : " ") + (f.numVoices == 1 ? engineName (f.voices[0].engine) : "?");
            }
        for (int q = 0; q < 4; ++q)
            if (i == posAt[q])
            {
                const engine::DisplayFrame& f = play.display();
                pos[q] = f.numVoices == 1 && f.voices[0].numHeads > 0 ? static_cast<double> (f.voices[0].heads[0].pos) : -1;
            }
    });
    check (maxVoices == 1, fmt ("one voice plays the whole chain (most voices %d)", maxVoices));
    check (engines == "voice cloud tape", "the engine follows each note: " + engines + " (expected voice cloud tape)");
    // 256 samples at 1x on a 4 s file move the head 0.00133 of the file (the prototype: 0.00133 at both joints)
    const double j1 = pos[1] - pos[0], j2 = pos[3] - pos[2];
    check (pos[0] >= 0 && pos[2] >= 0 && std::abs (j1 - 0.00133) < 0.001 && std::abs (j2 - 0.00133) < 0.001,
           fmt ("the playhead carries on across the joints: it moves %.5f and %.5f in the 256 samples around them (0.00133 at 1x)", j1, j2));
    double quietest = 1;
    for (std::int64_t a = 960; a + 480 <= static_cast<std::int64_t> (2.9 * kSr); a += 480)
        quietest = std::min (quietest, rig::rms (play.L, a, a + 480));
    check (quietest > 0.05, fmt ("no gap: the quietest 10 ms window has rms %.4f (> 0.05; the prototype 0.151)", quietest));
}
