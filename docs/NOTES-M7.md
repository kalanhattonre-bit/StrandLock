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
- In progress: the strip's chips, suggestion line and part picker; the Sound tab's Detect, Key and Scale.
- Next: the M7 tests (long file, part load, Region precision, root), the updated M2 tests, snapshots, CI, main.
