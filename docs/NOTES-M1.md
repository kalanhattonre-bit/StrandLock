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
