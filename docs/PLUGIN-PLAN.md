# StrandLock v1: the plugin build plan

This is the plan every builder follows to turn the tested browser prototype (`prototype-v4/`) into the StrandLock
VST3 instrument. It names the classes, files, tests and numbers. When a builder needs something this plan does not
say, they write it in `docs/NOTES-<milestone>.md` (for example `docs/NOTES-M1.md`) and carry on with the simplest
reading that keeps the prototype's behaviour. Nobody edits another milestone's files (section 7).

**Sources, in precedence order** (a higher one wins; every conflict is listed in section 8):
1. Kalan's v4 decisions as the tested prototype embodies them: `prototype-v4/BLUEPRINT.md` (the contract),
   `NOTES-integrator.md` (wins over `NOTES-model.md` / `NOTES-engine.md`), `src/sl-model.js`, `src/sl-engine.js`,
   `src/sl-sounds.js`, `src/sl-analysis.js` (981 node checks pass: `node prototype-v4/tests/run-all.js`), the UI files
   `src/sl-ui*.js` and `src/shell.html`, and his picks (decision ids D1-D10, idea ids like Y5, S3, C4, F3 from the picks
   page data: `...\strandlock-design\page\data.json`).
2. `StrandLock-handoff/PROMPT.md` (the v3-era brief): suite identity, plugin type, host sync, verification (section 14),
   README (section 15).
3. The design research: `...\strandlock-design\research\` (`risks.md` 10-12, `critique.json`, `code.md`, ...).

**Fixed suite rules.** JUCE 8.0.15 (GrainLock's pin), C++20, static MSVC runtime, Windows x64 VST3 only. GitHub
Actions is the only compiler. Releases happen only on `v*` tags, and only when Kalan says so. Parameter IDs, ranges
and choice-list lengths freeze at the first release. Real-time safety: no allocation, lock, logging or file access in
`processBlock`; immutable versions are swapped in atomically and freed off the audio thread. Cubase naming
everywhere: MIDI 60 = C3.

**Words used below.** *Clip*: the notes on StrandLock's own piano roll. *Note* (or strand): one clip note with its 10
lanes. *Lane*: a 0..1 curve over a note (nodes plus an optional generator). *Snapshot*: the compiled, read-only form
of a clip the audio thread plays. *Voice*: one sounding note (a chain of slid notes is one voice). *Head*: one of up
to 8 playheads inside a voice. *Grain*: one windowed read. *Tick*: the 32-sample control step. *Inst*: the
instrument settings (BLUEPRINT 1.4).

---

## 1. Architecture

### 1.1 Source tree

Two layers. **StrandCore** is a static library of plain C++20 with no JUCE: everything the prototype's four tested
modules do (model, engine, starter sounds, analysis). It compiles fast, runs headless, and is what the golden
fixtures compare. **The plugin layer** (JUCE) is the processor, parameters, state, host sync, document/undo, and UI.
GrainLock compiles its sources into both the plugin and the test runner; StrandLock does the same for the plugin
layer, and links StrandCore into both.

```
CMakeLists.txt                     targets StrandCore (static, no JUCE), StrandLock (VST3), StrandLockTests (console)
cmake/StrandFlags.cmake            floating-point flags for StrandCore (section 1.6)
.github/workflows/build.yml        GrainLock's workflow plus the fixture check (section 1.11)
Source/
  core/          [StrandCore] shared definitions the model and engine must agree on (BLUEPRINT 1)
    Version.h            kPluginVersion, kSnapshotVersion = 1, kStateFormatVersion = 1
    JsCompat.h/.cpp      toInt32 (JS "|0"), toUint32 (">>>0"), jsRound (Math.round), jsNumberToString (String(x)),
                         jsToFixed, jsToPrecision, toBase36, f32 (Math.fround), jsStringLess (UTF-16 order)
    Seeds.h/.cpp         fmix32, hash32(...int32), rand01, idHash (FNV-1a over UTF-16 code units)
    Rates.h/.cpp         enum class RateId (21 ids, SLModel.RATES order), rateBeats, parseRate, rateLabel, rateFromBeats
    Lanes.h/.cpp         enum class LaneId (10, registry order), kLanes[10] {id,key,mode,name,def,group,colours,sub},
                         kModes[8], laneDef, kSampleLanes
    Travel.h/.cpp        kF32Eps = 2e-8, travelSpeed, travelSpeedToV, kVFwdMin, kVBackMax, refreshSmooth
    Curves.h/.cpp        bendShape (PulseLock), gShape (v3 glide curve)
    Music.h/.cpp         kNoteNames, kScales[8] (SLModel.SCALES order), noteName (60 = "C3"), inScale, snapScale
    Inst.h/.cpp          struct Inst + enums, defaultInst, InstPatch, mergeInst, normalizeInst (model clamps),
                         clampForEngine (the engine's normInst clamps), instFromJson / instToJson
    JsonLite.h/.cpp      small JSON DOM: numbers parsed with std::from_chars (bit-exact round trip of JSON.stringify),
                         written shortest-round-trip; used by prototype import and by every fixture test
    Snapshot.h           the compiled clip the audio thread reads (section 1.4); produced by model, read by engine
    JsMath.h             (only if needed) fdlibm ports behind STRANDLOCK_FDLIBM, see section 1.6 rule 9
  rt/            [StrandCore, header-only] real-time hand-off pieces
    SpscFifo.h           fixed-capacity single-producer/single-consumer ring of trivially copyable items
    LatestSlot.h         triple-buffered latest-value slot (the display frame, section 1.5)
    VersionExchange.h    one-slot mailbox + epochs + message-thread pool; frees a version only below the oldest
                         epoch a voice still reads (section 1.3)
    StableSort.h         allocation-free stable insertion sort (std::stable_sort allocates; never use it on audio)
  model/         [StrandCore] port of sl-model.js (section 1.2 table)
    Node.h Nodes.cpp, Clip.h Clip.cpp, ClipEdit.h/.cpp, Normalize.h/.cpp, Generators.h/.cpp, Shapes.h/.cpp,
    LaneFormat.h/.cpp, Tools.h/.cpp, NoteSettings.h/.cpp, Glides.h/.cpp, Recipes.h/.cpp, Presets.h/.cpp,
    History.h/.cpp, ClipCodec.h/.cpp, SnapshotCompiler.h/.cpp
  engine/        [StrandCore] port of sl-engine.js
    EngineConfig.h       kCtrl 32, kMaxHeads 8, kVoiceGrains 8, kCloudGrains 5, kLockMaxN 12, kBudget48k 192,
                         kMaxPolyphony 32, kVoicePool 64, salts
    SampleData.h         decoded sample: float32 (or int16 for long files, M7) L/R, n, sr, stereo flag, name
    Tables.h/.cpp        HANN (4096+2), FADE (1024+2), hannTab, fadeIn - built at static-init time, never lazily
    SourceRead.h         readAbs: 4-point Hermite at a fractional frame, every index guarded
    Region.h/.cpp        RegionState, readReg (post/pre seam fades), seamRho, mapPos, grabPos
    Biquad.h             RBJ peak / low-pass, transposed direct form II, NaN guard
    Head.h               one playhead (position raw+off, speed, gains, grain slots, Lock captures)
    Voice.h              class Voice (one sounding chain)
    VoiceTick.cpp        Voice::tick (lanes, macros, pitch, refresh, scatter, heads, filters) - the Scan/Travel path
    VoiceRender.cpp      Voice::render, renderRun, runTape
    VoiceGrains.cpp      Voice::runGrains (Voice and Cloud)                                         [M3]
    VoiceLock.cpp        Voice::lockGrab, lockRho, runLock                                           [M3]
    VoiceChase.cpp       Voice::chase, ffAdvance, ffChain
    Engine.h Engine.cpp  class Engine: inputs, clocks, process, limiter
    EngineSchedule.cpp   scanStarts, startClip, doChase, addVoice (steal)
    EnginePlan.cpp       plan (budget, thinning, grants), measure
    EngineDisplay.cpp    fillDisplay, stats
    InstSmoother.h       the 20 ms parameter ramps, stepped once per tick (section 2.3)
    DisplayFrame.h       POD display frame + DisplaySlot (section 1.5)
  sounds/        [StrandCore] port of sl-sounds.js: StarterSounds.h/.cpp (makeStarterSound(id, seed) -> SampleData)
  analysis/      [StrandCore] port of sl-analysis.js: Analysis.h, PitchDetect.cpp, KeyDetect.cpp, PartSuggest.cpp, Fft.h [M7]
  probe/         [M0 only, deleted in M2] ProbeSynth.h/.cpp: 16-voice Hermite player of the starter Saw
  PluginProcessor.h/.cpp   StrandLockProcessor
  PluginEditor.h/.cpp      StrandLockEditor
  params/        ParamIds.h, ParameterLayout.h/.cpp (createParameterLayout), ParamText.h/.cpp (readouts),
                 ParamBridge.h/.cpp (atomics -> core::Inst on audio; InstPatch -> parameters on the message thread)
  state/         StateFormat.h/.cpp (container), StateChunks.h/.cpp (PARM INST UIST SMPL SAUD), Crc32.h,
                 UserSettings.h/.cpp (per-user PropertiesFile), PrototypeImport.h/.cpp [M8]
  host/          HostInfo.h/.cpp [M0], HostSync.h/.cpp [M2], MidiRouter.h/.cpp [M2]
  doc/           ClipDocument.h/.cpp, EditHistory.h/.cpp, SnapshotPublisher.h/.cpp, SampleManager.h/.cpp,
                 CommandQueue.h, Recorder.h/.cpp [M8]
  diag/          DiagLog.h (audio -> UI events), DiagModel.h/.cpp (aggregates for the diagnostics page)
  ui/            Theme.h/.cpp, StrandLookAndFeel.h/.cpp, Logo.h/.cpp (paintLogo), Controls.h/.cpp, MainView.h/.cpp,
                 TopBar, ModeBar, SelectionRow, ToolPanels, HintBar, Toast, KeyRouter, DiagnosticsPage,
                 tabs/ (SampleTab, MacrosTab, NoteSettingsTab, SlidesTab, CopyPasteTab, ModWheelsTab, SoundTab),
                 roll/ (RollView, RollGeometry, RollPainter, NoteLayerCache, RollGestures, PianoStrip, Ruler,
                        Inspector, SelectionBar, Drawers, LaneMenu, BoxSelect, GhostPreview),
                 strip/ (WaveStrip, WavePeaks, HeadsOverlay, PartChips), SampleTabRules.h/.cpp
Tests/
  TestMain.cpp           argument parsing and the runner; tests register themselves (section 7.3)
  support/               TestRegistry.h, Check.h/.cpp, AllocCounter.cpp, FixtureJson.h/.cpp, Harness.h/.cpp,
                         Measure.h/.cpp (FFT f0, autocorrelation, centroid, rms, largest step), TestSignals.h/.cpp
  model/ engine/ plugin/ ui/ analysis/   one test file per area
  fixtures/              golden data from the prototype (section 5); Fingerprints.h at M9
tools/
  make-fixtures.js       node: regenerates Tests/fixtures from prototype-v4/src (--only, --check); its areas are the
                         modules in fixtures/, found by listing the folder (a new area never edits this file)
  fixtures/              common.js, model.js, sounds.js, engine.js, analysis.js, ui.js, support.js, state.js (M8),
                         params.js (writes nothing: checks docs/parameters.json against SLModel on every run)
docs/                    PLUGIN-PLAN.md, parameters.json, NOTES-<milestone>.md
```

Namespaces: `sl::core`, `sl::rt`, `sl::model`, `sl::engine`, `sl::sounds`, `sl::analysis` in StrandCore;
`sl::params`, `sl::state`, `sl::host`, `sl::doc`, `sl::diag`, `sl::ui` in the plugin layer.

### 1.2 Which JS function becomes which C++ class or function

The C++ is a port, not a rewrite: same operation order, same constants, same names in camelCase. A builder ports a
function by reading it line by line. The model and engine may share one C++ implementation of a shared definition
(JS had to copy it because the worklet text could not reference SLModel); the fixtures prove both still match.

**sl-model.js (`SLModel`)**

| JS | C++ |
|---|---|
| `fmix32`, `hash32`, `rand01`, `idHash` | `core/Seeds`: `fmix32(uint32)`, `template<class... A> uint32 hash32(A...)` (each arg through `toInt32`), `double rand01(uint32)`, `uint32 idHash(std::u16string_view)` |
| `LANES`, `LANE`, `LANE_IDS`, `SAMPLE_LANES`, `MODES`, `laneIndex`, `laneDef` | `core/Lanes`: `LaneId`, `kLanes`, `kModes`, `laneIndex`, `laneDef`. **The registry order is part of the seed contract** (generators hash the lane index). New lanes are appended, never inserted. |
| `RATES`, `RATE_BEATS`, `rateBeats`, `isRate`, `rateLabel`, `rateFromBeats` | `core/Rates`: `RateId`, `rateBeats(RateId)`, `rateBeatsOr(double)`, `parseRate`, `rateLabel`, `rateFromBeats` |
| `defaultInst`, `mergeInst`, `normalizeInst` | `core/Inst`: `Inst defaultInst()`, `Inst mergeInst(const Inst&, const InstPatch&)`, `Inst normalizeInst(Inst)` |
| `F32_EPS`, `travelSpeed`, `travelSpeedToV`, `V_FWD_MIN`, `V_BACK_MAX`, `refreshSmooth` | `core/Travel` (one copy, used by model and engine) |
| `bendShape`, `gShape` | `core/Curves` |
| `noteName`, `inScale`, `snapScale`, `SCALES`, `NAMES` | `core/Music` |
| `mkNode`, `flat`, `nodesOf`, `normalizeNodes`, `cloneNodes`, `nodesSorted`, `tangents`, `evalSeg`, `segIndex`, `evalNodes`, `sampleNodes`, `rdpIdx`, `rdp`, `capNodes`, `sliceNodes`, `reverseNodes`, `flipNodes`, `scaleNodes` | `model/Node.h`, `model/Nodes.cpp`: `struct Node { double t, v; NodeShape shape; double bend; }`, `using NodeList = std::vector<Node>`; functions keep their names |
| `defaultSample`, `mkNote`, `newClip`, `cloneNote`, `cloneClip`, `noteLane`, `timeCmp`, `timeOrder` | `model/Clip`: `struct Gen`, `struct StartSpec`, `struct NoteSample`, `struct Note`, `using NotePtr = std::shared_ptr<const Note>`, `struct Clip { int bars; int beatsPerBar = 4; double offsetBeats = 0; bool loop = true; bool tieAcrossLoop = true /* F3; the engine honours it from M8; parity tests set false */; std::vector<NotePtr> notes; }`. `Note` also holds `uint64_t serial` (unique for the process, never reused: the `SnapshotCache` key) and `std::shared_ptr<const NoteExtras> extras` (null unless a newer CLIP gave the note content this version does not know, section 3). `kMaxClipNotes = 4096`, `kMaxClipBytes = 32 MiB` (section 3 limits). |
| `editNotes`, `idSet`, `selected`, `uniqueId`, `addNote`, `removeNotes`, `updateNotes`, `duplicateNotes` | `model/ClipEdit` (copy-on-write: untouched `NotePtr`s are shared between versions) |
| `normalizeSample`, `normalizeGen`, `makeGen`, `normalizeNote`, `normalizeClip` | `model/Normalize` (input: a `core::JsonValue`, so v3 saves, v4 saves and fixture clips all go through the prototype's own rules) |
| `genEval` (closure + one-step cache), `genOffset`, `tableLength`, `laneTable`, `setGen`, `clearGen`, `bakeGen`, `reroll` | `model/Generators`: `class GenEval` (holds the cache), `std::vector<float> laneTable(const Note&, LaneId, double res)` |
| `SHAPE_KEYS`, `SHAPE_NAMES`, `patternNodes`, `seededList`, `fitNodes`, `shapeNodes`, `speedShape`, `pathShape`, `shapeKeyKind`, `shapeKeysFor`, `applyShape` | `model/Shapes`: `enum class ShapeKey`, same functions |
| `grainBase`, `mapLane`, `formatLane` (+ `fmtMag`, `fmtSemis`, `fmtDb`, `trimNum`) | `model/LaneFormat` (returns `std::string`; uses `jsToFixed` so readouts are character-identical) |
| `connect`, `disconnect`, `walk`, `chop`, `laneEdit`, `reverseLane`, `flipLane`, `scaleLane`, `selectSimilar`, `humanize`, `strum`, `legato` | `model/Tools` |
| `applyNotePatch`, `setNoteSample`, `getMixed`, `setLaneNodes`, `setLaneNodesMany` | `model/NoteSettings` |
| `startSemis`, `endSemis`, `glideD`, `computeGlides`, `buildChains` | `model/Glides`: results kept in an insertion-ordered map (JS `Map` order is part of `buildChains`' tie-break) |
| `compileSnapshot` | `model/SnapshotCompiler`: `std::unique_ptr<core::Snapshot> compileSnapshot(const Clip&, int res = 256, const SnapshotCache* reuse = nullptr)` |
| `History` (splitState/joinState/sameState) | `model/History`: `template<class State> class VersionHistory` (200 steps, labels, push ignores an identical state). States hold `shared_ptr` versions, not text. |
| `STRAIGHT`, `RECIPES`, `applyRecipe`, `DICE_FIELDS`, `dice` | `model/Recipes` |
| `SOUND_PRESETS`, `applySoundPreset`, `SOUND_BASE`, `presetBuilder`, `strandDemo`, `clipPresetList`, `CLIP_PRESETS`, `loadClipPreset` | `model/Presets` (the 12 clip preset builders are ported line by line; fixtures check every node) |
| `freshId` (Date.now salt) | **not ported.** Every C++ note gets its id from `addNote`/`uniqueId`/presets, so ids, and the random values hashed from them, are reproducible. |

**sl-engine.js (`createStrandEngineModule`)**

| JS | C++ |
|---|---|
| `hash32`, `rand01`, `idHash`, `rateBeats`, `travelSpeed`, `gShape`, `LANE_IDS`, `LANE_DEF` | the `core/` functions above |
| `SALT_JUMP 0x5ca7`, `SALT_SKIP 0x51c1`, `SALT_SPRAY 0x5b7a`, `SALT_DRIFT 0x0d71`, `SALT_TINY 0x7177`, `SALT_START 0x57a7`, `HEAD_TUNE`, `CTRL`, `MAX_HEADS`, `VOICE_GRAINS`, `CLOUD_GRAINS`, `LOCK_MAX_N`, `BUDGET_48K` | `engine/EngineConfig.h` |
| `defaultInst`, `normInst` | `core::defaultInst`, `core::clampForEngine` (the engine's own clamps, kept separate from the model's) |
| `HANN`, `FADE`, `hannTab`, `fadeIn` | `engine/Tables` |
| `readAbs` (+ the inline Hermite fast paths in runTape/runGrains/runLock) | `engine/SourceRead.h`: `template<class S> void readAbs(const S&, double x, double& l, double& r)`; the fast paths are kept inline and must equal `readAbs` exactly, as in JS |
| `readReg`, `seamRho`, `grabPos`, `mapPos` | `engine/Region` |
| `Biquad` | `engine/Biquad.h` |
| `Head` | `engine/Head.h` (`std::array<double, 8>` grain slots instead of `Float64Array(8)`) |
| `Voice` constructor, `setNote`, `switchTo`, `release`, `steal` | `Voice::init(...)` (in place; the pool never allocates), `setNote`, `switchTo`, `release`, `steal` |
| `Voice.tick` | `VoiceTick.cpp: Voice::tick(int64 kAbs, bool ff)` |
| `render`, `renderRun`, `runTape` | `VoiceRender.cpp` |
| `runGrains` | `VoiceGrains.cpp` |
| `lockGrab`, `lockRho`, `runLock` | `VoiceLock.cpp` |
| `chase`, `ffAdvance`, `ffChain` | `VoiceChase.cpp` |
| `Engine` constructor | `Engine::prepare(double sampleRate)` (allocates the 64-voice pool once) |
| `setSample`, `setInst`, `setSnapshot`, `setTransport`, `noteOn`, `noteOff`, `allNotesOff`, `setController` | same names on `Engine` (`setController` split into `setController(Ctrl, double)` and `releaseController(Ctrl)` for the JS `null`) |
| `uAt`, `loopBeat`, `songBeat`, `kOf`, `effLen` | `Engine` private, `int64` sample counters, `double` beats |
| `addVoice`, `scanStarts`, `startClip`, `doChase` | `EngineSchedule.cpp`. `scanStarts` finds its candidates by binary search on the snapshot's start-sorted notes (for each loop pass `m` in the window, the notes with `s` in `[u0 - m Lb, u1 - m Lb]`), so it costs O(log N + starts) per 32-sample segment instead of walking every note (prototype: 2.5 % of a core at 96 kHz with 1024 notes, about 11 % with 4096); the starts and their order are unchanged. It writes into a fixed `std::array<StartEvent, 256>`: if more than 256 starts fall in one segment it keeps the 256 earliest by `(kS, s, p)`, starts the rest at the next segment's first sample and counts them in `stats().deferredStarts` (tested with 300 simultaneous starts). |
| `plan`, `measure` | `EnginePlan.cpp` (every `filter(...).sort(...)` becomes a fixed array + `rt::stableInsertionSort`; the wait list holds (head, voice) pairs, so it is sized `kVoicePool x kMaxHeads` = 512) |
| `process` | `Engine::process(float* L, float* R, int n)` |
| `getDisplay`, `stats`, `resetStats` | `fillDisplay(DisplayFrame&)`, `stats()`, `resetStats()` |
| `strandWorkletGlue` (messages, 30 Hz display posts) | `StrandLockProcessor::processBlock` + `host/HostSync` + `host/MidiRouter` + `doc/CommandQueue` + `DisplaySlot` |

**sl-sounds.js (`SLSounds`)**: `sounds/StarterSounds`: `kStarterList[8]` (sawopen first, D9), `makeStarterSound(StarterId,
int seed = 1)`, and private ports of `pitchedLoop`, `renderPartials`, `levelPower`, `spread`, `harmonicSound`,
`riseAndReturn`, `lp2mag2`, `makeSaw` ... `makeNoise`, `addDecaying`, `highpass`, `addCirc`, `taper`, `kick`, `snare`,
`hat`, `finish`. **SLSounds has its own `hash32`** (`0x9e3779b9` chained `fmix32`) and `noiseAt`; port them as
`sounds::hash32` / `sounds::noiseAt`, never the model's MurmurHash.

**sl-analysis.js (`SLAnalysis`)**: `analysis/`: `detectPitch` (YIN, 5 loudest 2048 frames, the integrator's
half-periodic rule and the 10-frame fallback), `detectKey` (Goertzel chroma + Krumhansl-Kessler), `suggestParts` with
`features`, `estimateTempo`, `overlapOk`, and the prototype's own radix-2 `fft` (not juce::dsp, so results match).

**UI modules** (behaviour to port, not code): `sl-ui.js` (`SLApp`: roll geometry, drawing, gestures, menus, strip,
keys, hint bar) -> `ui/roll/*`, `ui/strip/*`, `ui/KeyRouter`, `ui/HintBar`; `sl-ui-panels.js` (`SLPanels`: selection
bar, drawers, inspector, note settings) -> `ui/roll/SelectionBar`, `Drawers`, `Inspector`, `ui/tabs/NoteSettingsTab`;
its pure helpers (`fmtPos`, `parsePos`, `parseBeats`, `parsePitch`, `laneText`, `laneValueFromText`, `simulate`,
`thumbAmps`) port exactly and are fixture-tested; `sl-ui-sample.js` (`SLSampleUI`: Sample tab, knobs, recipes, macros,
preset menus, part chips) -> `ui/tabs/SampleTab`, `MacrosTab`, `ui/strip/PartChips`, and its exported pure part
(`contexts`, `fades`, `grainKind`, `headsMulti`, `toneAllOpen`, `randomInUse`, `macroText`, `offsetText`,
`suggestEngine`, `pickPart`, `parseNum/Ms/Pct`, `KNOB_MATH`, `RULES`, `kindOf`) -> `ui/SampleTabRules`, fixture-tested.

### 1.3 Threads and data flow

```
 message thread (UI, host param edits)            compiler thread              audio thread (processBlock)
 ------------------------------------            ---------------              ---------------------------
 ClipDocument: ClipPtr current (immutable)  -->  SnapshotPublisher:      -->  snapshots.acquire() -> Engine::setSnapshot
   every edit = new Clip version                   compileSnapshot(clip)        samples.acquire()   -> Engine::setSample
   EditHistory (VersionHistory<DocState>)          snapshots.publish(...)       ParamBridge::readInst(atomics) -> setInst
 SampleManager (worker thread): decode,                                         HostSync::read(playhead) -> setTransport
   starter sounds, analysis -> samples.publish                                  CommandQueue (UI -> audio): audition,
 Timer 30 Hz: DisplaySlot.takeLatest, DiagLog                                    let-go, preview, all-off
   drain; 10 Hz: pools collect()                                              MidiRouter + HostSync: split block at
                                                                                events and cycle wraps
                                                                                Engine::process(segment)
                                                                                snapshots/samples.setInUse(the engine's
                                                                                  oldest epochs still read)
                                                                                DisplaySlot.write (~30/s), DiagLog.push
```

- **Immutable versions.** A `Clip` holds `std::vector<NotePtr>`; an edit copies only the notes it changes (the JS
  copy-on-write rule). Undo states and the audio thread's snapshot come from the same versions: one copy of the truth
  (Z2, risks 7.1).
- **`rt::VersionExchange<T>`** (used for `Snapshot` and `SampleData`). A voice keeps raw pointers into the version it
  plays (its note's tables, chain and glide; a stolen voice's `SampleData`) through its release: the prototype's
  `setSnapshot` releases a voice whose note was deleted but leaves it reading the old note for its 60 ms release. So a
  version may be freed only when no voice reads it any more, and the audio thread, not `acquire()`, says which:
  - `publish(std::unique_ptr<T>)` (message/compiler side): stamps `epoch = ++publishCount`, adds it to the pool
    (`std::vector<std::unique_ptr<T>>`, message-side mutex; the audio thread never touches it), then
    `T* old = mailbox.exchange(p, acq_rel)`. If `old` is not null the audio thread never took it, so it is freed at
    once. While the host is not calling `processBlock` (stopped, or Cubase's "suspend when no audio" option), edits
    keep replacing the one waiting version: nothing piles up, and the first block after Play takes the newest edit.
  - `acquire()` (audio, once per block): `T* p = mailbox.exchange(nullptr, acq_rel); if (p) current = p;`, then
    `Engine::setSnapshot(current)` / `setSample`. Lock-free, no refcount.
  - `setInUse(e)` (audio, after `Engine::process`): `inUse.store(e, release)` with `e = engine.minEpochInUse(kind)`:
    the lowest epoch among `current` and every voice that is not done. Each `Voice` records the snapshot epoch and the
    sample epoch it reads; `setSnapshot` moves a voice whose note survives to the new epoch, while a voice whose note
    was deleted, or that is fading after a sample swap, keeps its old epoch until it is done.
  - `collect()` (message thread timer, 10 Hz): frees every pooled `T` whose `epoch < inUse`. Epochs only grow and a
    voice never moves to an older version, so a freed version can never be read again. While edits stream in, the
    pool holds the versions taken since the oldest releasing voice started its release (about 70 ms); after 100 ms
    with no publish and no releasing voice it holds 1.
  - While the processor is not prepared (before `prepareToPlay`, after `releaseResources`) the audio thread is known
    to be idle, and the message thread swaps directly.
  - **Not** `std::atomic<std::shared_ptr>` (it locks on MSVC, risks 7.2), and the audio thread never drops the last
    reference to anything.
- **Snapshot tables are shared between versions.** `Snapshot` keeps `std::shared_ptr<const NoteTables>` per note (the
  10 Float32 tables); the compiler reuses the previous snapshot's tables for every note it has seen before
  (`SnapshotCache` keyed by `Note::serial`: a pointer can be reused by a new note once a drag's intermediate version is
  freed, a serial never is; the cache keeps only the serials of the last compiled clip). Refcounts change only on the
  compiler/message threads. The audio thread reads raw `const float*` + length.
- **Sample swaps (C++ change, 8.2 #23).** `Engine::setSample` steals every voice (5 ms fade) as the prototype does,
  but each stolen voice finishes its fade reading the `SampleData` it was playing. (The prototype switches them to
  the new source at full gain and only then fades: a 220 -> 330 Hz sine swap jumps 1.57 times the steady step, the
  saw to the pad 10.9 times, a click when Kalan drops a sound in while playing.) The old `SampleData` is freed by
  `collect()` once no voice holds its epoch.
- **Parameters** live in APVTS atomics. `ParamBridge::readInst` builds a POD `core::Inst` at the start of every block
  (non-finite host values fall back to defaults, as GrainLock's `readParameters`). The non-parameter inst fields the
  audio thread reads (`seed`, the mod-wheel targets and CCs, the exact Region edges; section 2.2) are atomics in the
  processor, written by the document; key, scale and scale lock are UI-only.
- **UI -> audio commands** (`doc/CommandQueue`: `rt::SpscFifo<UiCommand, 256>`): audition note on/off (piano strip,
  hold-on-waveform with `startPos`), "Let go" of a wheel, all notes off, Preview start/stop, MIDI-learn arm.
- **Rule: no automatable parameter may need a snapshot recompile** (section 1.4), so automation, including Cubase's
  offline export, takes effect on the audio thread alone. The message thread can lag without changing the sound.
- **Threads**: message, audio, `SnapshotPublisher` (one `juce::Thread`, waits on an event, compiles the newest clip
  version only), `SampleManager` worker (decode, starter sounds, analysis, hashing, library copy, embed encoding).

### 1.4 The snapshot in C++ (BLUEPRINT 3.2, extended)

`core/Snapshot.h`:

```cpp
struct LaneTab   { const float* data; int32_t len; };          // len >= 2; a flat lane is stored as 2 equal values
struct StartSnap { uint8_t mode; double pos, range; int32_t seed; };   // region|carry|song|random, start.seed
struct GlideSnap { bool present; double d, curve; int32_t toIndex; };  // len is resolved at the tick (below)
struct ChainSnap { int32_t firstIndex, index, prevIndex, nextIndex; }; // -1 = none
struct NoteSnap {
    uint32_t idHash; int32_t idIndex;          // id string in Snapshot::ids; idHash = FNV-1a (UTF-16)
    double p, s, l, vel;
    int8_t engine, lock, motion, tape, rev;    // -1 = follow the instrument; resolved by the engine every tick
    uint8_t travel;                            // 0 speed, 1 path (left as the note's kind, even on Tape)
    double jumpBeats;                          // NaN = follow inst.jump
    bool restart;
    StartSnap start;
    double glide;                              // the note's own glide length (beats), NaN = follow inst.glideTime
    GlideSnap glides[3]; ChainSnap chains[3];  // one per glide mode: [0] off, [1] legato, [2] always
    LaneTab lanes[10];                         // registry order; drawing + generator, Float32, 0..1
};
struct Snapshot {
    uint32_t version = 1; int32_t bars, beatsPerBar, res; double offsetBeats; bool loop, tieAcrossLoop;
    std::vector<NoteSnap> notes;               // sorted by (s, p, id) exactly as compileSnapshot
    std::vector<std::string> ids;              // UTF-8
    std::vector<std::pair<uint32_t, int32_t>> byHash;   // sorted (idHash, index) for voice follow-up by id
    std::vector<std::shared_ptr<const NoteTables>> tables;
    uint64_t epoch, clipVersion;
};
```

- **What changed from the JS snapshot and why.** JS bakes the instrument's defaults into each note (`engine`, `lock`,
  `motion`, `tape`, `rev`, `jump`, glide `len`) and compiles chains for the current `glideMode`; the page recompiles
  when one of those inst keys changes (`COMPILE_KEYS` in sl-ui.js). In the plugin those are automatable parameters, so
  the snapshot keeps "follow" markers and all three glide-mode variants, and the engine resolves them at each tick:
  `engine = X.engine < 0 ? inst.engine : X.engine`; `lock = engine == cloud && (X.lock < 0 ? inst.lock : X.lock)`;
  `motion`, `tape`, `rev` the same way; `jump = isnan(X.jumpBeats) ? rateBeats(inst.jump) : X.jumpBeats`;
  glide `len = min(max(0, isnan(X.glide) ? inst.glideTime : X.glide), X.l * 0.95)`; glide and chain from
  `glides[inst.glideMode]` / `chains[inst.glideMode]`. `core::resolve(const NoteSnap&, const Inst&)` does this once,
  and both the engine and the snapshot fixture test call it. With a fixed inst the result is identical to the JS
  snapshot (the fixture test proves it).
- `chain.id` (the chain's first note) becomes `firstIndex`; the engine hashes `ids[firstIndex]` for drift and tiny
  head offsets (`idHash(chain.id)`), exactly as JS.
- Flat tables (every value equal) are stored as 2 values. The engine's lookup `tb[i0] + f * (tb[i1] - tb[i0])`
  returns the same float either way, so this saves memory without changing a sample.
- `notes` past the clip end are kept (the engine skips them when looping), as in JS.
- `beatsPerBar`, `offsetBeats`, `loop` and `tieAcrossLoop` are clip settings (section 3), so they travel with the
  snapshot. With `beatsPerBar` 4, `offsetBeats` 0, `loop` on and `tieAcrossLoop` off (what every parity test
  sets) the engine behaves exactly like the prototype.

### 1.5 The display slot (the worklet's `{type:'display'}` messages)

`engine/DisplayFrame.h`, plain data, filled on the audio thread by `Engine::fillDisplay` (a port of `getDisplay` +
`stats`, writing into the frame instead of allocating):

```cpp
struct DisplayHead  { uint8_t index; int8_t dir; bool thinned; float pos /*0..1 of the file*/; float level; };
struct DisplayVoice { uint32_t noteIdHash; int16_t p; uint8_t kind /*clip, live*/, engine /*tape, voice, cloud, lock*/,
                      motion; bool released; float env; uint8_t numHeads; DisplayHead heads[8]; };
struct DisplayGrain { float pos, age; };
struct DisplayStats { int32_t voices, load, thinnedHeads, maxLoad, budget, cuts, steals; };
struct DisplayFrame { int64_t sampleTime; double beat; bool playing; uint16_t numVoices; DisplayVoice voices[64];
                      uint16_t numGrains; DisplayGrain grains[128]; DisplayStats stats; float peakL, peakR; };
using DisplaySlot = rt::LatestSlot<DisplayFrame>;   // triple buffer, about 8.5 KB a frame
```

- Written after a block once `max(128, round(sr / 30))` samples have passed since the last write (the worklet's
  rule): the audio thread fills its back buffer and swaps it with the middle one (one atomic exchange, a "new" bit
  set); it never waits and never loses the newest frame. (Not GrainLock's 8-frame `ScopeFifo`: that drops the new
  frame when full, so after a pause the UI would show the frame from when the pause began.)
- The editor's 30 Hz timer calls `takeLatest` (swaps the middle buffer with its front one when the bit is set) and
  maps `noteIdHash` to its own clip's notes. No strings cross threads. Frames written while no editor was open are
  simply overwritten.
- The UI draws heads as numbered orchid lines with direction arrows and grains as dots (H4) from these frames only.

### 1.6 Precision rules (so the C++ plays the prototype's numbers)

1. **Two roundings to Float32, exactly where JS has them.** `sampleNodes` writes `clamp(evalSeg(...), 0, 1)` into a
   Float32Array; `laneTable` then reads that float, adds the generator in double and stores Float32 again. C++:
   `float o = (float) clamp(eval, 0, 1); o = (float) clamp((double) o + gen, 0, 1);`. Starter sounds round once in
   `finish`. Source samples are Float32. Everything else is `double`, as in JS.
2. **Same operation order, no reassociation, no FMA.** StrandCore compiles with `/fp:precise` (MSVC, explicit) and
   `-ffp-contract=off -fno-fast-math` (GCC/Clang, the Linux test job); never `/fp:fast`, `-ffast-math` or `/arch:AVX2`
   on StrandCore. Copy expressions as written (`a + (b - a) * u`, not `a * (1 - u) + b * u`).
3. **The Travel dead band at Float32 precision.** `kF32Eps = 2e-8`. Stopped for `0.48 - 2e-8 <= v <= 0.52 + 2e-8`,
   tested in double on the value the engine actually reads: the Float32 table entries interpolated in double
   (`lv = tb[i0] + (fi - i0) * (tb[i1] - tb[i0])`). Refresh "Smooth" is `v < 0.06 - 2e-8`. `Math.fround(v)` in
   `mapLane`/`scaleLane` is `(double)(float) v`. `kVFwdMin`/`kVBackMax` are computed exactly as JS does (stepping the
   Float32 bit pattern with `std::bit_cast<int32_t>`), so `travelSpeedToV(+-1/8)` returns 0.5200000405 / 0.4799999595.
   The heads snap `if (abs(H - round(H)) < 1e-5) H = round(H)` is kept.
4. **JS integer semantics.** `x | 0` = `toInt32` (ECMAScript ToInt32: truncate, wrap mod 2^32, NaN/inf -> 0), `>>> 0` =
   `toUint32`, `Math.imul` = `uint32` multiply. Every `hash32` argument goes through `toInt32` (grain and refresh
   counters can pass 2^31 in long notes and must wrap like JS). Where JS uses `x | 0` on a positive index it is a
   truncation; where it uses `Math.floor` it is a floor: port each spot as written.
5. **`Math.round` is not `std::round`.** `jsRound(x)`: `r = floor(x); if (x - r >= 0.5) r += 1` (ties toward +inf).
   Used everywhere JS calls `Math.round`.
6. **Strings that feed hashes.** Ids are hashed as UTF-16 code units (store UTF-8, convert for hashing and compare
   with `jsStringLess`). `uniqueId` builds `'add:' + n + ':' + p + ':' + s + ':' + l`, so `jsNumberToString` must
   reproduce `String(number)` (ECMAScript Number::toString: shortest round-trip digits from `std::to_chars`, decimal
   form for 1e-7 <= |x| < 1e21, exponent form otherwise, `-0` -> `"0"`). `toBase36` = `uint32.toString(36)`.
   `jsToFixed`/`jsToPrecision` round the exact binary value with ties to the larger candidate (`0.125.toFixed(2)` is
   `"0.13"`, `std::to_chars` would give `"0.12"`).
7. **Sorting and maps.** JS `Array.prototype.sort` is stable; use `std::stable_sort` with the same comparator (a
   negative result means "a first") off the audio thread, and `rt::stableInsertionSort` on it (the standard one
   allocates), on fixed arrays sized for the largest list: 64 voices, or 512 (head, voice) pairs in `plan()`'s wait
   list. JS `Map`/`Set` iterate in insertion order; use an insertion-ordered container wherever the order is
   observable (`computeGlides`' result feeds `buildChains`' tie-break, clusters in voice leading).
8. **`JSON.parse(JSON.stringify(x))` in the prototype** (deepClone in presets, recipes, mergeInst) turns `-0` into `0`
   and drops `undefined`. Comparisons in fixtures treat `+0 == -0`.
9. **Transcendentals.** `std::sin/cos/exp/pow/log2/log10/tan` may differ from V8 (whose `Math.*` are fdlibm ports) by
   one ulp. Results are identical after the Float32 rounding in almost every case and audibly identical always; the
   fixture tolerances (section 5) allow for it. Use `std::pow(2.0, x)` where JS has `Math.pow(2, x)` (not `exp2`). If a
   fixture shows a discrete decision flipping because of an ulp, port fdlibm's routine for that one function into
   `core/JsMath.h` (opt-in, `STRANDLOCK_FDLIBM`) rather than loosening the tolerance.
10. **Output buffer semantics.** The prototype renders into Float32Arrays: every `out[j] += x` reads a float, adds in
    double, stores a float. `Engine::process(float* L, float* R, int n)` does the same, voice by voice in the voices'
    insertion order, then the limiter. That makes the C++ accumulation bit-compatible with the worklet.
11. **Denormals.** The plugin runs under `juce::ScopedNoDenormals`; parity tests run under it too. A flushed
    subnormal differs by < 1e-30 from JS; fixture comparisons absorb it.
12. **Parameter floats.** APVTS stores floats, so in the plugin an inst value such as drift 0.3 becomes
    0.30000001192. Parity tests set `core::Inst` directly in double. Elsewhere the difference is <= 6e-8 relative and
    flips a seeded decision (for example Skip's `rand01 < skip`) with probability < 1e-8 per decision. Two
    consequences. (a) Every test that passes a value through a parameter compares it with `(double)(float) x`, the
    value APVTS holds (velSens 0.7 reads back as 0.699999988), never with `x`; each such test line says so. (b) The
    Region edges are fractions of files of up to 28.8 million frames, where one float32 step is up to 3.4 frames, so a
    one-bar loop part would be a few frames off and drift from the transport over many passes. StrandLock keeps the
    exact f64 edges it set itself in plugin state and plays them while the parameter still holds `(float)` of that
    edge; host automation that moves the parameter away plays the parameter's value (section 2.2).

### 1.7 Seeding rules (must give the prototype's random values bit for bit)

Integer hashing is exact in C++, so every random choice matches the prototype as long as these inputs match:

| Where | Value |
|---|---|
| any id | `idHash(id)` = FNV-1a 32 over UTF-16 code units, offset `0x811c9dc5`, prime `0x01000193` |
| `hash32(a, b, ...)` | MurmurHash3 x86_32 over int32 args, seed `0x9747b28c`, `h ^= 4 * argc`, `fmix32` |
| generator random (Random, Random steps, Smooth random) | `rand01(perNote == different ? hash32(idHash(gen.key ?? note.id), laneIndex, k, gen.seed) : hash32(laneIndex, k, gen.seed)) * 2 - 1`; `k = floor(phase + 1e-9)`; Re-roll = `gen.seed + 1` |
| shape `random` | `rand01(hash32(idh, laneIndex, i, seed, 0x5a9e))` (`idh = idHash(note.id)` for Each, 0 for Across) |
| `patternNodes` random branch | `seed = hash32(seed) % 10000`, then `seed = (seed * 9301 + 49297) % 233280` per node (int64 in C++) |
| Connect Random | `rand01(hash32(idHash(n.id), idHash(m.id), seed))` |
| Humanize | `rand01(hash32(idHash(n.id), k, seed)) * 2 - 1`; k = 1 time, 2 velocity, 10 + laneIndex lanes |
| Dice | `rand01(hash32(0x0d1ce, i, j, seed))`, i = 1..19 per field |
| Chop / Duplicate / Add ids | `'n' + toBase36(hash32(idHash(base), k, salt++))`, base `'chop:' + id`, `'dup:' + id + ':' + off`, `'add:' + count + ':' + p + ':' + s + ':' + l` |
| normalizeNote missing id | `'n' + toBase36(hash32(i, jsRound(p*100), jsRound(s*1e6), jsRound(l*1e6)))` |
| engine: random Start | `rand01(hash32(nh, 0x57a7, start.seed, inst.seed))`; Re-roll moves it by `start.seed + 1` or `inst.seed + 1` |
| engine: Scatter jump | `rand01(hash32(nh, head, jIdx, inst.seed, 0x5ca7)) * L`; `jIdx = floor(beat / jumpBeats + 1e-9)`: the clip loop beat for clip notes, and for live keys while the host plays; the free clock only for live keys while the host is stopped (`songBeat`) |
| engine: Refresh skip | `rand01(hash32(nh, head, refIdx, inst.seed, 0x51c1)) < skip` |
| engine: Spray | `rand01(hash32(nh, head, h.gIdx, inst.seed, 0x5b7a)) * 2 - 1` (Lock grabs too) |
| engine: Drift | `rand01(hash32(vh, head, 0x0d71, inst.seed)) * 2 - 1`, `vh = idHash(ids[chain.firstIndex])`, else `nh` |
| engine: tiny head offsets | `rand01(hash32(vh, head, 0x7177, inst.seed)) * 0.008` s for heads 2..8 |
| live keys | note id `"live:" + p` (so `nh = idHash("live:60")`) |

`nh` is `toInt32(idHash)`. Argument orders differ between salts: copy them exactly. The loop pass is never an input,
so every pass and every export plays the same (Y7). `inst.seed` is plugin state (section 2.4), changed only by Re-roll.
Starter sounds use SLSounds' own hash (section 1.2) with `seed = 1`.

### 1.8 Voices, heads and the budget (D3)

- **Voice pool:** 64 preallocated `Voice` objects (32 playable + room for 5 ms steal tails), each with 8 `Head`s,
  each head with 8 grain slots and two Lock captures. `inst.polyphony` (1..32, default 16) counts voices that are not
  stealing; the oldest (released first) is stolen with a 5 ms fade. If all 64 objects are busy, the oldest stealing
  voice is cut at once and counted in `stats().cuts`. Voices are kept in insertion order (an index list compacted in
  place), because the output sum and the planner follow that order.
- **Envelope:** 8 ms attack, 60 ms release, 5 ms steal, 10 ms chase fade-in. A chain plays on one voice: no new
  attack; playhead, grain phase and vibrato phase carry on (D2). `restart` breaks the chain.
- **Heads:** `H = clamp(inst.heads + (heads lane - 0.5) * 14 + (macro - 0.5) * 14, 1, 8)`, fractional (2.5 = two full
  heads and a third at half level), level `1/sqrt(H)`, spread offsets `i/H * spread` of the Region approached over
  20 ms (at most 0.2 frames per frame), pan `+-(spread * 0.75)`, head gains slew-limited to full scale in 20 ms.
- **Budget:** `B = max(16, floor(192 * 48000 / sr))` load units per sample (208 at 44.1 kHz, 192 at 48, 96 at 96 kHz).
  Units: Voice and Cloud = active grains; Lock = taps (N, or N + N' during a re-grab crossfade); Tape = 1 per head (2
  during a jump or stutter crossfade). `plan()` runs at every 32-sample boundary of the control grid and at every note
  start; steady heads are planned to `floor(0.75 B)` so 20 ms fade tails fit under B.
- **Control grid (C++ change, 8.2 #21).** The prototype counts the grid of `plan()` (and of the segment splits) from
  the engine's creation, so under budget pressure the same song differs with how many samples ran before Play
  (measured: up to 0.040 between Play after 0 and after 7 stopped samples, 8-head Voice notes). The plugin counts it
  from the sample where the transport last started (stopped -> playing): two renders that start playing at the same
  song position are identical, whatever ran before (risks 4.5). Jumps and wraps do not move it; while stopped it
  keeps its last origin. The `ramp20ms` smoothers step on the same grid.
- **Thinning fades, never cuts:** over budget, the top (fractional first) heads of other voices fade out over 20 ms,
  released voices first, then silent ones (level x env < 1e-3), then the oldest; the newest note never thins unless it
  alone exceeds B. A head that has not sounded yet waits (at most 20 ms) for the tails to leave room and fades in over
  20 ms; a new voice's first head never waits. Last resort only: cut the oldest tails (`stats().cuts`).
- **What the prototype does at the edges (the M3 criteria follow it).** With Lock N = 12, one voice's 8 heads cost
  the whole 48 kHz budget, so 16 quick note-ons cut fade tails (10-15 cuts in the fixtures), and at 96 kHz (B = 96)
  the newest Lock note plays 4 of its 8 heads (open question 8). With Voice and Cloud arpeggios and Lock Grain 4 the
  prototype measured 0 cuts.
- **Grain caps:** 8 grains per Voice head (above that grains get shorter, never dropped: spacing is the pitch),
  5 per Cloud head, Lock N <= 12 (8 heads x 2 captures x 12 = 192).
- **Chase cost (C++ change, 8.2 #22).** `chase()` replays every 32-sample tick from the chain's first note to now
  (JS: up to 2^17 ticks per voice), for every sounding note, inside one block. On the prototype a Play inside 16 held
  4-bar pads (Tape, 8 heads) spends about 270 ms in one 256-sample block (5.3 ms long), 16 Voice notes of 64 beats
  over 1 s: Cubase would drop out at every Play and locate. The plugin bounds it by samples, never by measured time,
  so block sizes still never change the output:
  - ticks more than 4096 ticks before the chase point are coarse: `tick` once per 16, then `ffAdvance(512)`;
  - one chase moment (a transport start, jump or wrap) runs up to `kChaseTicksNow = 4096` ticks at once, shared by its
    voices in `doChase` order: a chase whose voices together need at most that (one voice up to about 2.7 s into its
    note at 48 kHz) runs at once, exactly as the prototype (every fixture case does);
  - the rest runs at `kChaseTicksPer32 = 256` ticks per 32 output samples, on the control grid. A voice still chasing
    is silent and counts as waiting for the planner; once it has caught up with the current sample it starts its
    10 ms fade-in as the prototype does. The worst case above sounds about 0.3 s after Play instead of dropping out
    (open question 9). `stats().chaseTicks` counts the work.
- **Health counters.** The output loop replaces a non-finite sample with 0 and clamps to +-0.98, and the biquads
  reset on NaN, so a broken voice would pass as silence. `stats()` therefore also counts `nonFinite` (samples the
  output guard replaced, before the limiter) and `filterResets` (biquad NaN resets), and keeps the pre-limiter peak;
  every stability test requires nonFinite = filterResets = 0, a pre-limiter peak above 0.05 and an output rms above
  0.01.

### 1.9 Where the Scan / Travel path lives

PROMPT reserved a future "Scan lane"; v4 made it the Sample lane group (Travel, Heads, Grain, Refresh), the Region and
the per-voice heads. In C++:
- **Mapping** (`v -> signed speed`, dead band, `travelSpeedToV`): `core/Travel`.
- **Drawing tools** for Travel (Tape stop, Tape start, Reverse halfway, Rewind for the Speed kind; Play through, Play
  through backwards, Fit for the Path kind): `model/Shapes` (`speedShape`, `pathShape`, `fitNodes`).
- **Per-note kind** (`NoteSample::travel`, `speed` default, D4) and Start (`region|carry|song|random`): `model/Clip`,
  resolved in `core/Snapshot` (`travel`, `start`).
- **The moving playhead:** `engine/VoiceTick.cpp`, `Voice::tick`: Speed kind sets each head's speed to
  `travelSpeed(lane) * inst.speed * 2^((macro1 - 0.5) * 4) * direction(motion, head) * (1 + drift * 0.25 * r)` (x pitch ratio in
  Tape, capped +-16 / +-64); Path kind steers head 1 to `lane * L` (`h.spd = (pathPos + h.scat - h.raw) / 32`); Tape
  treats Path as Speed at 1x and Freeze as Forward; Scatter jumps at grid lines; Refresh holds or stutters.
  `engine/Head.h` holds `raw + off` (frames from the Region start).
- **The edges:** `engine/Region` (`readReg` loop with post/pre seam fades and GrainLock's correlation correction,
  `mapPos` fold for Bounce, clamp for Hold/Stop, `grabPos` unwrapped reads for grains in loop mode).
- **What the user sees:** `ui/strip/HeadsOverlay` (from `DisplayFrame`), chevrons and thumbnails in `ui/roll`.

### 1.10 Host sync and MIDI

`host/HostSync` turns `AudioPlayHead::PositionInfo` into the engine's `setTransport` once per block (and again at a cycle wrap inside it):
- **Playing:** `beat = ppq - clip.offsetBeats` (PROMPT's "Offset bars", stored in beats), `bpm` from the host (120 if
  missing or not in 1..1000), `loop = clip.loop`. The engine keeps the prototype's rules: a beat more than 0.1 beat
  away from its own clock is a jump (clip voices release, then chase at the new place); a smaller difference with an
  unchanged tempo changes nothing (absorbs Cubase's +-0.003 beat cycle jitter, risks 4.2). A host cycle that is a
  whole number of clip lengths is seamless by construction (the engine compares beats modulo the clip length).
- **Tempo changes (C++ change, 8.2 #3).** The prototype re-anchors to its own clock at a tempo change, which drifts
  from Cubase over a tempo ramp (240 samples over 120 -> 60 bpm in 8 bars, simulated). The plugin re-anchors to the
  host: at a tempo change with `|d| <= 0.1`, where `d = beat - loopBeat(cur)` reduced modulo the clip length exactly
  as the jump test computes it (`d -= Lb * round(d / Lb)` with loop on), the new anchor is `anchorBeat = cur + d` at
  the block's first sample `k0` (never the raw host beat: after a wrap that would move the unwrapped clock back by
  whole clip lengths). `scanHorizonU` is the unwrapped beat up to which starts were already emitted (the old anchor's
  `u` at `k0`). If the anchor moved back (`d < 0`), no note with `U < scanHorizonU` starts again. If it moved forward
  (`d > 0`), every note with `U` in `[scanHorizonU, u_new(k0))` starts at `k0` through `startClip`, `d` beats into
  itself (no fast-forward: `d <= 0.1` beat). Without that rule a downbeat whose wrap Cubase reports 0.0028 beat late
  is dropped (simulated: it started in 1 of 4 passes).
- **Cycle wraps inside a block.** HostSync reads the loop points: when the host reports looping and
  `ppq + blockBeats` crosses `ppqLoopEnd`, it splits the block at `ceil((ppqLoopEnd - ppq) / beatsPerSample)` and calls
  `setTransport` there with `beat = ppqLoopStart - offsetBeats` (MidiRouter already splits blocks; events keep their
  sample offsets). Cubase split blocks at the cycle end with ASIO-Guard off (risks 4.2); with it on, a wrap can fall
  inside one of its large blocks. The M0 checklist (step 7) asks what Kalan's Cubase does; the split is harmless when
  the host splits anyway.
- **Pre-roll (C++ change, 8.2 #4):** negative beats are never wrapped into the clip: `loopBeat(u) = u < 0 ? u : u -
  Lb * floor(u / Lb)`, and the same rule in `setTransport`'s `wrap()` (the prototype floor-wraps with loop on, which
  plays the end of the clip before bar 1, and uses `max(0, b)` with loop off, which plays the first note 2 beats
  early), in the jump branch's anchor and in `doChase` (a negative `u` chases nothing). So nothing plays before bar 1
  and the first note starts on its exact sample.
- **Stopped:** `playing = false`; the host beat is passed only when it moved by more than 1e-6 since the last block
  (else NaN), so the engine's free clock keeps running for live Scatter and synced Refresh while Cubase is stopped.
- **Preview (U5, M6):** when the host is stopped and Preview is on, HostSync runs its own clock at the host tempo
  (`playing = true`, beat counted in samples from the Preview start).
- **Never a quality switch for offline export** (`isNonRealtime` is shown in diagnostics only; determinism, risks
  4.5). M2's determinism test also renders with `setNonRealtime(true)` and requires bit-identical output, and the M0
  checklist (step 13) compares a normal export with a real-time one.

`host/MidiRouter` walks the block's `MidiBuffer` and renders up to each event's sample offset before applying it, so
events land on their exact sample at any block size: note on -> `noteOn(p, vel / 127.0)` (the engine reads > 1 as
0..127; the integrator's note says send `vel/127`); velocity 0 or note off -> `noteOff`; CC `mod1.cc` / `mod2.cc` ->
`setController(mod1|mod2, value / 127)`; pitch wheel -> bend `(v - 8192) / 8192` below centre and `/ 8191` above (so
full up is exactly +1); CC64 >= 64 -> sustain; CC120/CC123 -> `allNotesOff`; while MIDI Learn is armed the first other
CC is reported to the UI (M8). JUCE's VST3 wrapper turns Cubase's controller parameters back into MIDI (7-bit,
risks 6.4).

### 1.11 CMake and CI

- `CMakeLists.txt` (GrainLock's, adapted): `project(StrandLock VERSION 0.1.0)`, C++20, `CMAKE_MSVC_RUNTIME_LIBRARY
  "MultiThreaded$<$<CONFIG:Debug>:Debug>"`, FetchContent JUCE `8.0.15`, options `STRANDLOCK_BUILD_PLUGIN`,
  `STRANDLOCK_BUILD_TESTS`. Sources by directory glob with `CONFIGURE_DEPENDS` (so adding a file never edits CMake):
  `StrandCore` = `Source/{core,rt,model,engine,sounds,analysis}/*.cpp` (no JUCE, flags from `cmake/StrandFlags.cmake`);
  plugin layer = `Source/*.cpp` + `Source/{params,state,host,doc,diag,ui,probe}/**/*.cpp`; tests =
  `Tests/TestMain.cpp` + `Tests/{support,model,engine,plugin,ui,analysis}/*.cpp` + the plugin layer.
- `juce_add_plugin(StrandLock COMPANY_NAME "Kalan Hatton" COMPANY_WEBSITE "https://github.com/kalanhattonre-bit/StrandLock"
  BUNDLE_ID com.kalanhatton.strandlock PLUGIN_MANUFACTURER_CODE Khtn PLUGIN_CODE Stlk FORMATS VST3
  PRODUCT_NAME "StrandLock" IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Instrument Sampler)`; no input bus,
  stereo output only, latency 0.
- Definitions: `JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1
  JUCE_USE_MP3AUDIOFORMAT=1` (the same mp3 decoder on Windows and the Linux tests). Modules:
  `juce_audio_processors`, `juce_audio_formats`, `juce_audio_utils`, `juce_gui_extra` (no `juce_dsp`: the analysis
  uses the prototype's own FFT).
- `build.yml` = GrainLock's jobs with StrandLock names, run on every pushed branch (`branches: ['**']`, as
  `origin/m0-skeleton` already does, so a builder's branch is green or red before any PR) and on `v*` tags. Windows:
  configure, Release build, `StrandLockTests.exe` (all tests, cwd = repo root so `docs/parameters.json` and
  `Tests/fixtures` resolve), `--snapshot ui-snapshots`, `--write-fingerprint fingerprints`, `--write-state-fixtures
  state-fixtures`, uploads (snapshots `if-no-files-found: error`; fingerprints and state fixtures `warn` until M9
  commits them), Debug build + `--only alloc --alloc-minutes 3` + `--snapshot debug-snapshots` (jassert is compiled
  only in Debug, so this is where "no assertion" is proven; not uploaded), pluginval 1.0.4 `--strictness-level 10
  --validate-in-process`, upload VST3, release only on `v*` tags. Linux: dependencies, configure with the plugin off,
  build, run tests (CPU figures printed, gated on Windows only); a second configure with `-DSTRANDLOCK_SANITIZE=ON`
  (`-fsanitize=address,undefined -fno-omit-frame-pointer` on StrandCore and the runner) that runs the `model` and
  `engine` groups (a releasing voice reading a freed snapshot is a crash there, not a wrong sample); and **`node
  tools/make-fixtures.js --check`** (`setup-node` 24, the major the fixtures were made with; the script prints a note
  on any other): the committed fixtures must be what the prototype produces today, and `docs/parameters.json` must
  agree with SLModel. From M9 also `node tools/check-params.js`.

---

## 2. Parameters (frozen at the first release)

The full list, machine-readable, is `docs/parameters.json` (50 entries). The architect writes it, and
`tools/fixtures/params.js` checks it against SLModel on every `make-fixtures.js` run (`--check` on CI included):
defaults, `normalizeInst` clamps, choice lists and their labels, the macros first with `SLModel.MACRO_NAMES`, and every
`defaultInst()` field either a parameter or section 2.2 state. So a change to SLModel that the list does not follow
fails CI. Keys: `index, id, name, type, min, max, default, choices, unit, instField, smoothing` (the required schema)
plus `defaultLabel, automatable, step, skewCentre, display, group, touchedBy, rateIds, note`. A test (M0) builds the
APVTS layout and compares every field with the JSON.

**Conventions.** Plain parameter value = the inst value (0..1 stays 0..1 and is shown as %), except choices (index
into the listed order) and bools. `ParameterID{id, 1}` for every v1 parameter (GrainLock). `skewCentre` = the value
at normalised 0.5 (`NormalisableRange::setSkewForCentre`), `null` = linear. Choice lists never change length after
release; a new option is a new parameter. PROMPT 8.3 ids are kept where the meaning survived and never reused for a
different meaning (`startOffset` and `sustainLoop` are not used; the Region has new ids).
- **Every float parameter is continuous** (`step` 0, NormalisableRange interval 0). JUCE snaps every value, typed and
  imported ones included, to a non-zero interval, while the prototype keeps typed values (to 6 decimals) and
  `normalizeInst` accepts any value in range: a Lock Grain typed as 4.3 plays 4 taps in the prototype but would snap
  to 4.5 and play 5. The knobs snap while dragging exactly as SLSampleUI's `q` functions (`qLin`, `qLog`,
  `KNOB_MATH`) do, in the UI. Int parameters (bendRange, polyphony, rootNote) keep step 1.
- **No choice list that can grow (Z3).** Key, scale, scale lock and the two mod-wheel targets are plugin state
  (2.2), not parameters: the scale list and the lane list will grow (risks 6.1), and the engine never reads key or
  scale. The choice parameters left have lists fixed by BLUEPRINT (engine, motion, head tune, the 21 rates, Refresh
  modes, At the end, Auto Slide).
- **Macro ids are target-neutral** (`macro1` .. `macro8`): D8 and A2 let a macro be pointed somewhere else later
  inside StrandLock without a new parameter, so the frozen id must not name its target. The name is the D8 label
  (Speed .. Formant), which is what Cubase's Quick Controls show; the Sample tab's own knobs are "Sample Speed",
  "Sample Heads", "Sample Spread", "Sample Spray", so the two never read the same. Once reassignment exists (the
  reserved `MACR` chunk), the parameter's `getName` reads the assignment and the processor calls
  `updateHostDisplay(ChangeDetails().withParameterInfoChanged(true))`.
- **Rates are quarter-note beats** (BLUEPRINT 1.5: `1bar` = 4 beats) whatever the clip's `beatsPerBar`, so Jump
  every, Refresh every, Vibrato Sync and generator rates keep their sound when beatsPerBar changes, and saved
  generators and automation never move. The parameter's choice text stays "1 bar" (host lists are fixed); StrandLock's
  own UI reads `1bar`, `1barT`, `1barD` as "4 beats", "4 beats triplet", "4 beats dotted" when beatsPerBar is not 4.

### 2.1 The list (index: id, name, type, range, default, unit -> inst field; smoothing)

| # | id | name | type | range | default | unit | inst field | smooth |
|---|---|---|---|---|---|---|---|---|
| 0 | macro1 | Speed | float | 0..1 | 0.5 | (readout) | macros[0] | ramp |
| 1 | macro2 | Spread | float | 0..1 | 0.5 | (readout) | macros[1] | engine |
| 2 | macro3 | Heads | float | 0..1 | 0.5 | (readout) | macros[2] | engine |
| 3 | macro4 | Refresh | float | 0..1 | 0.5 | (readout) | macros[3] | none |
| 4 | macro5 | Grain | float | 0..1 | 0.5 | (readout) | macros[4] | none |
| 5 | macro6 | Spray | float | 0..1 | 0.5 | (readout) | macros[5] | none |
| 6 | macro7 | Tone | float | 0..1 | 0.5 | (readout) | macros[6] | ramp |
| 7 | macro8 | Formant | float | 0..1 | 0.5 | (readout) | macros[7] | ramp |
| 8 | ampOffset | Volume Offset | float | -1..1 | 0 | (readout) | offsets.amp | ramp |
| 9 | vibOffset | Vibrato Offset | float | -1..1 | 0 | (readout) | offsets.vib | ramp |
| 10 | toneOffset | Tone Offset | float | -1..1 | 0 | (readout) | offsets.tone | ramp |
| 11 | formantOffset | Formant Offset | float | -1..1 | 0 | (readout) | offsets.formant | ramp |
| 12 | hollowOffset | Hollow Offset | float | -1..1 | 0 | (readout) | offsets.hollow | ramp |
| 13 | outGain | Output Gain | float | -60..12, skew -12 | 0 | dB | outGain | ramp |
| 14 | engine | Engine | choice | Tape, Voice, Cloud | Voice | | engine | none |
| 15 | lock | Lock | bool | | Off | | lock | none |
| 16 | motion | Motion | choice | Forward, Backward, Bounce, Mirror, Freeze, Scatter | Forward | | motion | none |
| 17 | speed | Sample Speed | float | 0..16, skew 1 | 1 | x | speed | ramp |
| 18 | heads | Sample Heads | float | 1..8 | 1 | heads | heads | engine |
| 19 | spread | Sample Spread | float | 0..1 | 0.25 | % | spread | engine |
| 20 | grainVoice | Voice Grain | float | 0.25..16, skew 2 | 2 | cycles | grainVoice | none |
| 21 | grainCloud | Cloud Grain | float | 5..2000, skew 140 | 140 | ms | grainCloud | none |
| 22 | grainLock | Lock Grain | float | 1..16, skew 4 | 4 | waves | grainLock | none |
| 23 | spray | Sample Spray | float | 0..1 | 0 | % | spray | none |
| 24 | drift | Drift | float | 0..1 | 0 | % | drift | ramp |
| 25 | pitch | Pitch | float | -48..48 | 0 | st | pitch | ramp |
| 26 | tape | Tape | bool | | Off | | tape | none |
| 27 | reverseGrains | Each Grain Backwards | bool | | Off | | rev | none |
| 28 | headTune | Head Tune | choice | Same, Detune, Octaves, Fifths | Same | | headTune | none |
| 29 | jump | Jump Every | choice | 21 rates (1 bar .. 1/64 dotted) | 1/8 | | jump | none |
| 30 | refreshMode | Refresh Mode | choice | Smooth, Free, Synced | Smooth | | refresh.mode | none |
| 31 | refreshMs | Refresh Time | float | 5..2000, skew 120 | 120 | ms | refresh.ms | none |
| 32 | refreshRate | Refresh Every | choice | 21 rates | 1/16 | | refresh.rate | none |
| 33 | refreshBeatLock | Refresh Lock to Beat | bool | | On | | refresh.lockToBeat | none |
| 34 | refreshSkip | Refresh Skip | float | 0..1 | 0 | % | refresh.skip | none |
| 35 | regionStart | Region Start | float | 0..1 | 0 | % of file | region.start | none |
| 36 | regionEnd | Region End | float | 0..1 | 1 | % of file | region.end | none |
| 37 | atEnd | At the End | choice | Loop, Hold, Stop | Loop | | region.atEnd | none |
| 38 | loopXfade | Loop Crossfade | float | 0..500, skew 50 | 10 | ms | region.xfadeMs | none |
| 39 | glideMode | Auto Slide | choice | Off, Legato, Always | Legato | | glideMode | none |
| 40 | glideTime | Slide Time | float | 0..16, skew 0.5 | 0.5 | beats | glideTime | none |
| 41 | vibRate | Vibrato Speed | float | 0.1..20, skew 5.5 | 5.5 | Hz | vibRate | none |
| 42 | vibSync | Vibrato Sync | choice | Free + 21 rates | Free | | vibSync (new) | none |
| 43 | bendRange | Bend Range | int | 0..48 | 2 | st | bendRange | none |
| 44 | velSens | Velocity Sensitivity | float | 0..1 | 0.7 | % | velSens | engine |
| 45 | polyphony | Voices | int | 1..32 | 16 | voices | polyphony | none |
| 46 | liveLegato | Live Legato | bool | | Off | | liveLegato | none |
| 47 | rootNote | Root Note | int | 0..127 | 60 (C3) | | root | none |
| 48 | rootCents | Root Cents | float | -100..100 | 0 | cents | cents | ramp |
| 49 | bypass | Bypass | bool | | Off | | (host bypass) | ramp |

Rate choices are `SLModel.RATES` in order (`1bar, 1barT, 1barD, 1/2, 1/2T, 1/2D, 1/4, ..., 1/64D`) labelled with
`rateLabel` ("1 bar triplet", "1/16 dotted"). Readouts reproduce the prototype's text (`macroText`, `offsetText`,
`fmtX`, `fmtHeads`, `fmtPct`, `fmtCycles`, `fmtMs`, `fmtSt`, `noteName`); the `display` key in the JSON names each.
Text entry parses what the readout prints (GrainLock's `parseMs`/`parseWhole` plus SLSampleUI's `parseNum`).

Macros first (D8: Quick Controls 1-8), then the five whole-clip offsets (A3), then output, then the Sample tab, Refresh,
Region, playing, tuning, and the host bypass last (kept out of presets, resets and undo). Every parameter is
automatable.

`touchedBy` names what sets a parameter besides the host and its own control: `soundLoad` (loading a sound: a starter
sets root 60, cents 0 and the whole file as the Region), `soundPreset`, `recipe`, `dice`, `detect` (Detect pitch and
an accepted root suggestion), `partChip` (a part chip sets the Region).

### 2.2 Inst fields that are not parameters (plugin state, section 3)

All of these live in the INST chunk and in `DocState::np`, so Undo covers them with the clip.

| Field | Why it is state |
|---|---|
| `seed` | Re-roll's counter (Y7). Every seeded value hashes it, it changes only by Re-roll (one undo step), and automating it would make exports unpredictable. `std::atomic<int32_t>` for the audio thread. |
| `mod1.target`, `mod2.target`, `mod1.cc`, `mod2.cc` | The wheel targets and their MIDI Learn CCs. The target list grows with the lane registry (risks 6.1), so as a choice parameter it would break Z3 (no list that can grow after release); PROMPT 8.3 listed them as parameters (8.1 #27). Stored as the lane-id string ("off" for none): an id this version does not know is kept verbatim for re-save and plays as `normalizeInst`'s default. `std::atomic<int8_t>` lane index (-1 off) and `std::atomic<int8_t>` CC for the audio thread. |
| `key`, `scale`, `scaleLock` | Editing aids (row shading, scale snap); the engine never reads them, and the scale list can grow. Stored as the key number, the scale's name and a flag. UI only. |
| exact Region edges | f64 `regionStart`, `regionEnd` as StrandLock itself last set them (a sound load, a part chip, the strip, a preset, Undo). `ParamBridge::readInst` plays the exact edge while the parameter's float equals `(float)` of it, and the parameter's own value otherwise (host automation), so a part chip's one-bar loop keeps its exact frame length (1.6 #12). `std::atomic<double>` pair. |
| `liveTemplate` (M8) | The id of the note whose lanes shape live keys (A4); empty = none. |
| a second Region, macro assignment lists | Reserved (one Region in v1, pre-wired macros; critique MC1). Reserved chunks, nothing written in v1. |

Everything else in BLUEPRINT 1.4 `inst` is a parameter. Non-inst state (the clip and its settings, the sample
reference, analysis cache, UI state) is in section 3.

### 2.3 Smoothing classes (`smoothing` in the JSON)

- `none`: the new value applies at the next 32-sample tick, exactly as the prototype's `setInst` (discrete settings,
  and values that only affect new grains or events).
- `engine`: also applied at the next tick; the prototype engine already makes the change click-free (head-gain and
  spread-offset slews over 20 ms, per-tick gain ramps), so nothing is added.
- `ramp20ms`: C++ addition. `engine/InstSmoother` moves the value linearly to its target over 20 ms, one step per
  32-sample boundary of the control grid (1.8; inside `process`, so block size does not matter). Lane-type values
  (macros, offsets, speed, drift, pitch, cents) are then interpolated per sample by the voices as the prototype does.
  `outGain` and the bypass fade are applied in the output loop, where the prototype uses one gain per `process()`
  call, so their ramp is interpolated per sample there (linear in gain between the tick values); stepping them per
  tick would be a 1.5 kHz staircase (2.4 dB a step from -60 to +12 dB). A value that does not change never ramps,
  and smoothers snap to their targets on `prepareToPlay`, `reset`, state load and the first block, so static
  settings play exactly like the prototype. Parity tests call `Engine::setRampMs(0)`.
- With automation, output depends on the host's block boundaries (APVTS delivers values per block); the block-size
  determinism tests use static parameters (critique factualFlags), as GrainLock's do.

### 2.4 The bridge

- `ParamBridge::readInst(const ParameterRefs&, const NonParamAtomics&) -> core::Inst` (audio thread, every block):
  float atomics to `Inst` with `isfinite` fallbacks, choice index to enum, the exact Region edges (2.2), the excerpt
  mapping when only an embedded Region excerpt is loaded (section 3), `clampForEngine`.
- `ParamBridge::applyPatch(apvts, const InstPatch&)` (message thread): one gesture per parameter
  (`beginChangeGesture`, `setValueNotifyingHost`, `endChangeGesture`), as GrainLock's `applyPreset`; fields that are
  2.2 state go to the document instead. Used by sound loads, sound presets, recipes, Dice, Detect, undo/redo and
  prototype import.
- **Root and cents belong to the sound (v4, sl-ui.js).** A sound load (a starter: 60 and 0), Detect and an accepted
  root suggestion set rootNote and rootCents through `applyPatch` with no undo step of their own. Each undo state
  stores which sound was loaded (`NonParamState::soundRef`) and `SoundMeta` keeps every sound's own root and cents,
  so Undo of a sound change brings that sound back with its own root, as the prototype does. (PROMPT 10 says the
  detected root "can be undone"; v4 decided otherwise, 8.1 #29.) Applying a suggested key stays one undo step.
- **Undo and automation.** `doc/EditHistory` stores `DocState { ClipPtr clip; NonParamState np; ParamValues pv; }`
  in a `model::VersionHistory`. When an action is pushed, `pv` copies the previous state's values and overwrites only
  the parameters that action changed. Undo/redo writes only the parameters whose `pv` differs between the two states,
  so a value moved by Cubase automation in between is never reverted by Undo.

---

## 3. Plugin state format

Binary, never XML or base64 (risks 3.4). Little-endian. `getStateInformation` writes, `setStateInformation` reads.
`str8` = u8 byte length + UTF-8 (at most 255 bytes); `str16` = u16 byte length + UTF-8 (at most 4096 bytes).

```
Header (16 bytes):  'S' 'T' 'L' 'K' | u16 formatVersion = 1 | u16 flags = 0 | u32 totalLength | u32 headerCrc32
Then chunks:        u32 tag (FourCC) | u16 chunkVersion | u16 flags | u32 payloadLength | u32 payloadCrc32 | payload | pad to 4
```

| Tag | v | Payload | Written by |
|---|---|---|---|
| `PARM` | 1 | u16 count, then per parameter: str8 id (ASCII), f32 plain value. By id, not index. `bypass` excluded. | M0 |
| `INST` | 1 | i32 seed; str8 mod1Target, u8 mod1Cc, str8 mod2Target, u8 mod2Cc; u8 key, str8 scale, u8 scaleLock; f64 regionStartExact, f64 regionEndExact (NaN = none); str8 liveTemplateNoteId (empty = none; written from M8); (append-only) | M0 (M8 appends) |
| `CLIP` | 1 | the record layout below | M1 (codec), M2 (wired) |
| `SMPL` | 1 | u8 kind (0 starter, 1 file). Starter: str8 id, i32 seed. File: str16 path, u64 fileSize, 32-byte SHA-256 of the original bytes, f64 fileSampleRate, u32 channels, u64 fileFrames, u64 loadedStartFrame, u64 loadedFrames (the part loaded from a long file, in the original file's frames; 0 and fileFrames for a whole file), str16 displayName. Embedding: u8 keepInProject (the project's "Keep the sound in the project" toggle: 1 on, the default; 0 off), u8 embedAnswer (this file's answer to the 20-50 MB question: 0 not asked, 1 yes, 2 no, 3 just the Region). Analysis cache: u8 analysed, u8 hasPitch, f64 midi, f64 cents, u8 hasKey, u8 key, str8 scale, u8 partCount, parts (u8 kind, f64 start, f64 end, f64 bpm, u8 tempoFound, str8 label: the prototype's labels hold the part's time, so they are stored, not rebuilt). A cache this version cannot read is dropped and the sound analysed again. | M0 (starter), M7 (file) |
| `SAUD` | 1 | optional embedded audio: u8 encoding (0 = the original file's bytes, 1 = FLAC 24-bit excerpt, 2 = int16 PCM interleaved), u32 channels, f64 sampleRate, u64 excerptStartFrame, u64 excerptFrames (in the original file's frames; the loaded range for encodings 0 and 2), u64 byteLength, bytes | M7 |
| `UIST` | 1 | u8 theme (0 dark, 1 light), u16 uiScalePercent, u16 logicalWidth, u16 logicalHeight, u8 mode (1..8), str8 subLane, str8 tab, u8 folded, f32 zoomH, f32 zoomV, f64 scrollBeat, f32 scrollRow, f32 gridBeats (0 = free), u8 scope (each/across), u16 copyMask, f64 stripViewA, f64 stripViewB | M0 (+ fields appended by M4-M6) |
| reserved | | `CLPL` clip lanes (v1.1), `CLPS` clip slots (v1.2), `HEAD` per-head table, `MACR` macro assignments, `MODM` modulation matrix | later |

**The CLIP records.** Every record starts with its own length, so a later version can add fields to any of them (A8
chance and every-other-pass, risks 5.3 mute and sample slots, more note sample settings: H5, E4) without breaking a
1.0 reader. Enums are their prototype strings, as the JSON saves write them, so a value a later version adds survives.

```
CLIP payload    u32 settingsLength | i32 bars | i32 beatsPerBar | f64 offsetBeats | u8 loop | u8 tieAcrossLoop | (append-only)
                u32 noteCount | note record x noteCount
note record     u32 recordLength | str8 id | f64 p, s, l, vel, glide (NaN = null), gc | str8 glideTo (empty = null)
                | sample record | u8 laneCount | lane record x laneCount | (append-only: new per-note fields go here)
sample record   u16 recordLength | str8 motion ("" = null) | str8 travel | str8 start.mode | f64 start.pos
                | f64 start.range | i32 start.seed | i8 tape | i8 rev (-1 null, 0, 1) | str8 jump ("" = null)
                | str8 engine ("" = null) | i8 lock | u8 restart | (append-only)
lane record     u32 recordLength | str8 laneId | u16 nodeCount | nodes (f64 t, f64 v, u8 shape, f64 bend)
                | u8 hasGen | gen record if hasGen | (append-only)
gen record      u16 recordLength | str8 kind | str8 shape | str8 rate | f64 d0, d1, warp, amount, smooth
                | u8 lockToBeat | str8 perNote | i32 seed | u8 invert | str8 key ("" = null) | (append-only)
```

**Compatibility rules**
- Unknown chunk tags are skipped by length **and kept verbatim**, then written back unchanged on the next save, so a
  1.0 plugin opening and re-saving a 1.1 project does not destroy the 1.1 clip lanes.
- Every record (chunk payloads, the clip settings, notes, sample settings, lanes, generators) is append-only: a newer
  version only adds fields at the end of a record; an older reader reads the fields it knows and skips the rest by
  the record's length; a newer reader gives missing trailing fields their defaults. A field is never removed or
  re-typed (deprecated fields keep being written with a neutral value).
- **Content this version does not know is kept and written back** (Z4: "Later versions open old projects unchanged";
  equally, a 1.0 re-save must not destroy what 1.1 wrote): (a) the trailing bytes of any record, (b) a lane record
  whose id is not one of today's 10 lanes, (c) an enum string this version does not know (a new Motion, rate, engine
  or generator shape) or a node shape code above 3. They live in the decoded note (`Note::extras`: the unknown lane
  records verbatim and in order; for every record, its unknown trailing bytes; for a record holding an unknown value,
  its original bytes and the value it decoded to). The decoded value is what `model::normalizeNote` makes of the
  known fields (an unknown enum reads as its fallback, null = follow the instrument; an unknown node shape as smooth),
  so the note plays as well as this version can play it. The encoder writes a record's original bytes while its
  decoded value is unchanged; after an edit it encodes the record from its value and appends the kept trailing bytes
  again; unknown lane records are always written back after the known ones. Operations that copy a note (Duplicate,
  Chop, Paste) copy its extras.
- **What could not be loaded is kept too.** A blob with a newer `formatVersion`, a wrong magic, a bad header CRC or
  a `totalLength` larger than the buffer loads nothing: the plugin keeps its defaults, keeps the blob and writes it
  back byte-identical on every save (Cubase also auto-saves without asking) until the user makes an edit, with the
  banner "This project was saved by a newer StrandLock (or could not be read): editing will replace it". A known chunk
  with a bad CRC or out-of-range lengths is skipped on its own (its defaults stay) and kept the same way while its
  part of the state is unedited; the other chunks still load. `formatVersion` changes only for an incompatible
  container change (none planned).
- Parameters are matched by id. A parameter the state does not mention gets its **default**, never the instance's
  current value (GrainLock's `fillMissingParameters`).
- **One table of limits, used by the decoder and enforced by every edit, so StrandLock never writes a state it would
  refuse to load:** notes per clip <= 4096 (`kMaxClipNotes`), nodes per lane <= 128 (`capNodes`), lanes per note <= 64,
  `str8` <= 255 bytes, `str16` <= 4096 bytes, CLIP payload <= 32 MiB (`kMaxClipBytes`), embedded audio <= 50 MB, any
  other chunk <= 1 MiB, `totalLength` <= 32 MiB + 50 MB + 4 MiB. A fully drawn v1 note (10 lanes x 128 nodes x 25 B,
  plus ids and generators) encodes to about 36 KB, so 4096 of them (about 141 MiB) would not fit; the prototype caps
  only Chop (1024 notes). `ClipDocument` therefore checks every new clip version against both clip caps before it is
  accepted (it keeps a running encoded size; each note's size is cached by its serial) and refuses an edit that would
  pass either, leaving the clip unchanged and naming the cap in a toast, exactly as Chop refuses. That covers
  Duplicate, Paste, Rec (the pass stops adding notes and says so), import (refused whole) and every tool. Every count
  is validated against the bytes left in its record before any allocation.
- An id longer than 255 UTF-8 bytes (possible only in an imported prototype save) is replaced at import by
  `normalizeNote`'s deterministic missing-id rule (8.2 #27).
- The clip decoder rebuilds notes through `model::normalizeNote`, so even a valid-looking but odd chunk (NaN nodes,
  unsorted t, dangling `glideTo`) becomes a valid clip, exactly as the prototype's loader would make it.
- The serialized `SAUD` bytes are cached in memory (Cubase calls `getStateInformation` on every save and auto-save);
  nothing is re-encoded unless the sample changed.

**Embedding (C4, M7).** "Keep the sound in the project" is a project toggle (SMPL `keepInProject`, on by default).
With it on, the original file's bytes are embedded without asking up to 20 MB; between 20 and 50 MB StrandLock asks
once for that file (Yes / No / Just the Region) and stores the answer in SMPL `embedAnswer`, so later saves and
auto-saves never ask again; loading a different file starts a new SMPL and the question comes back. Above 50 MB, or
after "Just the Region", the Region is embedded (encoding 1: FLAC 24-bit of the Region plus up to 1 s either side;
the excerpt's start and length in original-file frames). The writer never writes an excerpt over 50 MB: a Region too
long for that is not embedded, and the user is told so. With the toggle off nothing is embedded and nothing is asked.

**The loaded range.** A file longer than 10 minutes is loaded as a part (M7); SMPL stores it as `loadedStartFrame`
and `loadedFrames` in the original file's frames. Every fraction StrandLock stores (the Region parameters and their
exact edges, part chips, the analysis cache) is relative to the loaded range, and a reload from the path, the
library or the embedded original bytes decodes the same range again. (Per-note Start positions are fractions of the
Region, so they need nothing.)

**Finding the sound on load:** the original path (if the SHA-256 matches), then the opt-in library
(`Documents\StrandLock\Samples\<sha256-prefix>.<ext>`, off unless the user turns it on, showing the size), then the
embedded bytes, then a "Locate sample..." banner that keeps every setting until a file is chosen. When only a Region
excerpt is available, the excerpt is the sample: `ParamBridge::readInst` maps the Region (a fraction of the loaded
range) onto the excerpt and clamps it inside, so automation or a part chip that reaches outside plays the nearest
edge of the saved audio, and the banner says "Part of this sound was not saved in the project". Region automation is
never blocked.

**Per-user data.** `UserSettings` (`%APPDATA%\StrandLock\StrandLock.settings`) holds the theme (it wins over the
`UIST` copy, PROMPT 3, risks 5.6; the `UIST` copy is used when there is no per-user value and by the snapshot tests),
the Dice pins (the prototype's `strandlock_v4_pins`), the last sound preset (`strandlock_v4_lastpreset`) and the
library switch. **User presets** (M8, the prototype's "My presets"): `Documents\StrandLock\Presets\<name>.slsound` is
a StrandLock state blob holding PARM with only the parameters a sound preset touches (`touchedBy` has
`soundPreset`), INST (seed) and SMPL (the sound reference, no audio); `<name>.slclip` holds a CLIP chunk.

**Prototype saves (M8, `state/PrototypeImport`).**
- **Getting the file.** Neither prototype can export a save (both keep it in the browser's storage), so the README
  tells Kalan: open the prototype page, press F12, type `copy(localStorage.getItem('strandlock_v4'))` in the Console
  (v3: `'strandlock_v3'`) and press Enter, then press "Paste prototype save" in StrandLock (or paste the text into a
  `.json` file and drop it on the window). `core::JsonLite` parses it.
- **v4** (`{v: 4, clip, inst, bpm, snap, loop, mode, subLane, tab, folded, sound, soundFile}`), along the prototype's
  own reopening path (sl-ui.js `init()`): `model::normalizeClip(clip)`, `core::normalizeInst(inst)`, then the saved
  starter (`sound`, or sawopen when it is not a starter id) loads the way `switchSound` does: root 60 and cents 0 (a
  starter is rooted at C3) and, when `soundFile` is true (the session ended on a user file), the Region reset to the
  whole file. `seed`, `mod1`, `mod2`, `key`, `scale` and `scaleLock` go to INST; the other inst fields go to the
  parameters through `ParamBridge::applyPatch`; `loop` to `clip.loop`; `snap` to UIST `gridBeats`; `mode`, `subLane`,
  `tab` and `folded` to UIST; `bpm` is ignored (Cubase sets the tempo); `soundFile: true` shows "Load the sound file
  you used".
- **v3** (`{dark, notes, bpm, bars, glideMode, glideTime, m1Lane, m2Lane, cc1, cc2, key, scale, lock, engine}`; a
  bare notes array or `{bars, notes}` is also accepted): `normalizeClip({bars, notes})` (v3 `lin:true` -> linear,
  missing ids made deterministically, dangling links dropped, missing lanes filled). The instrument is
  `normalizeInst` of `defaultInst()` merged with: `engine` formant -> voice, classic -> tape; `lock` -> `scaleLock`
  (v3's lock is the scale lock; v4's `lock` is Cloud's Lock and stays off); `m1Lane` / `m2Lane` -> `mod1` / `mod2`
  `.target`, with `'none'` -> `'off'`; `cc1` / `cc2` -> the mod CCs; `glideMode`, `glideTime`, `key`, `scale` as they
  are. Then the default starter loads (root 60, cents 0, the whole file). `dark` -> the `UIST` theme copy (the
  per-user theme still wins); `bpm` is ignored.
- Settings the prototypes never had get the plugin's defaults (beatsPerBar 4, offsetBeats 0, tieAcrossLoop on). The
  whole import is one undo step "Import prototype save". A prototype save never carries audio. The expected results
  (`state/import.json`) are generated by running exactly these paths (`tools/fixtures/state.js`).

**State tests (M0 container, M1 clip codec, M2 full state with a clip, M7 sample).**
- Round trip. M0: random parameter sets x opaque CLIP blobs (seeded random bytes, 0..64 KB) x starter references:
  `get -> set -> get` byte-identical, every parameter equal to the float it was set to. M2: every clip preset and
  fixture clip x random parameter sets through the processor's `getStateInformation` / `setStateInformation`:
  byte-identical, clip ids, links and nodes bit-exact (doubles compared by bits); and a pluginval-style
  random-parameter restore with a non-empty clip.
- Truncation: every prefix of a 4 KB reference blob (all lengths 0..4096) and 200 cut points of a 2 MB blob: 0
  crashes, 0 exceptions escaping, every parameter finite and in range afterwards; each load's time printed (target
  50 ms, gate 250 ms).
- Corruption: 10,000 seeded single-byte mutations and 1,000 seeded random blobs (0..64 KB): same criteria; no single
  allocation above 51 MiB (the SAUD buffer) and no more than 2 x the blob size + 64 MiB live during a load
  (AllocCounter's size tracking, 7.1).
- Forward compatibility (M1 codec, again through the processor in M2): (a) an extra unknown chunk; (b) a CLIP whose
  every record (settings, notes, sample settings, lanes, generators) carries 8 extra trailing bytes; (c) a note with
  an unknown lane id; (d) a note with an unknown motion string and a node shape code 9. Each loads and re-saves
  byte-identically; after editing that note's pitch, its trailing bytes, the unknown lane and the unknown values are
  still written and only `p` changed. (e) A blob with formatVersion 2, and one with a bad CLIP CRC: re-saved
  byte-identically while unedited, replaced after an edit, banner shown.
- Limits (M1 model, M2 processor): the largest editable clip (4096 notes filled until it encodes just under 32 MiB;
  with M7, plus a 50 MB embed) saves and loads; one more note or node is refused at edit time with the clip unchanged;
  Duplicate, Paste, Rec and import each refuse past 4096 notes.
- Old versions: from the first release on, `Tests/fixtures/state/v<version>-*.bin` is committed at every release and
  must load to the same parameters and clip. The files come from CI: `StrandLockTests --write-state-fixtures DIR`
  (the `SL_STATE_FIXTURE` registrar, 7.1) writes them, the "StrandLock-State-Fixtures" artifact carries them, and the
  release builder commits that artifact's files.
- Embedded sample (M7): a WAV made at test time by `TestSignals` (about 2 MB, fixed seed; no binary file is
  committed, so the 5 MB fixture budget is untouched) round-trips with identical bytes and bit-identical decoded
  audio; one 45 MB case (Release job only) saves and loads, time printed (target 2 s, gate 10 s); a part-loaded
  12-minute file (loaded from minute 6) reloads the same part from its path, from the library and from the embedded
  original bytes, with the Region and part chips on the same frames; a Region embed reloaded with the original
  missing plays the excerpt, clamps a Region set outside it and shows the banner; the embed answer survives save and
  load (no second question) and is asked again after another file is loaded.
- Region precision (M7): a one-bar part chip on a 10-minute 48 kHz file loops with a period equal to the f64 Region
  length in frames (0 frames of drift over 64 passes, where float32 edges would be up to 3.4 frames off); host
  automation to another value plays that value.

---

## 4. Milestones M0-M9

Adjusted from `critique.json`'s recommended milestones, with reasons:
- The v4 prototype already implements and tests things the critique scheduled late (Lock, Drift, Head tune, recipes,
  Dice, the Path kind, per-note engines). They move to where their code lives: model pieces into M1, Lock/Drift/Head
  tune into the grain milestone (M3).
- The critique's M4 ("port CycleLock's ShiftVoice and an EpochMap") does not match v4: the prototype's Voice engine is
  v3's root-grid PSOLA running on the same grain scheduler as Cloud (`runGrains(h, run, voice)`). So Voice and Cloud
  are ported together in M3; real pitch marks (E4) wait for v1.1.
- The roll UI (critique M5) is the biggest UI block, so it is split into M4 (Build and lane modes) and M5 (selection
  tools), and Sample-handling extras (embedding, library, Locate) move from M8 to M7 next to the other sample work.
- Every milestone keeps CI green: Release tests, UI snapshots, Debug-CRT allocation check, pluginval strictness 10.
  That holds from the scaffold on, because the scaffold already has a silent processor and an empty editor (7.1).

Each test line gives its numeric pass criterion. "Fixtures" refers to section 5.

**Timing gates.** The windows-latest runners are shared machines, and GrainLock gates CPU only at "faster than real
time" ("a figure to watch, not a promise"). So a test that measures time prints its figure and its target, and fails
only when slower than real time (CPU) or more than 5 times its target (milliseconds); a missed target is reported in
`docs/NOTES-<milestone>.md` for review, not a red build. Counts (ticks, allocations, voices, cuts) are always gated.

### M0 Probe (plugin shell) - parallel with M1

**Deliverable.** A VST3 that loads on a Cubase Instrument track with the suite theme in both palettes, the logo and
wordmark, the empty layout, every v1 parameter in final order, the binary state container, a diagnostics page, and the
built-in Saw playable from MIDI on a simple voice. Kalan then runs the checklist in section 6.

**Files.** Scaffold commit (section 7.1) plus: `Source/PluginProcessor.*`, `PluginEditor.*`, `params/*`,
`state/StateFormat.*`, `state/StateChunks.*` (PARM, INST, UIST, SMPL starter; CLIP kept as opaque bytes), `state/Crc32.h`,
`state/UserSettings.*`, `host/HostInfo.*`, `diag/*`, `sounds/StarterSounds.*`, `probe/ProbeSynth.*`, `ui/Theme.*` (both
palettes: every token of shell.html's `:root` and `[data-theme="light"]`), `ui/StrandLookAndFeel.*`, `ui/Logo.*`
(GrainLock's `paintLogo`: 2.2 px ring, one dot at 0.28 pi, in the accent; wordmark STRANDLOCK in
`Theme::font(17, bold, 0.2)`), `ui/Controls.*` (GrainLock's Knob, PillToggle, SegmentedControl, ChoiceBox, TabStrip
caption strip, PresetButton, ChevronButton, themed), `ui/MainView.*` (top bar with the theme toggle and host tempo
read-only, mode bar 1 Build .. 8 Sample, selection row, tool tabs Sample / Macros / Note settings / Slides / Copy-Paste
/ Mod wheels / Sound / Diagnostics, roll placeholder, waveform strip showing the loaded starter, hint bar),
`ui/tabs/MacrosTab`, `SoundTab`, `SampleTab` (plain knobs bound to the parameters), `ui/DiagnosticsPage.*`,
`Tests/plugin/*`, `Tests/support/Harness.*`, `Measure.*`, `tools/fixtures/sounds.js`, `ui.js`,
`Tests/fixtures/sounds/starter.json`, `Tests/fixtures/ui/theme.json`. Base layout 1100 x 680 logical, UI Scale 75-200%,
window resizable between 900 x 560 and 2400 x 1600 logical.

**ProbeSynth:** 16 voices playing the starter `saw` with 4-point Hermite at `2^((p - 60)/12) x 44100/sr`, 8 ms attack,
60 ms release, 5 ms steal, `velSens` and `outGain` applied, the engine's limiter (instant attack, 80 ms release,
ceiling 0.9, clamp 0.98). Deleted in M2.

| Test | Pass criterion |
|---|---|
| parameters match `docs/parameters.json` | 50 parameters in order; every id, name, type, min, max, default, choice list and step equal (floats within 1e-6; every float continuous); `isAutomatable` true for all |
| defaults = `defaultInst()` | `ParamBridge::readInst(defaults)` equals `core::defaultInst()` field by field after float rounding: each parameter-backed field equals `(double)(float)` of the `defaultInst()` value (velSens 0.7 reads 0.699999988, 1.6 #12); the 2.2 state fields exactly |
| ranges and text | `convertFrom0to1(convertTo0to1(v))` within 1e-5 x range at 101 points per parameter; text is idempotent for every parameter: `getText(getValueForText(getText(v))) == getText(v)` at the same points; and for the parameters whose readout is one-to-one (all but macro1-8 and the five offsets, whose readouts round coarsely or saturate: `ampOffset` prints "+3.9 dB" for every value from 0.2 to 1, `macroText(6)` steps 0.1 octave of 8.3) `getValueForText(getText(v))` is within the readout's printed resolution |
| state container | the M0 state tests of section 3 (round trip with opaque CLIP bytes, truncation, corruption, the unknown chunk (a), the unreadable blobs (e)) |
| theme | both palettes define all 77 tokens of `ui/theme.json` with identical values; light accent `#7B4FE0`, dark `#B58CFF`; lane line colours and the playhead >= 2.5:1 contrast against the roll background in both themes (the v4 palette: lanes 2.77-11.86, playhead 2.61 on white) |
| starter sounds | fixtures `sounds/starter.json`: lengths and root exact, windows within 2e-7, block RMS within 1e-6 relative; all 8 made in <= 2 s; `sawopen` made synchronously (target 60 ms, printed; the JS takes 88 ms) |
| probe tuning | Saw at MIDI 36/60/84 (C1/C3/C5) against root 60 at 44.1/48/96 kHz: f0 within +-3 cents |
| probe stability | 16 voices of chords for 10 s: non-finite samples counted before the output guard = 0, pre-limiter peak > 0.05, output rms > 0.01, peak <= 0.98; reported latency 0, no input bus, stereo out, `acceptsMidi` |
| block sizes | 10 s of MIDI rendered with blocks 1, 37, 64, 480, 4096 and random 1..700: bit-identical (max diff 0) |
| allocations | Release: 0 `operator new` and 0 `operator delete` on the audio thread over 10 rendered minutes; Debug CRT: 0 heap allocations over 3 minutes |
| diagnostics | with a fake playhead and fake MIDI, `DiagModel` reports the block sizes (min/max/histogram), ppq at block starts, cycle wraps, wraps that fell inside a block, `isNonRealtime`, note/CC numbers and the last host-changed parameter exactly |
| UI snapshots (artifact) | dark and light, 100 % and 75 %, plus a 900 x 560 narrow window, plus the diagnostics page, through `SL_SNAPSHOT`: rendered by the Release runner (uploaded) and by the Debug runner, where jassert is live (0 assertions); mean luminance < 0.25 (dark) and > 0.75 (light) |
| pluginval | strictness 10 passes |

### M1 Model core, headless - parallel with M0

**Deliverable.** All of sl-model.js in C++: lanes, nodes and segment shapes, generators with the shared Rate type and
the frozen seed recipe, every selection tool (Y2-Y5, Walk, Chop, Reverse, Flip, More/Less, Select similar, Humanize,
Strum, Legato), note settings, glides and the chain builder, recipes, Dice, sound and clip presets, the snapshot
compiler, immutable versions and `VersionHistory`, the clip codec, and the fixture generator. No UI.

**Files.** `Source/core/*` (completing the scaffold), `core/Snapshot.h`, `Source/model/*`, `Tests/model/test_*.cpp`
(`seeds`, `rates_lanes`, `nodes`, `generators`, `shapes`, `tools`, `glides`, `snapshot`, `presets`, `format`,
`normalize`, `history`, `codec`, `codec_compat`, `limits`, `immutability`, `jscompat`), `Tests/support/FixtureJson.*`,
`tools/make-fixtures.js` (completing the scaffold), `tools/fixtures/model.js`, `Tests/fixtures/model/*.json`.

| Test | Pass criterion |
|---|---|
| fixtures `model/*` | every case within section 5's tolerances; a printed count of cases that matched bit for bit |
| lane mapping table (PROMPT 14.4, BLUEPRINT 1.2) | `mapLane` at 41 values per lane equals the formula within 1e-12 relative |
| monotone smooth segments | 10,000 seeded random smooth node lists sampled at 1025 points: overshoot beyond the two neighbouring nodes <= 1e-9 |
| mode lock (PROMPT 14.5) | 1,000 seeded `setLaneNodes` calls: every other field of every note (start, length, pitch, the other 9 lanes, generators, settings) byte-identical in the codec encoding |
| immutability | for every operation in `tools.json`, the input clip's encoding before and after the call is byte-identical; untouched notes are pointer-shared with the input |
| undo | 1,000 seeded random edit sequences of 1-300 steps: undo to the start gives the start clip byte-identical, redo to the end gives the final clip; the 201st push drops the oldest; 200 one-note edits on a 1024-note clip hold <= 8 MB (AllocCounter's live bytes) |
| snapshot determinism and speed | compiling twice gives identical tables; targets printed: Strand Demo 2 ms, 256 chopped notes with a 1/32 pattern 10 ms, 1024 four-beat notes with a random generator 120 ms (Release) |
| clip codec | every fixture clip `encode -> decode -> encode` byte-identical, doubles bit-exact; a decoded clip equals `normalizeClip` of the same notes |
| forward compatibility | section 3's cases (b)-(d) on the codec: extra trailing bytes in every record, an unknown lane id, an unknown motion string and node shape code: load and re-save byte-identically; after a pitch edit only `p` changed |
| edit caps | a chop that would exceed 1024 notes returns `refused` with the count and leaves the clip unchanged; Duplicate, Paste and `addNote` past 4096 notes, and any edit whose result would encode above 32 MiB, return `refused` and leave the clip unchanged; the largest editable clip (4096 notes just under 32 MiB) encodes and decodes |
| JS compatibility helpers | `jsNumberToString`, `jsToFixed`, `jsToPrecision`, `toBase36`, `jsRound`, `toInt32` equal the 500+ fixture strings and numbers exactly |

### M2 First playable sound (engine core, Tape, host sync)

**Deliverable.** The real engine replaces ProbeSynth: snapshot hand-off, the voice pool with steal tails, Voice::tick
(lanes, macros, offsets, pitch, glides, vibrato, Refresh, Scatter, heads, Spread, Drift, Head tune, filters), the
Region with Loop/Hold/Stop and both seam fades, the Tape engine with every Motion, Refresh stutter, chains on one voice,
chase with its bounded cost (1.8), the limiter and health counters, the display slot, host sync with tempo
re-anchoring, cycle wraps inside blocks and pre-roll (1.10), the full state with a clip, live MIDI (velocity, bend,
CC64, wheels),
the starter sound menu, Browse, drop anywhere (C3), the waveform strip with the Region box, zoom, At the end, and a
default part for long files (4 s from 35 % in; M7 replaces it with the suggestions). Notes whose engine is Voice or
Cloud play as Tape until M3 (the diagnostics page says so). Kalan plays the saw and a part of one of his songs.

**Files.** `Source/rt/VersionExchange.h`, `LatestSlot.h`, `StableSort.h`, `Source/engine/*` except `VoiceGrains.cpp`
and `VoiceLock.cpp`, `host/HostSync.*`, `host/MidiRouter.*`, `doc/*` (ClipDocument with the edit caps, EditHistory,
SnapshotPublisher, SampleManager: decode with `AudioFormatManager` on the worker, starter sounds),
`ui/strip/WaveStrip.*`, `WavePeaks.*`, processor integration (CLIP chunk through `ClipCodec`), removal of
`Source/probe/`, `Tests/engine/test_*.cpp` (`parity`, `tuning_tape`, `hostsync`, `chase_cost`, `determinism`,
`seams`, `live`, `exchange`, `alloc_engine`, `stability`), `Tests/plugin/test_state_clip.cpp`,
`Tests/support/TestSignals.*`, `tools/fixtures/engine.js`, `support.js`, `Tests/fixtures/engine/defs.json`,
`engine/renders.json`, `support/signals.json`.

| Test | Pass criterion |
|---|---|
| fixtures `engine/defs.json`, and every `engine/renders.json` case with `needs: "tape"` (36) | section 5 tolerances (windows within 1e-5, stats exact) |
| Tape tuning (PROMPT 14.1, v4 naming) | MIDI 36/60/84 (C1/C3/C5) against a C3 root at 44.1/48/96 kHz, and a 44.1 kHz sample in a 48 kHz session (F2): f0 within +-3 cents |
| note starts (PROMPT 14.9) | fake playhead at constant 120, 87 and 133 bpm, loop passes 1-3: first sounding sample - exact beat time in [0, 1) samples; after a step change 120 -> 87 the same |
| jitter | host positions jittered +-0.003 beat (seeded) every block for 64 bars at a constant tempo: 0 doubled and 0 missed note starts, starts within [0, 1) sample of the unjittered render |
| cycle wrap | a fake host that reports its loop points and does not split blocks, 2048-sample blocks, the wrap 1, 200 and 2047 samples into a block: a cycle of 2 clip lengths renders equal to the uncycled render bit for bit; a cycle of 1.5 clip lengths chases at the wrap sample, playhead within 0.002 of expected (fraction of the file), and no note after the cycle end starts |
| tempo and cycle together | a tempo ramp across 3 cycle passes of a 1-bar clip, with the wrap reported +0.003 and then -0.003 beat off: every note starts exactly once per pass (1.10: forward and backward re-anchors) |
| pre-roll | playing from ppq -2 with loop on and with loop off, on a 2-bar clip with a note at beat 0 and a note sounding through its last 2 beats: rms of everything before ppq 0 = 0; the first note starts in [0, 1) samples of ppq 0 |
| chase (stop/start mid-note), Tape | Tape notes (`tape-chase-at-1.37`, and one with a moving Volume lane): the first sample is non-zero; rms(0-2.5 ms) / rms(12.5-15 ms) in 0.02..0.3; after 20 ms the chased output equals the straight render within 1e-4. (Voice, Cloud and Lock notes restart their grains on a new phase, so M3 checks them with the playhead, not the samples.) |
| chase cost | a transport start inside 16 x 16-beat 8-head Tape notes, and inside 16 x 64-beat notes, at 48 kHz: no 32-sample stretch runs more than 4096 + 256 chase ticks (`stats().chaseTicks`), every voice sounds within 0.35 s of Play, chased playheads within 0.002 of the straight render; the chase block's time printed |
| tempo drift | the fake host integrates ppq per sample from a continuous ramp 120 -> 60 bpm over 8 bars and reports the tempo at each block start, 512-sample blocks: every note start within -1..+1 samples of the host position and the last one within 2 samples (the prototype's own clock is 240 samples off here) |
| stopped processing | live Scatter at 1/8T while stopped: one jump per 1/3 beat of the free clock, 0..32 samples after each grid line; while playing (`tape-live-scatter-playing`) the jumps follow the song's beat |
| determinism (static settings) | 10 s of Strand Demo + chase at beat 1.7 + live MIDI at fixed sample positions + Scatter + Skip, blocks 1, 37, 128, 700, 4096, random 1..700: bit-identical; the same scenario rendered with `setNonRealtime(true)`: bit-identical; started after 0, 7 and 1000 stopped samples and aligned to the transport start: bit-identical; the scenario's rms > 0.01 (a no-op is not a pass) |
| loop seam | Tape, 10 ms crossfade: largest step within the seam zones / largest steady step < 1.5; a ramp source seam (0.85 -> 0.05) largest step < 0.01 |
| voice steal | 20 keys at polyphony 16: 16 voices, 4 steals; largest step within 10 ms of each steal / steady largest step < 1.5 |
| chain | C3 -> G3 slide on one Tape voice: max voices 1, min envelope across the joint > 0.999, the second note's playhead continues the first's within 0.002 |
| stability (PROMPT 14.6) | Strand Demo with 8 heads Tape and 16 live chords: `stats` nonFinite = 0 and filterResets = 0, pre-limiter peak > 0.05, output rms > 0.01, peak <= 0.98 |
| live | velocity gain = 1 - velSens (1 - vel) within 1e-6; bend +1 at bendRange 7 = +7 st within +-3 cents; sustain holds a released key until CC64 < 64; with mod1 -> Travel, `setController(mod1, 0.25)` plays -1x within 1e-6 and 0.5 stops (speed < 0.01x); through MidiRouter, CC 32 plays -0.98236x within 1e-4 |
| edits while playing | a Volume lane edit is heard within one tick (level ratio within 0.01 of expected, same voice); a deleted note releases (voices 0, then silence), also while 1,000 snapshots a second are published, with 0 reads of freed memory in the sanitizer job |
| exchange | a thread publishes 1,000 snapshots a second (each deleting and adding sounding notes) and 2 sample swaps while audio renders 10 s: 0 allocations and 0 frees on the audio thread; no version freed while a voice holds its epoch (a debug poison check on freed versions, and the sanitizer job); 100 ms after the last publish with no voice releasing, `collect()` leaves 1 version; edits published while `processBlock` is not called: the first block after Play plays the newest one; largest step at a sample swap / steady < 1.5 on a 220 -> 330 Hz sine pair (stolen voices fade on the old sample) |
| state with a clip | section 3's M2 round trip (every clip preset and fixture clip x random parameters through the processor), forward-compatibility cases (a)-(e) through `getStateInformation` / `setStateInformation`, and the limits case |
| allocations | as M0, with Strand Demo, live keys and parameter sweeps playing |
| CPU (Tape) | 16 voices x 8 heads Tape at 96 kHz, 64-sample blocks: target >= 8x real time (printed), gate > 1x; a 4096-note clip at 96 kHz with 64-sample blocks before any voice sounds: `scanStarts` time printed (target < 0.5 % of a core) |

### M3 Grain engines: Voice, Cloud, Lock, and the budget

**Deliverable.** `runGrains` (Voice: root-grid PSOLA with formant shift; Cloud: free grains in ms), `runLock`
(GrainLock's trick, Hann-weighted taps, correlation-corrected re-grab), the full budget planner (thinning, waiting
heads, cuts), Spray, Refresh hold, Each grain backwards, the Tape switch, Path kind for Voice and Cloud, per-note
engines, and the 96 kHz CPU benchmark. After this milestone the default sound (Voice) plays as in the prototype.

**Files.** `engine/VoiceGrains.cpp`, `engine/VoiceLock.cpp`, `engine/EnginePlan.cpp` (complete), `Tests/engine/test_*.cpp`
(`tuning_grains`, `formant`, `motions`, `budget`, `chase_grains`, `cpu`, `stereo`, `refresh_spray`).

| Test | Pass criterion |
|---|---|
| fixtures: every `engine/renders.json` case with `needs: "grains"` (37) | section 5 tolerances |
| Voice tuning (PROMPT 14.1) | C1/C3/C5 against a C3 root at 48 and 96 kHz: f0 within +-3 cents; Cloud f0 printed, not gated (plain Cloud grains are not phase-aligned and read +29 cents at 2x, which the prototype reports without gating; 8.1 #28) |
| chase, grain engines | Voice, Cloud and Lock notes chased at beat 1.37: playhead within 0.002 of the straight render; rms(0-2.5 ms) / rms(12.5-15 ms) in 0.02..0.3 (the JS criteria; their samples differ because the grains restart on a new phase) |
| Lock tuning | from noise and from a chord, notes C2/C3/C4, Refresh Smooth / 120 ms / 400 ms: within +-3 cents; harmonic lines > 10 dB over the floor from noise |
| formant (PROMPT 14.2) | Voice formant lane +6 st: spectral centroid rises > 25 % and f0 within +-3 cents; -6 st: centroid falls > 10 % and f0 within +-3 cents |
| glide (PROMPT 14.3) | legato C3 -> G3 lands within +-5 cents; an explicit `glideTo` link plays with Auto Slide Off |
| motions | Backward correlates with the reversed source r > 0.95 (Forward \|r\| < 0.1); Freeze position spread < 0.001; Bounce stays inside a 0.2..0.3 Region with 4-6 reversals in 2 s; Mirror heads move +1x -1x +1x -1x within 0.003 |
| Scatter | one jump per grid line at 1/8 and 87 bpm, 0 <= error < 32 samples; two renders identical; Re-roll moves >= 80 % of the targets; largest step around a jump < 0.05 |
| Refresh and Skip | Refresh 100 ms: read position moves < 0.002 inside a period; each refresh grabs 0.05 +- 0.01 further at 4x; Skip 0.5 grabs 3-10 of 13; identical renders; Re-roll changes them |
| Spray | 0.25 spray: position spread median > 4x the unsprayed + 0.002 and max < 0.04; identical renders; seed + 1 changes it |
| fractional heads | Heads 2 -> 2.5 -> 3 in Tape, Voice and Cloud: largest step at the changes / steady largest step < 1.5 |
| budget (D3), written from what the prototype does | the prototype's 16 notes x 8 heads layout (notes 0.05 beat apart at 120 bpm; Voice, Cloud and Lock N = 12; 44.1, 48 and 96 kHz): measured max load <= B everywhere (208 / 192 / 96). Voice, 8 heads, 6 notes 80 ms apart at 48 kHz: the newest note keeps all 8 heads after every note-on, and sounding heads are non-decreasing toward newer voices. cuts = 0 in the scenarios the prototype measured with none: Lock 8 heads Grain 4 and Voice 8 heads, arpeggios of 6 notes 0.1 s apart, and Strand Demo with recipes 3, 6 and 8 at 8 heads. Lock N = 12 x 8 heads: cuts, thinned heads and the newest note's sounding heads (8 / 8 / 4 at 44.1 / 48 / 96 kHz) equal the `budget-*` fixture stats exactly (live keys 256 samples apart; clip notes 0.05 beat apart) |
| stereo | mono source, Spread 0, 3 heads: L == R bit for bit; stereo source keeps \|corr(L, R)\| < 0.5; mono source, Spread 0.6: corr < 0.9; mono sum of 8 heads with Mirror and Spread 0.6 loses <= 4.5 dB against the mean of L and R power |
| stability | 16 voices x 8 heads in each engine: nonFinite = 0, filterResets = 0, pre-limiter peak > 0.05, output rms > 0.01, peak <= 0.98, load <= budget |
| determinism | the M2 block-size scenario with Lock, Cloud, a Voice-to-Cloud chain and Spray: bit-identical; an 8-head Voice chord under budget pressure (stats thinned > 0) started after 0, 7 and 1000 stopped samples: bit-identical, aligned to the transport start (the control grid, 1.8) |
| CPU at 96 kHz (min of 3 runs) | targets, printed: Cloud 16 voices x 8 heads 140 ms grains, Lock 16 x 8 N = 12 and Voice at C5 16 x 8 each >= 2.0x real time with 64-sample blocks; typical 8 voices x 2 heads per engine at 48 kHz >= 20x. Gate: > 1x real time |
| aliasing (printed only) | Tape +36 st on the saw: level of energy off the harmonic lines, in dB (feeds open question 6) |

### M4 Roll UI: Build mode and lane modes

**Deliverable.** The piano roll: ruler, piano strip (click to audition), grid, playhead (`#FF7A00`), notes as strands
with ribbons, Build mode (double-click to create, drag to move with grid and scale snap, end dots with the 0.4-row
dead zone, glide diamonds, Shift+click = Slide into, right-click menu), lane modes 2-7 with the lock rule, node editing
(Y6: drag, double-click to add or delete, segment bend diamonds, live readout in real units, pitch nodes snap to
semitones unless Shift), Freehand (RDP 0.012), the Keys toggle (C2), Undo/Redo buttons, the inspector (T6), the hint
bar, and a note-layer image cache.

**Files.** `ui/roll/RollView`, `RollGeometry`, `RollPainter`, `NoteLayerCache`, `RollGestures` (pure gesture state
machine, testable without a mouse), `PianoStrip`, `Ruler`, `Inspector`, `ui/KeyRouter`, `ui/HintBar` (texts from
`modeHint`), `ui/Toast`, `doc/ClipDocument` (selection, mode, active lane), `Tests/ui/test_*.cpp`
(`roll_gestures`, `mode_lock_ui`, `keys`, `inspector`, `roll_snapshots`, `roll_perf`), `Tests/fixtures/ui/inspector.json`
(SLPanels' pure helpers).

| Test | Pass criterion |
|---|---|
| gestures (model level) | double-click empty creates one note of max(1 beat, grid) at the snapped position; a drag moves with grid and scale snap; an end dot moves vertically only after 0.4 row; each finished gesture pushes exactly 1 undo step; a click that moves nothing pushes 0 |
| lane-mode lock | 500 seeded random gestures in each lane mode: 0 changes to start, length, pitch or any other lane |
| keys | with Keys off, every key event returns false (passes to Cubase); with Keys on, Space is never consumed; shortcuts work only with Keys on |
| inspector parsing | `fmtPos`, `parsePos`, `parseBeats`, `parsePitch`, `laneValueFromText` equal the fixture strings and values (values within 1e-9) |
| snapshots (artifact) | dark and light x Build, Pitch (selected note with diamonds) and Tone modes x 75 % / 100 % / 200 % and the narrow window (PROMPT 14.10); compared by eye with `reference/screenshots/v3-*.png` for layout intent |
| drawing cost | full repaint of a 512-note clip at 1100 x 680: target 50 ms (printed; gate 250 ms); moving one note repaints <= 3 notes' bounds (gated) |

### M5 Selection tools

**Deliverable.** Box select in every mode (Y1: Shift adds, Ctrl toggles; the Nodes switch picks nodes of the selected
notes), the selection bar (Shapes with Each/Across, Pattern, Random with Re-roll, Turn into nodes, Connect
Nearest/Furthest/Random, Disconnect, Walk, Chop, Reverse, Flip, More, Less, Select similar, Humanize, Strum, Legato),
the lane-button menu arrows (Y2-Y4), hover ghosts (U6), copy/paste lanes, and the Slides tab.

**Files.** `ui/roll/SelectionBar`, `Drawers`, `LaneMenu`, `BoxSelect`, `GhostPreview`, `ui/tabs/SlidesTab`,
`CopyPasteTab`, `Tests/ui/test_*.cpp` (`selection_tools`, `box_select`, `ghost`, `copy_paste`).

| Test | Pass criterion |
|---|---|
| tools through the UI layer | each selection-bar action on the fixture clips gives the same clip as the direct `model` call (codec bytes identical) |
| box select | seeded boxes over a 64-note clip select exactly the notes whose rectangle intersects the box; Shift adds, Ctrl toggles |
| ghosts | hovering any shape, pattern or recipe pushes 0 undo steps and publishes 0 snapshots |
| live sliders | dragging a Pattern slider applies live and is 1 undo step on release |
| copy/paste | pasting lanes stretches them to each target note's length (node t values identical); duplicate keeps links inside the group; a paste or duplicate past 4096 notes is refused with a toast (section 3 limits) |
| snapshots (artifact) | dark and light at 100 %: the selection bar with the Shapes drawer open, a hover ghost preview, a box select in progress (PROMPT 14.10) |

### M6 Sample mode 8, the Sample tab and Preview

**Deliverable.** Mode 8 with the Travel/Heads/Grain/Refresh chips, chevrons and per-note thumbnails (U7), the Sample
editor strip (live heads and grains from the display slot, click with notes selected sets their Start, hold to audition),
the full Sample tab (engine, Lock, Motion, knobs, Refresh, switches, Head tune, At the end, Jump every, faded = does
nothing now), the Macros tab with the 5 offsets, Recipes 1-8 and Dice with pins, the Note settings panel (S7, shows
"mixed"), the "Try this" hint line (U3), and Preview while Cubase is stopped if the M0 checklist shows Cubase keeps
processing.

**Files.** `ui/tabs/SampleTab`, `MacrosTab`, `NoteSettingsTab`, `ui/SampleTabRules`, `ui/strip/HeadsOverlay`,
roll Sample-mode drawing, `host/HostSync` Preview clock, `Tests/ui/test_*.cpp` (`sample_rules`, `readouts`,
`heads_overlay`, `note_settings`), `Tests/engine/test_preview.cpp`, `Tests/fixtures/ui/sample-rules.json`,
`ui/readouts.json`.

| Test | Pass criterion |
|---|---|
| fade rules | `SampleTabRules::fades` / `contexts` / `grainKind` for every sound preset x recipe x clip preset equal the fixture exactly |
| readouts | `macroText`, `offsetText` and every knob format equal the fixture strings exactly |
| knobs | each Sample-tab control changes exactly its own parameter (0 others), and every change is 1 undo step; a drag snaps as the prototype's `q` functions do, a typed value is kept as typed (Lock Grain 4.3 stays 4.3 and plays 4 taps) |
| recipes and Dice | each recipe button gives the fixture's clip (exact) and inst (each field within `(double)(float)` of the fixture's value: it passes through a parameter, 1.6 #12); Dice with pins changes no pinned field; the pins survive closing and reopening (UserSettings) |
| display slot | 10,000 frames written while the UI thread is paused: the audio thread never waits, and the UI's next take returns the last frame written |
| snapshots (artifact) | dark and light at 100 % and 200 %: mode 8 with the chips, chevrons and per-note thumbnails; the Sample tab with faded controls; the Macros tab; the heads overlay drawn from a fed `DisplayFrame` (PROMPT 14.10) |
| Preview | with the host stopped, notes start within [0, 1) sample of the Preview clock's beat; Preview never runs while the host plays |

### M7 Long songs and sample handling

**Deliverable.** Pitch and key detection (on load, in the background, and on "Detect pitch & key" for the Region),
part suggestions as chips (D10), the engine suggestion (E3), root suggestions that never retune silently (one click
to accept; like Detect it is not an undo step, because the root belongs to the sound, 2.4), int16 storage for long
files, a 10-minute length limit with "load a part" above it (the SMPL loaded range), the exact Region edges,
embedding with its saved answer, the opt-in library and Locate (C4), and drag sources tested on Kalan's checklist
results.

**Files.** `Source/analysis/*`, `doc/SampleManager` (library, embed, locate, int16, loaded range), `state/StateChunks`
SMPL file references and SAUD, `ui/strip/PartChips`, suggestion line, `Tests/analysis/test_detect.cpp`,
`Tests/plugin/test_embed.cpp`, `test_locate.cpp`, `test_long_file.cpp`, `test_part_load.cpp`,
`test_region_precision.cpp`, `test_root.cpp`, `Tests/fixtures/analysis/detect.json`.

| Test | Pass criterion |
|---|---|
| fixtures `analysis/detect.json` | detectPitch null-ness and MIDI exact, cents within 0.1, Hz within 1e-4 relative; detectKey exact; suggestParts kinds and labels exact, start/end within 1e-3 of the file (the prototype reads a DC input as MIDI 90, not "no clear pitch"; the fixture records it and `docs/NOTES-M7.md` says whether the plugin keeps it) |
| detection (PROMPT 14.11) | synthetic tones at 5 pitches within +-10 cents; a C major progression detected as C major; white noise gives "no clear pitch" |
| speed | suggestParts on a 5-minute stereo file: target 1.0 s (Release, printed) |
| long files | a 10-minute stereo 48 kHz file loads as int16 with at most its int16 bytes (115.2 MB) + 16 MB live (AllocCounter), decoded on the worker; 0 allocations on the audio thread during the swap |
| embedding | section 3's embedded-sample tests (a WAV made at test time, the part-loaded 12-minute file, a Region excerpt with the original missing, the saved answer); a Region embed (FLAC 24-bit) decodes within 2^-23 of the original samples; no excerpt is written above 50 MB |
| Region precision | section 3's test: a one-bar part on a 10-minute file, 0 frames of drift over 64 passes; host automation plays the parameter's value |
| root | Detect and an accepted root suggestion change rootNote and rootCents and push 0 undo steps; Undo of a sound change restores that sound's root and cents; applying a suggested key is 1 undo step; the analysis cache reloads "no clear pitch" and "no key" as such (hasPitch, hasKey) |
| locate order | with the original moved: library (if on) by hash, then embedded bytes, then the Locate banner with every setting kept |

### M8 Performance: Rec, live template, wheels, presets, import

**Deliverable.** Rec through an audio-thread queue (risks 8.2) with velocity, bend (with bendRange), wheels and CC64,
one undo step per pass; the live template (A4, if cheap: a chosen note's lanes shape live keys; its note id in INST);
the Mod wheels tab with MIDI Learn and Let go (targets and CCs in INST); Sound and Clip preset menus kept separate
(U4) plus user presets (`.slsound` / `.slclip`, section 3); prototype save import (Paste or drop, section 3); notes
that cross the loop end hold on (F3); Vibrato Sync.

**Files.** `doc/Recorder.*`, engine RecTap (`rt::SpscFifo<RecEvent, 8192>`), `ui/tabs/ModWheelsTab`, `ui/PresetMenus`,
`state/PrototypeImport.*`, the F3 changes (`model::Glides` across the loop, `Clip::tieAcrossLoop`, engine `effLen`),
`Tests/plugin/test_rec.cpp`, `test_presets.cpp`, `test_import.cpp`, `Tests/engine/test_tie_loop.cpp`,
`test_vibsync.cpp`, `tools/fixtures/state.js`, `Tests/fixtures/state/prototype-*.json`, `state/import.json`.

| Test | Pass criterion |
|---|---|
| Rec timing | recorded note starts equal the played sample positions exactly (before quantize); with grid 1/16 snap error 0 |
| Rec lanes | a bend of +1 at bendRange 7 records +7 st (pitch lane 0.5 + 7/24 within 1e-6); wheel curves simplified with RDP 0.012 stay within 0.012 of the played values |
| Rec undo | one pass = 1 undo step; Overdub/Replace per open question 2; a pass that reaches 4096 notes stops adding notes with a toast, and the pass so far is still 1 undo step |
| presets | loading each of the 8 sound presets changes 0 clip bytes; loading each of the 12 clip presets changes 0 parameters; user preset save -> load is byte-identical (the `.slsound` / `.slclip` file) |
| import | each of the 4 prototype saves imports to `state/import.json`: the clip exactly (node values within 1e-12), the clip settings and UIST fields exactly, INST fields exactly, and parameter fields within `(double)(float)` of the fixture's value (1.6 #12); the import is 1 undo step; "Paste prototype save" with the same text gives the same result |
| F3 tie across the loop | a note crossing the clip end: min envelope across the loop point > 0.999, playhead continues within 0.002; with `tieAcrossLoop` off every `engine/renders.json` case still passes |
| Vibrato Sync | synced 1/8 at 120 bpm: vibrato period = 0.25 s within 1 %; Free is bit-identical to M3 |

### M9 Release

**Deliverable.** Factory sound and clip presets built on the starter sounds, README in GrainLock's style (PROMPT 15:
download, install to `C:\Program Files\Common Files\VST3`, rescan, Instrument track, drag a sample in, the modes table
with every gesture and key, mod wheels and MIDI Learn, recording, Quick Controls, ASIO-Guard note, how to report a
problem with Cubase version, sample rate, buffer and the sample used), committed sound fingerprints, the frozen
parameter copy `docs/parameters-v1.json`, and `tools/check-params.js` (fails CI if a released id, range or choice
list changes). Tag only when Kalan says so.

**Files.** `Tests/Fingerprints.h`, `Tests/FingerprintScript.h` (its `SL_FINGERPRINT` writers replace the placeholder,
7.1), `README.md` (with the prototype-save steps of section 3), `docs/parameters-v1.json`, `tools/check-params.js`,
`.github/workflows/build.yml` (one step: `node tools/check-params.js` in the Linux job; the fingerprint and state-fixture
uploads switch to `if-no-files-found: error`), `Tests/fixtures/state/v1-*.bin` (from the CI artifact, section 3).

| Test | Pass criterion |
|---|---|
| fingerprints | every sound preset x C1/C3/C5 x 2 s: the table matches within 0.1 % (GrainLock's rule) |
| frozen parameters | `check-params.js` and the M0 parameter test pass against `parameters-v1.json` |
| everything | all tests green on Windows and Linux, Debug-CRT 3 minutes 0 allocations, Release 10 minutes 0 allocations, pluginval strictness 10 |

### PROMPT section 14, folded in

| PROMPT 14 | Where | What v4 changed |
|---|---|---|
| 1 tuning both engines | M0 (probe), M2 Tape, M3 Voice and Lock | Cubase naming: the same MIDI notes 36/60/84 are now called C1/C3/C5 against a C3 root (PROMPT said C2/C4/C6 against C4); Tape, Voice and Lock gated; Cloud f0 printed only, because plain Cloud grains are not phase-aligned (+29 cents at 2x, reported but not gated by the prototype; 8.1 #28) |
| 2 formant | M3 | unchanged numbers |
| 3 glide | M1 (model), M3 (sound) | glide 0 = instant jump on the same voice; the glide aims at the target's real start |
| 4 lanes | M1 | Tone default 1.0; four Sample lanes added |
| 5 mode lock | M1 (model), M4 (gestures) | eight modes |
| 6 stability | M2, M3 | peak <= 0.98 (stricter than 0 dBFS), counted before the output guards so a NaN cannot pass as silence; 16 voices x 8 heads |
| 7 state | M0 (container), M1 (clip codec), M2 (the whole state with a clip, through the processor), M7 (sample) | binary chunks with length-prefixed records; embedding up to 20/50 MB (C4) |
| 8 allocations | every milestone | 10 rendered minutes in Release plus 3 minutes on the Debug CRT (PROMPT: 10 minutes Debug) |
| 9 host sync | M2 | plus 133 bpm, tempo change and ramp, jitter, cycles, pre-roll, chase |
| 10 UI snapshots | M0 (shell), M4 (roll modes), M5 (selection bar, ghosts, box select), M6 (Sample mode, Sample and Macros tabs, heads overlay) | plus Sample mode, 75 % and 200 % |
| 11 detection | M7 | unchanged numbers |

---

## 5. Golden fixtures: the C++ must reproduce the tested prototype

`tools/make-fixtures.js` (node, no npm packages) loads `prototype-v4/src/*.js` exactly as the prototype's tests do (the
engine through `new Function(createStrandEngineModule.toString())`), runs the generator modules in `tools/fixtures/`,
and writes `Tests/fixtures/<area>/<file>.json`. The areas are the modules found by listing `tools/fixtures/` (except
`common.js`), so adding one never edits `make-fixtures.js`; a module may also export `check()` (`params.js` writes no
file and checks `docs/parameters.json` against SLModel, section 2). `--only model,engine,...` limits it; `--check`
regenerates in memory, compares with the committed files, runs the checks, enforces the size budget, and exits 1 on
any difference (CI, Linux job, Node 24). The generators never call `Math.random`, `Date`, or `mkNote` without an id.
Two full runs give byte-identical files (checked after this plan's review: three runs, 24 files, identical).

### 5.1 Common encoding

```
File   = { fixture, version: 1, generator: "tools/make-fixtures.js",
           prototype: { "sl-model.js": sha256, "sl-engine.js": sha256, "sl-sounds.js": sha256, "sl-analysis.js": sha256 },
           clips?: { [name]: Clip }, cases: Case[] }          // one case per line, so diffs stay readable
F32    = { f32: base64 }                       // little-endian IEEE-754 binary32
Table  = F32 | { n, flat: number }             // flat: all n values equal this binary32 (written as its exact double)
Nodes  = [ [t, v, shape, bend], ... ]          // shape 0 smooth, 1 linear, 2 bend, 3 step
Note   = { id, p, s, l, vel, glide: number|null, gc, glideTo: string|null,
           sample: { motion, travel, start: {mode, pos, range, seed}, tape, rev, jump, engine, lock, restart },
           c?: { [laneId]: Nodes },            // omitted lanes = flat at the lane default
           gen?: { [laneId]: Gen } }           // Gen exactly as normalizeGen returns it
Clip   = { bars, notes: Note[] }
Inst   = the full object normalizeInst returns
```

Hashes are taken after converting CRLF to LF. Numbers are `JSON.stringify` output; `FixtureJson` parses them with
`std::from_chars`, so every double comes back bit-identical (do not use `juce::JSON` for fixtures: its number parser
is not specified to round-trip every double). NaN, Infinity and -Infinity, which JSON cannot hold, are written as the
strings `"NaN"`, `"Infinity"`, `"-Infinity"`. All comparisons treat +0 and -0 as equal. Every `hash` of audio (sounds,
signals, renders) is FNV-1a 32 (offset `0x811c9dc5`, prime `0x01000193`) over the little-endian bytes of each Float32
channel in order (L then R, all n frames; a mono signal is one channel), written as 8 lowercase hex digits: the
prototype tests' own `fnv`. A clip named `"preset:<id>"` is `loadClipPreset(id)`, and a case with `recipe: n` uses the
clip after `applyRecipe`; `tools/README-fixtures.md` lists the rest of the encoding and the render harness.

### 5.2 Named input clips (`tools/fixtures/common.js`, all ids explicit)

`strand-demo` and the other 11 clip presets (`loadClipPreset`); `shapes4` (4 notes: 0.5, 1, 2.75 beats with a Path-kind
Travel, 4 beats; a chord and a touching pair); `connect-grid` (chords, ties in pitch distance, the +-24 st limit, the
0.13-beat overlap edge, the 0.05-beat chord window, a link coming in from outside the selection); `voice-lead`
(chord changes with a bass that can only reach the next bass, explicit-link targets, glide 0, restart);
`chop-src` (a Different random generator, a Same pattern, a link, Start positions); `humanized-chord`; `gens`;
`mixed-settings` (every per-note override, all four Start modes, restart).

### 5.3 The fixture files

| File | Cases | JSON shape of a case | C++ comparison |
|---|---|---|---|
| `model/seeds.json` | ~1,900 | `{kind:"hash32", args:int[], h}`, `{kind:"toInt32", x, i}`, `{kind:"idHash", s, h}` (60 strings incl. "", "sd-1", "live:60", non-ASCII, surrogate pairs), `{kind:"rate", id, beats, label}` (21 + unknown), `{kind:"rateFromBeats", beats, tol, id}`, `{kind:"travelSpeed", v, s}` (300 incl. Float32 edges), `{kind:"travelSpeedToV", x, v}`, `{kind:"constants", F32_EPS, V_FWD_MIN, V_BACK_MAX}`, `{kind:"bendShape", u, bend, y}` (21 x 21), `{kind:"gShape", u, c, y}` (21 x 21), `{kind:"jsNumber", x, s}` (500), `{kind:"toFixed", x, d, s}`, `{kind:"toPrecision", x, d, s}`, `{kind:"base36", u, s}`, `{kind:"jsRound", x, r}`, `{kind:"scale", key, scale, inScale:"0101...", snap:int[128]}` (96), `{kind:"noteName", p, name}` (128) | integers and strings exact; the dead-band decision (`s == 0`) exact; arithmetic doubles exact; `bendShape`, `gShape`, `travelSpeed`, `travelSpeedToV` within 4e-16 relative |
| `model/nodes.json` | 80 | `{name, input (raw nodes, may hold lin:true, unsorted t, "NaN"), def, normalized:Nodes, sample257:F32, evalRight:number[33], evalLeft:number[33], slices:[{u0, u1, nodes}], reversed, flipped, scaled:[{f, nodes}], capped?}` | node lists: same length and shapes, t/v/bend within 1e-12; tables: every value within 2 Float32 ulps (printed: share bit-identical); eval within 1e-12 |
| `model/shapes.json` | ~420 | `{clip:"shapes4", lane, key, scope:"each"\|"across", opts:{seed?, bpm?, sampleSeconds?, region?}, notes:{[id]: Nodes}}`: every key of `shapeKeysFor` on all 10 lanes, Travel in both kinds, seeds 1 and 7, Fit with a looping and a holding Region | as nodes |
| `model/generators.json` | ~300 | `{note:Note, lane, res:256, table:F32, steps?:number[]}`: 7 pattern shapes x rates 1/16T, 1/8, 1/4D, 1bar, 1/64 x lockToBeat on/off x warp 0, 0.8, -0.5 (on 1-beat notes at s = 0, 0.37, 5.5); Random with smooth 0/0.4/1 x Same/Different x amount 0.5/1 x seeds 1/2 x key null/"other"; the pitch lane's 1/6 scale; invert. Plus `{op:"reroll", clip, ids, lane, after:{[id]:{gens:{[lane]:seed}, startSeed}}}` and `{op:"bakeGen", note, lane, res, nodes}` (incl. the 1/16 Squiggle over 4 beats that needs the raised eps) | tables within 2 Float32 ulps; `steps` (the seeded random value per step) exact; reroll seeds exact; baked nodes as nodes |
| `model/tools.json` | ~150 | `{op, clip:name, args, result:{notes?:Note[] (changed notes, in clip order), order?:string[] (all ids), refused?, count?, ids?, mixed?}}` for connect (nearest, furthest, random seeds 1-4), disconnect, chop (1/16, 1/8T, 1/4 x stepStart 0, 0.125, true; refusal at maxNotes 20), walk (start, vel, tone), humanize (seeds 1-3), strum up/down, legato, scaleLane x every lane x 1.5/0.5, reverseLane, flipLane, selectSimilar (all kinds), setNoteSample/getMixed, setLaneNodes, duplicateNotes, addNote, removeNotes, updateNotes | ids, order, links, flags, integers exact; p, s, l exact; node values within 1e-12 |
| `model/glides.json` | ~50 | `{clip:name, inst:{glideMode, glideTime}, glides:[[sourceId, d, len, curve, toId, linked]...] (Map order), chains:string[][]}` for 12 presets + `voice-lead` x off/legato/always x glideTime 0.5/0.25 | ids and order exact; d, len within 1e-12 |
| `model/snapshots.json` | 23 | `{clip:name, inst:Inst, res, snapshot:{version, bars, beatsPerBar, res, notes:[{id, idHash, p, s, l, vel, engine, lock, motion, travel, start, tape, rev, jump, restart, glide, chain, lanes:{[laneId]:Table}}]}}`: Strand Demo at res 256; the other 11 presets at res 64; Strand Demo with Auto Slide off and always (res 32); recipes 1-8 applied to Strand Demo (res 32); `mixed-settings` (res 64) | C++ compiles the same clip, resolves with the same inst (`core::resolve`) and compares field by field exact; tables within 2 Float32 ulps; table lengths exact |
| `model/presets.json` | ~100 | `{kind:"clipPreset", id, name, clip}` x 12; `{kind:"soundPreset", id, from:Inst, result:{sound, inst}}` x 8 x 2; `{kind:"recipe", n, ids, result:{inst, notes}}` x 8 x 2; `{kind:"dice", inst, seed, pins, result:Inst}` x 60 | exact (node values within 1e-12) |
| `model/format.json` | ~800 | `{lane, v, ctx:{inst, bpm, travelKind}, value, text}`: 41 values per lane x engine voice/cloud/lock/tape x refresh smooth/free/sync x bpm 120/87 x travel speed/path | text exact; value within 1e-12 relative |
| `model/normalize.json` | 18 | `{kind:"clip"\|"inst", input, result}` (v3 `lin:true`, missing and duplicate ids, numeric ids, dangling links, NaN nodes, garbage inst) | exact |
| `sounds/starter.json` | 10 | `{id, seed, sampleRate, n, rootMidi, peak, hash, blockRms:F32 (per 4096 frames, L R interleaved), windows:[{at, L:F32, R:F32}] (512 frames at 0, n/2, n-512)}`: 8 sounds at seed 1, pad and beat at seed 2 | n, rootMidi exact; windows within 2e-7 absolute; blockRms within 1e-6 relative; peak within 1e-7; hash printed (bit-exact share) |
| `support/signals.json` | 12 | `{fn, args, n, hash, head:F32, tail:F32}` (lcg: `values`) for the JS test generators ported to `TestSignals`: `midiHz`, `lcg`, `tone`, `noise`, `formantTone`, `smoothNoise`, `chord`, `rampSource` from `prototype-v4/tests/test-engine.js` and `dc` from `test-engine-extra.js`. They are not copied: `common.js` extracts each function's text by name from those files at run time, and the file header carries both files' hashes, so an edit there shows up in `--check` | within 2 Float32 ulps; lcg values exact |
| `engine/defs.json` | 1 | `{CTRL, MAX_HEADS, VOICE_GRAINS, CLOUD_GRAINS, LOCK_MAX_N, BUDGET_48K, budgets:{44100, 48000, 88200, 96000, 192000}, HEAD_TUNE, HANN:F32, FADE:F32, salts}` | constants exact; tables within 1 Float32 ulp |
| `engine/renders.json` | 73 | `{name, needs:"tape"\|"grains", sr, bpm, block: int \| int[] \| {random, min, max}, sound:{starter, seed}\|{signal, args}, inst:Inst, clip?:name\|Clip (compiled by the C++ model: also an integration test), snapshot?: raw snapshot (engine-only cases), transport:[[sample, {playing, bpm, beat?, loop}]], events:[[sample, "noteOn", p, vel, opts?] \| [sample, "noteOff", p] \| [sample, "ctrl", name, value\|null] \| [sample, "allOff"] \| [sample, "inst", patch] \| [sample, "snapshot", name\|Clip]], n, out:{hash, blockRms:F32 (per 256 samples, L R), blockPeak:F32, windows:[{at, L:F32, R:F32}] (256 frames at note starts, seams, joints, jumps), stats:[[sample, voices, grains, thinnedHeads, maxGrains, budget, cuts, steals]] (every 1024), display:[[sample, beat, playing, [[noteId, p, kind, engine, motion, env, released, [[i, pos, dir, level, thinned]...]]...], grainCount]] (every 4800), trace?:{every:32, head0pos:F32}}}` | if `hash` matches: bit-exact (printed). Otherwise windows within 1e-5 absolute (-100 dBFS); blockRms within 1e-6 + 1e-4 x rms; blockPeak within 1e-5; stats integers exact; display ids, kinds, engines, motions, released, dir, thinned exact, pos, level, env, beat within 1e-6; trace within 1e-6 |
| `analysis/detect.json` | ~30 | `{input:{starter}\|{signal}, detectPitch:{midi, cents, hz}\|null, detectKey:{key, scale, r}\|null, suggestParts:[{kind, label, start, end, bpm?, tempoFound?}]}` | as M7's test line |
| `ui/theme.json` | 2 | `{theme:"dark"\|"light", tokens:{[name]: value}}` parsed from `shell.html` | exact strings |
| `ui/inspector.json` | ~200 | `{fn:"fmtPos"\|"parsePos"\|"parseBeats"\|"parsePitch"\|"laneValueFromText", input, ctx?, output}` | strings exact, values within 1e-9 |
| `ui/sample-rules.json`, `ui/readouts.json` | ~400 | `{fn:"fades"\|"contexts"\|"grainKind"\|"macroText"\|"offsetText"\|"fmtX"\|..., input, output}` | exact |
| `state/prototype-*.json` | 4 | prototype saves in each prototype's own format (2 v3, 2 v4; no real saves existed, so they are written with fixed ids), the M8 import inputs | (inputs) |
| `state/import.json` | 4 | `{kind:"v3save"\|"v4save", inputFile, result:{clip, clipSettings, inst, where:{params, INST}, sound, ui, ignored, soundFileBanner}}`, generated by M8's `tools/fixtures/state.js` along section 3's import paths | clip exact (node values within 1e-12); parameter fields within `(double)(float)`; the rest exact |

**`engine/renders.json` cases** (48 kHz, 128-sample blocks unless named). `needs: "tape"` (36 cases, run from M2):
Tape forward C3 and C5 (saw); Tape Backward, Bounce in Region 0.2-0.3, Mirror with 2 heads, Scatter 1/8T at 87 bpm with
seeds 1 and 2 (`trace`), Refresh synced 1/16 stutter, Region loop with the post seam, the pre seam, Hold and Stop;
Tape's Path-as-1x; a chain C3 -> G3 legato, a glide-0 jump, a restart; chase at beat 1.37 and inside a chain; tempo
change 120 -> 87; a transport jump; a stop and restart; 3 loop passes of a 1-bar clip at 133 bpm (stats only); live keys
with liveLegato, sustain, mod1 -> Travel and Heads, bend at range 7, polyphony 4 with 6 keys; live Scatter while the
host plays (`trace`); Heads 2.5, 8 heads Spread 0.6, Drift 0.6, Head tune octaves; a 44.1 kHz sound at 48 kHz; a note
deleted and an inst change while playing. `needs: "grains"` (37 cases, from M3): Path kind on Voice and Cloud; the
chain C3 -> G3 and a Tape-to-Cloud chain; chase at beat 1.37 on Voice; live mod1 -> Travel and Heads on Voice; Voice at
C1/C3/C5 and with formant +-6; Cloud default, Spray 0.3, Skip 0.25, Refresh free 60 ms, Each grain backwards, the Tape
switch at 2x; Lock from noise and from a chord; Heads 2.5, 8 heads Spread 0.6, Drift 0.6, Head tune
octaves/fifths/detune; the budget cases, 16 voices x 8 heads Lock N = 12 (stats only): 16 live keys 256 samples apart
at 48 and 96 kHz, and 16 clip notes 0.05 beat apart at 120 bpm at 44.1, 48 and 96 kHz; Strand Demo for 2 s at 120 bpm
with 4 windows; recipes 3, 6 and 8 on Strand Demo (1 s each); Strand Demo with random block sizes; a raw engine-only
snapshot. Every case is generated with process() split at each event sample, as `MidiRouter` does, and every
transport start falls on a multiple of 32 samples, where the plugin's control grid (1.8) and the prototype's agree.

### 5.4 Size budget (enforced by `--check`: total < 5 MB, any file < 1.2 MB)

| Area | Measured (2026-11-08, after the review) |
|---|---|
| model/snapshots.json | 0.85 MB (Strand Demo at res 256 is 176 KB with flat tables compressed) |
| model/generators.json | 0.45 MB |
| model/tools.json | 0.34 MB |
| other model files | 1.36 MB |
| sounds + support + engine/defs | 0.24 MB |
| engine/renders.json | 0.99 MB |
| analysis, ui | 0.36 MB |
| state | 0.10 MB |
| **total** | **4.68 MB** |

Binary test inputs (the 2 MB WAV, the 45 MB embed, the 12-minute file) are made at test time by `TestSignals`, never
committed; only the release state fixtures (`state/v<version>-*.bin`) are binary, and they are outside the budget.

If the budget is exceeded, cut case counts (not tolerances) and say so in `docs/NOTES-M1.md`.

---

## 6. M0: Kalan's Cubase checklist (plain English)

Load the test build from the Actions page (the "StrandLock-VST3-Windows" download), copy `StrandLock.vst3` into
`C:\Program Files\Common Files\VST3`, open Cubase and let it rescan. Then go through this list and send back the
answers and the text from the Diagnostics page's **Copy report** button.

1. **Your setup.** Cubase version (Help > About Cubase). Your audio interface. Sample rate and buffer size (Studio >
   Studio Setup > your ASIO driver > Control Panel). Is ASIO-Guard on, and at which level (Studio > Studio Setup >
   Audio System)? Windows display scaling (Settings > System > Display > Scale, for example 150 %).
2. **It loads.** Add an Instrument track and pick StrandLock. Does it appear under Instruments? Does the window open?
   Change the plugin's UI Scale (75, 100, 150, 200 %): is the text sharp, and does everything fit on your screen?
   Switch between the dark and light theme (sun/moon button). Close and reopen the window, then open a new project:
   is your theme remembered?
3. **It plays.** Play C3 on your keyboard (or Cubase's on-screen keyboard). Do you hear a saw? Play a few chords. Is it
   in tune with a Cubase piano on another track? Move the pitch bend and the mod wheel: do they show on the
   Diagnostics page?
4. **Keys, with the Keys switch OFF.** Click on the StrandLock window, then press 1, 2, ... 8, Space, Ctrl+Z,
   Ctrl+Shift+Z, Ctrl+Y, Delete, the arrow keys, and A to K. The Diagnostics page lists every key StrandLock received:
   which ones appear? Which ones did Cubase still react to (Space starts and stops Cubase, 1 to 8 change Cubase's
   tools)?
5. **Keys, with the Keys switch ON.** Click Keys so it lights up and repeat step 4. Which keys now reach StrandLock, and
   which ones does Cubase still take first?
6. **Dropping sounds.** Drag a WAV file onto the StrandLock window from each of these: Windows Explorer; MediaBay; the
   Pool (Project > Pool); an audio event in the project window (a trimmed piece of a longer recording); a Cubase factory
   loop. For each one: did it load? The Diagnostics page shows the file path it received and the file's length. Was
   it the whole file or only the trimmed piece?
7. **Playing with Cubase.** Press Play. The Diagnostics page shows the host position, tempo and block sizes. Does the
   position start at bar 1? Set a 4-bar cycle and let it go round 3 times: does the "cycle wraps" counter go up, and do
   the positions at the wrap look like 16.000 or slightly off (for example 15.997)? The page also counts "wraps inside a
   block": does that number go up with ASIO-Guard on? Add a tempo change on the tempo track: does the shown tempo
   follow?
8. **ASIO-Guard.** Watch the block sizes with the track NOT record-enabled, then with the red record button on. Do the
   block sizes change? Does playing live feel delayed when the track is not record-enabled?
9. **While Cubase is stopped.** Stop Cubase. Does the "blocks while stopped" counter keep going up? Try it with Cubase's
   option "Suspend VST 3 plug-in processing when no audio signals are received" (Studio > Studio Setup > VST Plug-ins)
   on and off.
10. **Quick Controls.** Open the track's Quick Controls in the Inspector. Are QC 1 to 8 already Speed, Spread, Heads,
    Refresh, Grain, Spray, Tone, Formant? Turn QC 1: does the Diagnostics page show "Speed" as the last parameter
    Cubase changed?
11. **Automation.** Write a little automation for the Speed knob, then play it back. Does the knob move?
12. **Saving.** Change a few knobs, save, close the project and open it again. Are the knobs and the theme as you left
    them? How big is the .cpr file?
13. **Exporting.** Record a few bars of chords on the track, set the locators around them, and export them twice with
    File > Export > Audio Mixdown: once as usual and once with "Real-Time Export" ticked. Send both files. (They
    should be identical; the Diagnostics page shows "offline" during the first one.)
14. **CPU.** Hold a 4-note chord and look at Cubase's Audio Performance meter (Studio > Audio Performance). Roughly how
    high is it?
15. **Copy report.** Press Copy report on the Diagnostics page and paste the text into your reply.

---

## 7. File ownership for parallel builders (M0 and M1)

### 7.1 Step zero: the scaffold commit (on main, before either branch starts)

Made by the M0 builder from this plan, merged first, so both branches build and run CI from day one.

**Start from `origin/m0-skeleton`.** That branch (5 commits, pushed 2026-11-08 before this plan) already has GrainLock's
workflow on every branch, a silent IS_SYNTH processor with a versioned state, the skeleton editor, the theme and the
LookAndFeel. It is not merged as it is: the scaffold commit is built from it and changes (1) its explicit
`STRANDLOCK_SOURCES` list into the globs of 1.11 and the `StrandCore` library; (2) its 512 lines of tests written
inline in `Tests/TestMain.cpp` into self-registered `SL_TEST` files under `Tests/plugin/` (TestMain keeps only the
runner); (3) its state into the empty binary container header (M0 fills the chunks); (4) its dropped fingerprint step
into the placeholder rule below. After the scaffold merges, the branch is superseded (Kalan can delete it).

- `CMakeLists.txt` (with the globs of section 1.11), `cmake/StrandFlags.cmake`, `.github/workflows/build.yml` (1.11),
  `.gitattributes` (GrainLock's), `.gitignore` additions.
- **A minimal plugin**, so the VST3, pluginval and `--snapshot` steps have something to build, validate and draw from
  the first commit: `Source/PluginProcessor.*` (silent IS_SYNTH: stereo out, no input bus, accepts MIDI, latency 0,
  an empty state) and `Source/PluginEditor.*` (an empty themed editor at 1100 x 680). M0 owns and grows both.
- **Contract headers, complete per this plan** (small, fully specified by BLUEPRINT 1): `Source/core/Version.h`,
  `Seeds.h/.cpp`, `Rates.h/.cpp`, `Lanes.h/.cpp`, `Music.h/.cpp`, `Inst.h/.cpp` (the struct, `defaultInst`, `InstPatch`
  and `clampForEngine`, which M0's `ParamBridge` needs, fully implemented and spot-tested against `engine/defs.json`'s
  `normInst` cases; `mergeInst`, `normalizeInst` and the JSON functions declared), `JsCompat.h/.cpp` (`toInt32`,
  `toUint32`, `jsRound`, `f32`, and `jsToFixed`, `jsToPrecision`, which M0's readouts need, fully implemented and
  spot-tested against `model/seeds.json`; the rest declared), `JsonLite.h/.cpp`, `Source/rt/SpscFifo.h`.
- `Tests/TestMain.cpp`; `Tests/support/TestRegistry.h` with four registrars: `SL_TEST(group, name)`, `SL_SNAPSHOT(name)`
  (run by `--snapshot DIR`, writes `DIR/<name>.png`), `SL_FINGERPRINT(name)` (run by `--write-fingerprint DIR`) and
  `SL_STATE_FIXTURE(name)` (run by `--write-state-fixtures DIR`, writes `DIR/<name>.bin`), so later milestones add
  writers without touching TestMain. Until M9 the fingerprint run writes `DIR/placeholder.txt` ("no sound fingerprints
  before M9") so its upload always has a file.
- `Check.h/.cpp` (`check`, `section`, `fmt`, failure count, GrainLock's output format); `AllocCounter.cpp` (operator
  new/delete counters for new **and** delete, plus the largest single request and the bytes live, through a size
  header in the hooked `operator new` and `_msize` / `malloc_usable_size` on the CRT and glibc paths; the glibc
  interposition and the Debug-CRT hook from GrainLock); `FixtureJson.h/.cpp` (reader + base64 F32 + ulp compare).
- `tools/make-fixtures.js` (CLI, module discovery by listing `tools/fixtures/`, `--check`, per-module `check()`, size
  budget), `tools/fixtures/common.js` and `tools/fixtures/params.js` (the parameter check; architect-owned like the
  JSON it checks). These already exist in the repo (written and checked during this plan's review).
- `Tests/model/test_contract.cpp` (hash32 / idHash / rateBeats / clampForEngine / jsToFixed spot checks so the
  scaffold's CI is not empty).

### 7.2 Who owns what after the scaffold

| Area | M0 (plugin shell) | M1 (model core) |
|---|---|---|
| Source | `PluginProcessor.*`, `PluginEditor.*`, `params/**`, `state/**`, `host/HostInfo.*`, `diag/**`, `ui/**`, `sounds/**`, `probe/**` | `core/**` (everything, incl. finishing `Inst.cpp`, `JsCompat.cpp`), `core/Snapshot.h`, `model/**` |
| Tests | `Tests/plugin/**`, `Tests/support/Harness.*`, `Measure.*` | `Tests/model/**` (except `test_contract.cpp`, which M1 may extend) |
| Fixtures | `Tests/fixtures/sounds/**`, `Tests/fixtures/ui/theme.json` | `Tests/fixtures/model/**` |
| Tools | `tools/fixtures/sounds.js`, `tools/fixtures/ui.js` | `tools/fixtures/model.js` |
| CI and build | `CMakeLists.txt` plugin settings, `build.yml` | none |

**Shared files, and the rule for each**
- `CMakeLists.txt`: globbed directories, so no builder adds source lists. Only M0 edits it (plugin settings,
  definitions); M1 asks in `docs/NOTES-M1.md` if it needs a flag.
- `Tests/TestMain.cpp`: nobody edits it after the scaffold. Tests register themselves:
  `SL_TEST("model", nodesSampleSmooth) { ... }` (static registrar in `TestRegistry.h`; the runner sorts by group and
  name). `--only <group>` runs one group; groups: `contract`, `model`, `params`, `state`, `theme`, `sounds`, `probe`,
  `diag`, `ui`, `alloc`, `cpu`, then `engine`, `hostsync`, `analysis`, `rec`, `reference` in later milestones.
  `--snapshot DIR`, `--write-fingerprint DIR`, `--write-state-fixtures DIR`, `--alloc-minutes N`, `--fixtures DIR`
  and `--list`; the three writers run what `SL_SNAPSHOT`, `SL_FINGERPRINT` and `SL_STATE_FIXTURE` registered (7.1).
- `Tests/support/Check.*`, `AllocCounter.cpp`: frozen after the scaffold; a change needs both builders' notes.
- `Tests/support/FixtureJson.*`: owned by M1 after the scaffold, **append-only** while M0 is open; M0 only calls it
  (for `sounds/starter.json` and `ui/theme.json`).
- `Source/core/Inst.h`, `Rates.h`, `Lanes.h`, `Music.h`, `Seeds.h`, `JsCompat.h`: owned by M1, but **append-only** while
  M0 is open: no renames or reordering of anything M0 includes. M0 only includes them, and everything M0 calls from
  them (`defaultInst`, `InstPatch`, `clampForEngine`, `jsToFixed`, `jsToPrecision`, the rates, lanes and note names)
  is already complete in the scaffold.
- `tools/make-fixtures.js`, `tools/fixtures/common.js`: owned by M1 after the scaffold; M0 adds only its own module
  files (found by listing the folder). `tools/fixtures/params.js`: architect only.
- `docs/PLUGIN-PLAN.md`, `docs/parameters.json`: architect only. Builders write `docs/NOTES-M0.md` /
  `docs/NOTES-M1.md`.
- CLIP chunk: M0 stores and re-saves it as opaque bytes (its round trip uses random opaque bytes); M1 writes
  `model/ClipCodec`; M2 wires them together and runs the round trip over every clip preset. Neither M0 nor M1 calls
  the other's new code.

**Merge order.** Scaffold, then M0 and M1 in parallel; whichever finishes first merges; the other rebases (the file
sets do not overlap, so the rebase is mechanical). M2 starts from main with both.

### 7.3 Rules for every later milestone

One test file per area, self-registered; one fixture module and folder per area; new folders are globbed by CMake
already; a builder who needs a change in another milestone's file writes it in their `docs/NOTES-<milestone>.md` and
works around it; the architect folds agreed changes into this plan.

### 7.4 M2: who builds what, and the architect's decisions (2026-11-09)

**Order.** Contract (one builder; merges to main) -> the engine chain (E1, E2) and the plugin chain (P1, P2) in
parallel, each on its own branch -> integration (I) on main once both have merged -> review -> fix. E2 merges the
engine to main when green and P2 merges the plugin chain when green; until I, the processor's audio path stays
ProbeSynth, so main always builds and plays. I swaps ProbeSynth for the Engine. Each part writes its own section in
`docs/NOTES-M2.md` (Contract, E1, E2, P1, P2, Integration, Fix) with a Progress list.

**Ownership.**
- **Contract:** `rt/VersionExchange.h`, `rt/LatestSlot.h`, `rt/StableSort.h` (complete, with unit tests in
  `Tests/engine/test_rt.cpp`); `engine/EngineConfig.h`, `engine/SampleData.h`, `engine/DisplayFrame.h` (with
  `DisplaySlot` on `LatestSlot`); `engine/Engine.h` with the whole public API of 1.2 (`prepare`, `setSample`,
  `setInst`, `setSnapshot`, `setTransport`, `noteOn`, `noteOff`, `allNotesOff`, `setController`, `releaseController`,
  `process`, `fillDisplay`, `stats`, `resetStats`, `minEpochInUse`), and a stub `Engine.cpp` that renders silence;
  `doc/CommandQueue.h`; `Tests/support/TestSignals.*` (from `support/signals.json`).
- **Engine chain (E1, E2):** everything else in `Source/engine/` except `VoiceGrains.cpp` and `VoiceLock.cpp` (M3).
  E adds private members and helpers freely but keeps the contract's public API; an addition to it goes in NOTES-M2.
  Tests: `Tests/engine/test_parity`, `test_tuning_tape`, `test_chase_cost`, `test_determinism`, `test_seams`,
  `test_live`, `test_alloc_engine`, `test_stability`, `test_cpu` (engine level: a test drives `Engine` directly).
  `tools/fixtures/engine.js` and `Tests/fixtures/engine/**` (append-only; `--check` stays green).
- **Plugin chain (P1, P2):** `doc/**` (except `CommandQueue.h`), `host/HostSync.*`, `host/MidiRouter.*`,
  `params/**`, `state/**`, `diag/**`, `ui/**`, `PluginEditor.*`, and `PluginProcessor.*` for everything except the
  audio path. Tests: `Tests/plugin/**`, `Tests/ui/**` (including `test_state_clip.cpp` and HostSync / MidiRouter unit
  tests against a recording fake engine).
- **Integration (I):** the processor's audio path (MidiRouter + HostSync + Engine, `DisplaySlot`, `setInUse` /
  `collect`), deleting `Source/probe/` and its tests, `Tests/engine/test_hostsync.cpp`, `test_exchange.cpp`, the
  processor-level live, edits-while-playing, determinism (`setNonRealtime`) and allocation rows, and an M2 section in
  `docs/CUBASE-CHECKLIST.md`. Once E and P have merged, I may touch any file to integrate.

**Decisions.**
1. **Host parameter changes never allocate, with the editor open or not.** M0's fix pass measured JUCE's
   `ParameterAttachment` posting a message from the audio thread (NOTES-M0). From M2 the editor binds no control
   through `ParameterAttachment` or the APVTS attachments: a control writes its parameter on the message thread
   (`beginChangeGesture`, `setValueNotifyingHost`, `endChangeGesture`), and the editor's 30 Hz timer reads each bound
   parameter and moves its control when the value changed (never while the user is dragging it).
   `alloc/hostChangesWithEditorOpen` then gates 0 allocations on every platform. (P2)
2. **Light theme colours** per question 9.1. (P2)
3. **Loading a state that names another sound.** `setStateInformation` hands the sound to the SampleManager and waits
   on its own thread for the worker (the worker never needs the message thread, so this cannot deadlock), up to 10 s,
   so the first block after a load, or an offline export started right after it, plays the loaded sound. If the sound
   is not ready in time, clip notes are silent until it arrives and the diagnostics count those blocks. The worker keeps
   the last 4 starter sounds it made, keyed by (id, seed), so a project that uses the current or a recent starter does
   not make it again. The 50 ms load and 60 ms `sawopen` targets stay, printed per the timing rule. (P1)
4. **Files in M2.** Browse and drop decode through `AudioFormatManager::registerBasicFormats()` on the worker. A file
   longer than 10 minutes loads its first 10 minutes and the toast says so (question 9.3). SMPL's file record (kind 1)
   is written from M2 (path, size, SHA-256, rate, channels, frames, the loaded range, display name; the analysis cache
   empty) and a project reload decodes the path again. When the file is missing the default starter plays, the toast
   says which file is missing, and the SMPL record is kept verbatim so a save does not lose it. The Locate banner and
   embedding stay M7. (P1)
5. **ProbeSynth goes.** I deletes `Source/probe/` and replaces the probe tuning test with Tape tuning; every other M0
   test that still applies keeps running.

### 7.5 M3: who builds what, and the architect's decisions (2026-11-10)

**Order.** M3 is engine work in files that touch each other (Voice::renderRun's dispatch, `playedEngine`, Head's grain
slots and Lock captures, the planner), so it runs as one chain on branch `m3-engine`: G (runGrains: Voice and Cloud)
-> L (runLock) -> B (the complete planner and the cross-engine rows). Each part fast-forwards main when CI is green,
so main always builds and plays and a cut-off loses at most one part. Then review and a fix pass. Each part writes
its section in `docs/NOTES-M3.md` (G, L, B, Fix) with a Progress list.

**Ownership.** All three parts own `Source/engine/**`, `Tests/engine/**`, `tools/fixtures/engine.js` and
`Tests/fixtures/engine/**` (append-only). Outside the engine they touch only what the switch needs: the Diagnostics
and Copy-report lines and the Sound tab note that say Voice and Cloud play as Tape (G removes the Voice and Cloud
part, L the Lock part), and `docs/CUBASE-CHECKLIST.md` (B adds an M3 section).
- **G:** `VoiceGrains.cpp` (Voice: root-grid PSOLA with the formant shift; Cloud: free grains in ms), Spray, Each grain
  backwards, Path kind for Voice and Cloud, `playedEngine` playing Voice and Cloud; rows: the `grains` fixture cases
  that need no Lock, Voice tuning, formant, glide, motions, Scatter, Refresh and Skip, Spray, the grain chase.
- **L:** `VoiceLock.cpp` (lockGrab, lockRho, runLock: Hann-weighted taps, correlation-corrected re-grab), the Lock
  switch on Cloud, Refresh hold; rows: the Lock fixture cases, Lock tuning, the Lock chase.
- **B:** `EnginePlan.cpp` complete (thinning, waiting heads, cuts in every engine), per-note engines and the Tape
  switch across chains, fractional heads; rows: budget (every `budget-*` fixture stat exactly), stereo, stability,
  determinism, CPU at 96 kHz, the aliasing figure, and every `grains` fixture case still green.

**Decisions.**
1. **Light theme contrast on what is drawn (M2 review R2-4).** The light theme's Tone (#d78000) and Sample (#b68f39)
   lane colours and the playhead (#ed7100) are darkened, same hue, to at least 3:1 against the darker roll row
   #eef0f5 and the strip #e8ebf6, not against #ffffff. The theme test's reference and its "no darker than needed"
   bound move with them. Done in the M3 fix pass.
2. **The chase's first share stays 4096 ticks (M2 review R2-9)** until Kalan's buffer size is known from the
   checklist; at 256 samples the one-off block fits.
3. **Question 9.6 (Tape far up).** B prints the Tape +36 st aliasing figure; the architect takes it to Kalan.
4. **CPU.** The 96 kHz targets of the M3 table are printed and gated at > 1x real time, as written. M2's Tape figure
   (7.6x against 8x) is reported, not chased.

---

## 8. Deviations

### 8.1 From PROMPT.md (the v3-era brief), and why

| # | PROMPT | Plan | Reason |
|---|---|---|---|
| 1 | Tests and names call MIDI 60 "C4" | MIDI 60 = C3 everywhere | Cubase naming (C5 pick, BLUEPRINT 1.1) |
| 2 | Engines Formant / Classic | Tape / Voice / Cloud, Lock a switch on Cloud | D1; v4 prototype |
| 3 | Seven modes | Eight: 8 Sample with Travel / Heads / Grain / Refresh | v4 (S3, S6) |
| 4 | Tone default 0.75 | 1.0 (open, about 18.9 kHz) | F1: 0.75 put a 4.5 kHz low-pass on every note |
| 5 | Classic formant EQ +9 dB | Tape formant EQ 18 x \|v - 0.5\| dB (0 dB at centre) | F2 |
| 6 | Classic rate omits the sample-rate ratio | rate x (sample rate / host rate) | F2: a 44.1 kHz file played 1.47 st sharp at 48 kHz |
| 7 | Vibrato `#A66BFF`; lane colours identical in both themes | Vibrato `#E05CFF`; the light theme uses darker lane lines (shell.html) | U8: vibrato was 1.3:1 against the accent; light lines were about 2:1 on white |
| 8 | Playhead in the accent colour | `#FF7A00` | v4 shell.html; risks 11 (accent was confusable with selection and vibrato) |
| 9 | Keyboard focus while the mouse is over the roll | Keys toggle; never Space; every action on screen | C2; risks 9.1 (Cubase takes 1-8, Ctrl+Z) |
| 10 | Start offset and Sustain loop | Region start/end, At the end (Loop/Hold/Stop), crossfade; per-note Start | S1 |
| 11 | Parameter list 8.3 | 50 parameters, macros first (D8), offsets kept (A3), PROMPT ids kept where the meaning survived (`sustainLoop`, `startOffset` retired, not reused; `scaleKey`, `scaleType`, `scaleLock`, `mod1Target`, `mod2Target` moved to state, #27) | D8, A3, Z3, task rules |
| 12 | `glideTime` as 1/32 .. 1/2 | continuous beats 0..16 | a choice list could not grow after release (risks 6.1) |
| 13 | Link with glide 0 = no glide | glide 0 = instant pitch jump that still plays on one voice | v4 (critique verdict) |
| 14 | Glide target not specified | from the source's real end to the target's real start (p + first/last pitch node) | v4 model (NOTES-model A.6) |
| 15 | Each note restarts the sample | slides keep the sample rolling on one voice (Restart sample here to opt out) | D2 |
| 16 | "About 4 grains", "renders ahead", "about 15 ms" | 8 grains per head with shorter grains above, budget 192/48 kHz, no lookahead, response within one grain | D3, F5, risks 2.2/2.4 |
| 17 | Clip 1-32 bars | 1-64 bars (prototype `newClip`), stored with `beatsPerBar` (default 4) | prototype; risks 4.1 (3/4 and 6/8 songs) |
| 18 | Clip in a ValueTree with juce::UndoManager | immutable versions + `VersionHistory`; binary state | risks 7.1, 3.4: one copy of the truth, no XML |
| 19 | Embed the audio under 50 MB, toggle | the original bytes up to 20 MB without asking, ask to 50 MB, Region-only FLAC above, opt-in library | C4 |
| 20 | 10-minute Debug allocation run | 10 rendered minutes in Release + 3 minutes Debug CRT, counting frees too | GrainLock's CI; risks 10.1 |
| 21 | Peak <= 0 dBFS | peak <= 0.98 | the v4 limiter; stricter |
| 22 | Pattern tab (5 shapes, 1/2-1/32, Apply) | generators that ride on the drawing (7 shapes, the shared 21-rate picker, Re-roll, Turn into nodes) | Y3, Y4, Y9, A1 |
| 23 | Presets: one menu | Sound presets and Clip presets separate (U4); PROMPT's 6 clips kept, 6 added | U4 |
| 24 | Mod wheel targets = the 6 lanes | any of the 10 lanes incl. the Sample sub-lanes | A4 |
| 25 | Live notes engine unspecified (v3: Classic only) | live keys use the selected engine and Sample settings | A4 |
| 26 | Default sound not stated | opens on the Saw that opens up | D9 |
| 27 | Root, key, scale, polyphony, engine, mod targets as parameters (8.3) | root, cents, polyphony and engine stay parameters (Cubase can automate retuning, voice count and the engine; their ranges and lists are fixed); key, scale, scale lock and the two mod-wheel targets are plugin state (2.2), undone with the clip | Z3, Kalan's v4 rule, outranks PROMPT 8.3: "no list that can grow after release", and the scale list and the lane list will grow (risks 6.1, 6.2); the engine never reads key or scale, so as parameters they gave Cubase nothing and only froze their lists |
| 28 | Tuning tests for both engines (14.1) | Tape, Voice and Lock gated within +-3 cents; Cloud f0 printed only | plain Cloud grains are not phase-aligned and read +29 cents at 2x; the prototype reports this without gating it (NOTES-integrator) |
| 29 | Detected root and key "can be undone" (10) | Detect and an accepted root suggestion set the root without an undo step; Undo of a sound change brings that sound's root back; applying a suggested key is one undo step | v4 (sl-ui.js: "Root and cents belong to the loaded sound: they are not undo steps") |
| 30 | (none) Macro parameters named by their targets | ids `macro1` .. `macro8`, names the D8 labels | D8 and A2: a macro can be pointed somewhere else later, and a frozen id must not name its target |

### 8.2 From the prototype, and why

| # | Prototype v4 | Plugin | Reason | Parity |
|---|---|---|---|---|
| 1 | Own Play button and tempo box | host transport only, plus Preview while stopped (U5) | PROMPT 4 | n/a |
| 2 | Snapshot bakes the instrument's defaults; recompiles on `COMPILE_KEYS` | "follow" markers resolved by the engine every tick, chains for all 3 glide modes | automation and offline export must work on the audio thread alone | identical for a fixed inst (fixture-tested) |
| 3 | Tempo change re-anchors to the engine's own clock | re-anchors to `cur + d` (d reduced modulo the clip, at most 0.1 beat); a scan horizon stops double starts when it moves back, and the notes it skips when it moves forward start at once (1.10) | tempo automation must not drift (240 samples over a ramp, risks 4.2/4.4) and a late cycle wrap must not drop the downbeat | identical with a constant tempo |
| 4 | Negative beats wrap into the clip (`loopBeat` and `setTransport`'s `wrap()`, floor-modulo or `max(0, b)`) | never wrapped before 0, in all four places (1.10) | pre-roll must stay silent (risks 4.8) | identical for beats >= 0 |
| 5 | While stopped the page sends a beat only on purpose | HostSync passes the host beat only when it moved | otherwise the free clock would never run | n/a |
| 6 | Parameter jumps at control rate; outGain is one gain per `process()` call | 20 ms ramps for `ramp20ms` parameters; outGain and the bypass fade ramp per sample (2.3) | zipper-free automation | identical for static values; parity tests set 0 ms |
| 7 | Flat lanes stored at full length | 2 values | memory | bit-identical |
| 8 | Voice array grows without limit | 64-object pool; if exhausted, the oldest stealing voice is cut (counted) | real-time safety | identical below 64 objects |
| 9 | `freshId` uses Date.now | deterministic ids only | reproducible ids and seeds | n/a |
| 10 | Rec samples the wheels on a 30 ms UI timer, bend fixed at +-2 | audio-thread queue, bendRange | risks 8.2/8.3 | n/a |
| 11 | Notes are cut at the clip end | F3: they hold on into the next pass (`tieAcrossLoop`, on by default), slides into the next pass allowed | Kalan's pick F3 | off = prototype |
| 12 | No Vibrato Sync | `vibSync` parameter | PROMPT 8.3 id; A1 one rate picker | Free = prototype |
| 13 | `beatsPerBar` always 4 | clip setting, default 4 | 3/4 and 6/8 songs | 4 = prototype |
| 14 | Float32 samples always | int16 above 3 minutes (M7) | RAM for long songs (risks 3.1) | parity cases use float |
| 15 | No anti-aliasing | none in v1 either; measured in M3 | the prototype is the contract; open question 6 | identical |
| 16 | Live wheels act on live voices only | same | prototype; open question 4 | identical |
| 17 | Sample saved in browser storage | embedding, library, Locate | C4 | n/a |
| 18 | Engine `normInst` clamps wider than the model's (speed +-16, grainLock 64, Refresh 10 s, crossfade 1 s, vibRate 50, outGain +24) | parameter ranges follow the model's `normalizeInst`; the engine keeps its clamps for safety | the UI never produces the wider values | identical for UI-made values |
| 19 | The display carries note id strings | `noteIdHash` | no strings across threads | n/a |
| 20 | MIDI velocity: the page sends 0..1 | `vel / 127`; CC120/123 all notes off; bend full up = +1 exactly | integrator note; MIDI | n/a |
| 21 | The control grid (`plan()`, segment splits) counts from the engine's creation | counts from the last transport start (1.8) | the same song must render the same whatever ran before Play (risks 4.5; measured difference 0.040) | identical when Play starts on a multiple of 32 samples (every fixture does) |
| 22 | `chase()` replays every tick inside one block | far past coarse, at most 4096 ticks at once, then 256 per 32 samples (1.8) | about 270 ms of work in one 5 ms block would make Cubase drop out at every Play | identical for chases under 4096 ticks (every fixture case) |
| 23 | A sample swap switches stolen voices to the new source at full gain | they finish their 5 ms fade on the old sample (1.3) | no click when a sound is dropped in while playing | identical when nothing sounds at the swap |
| 24 | The worklet sees the transport once per block | HostSync splits the block at a cycle wrap inside it (1.10) | ASIO-Guard's large blocks | n/a |
| 25 | Display messages posted to the page | a latest-value slot (1.5) | the UI always shows the newest frame | n/a |
| 26 | The Region is a double | exact f64 edges in state, the parameter as the automation layer (2.2) | a float32 Region edge is up to 3.4 frames off on a 10-minute file | identical |
| 27 | Ids of any length | an imported id over 255 UTF-8 bytes is renamed by `normalizeNote`'s missing-id rule (section 3) | the CLIP format's `str8` | only pathological imported saves |
| 28 | `normalizeInst` keeps a fractional root or bend range | rootNote and bendRange are int parameters and round it | parameter types; the prototype UI only makes whole values | identical for UI-made values |
| 29 | `scanStarts` walks every note each segment | binary search on the start-sorted notes (1.2) | CPU with large clips (about 11 % of a core at 4096 notes, 96 kHz) | identical |
| 30 | Only Chop caps the note count | every edit capped at 4096 notes and 32 MiB encoded, refused with a toast (section 3) | StrandLock must never save a project it cannot load | identical below the caps |
| 31 | The rate "1bar" is 4 beats | the same, and the UI reads it "4 beats" when beatsPerBar is not 4 (2.1) | 3/4 and 6/8 songs | identical |
| 32 | `plan()`'s fixed wait list size | 512 (head, voice) pairs (1.2) | 64 voices x 8 heads | identical |

### 8.3 From the research (critique / risks), and why

- CycleLock's ShiftVoice and an EpochMap are not ported for v1 (critique M4): v4's Voice is the root-grid PSOLA
  (BLUEPRINT 3.3); real pitch marks are E4, later.
- Lock, Drift, Head tune, recipes and Dice are in M1/M3, not M7: the prototype already has and tests them.
- The rate picker is BLUEPRINT 1.5's 21 ids (no 2/4/8 bars or Free on it; critique R1 proposed them); Refresh has its
  own Free mode and ms, Vibrato its own Free.
- The Sample lane colour is sand `#E2C275` (dark) / `#B8913A` (light), as v4 chose.

---

## 9. Open questions for Kalan

1. **Light theme colours.** In the light theme the Tone (orange) and Sample (sand) lines and the orange playhead are a
   little faint on white (2.6 to 2.9 to 1; 3 to 1 is the usual minimum). Darken them a touch, or keep the prototype's
   colours?
2. **Recording loops.** When the loop comes round while Rec is on, should new playing add to the notes already there
   (overdub) or replace the notes in that stretch?
3. **Long songs.** Is 10 minutes a long enough limit for one file? Above it StrandLock would offer to load just a part.
4. **Mod wheels.** Should the mod wheels also move the notes the clip plays, or only the keys you play live (as in the
   prototype)?
5. **Engines later.** The engine list (Tape, Voice, Cloud) is fixed at the first release, as Cubase stores automation
   by position. A fourth engine later would come as a separate switch. Is that all right?
6. **High transposition.** Tape played three octaves up sounds harsh in the prototype (no anti-aliasing). Fix it in
   v1 (more memory per sound, one more build stage), or leave it for later? The M3 build will measure how harsh.
7. **Quick Controls.** If your Cubase does not pick up the 8 macros as Quick Controls by itself (checklist step 10),
   is a one-time README step to assign them fine?
8. **Lock at 96 kHz.** At 96 kHz StrandLock has half as much work to spend per sample (the CPU budget is the same per
   second), so a Cloud + Lock note with long Lock grains (Grain 12) and 8 heads plays only 4 of its heads, as in the
   prototype. Keep that (it protects Cubase from overloads), or let the newest note keep all 8 at 96 kHz (more CPU)?
9. **Pressing Play inside long held notes.** To stop Cubase from crackling when you press Play in the middle of long
   notes, StrandLock catches those notes up in small steps. Notes held for many bars can come in up to about a third
   of a second after Play. Is that all right, or would you rather they start at once and risk a crackle?

**Kalan's answers (2026-11-09): "Defaults".** So:
1. **Light theme:** in the light theme the Tone and Sample lane colours and the playhead are darkened (same hue) to at
   least 3:1 against the background the M0 contrast test uses. The prototype keeps its colours; `ui/theme.json` stays
   the prototype's, and the theme test checks these tokens for >= 3:1 instead of equality. M2 (7.4).
2. **Recording loops:** a loop pass while Rec is on adds to the notes already there (overdub). M8.
3. **Long songs:** 10 minutes per file is enough; above it StrandLock offers to load a part (M7). Until M7 a longer
   file loads its first 10 minutes and says so (7.4).
4. **Mod wheels** move only the keys played live, as in the prototype.
5. **Engines:** Tape / Voice / Cloud is fixed at the first release; a fourth engine would come as a separate switch.
6. **Tape far up:** decided after M3 measures how harsh it is; the architect brings Kalan the figure.
7. **Quick Controls:** a one-time README step is fine if Cubase does not pick the macros up by itself. M9.
8. **Lock at 96 kHz:** keep the prototype's thinning (the newest Lock note plays 4 of its 8 heads at B = 96).
9. **Play inside long notes:** keep the bounded chase (1.8); such notes may come in up to about 0.35 s after Play.

---

## 10. Review log (2026-11-08)

Three reviewers (params-state, realtime-engine, milestones-ci) and the fixture builder reported on the first version
of this plan. Each finding was checked against the sources before it was acted on: the cited prototype lines were
read, and the reviewers' simulations (`sim1.js` .. `sim8.js`: budget, sample swap, chase cost, tempo re-anchor,
pre-roll, chase samples, control-grid phase, scan cost) were run again on the unchanged prototype; every measurement
reproduced within machine noise (chase 267 ms vs 272 ms; scan 9.9 % vs 11.6 % of a core). No finding was rejected
outright; two were applied in part (marked). The fixture generator was changed to match (`needs` tags, Tape cases,
`state/import.json`, the parameter check), run three times with byte-identical output (24 files, 4.68 MB), and
`--check` passes. `docs/parameters.json` now has 50 entries.

**params-state**

| # | Sev | Finding | Verdict | Done |
|---|---|---|---|---|
| P1 | high | CLIP records have no lengths; unknown lanes and per-note fields are lost on re-save | confirmed (`normalizeNote` loops only over today's 10 lanes) | every CLIP record length-prefixed and append-only; enums stored as strings; unknown trailing bytes, lanes and enum values kept in `Note::extras` and written back; tests (b)-(d) (3, M1, M2) |
| P2 | medium | decoder limits refuse states the editor can make | confirmed (a fully drawn note is about 36 KB; only Chop caps notes) | one limits table for decoder and editor: 4096 notes and 32 MiB per clip enforced on every edit with a toast; total limit derived from the chunk limits; largest-editable-state test (3, M1, M2, M5, M8) |
| P3 | medium | v4 import is not the prototype's reopening path; seed, CCs, snap, loop, UI fields unmapped | confirmed (sl-ui.js `init()` + `switchSound`) | import specified as that path (root 60 / cents 0, Region reset when `soundFile`), every field's destination listed; results generated by running it into `state/import.json` (3, 5.3, M8) |
| P4 | medium | v3 save keys mean different things in v4; no way to get a save file | confirmed (v3 `save()` at prototype-v3 line 734) | v3 mapping specified; README devtools line + "Paste prototype save"; v3 inst results now in the fixture (3, M8, M9) |
| P5 | medium | embedding toggle and answer not saved | confirmed | SMPL `keepInProject` and `embedAnswer`; asked again only for another file (3) |
| P6 | medium | no loaded-part offset for long files | confirmed | SMPL `loadedStartFrame` / `loadedFrames`; fractions relative to the loaded range; 12-minute part reload test (3, M7) |
| P7 | medium | Region-only excerpt: offsets unspecified, outside-excerpt playback undefined, no write cap | confirmed | excerpt start/length in original frames; Region mapped and clamped into the excerpt with a banner; never written above 50 MB (3, 2.4) |
| P8 | medium | float32 Region edges lose frames on long files | confirmed (28.8 M frames, up to 3.4 frames per edge) | exact f64 edges in INST, used while the parameter equals their float; drift test (1.6 #12, 2.2, 3, M7) |
| P9 | medium | macro ids name their targets though D8/A2 allow re-pointing | confirmed; applied in part | ids `macro1`..`macro8`; names kept as the D8 labels (what Quick Controls show), not "Macro 1 Speed"; the Sample knobs already carry "Sample ..."; names follow the target later via `updateHostDisplay` (2, 8.1 #30) |
| P10 | medium | key, scale, scale lock and mod targets frozen as growing choice lists (Z3) | confirmed | moved to INST state (lane-id strings for the targets); PROMPT conflict recorded (2, 2.2, 8.1 #27); 50 parameters |
| P11 | low | exact-equality tests through float parameters | confirmed (velSens 0.7 -> 0.699999988) | `(double)(float)` rule stated in 1.6 #12 and in each test line (M0, M6, M8) |
| P12 | low | step intervals snap typed and imported values | confirmed (Lock Grain 4.3 -> 4.5 -> 5 taps) | every float continuous (step 0), knob snapping in the UI as the prototype's `q` functions; JSON updated; M6 test |
| P13 | low | unreadable blobs and bad chunks are overwritten by the next save | confirmed | kept verbatim and written back until an edit, with a banner; test (e) (3) |
| P14 | low | root/cents undo behaviour differs from v4 without saying so; touchedBy empty | confirmed | follows v4 (not undo steps; Undo of a sound change restores its root); 8.1 #29; touchedBy filled for root, cents and Region (2.4, M7, JSON) |
| P15 | low | "1 bar" undefined for beatsPerBar other than 4 | confirmed | rates stay quarter-note beats; UI reads "4 beats" when beatsPerBar is not 4 (2, 8.2 #31, JSON notes) |
| P16 | low | analysis cache cannot say "no pitch" / "no key"; labels not stored | confirmed (labels hold the part's time) | `hasPitch`, `hasKey`, label strings stored (3, M7) |
| P17 | low | Dice pins, last preset, live template, user presets have no home | confirmed | UserSettings; INST `liveTemplateNoteId`; `.slsound` / `.slclip` files (3, M8) |
| P18 | low | parameters.json claims to be generated; nothing checks it | confirmed (no generator in the repo) | `tools/fixtures/params.js` checks it against SLModel on every `make-fixtures.js` run, CI included (written, run, and shown to catch injected errors) (2, 5, 1.11) |

**realtime-engine**

| # | Sev | Finding | Verdict | Done |
|---|---|---|---|---|
| R1 | high | releasing voices read a snapshot `collect()` may free | confirmed (sl-engine.js setSnapshot keeps the old note for released voices) | per-voice snapshot and sample epochs; the engine reports the oldest epoch in use after `process`; mailbox exchange; ASan/UBSan Linux job; churn test (1.3, 1.11, M2) |
| R2 | high | chase replays up to 2^17 ticks per voice in one block | confirmed (sim: 267 ms in a 5.3 ms block; 1.05 s for 64-beat Voice notes) | bounded, sample-scheduled chase: coarse far past, 4096 ticks at once, then 256 per 32 samples; tick-count gate; open question 9 (1.8, 8.2 #22, M2) |
| R3 | high | re-anchor drops notes when it moves forward; anchor value unspecified | confirmed (sim: downbeat 1 of 4 passes) | anchor `cur + d` with d reduced modulo the clip, forward-gap notes start at k0, backward horizon; tempo-and-cycle test (1.10, 8.2 #3, M2) |
| R4 | high | M3 budget criterion contradicts the prototype | confirmed (sim and fixtures: 15 cuts; 4 heads at 96 kHz) | M3 criteria rewritten from what the JS does; Lock N = 12 compared with fixture stats; 44.1 kHz budget case added; open question 8 (1.8, M3, 5.3) |
| R5 | medium | planner grid counts from engine creation, so renders depend on pre-roll samples | confirmed (sim: 0.040 difference) | grid anchored to the last transport start; 0/7/1000-sample tests in M2 and M3; fixture transport starts on multiples of 32 (1.8, 8.2 #21) |
| R6 | medium | sample-swap port contradicts its own gate | confirmed (sim: 10.9 and 1.57) | click-free swap chosen: stolen voices fade on the old sample (epoch keep-alive); gate on a sine pair (1.3, 8.2 #23, M2) |
| R7 | medium | tempo-drift test cannot tell the prototype from the fix | confirmed (sim: stepped host 0 error; continuous host 240 samples) | fake host specified (per-sample ppq, tempo per block); -1..+1 samples criterion; [0, 1) kept for constant tempo (M2) |
| R8 | medium | wraps inside a large block are not split | confirmed as a risk (depends on Cubase with ASIO-Guard) | HostSync splits at the computed wrap sample; mid-block wrap tests; checklist question (1.10, M2, 6) |
| R9 | medium | pre-roll still wrapped by `setTransport`'s `wrap()` | confirmed (sim: rms 0.20 before bar 1) | no wrapping of negative beats in all four places; test with loop on and off (1.10, 8.2 #4, M2) |
| R10 | medium | stability criteria cannot fail (guards hide NaN) | confirmed (output guard and biquad reset) | nonFinite / filterResets counters, pre-limiter peak, minimum rms in every stability line (1.8, M0, M2, M3) |
| R11 | low | GrainLock's FIFO shows the oldest frame after a pause | confirmed (ScopeFrame.h drops new frames when full) | triple-buffered latest-value slot; criterion kept (1.5, M6) |
| R12 | low | chase 1e-4 equality is false for grain engines | confirmed (sim: Voice 1.04) | limited to Tape and named; JS criteria for Voice/Cloud/Lock in M3 (M2, M3) |
| R13 | low | back-pressure plays a stale version after Play | confirmed | mailbox replaces the waiting version; test (1.3, M2) |
| R14 | low | live wheel test cannot tell CC 31 from 32 | confirmed (travelSpeed(31/127) = -1.0548) | inputs named: `setController(0.25)` within 1e-6, CC 32 within 1e-4 (M2) |
| R15 | low | insertion-sort size wrong for the wait list; start overflow unspecified | confirmed | 512 pairs; 256-start overflow rule with a counter and test (1.2, 1.6 #7) |
| R16 | low | `scanStarts` cost grows with clip size | confirmed (sim: 9.9 % at 4096 notes) | binary search, same starts and order; printed CPU case (1.2, M2) |
| R17 | low | outGain and bypass ramps would staircase | confirmed (sl-engine.js: one gain per call) | per-sample interpolation in the output loop (2.3, 8.2 #6) |
| R18 | low | SnapshotCache keyed by pointer can hit a reused address | confirmed | keyed by `Note::serial` (1.2, 1.3) |
| R19 | low | live Scatter clock wording | confirmed (`songBeat`) | wording fixed; `tape-live-scatter-playing` render added (1.7, 5.3, M2) |

**milestones-ci**

| # | Sev | Finding | Verdict | Done |
|---|---|---|---|---|
| C1 | high | scaffold has no processor or editor, so CI cannot be green; branch CI trigger | confirmed | minimal silent processor and empty editor in the scaffold; `branches: ['**']` (1.11, 4, 7.1) |
| C2 | high | M0 needs code M1 owns (`clampForEngine`, `InstPatch`, `jsToFixed`, `jsToPrecision`, clip presets) | confirmed | those move into the scaffold, fully implemented; M0 round trip uses opaque CLIP bytes, the preset round trip moves to M2; `JsCompat.h`, `FixtureJson` append-only (7.1, 7.2, 3) |
| C3 | high | M3 budget row vs exact fixture stats | confirmed (same as R4) | as R4 |
| C4 | medium | `origin/m0-skeleton` exists and contradicts the scaffold rules | confirmed (5 commits, explicit source list, inline tests) | the scaffold starts from it and lists the four changes; superseded after the merge (7.1) |
| C5 | medium | fingerprint, snapshot-writer and state-fixture outputs have nothing to produce | confirmed | `SL_SNAPSHOT`, `SL_FINGERPRINT`, `SL_STATE_FIXTURE` registrars; placeholder fingerprint; `--write-state-fixtures` and its artifact; `warn` until M9 (1.11, 3, 7.1, M9) |
| C6 | medium | renders have no engine tag, M2 cannot run the transport and live cases | confirmed | `needs` field in every case; transport, chain and live cases on Tape plus 8 Tape twins (36 tape, 37 grains); fixtures regenerated (5.3, M2, M3) |
| C7 | medium | M0 defaults test exact through floats | confirmed (same as P11) | `(double)(float)` (M0) |
| C8 | medium | text round trip impossible for many-to-one readouts | confirmed (ampOffset saturates at "+3.9 dB") | idempotent text for all; resolution check only for one-to-one readouts (M0) |
| C9 | medium | PROMPT 14.10 snapshots missing in M5 and M6 | confirmed | snapshot rows added (M5, M6, PROMPT 14 table) |
| C10 | medium | no full save/load with a real clip | confirmed | M2 state test through the processor (3, M2, PROMPT 14 table) |
| C11 | medium | absolute CPU and ms gates flaky on shared runners | confirmed (GrainLock gates > 1.0x only) | timing rule: print targets, gate > 1x real time or 5x the ms target; counts always gated (4, M0-M7) |
| C12 | medium | AllocCounter counts calls only; memory criteria need bytes | confirmed (GrainLock `alloccount`) | size tracking (largest request, live bytes) in the scaffold's AllocCounter; M7 limit as int16 bytes + 16 MB (7.1, 3, M7) |
| C13 | low | audio `hash` undefined | confirmed (the builder used FNV-1a 32) | defined in 5.1 |
| C14 | low | Node 22 on CI vs Node 24 locally | confirmed | `setup-node` 24; `make-fixtures.js` prints a note on another major (1.11, 5) |
| C15 | low | Cloud tuning silently dropped | confirmed (test-engine.js reports, does not gate) | retired explicitly, Cloud f0 printed (M3, 8.1 #28, PROMPT 14 table) |
| C16 | low | `check-params.js` has no CI step | confirmed | `build.yml` added to M9's files (1.11, M9) |
| C17 | low | 2 MB WAV fixture vs the size budget | confirmed | made at test time by `TestSignals`; binaries outside the budget (3, 5.4) |
| C18 | low | M8's import results in M1's file | confirmed | `state/import.json` from M8's `tools/fixtures/state.js`; `model/normalize.json` no longer holds them (5.3, M8) |
| C19 | low | signal generators cannot be loaded "as the tests do"; copies may drift | applied in part: the generator already extracts each function by name from the test files at run time and hashes those files, so there is no copy to drift | the plan now names the exact source and the mechanism (5.3) |
| C20 | low | "rendered without assertion" proves nothing in Release | confirmed | `--snapshot` also runs on the Debug runner (1.11, M0) |
| C21 | low | offline export never tested | confirmed | `setNonRealtime(true)` in M2's determinism test; checklist step 13 (1.10, 6, M2) |

**Fixture builder's notes**

| Note | Done |
|---|---|
| M3 budget expects cuts = 0 where the prototype cuts tails | ruled with R4: the fixtures are the expected result |
| `detectPitch` on DC returns MIDI 90 | recorded in M7's fixture line; `docs/NOTES-M7.md` decides whether the plugin keeps it |
| fixtures made on Node 24, CI planned on 22 | CI pinned to 24 (C14) |
| choices where the plan was open (saves written by hand, shared `insts`, `"preset:<id>"`, NaN/Infinity strings, case cuts) | accepted; the encoding notes in 5.1 point to `tools/README-fixtures.md` |
