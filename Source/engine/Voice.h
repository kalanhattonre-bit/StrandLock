#pragma once

// sl-engine.js Voice: one sounding note, or one chain of slid notes (PLUGIN-PLAN 1.2, 1.8, 1.9). The engine keeps
// 64 of them in a pool and starts each one in place with initClip or initLive (the pool never allocates).
//
// What a voice reads from its engine (the JS voice's E) is in a VoiceContext, which the engine owns and keeps up
// to date: the rate, the instrument of the moment, the beat clock, the wheels and bend. A voice keeps its own
// source (the sample it started on) and its own snapshot (the version its note is in) with their epochs, so a
// voice stolen by a sample swap fades on its old sample (8.2 #23) and a deleted note's voice releases on its old
// note (plan 1.3).
//
// The class is a port, so its fields keep the JS names and are public: the engine's planner, display and
// scheduler read and write them as the JS engine does.
//
// Files: Voice.cpp (init, setNote, switchTo, follow, release, steal), VoiceTick.cpp (tick), VoiceRender.cpp
// (render, renderRun, runTape), VoiceChase.cpp (chase, ffAdvance, ffChain); M3 adds VoiceGrains.cpp (runGrains)
// and VoiceLock.cpp (lockGrab, lockRho, runLock).

#include "core/Inst.h"
#include "core/Snapshot.h"
#include "engine/Biquad.h"
#include "engine/Clock.h"
#include "engine/EngineConfig.h"
#include "engine/Head.h"
#include "engine/Region.h"
#include "engine/SampleData.h"

#include <array>
#include <cstdint>
#include <limits>

namespace sl::engine
{
    /** What every voice reads from the engine at its ticks. The engine owns it; voices hold a pointer. */
    struct VoiceContext
    {
        double sr = kDefaultSampleRate;       // E.sr
        const core::Inst* inst = nullptr;     // E.inst: the instrument the voices read at their next tick
        const Clock* clock = nullptr;         // E's clock (uAt, kOf, loopBeat, songBeat, effLen, Lb, bpm)
        double wheel[2] = { 0, 0 };           // E.wheel: mod1, mod2 values 0..1
        bool wheelTouched[2] = { false, false };
        double bend = 0;                      // E.bend: -1..1
        std::int64_t filterResets = 0;        // biquad NaN resets, counted by every voice (Stats::filterResets)
    };

    /** The sample a voice plays, with the rate it plays it at, for the voice's whole life. */
    struct VoiceSource
    {
        const SampleData* data = nullptr;
        double sr = kDefaultSampleRate;   // S.sr: clamp (fin (data->sr, the engine's rate), 4000, 768000)
        std::uint64_t epoch = 0;

        /** The JS setSample's rate rule for this sample at an engine rate. */
        static VoiceSource of (const SampleData* sample, std::uint64_t sampleEpoch, double engineRate) noexcept;
    };

    /** The envelope's stage (JS ST_ATTACK .. ST_STEAL). */
    enum class EnvStage : std::uint8_t { attack = 1, sustain = 2, release = 3, steal = 4 };

    /** How a voice's heads travel this tick: the note's kind, or Tape's Path-as-1x (JS 'tapepath'). */
    enum class TravelPlay : std::uint8_t { speed, path, tapePath };

    /** The M2 -> M3 switch (plan 4, M2: "Notes whose engine is Voice or Cloud play as Tape until M3").
        Voice::tick works out the engine a note asks for and plays the one this returns, and Voice::asTape says
        which voices stood in as Tape. Since M3 part L every engine plays itself: Voice and Cloud play runGrains
        (VoiceGrains.cpp, part G), Lock plays runLock (VoiceLock.cpp, part L), so no note stands in any more.
        The function stays as the one place the switch was, and the stand-in tests check it plays every engine. */
    constexpr Kc playedEngine (Kc wanted) noexcept
    {
        return wanted;
    }

    /** A Snapshot note the JS engine would hold in its byId map (JS setSnapshot's filter: finite p, s and l, and
        l > 0), by index; nullptr for -1, an index out of range or a note the filter drops. */
    const core::NoteSnap* snapshotNote (const core::Snapshot* snap, std::int32_t index) noexcept;

    /** idHash ("live:" + p) for a whole p in 0..127, without building a string (audio thread). */
    std::uint32_t liveIdHash (int p) noexcept;

    class Voice
    {
    public:
        Voice() = default;
        Voice (const Voice&) = delete;
        Voice& operator= (const Voice&) = delete;

        //==========================================================================================
        // Starting (the JS constructor, in place) and following the clip (Voice.cpp).

        /** new Voice (E, 'clip', note, U, m, kStart): the note snapshot->notes[noteIndex] starting at unwrapped
            beat U in loop pass m, at sample kStart. serial is the engine's ++E.serial. The drift and tiny head
            offsets are seeded from the chain's first note under the instrument of the moment (ctx.inst). */
        void initClip (VoiceContext& context, const VoiceSource& source, const core::Snapshot* snapshot, std::uint64_t snapshotEpoch,
                       std::int32_t noteIndex, double startBeat, std::int64_t pass, std::int64_t kStart, std::uint64_t voiceSerial) noexcept;

        /** new Voice (E, 'live', note, 0, 0, E.k) for a live key: p a whole number 0..127, vel 0..1. The note is
            the JS live note: id "live:" + p, s 0, l Infinity, no lanes, no glide, no chain, Start at the Region
            start. */
        void initLive (VoiceContext& context, const VoiceSource& source, int p, double vel, std::int64_t kStart, std::uint64_t voiceSerial) noexcept;

        /** setNote (X): the note this voice reads (its lanes, its id hash). */
        void setNote (const core::NoteSnap* X) noexcept;

        /** switchTo (X, U): the chain hand-over (D2): same voice, no new attack; the playhead, grain phase and
            vibrato phase carry on. X is a note of this voice's snapshot. */
        void switchTo (const core::NoteSnap* X, double startBeat) noexcept;

        /** The JS setSnapshot's "edited notes follow at once": the voice moves to X in the new snapshot (and its
            epoch) at unwrapped beat U = X.s + m Lb. */
        void follow (const core::Snapshot* snapshot, std::uint64_t epoch, const core::NoteSnap* X, double startBeat) noexcept;

        /** Live legato and the fallback key (the JS note = Object.assign ({}, note, { p, vel })): a live voice
            takes another pitch (and velocity) and keeps its id and playhead. */
        void setLivePitch (int p, double vel) noexcept;
        void setLivePitch (int p) noexcept;

        void release() noexcept;
        void steal() noexcept;

        /** The first control tick at the voice's own start sample, then render skips its first tick (the JS
            addVoice: v.tick (k0, false); v.pre = true; the engine plans right after). */
        void startAt (std::int64_t k0) noexcept
        {
            tick (k0, false);
            pre = true;
        }

        //==========================================================================================
        // The control tick (VoiceTick.cpp).

        /** tick (kAbs, ff): every 32 samples from the voice's start. Lanes, wheels, offsets, macros; pitch with
            glides, vibrato and bend; Refresh and Scatter events; the Region; the heads (count, levels, Spread,
            pan, speeds, Drift, Head tune, start positions); per-voice filters and gain. ff = fast-forward (chase):
            no release decisions, no gains, no filters. */
        void tick (std::int64_t kAbs, bool ff) noexcept;

        //==========================================================================================
        // Sound (VoiceRender.cpp).

        /** render (outL, outR, o, n, k0): adds n samples to out[o .. o + n) (Float32 accumulation: read the
            float, add in double, store a float), ticking at every 32 samples of the voice's own grid. k0 is the
            engine sample of out[o]. Stops when the voice is done; does nothing while a bounded chase runs. */
        void render (float* outL, float* outR, int o, int n, std::int64_t k0) noexcept;

        /** renderRun: one stretch inside a tick (run <= 32 - ph): every head that is on, the filters, the
            envelope and the gain. */
        void renderRun (float* outL, float* outR, int o, int run) noexcept;

        /** runTape (h, run): Hermite reads at the head's speed; Scatter jumps and Refresh stutters crossfade 5 ms. */
        void runTape (Head& h, int run) noexcept;

        /** runGrains (h, run, voice) (VoiceGrains.cpp): Voice (voice = true: root-grid PSOLA, the formant kept)
            and Cloud (free grains in ms, Spray) grains, Hann windows with fractional start times; at most
            min (grant, 8 for Voice, 5 for Cloud) grains per head, and when full the oldest grain gives way. */
        void runGrains (Head& h, int run, bool voice) noexcept;

        /** lockGrab (h, x) (VoiceLock.cpp): where a Lock capture starts for a head at x (grabPos, plus Spray around
            it); counts the grab in h.gIdx. */
        double lockGrab (Head& h, double x) noexcept;

        /** lockRho (h): the correlation of the head's old and new capture over one period (16 phases, both
            channels summed), 0..1. */
        double lockRho (const Head& h) const noexcept;

        /** runLock (h, run): Cloud with the Lock switch (GrainLock's trick): N Hann-weighted taps one period apart
            on the head's capture, N <= min (12, half the grant); a re-grab crossfades the old capture into the new
            one over one period with the correlation correction. */
        void runLock (Head& h, int run) noexcept;

        //==========================================================================================
        // Chase (VoiceChase.cpp, PLUGIN-PLAN 1.8 and 8.2 #22).

        /** The JS chase (kNow), all at once: replays the control ticks from the chain's first note to kNow
            without sound, so the playhead lands where it would be had the note played from its start; then
            first = true, vt = ph = 0, the 10 ms chase fade-in, every head's grains cleared. Ticks more than 4096
            ticks before kNow are coarse (tick once per 16, ffAdvance (512)). Returns the ticks it ran. */
        std::int64_t chase (std::int64_t kNow) noexcept;

        /** The same in steps, for a chase spread over blocks (the engine's 4096-now, 256-per-32 rule).
            chaseBegin: the first tick at the note start (kOf (U), at most 2^17 ticks before kNow); the voice
            is silent from here until chaseEnd. Returns 1 (the tick it ran). */
        std::int64_t chaseBegin (std::int64_t kNow) noexcept;

        /** Runs whole ticks toward kNow, at most budget of them, coarse while more than 4096 ticks remain.
            Returns the ticks run. */
        std::int64_t chaseRun (std::int64_t kNow, std::int64_t budget) noexcept;

        /** True when no whole tick is left before kNow. */
        bool chaseCaughtUp (std::int64_t kNow) const noexcept { return chaseK + kCtrl > kNow; }

        /** The last part tick (ffAdvance (kNow - kk)) and the chase's end state; the voice sounds again. */
        void chaseEnd (std::int64_t kNow) noexcept;

        /** ffAdvance (n): every head moves n samples at its speed (and its Spread offset at its slew). */
        void ffAdvance (double n) noexcept;

        /** The coarse chase step (n = 512): ffAdvance (n), except that heads steered along a Path (pathSteer) move
            one tick (32 samples) at their speed, which puts them on the last tick's target, and stay there. At
            their steering speed a 512-sample step would overshoot by 15 times the distance and run away. */
        void ffAdvanceCoarse (double n) noexcept;

        /** ffChain (kk): while chasing, hand over to every chained note that has started by sample kk. */
        void ffChain (std::int64_t kk) noexcept;

        //==========================================================================================
        // Helpers.

        /** The resolved settings of the voice's note under the instrument of the moment (clip voices). */
        core::ResolvedNote resolved() const noexcept { return core::resolve (*note, *ctx->inst); }

        /** The next and previous notes of the voice's chain (in the voice's snapshot), or nullptr. */
        const core::NoteSnap* chainNext() const noexcept;
        const core::NoteSnap* chainPrev() const noexcept;

        //==========================================================================================
        // State (the JS fields). Public: the engine reads and writes them as the JS engine does.

        VoiceContext* ctx = nullptr;
        VoiceSource src;
        const core::Snapshot* snap = nullptr;   // clip voices: the version the note is in
        std::uint64_t snapEpoch = 0;

        VoiceKind kind = VoiceKind::clip;
        const core::NoteSnap* note = nullptr;   // a snapshot note, or liveNote
        core::NoteSnap liveNote;                // a live key's note
        double U = 0;                           // unwrapped beat where the note starts
        std::int64_t m = 0;                     // loop pass
        std::int64_t startK = 0;
        std::uint64_t serial = 0;

        std::array<Head, kMaxHeads> heads;
        std::int64_t vt = 0;                    // samples rendered (grain time)
        int ph = 0;                             // samples into the current tick, 0..31
        bool first = true, posInit = false, pre = false;
        double startPos = std::numeric_limits<double>::quiet_NaN();   // 0..1 of the Region, NaN = none

        double env = 0;
        EnvStage st = EnvStage::attack;
        double aStep = 0, rStep = 0, sStep = 0;
        bool released = false, done = false, bridging = false, sustained = false;
        int key = -1;                           // a live voice's key, -1 = none (JS null)

        double vibPh = 0, refPh = 0, refIdx = 0, jIdx = 0;
        bool refInit = false, jInit = false;

        Biquad hL, hR, fL, fR, lL, lR;          // hollow cut, formant EQ, tone low-pass
        double g0 = 0, g1 = 0, dg = 0;          // the voice gain at the tick's start and end, per-sample step
        double bufL[kCtrl] {}, bufR[kCtrl] {};

        RegionState reg;
        double lanes[core::kNumLanes] {};

        Kc kc = Kc::voice;                      // the engine played this tick (playedEngine)
        int lastKc = -1;
        double Hf = 1, level = 0, semis = 0;
        std::int32_t nh = 0;                    // the note's idHash as JS `| 0`
        bool rev = false;

        Kc engineName = Kc::voice;              // the engine the note asks for (the display's engine)
        core::Motion motion = core::Motion::forward;   // the motion as played
        bool asTape = false;                    // the note plays another engine than it asks for (never since M3 part L)
        double lpHz = 0, fmtDb = 0, fmtHz = 0, hollowDb = 0;

        // 8.2 #3: after a tempo change re-anchored the clock back by up to 0.1 beat, a voice that had already started
        // is up to that much ahead of the clock; it keeps sounding (the JS rule u < U releases only a note edited to
        // start later). Set by the engine, cleared once the clock reaches U.
        double reanchorSlack = 0;

        // A chase spread over blocks (chaseBegin .. chaseEnd).
        bool chasing = false;
        std::int64_t chaseK = 0;                // the sample the replayed ticks have reached
        // The last tick steered the heads along a Path (Path kind on Voice, Cloud or Lock: spd = (target - raw) / 32,
        // which lands on the target in one tick). A coarse chase step moves such heads one tick, not 512 samples.
        bool pathSteer = false;

    private:
        void initCommon (VoiceContext& context, const VoiceSource& source, VoiceKind voiceKind, double startBeat, std::int64_t pass,
                         std::int64_t kStart, std::uint64_t voiceSerial) noexcept;
        void seedHeads() noexcept;
        void chaseFinishState() noexcept;
    };
}
