# NOTES-M7

Builder notes for M7 (PLUGIN-PLAN section 4 "M7 Long songs and sample handling" and 7.9). Append only: one bullet per
item, with the reason. Each part writes its own section: V1, V2, V3, V4, Fix.

## V1

The analysis port: `prototype-v4/src/sl-analysis.js` (SLAnalysis) in `Source/analysis/`, plain C++20 with no JUCE (plan
7.9 V1, decisions 1 and 3). Branch `m7-songs`, from main 9270df9.

### Progress

- Done: `Source/analysis/` (Analysis.h, PitchDetect.cpp, KeyDetect.cpp, PartSuggest.cpp, Fft.h, Detail.h), ported line
  by line. A JavaScript copy of the C++ as written (block reads, chunked features, decision 3, the guards) gives all 34
  fixture cases bit for bit, with no read outside the sound (scratch `m7v1/port-check.js`).
- Done: `analysis/detect.json` gains 6 cases (other sample rates, the 5-minute song); `tools/fixtures/analysis.js` and
  `tools/README-fixtures.md` follow. `node tools/make-fixtures.js --check` passes (4.96 MB).
- Done: `Tests/analysis/test_detect.cpp` (group `analysis`); the sanitizer job runs the group.
- Done: run 44856126113 on fc592ac green on every job and step (figures below). After it, the test holds the starters
  while it builds the song (it was making repeated ones again) and names any case that is within tolerance but not
  bit for bit; these notes took the figures.
- Done: run 44858939907 on 346421d (those two changes) is green on every job and step. The one case within tolerance
  but not bit for bit is the Saw starter on both platforms: Hz 261.59511548626801 against the prototype's
  261.59511548626807 (the C++ Saw is within 2e-7 of SLSounds', not bit for bit). The song now takes 911 ms to make on
  Windows (369 ms on Linux).
- Last: main is fast-forwarded to the commit that adds these lines once its own run is green (main is still 9270df9).

### Files

- `Source/analysis/Analysis.h`: the API. `Source` (block reads: channels, frames, rate, `read (c, start, count, out)`)
  and `FloatSource` (over float channels someone else keeps alive); `Stop` (asked now and then; true ends the analysis
  with its empty answer); `detectPitch` -> `std::optional<Pitch>` {midi, cents, hz}; `detectKey` ->
  `std::optional<Key>` {key 0..11, Scale, r}; `suggestParts` -> `std::vector<Part>` {kind, label, start, end, bpm,
  tempoFound}; `features`, `estimateTempo`, `overlapOk` (the prototype exports the first two for its tests); `scaleName`
  ("Major"/"Minor") and `partKindName` ("steady"...), the prototype's strings. `PartKind`'s values are the prototype's
  display order (steady 0, hit 1, quiet 2, loop 3).
- `Source/analysis/PitchDetect.cpp`: detectPitch (YIN on the 5 loudest 2048 frames, the half-periodic rule, the
  10-frame fallback, two frames within 20 cents).
- `Source/analysis/KeyDetect.cpp`: detectKey (Goertzel chroma over MIDI 36-95 in up to 40 frames, Krumhansl-Kessler).
- `Source/analysis/PartSuggest.cpp`: features, estimateTempo, overlapOk, suggestParts (loop, hit, steady, quiet).
- `Source/analysis/Fft.h`: the prototype's `plan` and radix-2 `fft`, not juce::dsp.
- `Source/analysis/Detail.h` (a sixth file the plan does not list; internal): validInput, clean, mono and the 11 kHz
  copy, read from a Source in blocks; a signed-index array for the prototype's typed arrays.
- `Tests/analysis/test_detect.cpp`; `tools/fixtures/analysis.js`, `Tests/fixtures/analysis/detect.json`,
  `tools/README-fixtures.md`; `.github/workflows/build.yml` (one step: the sanitizer run takes `analysis` too).

### Decisions and deviations

- **Decision 3 (DC is "no clear pitch") as YIN's own rule.** The prototype's YIN divides each lag's difference by the
  running sum `(run || 1)`. On DC every difference is 0, so every lag scores 0, the shortest lag (sr / 1500) wins, and
  DC reads as MIDI 90 at 48 kHz. The plugin scores a lag whose running sum is still 0 as 1 (not periodic), as YIN's
  definition does for lag 0. That changes only a frame that is exactly constant over the lags compared; any other
  frame gives the prototype's numbers. So DC gives "no clear pitch" (all frames aperiodic), silence already did, a tone
  riding on a DC offset keeps its pitch (the offset cancels in the differences), and dither on DC reads as noise (no
  pitch). `PitchOptions::prototypeDc` keeps the prototype's reading for the tests. A deviation for plan 8.2.
- **No full-length copies (memory, not results).** The prototype makes a mono Float32Array of the whole file
  (detectPitch, detectKey) and an 11 kHz copy (suggestParts). For a 10-minute stereo 48 kHz file those are 115 MB and
  29 MB, against V2's long-files budget of the int16 bytes + 16 MB. The port reads the sound through `Source` in
  blocks and keeps only per-frame values; `refineOnset` makes the 11 kHz samples it needs again from the source. Each
  mono or 11 kHz sample comes from the same samples by the same operations, so every value is the prototype's. The
  test measures it (below).
- **Guards for input no real file gives**, each where the prototype reads past an array, divides by zero or never
  ends: suggestParts and features give nothing below 1 kHz (below about 120 Hz the prototype's spectrum indexes run
  past its FFT; real files are 8 kHz and up) or above 10 MHz; detectPitch gives no pitch when the found lag is 1 frame
  and the parabola puts the period at 0 (possible only below 3 kHz, where the prototype returns Infinity);
  estimateTempo drops a bar-lag refinement that gives no finite positive tempo (at the edge of its 9-lag search the
  parabola can put the lag at 0 or below, and the prototype's octave loops `while (bpm < 60) bpm *= 2` would then
  never end). None of them changes a fixture case.
- **The fixture gained 6 cases (append-only).** Every earlier case is at 44.1 or 48 kHz, where the 11 kHz step D is 4,
  so the prototype's other branch of the decimation was never compared. Added: a chord at 96 kHz (D = 9), a tone at
  22.05 kHz (D = 2), noise at 32 kHz (D = 3), joined files at 96 and 22.05 kHz (steady, hit and a found 60 bpm loop),
  and the 5-minute song of prototype-v4/tests/test-analysis.js, made by that file's own `buildSong` and `hash` (read
  from it by name, as support.js reads the signal makers; the header now also hashes the test file). The song is the
  plan's 5-minute speed input, so the speed row's file is also compared with the prototype part for part. The file is
  16,134 bytes (was 12,546); the fixtures are 4,962,066 bytes of the 5,000,000 budget.
- **Robustness of the comparison.** The C++ starters are within 2e-7 of SLSounds' but not all bit for bit, and the C++
  library's log, log10, log2, pow, cos and sin may differ from V8's by an ulp. In node, nudging 30 % of every one of
  those functions' results by one ulp (12 seeds) and 0.2 % of the input samples by one Float32 ulp (4 seeds) changed no
  result beyond the plan's tolerances on any case (scratch `m7v1/perturb.js`, `song.js`). So no fdlibm port was needed;
  `Math.exp` is `core::jsExp` anyway, and `x ** 2` is `x * x` (V8 gives the same bits; checked on 2 million values).
- **Cents within 0.1 means one rounding step.** The prototype rounds cents to one decimal, so a one-ulp difference can
  move it by exactly 0.1 (23.3 against 23.2 is 0.100000000000001 apart in doubles). The test allows 0.1 + 1e-9.
- **The input hash is counted, not gated.** The fixture's `hash` describes the prototype's input; the C++ starters are
  not all bit for bit (sounds/starter.json), so the test gates n, channels and rate and prints how many inputs match
  bit for bit.
- **`std` is `stdDev`** in `win ()`'s result (a member named `std` would compile but reads badly in C++).
- **Stop granularity.** detectPitch asks every 128 blocks of its energy pass and before each YIN frame; detectKey
  before each frame; features before each read of 16 coarse frames (32,768 frames at 44.1 kHz); suggestParts between
  its stages. A Stop that returns true makes the function return its empty answer.

### Tests (`Tests/analysis/test_detect.cpp`, group `analysis`)

| Test | Plan row | What it checks |
|---|---|---|
| `detectFixture` | fixtures `analysis/detect.json` | every case: n, channels and rate exact; detectPitch null-ness and MIDI exact, cents within 0.1, Hz within 1e-4 relative; detectKey key, scale and r exact; suggestParts count, kinds and labels exact, start and end within 1e-3, bpm and tempoFound exact on loops (0 and false elsewhere); the case count gated against the file (FileTally) and against 34; the DC case: the plugin says no pitch while the port with `prototypeDc` gives the recorded MIDI 90 +23.3 c 1500 Hz; on the 33 other cases detectPitch with and without `prototypeDc` agree exactly; every read stays inside the sound (a checking Source wraps every input) |
| `detectionSyntheticTones` | detection: 5 pitches | C1, A1, A2, A3, C5, each at 0, +23 and -37 cents, 1 and 24 harmonics, 44.1 and 48 kHz (the prototype test's tone): all found, every error within 10 cents |
| `detectionKeyProgressions` | detection: C major | the prototype test's I-IV-V-I in C major, and also G major and A minor (a chroma rotated the wrong way would still find C major; the other two catch it); silence has no key |
| `detectionNoClearPitch` | detection: white noise | 6 seeds of white noise; silence; a 30 ms note; DC (decision 3); and A3 riding on a DC offset keeps its pitch (the rule is narrow) |
| `suggestPartsFiveMinutes` | speed | the 5-minute song built at test time; 3 runs, the slowest printed against 1.0 s and gated at 5 s in optimised builds; the runs agree; at least 3 valid parts (the prototype test's constraints) |
| `longFileMemoryAndStop` | (for V2) | on the same song, each function's peak live heap stays under half the prototype's 11 kHz copy (6.6 MB; its mono copy would be 52.9 MB); a Stop that never fires changes nothing and is asked through each analysis; a Stop that fires on its fifth question gives no pitch, no key and no parts |
| `tempoOfResampledBeats` | (extra) | the prototype test's Beat loop resampled to 92, 100, 108, 128 and 140 bpm: a found tempo within 1 bpm |
| `analysisPieces` | (extra) | fft (512) against a direct DFT; the bit reversal a permutation; overlapOk at and past half; estimateTempo on 4 onset curves against SLAnalysis._estimateTempo's values (120, 139.67, 84.72, and 120 not found), and a flat curve |

### Results (CI run 44856126113 on fc592ac, all green)

- Windows Release: 2074 checks in 358 tests, 0 failed. Linux Release: 2066 checks in 358 tests, 0 failed. Sanitizers:
  contract, model, engine and analysis 912 checks in 154 tests; state 142 checks in 20 tests. Debug CRT `--only alloc`:
  41 checks in 8 tests, 0 JUCE assertions; Debug snapshots: 0 JUCE assertions. pluginval strictness 10: SUCCESS.
  Fixture check: passed (26 files, 4,962,066 bytes). No compiler warning from the new files (MSVC /W4, GCC).
- detect.json: 34 of 34 cases within the plan's tolerances on Windows, on Linux and under the sanitizers; 33 bit for
  bit. 23 of the 34 inputs are bit-exact copies of the prototype's (the rest use the starters, within 2e-7). The DC
  case: the plugin says no clear pitch; the port with the prototype's rule gives the recorded MIDI 90 +23.3 c,
  1500 Hz. Decision 3 changes none of the other 33 cases. 41,700 reads, 0 outside the sound.
- Detection: the largest errors over 12 tones each are 0.00 (C1), 0.00 (A1), 0.00 (A2), 0.30 (A3) and 2.00 cents
  (C5), against 10. Keys: C major (r 0.877), G major (0.841), A minor (0.804). Six seeds of white noise, silence, a
  30 ms note and DC: no clear pitch. A3 on a DC offset: MIDI 69 +0.1 c.
- Speed, suggestParts on the 5-minute stereo 44.1 kHz song (target 1.0 s, gate 5 s, the slowest of 3 runs): Windows
  87.5 / 87.5 / 88.2 ms, Linux 62.9 / 63.0 / 63.1 ms; under the sanitizers 216 / 215 / 224 ms (not gated). On the
  same song detectPitch takes 57.8 ms on Windows (22.0 ms Linux) and detectKey 28.3 ms (24.4 ms). No target is
  missed.
- Memory on the same song (both platforms): peak live heap 0.23 MB in detectPitch, 0.07 MB in detectKey and 1.66 MB
  in suggestParts (the largest block 0.21 MB), against the gate of 6.6 MB (half the prototype's 13.23 MB 11 kHz copy;
  its mono copy would be 52.92 MB). Stop was asked 56, 40 and 407 times; one that fires on its fifth question ends
  each analysis with its empty answer.
- The tempo of the resampled Beat loop: 92, 100, 108, 128 and 140 bpm found exactly. fft (512) against a direct DFT:
  1.42e-13 at most (the largest bin 183.3). estimateTempo: the prototype's 120, 139.67, 84.72 and 120 (not found).
- The 5-minute song took 1279 ms to make on Windows (571 ms on Linux): the test let each starter go after its section,
  and the shared cache made it again for the next. The test now holds them.
- Run 44858939907 on 346421d (the test holding its starters and naming the inexact case), all green with the same
  totals: suggestParts 86.7 ms on Windows (the slowest of 3), 73.5 ms on Linux, 261 ms under the sanitizers;
  detectPitch 57.4 ms, detectKey 28.2 ms (Windows). The case within tolerance but not bit for bit is the Saw starter
  (its Hz, in the 16th digit).

### Hand-off for V2

- **Calling it.** Wrap the decoded sound in a `Source` and call the three functions on the SampleManager worker
  (decision 1). For float data `FloatSource` takes the channel pointers; for the int16 storage of a long file, write a
  small `Source` whose `read` converts a block to floats (the analysis asks only for blocks inside the sound: channel,
  start, count > 0, start + count <= frames). For "Detect pitch & key" on the Region, a `Source` that offsets into the
  Region's frames is enough (or a FloatSource over offset pointers).
- **Cancelling.** Pass a `Stop` that returns true once a newer request is waiting (the SampleManager's "newest wins").
  A stopped call returns no pitch, no key or no parts, so check the Stop yourself before storing a result. The
  functions keep no state between calls, so two can run at once on different threads.
- **Memory.** No full-length buffer is made: on the 5-minute stereo song suggestParts peaks at 1.66 MB live and the
  others far less (CI figures above). A 10-minute file at 48 kHz has 2.2 times the frames, so about 3.6 MB, well
  inside the long-files row's 16 MB.
- **SMPL's cache.** `Pitch` {midi, cents, hz}, `Key` {key, scale, r} and `Part` {kind, label, start, end, bpm,
  tempoFound} hold every field plan 3's cache stores; `scaleName` and `partKindName` give the prototype's strings, and
  `PartKind`'s values (steady 0, hit 1, quiet 2, loop 3) can be the cache's u8. Labels hold the part's time, so store
  them as they are. Part start and end are fractions of what was analysed (the loaded range, plan 3).
- **Times.** On 5 minutes of stereo 44.1 kHz: suggestParts 88 ms, detectPitch 58 ms, detectKey 28 ms (Windows CI). A
  10-minute 48 kHz file should take about twice that, a few tenths of a second in all, so no progress display is needed.

### For the architect

- Plan 8.2 could take a row for decision 3 (DC is "no clear pitch": a lag whose running sum is still 0 scores 1) and
  one for the three guards (suggestParts below 1 kHz, a 1-frame lag below 3 kHz, estimateTempo's refinement).
- Plan 1.1's analysis folder has a sixth file, `Detail.h` (internal: the block readers).
- Plan 1.11's sanitizer run now takes the `analysis` group as well as `contract`, `model` and `engine`.

## V2

Long songs and the suggestions (plan 7.9 V2, decisions 1 and 2): 16-bit storage for long files, the 10-minute limit
with "load a part", the exact Region edges, the analysis on the SampleManager worker with its SMPL cache, the part
chips and the suggestion line. Branch `m7-songs`, from main 1e9f61c.

### Progress

- Done: 16-bit samples in `engine::SampleData` and every read path (readAbs, hermite, PlainStretch, readReg's seam,
  seamRho); the SampleManager decodes a part longer than 3 minutes straight to 16-bit, loads a part of at most 10
  minutes (the user's pick or SMPL's range), analyses each decoded file before handing it on (a starter's parts right
  after), runs Detect, root suggestions and the part picker's overview as jobs a newer request stops.
- Done: the processor's suggestions (root, key, engine), SoundMeta (each sound's own root), part chips and "Use the
  whole file" as one undo step each, Detect pitch & key.
- Done: the strip's chips and suggestion line, the part picker, the Sound tab's Detect, Key, Scale and Scale lock.
- Done: the M7 tests (long file, part load, Region precision, root), the chip tests, the M7 snapshots; two M2 checks
  follow M7's rules. Run 44866817094 (f44fa83) built on both platforms; run 44869651538 (42425cf) stopped at one
  missing include; run 44871606929 (abdd828) failed 3 checks, all in the tests (fixed in 80c2578, below).
- Done: every M7 snapshot looked at (the chips on a song with one hovered, the suggestion line, the part picker, the
  Sound tab, dark and light, the chips at 200 %): each shows what it should.
- Done: run 44873689608 on 80c2578 is green on every job and step (totals below). Its log showed one check that could
  not fail: the Region test's control read past its render (63 float32 periods are longer than 63 f64 ones), so it
  compared nothing and passed; the render now covers that window and the control must have compared a lag.
- Last: the commit with these lines gets its own green run, then main is fast-forwarded to it.

### Files

- `Source/engine/SampleData.h`: `SampleFormat::i16`, `L16`/`R16`, `toInt16`, `kInt16Scale`, `makeSampleData16`,
  `hasSamples`, `at`, `sampleBytes`.
- `Source/engine/SourceRead.h`: `sampleValue`, `hermite` and `readAbsOf` as templates over the stored type; `readAbs`
  picks the format once per call. `Source/engine/Region.h/.cpp`: PlainStretch holds both pointer pairs and picks once
  per run; seamRho reads through `SampleData::at`. `Source/engine/Engine.cpp`: setSample asks `hasSamples`.
- `Source/doc/SampleManager.h/.cpp`: the 16-bit decode, the loaded part, the analysis (`SoundAnalysis`, `toCache`,
  `fromCache`, `keyName`, `pitchToRoot`), the jobs (`detect`, `suggestRoot`, `requestOverview`, a starter's parts),
  `JobResult`, `FileOverview`, `SoundView`'s 16-bit pointers, channels, origin and analysis, `workerProbe` (tests).
- `Source/PluginProcessor.h/.cpp`: `Suggestions`, `EngineSuggestion`, `detectPitchAndKey`, `acceptRootSuggestion`,
  `acceptKeySuggestion`, `acceptEngineSuggestion`, `dismissEngineSuggestion`, `usePart`, `useWholeFile`,
  `setRegionStep`, `regionChanged`, `currentRegion`, `deliverAnalysis`, `waitForAnalysis`, `loadSoundFilePart`,
  `requestOverview`, `getOverview`, `historySize`; SoundMeta in applyStep and deliverSoundLoads; the cache's sync.
- `Source/doc/ClipDocument.cpp`: `sameNonParam` leaves the SMPL analysis cache out.
- `Source/ui/strip/PartChips.h/.cpp` (new), `PartPicker.h/.cpp` (new, a file plan 1.1 does not list),
  `WaveStrip.h/.cpp` (the file line's words, the chip row, the hover box, the picker), `WavePeaks.h/.cpp` and
  `ui/roll/NoteDecor` (peaks of a 16-bit sound), `ui/tabs/SoundTab` (Detect, Key, Scale, Scale lock),
  `ui/MainView` (the strip's height with its chip row, the Sound tab's refresh), `PluginEditor.cpp` (a report's
  toast length).
- Tests: `Tests/plugin/SongRig.h` (WAV files written at test time chunk by chunk, a one-minute song),
  `test_long_file.cpp`, `test_part_load.cpp`, `test_region_precision.cpp`, `test_root.cpp`,
  `Tests/ui/test_part_chips.cpp`, `test_m7_snapshots.cpp`; `test_state_clip.cpp` and `test_strip.cpp` updated.

### Decisions and deviations

- **16-bit above 3 minutes (plan 8.2 #14).** A loaded part longer than 180 s is kept as 16-bit samples, made chunk by
  chunk from JUCE's floats with `toInt16` (round half away from zero of v x 32768, kept inside -32768 .. 32767, NaN
  to 0). A 16-bit file comes back to its own values; a 24-bit or float file loses what is below -96 dB. A sample s is
  read as `s * 2^-15`, which is exactly the float JUCE's 16-bit reader gives, so every read of a 16-bit sound is the
  same double the same read of a float sound holding s / 32768 gives (`sixteenBitReadsLikeFloat` checks readAbs, the
  fast path, both seams, Hold, Bounce, seamRho and the four engines bit for bit). Float sounds take the same
  expressions as before, so every parity case stays bit for bit.
- **The format is picked once per call or per run.** readAbs branches once; PlainStretch keeps both pointer pairs and
  a flag set once per run, so its per-sample read has one predictable branch. No template of the voice code was
  needed.
- **A part is at most 10 minutes, from where it starts to 10 minutes on or the end of the file.** Plan 3's state test
  loads a 12-minute file "from minute 6", which is 6 minutes, so a part is not always 10 minutes long. A project's
  range is decoded again as it is (capped at 10 minutes, only a hand-made state could ask for more).
- **The part picker** (question 9.3's "offers to load a part"): it opens by itself when the user loads a new file
  longer than 10 minutes (StrandLock loads the first 10 minutes meanwhile, so something plays at once), and from the
  strip's "Load another part..." chip. The whole file is drawn from an overview the worker reads only when the picker
  asks (a job), never at load time, so a project load never reads a whole long file twice. A part picked from the
  same file does not open the picker again.
- **When the analysis runs (decision 1).** A file's load analysis runs on the worker before the sound is handed on,
  so the load's one undo step already holds the Region U2 chooses and the SMPL cache. A starter's parts are made
  right after it is handed on (a job result), so a state load's wait for its starter (7.4 decision 3) never waits for
  them. Detect, a root suggestion for a new Region and the overview are jobs between sounds. A newer sound request
  stops a decode, an analysis or a job at its next question, and its result is dropped.
- **Decision 2 against the prototype's runDetect 'auto'.** After a load the prototype sets a short file's root at once;
  StrandLock offers it as "Use A1 as the root" (suggestions never apply themselves). "Detect pitch & key" (the button)
  sets the root (no undo step) and the key (one undo step), as the prototype's 'button' path does. The prototype's
  "same root" rule (within 20 cents of the current root: no suggestion) covers the load's suggestion too.
- **U2 inside the load's step.** The prototype puts a song's Region on a suggested part after the load and folds it
  into the load's undo step; here the part is known before the hand-over, so the load's patch carries it. With no
  part suggested, M2's 4 s from 35 % in stays.
- **The SMPL cache describes the file, not an edit.** `sameNonParam` leaves it out, so a cache that arrives later never
  makes or splits an undo step; Undo keeps the newest cache known for a file (`keepCacheOf`); a project's file that is
  analysed after its load (its cache was empty, or held what this version cannot read: a part kind above 3, an unknown
  scale, a key above 11, a number out of range) gets the cache into SMPL (`syncSound`). Detect's pitch and key replace
  the cached ones, as the prototype's S.pitch and S.key do. The key's r is not stored (SMPL has no field for it).
- **SoundMeta (plan 2.4).** Each sound's own root and cents are remembered when it leaves (whatever moved them: Detect,
  a chip, the Sound tab's Root knob, automation) and when Detect or a chip sets them; Undo or Redo that changes the
  sound writes that sound's own root and cents instead of the steps' values. A user load still sets 60 and 0 (plan
  2.4's sound load), unlike the prototype's switchSound, which gives a starter back its remembered root.
- **E3 is a chip on the strip** ("Try Cloud", or "Cloud suits this sound (on)" with the accent border; no light-bulb
  glyph, which the plugin's fonts do not have). The prototype also shows it as a line on the Sample tab; that line is
  not built here (the strip's chip does the same job).
- **The suggestion line also has "Loop cleanly"** (the prototype's trimRegion: a short file whose whole Region loops,
  the end pulled in, one undo step "Trim the Region").
- **The chips' place.** A file's chips get a row of their own under the file line (the strip grows 26 px, the roll
  gives them up); a starter's one "Parts" chip sits beside the file line, as the prototype's strip shows it.
- **A click on a chip never makes the chips again inside that click**: they are made again on the editor's next tick
  (a chip deleted during its own click would crash).
- **Toasts of the processor's own** (Detect's sentence, "Root A1 +23 cents: the notes now play this part in tune",
  the engine's why) come through `takeSoundReports` with `LoadReport::toastMs`.
- **The Sound tab's Key, Scale and Scale lock** are INST state, one undo step each with the prototype's labels ("Key",
  "Scale", "Scale lock on" / "off"); the scale names in full words (sl-ui.js scaleLabel).
- **Two M2 checks follow M7's rules**: `sampleFileRecord` (the cache is filled on the worker; a song's Region is the
  part U2 chooses) and the strip's file line (it now ends with what listening found).
- **Drag sources (decision 4).** M2's drop handling stays. Only Cubase can test these, for V4's checklist: a file from
  MediaBay, a clip from the Pool, a trimmed audio event from the project window, and a file longer than 10 minutes
  (the picker should open).

### Tests

| Test | Plan row | What it checks |
|---|---|---|
| `engine/sixteenBitReadsLikeFloat` | long files | 16-bit samples s against floats holding s / 32768: readAbs (4000 reads, in, at and past the edges), the fast path and readReg (post and pre seams, Hold, Bounce) and seamRho, bit for bit; Tape, Voice, Cloud and Lock, half a second each, bit for bit; one sample one step away renders other bits (the control). Runs under the sanitizers. |
| `songs/longFileLoadsAs16Bit` | long files | a 10-minute stereo 48 kHz 16-bit WAV made at test time: decoded on the worker (another thread, which allocated the samples), peak live on the worker at most 115.2 MB + 16 MB, the message thread under 4 MB meanwhile, the audio thread 16-bit with 115.2 MB of samples, 0 allocations and frees in the 40 blocks of the swap and a note on it, 4 x 4096 frames of each channel equal to JUCE's reader, SMPL's whole range, the cache filled. |
| `alloc/sixteenBitSwapAllocatesNothing` | long files | a 200 s 16-bit sound swapped in while a note plays: 0 allocations (also on the Debug CRT, which counts every heap call). |
| `songs/newerRequestsStopTheWorker` | decision 1 | two Detects asked while the worker is busy: only the newer runs; a Detect then a new sound: the Detect gives nothing and the sound plays; a file overtaken while the worker reads it stops at its next question and never plays. |
| `songs/partOfLongFileLoads` | part load | a 12-minute file: its first 10 minutes by itself (the toast says so); the part from minute 6 (6 minutes, to the end) as one undo step, its frames the file's; SMPL's range through its record; a project saved and reloaded: the same range, every frame, the Region and the part chips the same, read from the cache, the state re-saved byte-identically; Undo brings the first part back; a part from minute 1 is 10 minutes. |
| `songs/partPickerLoadsThePart` | part load | the picker opens by itself, draws the overview, moves to minute 6 ("6:00 to 12:00 of 12:00"), loads that part, closes, and the file line names the part. |
| `songs/regionPrecisionOneBarPart` | Region precision | a one-bar part (2 s) at about 80 % of a 10-minute 48 kHz file, set the part chip's way (one undo step, exact edges); a live Tape note at the root: passes 2, 3, 9, 33 and 64 repeat pass 1 exactly k periods on (the best lag within 8 frames is 0, the largest difference under 1e-6: the voice's tone low-pass is an IIR filter, so the repeat is measured), where float32 edges would be at least 2 frames a pass off (the control finds no repeat near 63 float32 periods); host automation moves the start: readInst plays the parameter's value, the end keeps its exact edge, and the loop follows the new length. |
| `songs/rootSuggestionsAndDetect` | root | a tone at A1 +23 cents loads at C3 with a root suggestion; accepting it, and Detect, set root A1 +23 cents with 0 undo steps; the suggested key A major is 1 undo step; Detect on a built-in sound does nothing; Undo of a sound change brings the tone back with its own root and cents, Redo the Saw at C3. |
| `songs/analysisCacheReloadsAsSaved` | root | white noise: no clear pitch, no key; the cache says hasPitch 0, hasKey 0; a reload reads them as such and analyses nothing, and the strip says "no clear pitch"; the same project with its cache emptied is analysed again (the control). |
| `ui/partChipsSetTheRegion` | decision 2 | a one-minute song opens on pickPart's part inside its load's step; every part has a chip, the Region's lit; a chip sets the Region in one undo step; a root suggestion only for its Region; Use the whole file is one step; Undo; a hovered chip's hint. |
| `ui/suggestionLineDoesWhatItSays` | decision 2 | a short tone: "Use A1 as the root" (no step), "Use key A major" (one step, the Sound tab shows A), "Loop cleanly" (one step), "Voice suits this sound (on)" (hides); white noise: "Try Cloud" (one step), no key offered. |
| `ui/starterPartsChip` | D10 | the Saw's parts behind one "Parts" chip beside the file line; its menu's first part is one undo step and the chip then names it. |
| `ui/m7Pictures` and the `m7_*` snapshots | snapshots | the chips on a song with one hovered, the suggestion line, the part picker, the Sound tab; dark and light (the chips at 200 % too). |

### Results (run 44873689608 on 80c2578, all green; the M7 figures from it and from run 44871606929)

- Windows Release: 2169 checks in 371 tests, 0 failed (songs 7 tests, 55 checks; ui 134, 653; alloc 9, 43). Linux
  Release: 2161 checks in 371 tests, 0 failed. Sanitizers: contract, model, engine and analysis 920 checks in 155 tests
  (`sixteenBitReadsLikeFloat` among them); state 142 checks in 20 tests. Debug CRT `--only alloc`: 44 checks in 9 tests,
  0 JUCE assertions; Debug snapshots: 0 JUCE assertions. pluginval strictness 10: SUCCESS. Fixture check: passed (26
  files, 4,962,066 bytes). No new compiler warning from V2's files (the float-equal one in SampleData.h is M2's
  makeSampleData, moved down the file).
- 16-bit reads: 4000 readAbs reads and 15,000 fast-path, seam and seamRho positions (6358 on the fast path), 0 differ;
  Tape, Voice, Cloud and Lock bit for bit from 16-bit and from float (peaks 0.74 to 0.90); the nudged sample differs.
- Long files: the 10-minute stereo 48 kHz WAV (115,200,104 bytes, written in 1.1 s) loads as 16-bit: peak live on the
  worker 118.94 MB on Windows, 118.83 MB on Linux, against 131.20 MB (115.2 MB + 16 MB; the largest block 57.6 MB, one
  channel); the message thread 0.000 MB meanwhile; hash, decode and analysis 1170 ms on Windows (the analysis 316 ms),
  1167 ms on Linux (365 ms). 0 allocations and frees on the audio thread in the 40 blocks of the swap (Windows: operator
  new and delete; Linux: malloc too), and 0 in the 30 blocks of the 200 s swap on the Debug CRT (every heap call).
- Region precision: the part 0.803333... (one bar at 120 bpm) is 95,999 frames long in f64 and would be 96,001 with
  float32 edges (126 frames off after 64 passes); passes 2, 3, 9, 33 and 64 repeat pass 1 at exactly k f64 periods
  (drift 0 frames, the largest difference 0: bit for bit). Host automation of the start to 0.0004 earlier: the
  parameter's value plays, the loop is 107,519 frames a pass and repeats on it (drift 0). In both runs the control at 63
  float32 periods read past the render and compared nothing; the last commit renders far enough (its run's figure is
  the first real one).
- Root: the tone at A1 +23 cents is suggested as MIDI 45 +23; accepting it and Detect push 0 undo steps; the suggested
  key A major is 1 step ("Key from the sound: A major"); Undo of the Saw brings the tone back at MIDI 45, +23 cents.
  White noise: no clear pitch, no key, and its reload reads both from the cache with no new analysis.
- The one-minute song gets 4 parts (Steady note 0:10, Big hit 0:21, Quiet texture 0:14, One-bar loop 120 bpm 0:29)
  and opens on the steady note inside its load's one undo step; the Saw that opens up gets 2 parts behind its "Parts"
  chip; white noise offers "One-bar loop 120 bpm (guess) 0:00", "Try Cloud" and "Loop cleanly", and no key.

### Hand-off for V3

- **Embedding the loaded part.** SAUD encoding 2 (16-bit PCM interleaved, the loaded range) can be written straight
  from a 16-bit sound's `L16`/`R16` (`SoundView::left16`, `format`). A float sound (3 minutes or less) needs its own
  conversion. Encoding 0 (the original bytes) reloads through the same part rule: `decodeFile` takes the project's
  `SampleFile` (`expected`) and decodes its `loadedStartFrame` / `loadedFrames` again; it reads from a `juce::File`
  today, so decoding the embedded bytes needs it to take an input stream (or a temporary file).
- **The cache.** Every committed file sound carries its analysis in `SoundView::analysis` and `ref.file.cache`. A
  project load with a readable cache is not analysed again; one without is analysed before it is committed, and the
  processor's `syncSound` puts the cache into the document's SMPL. Keep that when the sound comes from the library or
  the embedded bytes.
- **Measuring memory on the worker.** `SampleManager::workerProbe` runs on the worker around each sound request; an
  `alloc::Scope` made there counts what the worker allocates (see `longFileLoadsAs16Bit`).
- **Files of the tests.** `Tests/plugin/SongRig.h` writes WAV files chunk by chunk at test time and reads frames back
  as JUCE's reader gives them.

### For the architect

- Plan 1.1's `ui/strip` folder has a fifth file, `PartPicker` (the part picker).
- Plan 3 "The loaded range": a part runs from where it starts to 10 minutes on or the end of the file, so it can be
  shorter than 10 minutes (its own state test loads 6 minutes from minute 6 of a 12-minute file).
- Plan 8.2 could take rows for: the short file's root offered as a chip instead of set at once (decision 2 against
  runDetect 'auto'); a song's first Region inside the load's own undo step (U2, known before the hand-over); the
  16-bit rounding (round half away from zero of v x 32768).
- E3's line on the Sample tab (the prototype shows the suggestion there as well as on the strip) is not built.

## V3

Keeping the sound with the project (plan 7.9 V3, section 3 "Embedding", "The loaded range" and "Finding the sound on
load", decisions 4 and 5): the SAUD chunk, "Keep the sound in the project" with the 20-50 MB question and its saved
answer, the Region excerpt (FLAC 24-bit, never over 50 MB), the opt-in library and Locate. Branch `m7-songs`, from main
9f8faa3.

### Progress

- Done: SAUD in `state/StateChunks` (read in place, kept whole) and `state/StateFormat` (an optional written tag; a
  container made in one buffer; a SAUD written again from its payload, not held twice).
- Done: `doc/SampleManager`: a file of up to 20 MB read into memory once (hashed and decoded from there, its bytes kept
  as a ready SAUD payload); the embed jobs (the original bytes read again, or a FLAC 24-bit Region excerpt); the library
  (lookup by SHA-256, copies, its size); the locate order (path, library, the project's SAUD, else missing); Locate.
- Done: the processor's policy (`syncEmbed`), the question, the switch, the library switch, Locate, the sound notice;
  `ParamBridge::readInst` maps the Region onto an excerpt; the strip draws and drags the Region on one.
- Done: the UI: `ui/SoundBanner` (the question, Locate, a part kept) under the top bar; the Sound tab's switch, what is
  kept, and the library line.
- Done: run 44927526903 on f2a61a6 (the code, the two M2 checks updated, no new test yet) built on both platforms;
  every earlier test passed on Windows and Linux and under the sanitizers.
- Done: runs 44929619697 (1591391, the tests and pictures), 44930610865 (4993c37, the limits case and the load's
  allocations) and 44931859255 (18859c9, the library line) green on every job and step. Every V3 picture looked at
  (the Locate banner, the question, a part kept with the strip on the excerpt, the Sound tab's rows; dark and light):
  each shows what it should. Before 18859c9 the library line read "the library holds not measured yet".
- Last: the commit with these lines (it also makes three test messages print the size of a file written inside a
  check's call, which MSVC printed as 0 bytes) gets its own green run, then main is fast-forwarded to it.

### Files

- `Source/state/StateChunks.h/.cpp`: `SaudHeader`, `SaudView` (read in place), `decodeSaud`, `writeSaudHeader`,
  `patchSaudHeader`, `encodeSaud`, `kSaudOriginal`/`kSaudFlac24`/`kSaudPcm16`, `kSaudHeaderBytes` (37); `sameBytes` (the
  same file's bytes: size and SHA-256).
- `Source/state/StateFormat.h/.cpp` (M0's file; plan 7.9 lets V3 touch it): SAUD in `kWrittenTags` after SMPL,
  `isOptionalTag` (a null payload writes no chunk), a container made in one buffer (`beginContainer`/`endContainer`), and
  a decoded SAUD written again from the processor's payload (`reframe`) instead of keeping a raw copy of up to 50 MB.
- `Source/state/UserSettings.h/.cpp`: `libraryOn` / `setLibraryOn` (the person's switch, off by default).
- `Source/engine/SampleData.h`: `ExcerptMap` (base, span) on `SampleData::excerpt`.
- `Source/params/ParamBridge.h/.cpp`: `readInst (refs, state, playing)` maps the Region onto an excerpt and keeps it inside.
- `Source/doc/SampleManager.h/.cpp`: `SoundSource`; `SoundView::original`, `excerpt`, `toSound`, `toLoaded`,
  `fileFrameOf`; `EmbedResult`; `loadFile`/`loadRef` take `keepOriginal` (and `loadRef` the project's SAUD); `locate`;
  `embedOriginal`, `embedExcerpt`, `takeEmbedResults`; the library (`defaultLibraryFolder`, `setLibraryOn`,
  `libraryFileFor`, `copyToLibrary`, `libraryBytes`, `libraryCopies`); `decodeFile` split into the hash/read and
  `decodeReader`; `decodeEmbedded`; `readerForBytes`; `runEmbed`, `runLibrary`.
- `Source/PluginProcessor.h/.cpp`: `get/setKeepInProject`, `EmbedAnswer`, `answerEmbedQuestion`, `is/setLibraryOn`,
  `getLibraryBytes`, `SoundNotice`/`getSoundNotice`, `locateSound`, `getEmbedStatus`, `getEmbeddedPayload`,
  `waitForEmbed`, `readInstFor`; the policy (`syncEmbed`, `deliverEmbed`, `askEmbed`, `setEmbedded`, `saudFor`,
  `keepAnswerOf`, `rememberAnswer`); SAUD read in `loadState`, written in `currentPayloads`; `getStateInformation` waits
  (at most 5 s) for a Region excerpt on its way; the excerpt map in `processBlock`.
- `Source/doc/ClipDocument.cpp`: `sameNonParam` leaves the switch and the answer out, as it does the cache.
- `Source/ui/SoundBanner.h/.cpp` (new, a file plan 1.1 does not list), `ui/MainView` (the line under the banner),
  `ui/tabs/SoundTab` (the keep row and the library row), `ui/strip/WaveStrip` (the Region on an excerpt),
  `ui/strip/PartChips` ("Load another part..." only for a file read from its path).
- Tests: `Tests/plugin/test_embed.cpp`, `test_locate.cpp`, `Tests/ui/test_m7_embed_pictures.cpp` (new);
  `test_state_clip.cpp` (`sampleFileRecord`) and `Tests/ui/test_strip.cpp` (a project whose file is gone) save with the
  switch off, so their missing-file checks still test a project that kept nothing.

### Decisions and deviations

- **What is kept (decision 5).** Switch on: a file of up to 20,000,000 bytes is kept whole (SAUD encoding 0) without
  asking; from there to 49,999,963 bytes (50,000,000 with SAUD's 37-byte header) StrandLock asks once; above, the Region
  and up to 1 s either side (encoding 1, FLAC 24-bit). MB here are 1,000,000 bytes, as plan 3's 50 MB limit is.
- **Until the question is answered, the Region is kept** (as above 50 MB). Plan 3 does not say what a save holds before
  the answer; keeping nothing would lose the sound if the file moved before Kalan answered, and the whole file is what
  the question asks about. "No" keeps nothing; "Just the Region" the excerpt; "Yes" the file.
- **Answers and the switch are no undo steps.** They say how the project holds the sound, not what plays (like the
  analysis cache, `sameNonParam` leaves them out). Undo and Redo keep each file's newest answer (by its bytes) and the
  project's switch as it is now. A file loaded again (the same bytes) keeps the answer it was given in this session.
- **The switch lives in SMPL's file record**, so a project on a built-in sound has no place for it: such a project
  opens with the switch on (the default). Appending a field to the starter record would be a format change of its own.
- **A file is read into memory only up to 20 MB** (the size kept without asking): it is hashed and decoded from those
  bytes, which then are the SAUD payload (no second read, no copy at save). A 20-50 MB file is read again, on the
  worker, only once Kalan says Yes; a file bigger than that never is.
- **Bytes in memory decode as the file does**: `readerForBytes` tries the formats whose extensions the file's name has,
  in the order `createReaderFor (File)` tries them, so a sound from memory, the library or the project is bit for bit the
  sound from disk.
- **The Region excerpt is made from the sound as it plays** (`SoundView`'s samples), each sample rounded to the nearest
  24-bit step: a 16-bit sound keeps every bit (s x 256), a float sound is within 2^-24. For a loaded part longer than 3
  minutes the plugin already holds 16-bit samples, so its excerpt is those. Its FLAC rate field is the sound's rate when
  libFLAC's streamable subset takes it (a whole number, a multiple of 10 above 65,535 Hz), else 48 kHz; SAUD's own f64
  rate is the one read back.
- **The excerpt follows the Region.** It is made again when it no longer covers the Region or is more than twice the
  length now needed, once the Region has stayed put for 300 ms (a drag moves it many times), and at once before a save:
  `getStateInformation` waits for an excerpt on its way, at most 5 s. Over 50 MB (the encoder stops as soon as the
  output passes it) nothing is kept, the Sound tab says so and a toast says it once per Region.
- **SAUD encoding 2 is read, never written.** Plan 3's embedding rules only ever call for the file's bytes or the Region
  excerpt. When the file cannot be read again for a Yes (moved after the load), the sound as it plays is kept instead,
  as FLAC 24-bit of the whole loaded part (lossless for 16- and 24-bit files under 3 minutes); it reloads as the whole
  sound, not an excerpt.
- **The project's own SAUD stays as it was while its sound is the project's**: written back byte for byte, so a project
  re-saves byte-identically. A SAUD this version cannot play (an encoding it does not know) is kept that way, with no
  banner; a damaged one (bad CRC) too, with the banner; both go once another sound is loaded. A SAUD and its SMPL file
  record go together: a copy whose bytes do not hash to SMPL's SHA-256 is refused.
- **The library** is `Documents\StrandLock\Samples\<first 16 hex digits of the SHA-256>.<extension>` (a folder of its own
  per test run in the test runner). Off unless turned on (UserSettings, per person); on, the file playing now and every
  file loaded after are copied on the worker (from the bytes in memory, or from the path hashed while copied, through a
  `.part` file renamed when whole), once each. A library file is used only when its SHA-256 is the project's. The Sound
  tab shows the library's size, on or off: measured on the worker once when the tab first needs it and after each copy.
- **`jobsRun ()` still counts only Detect and root-suggestion jobs** (as its comment says); embed and library jobs are
  jobs for `waitJobs` but not runs, so V2's job counts stay what they were.
- **Locate** loads the chosen file with no patch: the Region (and its exact edges), the root, every parameter and the
  part loaded stay. The same file (its SHA-256) keeps the project's record and analysis with the new path; another file
  is a new record (analysed, asked about again) that keeps the project's part when it is long enough, and the toast says
  it may sound different. It is one undo step "Locate <name>", so Undo goes back to the missing file and its banner.
  "Locate sample..." also shows when only a Region excerpt was kept, to bring back the whole file.
- **An excerpt on the strip**: the strip shows the excerpt's samples and draws and drags the Region where it falls on
  them (`toSound` / `toLoaded`); the part chips, Detect and root suggestions map the Region the same way. "Load another
  part..." and the part picker need the file at its path, so they do not show for a copy.
- **Undo to a sound only its SAUD holds**: the processor remembers the SAUDs of the last two files it held, so an Undo
  back to a sound whose file is gone still finds it.
- **Decision 4 (drag sources).** M2's drop handling stays as it is. V2's list (a file from MediaBay, a clip from the Pool,
  a trimmed audio event, a file longer than 10 minutes) is still the one only Cubase can test; for V4's checklist V3 adds:
  a project saved with a small file reopened after the file is moved (it plays from the project, no banner); a 20-50 MB
  file (the question once, and not again after saving and reopening); a file moved with Keep off (the Locate banner, and
  Locate sample... brings it back with every setting); the library switch (a moved file found there).

### Tests

| Test | Plan row | What it checks |
|---|---|---|
| `state/saudChunkRecord` | embedding | the SAUD record's fields read back in place; a newer version's trailing bytes kept; an unknown encoding reads (kept, never played); every prefix shorter than the payload and a byteLength past its end refused; a container writes SAUD only when there is one, after SMPL, byte for byte |
| `state/embeddedWavRoundTrip` | embedding: a WAV made at test time | a 2.1 MB stereo 16-bit TestSignals WAV (seeds 7 and 9) kept whole, the bytes identical, the header's channels, rate and range; the file moved: it plays from the project, every sample bit for bit, the toast says where, no banner, re-saved byte-identically; a copy with one byte changed is refused (its SHA-256) and the Locate banner shows (the control) |
| `state/embedForwardCompatibility` | kept unreadable blobs and forward compatibility with SAUD | an unknown encoding: kept byte for byte with no banner, still written after a parameter edit, gone after another sound; 8 trailing bytes: plays and re-saves byte-identically; a bad CRC: kept with the banner, both gone after another sound; formatVersion 2 with a SAUD: kept whole with the banner; 2000 seeded mutations (half with CRCs made anew): no exception, every parameter in range, every save sound |
| `songs/embedAnswerSavedOnce` | embedding: the saved answer | a 21.2 MB file asks (the text names it and its size); until answered the Region is kept; No: no SAUD, SMPL embedAnswer 2, no undo step, no second answer; save and load: no second question; another file: asked again; Yes: the whole file kept, bytes identical; Undo and Redo keep each file's answer; the switch off: no question, no SAUD |
| `songs/embedFortyFiveMegabytes` | embedding: one 45 MB case | 44.9 MB kept whole after Yes; with the file gone it loads from the project, every sample the same; save and load timed (target 2 s, gate 10 s); the load's allocations inside plan 3's limits (51 MiB, 2 x the blob + 64 MiB) and the SAUD held once (1.25 x the blob) |
| `songs/regionExcerptWithOriginalMissing` | embedding: a Region excerpt, FLAC 24-bit within 2^-23 | a 25 MB float WAV, Just the Region on 0:10 .. 0:14: SAUD encoding 1 covers it with up to 1 s either side; decoded apart from the plugin it is within 2^-23 of the original floats (and differs: real rounding), one frame off it is far off (the control); with the file gone the excerpt plays bit for bit, the banner says part was not saved; the Region maps onto the excerpt, automation before it plays its first edge and after it its last; for another sample the Region is the parameters' own (the control) |
| `songs/noExcerptOverFiftyMegabytes` | embedding: no excerpt above 50 MB | a 52 MB white-noise file (no question): the whole file as the Region is not kept, the toast and the Sound tab say so; a 2 s Region of it is kept (the control) |
| `songs/largestClipWithFiftyMegabyteEmbed` | state limits with M7 | the largest editable clip (4096 notes, one byte under 32 MiB) and a 49.99 MB file kept whole save together inside kMaxTotalBytes and load in another instance, re-saved byte-identically |
| `songs/partLoadedFileReloadsEverywhere` | embedding: the part-loaded 12-minute file | loaded from minute 6, the Region on a part chip, the library on: the SAUD header names the part; reloaded from its path, from the library (moved, on) and from the project (off): every frame, the Region's bits and the chips the same (from the cache), re-saved byte-identically; with neither copy it is missing (the control) |
| `songs/locateOrder` | locate order | the library (on) before the project's copy, the project's copy before the banner; the banner keeps every setting (every parameter, the exact edges, the SMPL record: re-saved byte-identically); a library that is off is not looked in (the control); in the editor, Locate sample... with the moved file plays it with every setting, one undo step "Locate a tone.wav", the banner goes; Undo brings the banner back |
| `songs/libraryCopiesByHash` | the library | off by default, nothing copied; on: the file playing copied as its first 16 hex digits + ".wav", its size measured; the same file not copied twice; another file copied; the Sound tab's line shows the size |
| `songs/locateAnotherFile` | Locate | another file: plays with every setting kept, its own record and analysis, the toast says it may differ; the project's switch stays, the new file has no answer yet |
| `ui/m7EmbedPictures` and the `m7_locate_*`, `m7_question_*`, `m7_part_kept_*`, `m7_keep_row_*` snapshots | (pictures) | the Locate banner, the question with Yes / No / Just the Region in that order, a part kept (the strip shows the excerpt), the Sound tab's keep and library rows; dark and light |

### Results (run 44931859255 on 18859c9, all green)

- Windows Release: 2275 checks in 384 tests, 0 failed (songs 16 tests, 126 checks; state 23, 168; ui 135, 661). Linux
  Release: 2267 checks in 384 tests, 0 failed. Sanitizers: contract, model, engine and analysis 920 checks in 155 tests;
  state 169 checks in 23 tests (the three V3 state tests under AddressSanitizer and UBSan). Debug CRT `--only alloc`: 44
  checks in 9 tests, 0 JUCE assertions; Debug snapshots: 0 JUCE assertions. pluginval strictness 10: SUCCESS. Fixture
  check: passed (26 files, 4,962,066 bytes). No compiler warning from V3's files (GCC's float-equal warning in
  SampleData.h is M2's makeSampleData, moved down the file again, by ExcerptMap).
- A 2,099,264-byte WAV kept whole: its bytes identical in SAUD; with the file gone every sample bit for bit, re-saved
  byte-identically; one changed byte refused.
- The 45 MB case (44,928,141-byte SAUD): saved in 99 ms and loaded, its sound decoded from the project, in 291 ms on
  Windows (186 and 432 ms on Linux), against a target of 2 s (gate 10 s). The load's largest allocation 44.9 MB (limit
  51 MiB) and 45.0 MB live at most for a 45.0 MB project (plan 3's limit is 2 x the blob + 64 MiB): the SAUD is held once.
- The Region excerpt: frames 432,000 + 288,000 for the Region 480,000 .. 672,000; within 5.96e-08 (2^-24) of the original
  floats, against 2^-23 = 1.19e-07; one frame off, 0.111. With the file gone the Region plays as 0.166667 .. 0.833333 of
  the excerpt; automation before it plays 0 .. 0, after it 1 .. 1.
- No excerpt over 50 MB: the whole 52,032,104-byte noise file as the Region is refused (the encoder stops at 50 MB);
  a 2 s Region of it is kept in 768,689 bytes (192,000 frames).
- The limits case: the largest clip and a 49,996,007-byte SAUD in a 83,551,596-byte project (the limit 87,748,736),
  loaded in another instance and re-saved byte-identically.
- The part-loaded 12-minute file from its path, the library and the project: the same frames, Region bits and 2 chips,
  re-saved byte-identically each time. The library: 569793c776a5bfc3.wav, 44,204 bytes measured, then 110,458 with a
  second file. 2000 mutations of a project with SAUD: 0 exceptions, 0 parameters out of range, 0 unsound saves.

### Hand-off for V4

- **Sounds from a copy.** A sound found in the library or in the project (`SoundView::source`) comes through
  `fileSound` like any file, so the octave levels V4 builds after a decode apply to it too. A Region excerpt is an
  ordinary `SampleData` with `excerpt.active`: `ParamBridge::readInst (refs, state, playing)` maps the Region onto it
  before the engine sees it, so the level choice needs nothing more.
- **Memory.** A file of up to 20 MB is held twice in memory while it plays: its samples, and its bytes ready to save
  (`SoundView::original`). The octave levels' budget (the fast-reads row) is of the samples, so the bytes are outside it.
- **Saves wait.** `getStateInformation` waits (at most 5 s) for a Region excerpt the worker is making; a save never waits
  otherwise. `waitForEmbed` and `getEmbeddedPayload` are there for tests.
- **For the Cubase checklist (M7 section)**, besides V2's list: reopen a project after moving its small file (it plays
  from the project, the toast says so); load a 20-50 MB file (the question once; save, reopen: not asked again); with
  Keep the sound in the project off, move the file (the Locate banner; Locate sample... brings it back with every
  setting); turn the library on in the Sound tab, move a file, reopen (it plays from the library); a song over 50 MB
  with a long Region (the toast says it is too long to keep).

### For the architect

- Plan 1.1's `ui` folder has a new file, `SoundBanner` (the sound line: the question, Locate, a part kept).
- Plan 3 could say what a save holds before the 20-50 MB question is answered (V3: the Region, as above 50 MB), that
  MB there is 1,000,000 bytes (the largest file kept whole is 49,999,963 bytes, with SAUD's 37-byte header), and that
  encoding 2 is read but never written by v1.
- Plan 3's switch has no field in a starter's SMPL record, so a project on a built-in sound opens with it on.
- Plan 3's limits row ("with M7, plus a 50 MB embed") runs in the `songs` group (Release, both platforms), not `state`:
  the case holds about a gigabyte at its peak, too much for the sanitizer job.
- M0's `state/StateFormat` changed (plan 7.9 lets V3 touch M2-M6 files; this one is M0's): SAUD is a written but optional
  tag, a container is made in one buffer, and a decoded SAUD is written again from its payload rather than kept twice.
- Plan 8.2 #17 (embedding, library, Locate) is now built.

## V4

Fast reads without aliasing (plan 7.9 V4, decision 6, question 9.6), M6's two leftovers (decision 7: the raw audition,
Preview on the Diagnostics page) and the M7 part of the Cubase checklist. Branch `m7-songs`, from main d1e2c9a.

### Progress

- Done: the design, tried first in node on the prototype engine with the levels patched into a copy of it (scratch
  `m7v4/sim-engine.js`, `sim-design.js`, `sim-sweep.js`): the decimation filter, the level read and the level rule.
- Done: `engine/OctaveLevels` (the levels, their read, the rule), the level choice in VoiceTick, runTapeLevels, the
  grains' and Lock's level reads, `Engine::setOctaveLevels` (the tests' switch), `Stats::levelReads` and `levelUses`;
  the SampleManager builds the levels after every decode and keeps a starter's with it; the raw audition
  (`Engine::rawAuditionOn` / `Off`, two CommandQueue commands, the strip's press outside the Region, its readout and its
  moving line); Preview's line on the Diagnostics page and in Copy report.
- Done: run 44941857786 on 3b462da (the code, no new test yet) built on both platforms; every test passed but V2's
  long-file budget (219.7 MB against the old 131.2 MB), which the tests commit moves to the new budget.
- Done: the tests (below), the Cubase checklist's M7 section, these notes.
- Done: run 44943723393 on bf7e80d did not build: the tests commit left out `EngineRender.h`'s `FixtureOptions`.
- Done: run 44978133608 on 140c271 (that header, a DiagModel parameter renamed for -Wshadow, the raw audition's
  pictures `strip_raw_audition_dark` / `_light`) is green on every job and step (figures below). Every V4 picture looked
  at: the raw audition (its line and readout, dark and light), the Diagnostics page with its Preview row (dark and
  light), and the playing strip (heads, no raw line).
- Done: the Cubase checklist's M7 steps follow the order of the task (moving the file before the 20-50 MB question),
  name the controls as the build shows them, and say plainly when the over-50 MB message comes.
- Last: the commit with these lines (it also prints the plain-words percentages with four decimals) gets its own
  green run, then main is fast-forwarded to it.

### Files

- `Source/engine/OctaveLevels.h/.cpp` (new; plan 1.1 does not list them): `levelFor` (the rule), `readLevel` (the
  8-tap windowed-sinc read of a level), `buildOctaveLevels` / `attachOctaveLevels` / `addOctaveLevels` (the builder,
  with a stop question), `LevelSet`; the read kernel and the decimation filter, built at static-init time.
- `Source/engine/SampleData.h`: `OctaveLevel`, `kMaxOctaveLevels`, `SampleData::numLevels`, `levels`, `levelStorage`,
  `levelBytes`.
- `Source/engine/Region.h`: `readRegLevel` (readReg line for line, every read from a level). `readReg`, `PlainStretch`
  and SourceRead.h are unchanged.
- `Source/engine/Head.h` (`lv`, `lvPrev`, `gL`), `Voice.h` (`VoiceContext::levels`, `levelReads`, `levelUses`,
  `countLevel`; `runTapeLevels`), `VoiceTick.cpp` (the level each Tape or Lock head reads, per tick), `VoiceRender.cpp`
  (`runTapeLevels`), `VoiceGrains.cpp` (a level per grain), `VoiceLock.cpp` (the taps' level, and lockRho's).
- `Source/engine/Engine.h/.cpp`, `EngineDisplay.cpp`, `EngineConfig.h`, `DisplayFrame.h`, and `EngineRaw.cpp` (new; plan
  1.1 does not list it): `setOctaveLevels`, `Stats::levelReads` / `levelUses`, the raw audition (`rawAuditionOn`,
  `rawAuditionOff`, four `RawPlayer`s, `renderRaw` after the output stage), `DisplayFrame::rawOn` / `rawPos`.
- `Source/doc/SampleManager.h/.cpp`: the levels after `fileSound` (stoppable, before the analysis), and a starter's
  levels kept in its cache entry; `Source/doc/CommandQueue.h`: `rawAuditionOn`, `rawAuditionOff`.
- `Source/PluginProcessor.cpp`: the two commands; Preview's state to the diagnostics each block.
- `Source/diag/DiagModel.h/.cpp` (`audioPreview`, `previewText`, the Snapshot fields, the report line),
  `Source/ui/DiagnosticsPage.cpp` (the Preview row), `Source/ui/strip/WaveStrip.h/.cpp` (the raw audition on a press
  outside the Region, its readout, its line).
- Tests: `Tests/engine/test_aliasing.cpp` (rewritten: the M3 figure is now gated), `Tests/engine/test_raw_audition.cpp`
  (new), `Tests/engine/EngineRender.h/.cpp` (`FixtureOptions`), `Tests/plugin/test_long_file.cpp` (the new budget and
  the levels), `Tests/plugin/test_diag.cpp` (the Preview line), `Tests/ui/test_strip.cpp` (an M2 check follows decision
  7a; the raw audition's line and its two pictures). `docs/CUBASE-CHECKLIST.md` (the M7 section, and one sentence in its introduction).

### Decisions and deviations

- **The level rule is "the lowest level at which the read is at most 1.25x", not floor (log2 rate).** With the
  plan's floor, a read between 1x and 2x reads the sound itself, and a level L read between 2^L and 2^(L+1) folds the
  top of that level's band. The plan's own row cannot pass then: in a 48 kHz session the 44.1 kHz Saw one octave up
  is a read at 1.84x, floor gives the sound itself, and that is M3's -27.8 dB; at 96 kHz +24 st (1.84x) the same. So
  each level holds nothing above 0.4 of its own rate (below), and the rule takes the lowest level where the read is at
  most 1.25x (0.4 x 1.25 = 0.5: nothing folds): level 0 up to 1.25x, level 1 up to 2.5x, level 2 up to 5x, level 3
  above. A read at or below 1.25x never touches a level, a wider promise than the plan's "at or below 1x", so a 48 kHz
  song in a 44.1 kHz session (1.09x at its own pitch) keeps its whole band and plays bit for bit as in M3. The price
  of octave levels with no aliasing: just past each change (1.25x, 2.5x, 5x) the output's band is half the Nyquist
  (11 kHz at 44.1 kHz), rising to all of it just before the next change. In node on the prototype engine with these
  levels, the floor rule gave -27.8 / -23.4 / -20.3 dB at +12 / +24 / +36 st at 48 kHz (M3: -27.8 / -19.6 / -15.1)
  and -35.9 / -32.3 dB at +24 / +36 st at 96 kHz: clean only at 44.1 kHz, where those reads are exactly 2x, 4x and
  8x. This rule, with the read below, gave -54.3 / -51.2 / -48.2 dB at 48 kHz and nothing above -48.2 dB in the nine
  cases.
- **A level is read with an 8-tap Kaiser-windowed sinc (beta 5, 512 fractions, the nearest taken), not the 4-point
  Hermite.** Read slower than its own rate (the 44.1 kHz Saw at 48 kHz: a level read at 0.92x), the Hermite leaves
  images of the upper part of a level's band at -8 to -30 dB, and a level's band is full of the note's own strong
  harmonics (level 3 of the Saw holds its harmonics 1 to 8), so in node the Hermite gave -39.1 / -36.0 / -32.8 dB at
  48 kHz, over the -40 the plan allows. A 6-point Lagrange gave -45.6 / -42.5 / -39.3, a 6-tap sinc -42.7 at worst,
  the 8-tap sinc -48.2 at worst; 256 or 1024 fractions changed nothing. The read sums its taps in pairs (no older read
  to match). Reads of the sound itself keep the Hermite, so M3's reads are untouched.
- **The decimation filter is a 75-tap Kaiser-windowed sinc (cut-off 0.225 of the rate, 60 dB, beta 5.65): flat to a
  fifth of the rate, at least 60 dB down from a quarter (the new half rate).** Not a half-band filter in the DSP sense:
  that one's transition is centred on the new half rate and folds what lies just above it (only 6 dB down at the
  edge), which on the Saw's level 3 alone was enough to miss -40 dB. "Half-band low-passed" in the plan's sense:
  everything above the new half rate taken out. The edges are held (the last frame repeats), as the reads hold them.
  Each output sums its taps in a fixed order, one tap at a time over a block of 4096 outputs, so the compiler can
  use its vector registers. 70 and 80 dB designs changed no figure in node.
- **The crossfade.** A Tape head's level is chosen per tick from its speed; when it changes, each read of that tick is
  the two levels' reads crossfaded with the weight (ph + j + 1) / 32, as the head gains ramp, so the tick ends on the
  new level alone. A Lock head reads at the rate ratio, which never changes for a voice, so Lock needs no fade (its
  level is chosen per tick the same way). A Voice or Cloud grain keeps the level its own read speed chose when it
  started; the grain windows crossfade one level into the next. Both levels of a fade are the same sound at the same
  frame (level L's frame j is the sound's frame j x 2^L), so a plain line fades them.
- **The plan's Voice sweep reads one level all along.** A Voice note's grains read at the formant's speed (the pitch
  sets their spacing, not their speed), so C3 -> C6 with the formant at +12 st reads level 1 throughout at 44.1 and
  48 kHz (the sound itself at 96 kHz): there is no level change to measure. The test checks exactly that, and adds two
  Voice sweeps that do change level: the formant raised from 0 to +12 st, and the formant at +12 st with the Tape
  switch and Speed 1 -> 4.
- **The levels are built on the worker after the decode, before the analysis and the hand-over** (so a sound never
  arrives without them and no second swap is needed), for every file, library copy, project copy (SAUD) and Region
  excerpt, through `fileSound`'s caller; a newer request stops the build at its next block. A starter's levels are
  built once and kept with it in the cache of 4. They take 7/8 of the sound's own bytes (n/2 + n/4 + n/8 frames).
- **The raw audition (decision 7a)** plays after the engine's output stage, at the prototype's 0.7, so outGain and the
  limiter leave it alone (the prototype's buffer source goes straight to its master), and the bypass fade applies. It
  starts at the frame floor (pos x n) of the sound that plays, reads at the sound's rate over the session's (a read
  above 1.25x reads a level, like every read), rises from 0 over 12 ms and falls from where it is over 30 ms. A second
  hold fades the first out (the prototype's stopAudition before rawAuditionStart); four players, so quick holds never
  allocate. A sound swap fades it on the sound it started on, whose epoch it keeps until the fade ends (as a stolen
  voice, 8.2 #23); all notes off fades it; reset and releaseResources stop it at once. It stops at the end of the sound,
  as the prototype's buffer source does. The strip draws its moving line (sl-ui.js rawPos) from two new display frame
  fields, `rawOn` and `rawPos`, so a frame of zeros draws none.
- **Preview's line (decision 7b)** is one sentence for the page (Position and tempo card) and the report (after the cycle
  wraps): "off"; "on, at beat 2.427 (Cubase is stopped, so StrandLock plays the clip on its own clock)"; or "on, not
  playing (it plays while Cubase is stopped)". The audio thread hands the switch as the block read it, whether the
  block played Preview's clock, and that clock's beat to DiagModel each block (atomics).
- **An M2 check follows decision 7a.** `ui/stripRegionGestures` (section 6) checked that holding the mouse outside the
  Region asks nothing to play. It now checks the raw audition's two commands (from that spot, then its release) and the
  new readout.
- **Decision 7c** stands: Region start, Region end and Loop fade are M0's sliders, and MidiRouter's Preview hand-over is
  as built.

### Tests

| Test | Plan row | What it checks |
|---|---|---|
| `engine/fastReadsAliasing` | fast reads: -40 dB, +0 st | the Saw on Tape at +12, +24, +36 st at 44.1, 48, 96 kHz with its levels: at most -40 dB off the harmonic lines, each read from the rule's level (Stats::levelUses); +0 st bit for bit with the levels switched off, no level read; the levels-off (M3) figure printed beside each; the measure itself (the Saw < -60 dB, a naive saw > -40 dB) |
| `engine/fastReadsParity` | renders.json at or below 1x bit for bit with M3; above 1x bit for bit with the prototype with the levels off | all 73 cases rendered three ways: no levels (M3's render, which `fixtureEngineRendersTape` / `Grains` compare with the prototype), levels built and switched off (bit for bit with it: samples and stats), levels on (every case that reads no level bit for bit with it; the others named, still sounding); the prototype hash matches counted |
| `engine/fastReadsSweeps` | the sweeps | Tape C3 -> C6 over 2 s (Bend range 36) at 44.1, 48, 96 kHz: 3, 3, 2 level changes; Voice formant 0 -> +12 st: 1, 1, 0; Voice formant +12 st with the Tape switch and Speed 1 -> 4: 2, 2, 2; each change's largest step (5 ms before to 15 ms after) over the largest steady step (the 30 ms before) < 1.5; the plan's Voice sweep (C3 -> C6, formant +12 st) reads the formant's level all along with no change; a click of twice the steady step reads > 1.5 (the control) |
| `engine/fastReadsLevelsMatchTheSound` | fast reads: memory (the levels' shape) | 3 levels of ceil (n / 2^L) frames in the sound's format, float and 16-bit, at most 7/8 of its bytes; each level read at the sound's frame x within 2e-3 of the sound at x (two tones, 1000 places), one level-3 frame off > 0.1 away (the control); level 1 keeps 0.15 of the rate within 0.05 dB and takes 0.27 out by more than 55 dB; 20 frames give 1 level, 15 none; a stop on its third question ends the build with none |
| `cpu/fastReadsTapeCpu96k` | fast reads: CPU | 16 voices x 8 heads Tape at 96 kHz, every head above 2x: with the levels against without, printed (target at most 15 % slower), gated faster than real time on Windows; levels read with them, none without |
| `songs/longFileLoadsAs16Bit` (V2's, new budget) | fast reads: memory | the 10-minute stereo 48 kHz file: peak live on the worker at most 2 x 115.2 MB + 16 MB, its 3 levels 16-bit and 100.8 MB, made on the worker; 0 allocations on the audio thread at the swap |
| `engine/rawAuditionPlaysAsRecorded` | decision 7a | held at 30 %: 12 ms rising, then exactly 0.7 x the stored samples from the held frame, both channels; the display frame's position; released: falling over 30 ms, then silence; a second hold replaces the first after the first's fade; outGain +12 dB changes nothing; a 48 kHz sound at 44.1 kHz plays the sound's readAbs every 48 / 44.1 frames, bit for bit; a swap keeps the old sound's epoch until the fade ends; no sound, nothing |
| `alloc/rawAuditionAllocatesNothing` | decision 7a | holds, releases, second holds and a sound swap while one plays, with notes, through the processor's command queue: 0 allocations and frees (the Debug CRT run counts every heap call) |
| `ui/stripRawAuditionLine`, `ui/stripRegionGestures` | decision 7a | a press outside the Region: its readout "... outside the Region: the file as recorded", it plays, the line is drawn where the engine says, and goes after the release; the two commands |
| the `strip_raw_audition_dark` / `_light` snapshots | (pictures) | the strip with the mouse held at 70 %, after the Region, while the chord plays as recorded: its line and the readout |
| `diag/previewLineOnThePageAndInTheReport` | decision 7b | off; on at beat 2.427 while Cubase is stopped (the beat exact), the same words on the page and in the report; off once Cubase plays |

### Results (run 44978133608 on 140c271, all green)

- Windows Release: 2321 checks in 392 tests, 0 failed (engine 101 tests, 627 checks; cpu 5, 20; songs 16, 127; ui 136,
  662; diag 4, 42; alloc 10, 45). Linux Release: 2312 checks in 392 tests, 0 failed. Sanitizers: contract, model, engine
  and analysis 957 checks in 159 tests (the five new engine tests among them); state 169 checks in 23 tests. Debug CRT
  `--only alloc`: 46 checks in 10 tests, 0 JUCE assertions; Debug snapshots (146 pictures): 0 JUCE assertions.
  pluginval strictness 10: SUCCESS. Fixture check: passed (26 files, 4,962,066 bytes). No compiler warning from V4's
  lines (the float-equal warnings in SampleData.h, DiagModel.cpp, EngineRender.cpp and test_diag.cpp are older lines).
- **The aliasing figure, in plain words.** At 48 kHz, played one, two and three octaves up, the Saw on Tape now has
  about 0.0004 %, 0.0008 % and 0.0015 % of its sound off the note's harmonics (-54.25, -51.20 and -48.15 dB), where
  M3 had 0.17 %, 1.1 % and 3.1 % (-27.77, -19.58 and -15.10 dB). The worst of the nine cases is -48.15 dB, against
  the -40 dB allowed. At 44.1 kHz: -99.12 / -62.07 / -63.88 dB (M3 -23.76 / -17.96 / -13.96). At 96 kHz: -56.10 dB at
  +12 st (a 0.92x read: no level, the same as M3), -64.99 and -62.30 dB at +24 and +36 st (M3 -35.92 / -22.82). Every
  run read the level the rule chose (2738, 2798 and 3548 head runs per note). +0 st is bit for bit with the levels
  off at all three rates (-104.04, -42.13 and -47.60 dB: M3's reads). The same on Linux, to the last digit printed.
  The node model of the design (`m7v4/sim-design.js`) had predicted these figures.
- Parity: all 73 renders.json cases rendered three ways. Levels built and switched off: 73 of 73 bit for bit with no
  levels at all (samples and stats). Levels on: the 60 cases that read no level are bit for bit with M3's render; 13
  read one (tape-forward-c5, tape-chain-c3-g3-legato, chase-inside-chain, loop-3-passes-133bpm, live-bend-range-7,
  live-poly4-6-keys, voice-formant-up6, cloud-tape-switch-2x, tape-head-tune-octaves, strand-demo-2s,
  strand-demo-recipe-6, inst-change-macros, raw-snapshot), each still the note (within 20 dB of M3's loudness). The
  prototype's own hash matches 48 cases on Windows and 24 on Linux, with the levels switched off and with none: M3's
  counts.
- Sweeps (largest step at a level change over the steady largest step, at most 1.5): Tape C3 -> C6 0.905 / 1.000 /
  0.968 at 44.1 kHz, 0.967 / 0.949 / 0.977 at 48 kHz, 0.934 / 0.980 at 96 kHz; Voice with the formant raised 0.905
  (44.1) and 1.015 (48), no change at 96 kHz; Voice with the formant at +12 st, the Tape switch and Speed 1 -> 4 0.912 /
  0.989, 0.950 / 0.939 and 0.941 / 0.993. The row's own Voice sweep reads level 1 all along at 44.1 and 48 kHz (2179
  and 2180 grains) and none at 96 kHz. The control, a click of twice the steady step, reads 2.03.
- The levels: a stereo sound of 96,007 frames gets levels of 48,004, 24,002 and 12,001 frames, 672,056 of its 768,056
  bytes (float) and 336,028 of 384,028 (16-bit). Each level read at a frame of the sound is the sound there within
  4.7e-4 / 7.8e-4 / 9.7e-4 (levels 1 / 2 / 3); one level-3 frame off is 0.287 away. Level 1 keeps a tone at 0.15 of
  the rate (-0.000 dB) and takes one at 0.27 down by 69.5 dB.
- Memory: the 10-minute stereo 48 kHz file peaks at 219.74 MB live on the worker on Windows (219.63 MB on Linux)
  against the new budget of 246.40 MB (2 x 115.2 MB + 16 MB). Its 3 levels are 16-bit, 100,800,000 bytes. The audio
  thread made 0 allocations and frees at the swap. The load (hash, decode, levels, analysis) took 2332 ms on Windows
  and 1382 ms on Linux, against 1170 and 1167 ms in V2's run: the levels add about 1.1 s to a 10-minute song on
  Windows and about 0.3 s on Linux (the old sound plays meanwhile). Not checked: whether MSVC leaves the decimation's
  tap loops unvectorised where GCC does not, which would explain the difference. V3's 45 MB project now loads in 670 ms
  on Windows (291 ms in V3) and 553 ms on Linux (432 ms), inside its 2 s target.
- **CPU: the target is missed.** Tape with 16 voices x 8 heads at 96 kHz, every head reading above 2x: 6.79 x real
  time with the levels against 9.63 x with M3's reads on Windows, 42.0 % slower; the target is at most 15 %. On Linux
  8.15 x against 10.00 x, 22.7 % slower. The gate (faster than real time) passes. The cost is the read: a level read
  takes 8 frames and 8 kernel values per channel where M3's Hermite takes 4 frames, about 3.5 ns more per head per
  sample. The Hermite cannot read the levels inside -40 dB (-32.8 dB at 48 kHz, above), so the cheaper read is
  not an option. Ways to win most of it back, all giving the same output bits: the kernel stored as doubles (no
  conversion per tap), a plain-stretch fast path for level reads like M3's PlainStretch (the level's pointers and the
  read's bounds worked out once per run), and both channels in one SSE2 register. Left for the fix pass, with the
  plan's timing rule (a missed target is reported, not a red build).
- The raw audition: held at 30 % (frame 28,800), the first 576 samples rise to 0.7 (within 1.4e-8 of the line), then
  13,824 samples are exactly 0.7 x the stored ones, both channels; let go, it falls over 30 ms (within 1.5e-8) and the
  last sound is 1439 samples after the release, under 1440; a second hold: after 30 ms 8160 samples are exactly the
  second's; outGain +12 dB changes no bit; a 48 kHz sound at 44.1 kHz plays the sound's own reads exactly (7469
  samples); a swap keeps the old sound's epoch until the fade ends. 0 allocations and frees in processBlock on Windows
  Release, Linux (malloc too) and the Debug CRT (every heap call). On the strip a press at 20 % says "0:01.20 · outside
  the Region: the file as recorded", plays (peak 0.441) and draws the line where the engine reads (0.230, 99
  head-coloured pixels); after the release, silence and no line.
- Preview's line: "off"; with Preview on and Cubase stopped at ppq 2 for 20 blocks of 512, "on, at beat 2.427 (Cubase
  is stopped, so StrandLock plays the clip on its own clock)" on the page and in Copy report (the beat 2.426667, as
  expected); "off" again once Cubase plays.

### For the architect

- Plan 1.1's `engine` folder has three new files: `OctaveLevels.h` and `OctaveLevels.cpp` (the levels, their read and
  the rule) and `EngineRaw.cpp` (the raw audition).
- Plan 4 M7 "fast reads": the rule takes the lowest level at which a read is at most 1.25x (level 1 from 1.25x, level 2
  from 2.5x, level 3 from 5x), not floor (log2 rate), whose reads between 1x and 2x of the sound or of a level fold as
  M3's did (the row's own +12 st case at 48 kHz is a 1.84x read). A read at or below 1.25x plays as in M3, a wider
  promise than the row's "at or below 1x". "Half-band low-passed" is built as everything above the new half rate taken
  out (a 75-tap filter), and a level is read with an 8-tap windowed sinc, not the Hermite.
- Plan 4 M7 "fast reads": the row's Voice sweep (C3 -> C6 with the formant at +12 st) has no level change to measure: a
  Voice note's grains read at the formant's speed whatever the pitch. The test checks that, and adds two Voice sweeps
  that do change level.
- Plan 4 M7 "fast reads: memory and CPU": the CPU target (at most 15 % slower) is missed: 42 % on Windows, 23 % on
  Linux (Results). The -40 dB gate needs the 8-tap read; the fix pass can try the same-output speed-ups named there.
  The plan has no row for the time the levels take to build: about 1.1 s more for a 10-minute song on Windows, 0.3 s
  on Linux (Results).
- Plan 8.2 could take rows for: the octave levels (the prototype has none; the engine's test-only switch keeps the
  parity cases); the raw audition added after the output stage at 0.7 (outGain and the limiter leave it alone, as the
  prototype's buffer source goes straight to its master; the bypass fade applies; it ends at the end of the sound, as
  the buffer source does, and 30 ms after the release, where the prototype stops its source at 40 ms).
- Plan 1.3's command list gains `rawAuditionOn` (with startPos) and `rawAuditionOff`; `engine::DisplayFrame` gains
  `rawOn` and `rawPos`; `engine::Stats` gains `levelReads` and `levelUses`.
- V2's E3 line on the Sample tab is still not built (V2, For the architect).

## M7 summary

M7 makes StrandLock work with Kalan's own songs. It was built in four parts on branch `m7-songs`, each merged to main
when its CI run was green.

- **V1, the analysis.** `Source/analysis/` ports sl-analysis.js line by line: pitch (YIN), key (chroma and
  Krumhansl-Kessler), suggested parts (steady note, big hit, quiet texture, one-bar loop with its tempo) and the
  prototype's FFT. All 34 fixture cases agree with the prototype (33 bit for bit). DC says "no clear pitch" (decision
  3). Suggesting parts for a 5-minute song takes about 90 ms on Windows (target 1 s), with under 2 MB of memory.
- **V2, long songs.** A sound longer than 3 minutes is kept as 16-bit samples, which play bit for bit as the same
  values in float. A file longer than 10 minutes loads a 10-minute part, chosen in the part picker. The Region's exact
  edges keep a one-bar loop on a 10-minute file from drifting (0 frames over 64 passes). The analysis runs on the
  worker, is saved in the project, and gives the part chips, the root, key and engine suggestions (never applied by
  themselves) and Detect pitch & key on the Sound tab.
- **V3, keeping the sound.** The project keeps the sound (SAUD): files up to 20 MB without asking, 20-50 MB if Kalan
  says yes (asked once per file), else the Region with 1 s either side as 24-bit FLAC, never over 50 MB. The opt-in
  library and Locate sample... find a moved file, with every setting kept. A 45 MB project saves in about 0.1 s and
  loads in about 0.3 s.
- **V4, fast reads and M6's leftovers.** After a decode the worker builds three octave levels of the sound; a read
  faster than 1.25x reads a level, so Tape far up no longer aliases: at 48 kHz the Saw three octaves up has 0.0015 % of
  its sound off its harmonics, where M3 had 3.1 %. Level changes in a sweep do not click. The levels take 7/8 of the
  sound's memory again. Holding the mouse on the waveform outside the Region now plays the song as recorded, and the
  Diagnostics page and Copy report have a Preview line.
- **Figures at the end** (run 44978133608): 2321 checks in 392 tests on Windows and 2312 on Linux, all passing; the
  sanitizers, the Debug allocation run, the Debug pictures (0 JUCE assertions) and pluginval at strictness 10 pass.
- **Not done, or open:** the CPU target of the fast reads (V4: 42 % slower on Windows in the worst case, not 15 %);
  the levels' build time on Windows (about 1.1 s more for a 10-minute song); E3's line on the Sample tab (V2); the
  drag sources only Cubase can test (MediaBay, the Pool, a trimmed audio event: `docs/CUBASE-CHECKLIST.md`, M7 step
  2); Kalan's answers to the M7 checklist.
- **For the fix pass:** each part's "For the architect" list; the CPU speed-ups and the build time in V4's Results;
  the checklist answers when they come.

## Fix

The M7 fix pass (plan 7.9: review, then a fix pass). Three reviewers (analysis parity; samples, state and threads; tests
and UI) reported 17 findings on main 15a2f8e. Each was read against the code first. Branch `m7-fixes`, from main
15a2f8e.

### Progress

- Done: every finding checked against the code (all 17 are real; none rejected). F1 was also checked in node on the V4
  builder's model of the engine with the levels patched in (`m7v4/sim-engine.js`, the reviewer's `review7-ap/alias-grid.js`
  and the fix pass's own `m7fix/grid.js`): it reproduces the CI figures and the -34 to -40 dB between the octave points.
- Done: the code and tests for every finding but C7-2's extra picture (below), the Cubase checklist, these notes.
- In progress: the first CI run of the branch.
- Next: read every failure and figure from CI, look at the new chip pictures, fill in Results, then main.
