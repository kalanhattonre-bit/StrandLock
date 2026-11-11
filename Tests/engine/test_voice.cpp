// One voice, driven directly (M2 part 1; the Engine's scheduler, planner and output stage come in part 2). A chain
// C3 -> G3 plays on one Tape voice with no new attack and the playhead carrying on (M2's "chain" row), a chase
// lands where the note would be (the JS chase, bit for bit, up to 4096 ticks), the coarse chase of a long note
// lands within 0.002 of the straight render (plan 1.8), a voice started again in place plays exactly like a new
// one, the envelope times, the biquads' NaN guard (Stats::filterResets) and the ramp20ms smoother (plan 2.3).
// Where the fixtures do not cover a case, the prototype's own numbers for the same per-voice render come from the
// JS Voice driven the same way (scratch script e1/ref.js). Group "engine".

#include "engine/VoiceRig.h"
#include "support/Check.h"
#include "support/TestRegistry.h"
#include "support/TestSignals.h"

#include "core/Inst.h"
#include "core/Lanes.h"
#include "engine/Biquad.h"
#include "engine/InstSmoother.h"
#include "engine/Region.h"
#include "engine/Voice.h"
#include "model/Clip.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// VoiceTick.cpp reads StartSnap::mode as model::StartMode's values without including the model.
static_assert (static_cast<int> (sl::model::StartMode::region) == 0 && static_cast<int> (sl::model::StartMode::song) == 2
                   && static_cast<int> (sl::model::StartMode::random) == 3,
               "VoiceTick.cpp's kStartSong and kStartRandom, and a live note's Start (mode 0 = region)");

namespace
{
    using namespace sl;
    using namespace sl::test;

    rig::RawNote rawNote (const char* id, double p, double s, double l)
    {
        rig::RawNote n;
        n.id = id;
        n.p = p;
        n.s = s;
        n.l = l;
        return n;
    }

    /** The chain C3 -> G3 of test-engine.js: A (C3, 2 beats, a legato glide of +7 st over 0.5 beat) into B (G3, 3 beats). */
    std::vector<rig::RawNote> chainNotes()
    {
        rig::RawNote a = rawNote ("A", 60, 0, 2);
        a.hasGlide = true;
        a.glideD = 7;
        a.glideLen = 0.5;
        a.glideCurve = 0;
        a.glideTo = "B";
        return { a, rawNote ("B", 67, 2, 3) };
    }

    struct ChainRun
    {
        std::vector<float> L, R;
        double minEnv = 1;
        bool releasedEarly = false;
        int switches = 0;
    };

    /** Renders the chain's voice (the engine's startClip: at B's start sample the voice playing A switches to B). */
    ChainRun runChain (std::unique_ptr<engine::SampleData> src, std::int64_t n, bool chained)
    {
        rig::VoiceRig rg (48000);
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        rg.setInst (inst);
        rg.setSample (std::move (src));
        rg.setSnapshot (rig::rawSnapshot (chainNotes(), 4, chained));
        rg.play (120, 0, false);
        engine::Voice& v = rg.clip (0);
        const core::NoteSnap* B = &rg.snap->notes[1];
        const std::int64_t kB = rg.clock.kOf (B->s);
        ChainRun out;
        out.L.assign (static_cast<std::size_t> (n), 0.0f);
        out.R.assign (static_cast<std::size_t> (n), 0.0f);
        rg.render (v, out.L, out.R, 0, n, 64, { kB }, [&] (std::int64_t k) {
            if (k > kB - 4800 && k < kB + 4800)
                out.minEnv = std::min (out.minEnv, v.env);
            if (k == kB)
            {
                // the JS startClip: pid = !X.restart && X.chain && X.chain.prevId; the voice playing pid takes X over
                if (v.released)
                    out.releasedEarly = true;
                const core::ResolvedNote rb = core::resolve (*B, rg.inst);
                const core::NoteSnap* prev = ! B->restart ? engine::snapshotNote (rg.snap.get(), rb.chain.prevIndex) : nullptr;
                if (prev != nullptr && ! v.released && ! v.done && v.note == prev)
                {
                    v.switchTo (B, B->s);
                    ++out.switches;
                }
            }
        });
        return out;
    }

    double posOf (const engine::Voice& v, int head = 0)
    {
        const engine::Head& h = v.heads[static_cast<std::size_t> (head)];
        return (v.reg.rs + engine::mapPos (v.reg, h.raw + h.off)) / static_cast<double> (v.src.data->n);
    }

    double circularDistance (double a, double b)
    {
        const double d = std::abs (a - b);
        return std::min (d, 1 - d);
    }
}

SL_TEST ("engine", voiceChainOneVoice)
{
    section ("chain C3 -> G3 on one Tape voice: no new attack at the joint, the playhead carries on");

    // test-engine.js 'legato glide C3 -> G3 on one voice': tone (midiHz (60), 6, SR, { partials: 12 }), 4.9 beats.
    {
        signals::ToneOpts opts;
        opts.partials = 12;
        const ChainRun c = runChain (rig::sampleOf (signals::tone (signals::midiHz (60), 6, 48000, opts), 48000), 117600, true);
        const std::string hash = signals::hex8 (rig::hashOf (c.L, c.R));
        note (fmt ("min envelope within 0.1 s of the joint %.6f; hash %s (the prototype's per-voice render d00ff3fd, %s)", c.minEnv, hash.c_str(),
                   hash == "d00ff3fd" ? "bit for bit" : "not bit for bit"));
        check (c.switches == 1 && ! c.releasedEarly, "the voice playing A takes B over at B's start (one voice for the chain)");
        check (c.minEnv > 0.999, fmt ("no new attack at the hand-over: min envelope across the joint %.6f (> 0.999)", c.minEnv));
        check (rig::rms (c.L, 0, 117600) > 0.05, fmt ("the render is not silent (rms %.4f)", rig::rms (c.L, 0, 117600)));
    }

    // The playhead: rampSource (4, SR) encodes the read position; probes 400 samples before the joint and 4000 after.
    const signals::Signal ramp = signals::rampSource (4, 48000);
    const std::int64_t n = 62400, kB = 48000;
    const ChainRun c = runChain (rig::sampleOf (ramp), n, true);
    const double before = rig::decodeAt (c.L, c.R, kB - 400), after = rig::decodeAt (c.L, c.R, kB + 4000);
    const double jsBefore = 0.2616012913184508, jsAfter = 0.2959359382681376;   // the prototype's voice, e1/ref.js
    note (fmt ("read position: %.9f before the joint, %.9f after (the prototype %.9f, %.9f)", before, after, jsBefore, jsAfter));
    check (std::abs (after - jsAfter) < 0.002, fmt ("B continues A's playhead: after the joint %.6f, the prototype's %.6f +- 0.002", after, jsAfter));
    check (std::abs (before - jsBefore) < 1e-6 && std::abs (after - jsAfter) < 1e-6,
           "both probes are the prototype's within 1e-6 (the port plays the same positions)");
    check (after - before > 0.01, fmt ("the playhead moved forward across the joint (%.4f)", after - before));

    // A no-op is not a pass: B started on its own voice (not chained) restarts at the Region start, far from there.
    rig::VoiceRig rg (48000);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    rg.setInst (inst);
    rg.setSample (rig::sampleOf (ramp));
    rg.setSnapshot (rig::rawSnapshot (chainNotes(), 4, false));
    rg.play (120, 0, false);
    engine::Voice& vb = rg.clip (1);
    std::vector<float> L (static_cast<std::size_t> (n)), R (static_cast<std::size_t> (n));
    rg.render (vb, L, R, kB, n, 64);
    const double alone = rig::decodeAt (L, R, kB + 4000);
    check (std::abs (alone - jsAfter) > 0.002, fmt ("negative control: B on a new voice reads %.4f at the same probe (restarted), %.4f away", alone,
                                                    std::abs (alone - jsAfter)));
}

SL_TEST ("engine", voiceChase)
{
    section ("chase: a note chased at beat 4 starts part-way, as the JS chase (bit for bit up to 4096 ticks)");

    // test-engine.js 'chase', Tape: rampSource (8, SR), a 16-beat note, the transport started at beat 4.
    const signals::Signal ramp = signals::rampSource (8, 48000);
    const double rn = static_cast<double> (ramp.channels[0].size());
    rig::VoiceRig rg (48000);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    rg.setInst (inst);
    rg.setSample (rig::sampleOf (ramp));
    rg.setSnapshot (rig::rawSnapshot ({ rawNote ("ch", 60, 0, 16) }, 4, false));
    rg.play (120, 4, false);
    std::int64_t ticks = 0;
    engine::Voice& v = rg.clipChased (0, 0, &ticks);
    const std::int64_t n = 9600;
    std::vector<float> L (static_cast<std::size_t> (n)), R (static_cast<std::size_t> (n));
    rg.render (v, L, R, 0, n, 128);
    const double got = rig::decodeAt (L, R, 2400, 100), exp = (2 * 48000.0 + 2400) / rn;
    const double fade = rig::rms (L, 0, 120) / rig::rms (L, 600, 720);
    note (fmt ("chased read position %.9f (the prototype's voice %.9f; where the note would be %.6f); first sample %.6g (the prototype "
               "0.000269309559); rms 0-2.5 ms / 12.5-15 ms = %.4f; %lld ticks replayed; hash %s (the prototype bc6a878b)",
               got, 0.2562493661446489, exp, static_cast<double> (L[0]), fade, static_cast<long long> (ticks),
               signals::hex8 (rig::hashOf (L, R)).c_str()));
    check (ticks == 3001, fmt ("the chase replayed every tick from the note start: 1 + 3000 (%lld)", static_cast<long long> (ticks)));
    check (L[0] != 0.0f, "the chased note sounds from the first sample");
    check (fade > 0.02 && fade < 0.3, fmt ("the chase fades in over 10 ms: rms (0-2.5 ms) / rms (12.5-15 ms) = %.4f (0.02 .. 0.3)", fade));
    check (std::abs (got - exp) < 0.002, fmt ("the chased playhead is where the note would be: %.6f, %.6f +- 0.002", got, exp));
    check (std::abs (got - 0.2562493661446489) < 1e-6 && std::abs (static_cast<double> (L[0]) - 0.0002693095593713224) < 1e-9,
           "the read position and the first sample are the prototype's (within 1e-6 and 1e-9)");

    // The JS chase loop itself (tick, then every 32 samples ffAdvance, ffChain, tick; then the part tick), replayed
    // here on a second voice: under 4096 ticks the C++ chase is exactly that.
    auto naive = rg.newClipVoice (0);
    const std::int64_t k0 = rg.clock.kOf (0);
    std::int64_t kk = k0;
    naive->tick (kk, true);
    while (kk + 32 <= 0)
    {
        naive->ffAdvance (32);
        kk += 32;
        naive->ffChain (kk);
        naive->tick (kk, true);
    }
    naive->ffAdvance (static_cast<double> (0 - kk));
    auto chased = rg.newClipVoice (0);
    chased->chase (0);
    bool same = true;
    for (int h = 0; h < engine::kMaxHeads; ++h)
        same = same && naive->heads[static_cast<std::size_t> (h)].raw == chased->heads[static_cast<std::size_t> (h)].raw
               && naive->heads[static_cast<std::size_t> (h)].off == chased->heads[static_cast<std::size_t> (h)].off;
    check (same, "a chase of 3000 ticks equals the JS chase loop bit for bit (every head's raw and off)");
}

SL_TEST ("engine", voiceCoarseChase)
{
    section ("coarse chase (plan 1.8): a 64-beat Tape note with a moving Travel lane, chased 60 beats in");

    const signals::Signal ramp = signals::rampSource (8, 48000);
    rig::RawNote longNote = rawNote ("long", 60, 0, 64);
    longNote.lanes[static_cast<std::size_t> (core::LaneId::travel)].set = true;   // 0.4x .. 2.5x and back, three times
    longNote.lanes[static_cast<std::size_t> (core::LaneId::travel)].fn = [] (double u) { return 0.75 + 0.1 * std::sin (2 * 3.141592653589793 * 3 * u); };

    rig::VoiceRig rg (48000);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    rg.setInst (inst);
    rg.setSample (rig::sampleOf (ramp));
    rg.setSnapshot (rig::rawSnapshot ({ longNote }, 16, false));
    rg.play (120, 60, false);   // sample 0 is beat 60: the note started 1,440,000 samples (45,000 ticks) earlier
    const std::int64_t kStart = rg.clock.kOf (0);
    check (kStart == -1440000, fmt ("the note started at sample %lld", static_cast<long long> (kStart)));

    // The straight render: the voice played from its start to sample 0 (its output is not needed).
    auto straight = rg.newClipVoice (0);
    straight->startAt (kStart);
    {
        float scratchL[128], scratchR[128];
        for (std::int64_t k = kStart; k < 0; k += 128)
        {
            std::fill (scratchL, scratchL + 128, 0.0f);
            std::fill (scratchR, scratchR + 128, 0.0f);
            straight->render (scratchL, scratchR, 0, static_cast<int> (std::min<std::int64_t> (128, 0 - k)), k);
        }
    }
    const double want = posOf (*straight);

    // The coarse chase, in one go.
    auto coarse = rg.newClipVoice (0);
    const std::int64_t ticks = coarse->chase (0);
    const double got = posOf (*coarse);
    const double err = circularDistance (got, want);

    // The same chase spread over steps of 256 ticks (the engine's per-32-sample share) to the same point.
    auto stepped = rg.newClipVoice (0);
    std::int64_t steppedTicks = stepped->chaseBegin (0);
    int calls = 0;
    while (! stepped->chaseCaughtUp (0) && calls < 100000)
    {
        steppedTicks += stepped->chaseRun (0, 256);
        ++calls;
    }
    const bool silentWhileChasing = stepped->chasing;
    stepped->chaseEnd (0);
    bool same = steppedTicks == ticks && ! stepped->chasing;
    for (int h = 0; h < engine::kMaxHeads; ++h)
        same = same && stepped->heads[static_cast<std::size_t> (h)].raw == coarse->heads[static_cast<std::size_t> (h)].raw
               && stepped->heads[static_cast<std::size_t> (h)].off == coarse->heads[static_cast<std::size_t> (h)].off;

    // A no-op is not a pass: the straight 1x guess (45,000 ticks at the lane's 1x) is far from the real playhead.
    const double guess = std::fmod (1440000.0 / static_cast<double> (ramp.channels[0].size()), 1.0);
    note (fmt ("straight render %.6f, coarse chase %.6f (off by %.2e of the file); %lld ticks (the JS would replay 45001); a plain 1x guess "
               "%.6f",
               want, got, err, static_cast<long long> (ticks), guess));
    check (err < 0.002, fmt ("the coarse chase reaches the straight render's playhead within 0.002 (%.2e)", err));
    check (ticks <= 1 + 4096 + 16 + 45000 / 16 && ticks > 4096,
           fmt ("ticks: 4096 fine ones and one per 16 before them (%lld, at most %d)", static_cast<long long> (ticks), 1 + 4096 + 16 + 45000 / 16));
    check (same && silentWhileChasing && calls > 1,
           fmt ("the chase spread over %d steps of 256 ticks lands on the same playhead bit for bit, silent until it ends", calls));
    check (circularDistance (guess, want) > 0.002, fmt ("negative control: the 1x guess is %.4f away from the straight render", circularDistance (guess, want)));

    // A chased voice is silent until chaseEnd, then sounds.
    {
        auto v = rg.newClipVoice (0);
        v->chaseBegin (0);
        std::vector<float> L (256), R (256);
        v->render (L.data(), R.data(), 0, 256, 0);
        bool silent = std::all_of (L.begin(), L.end(), [] (float x) { return x == 0.0f; });
        v->chaseRun (0, std::numeric_limits<std::int64_t>::max());
        v->chaseEnd (0);
        v->startAt (0);
        v->render (L.data(), R.data(), 0, 256, 0);
        check (silent && rig::rms (L, 0, 256) > 0, "a voice still chasing renders nothing; after chaseEnd it sounds");
    }
}

SL_TEST ("engine", voiceInitInPlace)
{
    section ("Voice::initLive / initClip start a used voice in place: it plays exactly like a new one");

    // Settings that reach most of the tick: Scatter at 1/16, Refresh Synced 1/16, 3 heads with Spread and Drift,
    // Head tune detune, a Region with a seam, vibrato from the wheel.
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    inst.motion = core::Motion::scatter;
    inst.jump = core::RateId::r1_16;
    inst.refresh.mode = core::RefreshMode::sync;
    inst.heads = 3;
    inst.spread = 0.5;
    inst.drift = 0.6;
    inst.headTune = core::HeadTune::detune;
    inst.region.start = 0.2;
    inst.region.end = 0.6;
    signals::ToneOpts opts;
    const std::vector<float> src = signals::tone (signals::midiHz (55), 2, 44100, opts);

    auto renderOnce = [&] (rig::VoiceRig& rg, std::vector<float>& L, std::vector<float>& R) {
        engine::Voice& v = rg.live (62, 0.9, 0);
        rg.render (v, L, R, 0, static_cast<std::int64_t> (L.size()), 100);
    };
    const std::size_t n = 48000;
    rig::VoiceRig fresh (48000), reused (48000);
    for (rig::VoiceRig* r : { &fresh, &reused })
    {
        r->setInst (inst);
        r->setSample (rig::sampleOf (src, 44100));
        r->ctx.wheelTouched[0] = true;
        r->ctx.wheel[0] = 0.5;   // mod1 -> Vibrato (the default target)
    }
    std::vector<float> L1 (n), R1 (n), L2 (n), R2 (n), L3 (n), R3 (n);
    renderOnce (fresh, L1, R1);
    renderOnce (reused, L2, R2);   // first life
    reused.voice->steal();
    renderOnce (reused, L3, R3);   // the same object, started again
    check (L1 == L3 && R1 == R3, "a voice started again in place renders bit for bit what a new voice renders");
    check (L1 == L2 && rig::rms (L1, 0, static_cast<std::int64_t> (n)) > 0.01,
           fmt ("and the render is not silent (rms %.4f)", rig::rms (L1, 0, static_cast<std::int64_t> (n))));
    check (fresh.voice->asTape == false && fresh.voice->engineName == engine::Kc::tape, "a Tape note does not count as a Tape stand-in");

    // M3 part G: a Voice-engine note plays Voice (runGrains) and is no stand-in; a voice reused in place plays its
    // grains bit for bit as a new one (the grain slots, counters and schedule start again).
    core::Inst voiceInst = inst;
    voiceInst.engine = core::Engine::voice;
    rig::VoiceRig voiceFresh (48000), voiceReused (48000);
    for (rig::VoiceRig* r : { &voiceFresh, &voiceReused })
    {
        r->setInst (voiceInst);
        r->setSample (rig::sampleOf (src, 44100));
    }
    std::vector<float> L4 (n), R4 (n), L5 (n), R5 (n), L7 (n), R7 (n);
    renderOnce (voiceFresh, L4, R4);
    renderOnce (voiceReused, L5, R5);   // first life
    voiceReused.voice->steal();
    renderOnce (voiceReused, L7, R7);   // the same object, started again
    check (! voiceFresh.voice->asTape && voiceFresh.voice->engineName == engine::Kc::voice && voiceFresh.voice->kc == engine::Kc::voice,
           "a Voice-engine note plays Voice: asTape is clear, the display engine and the played engine are Voice");
    check (L4 == L5 && L4 == L7 && R4 == R7 && L4 != L1 && rig::rms (L4, 0, static_cast<std::int64_t> (n)) > 0.01,
           fmt ("a Voice voice started again in place renders bit for bit what a new one renders, not the Tape render (rms %.4f)",
                rig::rms (L4, 0, static_cast<std::int64_t> (n))));

    // Until M3 part L a Lock note (Cloud with the Lock switch) plays as Tape and says so; its display engine is Lock.
    core::Inst lockInst = inst;
    lockInst.engine = core::Engine::cloud;
    lockInst.lock = true;
    rig::VoiceRig lockRig (48000);
    lockRig.setInst (lockInst);
    lockRig.setSample (rig::sampleOf (src, 44100));
    std::vector<float> L6 (n), R6 (n);
    renderOnce (lockRig, L6, R6);
    const bool lockStandsIn = engine::playedEngine (engine::Kc::lock) == engine::Kc::tape;
    check (lockRig.voice->asTape == lockStandsIn && lockRig.voice->engineName == engine::Kc::lock
               && lockRig.voice->kc == engine::playedEngine (engine::Kc::lock),
           fmt ("a Lock note: asTape %d (expected %d until part L), the display engine Lock, the played engine %d", lockRig.voice->asTape ? 1 : 0,
                lockStandsIn ? 1 : 0, static_cast<int> (lockRig.voice->kc)));
}

SL_TEST ("engine", voiceEnvelope)
{
    section ("envelope: 8 ms attack, 60 ms release, 5 ms steal (one live Tape voice on a DC source)");

    auto run = [] (bool steal, std::int64_t& attackEnd, std::int64_t& tailEnd) {
        rig::VoiceRig rg (48000);
        core::Inst inst = core::defaultInst();
        inst.engine = core::Engine::tape;
        rg.setInst (inst);
        rg.setSample (rig::sampleOf (signals::dc (2, 48000, 0.5), 48000));
        engine::Voice& v = rg.live (60, 1);
        std::vector<float> L (48000), R (48000);
        attackEnd = -1;
        tailEnd = -1;
        rg.render (v, L, R, 0, 48000, 32, { 9600 }, [&] (std::int64_t k) {
            if (attackEnd < 0 && v.env >= 1)
                attackEnd = k;
            if (k == 9600)
            {
                if (steal)
                    v.steal();
                else
                    v.release();
            }
            if (tailEnd < 0 && v.done)
                tailEnd = k - 9600;
        });
        return v.done;
    };
    std::int64_t a1 = 0, r1 = 0, a2 = 0, r2 = 0;
    const bool doneR = run (false, a1, r1);
    const bool doneS = run (true, a2, r2);
    note (fmt ("attack reached 1 by sample %lld; release took %lld samples, steal %lld (block grid 32)", static_cast<long long> (a1),
               static_cast<long long> (r1), static_cast<long long> (r2)));
    check (a1 >= 384 && a1 <= 416, fmt ("attack: 8 ms = 384 samples (%lld, read on a 32-sample grid)", static_cast<long long> (a1)));
    check (doneR && r1 >= 2880 && r1 <= 2912, fmt ("release: 60 ms = 2880 samples, then done (%lld)", static_cast<long long> (r1)));
    check (doneS && r2 >= 240 && r2 <= 256, fmt ("steal: 5 ms = 240 samples, then done (%lld)", static_cast<long long> (r2)));
}

SL_TEST ("engine", voiceFilterGuard)
{
    section ("the biquads' NaN guard: a voice reading a NaN frame resets its filters, counts it and stays finite");

    engine::Biquad b;
    b.setLow (48000, 1000, 0.7071067811865476);
    double ok[32], bad[32];
    for (int j = 0; j < 32; ++j)
    {
        ok[j] = std::sin (j * 0.3);
        bad[j] = j == 7 ? std::numeric_limits<double>::quiet_NaN() : std::sin (j * 0.3);
    }
    const bool resetOk = b.run (ok, 32);
    const bool resetBad = b.run (bad, 32);
    bool finite = true;
    for (int j = 0; j < 32; ++j)
        finite = finite && std::isfinite (bad[j]);
    check (! resetOk && resetBad && finite && b.z1 == 0 && b.z2 == 0, "Biquad::run: a clean buffer passes, a NaN resets the state and zeroes the bad samples");

    // A source with one NaN frame: the Hermite reads it, the filters reset and count it.
    std::vector<float> src (48000);
    for (std::size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<float> (0.5 * std::sin (2 * 3.141592653589793 * 220.0 * static_cast<double> (i) / 48000));
    src[3000] = std::numeric_limits<float>::quiet_NaN();
    rig::VoiceRig rg (48000);
    core::Inst inst = core::defaultInst();
    inst.engine = core::Engine::tape;
    rg.setInst (inst);
    rg.setSample (rig::sampleOf (src, 48000));
    engine::Voice& v = rg.live (60, 1);
    std::vector<float> L (9600), R (9600);
    rg.render (v, L, R, 0, 9600, 128);
    bool allFinite = true;
    for (std::size_t i = 0; i < L.size(); ++i)
        allFinite = allFinite && std::isfinite (L[i]) && std::isfinite (R[i]);
    check (rg.ctx.filterResets > 0 && allFinite,
           fmt ("the voice's filters reset %lld times and its output stayed finite", static_cast<long long> (rg.ctx.filterResets)));

    rig::VoiceRig clean (48000);
    clean.setInst (inst);
    src[3000] = 0;
    clean.setSample (rig::sampleOf (src, 48000));
    engine::Voice& w = clean.live (60, 1);
    clean.render (w, L, R, 0, 9600, 128);
    check (clean.ctx.filterResets == 0, "negative control: the same source without the NaN resets nothing");
}

SL_TEST ("engine", instSmoother)
{
    section ("InstSmoother (plan 2.3): ramp20ms fields move linearly on the control grid, the others apply at once");

    engine::InstSmoother s;
    s.prepare (48000, 20);
    check (s.stepsPerRamp() == 30, fmt ("20 ms at 48 kHz is 30 steps of 32 samples (%d)", s.stepsPerRamp()));
    core::Inst a = core::defaultInst();
    s.setTarget (a);
    check (s.current().speed == 1 && ! s.ramping(), "the first target applies at once");

    core::Inst b = a;
    b.speed = 2;
    b.heads = 3;          // engine class: at once
    b.macros[0] = 0.9;    // ramp
    b.macros[1] = 0.1;    // engine class: at once
    b.outGain = -6;       // ramp
    s.setTarget (b);
    check (s.current().speed == 1 && s.current().heads == 3 && s.current().macros[1] == 0.1 && s.current().macros[0] == 0.5 && s.ramping(),
           "ramp20ms fields (speed, macro 1, outGain) start where they were; heads and macro 2 apply at once");
    for (int i = 0; i < 15; ++i)
        s.step();
    check (std::abs (s.current().speed - 1.5) < 1e-12 && std::abs (s.current().outGain - (-3)) < 1e-12,
           fmt ("half way (15 steps): speed %.15g, outGain %.15g", s.current().speed, s.current().outGain));
    s.setTarget (b);   // the same values again: nothing restarts
    for (int i = 0; i < 15; ++i)
        s.step();
    check (s.current().speed == 2 && s.current().outGain == -6 && s.current().macros[0] == 0.9 && ! s.ramping(),
           "after 30 steps every ramp is exactly on its target, and setting the same target again did not restart it");

    core::Inst c = b;
    c.speed = 4;
    s.setTarget (c);
    for (int i = 0; i < 10; ++i)
        s.step();
    c.speed = 1;
    s.setTarget (c);   // retarget mid-ramp: from where it is
    const double from = s.current().speed;
    s.step();
    check (from > 2 && from < 4 && std::abs (s.current().speed - (from + (1 - from) / 30)) < 1e-12,
           fmt ("a new target mid-ramp ramps from where the value is (%.6f -> 1 in 30 steps)", from));
    s.snap();
    check (s.current().speed == 1 && ! s.ramping(), "snap () puts every value on its target");

    s.setBypass (true);
    for (int i = 0; i < 10; ++i)
        s.step();
    const double mid = s.value (engine::InstSmoother::bypass);
    for (int i = 0; i < 20; ++i)
        s.step();
    check (std::abs (mid - 2.0 / 3) < 1e-12 && s.value (engine::InstSmoother::bypass) == 0, "the bypass fade goes 1 -> 0 over 30 steps");

    s.setRampMs (0);
    c.speed = 3;
    s.setTarget (c);
    check (s.current().speed == 3 && ! s.ramping(), "with a ramp time of 0 (the parity tests) a value jumps at setTarget");
}
