// Per-note engines and the engine switch inside a chain (PLUGIN-PLAN 4 M3 deliverable, part B):
//  - each note plays its own engine, whatever the instrument's (prototype-v4/tests/test-engine.js "per-note engine"):
//    with the instrument on Cloud, a Tape note at C5 moves the formant up an octave (centroid x 2.0 in the
//    prototype, > 1.7) and a Voice note at C5 keeps it (x 1.04, < 1.25); the display shows each engine;
//  - a chain Voice -> Cloud -> Tape plays on one voice: the engine follows each note (the display), the playhead
//    carries on across both joints, and the sound never stops (every 10 ms window keeps an rms above 0.05; the
//    prototype's quietest is 0.151);
//  - the Tape switch (grain read rate x |speed|) belongs to each note too (prototype-v4/tests/test-engine-extra.js B):
//    live Cloud at Speed 2 keeps C3 with it off (+-75 cents, the granular smear the JS allows) and plays C4 with it on
//    (+-10); in a chain Voice (off) -> Cloud (on) and Cloud (on) -> Voice (off) at Speed 2, each note reads with its
//    own switch: Voice C3 within +-3 cents, Cloud with the switch on C4 within +-10. A voice that kept its first
//    note's switch would read the second note an octave off, so both directions are checked.
// Group "engine".

#include "engine/GrainRig.h"
#include "engine/VoiceRig.h"
#include "support/Check.h"
#include "support/Measure.h"
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

    /** The JS sine (f, secs, sr, amp = 0.5): amp sin (2 pi f i / sr), Float32. */
    std::vector<float> sine (double f, double secs, double sr)
    {
        const auto n = static_cast<std::size_t> (std::llround (secs * sr));
        std::vector<float> x (n);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = static_cast<float> (0.5 * std::sin (2 * 3.141592653589793 * f * static_cast<double> (i) / sr));
        return x;
    }

    /** The JS peakHz (x, sr, 40, 6000, 16, start): the strongest spectral peak from 40 Hz to 6 kHz in 2^16 samples from
        start, refined by a parabola on the dB magnitudes. */
    double peakHz (const std::vector<float>& x, double sr, std::int64_t start)
    {
        constexpr int order = 16;
        const std::size_t n = std::size_t { 1 } << order;
        const std::vector<double> m = grains::spectrum (x, start, order);
        const double bin = sr / static_cast<double> (n);
        const std::size_t lo = std::max<std::size_t> (2, static_cast<std::size_t> (std::floor (40 / bin)));
        const std::size_t hi = std::min<std::size_t> (n / 2 - 2, static_cast<std::size_t> (std::ceil (6000 / bin)));
        std::size_t bk = lo;
        for (std::size_t k = lo; k <= hi; ++k)
            if (m[k] > m[bk])
                bk = k;
        const double a = 20 * std::log10 (m[bk - 1] + 1e-30), b = 20 * std::log10 (m[bk] + 1e-30), c = 20 * std::log10 (m[bk + 1] + 1e-30);
        const double den = a - 2 * b + c;
        return (static_cast<double> (bk) + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0)) * bin;
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

SL_TEST ("engine", tapeSwitchFollowsEachNote)
{
    section ("the Tape switch (grain read rate x |speed|) at Speed 2 on a C3 sine: live Cloud with it off and on, and chains Voice (off) "
             "-> Cloud (on) and Cloud (on) -> Voice (off): each note reads with its own switch");

    const std::vector<float> src = sine (signals::midiHz (60), 8, kSr);
    struct Live
    {
        bool tape;
        int wantKey;
        double tol, proto;   // the prototype's cents (node)
    };
    const Live lives[] = { { false, 60, 75, 29.427 }, { true, 72, 10, 0.005 } };
    for (const Live& x : lives)
    {
        core::Inst inst = grains::instWith (core::Engine::cloud);
        inst.speed = 2;
        inst.tape = x.tape;
        grains::Play play (kSr, inst, grains::mono (src, kSr));
        play.e->noteOn (60, 1);
        play.render (static_cast<std::int64_t> (std::llround (1.6 * kSr)));
        const double f = peakHz (play.L, kSr, static_cast<std::int64_t> (play.L.size()) - 65536);
        const double c = measure::centsBetween (f, signals::midiHz (x.wantKey));
        check (std::abs (c) <= x.tol, fmt ("live Cloud, Speed 2, Tape switch %s: %.3f Hz, %+.3f cents from %s (+-%.0f; the prototype %+.3f)",
                                           x.tape ? "on" : "off", f, c, x.wantKey == 72 ? "C4" : "C3", x.tol, x.proto));
    }

    struct Chain
    {
        const char* name;
        core::Engine first;
        bool firstTape;
        core::Engine second;
        bool secondTape;
        const char* engines;
    };
    const Chain chains[] = { { "Voice (off) -> Cloud (on)", core::Engine::voice, false, core::Engine::cloud, true, "voice cloud" },
                             { "Cloud (on) -> Voice (off)", core::Engine::cloud, true, core::Engine::voice, false, "cloud voice" } };
    for (const Chain& x : chains)
    {
        rig::RawNote a = grains::rawNote ("t0", 60, 0, 4, x.first);
        a.tape = x.firstTape;
        rig::RawNote b = grains::rawNote ("t1", 60, 4, 4, x.second);
        b.tape = x.secondTape;
        core::Inst inst = grains::instWith (core::Engine::voice);
        inst.speed = 2;
        grains::Play play (kSr, inst, grains::mono (src, kSr));
        play.snapshot (rig::rawSnapshot ({ a, b }, 4, true));
        play.play (120, 0, false);
        int maxVoices = 0;
        std::string engines;
        play.render (static_cast<std::int64_t> (4 * kSr), 128, [&] (std::int64_t i) {
            maxVoices = std::max (maxVoices, play.e->stats().voices);
            if (i == 48000 || i == 144000)
            {
                const engine::DisplayFrame& f = play.display();
                engines += std::string (engines.empty() ? "" : " ") + (f.numVoices == 1 ? engineName (f.voices[0].engine) : "?");
            }
        });
        check (maxVoices == 1 && engines == x.engines,
               fmt ("%s: one voice (most %d), the engines %s (expected %s)", x.name, maxVoices, engines.c_str(), x.engines));
        // each note's own switch: on reads the C3 sine at 2x (C4, +-10 cents), off keeps C3 (Voice, +-3 cents)
        const double f1 = peakHz (play.L, kSr, 24000), f2 = peakHz (play.L, kSr, 120000);
        const double c1 = measure::centsBetween (f1, signals::midiHz (x.firstTape ? 72 : 60));
        const double c2 = measure::centsBetween (f2, signals::midiHz (x.secondTape ? 72 : 60));
        const double tol1 = x.firstTape ? 10 : 3, tol2 = x.secondTape ? 10 : 3;
        check (std::abs (c1) <= tol1 && std::abs (c2) <= tol2,
               fmt ("%s: note 1 %.3f Hz (%+.3f cents from %s, +-%.0f), note 2 %.3f Hz (%+.3f cents from %s, +-%.0f); the prototype +0.014 and "
                    "+0.005 cents in either order",
                    x.name, f1, c1, x.firstTape ? "C4" : "C3", tol1, f2, c2, x.secondTape ? "C4" : "C3", tol2));
    }
}
