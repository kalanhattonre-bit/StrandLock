// Live input on the Engine (PLUGIN-PLAN 4, M2 "live", "voice steal", the sample-swap line of "exchange"; 1.3, 8.2
// #23): velocity gain, pitch bend at bendRange 7, sustain, the mod wheel on the Travel lane, 20 keys at polyphony 16,
// and a sample swap while a key sounds (the stolen voice fades on the old sample). The MidiRouter line (CC 32 ->
// -0.98236x) and the processor-level live row are the integration step's. Group "engine".

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/Measure.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "engine/Engine.h"
#include "rt/VersionExchange.h"

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

    core::Inst tapeInst()
    {
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        return inst;
    }

    std::unique_ptr<engine::Engine> engineWith (const core::Inst& inst, const engine::SampleData* sample)
    {
        auto e = std::make_unique<engine::Engine>();
        e->prepare (48000);
        e->setInst (inst);
        e->setSample (sample, 1);
        return e;
    }

    double largestStep (const std::vector<float>& x, std::int64_t a, std::int64_t b)
    {
        double p = 0;
        for (std::int64_t i = std::max<std::int64_t> (1, a); i < b; ++i)
            p = std::max (p, std::abs (static_cast<double> (x[static_cast<std::size_t> (i)]) - x[static_cast<std::size_t> (i - 1)]));
        return p;
    }

    std::vector<float> sine (double hz, double secs)
    {
        signals::ToneOpts one;
        one.partials = 1;
        one.amp = 0.5;
        return signals::tone (hz, secs, 48000, one);
    }
}

SL_TEST ("engine", liveVelocityGain)
{
    section ("live: velocity gain = 1 - velSens (1 - vel) within 1e-6");

    auto dc = render::monoSample (signals::dc (2.0, 48000, 0.5), 48000);
    auto levelAt = [&dc] (double vel) {
        auto e = engineWith (tapeInst(), dc.get());
        render::Script s;
        s.at (0, [vel] (engine::Engine& en) { en.noteOn (60, vel); });
        std::vector<float> L, R;
        s.run (*e, 4800, render::fixedBlocks (128), L, R);
        return static_cast<double> (L[4799]);
    };
    const double full = levelAt (1.0);
    int ok = 0;
    double worst = 0;
    for (const double vel : { 0.5, 0.2, 100.0 })
    {
        const double v = vel > 1 ? vel / 127 : vel;
        const double want = 1 - 0.7 * (1 - v);
        const double got = levelAt (vel) / full;
        worst = std::max (worst, std::abs (got - want));
        ok += std::abs (got - want) <= 1e-6 ? 1 : 0;
    }
    check (ok == 3 && full > 0.1, fmt ("vel 0.5, 0.2 and 100 (MIDI, read as 100/127) give the gain ratio within 1e-6 (worst %.2e)", worst));
}

SL_TEST ("engine", liveBendRange)
{
    section ("live: bend +1 at bendRange 7 plays +7 semitones within +-3 cents");

    auto tone = render::monoSample (signals::tone (signals::midiHz (60), 6.0, 48000), 48000);
    core::Inst inst = tapeInst();
    inst.bendRange = 7;
    auto e = engineWith (inst, tone.get());
    render::Script s;
    s.at (0, [] (engine::Engine& en) {
        en.setController (engine::Ctrl::bend, 1.0);
        en.noteOn (60, 1.0);
    });
    std::vector<float> L, R;
    s.run (*e, 115200, render::fixedBlocks (256), L, R);
    const double f0 = measure::fftFundamentalHz (L, 48000, 16);
    const double cents = measure::centsBetween (f0, signals::midiHz (67));
    check (std::abs (cents) <= 3.0, fmt ("bend +1: %.3f Hz, %+.2f cents from G3 (%.3f Hz)", f0, cents, signals::midiHz (67)));
}

SL_TEST ("engine", liveSustain)
{
    section ("live: sustain holds a released key until it lifts");

    auto dc = render::monoSample (signals::dc (2.0, 48000, 0.5), 48000);
    for (const bool pedal : { true, false })
    {
        auto e = engineWith (tapeInst(), dc.get());
        render::Script s;
        if (pedal)
            s.at (100, [] (engine::Engine& en) { en.setController (engine::Ctrl::sustain, 1.0); });
        s.at (0, [] (engine::Engine& en) { en.noteOn (60, 1.0); });
        s.at (4800, [] (engine::Engine& en) { en.noteOff (60); });
        int voicesHeld = -1;
        bool releasedHeld = true;
        s.at (24000, [&voicesHeld, &releasedHeld] (engine::Engine& en) {
            voicesHeld = en.stats().voices;
            auto f = std::make_unique<engine::DisplayFrame>();
            en.fillDisplay (*f);
            releasedHeld = f->numVoices == 1 ? f->voices[0].released : true;
            en.setController (engine::Ctrl::sustain, 0.2);   // below 0.5: the pedal lifts
        });
        int voicesAfter = -1;
        s.at (24000 + 4800, [&voicesAfter] (engine::Engine& en) { voicesAfter = en.stats().voices; });
        std::vector<float> L, R;
        s.run (*e, 24000 + 4801, render::fixedBlocks (128), L, R);
        if (pedal)
            check (voicesHeld == 1 && ! releasedHeld && voicesAfter == 0,
                   fmt ("with the pedal down the released key still sounds 0.4 s later (%d voice, released %d) and ends once it lifts (%d)", voicesHeld,
                        releasedHeld ? 1 : 0, voicesAfter));
        else
            check (voicesHeld == 0, fmt ("negative control: without the pedal the key has ended 0.4 s after its note-off (%d voices)", voicesHeld));
    }
}

SL_TEST ("engine", liveModWheelTravel)
{
    section ("live: mod1 -> Travel at 0.25 plays -1x within 1e-6, at 0.5 it stops (speed < 0.01x)");

    auto tone = render::monoSample (signals::tone (signals::midiHz (60), 4.0, 48000), 48000);
    core::Inst inst = tapeInst();
    inst.mod1.target = static_cast<std::int8_t> (core::LaneId::travel);
    const double n = static_cast<double> (tone->n);
    auto speedAt = [&] (double wheel) {
        auto e = engineWith (inst, tone.get());
        e->setController (engine::Ctrl::mod1, wheel);
        e->noteOn (60, 1.0);
        auto f = std::make_unique<engine::DisplayFrame>();
        std::vector<float> L (480), R (480);
        double moved = 0, last = std::numeric_limits<double>::quiet_NaN();
        const int steps = 210;   // 4800 samples in, then 200 x 480 = 96000 samples measured
        for (int i = 0; i < steps; ++i)
        {
            e->process (L.data(), R.data(), 480);
            e->fillDisplay (*f);
            const double pos = f->numVoices == 1 && f->voices[0].numHeads > 0 ? f->voices[0].heads[0].pos : std::nan ("");
            if (i >= 10 && last == last)
            {
                double d = pos - last;
                d -= std::floor (d + 0.5);   // the Region wraps: steps are much smaller than half the file
                moved += d;
            }
            if (i >= 9)
                last = pos;
        }
        return moved * n / (200.0 * 480.0);
    };
    const double back = speedAt (0.25), stopped = speedAt (0.5), forward = speedAt (0.75);
    check (std::abs (back - -1.0) <= 1e-6, fmt ("wheel 0.25: %.9f x (expected -1)", back));
    check (std::abs (stopped) < 0.01, fmt ("wheel 0.5: %.2e x (stopped)", stopped));
    check (std::abs (forward - 1.0) <= 1e-6, fmt ("negative control: wheel 0.75 plays %+.9f x, so the measurement tells the directions apart", forward));
}

SL_TEST ("engine", voiceStealSmooth)
{
    section ("voice steal: 20 keys at polyphony 16: 16 voices, 4 steals, the largest step within 10 ms of each steal / steady < 1.5");

    auto low = render::monoSample (sine (110, 6.0), 48000);
    core::Inst inst = tapeInst();
    inst.outGain = -20;   // 16 voices stay under the limiter's ceiling, so the limiter adds no step of its own
    auto e = engineWith (inst, low.get());
    render::Script s;
    std::vector<std::int64_t> stealAt;
    for (int i = 0; i < 20; ++i)
    {
        const std::int64_t k = i * 2400;
        if (i >= 16)
            stealAt.push_back (k);
        s.at (k, [i] (engine::Engine& en) { en.noteOn (40 + i, 0.8); });
    }
    const std::int64_t lastKey = 19 * 2400, n = lastKey + 4800 + 24000;
    std::vector<float> L, R;
    s.run (*e, n, render::fixedBlocks (128), L, R);
    const engine::Stats st = e->stats();
    check (st.voices == 16 && st.steals == 4, fmt ("16 voices and 4 steals (%d, %lld)", st.voices, static_cast<long long> (st.steals)));
    const double steady = largestStep (L, lastKey + 4800, n);
    double worst = 0;
    for (const std::int64_t k : stealAt)
        worst = std::max (worst, largestStep (L, k, k + 480));
    check (steady > 0 && worst / steady < 1.5, fmt ("largest step within 10 ms of a steal / steady = %.3f (steady %.2e)", worst / steady, steady));

    // How a cut would look: the oldest key alone, at the moment it is stolen, against the steady step.
    auto alone = engineWith (inst, low.get());
    render::Script one;
    one.at (0, [] (engine::Engine& en) { en.noteOn (40, 0.8); });
    std::vector<float> La, Ra;
    one.run (*alone, stealAt[0] + 480, render::fixedBlocks (128), La, Ra);
    double amp = 0;
    for (std::int64_t i = stealAt[0] - 480; i < stealAt[0]; ++i)
        amp = std::max (amp, std::abs (static_cast<double> (La[static_cast<std::size_t> (i)])));
    note (fmt ("a cut (no 5 ms fade) of the stolen voice would step by up to %.2e, %.1f times the steady step", amp, amp / steady));
    check (amp / steady > 1.5, "so the step check would catch a steal without its fade");
}

SL_TEST ("engine", sampleSwapFadesOnOldSample)
{
    section ("sample swap 220 -> 330 Hz while a key sounds: the stolen voice fades on the old sample (8.2 #23), step / steady < 1.5");

    auto a = render::monoSample (sine (220, 2.0), 48000);
    auto b = render::monoSample (sine (330, 2.0), 48000);
    auto e = engineWith (tapeInst(), a.get());
    render::Script s;
    s.at (0, [] (engine::Engine& en) { en.noteOn (60, 1.0); });
    std::uint64_t heldRightAfter = 0, heldLater = 0;
    s.at (24000, [&b, &heldRightAfter] (engine::Engine& en) {
        en.setSample (b.get(), 2);
        heldRightAfter = en.minEpochInUse (engine::VersionKind::sample);
    });
    s.at (24480, [&heldLater] (engine::Engine& en) { heldLater = en.minEpochInUse (engine::VersionKind::sample); });
    std::vector<float> L, R;
    s.run (*e, 30000, render::fixedBlocks (128), L, R);
    const double steady = largestStep (L, 12000, 23900);
    const double swap = largestStep (L, 23990, 24480);
    check (steady > 0 && swap / steady < 1.5, fmt ("largest step at the swap / steady = %.3f", swap / steady));
    check (heldRightAfter == 1 && heldLater == 2,
           fmt ("the stolen voice keeps the old sample's epoch through its fade (%llu right after, %llu 10 ms later)",
                static_cast<unsigned long long> (heldRightAfter), static_cast<unsigned long long> (heldLater)));

    // A no-op is not a pass: the new sample played steadily steps 1.5 times as far, so a fade read from it (the
    // prototype's way) is what the check above rules out.
    auto eb = engineWith (tapeInst(), b.get());
    render::Script sb;
    sb.at (0, [] (engine::Engine& en) { en.noteOn (60, 1.0); });
    std::vector<float> Lb, Rb;
    sb.run (*eb, 24000, render::fixedBlocks (128), Lb, Rb);
    const double ratioB = largestStep (Lb, 12000, 23900) / steady;
    check (ratioB >= 1.45, fmt ("negative control: the 330 Hz sample's steady step is %.3f times the 220 Hz one", ratioB));
}
