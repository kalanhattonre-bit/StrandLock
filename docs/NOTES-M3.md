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
- Run 43102817014 on b616092 (the exchange fix): green on every job and step.
- In progress: CI on this commit (notes only), then the fast-forward of main.
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

### Results (CI)

- Run 43102817014 on b616092, green on every job and step: Windows Release 1299 checks in 205 tests (engine 362 in
  67, alloc 35 in 6, cpu 4 in 2); Linux Release 1298 in 205 (engine 362 in 67); the sanitizers' contract, model and
  engine groups 650 in 117 (engine 361 in 67), state 142 in 20; Debug-CRT allocation run 36 in 6, 0 JUCE
  assertions; Debug snapshots 0 assertions; pluginval strictness 10 SUCCESS; the fixture check passed (24 files,
  4.68 MB). The exchange stress: the deletion reached the audio 60 ms early in Release (the publisher ahead) and
  12 ms late under the sanitizers, and the tail was silent in all three.
- The figures below are from run 43099804752 on 3776cdf, the same code; run 43102817014 printed the same.
- Run 43099804752 on 3776cdf (the code of the merge head; only the exchange test's fix came after it): Windows
  Release 1298 checks in 205 tests, 0 failed; Linux Release 1297 in 205, 0 failed; Debug-CRT allocation run 36 in
  6, 0 failed; pluginval strictness 10 SUCCESS; the fixture check passed; the sanitizers' state group 142 in 20;
  their contract, model and engine groups 649 in 117 with the one exchange check above failing (fixed after).
- renders.json, needs grains: 28 of 28 within tolerance on both platforms, 20 bit for bit on Windows and 9 on Linux
  (needs tape: 36 of 36, 22 and 13, as before). The 9 Lock cases wait for L, as listed.
- Voice tuning: C1, C3, C5 at 48 and 96 kHz, the worst 0.0058 cents (gate 3). Cloud, printed: C1 -9.9 and -10.1
  cents, C3 +0.005 and -0.006, C5 +2.45 and +2.50 (48 and 96 kHz), the prototype's figures. Pitch +0.1 st reads
  +10.004 and +9.993 cents.
- Formant: +6 st raises the centroid 40.0 % (997 -> 1395 Hz), -6 st lowers it 31.2 % (685 Hz); f0 within 0.0015
  cents in all three; a 0.05 st nudge moves it 0.04 %.
- Glide: C3 -0.006 cents, G3 -0.003 (Voice) and -0.004 (Tape), one voice, envelope 1.0000 at the hand-over. Auto
  Slide Off with the glideTo link: one voice, G3 -0.003 cents; without the link 2 voices.
- Motions: Tape Backward r = 0.9910 at lag 0, Forward 0.0036; Backward reads -0.12499 (Voice) and -0.12514 (Cloud)
  in 0.5 s against -0.12500. Freeze spread 1.3e-5 (Voice) and 0 (Cloud), Forward 0.3000; Tape's Freeze moves
  0.30000 (1x). Bounce 4 turns on every engine, inside 0.2003 .. 0.3000 (Voice), 0.2056 .. 0.2956 (Cloud),
  0.2002 .. 0.2998 (Tape). Mirror +1.0000x -1.0000x +1.0000x -1.0000x on Voice and Cloud.
- Scatter at 1/8, 87 bpm: 15 jumps on 15 grid lines, 1.1 to 27.6 samples after them, on Tape, Voice and Cloud;
  renders identical; seed + 1 moves 15 of 15; the largest step 0.0048 (Tape), 0.0041 (Voice), under 0.0001 (Cloud).
- Refresh 100 ms at 4x: the read position moves 0.00000 inside a period (Smooth 0.0300); each refresh grabs
  0.0497 .. 0.0503 further. Skip 0.5: 6 of 13 grab (5 with seed 2), renders identical. Spray 0.25: median
  deviation 0.01077 against 0.00000, the most 0.0275; identical; seed + 1 differs.
- Chase at beat 1.37: the Voice and Cloud playheads equal the straight render's (difference 0), the fade ratio
  0.144 and 0.145; the audio reads 0.09177 (Voice) and 0.09453 (Cloud) against the playhead's 0.09187 (printed).
- Allocations: the engine sweep through Tape, Voice and Cloud makes 0 allocations and 0 frees (19 voices during
  the bounded chase), in Release and in the Debug-CRT run.

## L

M3 part L of 3 (plan 7.5 "L"): the Lock engine. Branch `m3-engine`, on top of G (main = 8ffa98d when L began).

### Progress

- Done: `engine/VoiceLock.cpp` (lockGrab, lockRho, runLock), the Lock arm of Voice::renderRun, `playedEngine`
  playing every engine as itself, the raw-snapshot reader in the renders harness, all 37 grains cases expected to
  run, the stand-in lines gone from the Diagnostics page and the Copy report (with their tests), the Lock rows
  (tuning, harmonic lines, chase, allocations). Commits 8b5ff56 (the engine) and b401898 (the rows).
- Done: CI run 43111963519 on b401898, green on every job and step at the first try (figures below).
- In progress: CI on this commit (notes only), then the fast-forward of main.
- Next: part B (see "For B" below).

### What was built

- **`Voice::runLock (h, run)`** (`engine/VoiceLock.cpp`) is sl-engine.js runLock line by line, with lockGrab and
  lockRho. A head captures the sample at its position (capA) and plays N taps one period of the played note apart
  (PR = P x the rate ratio), all at the same phase th of their period, Hann-weighted across the N periods and
  scaled by sqrt (2 / N). The sum repeats every period, so noise and chords play in tune. N is the Grain setting in
  periods (h.nT, at most 12, set by Voice::tick) and at most half the head's grant. A re-grab (capB) crossfades in
  over one period with equal-power fades and the correlation correction 1 / sqrt (1 + 2 rho gi go); lockRho is the
  two captures' correlation over one period (16 phases, both channels). At the end capB becomes capA.
- **Refresh hold.** Refresh Smooth re-grabs by itself every nA periods (h.periods). A Refresh in ms or synced
  holds the capture until Voice::tick sets h.regrab at a refresh that is not skipped (or at a Scatter jump), as
  E1 ported it. Spray scatters each grab with the grain engines' hash (gIdx counts the grabs).
- **The reads.** The taps read through `PlainStretch::read`, falling back to `readReg`, as runTape and runGrains
  do; lockRho calls readReg and hannTab, as the JS does; the tap weights read `hannTable ()` directly, as the JS
  reads HANN. The source is the voice's own (8.2 #23). No allocation: the captures are the Head's fields.
- **The switch.** `playedEngine (wanted)` returns `wanted`: since L no note plays as Tape except a note whose engine
  is Tape. renderRun dispatches Kc::lock to runLock (`else runLock (h, run);`, as the JS `else`). The Lock switch on
  Cloud (`lock = engine == cloud && (the note's or inst.lock)`), the Lock branch of Voice::tick (P, PR, dth, nT,
  est) and the display's captures were already there (E1, E2).
- **Lock units in the planner.** Nothing to add: Voice::tick sets est = 2 max (nT, nA, nB while crossfading),
  runLock measures N, or N + N' during a crossfade (h.peak, then segPeak), and `measure ()` sums them. The five
  budget-16x8-lock12-* fixtures (Lock N = 12, 16 voices x 8 heads, with thinning and 10 to 15 cuts) match the
  prototype's stats exactly, so the planner already does what they ask.
- **The raw-snapshot reader** (`render::rawSnapshotFromJson`, Tests/engine/EngineRender.cpp) turns the engine-only
  snapshot of tools/fixtures/engine.js (rawSnapOut) into a core::Snapshot: the notes in the file's order, every
  setting explicit, lanes from their Tables, glide and chain by id (the same in all three Auto Slide modes),
  parity clip settings. renderFixtureCase sets it as the JS does (`e.setSnapshot (spec.snapshot)`).
- **The stand-in lines are gone.** The Diagnostics page loses its Engines row; its Voices row reads "N now, most
  M"; the version row says "test build: Tape, Voice, Cloud and Lock engines". The Copy report loses its "Engines:"
  line and the "playing as Tape instead of Lock" part of its "Engine:" line. DiagModel no longer carries the
  stand-in counts, and the processor no longer copies them. The Sound tab never had such a line (G checked).

### Decisions and deviations

- **`Stats::tapeStandIns` and `Voice::asTape` stay, always 0.** They are engine-side and cost nothing, and the engine
  tests use them to check that no note stands in. The diagnostics side (DiagModel's figures and snapshot fields)
  went with the lines, since nothing showed them any more. B or the fix pass may remove the engine fields too.
- **The stand-in tests now check that every engine plays itself.** `engine/tapeStandInsCounted` became
  `engine/noTapeStandIns` (playedEngine is the identity for all four, tapeStandIns 0, the display shows grains for
  Voice and Cloud, one capture per head for Lock, none for Tape). `plugin/onlyLockStandsInAsTape` became
  `plugin/everyEnginePlaysItself` (through processBlock: 2 voices each, tapeStandIns 0, the newest display frame
  shows the engine and grains only for the grain engines, and the report's Engine line has no stand-in text). The
  Lock part of `engine/voiceInitInPlace` now checks that a Lock voice reused in place renders bit for bit what a
  new one renders (with Scatter and synced Refresh, so the re-grabs run), and not the Tape render.
- **The JS local `kc` in runLock is `kRho` in C++.** A local named kc would hide the member Voice::kc (GCC -Wshadow
  on StrandCore). Only the name differs.
- **The raw-snapshot reader refuses a glide longer than 0.95 of its note.** The JS reads X.glide.len as it is;
  core::resolve clamps the glide length to 0.95 l. The two agree for every shorter glide (the fixture's is 0.5 beat
  on a 2-beat note), and a case that would differ fails loudly instead of rendering something else.
- **No case waits for B.** `kWaitForLock` became `kWaitForB`, an empty `std::array`; `kGrainCasesNow` is 37. A case
  B finds it must hold back goes there with its reason, and the count follows.
- **Two negative controls for the grains fixtures:** voice-c3 played as Tape (G's) and now lock-from-noise played
  as plain Cloud (the Lock switch off): blockRms[0] 0.0515 against the prototype's 0.0557, far outside 1e-4.
- **Lock tuning runs 18 renders, not 12.** The JS gates Refresh Smooth and 120 ms; the M3 table also names 400 ms,
  so it is gated too (within 0.15 cents in the prototype and here). The measure is the JS periodF0 (Lock repeats
  exactly every period), ported to `GrainRig` with its Hermite read (samples outside the render read 0). The
  sources are the JS test's: noise seed 5, and the chord C3 E3 G3. Negative control: Pitch +0.1 st reads +9.82 cents.
- **"Harmonic lines > 10 dB over the floor"** is the JS gate: the worst Lock harmonicity (mean power on the lines
  k f, k = 1 .. 8, over the mean power in the gaps) is more than plain Cloud's on the same noise plus 10 dB. Plain
  Cloud reads -0.1 dB, so this is also more than 10 dB over the gaps.
- **The Lock chase** is G's chase test with a Lock note (Cloud, lock on the note and the instrument). The chased
  Lock note shows one capture (its one head) on the display, so it plays Lock, not a stand-in.
- **The engine allocation sweep** now cycles Tape, Voice, Cloud and Lock (Spray on Cloud and Lock, N = 12 on one
  step), and checks that display frames showed Lock voices playing (72 of them). The processor-level allocation
  tests already move the Engine and Lock parameters at random, so Lock can run there too (not counted there).

### For B

- **State of the branch:** every engine plays itself; nothing stands in as Tape. All 37 grains cases of
  renders.json run and match (`kWaitForB` is empty). The planner is the JS planner (NOTES-M2 E2); with Lock playing,
  the five budget-16x8-lock12-* fixtures already match the prototype's stats exactly (thinned heads, cuts, max
  load, budget), on both platforms.
- **B's rows still to write** (plan 4, M3 table, and 7.5 "B"): budget (max load <= B at 44.1 / 48 / 96 kHz for Voice,
  Cloud and Lock N = 12; the 6-note Voice arpeggio keeping the newest note's 8 heads; cuts = 0 where the prototype
  measured none; the newest note's sounding heads 8 / 8 / 4), stereo, stability with Lock, determinism with Lock and
  a Voice-to-Cloud chain, CPU at 96 kHz (Lock 16 x 8 N = 12 among them), fractional heads, the aliasing figure,
  and the CUBASE-CHECKLIST M3 section.
- **Helpers:** `GrainRig` now has `periodF0` and `harmonicity` besides G's harmonicF0, spectrum, centroid, corr and
  stats. `render::rawSnapshotFromJson` reads any raw snapshot in the fixture encoding.
- **Scratch scripts** that compute the prototype's Lock figures: `m3l/figs-lock.js` in the task scratchpad
  (arguments `tuning`, `harm`, `chase`; it loads `m3g/lib.js`).

### Results (CI)

- Run 43111963519 on b401898, green on every job and step: Windows Release 1312 checks in 207 tests (engine 371 in
  69, plugin 58 in 9, alloc 35 in 6, cpu 4 in 2); Linux Release 1311 in 207 (engine 371 in 69); the sanitizers'
  contract, model and engine groups 659 in 119 (engine 370 in 69), state 142 in 20; Debug-CRT allocation run 36 in
  6, 0 JUCE assertions; Debug snapshots 0 assertions; pluginval strictness 10 SUCCESS; the fixture check passed (24
  files, 4.68 MB).
- renders.json, needs grains: 37 of 37 within tolerance on both platforms, 26 bit for bit on Windows and 11 on
  Linux (needs tape: 36 of 36, 22 and 13, as before). The Lock cases on Windows: lock-from-noise, lock-from-chord,
  budget-16x8-lock12-48k and -96k, strand-demo-recipe-8 and raw-snapshot bit for bit; the three budget clip cases
  (stats only) not bit for bit but with the stats exact. On Linux lock-from-noise and lock-from-chord are bit for
  bit. Every case that is not bit for bit plays the Saw or Saw Open starter (NOTES-M2 E1).
- Lock tuning, 18 of 18 within +-3 cents, each the prototype's figure to 4 decimals: from noise C2 +0.30 / +0.16 /
  +0.12, C3 -0.08 / -0.04 / -0.04, C4 +1.04 / +0.55 / +0.51 cents (Smooth / 120 ms / 400 ms); from the chord C2
  -0.66 / +1.37 / -0.15, C3 +0.78 / +1.20 / -0.04, C4 -0.72 / +0.47 / +0.03. The worst 1.37 cents. Pitch +0.1 st
  reads +9.82 cents.
- Harmonic lines from noise at C3: Lock 16.6 dB (Smooth), 28.9 dB (120 ms), 34.7 dB (400 ms); plain Cloud -0.1 dB.
  The prototype's: 16.56, 28.87, 34.70 and -0.13.
- Chase at beat 1.37: the Lock playhead equals the straight render's (difference 0), the fade ratio 0.1452 (the
  prototype 0.1452), one capture on the display; the audio reads 0.09210 of the file at 2400 samples (the prototype
  0.09210; the playhead 0.09187). Voice and Cloud as in G.
- Allocations: the engine sweep through Tape, Voice, Cloud and Lock makes 0 allocations and 0 frees, with 72
  display frames showing Lock voices and 19 voices during the bounded chase, in Release and in the Debug-CRT run.
  The processor-level runs (Strand Demo, with the Engine and Lock parameters moving) also 0 and 0.

## B

M3 part B of 3 (plan 7.5 "B"): the complete budget planner and the rows that cross engines. Branch `m3-engine`, on
top of L (main = 17fc32a when B began).

### Progress

- Done: the B rows as tests (test_budget, test_stereo, test_fractional_heads, test_per_note_engines, test_aliasing,
  the grain additions to test_stability, test_determinism and test_cpu); the renders harness keeps the display of
  stats-only cases (the budget test reads the newest note's heads from it). Commit 5bbcef3; CI run 43144810679
  green on every job and step at the first try.
- Done: two more rows after a second read of the plan: waiting heads come in within 20 ms (in
  budgetNewestKeepsItsHeads), and the Tape switch follows each note, live and across a Voice-to-Cloud chain
  (tapeSwitchFollowsEachNote). The CUBASE-CHECKLIST M3 section. Commit 4fae6cc.
- Done: CI run 43149332941 on 4fae6cc, green on every job and step (results below); this section and the M3
  summary.
- In progress: CI on the notes commit, then the fast-forward of main.
- Next: the M3 review and fix pass.

### What was built

- **The planner, checked whole.** `Engine::plan` and `Engine::measure` (EnginePlan.cpp) were already sl-engine.js
  plan () and measure () line by line (NOTES-M2 E2). B read them again against the JS: the newest note, steady and
  fading load, the bring-back with hysteresis (0.9 x floor (0.75 B)), the thinning order (released, then silent at
  level x env < 1e-3, then oldest; top heads first, head 1 last; the newest only while it alone passes B), heads
  that have not sounded going silent at once, the grants (sounding heads keep their share, the others wait newest
  first), and the last-resort cuts of the oldest tails. They match, so the file's code did not change (its header
  comment now says what B checked). The planner is engine-blind, as in the JS: the units come from each head's est
  (set by Voice::tick for every engine) and its measured peak (runTape, runGrains, runLock). With every engine
  playing since L, that makes it complete in every engine.
- **Per-note engines and the Tape switch across chains** needed no engine code either: Voice::tick reads the
  engine, the Lock switch and the Tape switch from the note it plays now (rn.engine, rn.lock, rn.tape, as the JS
  reads X), and clears the grains when the engine changes inside a chain (lastKc), as the JS does.
- **Fractional heads** were already the JS (E1): H = clamp (heads + lane + macro, 1, 8), the top head at
  (H - floor (H)) of a full head's level, the gains slewed over 20 ms. B's row checks them in all four engines.
- **The rows** (Tests/engine): `test_budget.cpp` (the 16 x 8 layout at three rates, the newest note's heads and
  the waiting heads, no cuts where the prototype has none, the five budget-* fixtures' stats and newest heads, the
  budget edge allocating nothing, in "engine" and "alloc"); `test_stereo.cpp` (four rows in all four engines);
  `test_fractional_heads.cpp`; `test_per_note_engines.cpp` (per-note engines, the Voice -> Cloud -> Tape chain,
  the Tape switch live and across chains); `test_aliasing.cpp` (the printed figure); additions to
  `test_stability.cpp` (16 x 8 in every engine), `test_determinism.cpp` (the grain scenario and the chord under
  budget pressure) and `test_cpu.cpp` (the 96 kHz grain benchmarks and the typical 48 kHz load per engine).
- **The renders harness** (Tests/engine/EngineRender.cpp) now records display frames for stats-only cases too
  (they have none to compare), because the budget test reads the newest note's sounding heads from the last one.
  Filling a frame changes nothing the render or the stats depend on.
- **docs/CUBASE-CHECKLIST.md** has a new part, "M3: the grain engines", after the M0 and M2 parts.

### Decisions and deviations

- **EnginePlan.cpp's code is unchanged.** The plan's "complete" asked for thinning, waiting heads and cuts in every
  engine; E2 had ported the whole JS planner already, and G and L only had to set est and measure the peak in
  their engines. B found nothing to add, so it added checks instead of code.
- **"Waiting heads up to 20 ms" is not a timer**, in the JS or here. It follows from the planner: steady heads are
  planned to at most floor (0.75 B), so once the 20 ms fade tails of thinned heads have ended, every allowed head
  fits. B measures it: in the Voice arpeggio the newest note sounds all 8 heads within 448 samples (9.3 ms) of
  its note-on in the prototype; the row gates <= 960 + 32 samples (20 ms and one control tick) and that some head
  did wait (> 32).
- **The Tape switch row** is test-engine-extra.js B's (Cloud at Speed 2: switch off keeps C3 within 75 cents,
  the prototype +29.4; on plays C4 within 10, the prototype +0.005), plus two chains in which only one note has
  the switch on (Voice C3 within 3 cents, Cloud C4 within 10; the prototype +0.014 and +0.005). A voice that kept
  its first note's switch reads the second note an octave off.
- **Rows the plan names for Tape, Voice and Cloud also run on Lock** (fractional heads, stereo, stability), since
  Lock is a fourth way to play; each meets the same gate.
- **Each budget row also checks that the budget was under pressure** (heads thinned at some point), so none passes
  by doing nothing. The prototype's own figures (computed in node from sl-engine.js) are printed next to each; they
  are equal in every row (max load, thinned heads, cuts, the arpeggio's 0 0 6 14 22 30 and 1 1 1 1 6 8).
- **The Lock budget fixtures are checked twice:** the renders test compares their stats rows (section 5), and
  budgetLockFixtureStats compares thinned heads, max load, B and cuts at every row and the newest note's sounding
  heads at the end (8 / 8 / 4 at 44.1 / 48 / 96 kHz; the prototype's display). The three clip cases are not bit for
  bit on Windows (the pad's Saw-like partials, NOTES-M2 E1) but their stats are exact.
- **Stability with Lock makes cuts.** The JS "16 voices x 8 heads" stress uses Lock grain 12, so the Lock runs cut
  some fade tails (5 at 44.1 kHz, 10 at 48), as the budget fixtures do. The plan's stability row does not ask for
  0 cuts there; it asks for no non-finite sample, no filter reset, output in bounds and load <= B, which hold.
- **CPU, as decision 4 says:** printed against the targets and gated at > 1x on Windows only. The typical Lock load
  (8 voices x 2 heads at 48 kHz) reads 17.0x on the Windows runner against the 20x target (19.6x on Linux); every
  other figure meets its target. Not chased.
- **The aliasing figure is printed with a plain sentence**, and gated only on the measure itself (the band-limited
  Saw source reads -104 dB, a naive saw -21 dB) and on each render sounding.
- **The checklist's "CPU meter" is Cubase's Audio Performance meter.** StrandLock has no CPU meter of its own (the plan
  does not give it one), so the M3 steps say so and point to Cubase's, as the M0 and M2 steps do.
- **Stats::tapeStandIns and Voice::asTape stay** (always 0; L's decision). The tests use them to show no note
  stands in. Removing them is the fix pass's call.

### Results (CI)

- Run 43149332941 on 4fae6cc, green on every job and step: Windows Release 1433 checks in 227 tests, 0 failed
  (engine 476 in 86, alloc 37 in 7, cpu 18 in 4, plugin 58 in 9); Linux Release 1425 in 227 (engine 476 in 86,
  alloc 38 in 7, cpu 9 in 4, where times are printed but not gated); the sanitizers' contract, model and engine
  groups 764 in 136 (engine 475 in 86), state 142 in 20; Debug-CRT allocation run 38 in 7, 0 JUCE assertions;
  Debug snapshots 0 assertions; pluginval strictness 10 SUCCESS; the fixture check passed (24 files, 4.68 MB).
- Run 43144810679 on 5bbcef3 (the first push of B) was green on every job and step too: Windows 1426 checks in 226
  tests, Linux 1418 in 226, the sanitizers 757 in 135 and 142 in 20, Debug-CRT 38 in 7. Every figure below that
  both runs print is the same in both, on Windows and on Linux, except the times.
- renders.json, needs grains: 37 of 37 within tolerance on both platforms, 26 bit for bit on Windows and 11 on
  Linux (needs tape: 36 of 36, 22 and 13).
- Budget, the 16 x 8 layout (max load of B, heads held back at the end, cuts), every figure the prototype's:
  44.1 kHz (B 208) Voice 178, 96, 0; Cloud 141, 89, 0; Lock 147, 117, 5. 48 kHz (B 192) Voice 162, 99, 0; Cloud
  132, 92, 0; Lock 120, 118, 10. 96 kHz (B 96) Voice 84, 110, 0; Cloud 72, 110, 0; Lock 80, 120, 15.
- The Voice arpeggio (6 keys 80 ms apart): the newest keeps its 8 heads after every note-on; heads held back after
  each note-on 0 0 6 14 22 30; sounding heads, oldest first, 1 1 1 1 6 8; max load 192 of 192, 0 cuts; the newest
  sounds all 8 heads 32, 32, 32, 448, 448 and 448 samples after its note-on (at most 9.3 ms). All the prototype's.
- No cuts where the prototype has none: the Lock Grain 4 arpeggio (max load 144, 30 held back), the Voice arpeggio
  (162, 29), Strand Demo with recipes 3, 6 and 8 at Grain 12 (113, 117 and 192; 13, 4 and 32 held back).
- The Lock N = 12 fixtures: every stats row equal. At the end, budget-16x8-lock12-48k 10 cuts, 120 held back, max
  load 168 of 192, the newest note 8 heads; -96k 15, 124, 72 of 96, 4 heads; -clip-44.1k 15, 120, 180 of 208, 8;
  -clip-48k 15, 120, 180 of 192, 8; -clip-96k 15, 124, 84 of 96, 4.
- At the budget edge: 0 allocations and 0 frees, in Release and in the Debug-CRT run, with 10 and 15 cuts and 120
  and 124 heads held back at 48 and 96 kHz. The engine sweep through all four engines (L's) also 0 and 0.
- Fractional heads (the largest step at the changes over the steady one): Tape 0.861, Voice 1.000, Cloud 0.764,
  Lock 0.967 (gate 1.5; the prototype's to 4 decimals). A click of twice the steady step reads 2.32.
- Stereo (Tape / Voice / Cloud / Lock): Spread 0 on a mono source, L == R bit for bit in all four; a stereo source,
  corr 0.0033 / 0.0035 / 0.0033 / 0.0071; Spread 0.6, corr 0.841 / 0.839 / 0.841 / 0.826; Mirror with 8 heads and
  Spread 0.6, the mono sum loses 0.246 / 0.227 / 0.250 / 0.235 dB. All the prototype's.
- Per-note engines: a Tape note at C5 lifts the centroid x 2.000, a Voice note x 1.037. The chain Voice -> Cloud ->
  Tape plays on one voice (the engines voice, cloud, tape), the playhead moves 0.00133 around each joint (1x), the
  quietest 10 ms has rms 0.151. The Tape switch: live Cloud at Speed 2 reads +29.43 cents from C3 with it off and
  +0.005 from C4 with it on; in both chains Voice reads +0.014 cents from C3 and Cloud with the switch +0.005 from C4.
- Stability, 16 x 8: nonFinite 0 and filterResets 0 in all six runs; pre-limiter peaks 9.1 to 32.7, output rms
  0.34 to 0.44, peak 0.900; max load 175 of 192 (Voice), 96 (Tape), 132 (Cloud), 156 (Lock), 91 of 96 (Voice at
  96 kHz), 152 of 208 (Lock at 44.1 kHz).
- Determinism: the grain scenario is bit-identical across blocks of 1, 37, 700, 4096 and random sizes and after 7
  and 1000 stopped samples (rms 0.306); the Voice chord under pressure (30 heads held back) is bit-identical after
  0, 7 and 1000 stopped samples.
- CPU on the Windows runner, best of 3 (run 43149332941, then 43144810679): at 96 kHz, 16 x 8, Cloud with 140 ms
  grains 7.55x and 7.83x, Lock N = 12 8.20x and 8.52x, Voice around C5 6.64x and 6.91x (targets 2.0x); at 48 kHz,
  8 voices x 2 heads, Tape 39.4x and 39.7x, Voice 29.5x and 30.5x, Cloud 21.8x and 22.0x, Lock 16.8x and 17.0x
  (targets 20x: Lock is short of it); M2's Tape 16 x 8 at 96 kHz 8.2x twice (target 8x). On Linux (not gated)
  10.8x, 11.4x, 9.5x; 54.6x, 42.0x, 30.7x, 23.1x; Tape 10.7x.
- Aliasing, Tape on the Saw at 48 kHz, the energy off the harmonic lines: -42.13 dB at +0 st, -27.77 at +12, -19.58
  at +24 and -15.10 at +36 (the prototype's to 2 decimals). The measure itself: the Saw source -104.1 dB, a naive
  saw -21.4 dB.

## M3 summary

- **What M3 built.** Every engine plays itself now: Tape (M2), Voice and Cloud (part G, runGrains), and Cloud with
  the Lock switch (part L, runLock). The budget planner (E2's port of the JS) is complete in all of them, and part B
  checked it with every engine playing. Spray, Refresh hold, Each grain backwards, the Tape switch, Path kind for
  Voice and Cloud, per-note engines, chains across engines and fractional heads all play as in the prototype. The
  default sound (Voice) plays as in the prototype.
- **Against the prototype.** All 37 grains cases of renders.json are within the section 5 tolerances on both
  platforms (26 bit for bit on Windows, 11 on Linux), and the 36 Tape cases are still green (22 and 13). Every
  case that is not bit for bit plays the Saw, Saw that opens up or pad starter (NOTES-M2 E1).
- **The M3 table.** Every row is a gated test, except the timing figures (printed against their targets, gated at
  faster than real time on Windows, decision 4) and the aliasing figure (printed). The figures are in the G, L and
  B results above.
- **Tests at the merge head** (run 43149332941): Windows Release 1433 checks in 227 tests, Linux Release 1425 in
  227, the sanitizers 764 in 136 and 142 in 20, the Debug-CRT allocation run 38 in 7, pluginval strictness 10,
  all passed.
- **The aliasing figure, for open question 6.** Played three octaves above its recorded pitch (+36 semitones), the
  Tape engine turns the Saw into a note in which about 3 % of the sound (-15.1 dB) is aliasing: tones that are not
  part of the note. Two octaves up it is about 1 % (-19.6 dB), one octave up 0.2 % (-27.8 dB), and at the recorded
  pitch 0.006 % (-42.1 dB). The prototype measures the same.
- **Open for the review and the fix pass.**
  - The typical Lock load at 48 kHz (8 voices x 2 heads) runs about 17x real time on the Windows runner against a
    20x target (printed, not chased, decision 4). Every other CPU figure meets its target.
  - Decision 1 (the light theme's darker Tone, Sample and playhead colours) is the fix pass's.
  - Stats::tapeStandIns and Voice::asTape are still in the engine, always 0.
  - StrandLock has no CPU meter of its own; the checklist uses Cubase's Audio Performance meter.
  - Kalan's answers still to come from the checklist's M3 part: how the engines sound in his Cubase, and whether
    a big Lock chord at his rate or at 96 kHz crackles.

## Fix

The M3 fix pass: the findings of the three M3 reviews (grain parity, Lock and budget, tests and real time) and plan
7.5 decision 1. Branch `m3-fixes`, on top of main = 4720ffe.

### Progress

- Done: every finding checked against the code and the prototype (in node), all seven confirmed. The fixes and
  their tests (below). Plan 7.5 decision 1 (the light theme colours). Commits 9cd2ec5 .. e11c4bb.
- Done: CI run 43160312596 on e11c4bb, green on every job and step at the first try (results below). The light
  snapshots looked at: the darker Tone and Sample colours and the playhead read clearly on the roll and the strip.
- In progress: CI on this commit (notes only), then the fast-forward of main and the CI run on main.
- Next: nothing in this pass; the architect's list below.

### The findings

- **R3-GP-1 (high): a Path-kind Voice, Cloud or Lock note chased far in ran away to NaN. Fixed.** Confirmed in node
  with the C++ chase rule copied onto sl-engine.js (scratch `m3fix/path-chase.js`): a 16-beat Path note, Play at
  beat 12, gives head 1 NaN in all three engines at 48 kHz, and at beat 6 at 96 kHz. A Path head steers to its
  target each tick (`h.spd = (pathPos + h.scat - h.raw) / 32`), and the coarse chase step moved it 512 samples at
  that speed: 16 times too far, so its error was multiplied by -15 each step. Now `Voice::ffAdvanceCoarse` moves a
  Path-steered head one tick of the 512 samples (it lands on that tick's target and stays there until the next
  coarse tick steers it again), while the Spread offset still moves the whole 512. The flag is `Voice::pathSteer`,
  set by Voice::tick where it sets every head's speed. Steering forgets where a head came from, so the last 4096
  fine ticks land it exactly where the JS chase does: in node the fixed rule renders bit for bit what the JS chase
  renders, in all six cases. Speed-kind heads and every chase under 4096 ticks are unchanged.
  Test: `engine/chaseGrainsPathFarIn` (test_chase_grains.cpp): Voice, Cloud and Lock, 16 beats, Travel path
  0.1 -> 0.9, Play at beat 12 (48 kHz) and beat 6 (96 kHz). The chase must take coarse steps (fewer ticks replayed
  than the note has), head 1 must be finite and within 0.002 of the straight render, and the audio must read within
  0.01 of the playhead (a runaway head reads frame 0). The build before the fix gives NaN.
- **R3-LB-1 (medium): the budget rows could not catch several planner bugs. Fixed.** Confirmed: I put the
  reviewer's 24 mutations into sl-engine.js plan () and ran every scenario of test_budget.cpp in node (scratch
  `m3fix/sim3.js`); nine of them changed no gated figure. Now the prototype's figures are gated exactly: max load,
  heads held back and cuts of the nine layouts; the arpeggio's heads held back after each note-on (0 0 6 14 22 30),
  waits (32 32 32 448 448 448), heads per voice (1 1 1 1 6 8) and max load 192; the two arpeggios' and the three
  recipes' max load and heads held back. New rows, each turning red under its mutation (scratch `m3fix/sim7.js`):
  the bring-back after the 3 newest keys let go (in budgetNewestKeepsItsHeads), and `engine/budgetThinningOrder`
  with quietest first (loud, faint at -45 dB, silent, new), released first, a dyad after an arpeggio (the wait list
  newest first; the last-resort cuts take tails only, oldest first) and the cut order (released tails first, at
  96 kHz). With these and the budget-* fixtures every mutation goes red except two that change nothing the engine
  can show: `lockHalfIsGrant` (N <= half the grant never binds, because a granted Lock head's grant is
  2 max (nT, nA, nB)) and `measureNoReset` (a done voice's peaks are never read again).
- **R3-LB-2 (medium): Play inside a held heavy chord made cuts the prototype does not make. Fixed.** Confirmed in
  node with the C++ bounded chase (doChase, continueChases, finishChase) copied onto sl-engine.js (scratch
  `m3fix/emu.js`, `chord2.js`): a Lock N = 12 x 8-head chord, Play inside it, gives 2 cuts (4 notes, 96 kHz), 6
  (8 notes, 96 kHz) and 4 (8 notes, 48 kHz, beat 2); the prototype 0. The planner passed chasing voices over, so
  each late voice became the newest when it caught up, after the earlier ones had sounded. Now the planner counts a
  chasing voice as plan 1.8 says, waiting: it can be the newest note, its heads count in the steady load, and it is
  a thinning candidate (its heads have not sounded, so a thinned one just waits). It is never granted and nothing
  of it is cut. In node this gives 0 cuts in all eight chord cases I tried, and the chord's last note sounds 4
  (96 kHz) or 8 (48 kHz) heads at the end, as in the prototype. Test: `engine/budgetChaseInsideLockChord`.
- **R3-LB-3 (low) and R3-1 (medium): the budget-pressure determinism row could not see the control grid. Fixed.**
  Confirmed in node: the old chord (every note at Play) renders bit for bit the same in the prototype after 0, 7 and
  1000 stopped samples, although the prototype's grid counts from the engine's creation. The chord's notes now start
  0.13 beat apart and end at different times (R3-1's layout): the prototype then differs by 0.0227 after 7 and after
  1000 stopped samples, so a C++ build that lost the grid origin (8.2 #21) turns the row red. Still 30 heads held
  back at the most. The same row now also renders in blocks of 1, 37, 700, 4096 and random 1..700 (the prototype
  renders them all bit for bit).
- **R3-2 (medium): the checklist said cuts stay at 0 except at Lock grain 12. Fixed.** Confirmed in node (scratch
  `m3fix/cuts.js`, the prototype's planner, which the C++ matches): cuts come at the default Lock grain 4 when
  chord notes land within about 5 ms, in Voice and Cloud at 8 heads too (the dyad row above makes 2), and with Lock
  grain 11 or more at 48 kHz (12 at 44.1 kHz) or 6 or more at 96 kHz nearly every new note cuts a tail. The M3
  part now has a "Cuts" bullet that says so, and steps 8 and 9 ask Kalan for the cut count and whether he hears a
  click when notes start, instead of calling any cut a fault.
- **R3-3 (low): the M2 part still said Voice and Cloud play as Tape. Fixed.** The bullet now says it was true of
  the M2 build only.
- **Plan 7.5 decision 1: the light theme's Tone, Sample and playhead colours.** Darkened in the prototype's hue
  (HSL lightness lowered, hue and saturation kept, 8-bit rounding) to the lightest colour that reaches 3:1 against
  both the darker roll row #eef0f5 and the strip #e8ebf6. The strip is the darker one and binds:
  Tone #d78000 -> **#c37500** (3.140:1 on the row, 3.009:1 on the strip; the prototype's #e08600 2.430 and 2.328),
  Sample #b68f39 -> **#a68234** (3.141 and 3.009; the prototype's #b8913a 2.576 and 2.468),
  playhead #ed7100 -> **#d86700** (3.143 and 3.011; the prototype's #ff7a00 2.292 and 2.196).
  Hue shifts against the prototype's: 0.11, 0.38 and 0.10 degrees. The theme test's reference is now the row and
  the strip (>= 3:1 on both), and "no darker than needed" is under 3.1:1 on the strip.

### Decisions and deviations

- **A chasing voice thins first, so the voices of a long chord settle differently after Play.** With the planner
  counting chasing voices, the voices the bounded chase starts later have not sounded when a new one begins, and the
  prototype's own rule (a head that has not sounded goes silent at once, and voices at env 0 count as quiet) thins
  them before the older voices that already sound. So after Play inside 16 held Tape pads the late voices keep 1
  head and the early ones 8, where the prototype (every voice at the same sample) keeps 1 head on the oldest 8.
  Measured in node: the same heads and load in all, 0 cuts, and every voice sounding by 0.17 s. Before the fix the
  C++ ended like the prototype but cut tails on the way (0 to 3 cuts in those cases, 2 to 7 with Lock N = 12).
  No cut is the plan's rule; which held notes are thinner is not audible as a fault. No fixture case is affected:
  every fixture chase runs at once.
- **The Path fix moves the head one tick per coarse step, not the step's 16 ticks of steering.** It lags the moving
  target by at most one coarse step (512 samples of the path) until the fine ticks, which then land it exactly.
  The reviewer's second option (no coarse steps for Path voices) would cost up to 16 times the ticks for the far
  past. Plan 8.2 #22 calls the far past approximate; for Path heads it is now exact at the chase point.
- **The new budget figures are gated exactly, as the fixtures' stats are.** They are planner decisions (integers),
  which the C++ has matched on both platforms in every budget row so far. Each one is printed with the prototype's.
- **The new determinism layout follows R3-1** (staggered starts and ends) rather than R3-LB-3's 0.0137-beat spacing:
  both see the grid in the prototype (0.0227 and 0.041); R3-1's also ends notes while others thin.
- **Stats::tapeStandIns and Voice::asTape stay** (always 0; L and B left them to this pass). The engine tests use
  them to show that no note stands in. Removing them changes nothing Kalan sees.

### For the architect

- **Cuts are part of the planner, not a rare edge.** Plan 1.8 and 4 (M3) read as if cuts happen only with Lock
  N = 12. The prototype's planner (and so the C++) cuts tails whenever a new voice's first head does not fit beside
  the fade tails: notes that start within a few ms of each other while others sound, in every grain engine at 8
  heads, and nearly every new note at Lock grain 11 or more (48 kHz) or 6 or more (96 kHz). A cut stops a tail at
  once, which may click. The checklist now asks Kalan whether he hears it. If he does, the rule to look at is "a
  new voice's first head never waits" (plan 1.8).
- **Plan 8.2 #22 (the far past is approximate)** could say that a Path-kind head lands exactly at the chase point
  (VoiceChase.cpp, ffAdvanceCoarse).
- **Plan 1.8's "a voice still chasing counts as waiting"** is now what the planner does; its consequence for which
  held notes thin after Play is above (Decisions).

### Results (CI)

- Run 43160312596 on e11c4bb, green on every job and step: Windows Release 1476 checks in 230 tests, Linux Release
  1468 in 230, the sanitizers' contract, model and engine groups 806 in 139 and the state group 142 in 20, the
  Debug-CRT allocation run 38 in 7 (0 JUCE assertions), Debug snapshots 0 assertions, pluginval strictness 10
  SUCCESS, the fixture check passed. renders.json: needs grains 37 of 37 within tolerance (26 bit for bit on
  Windows, 11 on Linux), needs tape 36 of 36 (22 and 13), as before.
- The Path chase far in (Voice, Cloud, Lock; 48 kHz at beat 12, 96 kHz at beat 6): 4413 ticks replayed for 9001
  ticks of note (coarse steps taken), head 1 equal to the straight render's (0.705000 and 0.402500, difference 0,
  the prototype's), the audio reading 0.70507 / 0.70716 / 0.70513 and 0.40247 / 0.40701 / 0.40252 of the file
  (the prototype 0.70480 / 0.70708 / 0.70530 and 0.40255 / 0.40692 / 0.40245).
- The Lock chord, Play inside it: 0 cuts in all three (before the fix 2, 6 and 4 in node); chase ticks 6014, 12110,
  12110 (spread over blocks); heads held back 28, 60, 56 (the prototype's); max load 84 of 96, 72 of 96, 156 of
  192; the chord's last note sounds 4, 4 and 8 heads (the prototype's).
- The budget figures, now gated exactly, all equal the prototype's on both platforms: the nine layouts (as in B's
  results), the arpeggio (waits 32 32 32 448 448 448, held back 0 0 6 14 22 30, per voice 1 1 1 1 6 8, max 192),
  the arpeggios 144 / 30 and 162 / 29, the recipes 113 / 13, 117 / 4, 192 / 32. The new rows: bring-back
  1/1,7/7,8/8 (8 held back, 0 cuts); quietest first 1/1,8/8,1/1,8/8 (14, 0); released first 8/8,r2/2,8/8 (6, 0);
  the dyad 1/1,1/1,1/6,6/8,1/1,7/7 then 1/1,1/1,1/1,6/6,1/1,8/8 (2 cuts); the cut order 1/8,r0/3,1/1 ..
  1/1,r0/3,8/8 (1 cut).
- Determinism under budget pressure: 30 heads held back at the most, rms 0.2445 (the prototype's); bit-identical
  after 7 and 1000 stopped samples and in blocks of 1, 37, 700, 4096 and random 1..700.
- The light theme: Tone #c37500 3.140:1 on the roll row and 3.009:1 on the strip, Sample #a68234 3.141 and 3.009,
  the playhead #d86700 3.143 and 3.011; hues 36.00 / 41.05 / 28.61 degrees against the prototype's 35.89 / 41.43 /
  28.71.
- The aliasing figure is unchanged: Tape at +36 st on the Saw, -15.10 dB of the energy off the harmonic lines (the
  prototype -15.10 dB), about 3.1 % of the sound.
