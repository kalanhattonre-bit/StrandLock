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
