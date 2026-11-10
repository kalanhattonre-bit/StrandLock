# NOTES-M2

Builder notes for M2 (PLUGIN-PLAN section 4 and 7.4). Append only: one bullet per item, with the reason. Each part
writes its own section: Contract, E1, E2, P1, P2, Integration, Fix.

## Contract

The contract step (plan 7.4) writes the headers both chains code against, the real-time pieces in full with their
tests, a stub engine that renders silence, the command queue and the test signals. Branch `m2-contract`.

### Progress

- Done: `rt/VersionExchange.h`, `rt/LatestSlot.h`, `rt/StableSort.h`; `engine/EngineConfig.h`, `SampleData.h`,
  `DisplayFrame.h`, `Engine.h`, the stub `Engine.cpp`; `doc/CommandQueue.h`; `Tests/support/TestSignals.*`;
  tests `Tests/engine/test_rt.cpp`, `Tests/engine/test_engine_api.cpp`, `Tests/support/test_signals.cpp`.
- In progress: CI on `m2-contract`.
- Next: CI green, rebase on main if it moved, fast-forward main.

### The files and who uses what

| File | What it is | Used by |
|---|---|---|
| `Source/rt/VersionExchange.h` | the snapshot and sample hand-off (plan 1.3), complete | I (processor), P (SnapshotPublisher, SampleManager publish), E (nothing directly) |
| `Source/rt/LatestSlot.h` | triple buffer for the display frame (1.5), complete | I (audio writes), P (editor reads) |
| `Source/rt/StableSort.h` | allocation-free stable sort (1.6 rule 7), complete | E (scanStarts, plan) |
| `Source/engine/EngineConfig.h` | every constant and salt of 1.2's table and 1.8 | E, and the tests |
| `Source/engine/SampleData.h` | the decoded sample and its factory | P (SampleManager makes them), E (reads them) |
| `Source/engine/DisplayFrame.h` | the display frame, `DisplaySlot`, `displayEvery` | E (fills), P (draws), I (writes the slot) |
| `Source/engine/Engine.h` | the engine's public API | E implements, I calls, P's fake engine mirrors it |
| `Source/engine/Engine.cpp` | a stub: silence, the clock, the epochs | E replaces the whole file |
| `Source/doc/CommandQueue.h` | `UiCommand` and `CommandQueue` (`rt::SpscFifo<UiCommand, 256>`) | P (pushes), I (drains) |
| `Tests/support/TestSignals.*` | the prototype tests' signal makers, `makeSignal`, `fnvF32`, `hex8` | E (renders.json sounds), M7 (detect.json) |

### rt::VersionExchange<T>: what each call does

- `publish (std::unique_ptr<T>)` (any thread but the audio thread): stamps `epoch = ++publishCount` and writes it
  into `T::epoch` when T has that member (Snapshot and SampleData do). Adds the version to the pool (a vector behind
  a mutex the audio thread never touches) and puts it in the one-slot mailbox. A version still waiting there was
  never taken by the audio thread, so it is freed at once. Returns the epoch; nullptr is ignored (returns 0).
- `acquire ()` (audio, once per block): one exchange on the mailbox. True when `current ()` changed. Also true once
  after `setPrepared (true)`, so the first block after a prepare hands the version to the engine again.
- `current ()`, `currentEpoch ()` (audio): what to give `Engine::setSnapshot` / `setSample`.
- `setInUse (epoch)` (audio, after `Engine::process`): pass `engine.minEpochInUse (kind)`. The value is clamped to
  `currentEpoch ()`, so `rt::kNoEpoch` ("the engine reads nothing") can never free the current or a waiting version.
  A value below the one before is counted in `inUseRegressions ()`: a bookkeeping bug in the engine.
- `collect ()` (message timer, 10 Hz): frees every pooled version with `epoch < inUse`. Never the current one, never
  the waiting one.
- `setPrepared (bool)`: true at the end of `prepareToPlay` (after `Engine::prepare`), false at the start of
  `releaseResources` (after `Engine::forgetVersions`). While not prepared the audio thread is idle, so `publish`
  swaps directly: the new version becomes current and everything older is freed at once (the pool holds 1).
  `setPrepared (false)` settles on the newest version the same way. A new exchange starts not prepared, so the
  processor's constructor and a state load before `prepareToPlay` swap directly.
- Debug poison: with poisoning on, a freed version is destroyed, filled with 0xDD and kept in a quarantine of the last
  64 frees (marked unreadable under AddressSanitizer) before it really goes back to the heap. `rt::looksPoisoned (p)`
  checks the first 8 bytes. On by default in Debug, under AddressSanitizer and with `STRANDLOCK_RT_POISON`; the
  constructor's flag turns it on or off. Test helper: `holdsFreed (p)`.

### The processor's block (for I)

```
if (snapshots.acquire()) engine.setSnapshot (snapshots.current(), snapshots.currentEpoch());
if (samples.acquire())   engine.setSample (samples.current(), samples.currentEpoch());
engine.setInst (readInst (...)); engine.setBypass (readBypass (...));
commands (CommandQueue), MIDI (MidiRouter) and transport (HostSync), each at its sample, process () between;
snapshots.setInUse (engine.minEpochInUse (VersionKind::snapshot));
samples.setInUse (engine.minEpochInUse (VersionKind::sample));
every displayEvery (sr) samples: engine.fillDisplay (displaySlot.beginWrite()); displaySlot.endWrite();
```

prepareToPlay: `engine.prepare (sr)`, then `snapshots.setPrepared (true)`, `samples.setPrepared (true)`.
releaseResources: `engine.forgetVersions ()`, then `snapshots.setPrepared (false)`, `samples.setPrepared (false)`.

### Engine: what each call must do (for E; I and P rely on it)

Message thread, audio idle:
- `Engine ()`: the JS constructor's defaults (48 kHz, its budget, defaultInst, no sample, no snapshot). Allocates
  nothing.
- `prepare (sampleRate)`: the JS constructor. Rate clamped to 8000..384000 (48000 if not finite), the budget, the
  limiter coefficient. Allocates the 64-voice pool the first time only. Resets voices, clocks, the control grid,
  limiter, ramps and stats; drops the snapshot and the sample; keeps the inst.
- `forgetVersions ()`: every voice stops at once; the snapshot and sample pointers go. Allocates nothing.

Audio thread (never allocate, lock, wait, log or touch files):
- `reset ()`: voices stop at once, live keys, wheels, bend and sustain clear, limiter and ramps at rest. Keeps
  sample, snapshot, inst, transport and clocks.
- `setSample (const SampleData*, epoch)`: JS setSample; every voice is stolen (5 ms). Stolen voices finish their fade
  on the old sample and keep its epoch (8.2 #23). nullptr = silence. Rate played: clamp (sr, 4000, 768000), or the
  engine's rate when sr is not finite.
- `setInst (const core::Inst&)`: `clampForEngine`, `outGainLin`; ramp20ms parameters ramp (2.3), the rest apply at the
  next tick.
- `setSnapshot (const core::Snapshot*, epoch)`: JS setSnapshot; surviving notes' voices move to the new note and epoch
  at once, deleted notes' voices release and keep their old note and epoch until done. Resolve follow markers with
  `core::resolve` every tick.
- `setTransport (const Transport&)`: JS setTransport with the plan's changes (1.10, 8.2 #3, #4, #21). `Transport`:
  `playing`, `beat` (NaN = none; host ppq minus offsetBeats), `bpm` (NaN = keep), `loop`.
- `noteOn (p, vel, NoteOnOpts {startPos})`, `noteOff (p)`, `allNotesOff ()`: the JS live-key rules. vel > 1 reads as
  0..127.
- `setController (Ctrl, value)` and `releaseController (Ctrl)`: `Ctrl` is mod1, mod2, bend, sustain. NaN on a wheel
  lets go. Release: wheels let go, bend 0, sustain up.
- `process (L, R, n)`: overwrites both (non-null) with n samples. No sample: silence, the clock still moves.
- `fillDisplay (DisplayFrame&)`: getDisplay + stats into the frame; resets the peak meters.
- `stats ()`, `resetStats ()`: the JS seven fields in order, plus deferredStarts, chaseTicks, nonFinite,
  filterResets, preLimiterPeak. resetStats clears maxGrains, cuts, steals and every plugin counter.
- `minEpochInUse (VersionKind)`: lowest epoch of the current version and every voice not done; `rt::kNoEpoch` when
  none.
- `setRampMs (ms)` (0 for parity), `setBypass (bool)` (20 ms fade per sample, voices keep running), `snapRamps ()`.
- `sampleRate ()`, `sampleTime ()` (JS this.k).

### Decisions

- **VersionExchange keeps the epoch beside each version** (a small pool entry) and also stamps `T::epoch` when T
  has one. So it works for any T, and a Snapshot's own `epoch` field matches what the engine is told.
- **`setInUse` clamps to the current epoch.** Plan 1.3 says the audio thread stores the engine's minimum. If the engine
  reads nothing (no snapshot yet, or forgotten), that minimum is undefined; without the clamp a "nothing" value would
  let `collect` free a version the audio thread is about to take. With it, `kNoEpoch` is safe.
- **`acquire` returns true once after `setPrepared (true)`.** `Engine::prepare` drops its pointers (a re-prepare at a
  new rate resets the voices that held them), so the first block after a prepare must hand the current version over
  again even when nothing new was published.
- **`setPrepared (false)` settles on the newest version and frees the rest**, the same as a direct publish. Before it,
  the processor calls `Engine::forgetVersions ()` (a contract addition) so the engine points at nothing the message
  thread may free. Plan 1.3 only says the message thread "swaps directly" while not prepared.
- **The poison quarantine** keeps the last 64 freed blocks, filled with 0xDD, so a read of a freed version sees the
  poison instead of reused memory. It covers the version object itself. Memory the object owned (vectors, tables
  shared with other snapshots) is freed normally; the sanitizer job catches reads of that. Default on in Debug
  builds (so the Debug CRT and the Debug snapshots run with it), off in Release unless asked.
- **SampleData points into shared storage.** `L` and `R` are raw pointers; `storage` (a `shared_ptr<const void>`)
  keeps them alive. A starter sound (`sharedStarterSound`) is then wrapped without a 2 MB copy, and the waveform strip
  can share the same samples. `R == L` when mono, as the JS source. `makeSampleData` applies the JS setSample rules
  (fewer than 8 frames = no sound, a right channel of another length or with identical samples = mono). The engine,
  not the factory, clamps the rate, because the JS falls back to the engine's own rate when the file's is not finite.
- **SampleData's first field is `epoch`**, and a Snapshot's is `version` (1), so a live version never starts with
  0xDD bytes and `looksPoisoned` tells them apart.
- **Room for int16:** `SampleFormat` has only `f32` now. M7 adds 16-bit storage as a second read path (readAbs is a
  template over the source), so nothing in this struct has to move.
- **Engine API additions beyond 1.2's list**, each needed by a plan section: `forgetVersions` (1.3, above), `reset`
  (AudioProcessor::reset and 2.3's "smoothers snap on reset"), `setRampMs` (2.3 and tools/README-fixtures.md: parity
  tests set 0), `setBypass` (2.1 #49, 2.3: the bypass fade is in the engine's output loop, as ProbeSynth does it),
  `snapRamps` (2.3: snap on state load), `sampleRate`, `sampleTime`.
- **`noteOn` and `noteOff` take a double pitch**, as the JS does (it rounds and clamps), so fixture events pass
  through unchanged. The JS return value (the voice) is not used anywhere, so they return nothing.
- **`NoteOnOpts`** carries `startPos` (NaN = none); a struct leaves room for more options without a new signature.
- **Stats fields keep the JS names** (`grains` is the JS `st.load`, `maxGrains` is `st.maxLoad`), so the fixtures'
  stats rows map one to one. They are `int32` (the load is a sum of integer head peaks); the counters are `int64`.
- **`DisplayFrame` follows 1.5 field for field.** `engine` holds `Kc` (tape, voice, cloud, lock), `kind` holds
  `VoiceKind` (clip, live), `motion` a `core::Motion` as played (Tape plays Freeze as Forward). `peakL`/`peakR` are the
  largest output samples since the frame before; `fillDisplay` resets them. Frame size printed by the API test.
- **`displayEvery (sr)`** is the worklet's `max (128, round (sr / 30))` with `jsRound`.
- **EngineConfig adds the prototype's inline times and limits** (attack 8 ms, release 60 ms, steal 5 ms, chase fade
  10 ms, 20 ms slews, the 5 ms Tape crossfade, the limiter's 0.9 / 80 ms, the 0.98 clamp, the 1e6 guard, the 1e-3
  quiet level, the 0.75 soft budget, `1 << 17` chase cap), the rate clamps, `kMaxStarts` 256, `kWaitList` 512, the
  coarse-chase constants (4096, 16), `kRampMs` 20, `kHannN`, `kFadeN`, `kMotionDir` (the JS DIR in `core::Motion`
  order) and `budgetFor (sr)`. E may use them or keep the JS literals; the values are the same.
- **`Voice` and `Head` are forward-declared as `class`.** Define them as `class` too (MSVC warns when a `struct`
  definition meets a `class` declaration). The pool is `std::unique_ptr<Voice[], VoicePoolDeleter>`; the stub's
  deleter does nothing because the stub never makes voices. E's Engine.cpp defines it as `delete[] pool` once Voice is
  complete. E may change every private member.
- **CommandQueue:** `UiCommand { kind, note, wheel, velocity, startPos }` with one factory per kind. The velocity is a
  double, so an audition at 0.7 plays 0.7 exactly (as the prototype), not the float 0.699999988. `midiLearnArm` with
  wheel -1 disarms, so no separate cancel kind. 256 slots: the queue refuses (push returns false) beyond that.
- **TestSignals** follows each JS function line by line, with `x[i] += v` as read float, add in double, store float.
  `Math.exp` is `core::jsExp` (V8's own, bit for bit); `Math.sin` and `Math.pow` are the C library's (one ulp at most,
  inside the file's 2-ulp tolerance). Also `makeSignal (fn, args)` / `makeSignal ({signal, args})` for the other
  fixture files, and the fixtures' `fnvF32` and `hex8`.
- **Tests added beyond 7.4's list.** `test_engine_api.cpp` checks EngineConfig against `engine/defs.json` (the
  constants, budgets, HEAD_TUNE, salts, lane defaults; not the HANN and FADE tables, which are E's `Tables`) and the
  API behaviour both chains rely on. Every check there is true of the real engine too (silence with no sample or no
  notes, the epochs reported, 0 allocations on every audio-thread call), so E's engine must keep it green.
  `test_signals.cpp` sits in `Tests/support` beside TestSignals. All are in group `engine` (the sanitizer job runs
  it); the exchange stress also runs in group `alloc`, so the Debug-CRT job counts every heap call on its reader.
- **Negative controls.** Each rt check has a deliberate wrong use beside it that the same check must catch: a reader
  that forgets a releasing voice sees the poison; a frame mixing two writes fails the whole-frame check; an insertion
  sort that also passes equal elements differs from `std::stable_sort`; a tone with another seed fails the 2-ulp window.

## E1

The engine chain, part 1 of 2: everything one voice does (plan 7.4). Branch `m2-engine`; part 2 (E2) continues on
the same branch and worktree and merges it.

### Progress

- Done: `engine/EngineMath.h`, `Tables`, `SourceRead.h`, `Region`, `Biquad.h`, `Head.h`, `Clock.h`, `InstSmoother.h`,
  `Voice.h` / `Voice.cpp`, `VoiceTick.cpp`, `VoiceRender.cpp`, `VoiceChase.cpp`; `Stats::tapeStandIns` in `Engine.h`.
  Tests `Tests/engine/test_parity.cpp` (engine/defs.json), `test_seams.cpp`, `test_voice.cpp`,
  `test_voice_parity.cpp`, `test_voice_alloc.cpp`, with the rig `Tests/engine/VoiceRig.*` and `EngineFixture.*`.
  CI green on `m2-engine` (see "Results").
- In progress: nothing.
- Next (E2): the Engine itself (see "For E2" below).

### Tests

| Test (group engine) | What it checks |
|---|---|
| `fixtureEngineDefs` | engine/defs.json, every case counted against the file (6): constants, budgets, HEAD_TUNE, salts, lane defaults, HANN and FADE within 1 ulp, the 5 normInst cases through `clampForEngine` exactly; a table 2 ulps off fails |
| `readAbsFastPaths` | `hermite` equals readAbs on frames 1 .. n-3; `PlainStretch::read` equals `readReg` bit for bit at 20,000 random positions plus every edge, in 7 kinds of Region (post, pre and no seam, from frame 0, Hold, Hold to the file end, Bounce), mono and stereo; a fast path without the seam rule fails |
| `loopSeamTape`, `loopSeamRamp` | M2's loop seam row on one live Tape voice, as test-engine.js: 10 ms seam ratio < 1.5 and the ramp's largest step < 0.01, both the prototype's numbers; without the crossfade both measures fail |
| `voiceChainOneVoice` | M2's chain row: C3 -> G3 on one voice, min envelope across the joint > 0.999, B continues A's playhead within 0.002 of the prototype (and within 1e-6); B on its own voice restarts |
| `voiceChase` | test-engine.js's chase on one voice: playhead within 0.002, first sample non-zero, the 10 ms fade ratio, 3001 ticks; under 4096 ticks it is the JS chase loop bit for bit |
| `voiceCoarseChase` | plan 1.8: a 64-beat note with a moving Travel lane chased 60 beats in lands within 0.002 of the straight render (1.7e-4), with 6661 ticks instead of 45001; the stepped chase equals the one-shot bit for bit and is silent until it ends |
| `voiceParityWithPrototype` | 17 per-voice renders against the JS Voice: every Motion, Scatter, Refresh, heads, Hold, Stop, the pre seam, wheels, bend, offsets, macros, lanes, Starts, Path-as-1x |
| `voiceInitInPlace`, `voiceEnvelope`, `voiceFilterGuard`, `instSmoother` | a reused voice renders like a new one; 8 / 60 / 5 ms; the NaN guard counts and stays finite; plan 2.3's ramps |
| `voiceNoAllocations` (also group alloc) | the voice's audio-thread work allocates nothing |

### Files

| File | What it is |
|---|---|
| `engine/EngineMath.h` | the JS helpers: `clampJs` (NaN passes), `fin`, `jsMin` / `jsMax` (NaN and signed zero as JS), `toInt64`, `kTau` |
| `engine/Tables.h/.cpp` | HANN and FADE (Float32, built at static init), `hannTab`, `fadeIn` |
| `engine/SourceRead.h` | `readAbs` (a template over the source) and `hermite`, the fast paths' shared expression |
| `engine/Region.h/.cpp` | `RegionState` (the JS `reg`), `readReg`, `seamRho`, `mapPos`, `grabPos`, and `PlainStretch`: the inline fast path of runTape (and of runGrains and runLock in M3) |
| `engine/Biquad.h` | the RBJ peak and low-pass; `run` returns true when the NaN guard reset it |
| `engine/Head.h` | one playhead: the JS fields, with `std::array<double, 8>` grain slots |
| `engine/Clock.h` | the beat clock (`uAt`, `loopBeat` with 8.2 #4, `songBeat`, `kOf`, `effLen`, `Lb`, `bpm`) |
| `engine/InstSmoother.h` | plan 2.3's ramp20ms smoother |
| `engine/Voice.h` | `VoiceContext`, `VoiceSource`, `playedEngine` (the M3 switch), `snapshotNote`, `liveIdHash`, `class Voice` |
| `engine/Voice.cpp` | `initClip`, `initLive` (the JS constructor, in place), `setNote`, `switchTo`, `follow`, `setLivePitch`, `release`, `steal` |
| `engine/VoiceTick.cpp` | `Voice::tick` |
| `engine/VoiceRender.cpp` | `render`, `renderRun`, `runTape` |
| `engine/VoiceChase.cpp` | `chase` (with the coarse rule), `chaseBegin` / `chaseRun` / `chaseEnd`, `ffAdvance`, `ffChain` |

### For E2: what each Voice function expects from the Engine

- **VoiceContext.** The Engine owns one and gives every voice a pointer at init. It sets `sr` in `prepare`. `inst`
  points at what the voices read at their next tick (the smoother's `current ()`, so the address never changes).
  `clock` points at the Engine's `Clock`. `wheel`, `wheelTouched` and `bend` are `setController`'s. `filterResets` is
  counted by the voices: copy it into `Stats::filterResets` and zero it in `resetStats`.
- **Clock.** It replaces the JS fields `playing, loop, bpm, db, anchorBeat, anchorK, freeBeat0, freeK0, Lb`.
  `setTransport` and `setSnapshot` write them (db = bpm / 60 / sr). `loopBeat` already has 8.2 #4 (no wrap below 0);
  `setTransport`'s `wrap ()`, the jump anchor and `doChase` still need it.
- **VoiceSource.** `VoiceSource::of (sample, epoch, sr)` at `setSample` (the JS rate rule). A new voice gets the
  current one; a stolen voice keeps its own to the end of its fade (8.2 #23). `Voice::src.epoch` and
  `Voice::snapEpoch` are what `minEpochInUse` reads for every voice that is not done.
- **Starting a clip voice** (startClip / addVoice): `v.initClip (ctx, source, snap, snapEpoch, index, U, m, kS,
  ++serial)`, put it last in the voice list, `v.startAt (kS)` (the JS `v.tick (k0, false); v.pre = true`), then
  `plan ()`. A chain hand-over is `v.switchTo (&snap->notes[i], U)`. Find the previous note with
  `core::resolve (X, inst).chain.prevIndex` (none when `X.restart`).
- **Starting a live voice** (noteOn): `v.initLive (ctx, source, p, vel, k, ++serial)` (p already rounded and clamped,
  vel 0..1; it sets `key = p`), then `v.startPos = clamp (opts.startPos, 0, 1)` when given, **before**
  `v.startAt (k)` and `plan ()`. Live legato and noteOff's fallback key: `v.setLivePitch (p, vel)` and
  `v.setLivePitch (q)`.
- **setSnapshot.** A clip voice that is not done follows its note by id (`Snapshot::byHash`, then compare the id
  string): `v.follow (newSnap, epoch, &newSnap->notes[i], X.s + v.m * Lb)`. A deleted note's voice gets
  `v.release ()` and keeps its old `snap` and `snapEpoch`.
- **Chase.** One-shot (a chase moment inside the 4096-tick budget): `ticks = v.chase (k); v.ffChain (k);`, then add
  it as above with `startAt (k)`. Spread over blocks: `v.chaseBegin (k)` (returns 1 tick), add it to the voice list
  (it renders nothing while `v.chasing`), then per 32 samples `v.chaseRun (kNow, budget)` (returns the ticks it ran);
  when `v.chaseCaughtUp (kNow)`: `v.chaseEnd (kNow); v.ffChain (kNow); v.startAt (kNow);` and `plan ()`. `plan ()`
  must leave a chasing voice's heads without grants (it is "waiting", plan 1.8): with a grant its ff ticks would turn
  its heads on. Count the returned ticks in `Stats::chaseTicks`.
- **Render.** `v.render (outL, outR, o, n, k0)` per segment, voices in list order, exactly as the JS. `measure ()`
  reads and clears `h.segPeak`.
- **Planner fields.** `heads[i].want / allowed / est / grant / on / active / g0L .. dR / needPrime / clearGrains ()`,
  `v.vt`, `v.level`, `v.env`, `v.released`, `v.serial`, `v.done`, `v.st` are the JS fields, public.
- **Display.** `engineName` (the engine the note asks for, a `Kc`), `motion` (as played), `kind`, `note->idHash`,
  `note->p`, `env`, `released`. Per head that is on: `(reg.rs + mapPos (reg, raw + off)) / src.data->n`, `dir`,
  `0.5 (g1L + g1R) stopG`, `want && !allowed`. `Stats::tapeStandIns` = the voices not done with `asTape`.
- **InstSmoother.** `setInst`: `smoother.setTarget (core::clampForEngine (inst))`. `step ()` at every 32-sample
  boundary of the control grid (8.2 #21). `setRampMs` and `snapRamps` call into it. `Engine::prepare`:
  `smoother.prepare (sr, rampMs)` then `snap ()`. `setBypass` -> `smoother.setBypass`. outGain's per-sample ramp: the
  tick values are `current ().outGain` before and after `step ()`; the bypass fade is `value (InstSmoother::bypass)`.

### What is left for E2

`Engine.cpp` (replace the stub: prepare with the pool, the inputs, the clocks, `process`, the limiter, the health
counters), `EngineSchedule.cpp` (`scanStarts` with the binary search and the 256-start cap, `startClip`, `doChase` with
the bounded budget, `addVoice` with steals and the 64-object pool), `EnginePlan.cpp` (`plan`, `measure`),
`EngineDisplay.cpp` (`fillDisplay`, `stats`), setTransport with 8.2 #3, #4 and #21, `engine/renders.json` (the 36 tape
cases), and the engine-level M2 rows (tuning, note starts, jitter, cycle wrap, tempo, pre-roll, chase stop and start,
chase cost, tempo drift, stopped processing, determinism, voice steal, stability, live, allocations, CPU).

### Decisions and deviations

- **What a voice reads from its engine is a `VoiceContext`**, not the Engine itself. So a Voice can be built and
  tested before the Engine exists (the tests drive one directly), and Engine.h's private part stays E2's.
- **A voice keeps its own source and snapshot pointers with their epochs** (plan 1.3, 8.2 #23). Chain lookups
  (`ffChain`, the bridging test in `tick`, `chainNext`) use the voice's own snapshot. The JS uses the engine's current
  `byId`. The two are the same for every voice that is not released (setSnapshot releases a voice whose note was
  deleted), and only voices that are not released look a chain up.
- **`snapshotNote`** applies the JS setSnapshot filter (finite p, s, l and l > 0) to a chain or glide link, as `byId`
  would. Compiled snapshots never hold such notes; a raw one could.
- **A live voice's note lives inside the Voice** (`liveNote`). Its lane tables are empty, so every lane reads its
  default, as the JS `lanes: null`. `liveIdHash (p)` hashes "live:" + p without building a string.
- **The M3 switch is one function**, `playedEngine (Kc wanted)` in Voice.h. `tick` works out the engine the note asks
  for, plays what `playedEngine` returns (Tape until M3) and sets `Voice::asTape`. The switch sits where the JS
  computes `kc`, so everything that depends on it follows Tape: the speed carries the pitch, Freeze plays Forward,
  Path plays as Tape's 1x, the formant EQ is Tape's, and the budget estimate is 2. `engineName` keeps the asked-for
  engine (the display). renderRun dispatches only Tape; M3 adds runGrains and runLock there.
- **`Stats::tapeStandIns`** is an addition to the contract's Stats (at its end): the voices that stood in as Tape, for
  the diagnostics page (plan 4, M2: "the diagnostics page says so").
- **Vibrato Sync (8.2 #12) is in `tick`.** With `vibSync` not Free the vibrato rate is (bpm / 60) / rateBeats (the
  parameter's note in docs/parameters.json). Free is the prototype's `vibRate`, bit for bit. Its test is M8's.
- **The coarse chase rule, exactly.** From the replay point kk, a step is coarse (ffAdvance (512), then one tick)
  while `kNow - (kk + 512) > 4096 x 32`, that is while more than 4096 ticks would still remain after it. The last
  4096 to 4111 ticks are replayed one by one. So a chase of at most 4096 ticks (every fixture case) is the JS chase
  (checked bit for bit against the JS loop on 3000 ticks). Coarse ticks move the vibrato and Refresh phases once per
  16 ticks: the far past is approximate, as with the JS's own 2^17-tick cap.
- **A chase can run in steps** (`chaseBegin`, `chaseRun` with a tick budget, `chaseEnd`). With the same chase point it
  gives the one-shot result bit for bit (tested with steps of 256). With a moving chase point (the engine's current
  sample) the coarse/fine boundary moves with it.
- **InstSmoother: the none and engine classes apply at `setTarget`,** not at the next grid boundary. The prototype's
  setInst changes `E.inst` at once, and each voice reads it at its own next tick (a voice's ticks run on its own
  32-sample grid, not the engine's). So applying at once is what "applies at the next tick" means for every voice.
  The ramp20ms fields step on the grid. The first value a field ever gets applies at once.
- **readAbs is a template; the Region functions and PlainStretch take `SampleData`.** M7's 16-bit storage adds its
  own read path there.
- **The JS reference numbers in the tests** (seam ratios, the ramp's largest step, the chain's probes, the chase's
  position and first sample, and the per-voice render hashes printed as notes) come from a scratch node script. It
  loads the prototype through `tools/fixtures/common.js` and drives the JS Voice directly, exactly as the C++ rig
  drives `engine::Voice`: a live voice from `noteOn`; a clip voice made with the live voice's constructor
  (`e.voices[0].constructor`) and started as `addVoice` does; `v.render (L, R, k, n, k)` in the same blocks; the
  chain's hand-over at B's start sample, as `startClip` does. Its values: seam ratio 1.0337246672832128 (no fade
  16.193937828473878), ramp largest step 0.0012744665145874023 (no fade 0.20682281255722046), chain probes
  0.2616012913184508 and 0.2959359382681376, chase position 0.2562493661446489 and first sample
  0.0002693095593713224.
- **`Tests/engine/EngineFixture.*`** repeats test_contract's JSON-to-Inst coercion (what JS does to the values before
  normInst), because test_contract keeps it private and the engine group runs every defs.json case itself.
- **For M3: read through `PlainStretch`.** runGrains and runLock have the same inline fast path as runTape in the
  JS. Calling `PlainStretch::read` (falling back to `readReg`) keeps the three identical and covered by
  `readAbsFastPaths`. Use `hannTable ()` for the inline Hann lookups (the JS reads HANN directly, not hannTab).
- **The per-voice parity table** (`test_voice_parity.cpp`) holds the JS Voice's results for 17 renders, made by a
  second scratch script in the same way (the Saw starter, 1 s at 48 kHz, 128-sample blocks): RMS of both channels,
  12 samples and head 1's position. On the first CI run all 17 matched with 0 difference at every probed sample.

### Results (CI)

- Run 42733606918 on 40b0130 (the last code commit), green on both jobs. Windows Release: 851 checks in 117 tests, 0
  failed (engine 24 tests, 188 checks; alloc 5, 31). Debug CRT `--only alloc`: 32 checks in 5 tests, 0 failed, 0
  JUCE assertions. pluginval strictness 10: SUCCESS. Fixture check: passed. Linux Release: 852 checks in 117 tests
  (engine 24, 188; alloc 5, 32). Linux sanitizers: contract, model and engine 476 checks in 74 tests (engine 24,
  187); state 79 checks in 10 tests.
- Bit for bit with the prototype, on both platforms: engine/defs.json 6 of 6, and the per-voice renders on the test
  signals (the loop seam on the tone with and without the fade, the ramp seam, the chain on the tone, the chase on
  the ramp: their hashes equal the JS Voice's).
- The 17-case parity table on the Saw starter is within tolerance on both platforms, with 0 difference at every
  probed sample, but only 5 hashes match on Windows (2 on Linux). The likely reason is the source, not the voice:
  the Saw starter itself is not bit for bit with SLSounds (test_sounds prints hash 2c2dfee3 against the prototype's
  44f47b11, every window within 0), while the same voice paths are bit for bit on the test signals. Not proven case
  by case. E2 should expect the same in renders.json on the starters: hashes printed, windows within 1e-5.


## E2

The engine chain, part 2 of 2: the Engine itself (plan 7.4). Branch `m2-engine`, on top of E1; it merges the engine
chain to main.

### Progress

- Done: `engine/Engine.cpp` (the stub replaced), `EngineSchedule.cpp`, `EnginePlan.cpp`, `EngineDisplay.cpp`, the
  private part of `Engine.h`, `Voice::reanchorSlack`. Tests: `test_parity_renders`, `test_tuning_tape`,
  `test_transport`, `test_chase_cost`, `test_determinism`, `test_live`, `test_stability`, `test_alloc_engine`,
  `test_cpu`, with the harness `Tests/engine/EngineRender.*`. CI green (see "Results").
- In progress: nothing.
- Next: the integration step (I): HostSync, MidiRouter and the processor's audio path on this Engine.

### Tests

| Test | Group | What it checks (the M2 row) |
|---|---|---|
| `fixtureEngineRendersTape` | engine | every renders.json case with needs "tape" (36, counted against the file), section 5's tolerances; bit-for-bit share printed; a window sample 2e-5 off and a stats row one voice off both fail |
| `tapeTuning` | engine | MIDI 36/60/84 against a C3 root at 44.1/48/96 kHz and a 44.1 kHz sample at 48 kHz: f0 within +-3 cents; control: a sample declared at the wrong rate measures about 147 cents off |
| `noteStartsOnTheBeat` | engine | 120, 87, 133 bpm, 3 loop passes, and a step 120 -> 87: every start in [0, 1) sample of its beat (1e-6 of rounding allowed below 0); control: against the old tempo the same starts fail |
| `hostJitterChangesNothing` | engine | +-0.003 beat jitter every 512-sample block for 64 bars: 256 starts on time, never 2 voices, the render bit-identical to the unjittered one |
| `preRollStaysSilent` | engine | Play from beat -2, loop on and off: rms before beat 0 is 0, the first sound at the exact sample of beat 0; control: Play at beat 6.5 sounds the last note at once |
| `tempoRampFollowsHost` | engine | the host ramps 120 -> 60 bpm over 8 bars (ppq integrated per sample), tempo and beat per 512-sample block: all 32 starts within 1 sample, the last within 2; control: with the beat left out (the prototype's own clock) the last start is far off |
| `scatterWhileStopped` | engine | stopped, Scatter 1/8T on a live key: 11 jumps, each 0..32 samples after its 1/3-beat grid line of the free clock |
| `chaseStartsMidNote` | engine | Play at beat 1.37 inside a Tape note, plain and with a moving Volume lane: the first sample not 0, rms (0-2.5 ms) / rms (12.5-15 ms) in 0.02..0.3, after 20 ms equal to the straight render within 1e-4 |
| `reAnchorForwardAndBack` | engine | a tempo change with the host 0.05 beat ahead starts the note it jumped over at once; 0.05 beat behind, no note starts twice and the note that just started keeps sounding |
| `chainOnOneVoice` | engine | C3 -> G3 through the Engine: at most 1 voice, sound without a gap; control: Restart on the second note gives 2 voices |
| `chaseCostBounded` | engine | Play inside 16 x 16-beat and 16 x 64-beat 8-head Tape notes at 48 kHz: no 32-sample stretch above 4096 + 256 chase ticks, all 16 voices sounding within 0.35 s, chased playheads within 0.002 of the straight ones (the straight formula itself checked against 16 notes played from their start); the chase block's time printed; the stepped chase bit-identical in 32- and 7-sample blocks |
| `deterministicAcrossBlocksAndStarts` | engine | 10 s of Strand Demo, chase at 1.7, live keys, Scatter, Skip, a jump: blocks 1, 37, 128, 700, 4096 and random 1..700 bit-identical; started after 0, 7 and 1000 stopped samples bit-identical; rms > 0.01; control: Play at 1.75 differs |
| `liveVelocityGain`, `liveBendRange`, `liveSustain`, `liveModWheelTravel` | engine | velocity gain within 1e-6 (0.5, 0.2, MIDI 100); bend +1 at range 7 within +-3 cents of G3; sustain holds a released key until it lifts (control: without it the key ends); mod1 -> Travel at 0.25 plays -1x within 1e-6, 0.5 stops, control 0.75 plays +1x |
| `voiceStealSmooth` | engine | 20 keys at polyphony 16: 16 voices, 4 steals, largest step within 10 ms of a steal / steady < 1.5; the step a cut would make is printed and checked to be far above 1.5 |
| `sampleSwapFadesOnOldSample` | engine | 220 -> 330 Hz swap under a sounding key: step / steady < 1.5, the stolen voice holds the old sample's epoch through its fade; control: the 330 Hz sample steps 1.5 times as far |
| `stabilityStrandDemoChords` | engine | Strand Demo, 8 heads Tape, 16 live chords, 10 s: nonFinite = filterResets = 0, pre-limiter peak > 0.05, rms > 0.01, peak <= 0.98 |
| `healthCountersCount` | engine | a 1e7 source trips the output guard (nonFinite > 0), a NaN in the source resets the filters (filterResets > 0), the output stays finite, resetStats clears both |
| `deferredStartsAndPoolCuts` | engine | 300 notes at one sample: 256 start, 44 start at the next segment (deferredStarts = 44); steals and pool cuts counted exactly |
| `editsFollowAndEpochs` | engine | a deleted note's voice holds its snapshot epoch while it releases, then the new one only; a Volume edit is heard within two ticks on the same voice; no snapshot: no epoch |
| `tapeStandInsCounted` | engine | Voice and Cloud keys count in tapeStandIns (until M3: the expectation follows `playedEngine`), the display shows the engine asked for |
| `engineNoAllocations` (also `alloc`) | engine, alloc | 0 allocations and frees in 10 s of everything at once (parameter sweeps with ramps, bypass, wheels, chords, a jump, a bounded chase, a sample swap, snapshot swaps, stop and start, display, stats, reset) |
| `engineTapeCpu`, `scanStartsCpu` | cpu | 16 voices x 8 heads Tape at 96 kHz in 64-sample blocks (target 8x, gate > 1x); scanStarts on 4096 notes before any voice sounds (target 0.5 % of a core, gate 2.5 %); gated on Windows only |

### Files

| File | What it is |
|---|---|
| `engine/Engine.h` (private part) | the pool, the voice list (pool slots in insertion order), the clock, the smoother, the context, the start list, the scan rules, the planner's arrays |
| `engine/Engine.cpp` | prepare, forgetVersions, reset, setSample, setInst, setSnapshot, setTransport (1.10, 8.2 #3, #4, #21), noteOn, noteOff, allNotesOff, the controllers, process (32-sample segments on the control grid), the output stage, minEpochInUse, the ramp calls |
| `engine/EngineSchedule.cpp` | the pool (allocVoice, the cut), addVoice (admit + start), scanStarts (binary search, 256 starts, deferral, scan rules, catch-up), startClip, doChase, the per-32 chase continuation |
| `engine/EnginePlan.cpp` | plan and measure, ported whole |
| `engine/EngineDisplay.cpp` | fillDisplay, stats, resetStats |
| `Tests/engine/EngineRender.*` | the renders.json harness, and `Script`: actions at exact samples, host blocks, a hook per block |

### Decisions and deviations

- **The private part of Engine.h is new.** It includes `Voice.h`, `Clock.h` and `InstSmoother.h` (the contract's
  forward declarations stay). The contract's private `inst`, `transport` and `bypassed` are gone: the smoother holds
  the instrument (`ctx.inst` points at `smoother.current ()`), the Clock the transport. The public API is unchanged.
- **The output stage runs per 32-sample segment**, inside the segment loop. The limiter works sample by sample, so
  this is the JS loop over the whole call. It lets outGain and the bypass fade move per sample between the grid
  values (2.3): the gain at grid position c is g0 + (g1 - g0) (c + 1) / 32, in linear gain between 10^(dB / 20) of
  the values before and after the step. A value that does not move is that exact gain, so static settings play the
  JS numbers. A value set between two boundaries with no ramp (ramp time 0, the parity tests) applies at once.
- **The scan horizon of 8.2 #3 is a rule per re-anchor.** At a tempo or loop change the engine keeps the old clock
  (anchor, rate) and the sample kR. A start whose kOf (U) under the old clock is before kR was emitted already and is
  not emitted again. When the clock moved forward, the first scan after kR also takes the starts the new clock has
  passed (kOf below kR, not emitted by the old clock) and starts them at kR. Exact in samples, so no start is doubled
  or lost by rounding. Up to 8 rules are kept; each expires once the scan has passed its horizon (a tempo ramp
  re-anchors every block and holds 1 or 2). Beyond 8, the oldest is dropped.
- **A backward re-anchor keeps just-started notes sounding (`Voice::reanchorSlack`).** Found while testing the rule
  above: moved back by up to 0.1 beat, the clock is behind a note that has just started, and the JS rule
  `u < U - 1e-6 -> release` (meant for a note edited to start later) released it at once. With Cubase reporting a
  cycle wrap a little late at a tempo change, that cuts the downbeat. The engine sets the slack to U - u for every
  clip voice the clock is now behind; the tick tolerates that much and clears it once the clock reaches U. Without a
  backward re-anchor the slack is 0 and the rule is the JS's.
- **The tempo re-anchor also fixes a JS sliver.** The JS re-anchors at cur and scans from u - 2 db, so after a tempo
  increase a note with U in (cur - db_new, cur - db_old] started twice (found by reading the code; a sliver of
  about 1e-7 beat, not measured). The rule above suppresses it.
- **Starts past 256 in one segment.** scanStarts keeps the 256 earliest by the JS key (kS, s, p), made total with the
  note index (the JS sort is stable on note order) and the pass, in a sorted fixed array. The rest are counted in
  deferredStarts and started at the next segment's first sample (found again with the key of the last one started).
  A new snapshot or a re-anchor ends a deferral (its keys are note indices and old-clock samples): a pathological
  case only (more than 256 starts in 32 samples at that very moment).
- **Binary search with the JS tests.** scanStarts finds candidates for each loop pass m by binary search on s
  (widened by 1e-6 beat), then runs each through the JS tests exactly as written (the clip range, mA..mB, U in the
  window, kOf, playStartK). So the starts are the JS's bit for bit. A snapshot whose notes are not all kept by the
  JS filter, or not start-sorted (a raw one), falls back to the JS walk over every note.
- **The bounded chase.** One chase moment (doChase) runs up to 4096 ticks, shared in doChase order: each voice
  begins (1 tick) and runs with what is left. A voice that catches up is added exactly as the JS (chase, ffChain,
  addVoice): every fixture case. A voice that does not is admitted to the voice list at once (steals, polyphony,
  setSnapshot following and minEpochInUse see it) but stays `chasing`: render skips it, plan () passes it over (no
  grants), the display does not show it, stats ().voices counts it. At every boundary of the control grid (not the
  chase moment's own) the chasing voices get 256 ticks, in list order; one that has caught up runs chaseEnd,
  ffChain, startAt and plan () there, with its 10 ms fade-in. A voice that got no budget at the chase moment begins
  on the grid: the engine marks it `chasing` with `chaseK = INT64_MIN` ("not begun"). Released or stolen while
  chasing, a voice is done at once (it never sounded). So any 32 samples run at most 4096 + 256 ticks.
- **startClip with a bounded chase.** Two checks that never fire on the JS paths (the JS chase ends before the
  starts of its sample are scanned): a chain note whose chain a voice is still chasing does nothing (the chase hands
  over by itself), and a note a just-finished chase already handed over to at this very sample does not start a
  second voice.
- **doChase returns when u < 0** (8.2 #4: before bar 1 nothing is sounding), besides the plan's wrap () and the jump
  anchor (Clock::loopBeat already had it).
- **reset () re-chases.** AudioProcessor::reset stops every voice at once; while the transport plays, the clip
  notes that were sounding come back through a chase at the next sample (pendingChase), so a host reset in the
  middle of a song does not leave the held notes silent until the next note start.
- **Stopping clears a pending chase** (the JS leaves it, and its doChase returns while stopped: the same).
- **The display's head position is a fraction of the voice's own sample** (the JS uses the engine's current one):
  the same except for a voice fading on the sample before a swap (8.2 #23).
- **minEpochInUse ignores epoch 0** (a version given without an epoch). The VersionExchange always stamps 1 or more.
- **The pool.** A new voice takes the first free pool object; compaction (the JS's at the end of each segment) also
  runs before taking one, so a done voice's object is reused at once. With all 64 busy, the oldest stealing voice is
  cut (stats ().cuts); if none is stealing (not reachable with polyphony at most 32) the oldest voice.
- **Test expectations learned on CI.** A voice stolen before it ever sounded (env 0) ends at its first sample, so 300
  simultaneous starts leave 32 voices, not 64; and the biquads' own guard zeroes values beyond 1e9, so the output
  guard's control uses a 1e7 source.
- **What the engine-level tests do not cover** (the integration step's, plan 7.4): HostSync's cycle wraps inside a
  block and the tempo-and-cycle row, MidiRouter's CC 32 -> -0.98236x, setNonRealtime, edits while publishing 1,000
  snapshots a second and the exchange row, the processor's allocation row.

### Results (CI)

- Run 42744115791 on a80ea2d (the last code commit), green on both jobs. Windows Release: 934 checks in 144 tests,
  0 failed (engine 48 tests, 264 checks; alloc 6, 34; cpu 2, 4). Debug CRT `--only alloc`: 35 checks in 6 tests, 0
  failed, 0 JUCE assertions. pluginval strictness 10: SUCCESS. Fixture check: passed. Linux Release: 933 checks in 144
  tests (engine 48, 264; alloc 6, 35; cpu 2, 2). Linux sanitizers: contract, model and engine 552 checks in 98 tests
  (engine 48, 263); state 79 checks in 10 tests.
- renders.json, needs tape: 36 of 36 within tolerance on both platforms; bit for bit 22 on Windows, 13 on Linux.
  Every case that is not bit for bit plays the Saw starter (14 on Windows), which is itself not bit for bit with
  SLSounds (E1); every case on a test signal is bit for bit.
- Figures (Windows; Linux in brackets). CPU, 16 voices x 8 heads Tape at 96 kHz: 10.5x real time (9.2x), target 8x.
  scanStarts on 4096 notes: 0.063 % of a core (0.069 %), target 0.5 %. Chase cost: the most ticks in one 32-sample
  stretch 4096 (the chase moment); 70,958 ticks in all for 16 x 16 beats and 107,030 for 16 x 64 beats; all 16
  voices sounding by 0.175 s and 0.269 s; chased playheads within 3e-8 of the straight ones. Tuning: the worst of 12
  is 0.12 cents. Tempo ramp: every start on the host's sample (the prototype's own clock: 240 samples off, as the
  plan says). Voice steal: step ratio 0.68. Sample swap: 0.81. Stability: a pre-limiter peak of 3.18 (the limiter
  holds the output at 0.90).
- For review (no target in the plan, printed only): the chase moment's own 32-sample block takes about 1.7 ms on the
  CI runner, while a 32-sample block lasts 0.67 ms at 48 kHz. Cubase's buffer is larger than 32 samples (a 256-sample
  block lasts 5.3 ms), so the one-off cost of 4096 ticks fits; the plan's 270 ms worst case is gone. Every timing
  target of the M2 table was met.

## Engine summary

For whoever builds on the engine (integration, M3).

- **What it is.** `sl::engine::Engine` is the port of sl-engine.js's Engine with every plan change of 8.2 (#3, #4,
  #6, #8, #21, #22, #23, #29, #32). It plays every note as Tape until M3 (`playedEngine` in Voice.h is the one
  switch; `Stats::tapeStandIns` and the display's `engine` field say which notes stood in).
- **Proof.** renders.json: all 36 Tape cases within section 5's tolerances on both platforms, most bit for bit; the
  rest are on the Saw starter, which is itself not bit for bit with SLSounds (E1). defs.json 6 of 6.
- **Calling it (the processor, plan 1.3).** prepare (sr) once per prepareToPlay (it allocates the 64-voice pool the
  first time only), then per block: setSnapshot / setSample when the exchanges hand over a version, setInst with the
  parameters, setBypass; MIDI, commands and transport each at its sample with process () for the stretches between;
  setInUse (minEpochInUse (...)) for both kinds; fillDisplay every displayEvery (sr) samples. releaseResources:
  forgetVersions () first. Nothing on the audio thread allocates (gated with everything running).
- **The transport (1.10).** setTransport once per block and at a cycle wrap (HostSync's split). A beat more than 0.1
  away from the engine's clock (modulo the clip) is a jump: clip voices release, the notes sounding there are
  chased. A smaller difference changes nothing unless the tempo or loop changed; then the clock re-anchors to the
  host's beat with the scan rules (no start doubled or lost) and the just-started notes keep sounding. Negative
  beats never wrap: pre-roll is silent. While stopped, pass the beat only when it moved (the free clock runs).
- **The control grid** counts from the last transport start: two renders that start playing at the same song
  position are bit-identical, whatever ran before, at any block size.
- **Chase cost.** A Play inside long notes runs at most 4096 ticks at once, then 256 per 32 samples; the 16 x 64-beat
  case sounds after about 0.28 s. The chase moment's own block is the expensive one (printed by chaseCostBounded).
- **For M3.** runGrains and runLock go into Voice::renderRun's dispatch (VoiceRender.cpp) and `playedEngine` becomes
  `return wanted;`. The planner (EnginePlan.cpp) is already the whole JS planner: the grain units only change what
  the heads estimate (`est`) and measure (`segPeak`). The display already writes grains (Voice and Cloud slots,
  Lock captures). `tapeStandInsCounted` follows `playedEngine` and needs no edit. The 37 grains cases of
  renders.json run through the same harness (`render::renderFixtureCase`): drop the `needs == "tape"` filter in
  test_parity_renders.cpp (the raw-snapshot case needs a raw-snapshot reader in the harness; it says so now).

## P1

The plugin chain, part 1 (plan 7.4): the document, the sounds, the state and the host plumbing. Branch
`m2-plugin`. The audio path stays ProbeSynth until the integration step.

### Progress

- Done: state (SMPL file record, CLIP through ClipCodec, shared payloads), doc/ (ClipDocument, EditHistory,
  SnapshotPublisher, SampleManager, Sha256), ParamBridge::applyPatch, host/HostSync, host/MidiRouter, the processor
  wiring, the tests (test_state_clip, test_doc, test_host_midi, the M0 tests updated), the decisions and the
  hand-off below. CI run 42732142894: everything built; Linux passed every new test (2 failures: an M0 test with a
  stale chunk index, fixed); Windows stopped in probe/levels (the stack, fixed below).
- In progress: CI on the fixes.
- Next: CI green, the CI figures here.

### Decisions and deviations

- **A new instance opens on Strand Demo and the Saw that opens up.** BLUEPRINT 6: "Strand Demo clip and the
  saw-that-opens-up sound load on first open" (the prototype's `init()`; its `init` sound preset is
  `defaultInst()`). A state that holds no readable CLIP chunk (an M0 project, a damaged chunk) gets an empty 4-bar
  clip instead, the clip's default, as a parameter the state does not mention gets its default. So an M0 project
  does not start playing chords when it is opened again.
- **CLIP is one of the chunks this version writes**, in plan 3's table order: PARM, INST, CLIP, SMPL, UIST. A state
  without one re-saves with it appended after the kept chunks (M0's rule for missing chunks).
- **Payloads are shared** (`state::PayloadPtr`). The processor encodes the clip once per clip version, and a save or
  the banner check (twice a second) compares the shared payload, so a 32 MiB clip is never copied or encoded again.
- **SMPL's default starter is now `sawopen`** (D9), and SMPL kind 1 (the file record) is read and written with
  every field of plan 3, the analysis cache written empty (M7 fills it). A cache that a record holds is kept and
  written back as it was.
- **Who publishes a loaded sound.** A sound the user loads changes parameters (root and cents, the Region), which
  plan 2.4 sets on the message thread as host-visible edits. So the worker hands such a sound back
  (`SampleManager::takePending`), and the processor applies the patch first and publishes the sound after it: the
  audio thread never plays the new sound with the old Region. A project load, Undo and a new instance's default
  change no parameter, so the worker publishes those itself, and `setStateInformation` waits for that on its own
  thread (decision 3).
- **Newest wins.** The worker makes only the newest request waiting, and a sound whose request is older than the
  newest one made is never published (a project load that arrives while a user load is being made wins).
- **`prepareToPlay` waits too** (up to 10 s) for the sound and the clip already asked for (a new instance's
  default). The reason is decision 3's: the first block plays the sound, and the worker never needs the thread
  that waits. Without it a new instance would play silence for about 80 ms after it is made.
- **`setStateInformation` also waits for the loaded clip's snapshot** (up to 10 s), so the first block after a load,
  or an offline export started right after it, plays the loaded clip. The SnapshotPublisher's thread compiles it;
  nothing else compiles, so its SnapshotCache is never shared between threads.
- **A snapshot's memory is bounded** (NOTES-M1 asked for it). `SnapshotSizer` gives an upper bound of the lane
  tables at a resolution (a lane counts as flat, 2 floats, only when it has no generator and every node holds the
  same value). The publisher compiles at the finest of 256, 128, 64, 32, 16 and 8 that fits 256 MiB, measures the
  result, and goes one step coarser if it is still over. A clip that does not fit even at 8 is not compiled: an
  empty snapshot plays, and `stats().tooDetailed` says so. ClipDocument refuses an edit that would make such a clip
  ("That would make the clip too detailed to play ..."). This is a third refusal beside plan 3's two caps, because
  a clip inside both caps (4096 long notes with every lane moving: a few MB encoded) could need gigabytes of tables.
  Only a hand-made state can now hold one.
- **ParamBridge::applyPatch opens a gesture only for a parameter whose value changes**, and returns what changed
  (EditHistory needs it). GrainLock's `applyPreset` opens one for every parameter; an unchanged one would only add
  noise to Cubase's edit history.
- **A project's file is found only while its size and SHA-256 match** (plan 3: "the original path (if the SHA-256
  matches)"). A file at the path with other bytes counts as missing, and the toast says it changed. A saved path
  that is not absolute on this system (a project from a Mac) counts as missing without being made into a
  `juce::File` (which asserts on a relative path).
- **A long file's part** follows the prototype's rule for a song: longer than 20 s (sl-ui.js `LONG_FILE`), measured
  on the part loaded. Its Region starts as 0.35 .. 0.35 + 4 / seconds, with the exact edges in INST. A file longer
  than 10 minutes loads its first 10 minutes (`floor (600 x rate)` frames) and the toast says so.
- **SHA-256 is written out in `doc/Sha256`.** The plan's JUCE modules do not include `juce_cryptography`, and
  `CMakeLists.txt` is not this part's file. It is checked against the FIPS 180-4 vectors.
- **The probe plays from the sample exchange.** ProbeSynth's `setSound` takes `engine::SampleData` (it took a
  `StarterSound`), so the probe plays whatever the SampleManager published, files included, until the integration
  step replaces it. `Source/probe/SoundSlot.h` is no longer used; the integration step deletes it with `probe/`.
- **The probe tests load the starter Saw first** (`useSaw`): a new instance now opens on sawopen, and the probe
  tests' criteria (M0) were written for the Saw.
- **The strip reads `doc::SoundView`.** MainView's small StripView draws the current sound from the SoundView
  (name, file or built-in, missing) so the editor keeps working; P2 replaces it with `ui/strip/WaveStrip`.
- **HostSync takes `diag::HostBlock`**, what `diag::readHostBlock` already reads from the play head without
  allocating, so the play head is read once per block.
- **MidiRouter sends a CC to both wheels when both listen to it**, and checks CC64 and CC120/CC123 on their own, so a
  wheel assigned to CC64 still sustains.
- **The live row's "CC 32" is the mod wheel (CC1) at value 32**: 32 / 127 on a Travel target plays -0.98236x (the
  prototype's `travelSpeed`). No 14-bit pair is needed; JUCE's VST3 wrapper sends 7-bit controllers.
- **Undo and sounds.** An undo step that changes the sound asks the SampleManager for the old one (no patch); the
  root, cents and Region the step changed come back through pv. `SoundMeta` (each sound's own root, plan 2.4) is
  not kept yet: every starter, and every file until M7's Detect, is rooted at C3, so pv holds the same values.
- **Load reports are kept until the editor takes them, at most 32**, so a project opened with no editor does not
  collect them for ever.
- **The diagnostics count the blocks played before a state load's sound is there** (decision 3):
  `DiagModel::audioWaitingForSound`, shown on the report's State line. The probe plays no clip notes; the
  integration step silences clip notes in those blocks (the processor's `awaitedSound` against
  `SampleManager::finishedRequest`, both lock-free).
- **Group names.** The doc layer's unit tests are group `doc`, HostSync and MidiRouter's `hostsync` (plan 7.2 names
  it for later milestones), applyPatch's `params`. The state tests stay in `state`, which the sanitizer job runs, so
  the two threads run under AddressSanitizer there.
- **The processor keeps its two rings on the heap**: the diagnostics' 4096-event ring (about 200 KB) and the
  command queue. With the ring inline a processor was about 200 KB, and probe/levels keeps five on the test
  thread's stack: with M2's additions that passed Windows' 1 MB main-thread stack and the runner stopped there with
  no output (CI run 42732142894, twice; Linux has 8 MB). `doc/processorFootprint` now holds `sizeof
  (StrandLockProcessor)` to 128 KB. For the integration step: keep the DisplaySlot (three frames of about 8.5 KB)
  and anything else large behind a pointer too.

### Hand-off for the UI builder (P2)

Everything below is on the processor (`StrandLockProcessor`), message thread unless it says otherwise.

- **Sounds.**
  - Load a starter: `loadStarterSound (sounds::StarterId id, std::int32_t seed = 1)`. Load a file (Browse, a drop):
    `loadSoundFile (juce::File)`. Both return a request number at once; the worker makes or decodes the sound.
  - When it is ready the processor's AsyncUpdater (`handleAsyncUpdate`) sets root 60, cents 0 and the Region (the
    whole sound, or 4 s from 35 % in for a song) as host-visible edits, records the sound in SMPL, makes one undo
    step ("Sound: Evolving pad", "Load song.wav") and publishes the sound. The editor needs nothing for that. A test
    with no message loop calls `waitForSounds()` (waits, then delivers).
  - The current sound: `getCurrentSound()` returns `std::shared_ptr<const doc::SoundView>`: `left`, `right` (the
    same pointer when mono), `frames`, `sampleRate`, `seconds()`, `stereo`, `name`, `builtIn`, `starter` and `seed`
    (when built in), `ref` (the SMPL record: a file's path, size, SHA-256, the loaded part), `missing` (the
    project's file was not found; the default starter plays), `cut` (a file over 10 minutes, its first 10 minutes),
    `request`. It is the peaks source: the samples are shared with what the engine plays and never change, so the
    strip can keep the pointer and compare it in its timer (a new sound is a new pointer). It is null only for the
    first few tens of milliseconds after construction.
  - Toasts: `takeSoundReports()` returns `doc::LoadReport { request, origin, ok, message }` since the last call.
    Show the ones with a non-empty `message`: a missing or changed project file ("Could not find the sound file
    ..."), a file cut to 10 minutes, a song's default part, a file that could not be read. The prototype's
    "Loading ..." toast before the load is the UI's own.
  - `getSampleRef()` is what the project names (`state::SampleRef`, kind 0 starter or 1 file).
- **The clip.** `getDocument()` is the `doc::ClipDocument`: `current()` (the immutable `model::ClipPtr`), `version()`,
  `encodedBytes()`, `instState()`, `soundRef()`. Edit through the processor so the edit is one undo step:
  `editClip (label, model::EditResult)` or `editClip (label, model::Clip)`. The outcome (`doc::EditOutcome`) has
  `accepted`, `refused` and `message`: show `message` as the toast when refused (the note cap, 32 MiB, or "too
  detailed to play"); the clip is then unchanged. `undo()`, `redo()`, `canUndo()`, `canRedo()`, `undoLabel()` and
  `redoLabel()` cover the clip, the INST state, the sound and the parameters the steps changed.
- **INST state** (seed, wheel targets and CCs, key, scale, scale lock, exact Region edges): `getInstState()` and
  `setInstState (s)`; the document writes the audio thread's atomics.
- **Snapshots.** `getSnapshotPublisher().stats()` has `tooDetailed` (show a banner when true) and the resolution
  used; `waitForSnapshot()` for tests.
- **Commands to the audio thread**: `getCommandQueue()` (doc/CommandQueue.h): push `UiCommand::auditionOn` etc.
  Nothing drains it until the integration step.
- **Freeing versions**: call `collectVersions()` from the editor's 10 Hz timer (plan 1.3). The publisher and the
  sound worker also collect after each publish, so nothing piles up without an editor.
- **Left for later parts**: the audio path (Engine, HostSync, MidiRouter, the display slot, setInUse; the
  integration step), the starter menu, Browse and drop wiring, the waveform strip with the Region box and zoom
  (P2), MIDI Learn (M8), Preview (M6), SoundMeta and Detect (M7), the analysis cache (M7).
