// The grain engines in tune (PLUGIN-PLAN 4, M3 part G; PROMPT 14.1 and 14.3), as prototype-v4/tests/test-engine.js
// measures them:
//  - Voice tuning: MIDI 36, 60 and 84 (C1, C3, C5) as live keys against a C3 root, at 48 and 96 kHz, on the
//    prototype tests' C3 tone (12 partials) recorded at 44.1 kHz (so the rate ratio is in the test too): f0 by
//    harmonicF0 over the last 2^16 samples of a 2 s render, within +-3 cents. Cloud f0 is printed, not gated (plain
//    Cloud grains are not phase-aligned; plan 8.1 #28).
//  - glide: a legato C3 -> G3 chain on one voice, Voice and Tape: C3 steady within +-3 cents, the glide lands on G3
//    within +-5 cents, one voice, the envelope stays up across the hand-over; and an explicit glideTo link (made
//    and compiled by the model) plays its slide on one voice with Auto Slide Off.
// Group "engine".

#include "engine/GrainRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "engine/Engine.h"
#include "model/Clip.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
    using namespace sl;
    using namespace sl::test;

    /** The JS tuning row's source: the C3 tone, 6 s at 44.1 kHz. Made once. */
    const std::vector<float>& c3At44k()
    {
        static const std::vector<float> made = signals::tone (signals::midiHz (60), 6, 44100);
        return made;
    }

    /** f0 of a live key p on an engine at sr, in cents from p's pitch. */
    grains::F0 keyF0 (double sr, core::Engine engine, int p, double pitch = 0)
    {
        core::Inst inst = grains::instWith (engine);
        inst.root = 60;
        inst.pitch = pitch;
        grains::Play play (sr, inst, grains::mono (c3At44k(), 44100));
        play.e->noteOn (p, 1);
        play.render (static_cast<std::int64_t> (2.0 * sr));
        return grains::harmonicF0 (play.L, sr, signals::midiHz (p), 16);
    }
}

SL_TEST ("engine", voiceTuning)
{
    section ("Voice tuning: C1 / C3 / C5 against a C3 root at 48 and 96 kHz, a 44.1 kHz source (+-3 cents); Cloud printed");

    const int keys[] = { 36, 60, 84 };
    const char* names[] = { "C1", "C3", "C5" };
    int ok = 0, total = 0;
    double worst = 0;
    for (const double sr : { 48000.0, 96000.0 })
        for (int i = 0; i < 3; ++i)
        {
            const grains::F0 v = keyF0 (sr, core::Engine::voice, keys[i]);
            const double c = grains::cents (v.f0, signals::midiHz (keys[i]));
            ++total;
            ok += std::abs (c) <= 3.0 ? 1 : 0;
            worst = std::max (worst, std::abs (c));
            note (fmt ("Voice %s at %.0f kHz: %+.4f cents (%d harmonics)", names[i], sr / 1000, c, v.used));
            const grains::F0 cl = keyF0 (sr, core::Engine::cloud, keys[i]);
            note (fmt ("Cloud %s at %.0f kHz: %+.4f cents, %d harmonics (printed, not gated: plain Cloud grains are not phase-aligned)", names[i], sr / 1000,
                       grains::cents (cl.f0, signals::midiHz (keys[i])), cl.used));
        }
    check (ok == total && total == 6, fmt ("Voice: all 6 within +-3 cents (%d of %d; the worst %.4f cents)", ok, total, worst));

    // A no-op is not a pass: Pitch +0.1 semitone (10 cents) on C3 measures 10 cents off, outside the gate.
    for (const double sr : { 48000.0, 96000.0 })
    {
        const double c = grains::cents (keyF0 (sr, core::Engine::voice, 60, 0.1).f0, signals::midiHz (60));
        check (std::abs (c - 10) <= 3.0 && std::abs (c) > 3.0,
               fmt ("negative control at %.0f kHz: Voice C3 with Pitch +0.1 st measures %+.3f cents (10 expected), outside +-3", sr / 1000, c));
    }
}

SL_TEST ("engine", glideLegatoC3ToG3)
{
    section ("glide: legato C3 -> G3 on one voice, Voice and Tape (C3 within +-3 cents, lands on G3 within +-5, no new attack)");

    const double sr = 48000;
    const std::vector<float> src = signals::tone (signals::midiHz (60), 6, sr);
    const auto kB = static_cast<std::int64_t> (2 * 60 / 120.0 * sr);
    for (const core::Engine eng : { core::Engine::voice, core::Engine::tape })
    {
        const char* name = eng == core::Engine::voice ? "Voice" : "Tape";
        rig::RawNote A = grains::rawNote ("A", 60, 0, 2, eng);
        A.hasGlide = true;
        A.glideD = 7;
        A.glideLen = 0.5;
        A.glideCurve = 0;
        A.glideTo = "B";
        const rig::RawNote B = grains::rawNote ("B", 67, 2, 3, eng);
        grains::Play play (sr, grains::instWith (eng), grains::mono (src, sr));
        play.snapshot (rig::rawSnapshot ({ A, B }, 4, true));
        play.play (120, 0, false);
        int maxVoices = 0;
        double minEnv = 1;
        play.render (static_cast<std::int64_t> (std::round (4.9 * 60 / 120.0 * sr)), 64, [&] (std::int64_t i) {
            maxVoices = std::max (maxVoices, static_cast<int> (play.e->stats().voices));
            if (i > kB - 4800 && i < kB + 4800)
            {
                const engine::DisplayFrame& f = play.display();
                if (f.numVoices > 0)
                    minEnv = std::min (minEnv, static_cast<double> (f.voices[0].env));
            }
        });
        const double cA = grains::cents (grains::harmonicF0 (play.L, sr, signals::midiHz (60), 15, 4800).f0, signals::midiHz (60));
        const double cB = grains::cents (grains::harmonicF0 (play.L, sr, signals::midiHz (67), 15, static_cast<std::int64_t> (std::round (2.6 * 60 / 120.0 * sr))).f0,
                                         signals::midiHz (67));
        check (std::abs (cA) <= 3.0, fmt ("%s: note A steady at C3 (%+.3f cents, +-3)", name, cA));
        check (std::abs (cB) <= 5.0, fmt ("%s: the glide lands on G3 (%+.3f cents, +-5)", name, cB));
        check (maxVoices == 1, fmt ("%s: the chain plays on one voice (most voices %d)", name, maxVoices));
        check (minEnv > 0.999, fmt ("%s: no new attack at the hand-over (min envelope %.4f, > 0.999)", name, minEnv));
    }
}

SL_TEST ("engine", glideToWithAutoSlideOff)
{
    section ("glide: an explicit glideTo link plays its slide with Auto Slide Off (Voice; the clip compiled by the model)");

    const double sr = 48000;
    const std::vector<float> src = signals::tone (signals::midiHz (60), 6, sr);
    auto run = [&] (bool link, int& maxVoices, double& cA, double& cB) {
        model::Note a = model::mkNote (60, 0, 2, "ga");
        const model::Note b = model::mkNote (67, 2, 3, "gb");
        if (link)
            a.glideTo = "gb";
        model::Clip clip = model::newClip (4);
        clip.notes = { std::make_shared<const model::Note> (a), std::make_shared<const model::Note> (b) };
        core::Inst inst = grains::instWith (core::Engine::voice);
        inst.glideMode = core::GlideMode::off;
        grains::Play play (sr, inst, grains::mono (src, sr));
        play.snapshot (render::compile (clip, 256));
        play.play (120, 0, false);
        maxVoices = 0;
        play.render (static_cast<std::int64_t> (std::round (4.9 * 60 / 120.0 * sr)), 64,
                     [&] (std::int64_t) { maxVoices = std::max (maxVoices, static_cast<int> (play.e->stats().voices)); });
        cA = grains::cents (grains::harmonicF0 (play.L, sr, signals::midiHz (60), 15, 4800).f0, signals::midiHz (60));
        cB = grains::cents (grains::harmonicF0 (play.L, sr, signals::midiHz (67), 15, static_cast<std::int64_t> (std::round (2.6 * 60 / 120.0 * sr))).f0,
                            signals::midiHz (67));
    };
    int linked = 0, unlinked = 0;
    double cA = 0, cB = 0, uA = 0, uB = 0;
    run (true, linked, cA, cB);
    run (false, unlinked, uA, uB);
    check (linked == 1 && std::abs (cA) <= 3.0 && std::abs (cB) <= 5.0,
           fmt ("Auto Slide Off, C3 linked to G3: one voice (%d), C3 %+.3f cents (+-3), lands on G3 %+.3f cents (+-5)", linked, cA, cB));
    check (unlinked == 2, fmt ("negative control: the same notes without the link play on two voices with Auto Slide Off (%d)", unlinked));
}
