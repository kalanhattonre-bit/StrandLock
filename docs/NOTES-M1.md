# NOTES-M1

Builder notes for M1 (PLUGIN-PLAN section 4 and 7). Append only: one bullet per item, with the reason.

## M1 part 1

What part 1 built: the model's data types for all of M1, the rest of `core/`, nodes, generators, shapes and the
lane readouts, with their fixture tests. Parts 2 and 3 only add functions (and the Snapshot types, below).

### The types (Source/model)

- `Node.h`: `NodeShape { smooth, linear, bend, step }` (the fixture and CLIP codes 0..3), `Node { t, v, shape, bend }`,
  `NodeList = std::vector<Node>`, `kMaxNodes = 128`, `kSliceTol`. The node functions are declared here and live in
  `Nodes.cpp`, as plan 1.1 names them.
- `Clip.h`: `Gen` (kind, shape, rate as `core::RateId`, d0, d1, warp, lockToBeat, amount, smooth, perNote, seed, invert,
  key), `StartSpec`, `NoteSample` (every "follow the instrument" field is an empty `std::optional`), `Note`, `NotePtr =
  shared_ptr<const Note>`, `Clip` (bars, beatsPerBar, offsetBeats, loop, tieAcrossLoop, notes, extras), `ClipPtr`, and
  the limits (`kMaxClipNotes`, `kMaxClipBytes`, `kMaxLanesPerNote`, `kMaxChopNotes`, `kMinLen`).
- JavaScript nulls: a null `glide` is an empty `std::optional<double>`; a null `glideTo` or Gen `key` is an empty
  string. The prototype never keeps an empty id or link (normalizeNote reads `''` as null), so nothing is lost.
- `Note::c` and `Note::gen` are `std::array<..., 10>` in registry order. `Note::lane (LaneId)` and `genOf (LaneId)`
  index them. A default-constructed Note has empty lanes; `mkNote` fills them flat at their defaults, and `noteLane`
  reads an empty lane as flat at its default, as the prototype's noteLane does.
- `Note::serial` is a `NoteSerial`: a process-wide counter that gives a fresh value on every construction, copy and
  copy-assignment (a move copies too). So any Note that may have been edited has a new serial, and the
  SnapshotCache (plan 1.3) can key on it safely. `sameContent (a, b)` compares notes without the serial.
- `NoteExtras` (plan 3, "content this version does not know") is defined in full for the clip codec: the note
  record's trailing bytes, the sample record (`KeptRecord`: trailing bytes, and the original bytes when it held an
  unknown value) with the value it decoded to, per lane the lane and generator records the same way, and the
  unknown lane records verbatim. `ClipExtras` keeps the clip settings record's trailing bytes. The codec fills them;
  every other note has a null pointer. `cloneNote` shares the pointer.
- `ClipEdit.h`: `Ids` (every note, or a list; a single id string is a list of one), `IdSet`, `idSet`, `selected`,
  `withNotes`, and `editNotes (clip, ids, fn (Note& copy, const Note& original))`. Like the prototype, editNotes copies
  every note in ids even when fn changes nothing (tools.json lists those copies), and shares every other NotePtr.
- `Generators.h`: `GenPatch` (for `makeGen`), `GenEval` (the prototype's genEval closure; it holds the one-step
  random cache, `randomStep (k)` is the plan 1.7 value of step k, `phase (t)` the step phase).
- `Shapes.h`: `ShapeKey` (15 keys in SHAPE_KEYS order; "wobble" parses as squiggle), `ShapeScope`, `PatternShape`,
  `PatternOpts`, `ShapeOpts`. In both option structs a NaN number means "not given" and takes the prototype's
  default, exactly as its `num (o.x, d)` does.
- `LaneFormat.h`: `LaneCtx { inst, bpm, travelKind }` (the prototype's ctx), `formatCtx (inst, bpm, note*)`,
  `GrainBase`, `GrainUnit`.

### Where each JS function landed

- core: `fmix32`, `hash32`, `rand01`, `idHash` in `Seeds` (scaffold); `toBase36`, `jsStringLess` added to `JsCompat`;
  `F32_EPS`, `travelSpeed`, `travelSpeedToV`, `V_FWD_MIN`, `V_BACK_MAX`, `refreshSmooth` in `Travel` (the two edges are
  `constexpr`, found by stepping the Float32 bit pattern); `bendShape`, `gShape` in `Curves`; `mergeInst`,
  `normalizeInst`, `instFromJson`, `instToJson` in `Inst`; V8's `Math.exp` as `jsExp` in `JsMath`.
- `model/Nodes.cpp`: `mkNode`, `flat`, `normalizeNodes`, `nodesSorted`, `tangents`, `evalSeg`, `segIndex`, `evalNodes`,
  `sampleNodes`, `rdpIdx`, `rdp`, `capNodes`, `sliceNodes`, `reverseNodes`, `flipNodes`, `scaleNodes`. (`nodesOf` and
  `cloneNodes` are a vector literal and a copy.)
- `model/Clip.cpp`: `defaultSample`, `mkNote`, `newClip`, `cloneNote`, `cloneClip`, `noteLane`, `timeCmp`, `timeOrder`.
- `model/ClipEdit.cpp`: `idSet`, `selected`, `editNotes`.
- `model/Normalize.cpp` (the JSON boundary): `nodesFromJson` (a prototype node list before normalizeNodes cleans
  it: v3 `lin:true`, strings and missing fields read as the prototype reads them), `genFromJson` (normalizeGen),
  `sampleFromJson` (normalizeSample), `jsTruthy`, `jsonNumber`.
- `model/Generators.cpp`: `normalizeGen` (typed), `makeGen`, `genEval` (`GenEval`), `genOffset`, `tableLength`,
  `laneTable`, `setGen`, `clearGen`, `bakeGen`, `reroll`.
- `model/Shapes.cpp`: `SHAPE_KEYS`, `SHAPE_NAMES`, `patternNodes`, `seededList`, `fitNodes`, `shapeNodes`,
  `speedShape`, `pathShape`, `shapeKeyKind`, `shapeKeysFor`, `applyShape`.
- `model/LaneFormat.cpp`: `grainBase`, `mapLane`, `formatLane`, `fmtMag`, `fmtSemis`, `fmtDb`, `trimNum`, and the
  UI's `formatCtx`.

### For parts 2 and 3

- Still to port: `normalizeNote` and `normalizeClip` (add them to `Normalize.cpp`: they take the prototype's own
  note objects, so they use `nodesFromJson`, `genFromJson` and `sampleFromJson`), the rest of `ClipEdit` (`uniqueId`,
  `addNote`, `removeNotes`, `updateNotes`, `duplicateNotes`), Tools, NoteSettings, Glides, Recipes, Presets, History,
  the clip codec and the snapshot compiler. `core/Snapshot.h` is not written yet: plan 1.4 gives its layout, and
  `NoteTables` is the compiler's to define.
- The fixture tests read notes and clips through `Tests/model/ModelFixture.h` (`noteFrom`, `clipFrom`, `clipsFrom`,
  `nodesMatch`, `tableMatch`) and count cases with `FileTally`, which prints the case count and the bit-for-bit count
  and fails when the count differs from the file's. Use it for tools, glides, snapshots, presets and normalize.
- `test_inst.cpp` already checks the six instrument cases of `model/normalize.json` and the three full instruments
  of `presets.json`. The test for the whole of normalize.json (its 12 clip cases) belongs with normalizeClip.
- `applyShape` returns `std::optional<Clip>`: nothing where the prototype throws (a Travel-only key on another lane).
  The tools that throw in the prototype for an unknown lane id cannot meet one in C++ (`LaneId` is typed).
- `editNotes`'s fn edits the copy in place; to replace a note entirely, assign to the copy.
- `laneTable` takes `N | 0` for its sample count, as the prototype does, so a note far longer than any song
  (l * res above 2^31) would ask for a huge table. normalizeNote does not bound `l`; the snapshot compiler or the
  clip limits should.

### Deviations and decisions

- `bendShape` uses `core::jsExp`, a port of V8's `Math.exp` (fdlibm e_exp.c plus V8's special case exp (1) = e),
  always, not behind an opt-in `STRANDLOCK_FDLIBM` switch (plan 1.6 rule 9). Reason: `(exp (k u) - 1) / (exp (k) - 1)`
  turns a one-ulp difference in exp into up to 5e-15 relative, and 70 of seeds.json's 441 bendShape cases would then
  miss the file's 4e-16. The port was checked in node against `Math.exp` on 2.2 million inputs, all bit for bit, and
  the C++ test checks 20 of them by their bit patterns. A compile switch would need a CMake flag, which is M0's file.
- `gShape`, `travelSpeed` and `mapLane` use `std::pow`, as the plan says. On Windows that is the same CRT pow the
  fixtures were made with. On Linux (glibc) a one-ulp pow difference near a rounding tie is possible (NOTES-M0); 28 of
  the 441 gShape cases would turn such a difference into more than 4e-16 because of the `1 - pow (...)` cancellation.
  If the Linux job ever shows it, the answer is a correctly rounded pow for gShape, not a looser tolerance.
- `evalNodes` and `reverseNodes` on an empty list return 0.5 and flat (0.5); the prototype throws there. A note's lane
  is never empty after normalizeNote, so this only guards hand-made lists.
- `instFromJson` drops top-level keys it does not know (normalizeInst keeps them on the object; nothing reads them).
- The monotone-smooth test also checks the Float32 table: every `sampleNodes` value lies between its two nodes
  rounded to Float32, which follows exactly from the double bound and makes the table side of the rule a gated check.

## M1 part 2

What part 2 built: the editing tools. Every function returns a new clip and shares every note it did not act on
with the input (copy-on-write); nothing changes a note behind a NotePtr.

### Progress

- Done: everything below. CI was green on m1-model at 18637a2 (run 42658506386): Windows 244 checks in 42 tests,
  Linux 245 in 42, Linux sanitizers 185 in 34, Debug CRT allocation 15 in 2, pluginval strictness 10 SUCCESS.
- Next (part 3): History, the clip codec, the snapshot compiler, and the items under "For part 3".

### Where each JS function landed

- `model/ClipEdit`: `uniqueId`, `addNote`, `removeNotes`, `updateNotes` (the value form; the function form is
  `editNotes`), `duplicateNotes`, plus `pasteNotes`, `copyLanes` and `pasteLanes` (below). The edits that can be
  refused return `EditResult { clip, refused, count, ids }`.
- `model/NoteSettings`: `applyNotePatch` (`SamplePatch`; `Choice<T>` = not given / null / a value), `setNoteSample`,
  `getMixed` (`MixedSettings`, each field a `Mixed<T>`; nothing when no note is selected), `setLaneNodes`,
  `setLaneNodesMany`.
- `model/Tools`: `connect` (`ConnectMode` nearest / furthest / random), `disconnect`, `walk` (`WalkField`), `chop` (a
  RateId or a number of beats; `ChopStart` for the prototype's stepStart number or `true`), `reverseLane`, `flipLane`,
  `scaleLane` (More / Less), `selectSimilar` (`SimilarKind`), `humanize` (`HumanizeAmounts`; the prototype's
  `lanes: number` is `volumeAndTone`), `strum` (`StrumDir`), `legato`. `laneEdit` is private to Tools.cpp.
- `model/Glides`: `startSemis`, `endSemis`, `glideD`, `computeGlides` (returns a `GlideMap`, insertion-ordered like a
  JavaScript Map: a second `set` keeps the key's place), `buildChains` (`Chains` = lists of ids).
- `model/Recipes`: `straightPatch` (STRAIGHT as an `InstPatch`), `recipes` (RECIPES; each `RecipeLane` is reset, shape or
  generator, in the prototype's key order), `applyRecipe`, `kDiceFields`, `dice` (pins as a list of names).
- `model/Presets`: `soundPresets`, `soundBase`, `applySoundPreset`, `strandDemo`, `clipPresets` (CLIP_PRESETS, built
  once on first use), `loadClipPreset`. `presetBuilder` is the private `Builder` in Presets.cpp.
- `model/Normalize`: `normalizeSample`, `normalizeNote` and `normalizeClip` on typed notes, and `noteFromJson` /
  `clipFromJson` (SLModel.normalizeNote / normalizeClip on JSON). The plan's test list names `normalize`: the 12 clip
  cases of normalize.json are in `Tests/model/test_normalize.cpp`.
- Tests: `test_tools`, `test_glides`, `test_presets`, `test_normalize`, `test_immutability` (immutability and mode
  lock), `test_limits` (edit caps, paste). `Tests/model/ToolOps.*` runs one tools.json case (shared by two tests).
  `ModelFixture` gained `notesClose`, `clipNotesClose`, `bitIdentical`, `idsFrom`, `stringsFrom` and `jsonSame` (moved
  from test_inst.cpp).

### Results (CI)

- Bit for bit on Windows: tools.json 132 of 132, glides.json 78 of 78, presets.json 105 of 105, normalize.json's clip
  cases 12 of 12. On Linux presets.json is 98 of 105 bit for bit, all within tolerance: the 7 cases hold Strand
  Demo's chord notes, whose tone lane is `patternNodes` with `acc 0.6`, and `Math.pow (x, 1 / (1 + acc))` is glibc's
  pow there (one ulp, as NOTES-M0 and part 1 say).
- Immutability: 132 operations, the input unchanged after each, 268 result notes checked to be the input's own.
  Mode lock: 1,000 calls (20 with an id the clip does not hold) on the 12 presets and a clip of unnormalized lanes.

### Decisions and deviations

- **Paste notes is new.** The prototype has only Copy / Paste lanes (sl-ui.js), which never adds a note, yet the plan
  caps "Paste" at 4096 notes. `pasteNotes (clip, notes, at)` adds copies of clipboard notes, the earliest at `at`,
  links inside the group kept, ids `uniqueId (taken, 'paste:' + id + ':' + at, k)`, refused past 4096. The prototype's
  Copy / Paste lanes moved from sl-ui.js into ClipEdit as `copyLanes` / `pasteLanes`, so M5 can call them.
- **The note cap lives in the model functions.** `addNote`, `duplicateNotes` and `pasteNotes` take `maxNotes` (default
  and ceiling `kMaxClipNotes`) and refuse with the count, the clip unchanged. `chop` keeps its 1024 default, but its
  `maxNotes` is capped at 4096, so no tool can make a clip the decoder would refuse. The prototype has no such cap.
- `chop` with a step that is not a positive finite number reads 1/16 (rateBeats' rule). In the prototype a NaN step
  makes NaN piece counts and silently drops the chopped notes.
- `selectSimilar` "bar" uses the clip's `beatsPerBar` (the prototype's BEATS_PER_BAR is always 4, which every fixture
  clip has). "invert" reads `Ids::all ()` as the prototype's null (no selection), so it gives every note.
- `strum` steps over a note whose start is NaN; the prototype would loop for ever there. Normalized clips hold none.
- Where the prototype throws (an unknown recipe, sound preset or clip preset) the C++ returns nothing. Unknown lane
  ids, modes and kinds cannot be written in C++ (typed enums).
- A `SamplePatch` cannot hold the values applyNotePatch ignores ('mixed', an engine name that does not exist, 'yes' for
  tape); whoever builds a patch from text leaves those fields unset, as `Tests/model/ToolOps.cpp` does.
- `updateNotes` writes glide, gc and glideTo as given, as the prototype does (it can make a self link). The UI should
  not pass one; `normalizeClip` drops it at the next load.
- `normalizeClip` on a typed clip keeps beatsPerBar, offsetBeats, loop, tieAcrossLoop and the clip extras (the
  prototype's clips have none of them). `loadClipPreset` normalizes the preset's notes directly instead of a
  `JSON.parse (JSON.stringify ())` copy: that copy only turns -0 into 0, and no preset holds a -0.
- `computeGlides` normalizes the instrument it is given, as the prototype does. `buildChains (clip)` with no glides
  uses the default instrument (the prototype's `computeGlides (clip)` with no inst).
- Tolerances: tools.json names p, s, l and node values but no other number; vel, glide, gc, the Start position and
  generator depths are held to the node values' 1e-12 (all are bit for bit on both platforms). presets.json holds
  every value exact except node values (1e-12); normalize.json holds everything exact.
- **Immutability and mode lock compare fields, not the encoding.** The plan says "byte-identical in the codec
  encoding"; the codec is part 3's. Until then both tests compare every field of every note as bit patterns
  (`modelfx::bitIdentical`: -0 differs from 0, and the extras pointer must be the same), plus the clip settings.

### For part 3

- Switch `test_immutability`'s two tests to compare the codec encoding (keep the field comparison too), and add the
  32 MiB encoded-size cap to the edit-cap test (`ClipDocument` keeps a running size, plan 3; the model functions
  only know note counts).
- Plan 8.2 #27 (an id over 255 UTF-8 bytes is renamed by the missing-id rule) is not in `normalizeNote` yet: it
  belongs where the codec and the prototype import call it.
- The codec should rebuild decoded notes through the typed `normalizeNote` / `normalizeClip` (plan 3: "The clip
  decoder rebuilds notes through model::normalizeNote"); `clipFromJson` is the entry for prototype JSON.
- History (`VersionHistory`), the clip codec and the snapshot compiler are untouched. The compiler needs all three
  glide modes: call `computeGlides` on copies of the inst with `glideMode` off, legato and always.
