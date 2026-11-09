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
