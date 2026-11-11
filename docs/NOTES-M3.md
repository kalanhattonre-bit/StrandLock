# NOTES-M3

Builder notes for M3 (PLUGIN-PLAN section 4 and 7.5). Append only: one bullet per item, with the reason. Each part
writes its own section: G, L, B, Fix.

## G

M3 part G of 3 (plan 7.5 "G"): the grain engines Voice and Cloud. Branch `m3-engine`.

### Progress

- Done: `engine/VoiceGrains.cpp` (Voice::runGrains), the dispatch in Voice::renderRun, `playedEngine` playing
  Voice and Cloud as themselves (Lock still as Tape), the stand-in lines on the Diagnostics page and in the Copy
  report, `Stats::tapeStandIns` (now Lock only), the renders test for the grains cases, the stand-in tests.
  CI run 43091177044 on 19d90f3 green on every job: 28 of 28 grains cases within tolerance.
- Done: the M3 table rows in `test_tuning_grains`, `test_formant`, `test_motions`, `test_refresh_spray`,
  `test_chase_grains`, with the rig `Tests/engine/GrainRig.*` (commit 2b7ba95); the engine allocation sweep now
  plays Voice and Cloud too.
- Run 43094374797 on 2b7ba95: every new row passed but Scatter (6 checks, on both platforms): the test played the
  87 bpm row at 120 bpm (its helper's fixed tempo), so the jumps missed the 87 bpm grid. Fixed in the test.
- Run 43099804752 on 3776cdf: every step green but one check of M2's `engine/exchangeUnderLoad` under the sanitizers
  (Tape only, so not the grains): a timing race in the test, fixed (see "Decisions").
- In progress: CI on the last commit, then the fast-forward of main.
- Next: part L (see "For L" below).

### What was built

- **`Voice::runGrains (h, run, voice)`** (`engine/VoiceGrains.cpp`) is sl-engine.js runGrains line by line: the
  same operation order and constants. Voice is the root-grid PSOLA (grains one period P of the played note apart,
  each starting on a whole number of root cycles Ts from its loop pass's Region start, read at the formant ratio);
  Cloud is free grains (hop = len / 3, read at the pitch ratio, Spray around the head). The grain cap is
  max (1, min (8 or 5, grant)); when a head is full the oldest grain gives way, so above the cap grains get shorter
  and none is dropped. Each grain backwards (rev) plays every grain from its end. A smooth head aims each grain
  half a grain ahead of the head; a refreshing head reads its last grab (h.grab, set by Voice::tick).
- **The reads.** The JS inline Hermite fast path is `PlainStretch::read` (bit for bit readReg's on the plain
  stretch, as E1 asked), the fallback `readReg`. The Hann lookups read `hannTable ()` directly, as the JS reads HANN.
  Grain time `t` is the voice's int64 sample count as a double (exact below 2^53). The grain slots are the Head's
  fixed arrays (read through raw pointers so int indices need no casts). No allocation, lock or wait.
- **The switch.** `playedEngine (wanted)` returns Tape for Lock and the asked-for engine for the rest. Voice::tick
  already had the Voice and Cloud branches (E1 ported them: P, len, step, norm, hop, est), so nothing else changed
  there. renderRun dispatches Kc::voice and Kc::cloud to runGrains.
- **Grain units in the planner.** Nothing to add: the planner is the whole JS planner (NOTES-M2 E2), Voice::tick
  sets each head's est (Voice min (8, ceil (len / P) + 1), Cloud min (5, ceil (len / hop) + 1)) and runGrains
  measures each head's most grains in the stretch (h.peak, then segPeak), which `measure ()` sums. The stats rows of
  all 28 grains cases (load, maxLoad, thinned, cuts) equal the prototype's exactly. None of the 28 thins a head (in
  the prototype too); thinning and cuts are the budget rows, B's.
- **Path kind, Refresh, Spray, Scatter for the grain engines** needed no new code outside runGrains: Voice::tick
  (E1) already steers a Path-kind head (`h.spd = (pathPos + h.scat - h.raw) / 32`), sets h.grab at a refresh that
  is not skipped and at a Scatter jump, and h.spray. The fixtures `voice-path-ramp`, `cloud-path-ramp`,
  `cloud-skip-0.25`, `cloud-refresh-free-60ms`, `cloud-spray-0.3` and `cloud-each-grain-backwards` cover them bit
  for bit or within tolerance.
- **The stand-in lines.** The Diagnostics page's Engines row now says "Tape, Voice and Cloud play now. Notes set
  to Lock play as Tape until the next build.", its Voices row and the Copy report's Engine line say "playing as
  Tape instead of Lock", and the version row says "test build: Tape, Voice and Cloud engines". `Stats::tapeStandIns`
  now counts Lock notes only (always 0 once L plays Lock). The Sound tab has no line about the engines (checked:
  SoundTab.cpp's only caption is about the sound menu and M7), so nothing changed there.

### Decisions and deviations

- **9 grains cases wait for part L**, each named with its reason in `kWaitForLock` (test_parity_renders.cpp), and
  the test expects exactly 28 to run. Found by counting runLock calls in the prototype for every case (a scratch
  copy of the engine's text with a counter): these 9 call runLock, the other 28 never do. They are lock-from-noise,
  lock-from-chord, the 5 budget-16x8-lock12-* cases (Lock N = 12; their budget row is B's), strand-demo-recipe-8
  (recipe 8 is Cloud with Lock) and raw-snapshot (note raw-c is Cloud with Lock; the harness also has no reader for
  a raw snapshot yet: renderFixtureCase refuses it). No case needs more of the planner than there is: in the
  prototype none of the 28 thins a head.
- **The parameter tuning test pins Tape.** `plugin/parametersReachTheEngine` checks that Root Note D3 plays C3 two
  semitones low and Root Cents -50 half a semitone high. That holds for Tape only: the Voice engine plays the key's
  own pitch whatever the root (P = sr / fHz, and fHz = rootHz x 2^((p - root) / 12) = the key's frequency; the root
  sets the grain size and the phase grid), as in the prototype. The test always meant Tape ("Tape at C3 against its
  C3 root"); the default engine is Voice, which played as Tape until now. Voice tuning is `engine/voiceTuning`.
- **Backward is checked two ways, as the prototype does.** Tape reads 1:1 at the root, so its output correlates
  with the reversed source (r > 0.95, Forward |r| < 0.1: the M3 row's numbers). A Voice or Cloud grain plays forwards
  inside itself, so its output does not (measured: r 0.04 and 0.003); for them Backward is the read position
  falling at 1x within 0.003 over 0.5 s (the JS checks Voice; Cloud added).
- **Mirror "within 0.003"** is each head's speed (display position over 0.1 s, in x) within 0.003 of +1 or -1, and
  its arrow (dir) the same sign. The JS checks the signs only; the C++ heads move at exactly +-1.000000x.
- **Scatter runs on Tape (the JS row), Voice and Cloud** with the same criteria; all three put 15 jumps on the 15
  grid lines, 1.1 to 27.6 samples after them.
- **The chase compares the playhead, not the audio.** A chased grain voice primes its grains as if it had been
  running (needPrime), all aimed from the head's position at that one sample, so for a moment they read up to two
  hops ahead: 2400 samples after Play the Cloud audio (140 ms grains) reads 0.0027 of the file ahead of the head, as
  in the prototype (printed). The plan's row says "playhead"; the test compares head 1's display position with the
  straight render's at the same song sample (0 difference in the prototype).
- **Voice tuning uses the JS measurement**, harmonicF0 (harmonics within +-60 cents and 40 dB of the strongest,
  magnitude-weighted), on the JS source (the C3 tone at 44.1 kHz). The suite's fftFundamentalHz (lowest peak within
  12 dB) would pick a harmonic at C1, where the grains' energy sits around C3.
- **The formant control** renders the centre again with a 0.05 st offset: the centroid moves 0.04 % in the
  prototype, so the 25 % and 10 % gates measure the formant, not noise.
- **Negative controls.** Voice tuning: Pitch +0.1 st measures +10 cents. The grains fixture test: voice-c3 played
  as Tape fails the comparison. Freeze: Forward spreads 0.3. Refresh: Smooth moves 0.03 inside a period. The chase:
  heads left at the note start would be 0.086 away. Glide: without the glideTo link, Auto Slide Off plays 2 voices.
  The stand-in tests show grains on the display for Voice and Cloud and none for Tape and the Lock stand-in.
- **M2's exchange stress had a race, now fixed.** `engine/exchangeUnderLoad` deletes every note at 9.5 s of audio
  and wanted silence from 9.6 s. But the two threads pace each other loosely: the audio may run up to 96 publishes
  (96 ms) ahead of the publisher, and the publisher compiled the empty clip afresh for every publish, which is slow
  under the sanitizers. So the audio could reach 9.5 s, where the clip's "keep3" note starts (beat 19), before the
  deletion arrived; the note then sounded until its 60 ms release ended, after 9.6 s (run 43099804752: peak 0.07 in
  the tail; voices 0 at the end). It plays Tape only, so the grains did not cause it; it had passed by luck. The
  test now compiles the empty clip once, notes the first block that plays it, checks that this lies inside the
  pacing window (-67 .. +99 ms of 9.5 s), and wants silence from 100 ms after the later of that block and 9.5 s.
- **The engine allocation test's sweep** (test_alloc_engine) now moves through Tape, Voice and Cloud, with Spray
  and Each grain backwards on some steps, so runGrains is counted in the Debug-CRT job too. The processor-level
  allocation tests already played Voice (the default engine) and moved the Engine parameter.

### For L

- **The switch:** make `playedEngine` `return wanted;` (Voice.h), and add the Lock arm to Voice::renderRun's
  dispatch (`else runLock (h, run);`, VoiceRender.cpp). Voice::tick already has the Lock branch (P, PR, dth, nT,
  est = 2 max (nT, nA, nB while crossfading)) and the Refresh / Scatter regrab flags. The display already shows Lock
  captures (capA, th).
- **runLock** reads the same way runGrains does: `PlainStretch plain (S, G)` once per run, `plain.read (x, a, b)`
  or `readReg`, `hannTable ()` for the inline Hann weights, `hannTab` and `fadeIn` where the JS calls them.
  lockGrab's Spray uses the same hash as runGrains (`hash32 (nh, h.i, h.gIdx, seed, kSaltSpray)`).
- **Fixtures:** remove the Lock cases from `kWaitForLock` in test_parity_renders.cpp as they pass, and change
  nothing else: `kGrainCasesNow` follows. raw-snapshot also needs a raw-snapshot reader in
  `Tests/engine/EngineRender.cpp` (renderFixtureCase refuses a case with "snapshot"): the engine-only BLUEPRINT 3.2
  snapshot of tools/fixtures/engine.js rawSnapshot () / rawSnapOut () (settings resolved, lanes as Tables, chain and
  glide by id) into core::Snapshot, as `rig::rawSnapshot` builds one. The budget cases should then pass as they are
  (the planner is the JS planner); if one does not, it is B's.
- **The stand-in lines after L:** tapeStandIns is always 0. `engine/tapeStandInsCounted`, the Lock check in
  `engine/voiceInitInPlace` and `plugin/onlyLockStandsInAsTape` follow `playedEngine` and need no edit (they then
  expect 0 stand-ins and grains on the display for Lock). The texts need L's edit: `kEnginesNote` and the Voices
  row in ui/DiagnosticsPage.cpp, the two report lines in diag/DiagModel.cpp ("Engines:" and "playing as Tape instead
  of Lock"), the version row, and the comments on Stats::tapeStandIns, Voice::asTape, DiagModel::EngineFigures and
  the processor's class comment. `onlyLockStandsInAsTape` looks for "playing as Tape instead of Lock: N now" in the
  report, so change both together.
- **Lock tuning** in the JS uses periodF0 (the period estimator by Hermite-interpolated differences) and the
  harmonicity measure (energy on the lines over the gaps); neither is in `GrainRig` yet. harmonicF0, spectrum,
  centroid, corr and stats are, ported from test-engine.js.
- **The Lock chase** row (beat 1.37) can reuse `test_chase_grains.cpp`'s playFrom with core::Engine::cloud and
  inst.lock = true.
