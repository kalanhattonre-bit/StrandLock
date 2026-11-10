// sl-engine.js Engine: the inputs, the clocks, process () and the output stage (PLUGIN-PLAN 1.2, 1.3, 1.8, 1.10).
// The scheduler (scanStarts, startClip, doChase, addVoice) is EngineSchedule.cpp, the budget planner EnginePlan.cpp,
// the display and stats EngineDisplay.cpp. Ported line by line from the JS; the plugin's changes are marked with
// their number in plan 8.2:
//   #3  a tempo or loop change re-anchors to the host (cur + d), with scan rules so no start is doubled or missed;
//   #4  negative beats are never wrapped into the clip (setTransport's wrap () and the jump anchor);
//   #6  ramp20ms parameters ramp on the control grid; outGain and the bypass fade ramp per sample;
//   #8  a 64-voice pool;
//   #21 the control grid counts from the last transport start;
//   #22 the chase is bounded (EngineSchedule.cpp);
//   #23 a voice stolen by a sample swap fades on its own sample (VoiceSource).
// Everything here except prepare () runs on the audio thread: no allocation, no lock, no wait.

#include "engine/Engine.h"

#include "core/JsCompat.h"
#include "core/JsMath.h"
#include "engine/EngineMath.h"
#include "rt/VersionExchange.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace sl::engine
{
    void Engine::VoicePoolDeleter::operator() (Voice* pool) const noexcept
    {
        delete[] pool;
    }

    Engine::Engine() noexcept
    {
        smoother.prepare (sr, rampMs);
        smoother.setTarget (core::clampForEngine (core::defaultInst()));
        ctx.sr = sr;
        ctx.inst = &smoother.current();
        ctx.clock = &clock;
        clock.db = 2 / sr;
        budget = budgetFor (sr);
        st.budget = budget;
        limRel = 1 - core::jsExp (-1 / (kLimiterReleaseSeconds * sr));
        source = VoiceSource::of (nullptr, 0, sr);
    }

    Engine::~Engine() = default;

    //==============================================================================================
    // Message thread, audio idle.

    void Engine::prepare (double rate)
    {
        // JS constructor: this.sr = clamp (fin (opts.sampleRate, 48000), 8000, 384000)
        sr = std::isfinite (rate) ? clampJs (rate, kMinSampleRate, kMaxSampleRate) : kDefaultSampleRate;
        if (voicePool == nullptr)
            voicePool.reset (new Voice[kVoicePool]);   // the only allocation the engine makes
        numVoices = 0;
        slotUsed.fill (false);
        serial = 0;
        anyChasing = false;
        k = 0;

        smoother.prepare (sr, rampMs);
        smoother.snap();
        ctx = VoiceContext();
        ctx.sr = sr;
        ctx.inst = &smoother.current();
        ctx.clock = &clock;

        clock = Clock();
        clock.db = 2 / sr;   // this.db = 2 / this.sr (120 bpm)
        snap = nullptr;
        snapEpoch = 0;
        noteCount = 0;
        notesSearchable = false;
        src = nullptr;
        srcEpoch = 0;
        source = VoiceSource::of (nullptr, 0, sr);

        pendingChase = false;
        playStartK = 0;
        gridOrigin = 0;
        liveCount = 0;
        sustain = false;
        clearScanState();

        budget = budgetFor (sr);
        limG = 1;
        limRel = 1 - core::jsExp (-1 / (kLimiterReleaseSeconds * sr));
        cellOut0 = cellOut1 = smoother.value (InstSmoother::outGain);
        cellByp0 = cellByp1 = smoother.value (InstSmoother::bypass);
        st = Stats();
        st.budget = budget;
        peakL = peakR = 0.0f;
    }

    void Engine::forgetVersions() noexcept
    {
        for (int i = 0; i < numVoices; ++i)
            voiceAt (order[static_cast<std::size_t> (i)]).done = true;
        numVoices = 0;
        slotUsed.fill (false);
        anyChasing = false;
        snap = nullptr;
        snapEpoch = 0;
        noteCount = 0;
        notesSearchable = false;
        clock.Lb = 16;
        src = nullptr;
        srcEpoch = 0;
        source = VoiceSource::of (nullptr, 0, sr);
        clearScanState();
    }

    //==============================================================================================
    // Small helpers.

    bool Engine::noteOk (const core::NoteSnap& X) const noexcept
    {
        // JS setSnapshot: ok = n => n && n.id != null && isFinite (n.p) && isFinite (n.s) && isFinite (n.l) && n.l > 0
        return std::isfinite (X.p) && std::isfinite (X.s) && std::isfinite (X.l) && X.l > 0;
    }

    void Engine::compactVoices() noexcept
    {
        // JS: let w = 0; for (const v of this.voices) if (!v.done) this.voices[w++] = v; this.voices.length = w;
        int w = 0;
        bool chasing = false;
        for (int i = 0; i < numVoices; ++i)
        {
            const std::uint8_t slot = order[static_cast<std::size_t> (i)];
            const Voice& v = voiceAt (slot);
            if (v.done)
            {
                slotUsed[static_cast<std::size_t> (slot)] = false;
                continue;
            }
            chasing = chasing || v.chasing;
            order[static_cast<std::size_t> (w++)] = slot;
        }
        numVoices = w;
        anyChasing = chasing;
    }

    void Engine::dropReleasedChases() noexcept
    {
        // A voice still chasing has never sounded: released or stolen, it is done at once (the JS release of a
        // voice at env 0 ends it at its next sample).
        if (! anyChasing)
            return;
        for (int i = 0; i < numVoices; ++i)
        {
            Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (v.chasing && v.released)
            {
                v.chasing = false;
                v.done = true;
            }
        }
    }

    void Engine::releaseClipVoices() noexcept
    {
        // for (const v of this.voices) if (v.kind === 'clip') v.release ();
        for (int i = 0; i < numVoices; ++i)
        {
            Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (v.kind == VoiceKind::clip)
                v.release();
        }
        dropReleasedChases();
    }

    void Engine::clearScanState() noexcept
    {
        numStarts = 0;
        deferActive = false;
        numRules = 0;
        catchUpPending = false;
    }

    void Engine::liveStackRemove (int p) noexcept
    {
        // this.liveStack = this.liveStack.filter (q => q !== p)
        int w = 0;
        for (int i = 0; i < liveCount; ++i)
            if (liveStack[static_cast<std::size_t> (i)] != p)
                liveStack[static_cast<std::size_t> (w++)] = liveStack[static_cast<std::size_t> (i)];
        liveCount = w;
    }

    bool Engine::liveStackHas (int p) const noexcept
    {
        for (int i = 0; i < liveCount; ++i)
            if (liveStack[static_cast<std::size_t> (i)] == p)
                return true;
        return false;
    }

    //==============================================================================================
    // Audio thread: inputs.

    void Engine::reset() noexcept
    {
        for (int i = 0; i < numVoices; ++i)
            voiceAt (order[static_cast<std::size_t> (i)]).done = true;
        numVoices = 0;
        slotUsed.fill (false);
        anyChasing = false;
        liveCount = 0;
        sustain = false;
        ctx.wheel[0] = ctx.wheel[1] = 0;
        ctx.wheelTouched[0] = ctx.wheelTouched[1] = false;
        ctx.bend = 0;
        limG = 1;
        smoother.snap();
        cellOut0 = cellOut1 = smoother.value (InstSmoother::outGain);
        cellByp0 = cellByp1 = smoother.value (InstSmoother::bypass);
        peakL = peakR = 0.0f;
        numStarts = 0;
        deferActive = false;
        catchUpPending = false;
        // The clip notes that were sounding come back through a chase at the next sample (their voices are gone).
        pendingChase = clock.playing;
    }

    void Engine::setSample (const SampleData* sample, std::uint64_t epoch) noexcept
    {
        // for (const v of this.voices) v.steal (); a stolen voice keeps its own source to the end (8.2 #23)
        for (int i = 0; i < numVoices; ++i)
            voiceAt (order[static_cast<std::size_t> (i)]).steal();
        dropReleasedChases();
        if (sample == nullptr || sample->L == nullptr || sample->n < 8)
        {
            src = nullptr;
            srcEpoch = 0;
            source = VoiceSource::of (nullptr, 0, sr);
            return;
        }
        src = sample;
        srcEpoch = epoch;
        source = VoiceSource::of (sample, epoch, sr);
    }

    void Engine::setInst (const core::Inst& next) noexcept
    {
        // this.inst = normInst (inst); outGainLin = 10^(outGain / 20) is the output stage's (from the smoother)
        smoother.setTarget (core::clampForEngine (next));
    }

    void Engine::setSnapshot (const core::Snapshot* snapshot, std::uint64_t epoch) noexcept
    {
        snap = snapshot;
        snapEpoch = snapshot != nullptr ? epoch : 0;
        noteCount = 0;
        notesSearchable = snapshot != nullptr;
        double lastS = -std::numeric_limits<double>::infinity();
        if (snapshot != nullptr)
            for (const core::NoteSnap& X : snapshot->notes)
            {
                if (noteOk (X))
                    ++noteCount;
                else
                    notesSearchable = false;
                if (! (X.s >= lastS))
                    notesSearchable = false;
                lastS = X.s;
            }
        // this.Lb = max (1, fin (bars, 4)) x max (1, fin (beatsPerBar, 4))
        const double bars = snapshot != nullptr ? static_cast<double> (snapshot->bars) : 4;
        const double bpb = snapshot != nullptr ? static_cast<double> (snapshot->beatsPerBar) : 4;
        clock.Lb = jsMax (1, bars) * jsMax (1, bpb);
        // the start list's deferral names notes by index: a new snapshot ends it
        deferActive = false;

        for (int i = 0; i < numVoices; ++i)
        {
            Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (v.kind != VoiceKind::clip || v.done)
                continue;
            // this.byId.get (v.note.id): the last note with that id that the filter keeps
            const core::NoteSnap* nx = nullptr;
            if (snapshot != nullptr && v.snap != nullptr && v.note != nullptr)
            {
                const std::uint32_t h = v.note->idHash;
                const std::string* id = nullptr;
                const auto idIndex = static_cast<std::size_t> (v.note->idIndex);
                if (v.note->idIndex >= 0 && idIndex < v.snap->ids.size())
                    id = &v.snap->ids[idIndex];
                const auto& byHash = snapshot->byHash;
                auto it = std::lower_bound (byHash.begin(), byHash.end(),
                                            std::pair<std::uint32_t, std::int32_t> (h, std::numeric_limits<std::int32_t>::min()));
                for (; it != byHash.end() && it->first == h; ++it)
                {
                    if (it->second < 0 || static_cast<std::size_t> (it->second) >= snapshot->notes.size())
                        continue;
                    const core::NoteSnap& cand = snapshot->notes[static_cast<std::size_t> (it->second)];
                    if (! noteOk (cand))
                        continue;
                    const auto ci = static_cast<std::size_t> (cand.idIndex);
                    const bool sameId = id == nullptr || (cand.idIndex >= 0 && ci < snapshot->ids.size() && snapshot->ids[ci] == *id);
                    if (sameId && (nx == nullptr || &cand > nx))
                        nx = &cand;
                }
            }
            if (nx == nullptr)
            {
                v.release();   // deleted notes release (and keep reading their old note, and its epoch)
                continue;
            }
            v.follow (snapshot, snapEpoch, nx, nx->s + static_cast<double> (v.m) * clock.Lb);   // edited notes follow at once
        }
        dropReleasedChases();
    }

    void Engine::setTransport (const Transport& t) noexcept
    {
        Clock& C = clock;
        const bool playing = t.playing;
        const double bpm = clampJs (fin (t.bpm, C.bpm), 10, 999);
        const bool loop = t.loop;
        const double beat = fin (t.beat, std::numeric_limits<double>::quiet_NaN());
        const bool hasBeat = beat == beat;
        const double Lb = C.Lb;
        // 8.2 #4: a negative beat is never wrapped into the clip (JS: loop ? floor-wrap : max (0, b))
        auto wrap = [loop, Lb] (double x) { return x < 0 || ! loop ? x : x - Lb * std::floor (x / Lb); };
        const double rate = sr;
        auto setTempo = [&C, bpm, loop, rate] {
            C.bpm = bpm;
            C.db = bpm / 60 / rate;
            C.loop = loop;
        };

        if (playing && ! C.playing)
        {
            setTempo();
            C.anchorBeat = hasBeat ? wrap (beat) : 0;
            C.anchorK = k;
            C.playing = true;
            pendingChase = true;
            playStartK = k;
            gridOrigin = k;   // 8.2 #21: the control grid counts from here
            clearScanState();
        }
        else if (! playing && C.playing)
        {
            const double was = C.loopBeat (C.uAt (k));
            C.playing = false;
            releaseClipVoices();   // stopping releases clip voices
            setTempo();
            C.freeBeat0 = hasBeat ? beat : was;
            C.freeK0 = k;
            pendingChase = false;
            clearScanState();
        }
        else if (playing)
        {
            const double cur = C.uAt (k), curL = C.loopBeat (cur);
            double d = 0;
            if (hasBeat)
            {
                d = beat - curL;
                if (loop)
                    d -= Lb * core::jsRound (d / Lb);
                if (std::abs (d) > 0.1)
                {   // a jump: release clip voices and chase at the new place
                    releaseClipVoices();
                    setTempo();
                    C.anchorBeat = wrap (beat);
                    C.anchorK = k;
                    pendingChase = true;
                    playStartK = k;
                    clearScanState();
                    return;
                }
            }
            if (bpm == C.bpm && loop == C.loop)
                return;   // re-sent unchanged: the clock stays exactly as it is
            // 8.2 #3: a tempo or loop change re-anchors to the host, cur + d. The old clock emitted every start with
            // kOf (U) < k: a rule keeps those from starting again. The first scan after this starts at once the
            // starts the new clock has already passed and the old one had not emitted: the notes the clock jumped
            // over (d > 0), and, at any d, a note in the last fraction of a sample before k when the tempo falls
            // (the old clock put it at k, the slower new one before k: the JS loses it).
            if (numRules == kMaxScanRules)
            {
                // Full: drop the rule with the lowest horizon. Each rule keeps back every start below its horizon
                // (less about one of its samples), so the rule with the highest horizon covers it exactly.
                int lowest = 0;
                for (int r = 1; r < numRules; ++r)
                {
                    const ScanRule& a = rules[static_cast<std::size_t> (r)];
                    const ScanRule& b = rules[static_cast<std::size_t> (lowest)];
                    if (a.horizonU - a.db < b.horizonU - b.db)
                        lowest = r;
                }
                for (int r = lowest + 1; r < numRules; ++r)
                    rules[static_cast<std::size_t> (r - 1)] = rules[static_cast<std::size_t> (r)];
                --numRules;
            }
            ScanRule& rule = rules[static_cast<std::size_t> (numRules++)];
            rule.anchorBeat = C.anchorBeat;
            rule.anchorK = C.anchorK;
            rule.db = C.db;
            rule.kR = k;
            rule.horizonU = cur;
            // the rules keep every start the old clock emitted out of the catch-up, so it may reach back each time
            const double lowU = cur - 2 * C.db;
            catchUpLowU = catchUpPending ? jsMin (catchUpLowU, lowU) : lowU;
            catchUpPending = true;
            deferActive = false;   // a deferral's keys are samples of the old clock
            C.anchorBeat = cur + d;
            C.anchorK = k;
            setTempo();
            // a clock moved back leaves the notes that started in the last |d| beats ahead of it: they keep sounding
            // (their voices would otherwise release at once, by the rule meant for a note edited to start later)
            const double uNew = cur + d;
            for (int i = 0; i < numVoices; ++i)
            {
                Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                if (v.kind == VoiceKind::clip && ! v.done && v.U > uNew)
                    v.reanchorSlack = jsMax (v.reanchorSlack, v.U - uNew);
            }
        }
        else
        {
            if (! hasBeat && bpm == C.bpm && loop == C.loop)
                return;
            const double was = C.songBeat (k);
            setTempo();
            C.freeBeat0 = hasBeat ? beat : was;
            C.freeK0 = k;
        }
    }

    void Engine::noteOn (double pIn, double velIn, const NoteOnOpts& opts) noexcept
    {
        const int p = static_cast<int> (clampJs (core::jsRound (fin (pIn, 60)), 0, 127));
        double vel = fin (velIn, 0.8);
        if (vel > 1)
            vel /= 127;
        vel = clampJs (vel, 0, 1);
        if (src == nullptr || voicePool == nullptr)
            return;
        if (vel <= 0)
        {
            noteOff (p);
            return;
        }
        const core::Inst& I = smoother.current();
        if (I.liveLegato)
        {   // a new key while another is held carries its playhead
            Voice* held = nullptr;
            for (int i = 0; i < numVoices; ++i)
            {
                Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                if (v.kind == VoiceKind::live && ! v.released && ! v.done && (held == nullptr || v.serial > held->serial))
                    held = &v;
            }
            if (held != nullptr)
            {
                liveStackRemove (p);
                liveStack[static_cast<std::size_t> (liveCount++)] = static_cast<std::uint8_t> (p);
                held->setLivePitch (p, vel);
                return;
            }
        }
        for (int i = 0; i < numVoices; ++i)
        {
            Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (v.kind == VoiceKind::live && v.key == p && ! v.released)
                v.release();
        }
        Voice* v = allocVoice();
        if (v == nullptr)
            return;
        v->initLive (ctx, source, p, vel, k, ++serial);
        if (! std::isnan (opts.startPos))
            v->startPos = clampJs (fin (opts.startPos, 0), 0, 1);
        addVoice (static_cast<int> (v - voicePool.get()), k);
        liveStackRemove (p);
        liveStack[static_cast<std::size_t> (liveCount++)] = static_cast<std::uint8_t> (p);
    }

    void Engine::noteOff (double pIn) noexcept
    {
        const int p = static_cast<int> (clampJs (core::jsRound (fin (pIn, 60)), 0, 127));
        liveStackRemove (p);
        const core::Inst& I = smoother.current();
        for (int i = 0; i < numVoices; ++i)
        {
            Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
            if (v.kind != VoiceKind::live || v.released || v.key != p)
                continue;
            if (I.liveLegato && liveCount > 0)
                v.setLivePitch (liveStack[static_cast<std::size_t> (liveCount - 1)]);   // fall back to the last key still held
            else if (sustain)
                v.sustained = true;
            else
                v.release();
        }
    }

    void Engine::allNotesOff() noexcept
    {
        for (int i = 0; i < numVoices; ++i)
            voiceAt (order[static_cast<std::size_t> (i)]).steal();
        dropReleasedChases();
        liveCount = 0;
    }

    void Engine::setController (Ctrl controller, double value) noexcept
    {
        if (controller == Ctrl::mod1 || controller == Ctrl::mod2)
        {
            const int i = controller == Ctrl::mod1 ? 0 : 1;
            if (value != value)
            {
                ctx.wheelTouched[i] = false;   // null = let go (the lanes rule again)
                return;
            }
            ctx.wheel[i] = clampJs (fin (value, 0), 0, 1);
            ctx.wheelTouched[i] = true;
        }
        else if (controller == Ctrl::bend)
        {
            ctx.bend = clampJs (fin (value, 0), -1, 1);
        }
        else
        {
            sustain = value >= 0.5;   // !!value && value >= 0.5 (NaN is false)
            if (! sustain)
                for (int i = 0; i < numVoices; ++i)
                {
                    Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                    if (v.kind == VoiceKind::live && v.sustained && ! v.released)
                    {
                        v.sustained = false;
                        if (! liveStackHas (v.key))
                            v.release();
                    }
                }
        }
    }

    void Engine::releaseController (Ctrl controller) noexcept
    {
        // setController (name, null): the wheels let go; fin (null, 0) is 0 for bend; !!null is false for sustain
        if (controller == Ctrl::bend || controller == Ctrl::sustain)
            setController (controller, 0);
        else
            setController (controller, std::numeric_limits<double>::quiet_NaN());
    }

    //==============================================================================================
    // Audio thread: process.

    void Engine::process (float* L, float* R, int n) noexcept
    {
        if (n <= 0)
            return;
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        if (src == nullptr || voicePool == nullptr)
        {
            k += n;
            catchUpPending = false;
            return;
        }
        int i = 0;
        while (i < n)
        {
            const std::int64_t k0 = k;
            std::int64_t cell = (k0 - gridOrigin) % kCtrl;
            if (cell < 0)
                cell += kCtrl;
            const int gridPos = static_cast<int> (cell);
            if (gridPos == 0)
            {
                // a boundary of the control grid (8.2 #21): the ramps move one step; outGain and the bypass fade are
                // interpolated per sample between the values before and after it
                const double out0 = smoother.value (InstSmoother::outGain), byp0 = smoother.value (InstSmoother::bypass);
                smoother.step();
                cellOut0 = out0;
                cellOut1 = smoother.value (InstSmoother::outGain);
                cellByp0 = byp0;
                cellByp1 = smoother.value (InstSmoother::bypass);
            }
            else
            {
                // a value set since the boundary without a ramp (ramp time 0) applies at once, as the JS
                const double out = smoother.value (InstSmoother::outGain), byp = smoother.value (InstSmoother::bypass);
                if (out != cellOut1)
                    cellOut0 = cellOut1 = out;
                if (byp != cellByp1)
                    cellByp0 = cellByp1 = byp;
            }

            bool chasedNow = false;
            if (pendingChase)
            {
                pendingChase = false;
                doChase (k0);
                chasedNow = true;
            }
            if (anyChasing && gridPos == 0 && ! chasedNow)
                continueChases (k0);

            const int seg = std::min (n - i, kCtrl - gridPos);
            scanStarts (k0, k0 + seg);
            int pos = 0, ei = 0;
            bool needPlan = gridPos == 0;
            while (pos < seg)
            {
                while (ei < numStarts && starts[static_cast<std::size_t> (ei)].kS == k0 + pos)
                {
                    startClip (starts[static_cast<std::size_t> (ei)]);
                    ++ei;
                    needPlan = false;
                }
                if (needPlan)
                {
                    plan();
                    needPlan = false;
                }
                const int nextE = ei < numStarts ? static_cast<int> (starts[static_cast<std::size_t> (ei)].kS - k0) : seg;
                const int len = nextE - pos;
                if (len > 0)
                {
                    for (int vi = 0; vi < numVoices; ++vi)
                    {
                        Voice& v = voiceAt (order[static_cast<std::size_t> (vi)]);
                        if (! v.done)
                            v.render (L, R, i + pos, len, k0 + pos);
                    }
                    measure();
                }
                pos = nextE;
            }
            compactVoices();
            outputStage (L, R, i, seg, gridPos);
            k += seg;
            i += seg;
        }
    }

    void Engine::outputStage (float* L, float* R, int from, int count, int gridPos) noexcept
    {
        // output: outGain, the bypass fade, the guard (never NaN / inf; counted), the soft limiter (instant attack,
        // 80 ms release, ceiling 0.9), peak <= 0.98. Per segment: the limiter runs sample by sample, so this is the
        // JS loop over the whole call.
        const double g0 = std::pow (10.0, cellOut0 / 20), g1 = cellOut1 == cellOut0 ? g0 : std::pow (10.0, cellOut1 / 20);
        const double b0 = cellByp0, b1 = cellByp1;
        const double T = kLimiterCeiling, rel = limRel;
        double lg = limG, prePeak = st.preLimiterPeak;
        std::int64_t bad = 0;
        float pkL = peakL, pkR = peakR;
        for (int j = 0; j < count; ++j)
        {
            const double c = static_cast<double> (gridPos + j + 1) / kCtrl;
            const double g = g1 == g0 ? g0 : g0 + (g1 - g0) * c;
            const double bp = b1 == b0 ? b0 : b0 + (b1 - b0) * c;
            const int o = from + j;
            double l = L[o] * g, r = R[o] * g;
            if (bp != 1)
            {
                l *= bp;
                r *= bp;
            }
            if (! (l > -kOutputGuard && l < kOutputGuard))
            {
                l = 0;
                ++bad;
            }
            if (! (r > -kOutputGuard && r < kOutputGuard))
            {
                r = 0;
                ++bad;
            }
            const double al = l < 0 ? -l : l, ar = r < 0 ? -r : r, pk = al > ar ? al : ar;
            if (pk > prePeak)
                prePeak = pk;
            const double tg = pk > T ? T / pk : 1;
            if (tg < lg)
                lg = tg;
            else
                lg += (tg - lg) * rel;
            l *= lg;
            r *= lg;
            const float ol = static_cast<float> (l > kOutputClamp ? kOutputClamp : l < -kOutputClamp ? -kOutputClamp : l);
            const float orr = static_cast<float> (r > kOutputClamp ? kOutputClamp : r < -kOutputClamp ? -kOutputClamp : r);
            L[o] = ol;
            R[o] = orr;
            const float aol = ol < 0 ? -ol : ol, aor = orr < 0 ? -orr : orr;
            if (aol > pkL)
                pkL = aol;
            if (aor > pkR)
                pkR = aor;
        }
        limG = lg;
        st.preLimiterPeak = prePeak;
        st.nonFinite += bad;
        peakL = pkL;
        peakR = pkR;
    }

    //==============================================================================================

    std::uint64_t Engine::minEpochInUse (VersionKind kind) const noexcept
    {
        std::uint64_t e = rt::kNoEpoch;
        if (kind == VersionKind::snapshot)
        {
            if (snap != nullptr && snapEpoch != 0)
                e = snapEpoch;
            for (int i = 0; i < numVoices; ++i)
            {
                const Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                if (! v.done && v.kind == VoiceKind::clip && v.snap != nullptr && v.snapEpoch != 0 && v.snapEpoch < e)
                    e = v.snapEpoch;
            }
        }
        else
        {
            if (src != nullptr && srcEpoch != 0)
                e = srcEpoch;
            for (int i = 0; i < numVoices; ++i)
            {
                const Voice& v = voiceAt (order[static_cast<std::size_t> (i)]);
                if (! v.done && v.src.data != nullptr && v.src.epoch != 0 && v.src.epoch < e)
                    e = v.src.epoch;
            }
        }
        return e;
    }

    void Engine::setRampMs (double ms) noexcept
    {
        rampMs = std::isfinite (ms) && ms > 0.0 ? ms : 0.0;
        smoother.setRampMs (rampMs);
    }

    void Engine::setBypass (bool on) noexcept
    {
        smoother.setBypass (on);
    }

    void Engine::snapRamps() noexcept
    {
        smoother.snap();
        cellOut0 = cellOut1 = smoother.value (InstSmoother::outGain);
        cellByp0 = cellByp1 = smoother.value (InstSmoother::bypass);
    }

    void Engine::rechase() noexcept
    {
        releaseClipVoices();
        pendingChase = clock.playing;
    }
}
