# NOTES-M0

Builder notes for M0 (PLUGIN-PLAN section 4 and 7). Append only: one bullet per item, with the reason.

## Scaffold

- Built from `origin/main`, not `origin/m0-skeleton` as 7.1 says. Main already holds the skeleton's 5 commits plus
  the plan, `docs/parameters.json`, `tools/` and `Tests/fixtures/`, so starting there loses nothing.
- `jsNumberToString` is implemented in the scaffold, not only declared. `jsToFixed` needs it (`toFixed` prints
  String (x) for |x| >= 1e21), and JsonLite writes numbers with it. It is checked against all 500 `jsNumber` cases.
  `toBase36` and `jsStringLess` are declared only, for M1, as 7.1 says.
- `jsToFixed` and `jsToPrecision` read the exact decimal expansion of the double from `std::to_chars` (fixed, with
  exactly as many fraction digits as the double has) and round half up on it. The same algorithm, written in
  JavaScript with BigInt, matched V8's `toFixed`, `toPrecision` and `String` on 883,315 values before it was ported.
- `core::Inst` stores every JavaScript number as a double (polyphony, root and bendRange too), because the engine's
  clamps act on the double (`Math.round (32.6)` -> 33 -> 32). The exceptions are `seed` (int32, what `x | 0`
  leaves), the mod-wheel `cc` and `key` (int), `scale` (an index into `kScales`), the mod-wheel targets (a lane
  index, -1 = off) and `vibSync` (-1 = Free, else a `RateId`). No strings: Inst stays trivially copyable for the
  audio thread.
- `clampForEngine` maps a choice outside its list to the engine's fallback for an unknown string (voice, forward,
  smooth, loop, same). For `jump` and `refresh.rate` that is 1/16, because the prototype engine keeps an unknown
  rate string and `rateBeats` plays it as 0.25 beats (the model's `normalizeInst` would say 1/8 for jump instead).
  The prototype engine never reads `glideMode`, `glideTime`, `vibSync` or `scale`; the plugin's engine indexes by
  some of them, so `clampForEngine` keeps those in range too (legato, 0.5 when non-finite, Free, Chromatic).
- The `normInst` spot test reads each fixture input through a small test-side loader that does what JavaScript does
  before `normInst` runs: a non-number becomes NaN, an unknown choice string becomes a value outside its enum, a bool
  field takes JavaScript truthiness, `refresh.lockToBeat` is `!== false`, and `seed` is `fin (x, 1) | 0`. The typed
  Inst cannot hold `"yes"` or `"grain"`, so that coercion belongs to the JSON boundary (`instFromJson`, M1).
- The state header's `headerCrc32` is the CRC-32 (zlib, `state/Crc32.h`) of the 12 header bytes before it. The plan
  names the field but not what it covers. The scaffold reads only the header: a newer `formatVersion` loads nothing
  and is not kept yet (keeping unreadable blobs verbatim is M0's container work).
- The sanitizer step runs `--only contract,model,engine`, not only `model,engine`: before M1 lands those two groups
  are empty, and the contract group is the code they will build on.
- Sanitizer flags go on StrandCore and on the runner's own source files, not on the JUCE module sources the runner
  also compiles, so a report points at StrandLock code. Under `STRANDLOCK_SANITIZE` the glibc malloc interposition is
  compiled out (it would hand ASan's allocator a heap it cannot see); operator new still counts, through malloc.
- The Debug test build sets `JUCE_LOG_ASSERTIONS=1`, so jassert reports through `Logger::writeToLog`. The runner
  installs a Logger that counts them and exits non-zero when any fired, in the tests and in the snapshot writer.
  Without that, jassert in a process with no debugger only writes to the debug output and nothing fails.
- The runner fails a test that makes no check, a registered name that appears twice in a group, and an `--only`
  list that matches no test (a typo would otherwise pass silently). A group named in `--only` that has no tests yet
  only prints a note. `--only` takes a comma list. The summary prints tests, checks and failures per group.
- AllocCounter replaces every global operator new and delete form (plain, array, nothrow, aligned, sized) with a
  16-byte size header, so the largest request and the live bytes are exact on that path. On glibc and the Debug CRT,
  malloc-family calls are counted too, sized with `malloc_usable_size` / `_msize_dbg`. Counting is per thread and
  only inside a `Scope`; `liveBytes` is net within the counted scopes and `peakLiveBytes` is its maximum.
- `Lanes.h`: only the pitch lane has its own dark and light line colours; for the other lanes `colourDark` and
  `colourLight` equal `colour`, so code can always read the theme's field.
- CMake checks at configure time that `Source/core/Version.h` says the same version as `project(VERSION 0.1.0)`.
  The editor now prints `kPluginVersion` instead of the old `STRANDLOCK_VERSION` definition.
- The UI namespace is now `sl::ui` (it was `strandlock::ui`), as plan 1.1 names it.
- The scaffold's editor keeps the skeleton's 75 % to 200 % limits (825 x 510 to 2200 x 1360, fixed aspect ratio).
  M0's own criteria (UI Scale 75-200 %, a window from 900 x 560 to 2400 x 1600) replace them.
- `.gitattributes` is GrainLock's, plus `binary` for `*.wav`, `*.bin` (the release state fixtures) and `*.zip`.
- `node tools/make-fixtures.js --check` runs in the Windows job with Node 24.16.0, not in the Linux job with
  Node 24 as plan 1.11 says. On the first scaffold run (CI run 42570049264, ubuntu, Node 24.22.2) six files differed
  (model/seeds, nodes, presets, ui/sample-rules, state/prototype-v4-demo, state/import). V8's `Math.pow` calls the C
  library's `pow`, and glibc's pow is accurate to about 0.52 ulp, not always correctly rounded. The first difference
  is seeds.json line 751, `travelSpeed (0.8358960531186312)`, whose exact value lies 0.0007 ulp from a rounding tie.
  Windows' pow (where the fixtures were made) rounds all 249 travelSpeed inputs correctly (checked with BigInt).
  So the fixtures can only be reproduced byte for byte on Windows, and the check runs there with the exact Node
  release the files were made with.
- For M1 and M2: the same holds for the C++. `std::pow` on Linux (glibc) can be one ulp from the fixture where
  Windows matches it, at inputs like the one above. The plan's tolerances absorb one ulp; if a discrete decision
  ever flips on such a value, that is plan 1.6 rule 9's fdlibm case.

## M0 part 1 (engine side of the probe build)

### Progress

- Done: parameters (`params/**`), the binary state (`state/**`), the diagnostics model (`diag/**`), host info, the
  probe voice (`probe/**`), the starter sounds (`sounds/**`), `UserSettings`, `Tests/support/Harness.*` and
  `Measure.*`, and the tests for every M0 table row that is part 1's (groups `params`, `state`, `sounds`, `probe`,
  `diag`, `alloc`).
- Not part 1's: the editor, the theme tokens, the Diagnostics page and the UI snapshots (part 2, on this branch).
- Next (part 2): the UI. The hand-off below lists what it can call.

### Hand-off for the UI builder

- **Parameters.** `proc.getApvts()` holds the 50 parameters in the frozen order. Ids, names and ranges are in
  `params/ParamIds.h` (`params::kParams[i]`, `params::P`, `params::findParam`); `proc.getParam (params::P::speed)`
  returns one. Float parameters are `params::FloatParameter`, a `juce::RangedAudioParameter`, so
  `AudioProcessorValueTreeState::SliderAttachment` and `ParameterAttachment` work on them as usual. Choices, switches
  and whole numbers are JUCE's own classes. `getText` and `getValueForText` give the prototype's readouts
  (`params/ParamText.h`). The knob snapping while dragging (`qLin`, `qLog`, `KNOB_MATH`) is the UI's job.
- To set a parameter from code with one host gesture, use `beginChangeGesture`, `setValueNotifyingHost`,
  `endChangeGesture`. A change inside a gesture is not reported as the host's on the Diagnostics page.
  `params::setPlainValue (param, index, plain)` sets a plain value without a gesture (state loads use it).
- **Instrument.** `proc.readInst()` is the `core::Inst` the audio thread plays now. `getInstState` and
  `setInstState` hold the INST fields that are not parameters (seed, wheel targets and CCs, key, scale, scale lock,
  exact Region edges).
- **Theme, UI scale, Keys default.** `proc.getTheme()` is the per-user theme when there is one, else the project's
  UIST copy. `proc.setTheme (t)` writes both. `proc.getUserSettings()` is the shared `state::UserSettings`
  (a `juce::ChangeBroadcaster`): `uiScalePercent()` (empty when never set), `setUiScalePercent`, `keysDefault()`,
  `setKeysDefault`. Every set is written to `%APPDATA%\StrandLock\StrandLock.settings` at once. In the test runner
  the file is a fresh one in the temp folder, so snapshots start from no per-user values. Suggested rule for the UI
  scale: the per-user value wins, and the UIST copy (`getUiState().uiScalePercent`) is used when there is none, as
  for the theme.
- **UIST.** `proc.getUiState()` and `setUiState (s)`: theme copy, UI scale, logical size, mode, sub-lane, tab,
  folded, zooms, scroll, grid, scope, copy mask, strip view. Decoding clamps every field to a usable value.
- **The waveform strip.** `proc.getLoadedSound()` returns `std::shared_ptr<const sounds::StarterSound>`, never
  null: `left`, `right` (float vectors), `sampleRate` (44100), `frames()`, `seconds()`, `id`, `seed`. It changes when
  a state load names another starter, so compare the pointer in the editor's timer and redraw when it changes.
- **Banner.** `proc.getStateBanner()` is non-empty while a state this version could not read (all of it, or one
  chunk) is being written back unchanged. Show it as the plan's banner. `proc.getUnknownChunkCount()` counts the
  unknown chunks kept.
- **Diagnostics.** `proc.getDiag()` is the `diag::DiagModel`. Call it on the message thread only.
  - `snapshot()` drains the audio thread's ring and returns everything: block counts (playing, stopped, no
    position, offline), min, max and last block size, the size histogram (`kBuckets` ranges: 1, 2-3, 4-7, ... 8192
    and up), the first 8 distinct sizes with counts (`exactSizes`) and `otherSizes`, the latest position (ppq, bpm,
    loop points, time signature, playing, looping, recording), cycle wraps, wraps inside a block and other jumps with
    their before and after positions, tempo min, max and changes, MIDI counts and the last note, velocity, CC, value,
    pitch bend and mod wheel, the recent MIDI list (block and sample of each event), the recent block starts with
    their ppq, the last parameter the host changed (`lastHostParam`, `lastHostParamValue`), state saves and loads
    with sizes, keys, drops, and the display scale. Call it from a timer (10 to 30 Hz). The totals are atomics and
    never lost; the recent history is a ring of 4096 events, and `lostEvents` counts what fell off while nothing
    drained it.
  - `keyReceived (description, keysSwitchOn)`: every key the editor gets (checklist steps 4 and 5).
  - `filesDropped (paths)`: each path with its size, and for an audio file JUCE reads, its frames, sample rate,
    seconds and channels (step 6). `textDropped (text)` for a text drop.
  - `setDisplayScale (desktopScale, uiScalePercent)`: what the editor sees (Windows scaling and the UI scale).
  - `reportText()`: the Copy report text, one line per item: plugin version, host name and version (PluginHostType
    plus the host program's file version), sample rate and the prepared block size, display scale, block sizes and
    ranges, recent block sizes, blocks while playing and stopped, offline blocks, position and tempo, block-start
    ppq, wraps, MIDI, the last host-changed parameter with its readout, state saves and loads, keys and drops.
  - The host's tempo for the top bar: `snapshot().hasBpm` and `snapshot().bpm`.
- **Host.** `proc.getHostInfo()`: `name`, `version`, `path`, `format`, `isCubase`, `summary()`.

### Deviations and decisions (part 1)

- The probe plays the starter `saw`, as the M0 section says, so a new instance's SMPL names `saw` (seed 1).
  M2 makes `sawopen` the default (D9).
- Float parameters are a small `FloatParameter` class, not `juce::AudioParameterFloat`. It keeps the exact plain
  value it was set to. With AudioParameterFloat a value can only go in through its normalised value, and plain to
  normalised to plain moves a skewed value by an ulp, so a get, set, get of the state would not be byte-identical.
- A state that lacks some chunks re-saves with the missing chunks appended after the kept ones, written from the
  defaults. The plan does not say where they go; this keeps every kept chunk in its place.
- A cut end of a container (bytes that hold no whole chunk) is kept, but written after every other chunk. In the
  middle, its header would claim the chunks after it as its payload, and the next load would lose them.
- A CLIP chunk with a bad CRC stays kept, with the banner, through parameter edits: M0 cannot edit a clip, so its
  part of the state stays unedited. "Replaced after an edit" (plan 3 (e)) is tested for the whole-blob cases and for
  a damaged PARM; for CLIP it comes with M2's clip edits.
- The sanitizer job also runs the `state` group. The state tests load every blob from a heap block of exactly its
  size, so a read past the data is an AddressSanitizer error there.
- Speed readouts under 1e-6 print in exponent form ("1.60e-7x", toPrecision's rule). SLSampleUI's parseNum would
  read that back as 1.6, so text entry for Speed reads an exponent. Text entry at a readout's step boundary
  ("1000 ms" means 999.5 to 1000; 1000 prints "1 s") tries values just inside the printed step before the general
  search.
- The Diagnostics page counts a move of the host position by more than 0.05 beat between playing blocks as a wrap
  (backwards with the cycle on) or a jump. The engine's own jump rule (0.1 beat, M2) is separate.
- The report also gives the size of the last state saved and loaded (checklist step 12 asks how big the project is).
- `ParamBridge::applyPatch` (plan 2.4) is not written yet: nothing in M0 loads a sound or a preset from the UI.
  M2 or M6 adds it when sound loads, presets and undo need it.
- Starter sounds are made on the message thread when a state load names one (tens of milliseconds). The plan's
  SampleManager worker thread comes with M2.
