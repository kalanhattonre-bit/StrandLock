#pragma once

// sl::engine::Engine: the port of sl-engine.js's Engine (PLUGIN-PLAN 1.2, 1.3, 1.8, 1.10). This header is the
// M2 contract (plan 7.4): the engine chain implements it, the plugin chain codes against it. Keep the public
// API; private members and helpers are the engine chain's to change. Every comment says what the call does,
// from the JS, and where the plugin differs (plan 8.2).
//
// Threads. prepare (), forgetVersions () and the destructor run on the message thread while the audio thread is
// idle (prepareToPlay, releaseResources). Everything else runs on the audio thread, inside processBlock: it
// never allocates, locks, waits, logs or touches a file. Inputs land between process () calls, so a caller who
// wants an event on sample s renders up to s first (MidiRouter and HostSync split the block, plan 1.10).
//
// Versions. The engine keeps raw pointers into the Snapshot and SampleData it was given, and every voice keeps
// pointers into the version it plays (a deleted note's voice reads its old note through its 60 ms release; a
// voice stolen by a sample swap fades on the old sample). minEpochInUse () tells rt::VersionExchange::setInUse
// which versions are still read, so the message side frees only what nothing reads.
//
// The processor's block, in order (plan 1.3; Integration builds it):
//   if (snapshots.acquire()) engine.setSnapshot (snapshots.current(), snapshots.currentEpoch());
//   if (samples.acquire())   engine.setSample (samples.current(), samples.currentEpoch());
//   engine.setInst (ParamBridge::readInst (...)); engine.setBypass (...);
//   commands, MIDI and transport, each at its sample, with engine.process () for the stretches between;
//   snapshots.setInUse (engine.minEpochInUse (VersionKind::snapshot));
//   samples.setInUse (engine.minEpochInUse (VersionKind::sample));
//   every displayEvery (sr) samples: engine.fillDisplay (displaySlot.beginWrite()); displaySlot.endWrite();

#include "core/Inst.h"
#include "core/Snapshot.h"
#include "engine/Clock.h"
#include "engine/DisplayFrame.h"
#include "engine/EngineConfig.h"
#include "engine/InstSmoother.h"
#include "engine/SampleData.h"
#include "engine/Voice.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>

namespace sl::engine
{
    class Voice;   // engine/Voice.h (engine chain): one sounding chain
    class Head;    // engine/Head.h (engine chain): one playhead of a voice

    /** setController's names: the JS 'mod1', 'mod2', 'bend' and 'sustain'. */
    enum class Ctrl : std::uint8_t { mod1 = 0, mod2 = 1, bend = 2, sustain = 3 };

    /** The two kinds of version the engine reads (minEpochInUse). */
    enum class VersionKind : std::uint8_t { snapshot = 0, sample = 1 };

    /** setTransport's argument (plan 1.10; HostSync makes it once per block and at a cycle wrap inside one). */
    struct Transport
    {
        bool playing = false;
        double beat = std::numeric_limits<double>::quiet_NaN();   // the song beat at this sample (host ppq minus
                                                                  // the clip's offsetBeats); NaN = none
        double bpm = 120.0;                                       // NaN = keep the engine's tempo (JS fin (t.bpm, this.bpm))
        bool loop = true;                                         // the clip's loop setting
    };

    /** noteOn's options (JS opts). */
    struct NoteOnOpts
    {
        double startPos = std::numeric_limits<double>::quiet_NaN();   // 0..1 of the Region: where the heads start
                                                                      // (hold-on-waveform audition); NaN = none
    };

    /** stats (): everything the JS stats () returns, plus the plugin's counters (plan 1.2, 1.8). */
    struct Stats
    {
        // The JS stats (), in its order (the fixtures' stats rows).
        std::int32_t voices = 0;          // voices that are not done
        std::int32_t grains = 0;          // st.load: the measured load of the stretch rendered last (grains + Lock taps
                                          // + Tape heads)
        std::int32_t thinnedHeads = 0;    // st.thinned: heads that want to sound but the budget holds back
        std::int32_t maxGrains = 0;       // st.maxLoad: the highest load since resetStats
        std::int32_t budget = 0;          // B = max (16, floor (192 x 48000 / sr))
        std::int64_t cuts = 0;            // fade tails cut at once (last resort, and a full 64-voice pool)
        std::int64_t steals = 0;          // voices stolen by polyphony (5 ms fade)

        // The plugin's additions.
        std::int64_t deferredStarts = 0;  // starts past the 256 of one segment, moved to the next segment (1.2)
        std::int64_t chaseTicks = 0;      // control ticks replayed by chases (1.8)
        std::int64_t nonFinite = 0;       // output samples the guard replaced, counted before the limiter (1.8)
        std::int64_t filterResets = 0;    // biquad NaN resets (1.8)
        double preLimiterPeak = 0.0;      // the largest |sample| after outGain, before the limiter

        // M2 until M3 (E1's addition, NOTES-M2): voices that are not done whose note asks for Voice, Cloud or Lock
        // and plays as Tape (Voice::asTape), so the diagnostics page can say so. Always 0 once M3 plays the grains.
        std::int32_t tapeStandIns = 0;
    };

    class Engine
    {
    public:
        /** The JS constructor's defaults (48 kHz, defaultInst, no sample, no snapshot, stopped at beat 0).
            Allocates nothing: call prepare before process. */
        Engine() noexcept;
        ~Engine();

        Engine (const Engine&) = delete;
        Engine& operator= (const Engine&) = delete;

        //==========================================================================================
        // Message thread, while the audio thread is idle.

        /** The JS constructor: sr = clamp (sampleRate, 8000, 384000) (48000 if not finite), the budget for that
            rate, the limiter's release coefficient. Allocates the 64-voice pool the first time (the only place the
            engine allocates); later calls reuse it. Resets everything else as a new engine: no voices, stopped,
            beat 0, clocks, control grid, limiter, ramps and stats at rest. Drops the snapshot and the sample (the
            processor hands them back: VersionExchange::setPrepared (true) makes the next acquire () true) and
            keeps the inst. */
        void prepare (double sampleRate);

        /** releaseResources: every voice stops at once and the engine forgets its snapshot and sample, so it
            points at no version the message side may free once the exchanges are not prepared. Allocates
            nothing. */
        void forgetVersions() noexcept;

        //==========================================================================================
        // Audio thread.

        /** AudioProcessor::reset: every voice stops at once (no fade), the live key stack, the wheels, bend and
            sustain clear, the limiter and the ramps go to rest (ramps snap to their targets). Keeps the sample,
            snapshot, inst, transport and clocks. */
        void reset() noexcept;

        /** JS setSample: every voice is stolen (5 ms fade). C++ change (8.2 #23): a stolen voice finishes its
            fade reading the sample it was playing (it keeps that sample's epoch until it is done). nullptr = no
            sound: process renders silence and noteOn is ignored. The source rate played is
            clamp (sample->sr, 4000, 768000), or the engine's rate when sample->sr is not finite. */
        void setSample (const SampleData* sample, std::uint64_t epoch) noexcept;

        /** JS setInst: the engine's own clamps (core::clampForEngine) and outGainLin = 10^(outGain / 20). Call it
            every block with the parameters' values. Parameters of the ramp20ms class (macros, offsets, outGain,
            speed, drift, pitch, rootCents; plan 2.1, 2.3) move to the new value over the ramp time, one step per
            32-sample boundary of the control grid (outGain and the bypass fade interpolated per sample in the
            output loop); the others apply at the next tick, as the prototype. A value that does not change never
            ramps. */
        void setInst (const core::Inst& inst) noexcept;

        /** JS setSnapshot: the clip the engine plays (nullptr = no clip). Lb = max (1, bars) x max (1,
            beatsPerBar). Notes the JS filter drops (non-finite p, s or l, or l <= 0) are skipped. Every clip voice
            that is not done follows its note by id (Snapshot::byHash): a note that survived moves the voice to the
            new note at once (U = s + m Lb) and to this epoch; a deleted note's voice releases and keeps reading
            its old note, and its old epoch, until it is done. The engine resolves the "follow the instrument"
            markers every tick with core::resolve (plan 1.4). */
        void setSnapshot (const core::Snapshot* snapshot, std::uint64_t epoch) noexcept;

        /** JS setTransport (plan 1.10). Starting: anchor at beat (0 if NaN), chase pending, the control grid
            restarts here (8.2 #21). Stopping: clip voices release; the free clock continues from beat (or from
            where the song was). Playing: a beat more than 0.1 beat from the engine's own clock (reduced modulo the
            clip with loop on) is a jump (clip voices release, chase at the new place); a smaller difference with an
            unchanged tempo and loop changes nothing; a tempo or loop change re-anchors to the host, cur + d
            (8.2 #3, with the scan horizon). Stopped: a beat (not NaN) or a tempo change re-anchors the free clock.
            Negative beats are never wrapped into the clip (8.2 #4). bpm is clamped to 10..999. */
        void setTransport (const Transport& transport) noexcept;

        /** JS noteOn: a live key. p = clamp (round (p), 0, 127); vel above 1 reads as 0..127 (MidiRouter sends
            vel / 127); vel <= 0 is noteOff. Ignored with no sample. With liveLegato and a key held, the newest held
            voice takes the new pitch and keeps its playhead. Otherwise a held voice on the same key releases and
            a new voice starts (id "live:" + p), stealing the oldest (released first, 5 ms) above polyphony. */
        void noteOn (double p, double vel, const NoteOnOpts& opts = {}) noexcept;

        /** JS noteOff: with liveLegato and other keys still held, the voice falls back to the last of them (same
            playhead); with sustain down it is held until sustain lifts; otherwise it releases (60 ms). */
        void noteOff (double p) noexcept;

        /** JS allNotesOff: every voice is stolen (5 ms fade) and the live key stack clears. MidiRouter sends
            CC120 and CC123 here. */
        void allNotesOff() noexcept;

        /** JS setController (name, value). mod1, mod2: the wheel's value 0..1 rides the wheel's target lane on live
            voices (a NaN value lets go, as releaseController). bend: -1..1 (x bendRange semitones on live voices).
            sustain: down when value >= 0.5; lifting it releases every sustained voice whose key is no longer held. */
        void setController (Ctrl controller, double value) noexcept;

        /** JS setController (name, null): mod1, mod2 let go (the lanes rule again); bend returns to 0; sustain
            lifts. */
        void releaseController (Ctrl controller) noexcept;

        /** JS process: overwrites L and R (both non-null, n samples) with the next n samples. Splits at the
            32-sample control grid and at note starts; chases (bounded, 8.2 #22), scans starts (binary search,
            8.2 #29), plans the budget, renders the voices in their order (float accumulation, plan 1.6 rule 10),
            then outGain, the bypass fade, the output guard (non-finite or beyond +-1e6 -> 0, counted), the
            limiter (instant attack, 80 ms release, ceiling 0.9) and the clamp at +-0.98. With no sample it
            renders silence and only the clock moves. n <= 0 does nothing. */
        void process (float* L, float* R, int n) noexcept;

        /** JS getDisplay () + stats () into the frame (plan 1.5): the voices that are not done, in voice order, with
            their heads that are on; up to 128 grains; the beat (songBeat), playing, the sample time; the output
            peaks since the last call (then reset). Writes every field the frame's counts cover. */
        void fillDisplay (DisplayFrame& frame) noexcept;

        /** JS stats (), plus the plugin's counters. */
        Stats stats() const noexcept;

        /** JS resetStats (maxGrains, cuts, steals), plus every plugin counter and the pre-limiter peak. */
        void resetStats() noexcept;

        /** The lowest epoch of that kind the engine still reads: its current snapshot (or sample) and every voice
            that is not done. rt::kNoEpoch when it reads none (VersionExchange::setInUse clamps that). */
        std::uint64_t minEpochInUse (VersionKind kind) const noexcept;

        //==========================================================================================
        // The plugin's additions (plan 2.3, 2.1 #49), audio thread.

        /** The ramp20ms smoothers' time (default 20 ms; 0 = every value jumps at the next tick, as the prototype:
            the parity tests set 0). */
        void setRampMs (double ms) noexcept;

        /** The host bypass (parameter 49): the output fades to silence (true) or back (false) over the ramp
            time, per sample; the voices keep running underneath. */
        void setBypass (bool bypassed) noexcept;

        /** Sets every ramp to its target now (state load, plan 2.3). */
        void snapRamps() noexcept;

        //==========================================================================================
        // Read-only (any thread while the audio thread is idle; the audio thread itself at any time).

        double sampleRate() const noexcept { return sr; }

        /** Samples rendered since prepare (JS this.k). */
        std::int64_t sampleTime() const noexcept { return k; }

    private:
        struct VoicePoolDeleter
        {
            void operator() (Voice* pool) const noexcept;   // Engine.cpp: delete[] once Voice is complete
        };

        /** One start found by scanStarts (the JS {kS, X, U, m}). kKey is the sample the start belongs to (the
            sort key, as the JS kS); kS is where it really starts (kKey, or the segment's first sample for a start
            deferred past the 256 of a segment or caught up after a forward re-anchor). */
        struct StartEvent
        {
            std::int64_t kS = 0, kKey = 0;
            double U = 0, s = 0, p = 0;
            std::int64_t m = 0;
            std::int32_t index = 0;   // the note in snap->notes
        };

        /** A clock that was replaced at a tempo or loop change (8.2 #3): every start with kOf (U) < kR under
            it was already emitted, so scanStarts never emits it again. Expires once the scan has passed it. */
        struct ScanRule
        {
            double anchorBeat = 0, db = 1, horizonU = 0;
            std::int64_t anchorK = 0, kR = 0;
        };

        static constexpr int kMaxScanRules = 8;
        static constexpr std::int64_t kChaseNotBegun = std::numeric_limits<std::int64_t>::min();

        //------------------------------------------------------------------------------------------
        // Engine.cpp
        Voice& voiceAt (int slot) noexcept { return voicePool[static_cast<std::size_t> (slot)]; }
        const Voice& voiceAt (int slot) const noexcept { return voicePool[static_cast<std::size_t> (slot)]; }
        void releaseClipVoices() noexcept;
        void dropReleasedChases() noexcept;
        void compactVoices() noexcept;
        void clearScanState() noexcept;
        void liveStackRemove (int p) noexcept;
        bool liveStackHas (int p) const noexcept;
        void outputStage (float* L, float* R, int from, int count, int gridPos) noexcept;
        bool noteOk (const core::NoteSnap& X) const noexcept;

        // EngineSchedule.cpp
        Voice* allocVoice() noexcept;
        void admitVoice (int slot) noexcept;
        void addVoice (int slot, std::int64_t k0) noexcept;
        void scanStarts (std::int64_t k0, std::int64_t k1) noexcept;
        void collectStarts (double ua, double ub, std::int64_t k0, std::int64_t k1, std::int64_t lowK) noexcept;
        void offerStart (std::int32_t index, std::int64_t m, double U, std::int64_t k0, std::int64_t k1, std::int64_t lowK) noexcept;
        bool suppressed (double U) const noexcept;
        void startClip (const StartEvent& e) noexcept;
        void doChase (std::int64_t kNow) noexcept;
        void continueChases (std::int64_t kNow) noexcept;
        void finishChase (int slot, std::int64_t kNow) noexcept;

        // EnginePlan.cpp
        void plan() noexcept;
        void measure() noexcept;

        //------------------------------------------------------------------------------------------
        std::unique_ptr<Voice[], VoicePoolDeleter> voicePool;   // kVoicePool voices, made by the first prepare
        std::array<std::uint8_t, kVoicePool> order {};          // the voice list: pool slots in insertion order
        int numVoices = 0;
        std::array<bool, kVoicePool> slotUsed {};
        std::uint64_t serial = 0;
        bool anyChasing = false;

        double sr = kDefaultSampleRate;
        std::int64_t k = 0;
        InstSmoother smoother;                  // what the voices read: ctx.inst points at smoother.current ()
        VoiceContext ctx;
        Clock clock;
        const core::Snapshot* snap = nullptr;
        std::uint64_t snapEpoch = 0;
        int noteCount = 0;                      // notes the JS filter keeps (this.notes.length)
        bool notesSearchable = false;           // every note kept and sorted by s: scanStarts may binary search
        const SampleData* src = nullptr;
        std::uint64_t srcEpoch = 0;
        VoiceSource source;                     // the current sample at its played rate (new voices get it)

        // transport (JS pendingChase, playStartK) and the plugin's control grid origin (8.2 #21)
        bool pendingChase = false;
        std::int64_t playStartK = 0;
        std::int64_t gridOrigin = 0;

        // live input
        std::array<std::uint8_t, 128> liveStack {};
        int liveCount = 0;
        bool sustain = false;

        // scanStarts: the fixed start list, the deferral past 256 starts, the scan rules of re-anchors
        std::array<StartEvent, kMaxStarts> starts {};
        int numStarts = 0;
        std::int64_t startCandidates = 0;
        bool deferActive = false;
        StartEvent deferCut {};                 // the last start emitted before the deferral (its key)
        std::array<ScanRule, kMaxScanRules> rules {};
        int numRules = 0;
        bool catchUpPending = false;
        double catchUpLowU = 0;

        // plan (): the wait list holds (slot, head) pairs; scratch voice orders
        std::array<std::uint16_t, kWaitList> waitList {};
        std::array<std::uint8_t, kVoicePool> scratch {};

        // output
        int budget = kBudget48k;
        double limG = 1, limRel = 0;
        double cellOut0 = 0, cellOut1 = 0, cellByp0 = 1, cellByp1 = 1;   // outGain (dB) and bypass at the grid cell's ends
        double rampMs = kRampMs;
        Stats st;
        float peakL = 0.0f, peakR = 0.0f;
    };
}
