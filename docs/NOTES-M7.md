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
- In progress: the first CI run on the branch.
- Next: the CI figures below, then the fast-forward of main.

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
| `detectionKeyProgressions` | detection: C major | the prototype test's I-IV-V-I in C major, and also G major and A minor (a chroma rotated the wrong way still finds C major); silence has no key |
| `detectionNoClearPitch` | detection: white noise | 6 seeds of white noise; silence; a 30 ms note; DC (decision 3); and A3 riding on a DC offset keeps its pitch (the rule is narrow) |
| `suggestPartsFiveMinutes` | speed | the 5-minute song built at test time; 3 runs, the slowest printed against 1.0 s and gated at 5 s in optimised builds; the runs agree; at least 3 valid parts (the prototype test's constraints) |
| `longFileMemoryAndStop` | (for V2) | on the same song, each function's peak live heap stays under half the prototype's 11 kHz copy (6.6 MB; its mono copy would be 52.9 MB); a Stop that never fires changes nothing and is asked through each analysis; a Stop that fires on its fifth question gives no pitch, no key and no parts |
| `tempoOfResampledBeats` | (extra) | the prototype test's Beat loop resampled to 92, 100, 108, 128 and 140 bpm: a found tempo within 1 bpm |
| `analysisPieces` | (extra) | fft (512) against a direct DFT; the bit reversal a permutation; overlapOk at and past half; estimateTempo on 4 onset curves against SLAnalysis._estimateTempo's values (120, 139.67, 84.72, and 120 not found), and a flat curve |

### Results (CI)

- (to come)

### Hand-off for V2

- **Calling it.** Wrap the decoded sound in a `Source` and call the three functions on the SampleManager worker
  (decision 1). For float data `FloatSource` takes the channel pointers; for the int16 storage of a long file, write a
  small `Source` whose `read` converts a block to floats (the analysis asks only for blocks inside the sound: channel,
  start, count > 0, start + count <= frames). For "Detect pitch & key" on the Region, a `Source` that offsets into the
  Region's frames is enough (or a FloatSource over offset pointers).
- **Cancelling.** Pass a `Stop` that returns true once a newer request is waiting (the SampleManager's "newest wins").
  A stopped call returns no pitch, no key or no parts, so check the Stop yourself before storing a result.
- **Memory.** No full-length buffer is made: on the 5-minute stereo song each function stays under 6.6 MB live (CI
  figures above); a 10-minute file at 48 kHz has about twice the frames, so about twice the per-frame values.
- **SMPL's cache.** `Pitch` {midi, cents, hz}, `Key` {key, scale, r} and `Part` {kind, label, start, end, bpm,
  tempoFound} hold every field plan 3's cache stores; `scaleName` and `partKindName` give the prototype's strings, and
  `PartKind`'s values (steady 0, hit 1, quiet 2, loop 3) can be the cache's u8. Labels hold the part's time, so store
  them as they are. Part start and end are fractions of what was analysed (the loaded range, plan 3).
- **Times.** See the CI figures: suggestParts on 5 minutes of stereo 44.1 kHz, detectPitch and detectKey on the same.
