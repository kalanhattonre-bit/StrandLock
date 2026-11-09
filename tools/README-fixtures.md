# Golden fixtures

The files under `Tests/fixtures/` are the answers the tested browser prototype (`prototype-v4/`) gives to a fixed set
of questions. The C++ port must give the same answers, within the tolerance written in each file. Nothing in them is
typed by hand: `tools/make-fixtures.js` asks the prototype's own code and writes down what it returns
(docs/PLUGIN-PLAN.md section 5).

## Regenerating

```
node tools/make-fixtures.js                      # write every fixture (about 7 s)
node tools/make-fixtures.js --only model,engine  # only some areas
node tools/make-fixtures.js --check              # regenerate in memory and compare with the committed files
node tools/make-fixtures.js --out DIR            # write somewhere else (to compare two runs)
```

Areas: one per module in `tools/fixtures/` (except `common.js`), found by listing the folder, so a new area never
edits `make-fixtures.js`: `model`, `sounds`, `support`, `engine`, `analysis`, `ui`, `state` (M8: the prototype saves
and `state/import.json`) and `params`. `params` writes no file: it checks `docs/parameters.json` against SLModel
(defaults, `normalizeInst` clamps, choice lists and labels, the macros first, every `defaultInst()` field either a
parameter or plan 2.2 state) on every run, `--check` included, so a prototype change the parameter list does not
follow fails CI. Any module may export `check()` returning problem strings.

`--check` exits 1 when any file differs from what the prototype makes today, when a committed fixture is no longer
generated (stale), when a module's `check()` reports a problem, or when the size budget is broken (total under 5 MB,
every file under 1.2 MB). It reads committed
files with CRLF turned into LF, so a Windows checkout passes. It is meant for the Linux CI job (plan 1.11).

Regenerate whenever a file in `prototype-v4/src/` changes. Every fixture records the SHA-256 (after CRLF to LF) of the
prototype files it was made from, under `prototype`, so a diff shows which source moved.

No npm packages. Node only (made with Node v24.16.0; CI pins Node 24, plan 1.11, and `make-fixtures.js` prints a note
when it runs on another major). The prototype is read, never written.

## Determinism

The generators never call `Math.random`, `Date` or `mkNote` without an id; every random value is the prototype's own
seeded hash. Checked on 2026-11-08 after the plan review (24 files): a run into a scratch folder, a run into `Tests/fixtures` and a
third run into another scratch folder are byte for byte identical, and `--check` passes. Renders were also checked to be the same at block sizes 1, 37, 128, 700, 4096, a
cycled list and random sizes, so splitting `process()` at extra points never changes a sample.

A separate check (not committed) decoded every file the way a C++ reader would (base64 Float32, node tuples, omitted
default lanes, `"NaN"` strings), rebuilt the inputs from the JSON alone, ran the prototype again and compared:
17,649 checks passed, including 57 renders rebuilt from the file with different block sizes.

## Size

| File | Cases | Bytes |
|---|---:|---:|
| model/seeds.json | 3077 | 240,041 |
| model/nodes.json | 80 | 487,988 |
| model/shapes.json | 398 | 192,836 |
| model/generators.json | 218 | 448,884 |
| model/tools.json | 132 | 340,900 |
| model/glides.json | 78 | 37,009 |
| model/snapshots.json | 24 | 846,208 |
| model/presets.json | 105 | 216,296 |
| model/format.json | 1209 | 173,498 |
| model/normalize.json | 18 | 14,528 |
| sounds/starter.json | 10 | 174,137 |
| support/signals.json | 12 | 33,888 |
| engine/defs.json | 6 | 33,165 |
| engine/renders.json | 73 | 986,629 |
| analysis/detect.json | 28 | 12,546 |
| ui/theme.json | 2 | 4,279 |
| ui/inspector.json | 408 | 106,025 |
| ui/sample-rules.json | 309 | 176,595 |
| ui/readouts.json | 907 | 60,879 |
| state/prototype-*.json (4) | - | 65,770 |
| state/import.json | 4 | 29,508 |
| **total** | | **4,681,609 (4.68 MB)** |

Strand Demo at res 256 is 176 KB inside snapshots.json, as the plan measured.

## What each fixture proves

| File | Proves that the C++ ... |
|---|---|
| `model/seeds.json` | hashes, rounds and prints numbers exactly as JavaScript: `hash32` (MurmurHash3, including arguments past int32), `toInt32`, `idHash` over UTF-16 (non-ASCII and surrogate pairs), the 21 rates and their labels, `rateFromBeats`, the Travel speed curve and its Float32 dead-band edges, `travelSpeedToV`, `bendShape`, `gShape`, `String(x)`, `toFixed`, `toPrecision`, base 36, `Math.round`, all 96 key x scale snaps and the 128 note names. Every random choice in StrandLock rests on these. |
| `model/nodes.json` | cleans any node list (v3 `lin:true`, unsorted, NaN, out of range, more than 128 nodes) and then samples, evaluates (both sides of a vertical edge), slices, reverses, flips and scales it the same way. This is the drawing every lane is made of. |
| `model/shapes.json` | puts every shape key on every lane, Each and Across, both Travel kinds, two seeds for Random, and Fit with looping, holding and stopping Regions, giving the same nodes. |
| `model/generators.json` | turns Pattern and Random generators into the same Float32 lane tables (7 shapes x 5 rates, lock to beat on and off, warp, smooth, Same/Different, key, invert, the pitch lane's 1/6 scale), gives the same random value per step, Re-rolls the same seeds and bakes the same nodes (including the 1/16 Squiggle over 4 beats that needs the raised RDP tolerance). |
| `model/tools.json` | every selection tool (Connect nearest/furthest/random, Disconnect, Chop with its refusal, Walk, Humanize, Strum, Legato, More/Less, Reverse, Flip, Select similar, note settings, add/remove/update/duplicate, set/clear generators) changes the same notes in the same way, and leaves the others shared (copy-on-write). |
| `model/glides.json` | builds the same automatic slides and the same chains (one voice per chain) for the 12 clip presets and a voice-leading test clip, in all three Auto Slide modes. |
| `model/snapshots.json` | compiles a clip into the same snapshot the engine plays: settings resolved per note, glide and chain links, and all 10 lane tables, for every clip preset, every recipe and a clip with every per-note override. |
| `model/presets.json` | builds the 12 clip presets node for node, and applies sound presets, recipes and Dice (with pins) to the same instrument values. |
| `model/format.json` | prints every lane value the way the prototype does ("+3 st", "0.5x backwards", "Refresh 1/16 (125 ms at 120 bpm)") and maps it to the same real value. |
| `model/normalize.json` | loads old and damaged data the way the prototype does: v3 notes, missing/duplicate/numeric ids, dangling links, NaN nodes, garbage instruments. |
| `state/prototype-*.json` | (inputs only) two v3 and two v4 prototype saves for the M8 import. |
| `state/import.json` | (M8) imports each save as the plan says: the v4 saves along the prototype's own reopening path (normalize, then the saved starter's root 60 / cents 0, and the whole file as the Region when the session ended on a user file); the v3 saves through the plan's v3 mapping (formant->Voice, classic->Tape, lock->scale lock, `none`->off, CCs, theme); and where every field lands (parameters, the INST chunk, UIST, the clip settings). |
| `sounds/starter.json` | makes the 8 starter sounds sample-identical (length, root, peak, block RMS, three 512-frame windows, a hash). |
| `support/signals.json` | ports the prototype tests' signal makers (`lcg`, `tone`, `noise`, `chord`, `formantTone`, `smoothNoise`, `rampSource`, `dc`) exactly, so engine and analysis cases can name them. |
| `engine/defs.json` | uses the same engine constants: control rate, head and grain caps, budgets per sample rate, head tunings, the Hann and fade tables, the seed salts, and the engine's own instrument clamps. |
| `engine/renders.json` | plays the same audio: 73 renders (36 tagged `needs: "tape"` for M2, 37 `"grains"`) covering Tape (every Motion, Scatter with two seeds, Refresh stutter, Region loop with both seam fades, Hold, Stop), Path kind, chains, glide 0, restart, an engine change inside a chain, chase, tempo change, transport jump, stop/start, loop passes, live keys (legato, sustain, mod wheel to Travel and Heads, bend, polyphony steals, all off), Voice at C1/C3/C5 and with formant, Cloud (Spray, Skip, Refresh, each grain backwards, Tape switch), Lock from noise and from a chord, fractional and 8 heads, Drift, Head tune, the voice budget at 44.1, 48 and 96 kHz, live Scatter while the host plays, a 44.1 kHz source in a 48 kHz session, Strand Demo, three recipes, random block sizes, a note deleted while playing, an instrument change while playing and an engine-only snapshot. Each carries a whole-render hash, per-256-sample RMS and peak, 256-frame windows, voice/grain stats every 1024 samples and the display every 4800. |
| `analysis/detect.json` | detects the same pitch, key and suggested parts on the starters, the test signals, silence, and four long files joined from them. |
| `ui/theme.json` | has the 77 colour tokens of each theme with the prototype's exact values. |
| `ui/inspector.json` | parses and prints bar positions, lengths, note names, rates and typed lane values like the inspector, and simulates what head 1 reads for the note thumbnails. |
| `ui/sample-rules.json` | fades the same Sample-tab controls with the same reasons, finds the same contexts and grain kind, and makes the same engine suggestion and default part. |
| `ui/readouts.json` | prints every macro, offset and knob readout character for character, and parses typed values and knob positions the same way. |

## Encoding (plan 5.1, plus what the plan left open)

- Header on the first line: `fixture, version 1, generator, prototype` (hashes), `tolerance` (the plan's sentence in
  `text`, plus the same numbers as fields), `notes` (how to read this file). Then `clips` and `insts` tables, one entry
  per line, then `cases`, one per line.
- `F32 = {f32: base64}` little-endian binary32. `Table = F32 | {n, flat}`. `Nodes = [[t, v, shape, bend], ...]` with
  shape 0 smooth, 1 linear, 2 bend, 3 step. `Note` and `Clip` as in the plan: a lane that is flat at its default is
  left out of `c`, a lane with no generator is left out of `gen`.
- Numbers are what `JSON.stringify` prints (shortest round trip). `NaN`, `Infinity` and `-Infinity` are the strings
  `"NaN"`, `"Infinity"`, `"-Infinity"` (the plan names only NaN). `-0` prints as `0`; compare +0 and -0 as equal.
- A clip name `"preset:<id>"` means `loadClipPreset(id)`; model/presets.json checks those clips node for node, so no
  other file repeats them.
- A case with `recipe: n` uses the clip after `applyRecipe(clip, inst, n, every note)`. The clip half of a recipe does
  not depend on the instrument (checked at generation time).
- `insts` (presets.json, sample-rules.json) holds full instruments that cases name instead of repeating them.
- `hash` everywhere is FNV-1a 32 over the Float32 bytes of each channel in order (the prototype tests' `fnv`), as 8
  hex digits.

## The render harness (engine/renders.json)

The C++ test must drive its engine exactly like this, or the hashes cannot match:

1. `Engine({sampleRate: sr})`, `setInst(inst)`, `setSample(sound)`, then `setSnapshot` of the compiled clip
   (`compileSnapshot(clip, inst, {res})`, res 256 unless given) or of the raw snapshot.
2. `process()` is split at every host block boundary, every transport and event sample, every 1024th sample (stats),
   every 4800th (display) and, for `trace` cases, every 32nd.
3. At each split point p: first record the observations of the state after rendering `[0, p)` (none at p = 0), then
   apply the transport entries for p in order, then the events for p in order, then render to the next point.
4. `block` is an int (fixed host blocks), an int list (cycled) or `{random: seed, min, max}`: sizes
   `min + floor(lcg(seed)() * (max - min + 1))` with the tests' `lcg`.
5. Events: `noteOn p vel [opts]`, `noteOff p`, `ctrl name value|null`, `allOff`, `inst patch` (new inst =
   `normalizeInst(mergeInst(current, patch))`; when a COMPILE_KEYS value changes and a clip is loaded the clip is
   recompiled, as sl-ui.js does), `snapshot name|Clip` (compiled with the current inst).
6. Parity settings: beatsPerBar 4, offsetBeats 0, loop on, tieAcrossLoop off, ramp smoothing 0 ms
   (`Engine::setRampMs(0)`), instrument values in double (not through APVTS floats).
7. `needs`: `"tape"` when every voice of the case plays the Tape engine (M2 runs exactly these), else `"grains"`.
8. Every transport start falls on a multiple of 32 samples: there the plugin's control grid (anchored to the transport
   start, plan 1.8) and the prototype's global one agree. The plugin's own grid behaviour has its own C++ test.

## Choices made where the plan was open

- **Real prototype saves did not exist.** `state/prototype-*.json` are written in each prototype's own save format
  (v3: the object v3's `save()` stored under `strandlock_v3`; v4: what sl-ui.js stores under `strandlock_v4`) with
  fixed ids. After the plan review, the v3 saves also carry an instrument result through the plan's v3 mapping (plan
  section 3), and the edited v3 save uses `m2Lane: 'none'`; the v4 session save ended on a user file with a detected
  root (57, -12 cents), a Dorian key and a mod target, so the import's root/Region reset is visible. All of it moved
  from `model/normalize.json` to `state/import.json` (M8 owns it).
- **Generators:** the plan's product (shapes x rates x lock x warp x three starts) repeats itself, because warp does
  nothing when locked to the beat. Locked cases cycle through the three starts; unlocked cases take every warp. Each
  case with random values (Random, Random steps, Smooth random) also lists the seeded value of every step (`k0` +
  `steps`). For the 17 Random cases with no smoothing, those values were checked at generation time to equal what the
  generator really plays, sample for sample.
- **Sample rules:** every sound preset x recipe x clip preset would be 864 fades results, far over budget. The file has
  every sound preset x 4 clips, every recipe on Strand Demo and 8 targeted cases.
- **Format:** the 41 values per lane are crossed only with the settings that lane depends on (heads 1/3/7.5; the four
  grain engines plus edges; Refresh smooth/free/sync at 120 and 87 bpm; Travel speed/path), plus the Float32 band edges.
- **Analysis:** the plan names no long input for part suggestions, so four 15-20 s files are joined from starters,
  signals and silence (`{concat: [...]}`, `{silence: s, sampleRate}`).
- **Engine renders:** windows are placed at note starts, seams, joints, jumps and changes for each case (at most 4).
  Transport, chain and live cases play the Tape engine so M2 can run them; eight Voice cases have Tape twins
  (`tape-...`).
  Loops and the budget cases are stats only. A raw engine-only snapshot case is included so the C++ engine can be
  tested without its compiler.
- **Signals:** `lcg` returns doubles, so its cases list values instead of Float32 head/tail.
- **Private prototype values** (the engine's HANN/FADE tables, grain caps and salts; SLSampleUI's `fmtX`, `fmtMs` and
  friends) are read from an in-memory copy of the real source with those names added to its return value. The files on
  disk are never changed, and every render uses the unmodified engine text.
- **Extra cases beyond the plan's list:** setGen/clearGen in tools.json; the engine's own clamps (`normInst`) in
  defs.json; `rateId`, `fmtNum`, `peakBins`, `thumbAmps`, `tabAt` in inspector.json; `kindOf`, `suggestEngine`,
  `pickPart` in sample-rules.json; knob maths in readouts.json.

## Findings for the builders

- **M3's budget criterion (ruled in the plan review).** The prototype cuts fade tails with 16 voices x 8 heads and
  Lock N = 12: 10 cuts (48 kHz) and 15 (96 kHz) with 16 live keys 256 samples apart, 15 at 44.1, 48 and 96 kHz with
  16 clip notes 0.05 beat apart, and at 96 kHz the newest note sounds 4 heads. The plan's M3 line now takes these
  stats as the expected result and keeps cuts = 0 only for the scenarios the prototype measured with none.
- `detectPitch` on a constant (DC) signal returns MIDI 90 rather than "no clear pitch". The fixture records it; worth
  a look before M7.
- Node version: the plan now pins `setup-node 24`, the major these files were made with. JSON printing, `toFixed`,
  `toPrecision` and `String(x)` are fixed by the language standard, but `Math.sin/exp/pow` belong to the engine
  version; `make-fixtures.js` prints a note when it runs on another major.
