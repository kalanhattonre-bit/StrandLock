// The engine's transport rules (PLUGIN-PLAN 1.10, 8.2 #3, #4), driven through Engine::setTransport by a small fake
// host (the HostSync versions of these rows are the integration step's): note starts at constant tempos and after a
// step change, host jitter, pre-roll, a tempo ramp, Scatter while stopped, and the chase when Play starts inside a
// note. Note starts are found as the first non-zero sample after silence: the notes are short and far apart, the
// source is a constant (DC), so a note sounds from its first sample and is silent between notes. Group "engine".

#include "engine/EngineRender.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "engine/Engine.h"
#include "model/Clip.h"
#include "model/Node.h"
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

    struct NoteSpec
    {
        double p, s, l;
    };

    model::Clip clipOf (int bars, const std::vector<NoteSpec>& notes)
    {
        model::Clip c = model::newClip (bars);
        int i = 0;
        for (const NoteSpec& n : notes)
            c.notes.push_back (std::make_shared<const model::Note> (model::mkNote (n.p, n.s, n.l, "t" + std::to_string (i++))));
        return c;
    }

    core::Inst tapeInst()
    {
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        return inst;
    }

    struct Rig
    {
        std::unique_ptr<engine::Engine> e = std::make_unique<engine::Engine>();
        std::unique_ptr<engine::SampleData> sample;
        std::unique_ptr<core::Snapshot> snap;

        Rig (double sr, const core::Inst& inst, std::unique_ptr<engine::SampleData> s, const model::Clip* clip)
        {
            e->prepare (sr);
            e->setInst (inst);
            sample = std::move (s);
            e->setSample (sample.get(), 1);
            if (clip != nullptr)
            {
                snap = render::compile (*clip);
                e->setSnapshot (snap.get(), 1);
            }
        }
    };

    std::unique_ptr<engine::SampleData> dcSample (double secs = 4.0)
    {
        return render::monoSample (signals::dc (secs, 48000, 0.5), 48000);
    }

    engine::Transport playAt (double bpm, double beat, bool loop = true)
    {
        engine::Transport t;
        t.playing = true;
        t.bpm = bpm;
        t.beat = beat;
        t.loop = loop;
        return t;
    }

    /** The first sample of every stretch of sound that follows silence. */
    std::vector<std::int64_t> soundStarts (const std::vector<float>& L, const std::vector<float>& R)
    {
        std::vector<std::int64_t> out;
        bool sounding = false;
        for (std::size_t i = 0; i < L.size(); ++i)
        {
            const bool now = L[i] != 0.0f || R[i] != 0.0f;
            if (now && ! sounding)
                out.push_back (static_cast<std::int64_t> (i));
            sounding = now;
        }
        return out;
    }

    /** Every start within [0, 1) sample after its exact time (a hair of rounding below 0 allowed). */
    bool startsOnTime (const std::vector<std::int64_t>& got, const std::vector<double>& exact, std::string& why)
    {
        if (got.size() != exact.size())
        {
            why = fmt ("%zu starts, expected %zu", got.size(), exact.size());
            return false;
        }
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            const double off = static_cast<double> (got[i]) - exact[i];
            if (! (off >= -1e-6 && off < 1))
            {
                why = fmt ("start %zu at sample %lld, the exact time %.4f (%+.4f)", i, static_cast<long long> (got[i]), exact[i], off);
                return false;
            }
        }
        return true;
    }

    double rmsOf (const std::vector<float>& x, std::int64_t a, std::int64_t b)
    {
        double s = 0;
        for (std::int64_t i = a; i < b; ++i)
            s += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i)];
        return b > a ? std::sqrt (s / static_cast<double> (b - a)) : 0;
    }

    const std::vector<NoteSpec> kFourShort = { { 60, 0, 0.25 }, { 62, 1, 0.25 }, { 64, 2, 0.25 }, { 65, 3, 0.25 } };
}

SL_TEST ("engine", noteStartsOnTheBeat)
{
    section ("note starts at constant 120, 87 and 133 bpm, loop passes 1-3, and after a step change 120 -> 87: [0, 1) sample after the beat");

    const model::Clip clip = clipOf (1, kFourShort);
    for (const double bpm : { 120.0, 87.0, 133.0 })
    {
        Rig rig (48000, tapeInst(), dcSample(), &clip);
        const double spb = 60.0 / bpm * 48000;
        render::Script script;
        script.at (0, [bpm] (engine::Engine& e) { e.setTransport (playAt (bpm, 0)); });
        std::vector<float> L, R;
        script.run (*rig.e, static_cast<std::int64_t> (12 * spb), render::fixedBlocks (128), L, R);
        std::vector<double> exact;
        for (int b = 0; b < 12; ++b)
            exact.push_back (b * spb);
        std::string why;
        const bool onTime = startsOnTime (soundStarts (L, R), exact, why);
        check (onTime, fmt ("%g bpm: 12 starts over 3 passes, each in [0, 1) sample of its beat %s", bpm, why.c_str()));
    }

    // a step change 120 -> 87 at beat 3.5 (sample 84000), the host's beat sent with it
    {
        Rig rig (48000, tapeInst(), dcSample(), &clip);
        render::Script script;
        script.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 0)); });
        script.at (84000, [] (engine::Engine& e) { e.setTransport (playAt (87, 3.5)); });
        const double spb87 = 60.0 / 87 * 48000;
        std::vector<float> L, R;
        script.run (*rig.e, static_cast<std::int64_t> (84000 + 8.5 * spb87), render::fixedBlocks (128), L, R);
        std::vector<double> exact;
        for (int b = 0; b < 12; ++b)
            exact.push_back (b < 4 ? b * 24000.0 : 84000 + (b - 3.5) * spb87);
        std::string why;
        const bool onTime = startsOnTime (soundStarts (L, R), exact, why);
        check (onTime, "after 120 -> 87 at beat 3.5: every start in [0, 1) sample of its beat " + why);

        // a no-op is not a pass: the same starts measured against the old tempo are thousands of samples off
        std::vector<double> stale;
        for (int b = 0; b < 12; ++b)
            stale.push_back (b * 24000.0);
        std::string why2;
        const bool staleFails = ! startsOnTime (soundStarts (L, R), stale, why2);
        check (staleFails, "negative control: against 120 bpm throughout the same starts fail (" + why2 + ")");
    }
}

SL_TEST ("engine", hostJitterChangesNothing)
{
    section ("host positions jittered +-0.003 beat every block for 64 bars at 120 bpm: no start doubled or missed, the render unchanged");

    const model::Clip clip = clipOf (1, kFourShort);
    const std::int64_t n = 64 * 4 * 24000;
    std::vector<float> L0, R0, L1, R1;
    {
        Rig rig (48000, tapeInst(), dcSample(), &clip);
        render::Script script;
        script.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 0)); });
        script.run (*rig.e, n, render::fixedBlocks (512), L0, R0);
    }
    int maxVoices = 0;
    {
        Rig rig (48000, tapeInst(), dcSample(), &clip);
        signals::Lcg jitter (5);
        render::Script script;
        script.run (*rig.e, n, render::fixedBlocks (512), L1, R1, [&jitter, &maxVoices] (engine::Engine& e, std::int64_t k) {
            maxVoices = std::max (maxVoices, e.stats().voices);
            const double exact = static_cast<double> (k) / 24000.0;   // the host's ppq at this block
            e.setTransport (playAt (120, k == 0 ? 0.0 : exact + (jitter() * 2 - 1) * 0.003));
        });
    }
    const auto starts = soundStarts (L1, R1);
    std::vector<double> exact;
    for (int b = 0; b < 256; ++b)
        exact.push_back (b * 24000.0);
    std::string why;
    const bool onTime = startsOnTime (starts, exact, why);
    check (onTime, fmt ("256 starts (%zu found), each in [0, 1) sample of its beat %s", starts.size(), why.c_str()));
    check (L1 == L0 && R1 == R0, "the jittered render is the unjittered one, bit for bit");
    check (maxVoices == 1, fmt ("never more than one voice at a block start (%d): no note started twice", maxVoices));
}

SL_TEST ("engine", preRollStaysSilent)
{
    section ("pre-roll: playing from beat -2, loop on and off: silence before beat 0, the first note on its sample (8.2 #4)");

    // a 2-bar clip: a note at beat 0 and one sounding through the clip's last 2 beats
    const model::Clip clip = clipOf (2, { { 60, 0, 1 }, { 67, 6, 2 } });
    for (const bool loop : { true, false })
    {
        Rig rig (48000, tapeInst(), dcSample(), &clip);
        render::Script script;
        script.at (0, [loop] (engine::Engine& e) { e.setTransport (playAt (120, -2, loop)); });
        std::vector<float> L, R;
        script.run (*rig.e, 48000 + 24000, render::fixedBlocks (128), L, R);
        const double before = std::max (rmsOf (L, 0, 48000), rmsOf (R, 0, 48000));
        const std::int64_t first = render::firstSounding (L, R, 0, 72000);
        check (before == 0.0 && first == 48000, fmt ("loop %s: rms before beat 0 = %g, the first sound at sample %lld (beat 0 = 48000)", loop ? "on" : "off",
                                                     before, static_cast<long long> (first)));
    }
    // a no-op is not a pass: Play inside the last note (beat 6.5) sounds at once, so a wrapped pre-roll would be heard
    Rig rig (48000, tapeInst(), dcSample(), &clip);
    render::Script script;
    script.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 6.5)); });
    std::vector<float> L, R;
    script.run (*rig.e, 4800, render::fixedBlocks (128), L, R);
    check (rmsOf (L, 0, 4800) > 0.01, fmt ("negative control: Play at beat 6.5 sounds the last note at once (rms %.3f)", rmsOf (L, 0, 4800)));
}

SL_TEST ("engine", tempoRampFollowsHost)
{
    section ("tempo drift: a host ramp 120 -> 60 bpm over 8 bars, tempo and beat sent per 512-sample block: every start within 1 sample (8.2 #3)");

    std::vector<NoteSpec> notes;
    for (int b = 0; b < 32; ++b)
        notes.push_back ({ 60.0 + (b % 12), static_cast<double> (b), 0.25 });
    const model::Clip clip = clipOf (8, notes);

    // the host integrates its position per sample from a tempo that falls linearly with the beat
    auto bpmAt = [] (double ppq) { return 120 - 60 * std::min (ppq, 32.0) / 32; };
    std::vector<double> ppq (1, 0.0);
    while (ppq.back() < 31.5)
        ppq.push_back (ppq.back() + bpmAt (ppq.back()) / 60 / 48000);
    const auto n = static_cast<std::int64_t> (ppq.size());
    std::vector<double> hostStart;
    for (int b = 0; b < 32; ++b)
    {
        const auto it = std::lower_bound (ppq.begin(), ppq.end(), static_cast<double> (b));
        hostStart.push_back (static_cast<double> (it - ppq.begin()));
    }

    auto runRamp = [&] (bool sendBeat) {
        Rig rig (48000, tapeInst(), dcSample(), &clip);
        render::Script script;
        std::vector<float> L, R;
        script.run (*rig.e, n, render::fixedBlocks (512), L, R, [&] (engine::Engine& e, std::int64_t k) {
            const double p = ppq[static_cast<std::size_t> (k)];
            e.setTransport (playAt (bpmAt (p), k == 0 || sendBeat ? p : std::numeric_limits<double>::quiet_NaN()));
        });
        return soundStarts (L, R);
    };

    const auto starts = runRamp (true);
    double worst = 0;
    bool all = starts.size() == 32;
    for (std::size_t i = 0; all && i < starts.size(); ++i)
    {
        const double off = static_cast<double> (starts[i]) - hostStart[i];
        worst = std::max (worst, std::abs (off));
        all = std::abs (off) <= 1;
    }
    const double last = starts.size() == 32 ? static_cast<double> (starts.back()) - hostStart.back() : 1e9;
    check (all && std::abs (last) <= 2, fmt ("32 starts (%zu): each within 1 sample of the host's beat (worst %.0f), the last %+.0f", starts.size(), worst, last));

    // a no-op is not a pass: the prototype's own clock (the tempo sent, the beat not) drifts far from the host
    const auto drift = runRamp (false);
    const double driftLast = drift.size() == 32 ? static_cast<double> (drift.back()) - hostStart.back() : 1e9;
    check (std::abs (driftLast) > 50, fmt ("negative control: re-anchored to its own clock, the last start is %+.0f samples off", driftLast));
}

SL_TEST ("engine", scatterWhileStopped)
{
    section ("stopped: a live key with Scatter 1/8T jumps once per 1/3 beat of the free clock, 0..32 samples after each grid line");

    core::Inst inst = tapeInst();
    inst.motion = core::Motion::scatter;
    inst.jump = core::RateId::r1_8T;
    Rig rig (48000, inst, dcSample (20.0), nullptr);
    engine::Transport stopped;
    stopped.playing = false;
    stopped.bpm = 120;
    stopped.beat = 0;
    rig.e->setTransport (stopped);   // the free clock: beat 0 at sample 0, 1/3 beat = 8000 samples
    auto frame = std::make_unique<engine::DisplayFrame>();
    float L = 0, R = 0;
    std::vector<std::int64_t> jumps;
    double lastPos = std::numeric_limits<double>::quiet_NaN();
    const double n = static_cast<double> (rig.sample->n);
    for (std::int64_t k = 0; k < 96000; ++k)
    {
        if (k == 1000)
            rig.e->noteOn (60, 1.0);
        rig.e->process (&L, &R, 1);
        rig.e->fillDisplay (*frame);
        double pos = std::numeric_limits<double>::quiet_NaN();
        if (frame->numVoices == 1 && frame->voices[0].numHeads >= 1)
            pos = frame->voices[0].heads[0].pos;
        if (pos == pos && lastPos == lastPos && std::abs ((pos - lastPos) * n) > 100)
            jumps.push_back (k);
        lastPos = pos;
    }
    bool onGrid = jumps.size() == 11;
    std::string why;
    for (std::size_t j = 0; onGrid && j < jumps.size(); ++j)
    {
        const std::int64_t line = static_cast<std::int64_t> (j + 1) * 8000;
        const std::int64_t late = jumps[j] - line;
        onGrid = late >= 0 && late <= 32;
        if (! onGrid)
            why = fmt (": jump %zu at %lld, its grid line %lld", j, static_cast<long long> (jumps[j]), static_cast<long long> (line));
    }
    check (onGrid, fmt ("11 jumps between samples 1000 and 96000 (%zu), each 0..32 samples after its grid line%s", jumps.size(), why.c_str()));
}

SL_TEST ("engine", chaseStartsMidNote)
{
    section ("chase: Play inside a Tape note (beat 1.37, and with a moving Volume lane): sounds at once, fades in 10 ms, then equals the straight render");

    model::Clip plain = clipOf (4, { { 60, 0, 4 } });
    model::Clip moving = plain;
    {
        model::Note n = *moving.notes[0];
        n.lane (core::LaneId::amp) = model::NodeList { { 0.0, 0.3, model::NodeShape::linear, 0.0 }, { 1.0, 0.9, model::NodeShape::linear, 0.0 } };
        moving.notes[0] = std::make_shared<const model::Note> (n);
    }
    const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
    for (const model::Clip* clip : { &plain, &moving })
    {
        const char* label = clip == &plain ? "plain" : "moving Volume lane";
        const std::int64_t kChase = 32880;   // beat 1.37 at 120 bpm
        const std::int64_t span = 4800;
        std::vector<float> Ls, Rs, Lc, Rc;
        {
            Rig rig (48000, tapeInst(), engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw"), clip);
            render::Script script;
            script.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 0)); });
            script.run (*rig.e, kChase + span, render::fixedBlocks (128), Ls, Rs);
        }
        {
            Rig rig (48000, tapeInst(), engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw"), clip);
            render::Script script;
            script.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 1.37)); });
            script.run (*rig.e, span, render::fixedBlocks (128), Lc, Rc);
        }
        const bool firstNonZero = Lc[0] != 0.0f || Rc[0] != 0.0f;
        const double ratio = rmsOf (Lc, 0, 120) / rmsOf (Lc, 600, 720);
        double diff = 0;
        for (std::int64_t j = 960; j < span; ++j)
        {
            diff = std::max (diff, std::abs (static_cast<double> (Lc[static_cast<std::size_t> (j)]) - Ls[static_cast<std::size_t> (kChase + j)]));
            diff = std::max (diff, std::abs (static_cast<double> (Rc[static_cast<std::size_t> (j)]) - Rs[static_cast<std::size_t> (kChase + j)]));
        }
        const double before = rmsOf (Lc, 600, 720);
        check (firstNonZero, fmt ("%s: the first sample is not zero (%.3g)", label, static_cast<double> (Lc[0])));
        check (ratio >= 0.02 && ratio <= 0.3, fmt ("%s: rms (0-2.5 ms) / rms (12.5-15 ms) = %.3f, in 0.02..0.3", label, ratio));
        check (diff <= 1e-4 && before > 0.01, fmt ("%s: after 20 ms the chased output equals the straight render within 1e-4 (largest difference %.2e)", label, diff));
    }
}

SL_TEST ("engine", reAnchorForwardAndBack)
{
    section ("a tempo change re-anchors to the host's beat (8.2 #3): forward, the note it jumps over starts at once; back, no note starts twice");

    const model::Clip clip = clipOf (1, kFourShort);
    const double spb100 = 60.0 / 100 * 48000;
    struct Way
    {
        const char* label;
        std::int64_t at;    // the re-anchor sample (beat 2 -/+ 0.02 at 120 bpm)
        double hostBeat;    // what the host says there: 0.05 beat ahead of, or behind, the engine's clock
    };
    const Way ways[] = { { "forward (+0.05 beat, just before the note at beat 2)", 48000 - 480, 2.03 },
                         { "back (-0.05 beat, just after the note at beat 2)", 48000 + 480, 1.97 } };
    for (const Way& w : ways)
    {
        Rig rig (48000, tapeInst(), dcSample(), &clip);
        render::Script script;
        script.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 0)); });
        script.at (w.at, [w] (engine::Engine& e) { e.setTransport (playAt (100, w.hostBeat)); });
        int maxVoices = 0;
        std::vector<float> L, R;
        const auto n = static_cast<std::int64_t> (w.at + (4.5 - w.hostBeat) * spb100);
        script.run (*rig.e, n, render::fixedBlocks (32), L, R,
                    [&maxVoices] (engine::Engine& e, std::int64_t) { maxVoices = std::max (maxVoices, e.stats().voices); });
        // after the re-anchor beat b sounds at w.at + (b - hostBeat) x 0.6 s; a note the clock jumped over, at once
        std::vector<double> exact { 0, 24000 };
        for (const double b : { 2.0, 3.0, 4.0 })
        {
            if (b == 2.0 && w.hostBeat > 2)
                exact.push_back (static_cast<double> (w.at));
            else if (b == 2.0)
                exact.push_back (48000);
            else
                exact.push_back (static_cast<double> (w.at) + (b - w.hostBeat) * spb100);
        }
        std::string why;
        const bool onTime = startsOnTime (soundStarts (L, R), exact, why);
        check (onTime && maxVoices == 1,
               fmt ("%s: 5 starts, each once and on time (most voices at once %d) %s", w.label, maxVoices, why.c_str()));
        // the note at beat 2 plays its length after the re-anchor (moved back, the clock is behind its start for a
        // moment: it must not release, as the JS rule for a note edited to start later would)
        check (L[52000] != 0.0f, fmt ("%s: the note at beat 2 still sounds 4000 samples after its start", w.label));
    }
}

SL_TEST ("engine", tempoDropKeepsTheNoteOnIt)
{
    section ("a tempo drop re-anchored on a block start (host beat = the engine's clock): a note a fraction of a sample before it starts once (fix pass, M2-ENG-1)");

    // A note at beat 4, eps samples before sample 96000 at 120 bpm. The old clock puts it at 96000, so it had not
    // started; a slower new clock puts it before 96000. The JS loses it when eps > newBpm / 120 (the M2 review).
    const model::Clip clip = clipOf (2, { { 60, 4, 1 } });
    struct Case
    {
        double eps, bpm;
    };
    const Case cases[] = { { 0.8, 87 }, { 0.99, 87 }, { 0.5, 60 }, { 0.99, 60 }, { 0.1, 87 }, { 0.8, 119 } };
    for (const Case& c : cases)
    {
        Rig rig (48000, tapeInst(), dcSample(), &clip);
        const double db = 120.0 / 60 / 48000;
        const double b0 = c.eps * db;
        const double cur = b0 + 96000.0 * db;   // the engine's clock there: uAt (96000), computed as it does
        render::Script script;
        script.at (0, [b0] (engine::Engine& e) { e.setTransport (playAt (120, b0)); });
        script.at (96000, [c, cur] (engine::Engine& e) { e.setTransport (playAt (c.bpm, cur)); });
        int maxVoices = 0;
        std::vector<float> L, R;
        script.run (*rig.e, 96000 + 24000, render::fixedBlocks (128), L, R,
                    [&maxVoices] (engine::Engine& e, std::int64_t) { maxVoices = std::max (maxVoices, e.stats().voices); });
        std::string why;
        const bool onTime = startsOnTime (soundStarts (L, R), { 96000.0 - c.eps }, why);
        check (onTime && maxVoices == 1,
               fmt ("eps %.2f, 120 -> %g bpm: the note starts once (most voices %d), in [0, 1) sample of its time %s", c.eps, c.bpm, maxVoices,
                    why.c_str()));
    }
}

SL_TEST ("engine", manyReAnchorsKeepTheOldestHorizon)
{
    section ("12 tempo re-anchors in 12 samples, each moving the clock back, just after a note started: it starts once (fix pass, HMS-2)");

    // The note at beat 2 starts at sample 48000. At 48480 (beat 2.02) the host says 1.97 at 119 bpm, then on each
    // of the next 11 samples one more bpm down and a sample's worth further back. The first rule (horizon 2.02)
    // is the one that keeps the note from starting again; the other 11 rules lie below beat 2. More than 8 rules
    // are live at once, so one must go: dropping the oldest (the first) let the note start a second time when
    // the clock reached beat 2 again (a node model of the scan put it at sample 49304).
    const model::Clip clip = clipOf (1, kFourShort);
    Rig rig (48000, tapeInst(), dcSample(), &clip);
    render::Script script;
    script.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 0)); });
    constexpr int kMoves = 12;
    for (int j = 0; j < kMoves; ++j)
        script.at (48480 + j, [j] (engine::Engine& e) { e.setTransport (playAt (119.0 - j, 1.97 - j / 24000.0)); });
    const double lastBeat = 1.97 - (kMoves - 1) / 24000.0, lastBpm = 119.0 - (kMoves - 1);
    const double spbLast = 60.0 / lastBpm * 48000;
    const double lastAt = 48480.0 + (kMoves - 1);
    int maxVoices = 0;
    double clockAfter = 0;
    auto frame = std::make_unique<engine::DisplayFrame>();
    std::vector<float> L, R;
    script.run (*rig.e, static_cast<std::int64_t> (lastAt + (3.5 - lastBeat) * spbLast), render::fixedBlocks (32), L, R,
                [&maxVoices, &clockAfter, &frame] (engine::Engine& e, std::int64_t k) {
                    maxVoices = std::max (maxVoices, e.stats().voices);
                    if (k == 48512)
                    {
                        e.fillDisplay (*frame);
                        clockAfter = frame->beat;
                    }
                });
    std::string why;
    const bool onTime = startsOnTime (soundStarts (L, R), { 0, 24000, 48000, lastAt + (3 - lastBeat) * spbLast }, why);
    check (clockAfter > 1.96 && clockAfter < 2.0,
           fmt ("the engine's clock at sample 48512 is beat %.5f: the moves put it behind the note at beat 2, which had started", clockAfter));
    check (onTime && maxVoices == 1,
           fmt ("beats 0, 1, 2 and 3 start once each (most voices at once %d) and on time %s", maxVoices, why.c_str()));
    check (L[52000] != 0.0f, "the note at beat 2 still sounds 4000 samples after its start (the slack of a backward re-anchor)");
}

SL_TEST ("engine", chainOnOneVoice)
{
    section ("a chain C3 -> G3 (Auto Slide legato) plays on one voice through the Engine: max voices 1");

    const model::Clip clip = clipOf (4, { { 60, 0, 2 }, { 67, 2, 2 } });
    const sounds::StarterSound saw = sounds::makeStarterSound (sounds::StarterId::saw, 1);
    Rig rig (48000, tapeInst(), engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw"), &clip);
    render::Script script;
    script.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 0)); });
    int maxVoices = 0;
    std::vector<float> L, R;
    script.run (*rig.e, 96000 + 4800, render::fixedBlocks (64),
                L, R, [&maxVoices] (engine::Engine& e, std::int64_t) { maxVoices = std::max (maxVoices, e.stats().voices); });
    const auto starts = soundStarts (L, R);
    check (maxVoices == 1 && starts.size() == 1 && starts[0] == 0,
           fmt ("one voice at most (%d), sounding from sample 0 without a gap at the joint (%zu stretches of sound)", maxVoices, starts.size()));

    // a no-op is not a pass: with a restart on the second note the chain breaks, and two voices overlap at the joint
    model::Clip broken = clip;
    model::Note second = *broken.notes[1];
    second.sample.restart = true;
    broken.notes[1] = std::make_shared<const model::Note> (second);
    Rig rig2 (48000, tapeInst(), engine::makeSampleData (saw.left, saw.right, saw.sampleRate, "saw"), &broken);
    render::Script script2;
    script2.at (0, [] (engine::Engine& e) { e.setTransport (playAt (120, 0)); });
    int maxVoices2 = 0;
    script2.run (*rig2.e, 96000, render::fixedBlocks (64), L, R,
                 [&maxVoices2] (engine::Engine& e, std::int64_t) { maxVoices2 = std::max (maxVoices2, e.stats().voices); });
    check (maxVoices2 == 2, fmt ("negative control: with Restart on the second note, %d voices overlap at the joint", maxVoices2));
}
