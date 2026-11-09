# StrandLock prototype v4: blueprint

This is the contract every builder follows. It turns Kalan's picks (8 Nov 2026) into a browser prototype that
extends `../StrandLock-handoff/reference/prototype-v3-strandlock.html` (v3). v3 keeps working: everything it can do,
v4 can do. Where this file and v3 disagree, this file wins.

Background (read when you need detail, not up front):
- Kalan's picks page data: `C:\Users\ThinkPad\.claude\projects\C--Users-ThinkPad-Desktop-PDF-Bundler\strandlock-design\page\data.json`
  (every idea's wording; ids like S3, Y5 below refer to it).
- Research and reviewer verdicts: `...\strandlock-design\research\` (critique.json = merged verdicts, design-*.json = proposals,
  code.md = how v3's engine and GrainLock/PulseLock code work, risks.md = spec problems).
- The suite plugins (read-only, never modify): `C:\Users\ThinkPad\Desktop\GrainLock`, `...\PulseLock`, `...\CycleLock`.

## 0. Who owns which file

| File | Owner | What |
|---|---|---|
| `src/sl-model.js` | model builder | data model, lanes, generators, every selection tool, glides and chains, undo, recipes, presets, snapshot compiler. No DOM, no audio. |
| `src/sl-engine.js` | engine builder | the whole sound engine, self-contained. Runs in node (tests), in an AudioWorklet, and on the main thread as a fallback. |
| `src/sl-sounds.js` | sounds builder | the 8 built-in starter sounds, seeded. |
| `src/sl-analysis.js` | sounds builder | pitch and key detection (ported from v3) and part suggestions for long files. |
| `src/shell.html`, `src/sl-ui.js` | UI builder | the page: markup, CSS, roll editor, panels, waveform strip, audio hookup. |
| `tests/test-*.js` | each builder for their module | node tests; `node tests/run-all.js` runs all. |
| `build.js` | UI builder | concatenates shell.html + every src/*.js into `dist/prototype-v4-strandlock.html` (one file that opens by double-click). |

Never edit another owner's file. If you need a change there, write it in `NOTES-<yourname>.md` next to this file and work around it.

Every `src/*.js` is a plain script (no `import`/`export`). It defines exactly one global and ends with
`if (typeof module !== 'undefined') module.exports = <global>;` so node tests can `require` it.
Globals: `SLModel`, `createStrandEngineModule` (a function, see 3), `SLSounds`, `SLAnalysis`, and the UI's own.
Plain modern JS (ES2020), no libraries, no network, no `Math.random` anywhere except UI-only ids.
All randomness is seeded (section 1.6). Node 24 is installed; nothing else (no npm packages).

## 1. Shared definitions (model and engine must agree exactly)

### 1.1 Naming
- Cubase naming: MIDI 60 = **C3**. Use it in every label, test and comment. (v3 called it C4; that is a fix, C5 on the picks page.)
- Engines: `'tape'` (shown "Tape"), `'voice'` (shown "Voice"), `'cloud'` (shown "Cloud"), plus `lock: true|false` on Cloud only (shown as a Lock switch).
- Motions: `'forward' | 'backward' | 'bounce' | 'mirror' | 'freeze' | 'scatter'`.

### 1.2 Lanes (registry order = mode order; values are 0..1 in nodes and tables)

| id | mode key | name | default v | real value (v -> unit) | group |
|---|---|---|---|---|---|
| pitch | 2 | Pitch | 0.5 | (v-0.5)*24 semitones | pitch |
| amp | 3 | Volume | 0.8 | gain v*v*1.3 | level |
| vib | 4 | Vibrato | 0 | depth v*1 semitone at inst.vibRate | pitch |
| tone | 5 | Tone | **1.0** | low-pass 60*2^(v*8.3) Hz (1.0 ~ 18.9 kHz = open) | tone |
| formant | 6 | Formant | 0.5 | (v-0.5)*24 semitones formant shift | tone |
| hollow | 7 | Hollow | 0 | peaking cut 950 Hz, Q 0.8, gain -v*28 dB | tone |
| travel | 8 | Travel | 0.75 | see 1.3 | sample |
| heads | 8 | Heads | 0.5 | relative: +(v-0.5)*14 heads | sample |
| grain | 8 | Grain | 0.5 | relative: x 2^((v-0.5)*4) | sample |
| refresh | 8 | Refresh | 0.5 | relative: period / 2^((v-0.5)*6); v < 0.06 = Smooth | sample |

Mode 8 "Sample" has four sub-lanes chosen with chips: Travel, Heads, Grain, Refresh. Colours: pitch strand `#2ECF98`
(dark line `#3EE6A8`, light `#0B9E74`), amp `#4F8BFF`, vib **`#E05CFF`** (moved away from the orchid accent), tone `#FF9F1C`,
formant `#FF4D8D`, hollow `#22C3E6`, all four Sample sub-lanes **`#E2C275`** (sand). Accent orchid `#B58CFF` dark / `#7B4FE0` light.

### 1.3 Travel
Per note `travel: 'speed' | 'path'` (default `'speed'`, decision D4).
- **speed** kind, v -> signed speed multiplier s(v):
  - 0.48 <= v <= 0.52: 0 (stopped; the dead band)
  - v > 0.52: +2^(((v-0.75)/0.23)*3), so 0.52 -> 1/8x, 0.75 -> 1x, 0.98 -> 8x; clamp at 8x above 0.98
  - v < 0.48: -2^(((0.25-v)/0.23)*3), so 0.48 -> -1/8x, 0.25 -> -1x, 0.02 -> -8x
  - (integrator, 8 Nov) The band is inclusive **at Float32 precision**: stopped for 0.48 - 2e-8 <= v <= 0.52 + 2e-8
    (`F32_EPS` = 2e-8 in both modules). Snapshot tables are Float32 and Math.fround(0.48) = 0.4799999893, so without
    this a node drawn exactly on 0.48 played -1/8x. `travelSpeedToV(+-1/8)` returns the first Float32 value outside
    the band (0.5200000405 / 0.4799999595), so typing 1/8x plays 1/8x. The Refresh "v < 0.06 = Smooth" test uses the
    same tolerance (v < 0.06 - 2e-8). SLModel.mapLane reads lane values as Math.fround(v), exactly what the engine reads.
  - Effective head speed = s(v) * inst.speed * macroSpeed * motionDirection(head) * driftFactor(head), capped at 16x.
- **path** kind: v = head 1's position inside the Region (0 = Region start, 1 = Region end). Other heads keep their Spread offsets. Motion direction and Speed are ignored, except Scatter still jumps (it offsets the path).
- In the **Tape** engine, Path and Freeze are not available (UI fades them; engine treats Path as speed kind at 1x, Freeze as Forward).

### 1.4 Instrument settings (`inst`), with defaults
```
{ engine:'voice', lock:false, motion:'forward', speed:1, heads:1, spread:0.25,
  grainVoice:2, grainCloud:140, grainLock:4,            // cycles of the sample root | ms | wavelengths of the played note
  refresh:{ mode:'smooth', ms:120, rate:'1/16', lockToBeat:true, skip:0 },   // mode 'smooth'|'free'|'sync'
  spray:0, pitch:0, tape:false, rev:false, drift:0, headTune:'same',         // 'same'|'detune'|'octaves'|'fifths'
  region:{ start:0, end:1, atEnd:'loop', xfadeMs:10 },                       // atEnd 'loop'|'hold'|'stop'
  jump:'1/8',                                                                 // Scatter's jump rate
  macros:[0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5],   // Speed, Spread, Heads, Refresh, Grain, Spray, Tone, Formant (D8)
  offsets:{ amp:0, vib:0, tone:0, formant:0, hollow:0 },                      // -1..1, added to lane v then clamped
  glideMode:'legato', glideTime:0.5, vibRate:5.5, root:60, cents:0, key:0, scale:'Chromatic', scaleLock:false,
  velSens:0.7, polyphony:16, outGain:0, bendRange:2,
  mod1:{ target:'vib', cc:1 }, mod2:{ target:'tone', cc:2 },   // target any lane id incl. 'travel','heads','grain','refresh', or 'off'
  liveLegato:false, seed:1 }
```
Macros (0..1, 0.5 = no change): Speed x2^((m-0.5)*4); Spread +(m-0.5)*2; Heads +(m-0.5)*14; Refresh period / 2^((m-0.5)*6)
(only when refresh mode is not smooth); Grain x2^((m-0.5)*4); Spray +(m-0.5)*2; Tone lane v +(m-0.5); Formant lane v +(m-0.5).
Heads after lanes and macros clamp to 1..8 and are fractional: 2.5 = 2 full heads + a third at half level.

### 1.5 Rates (one picker everywhere, A1)
Ids `'1bar','1/2','1/4','1/8','1/16','1/32','1/64'` each with optional suffix `'T'` (x2/3) or `'D'` (x1.5): e.g. `'1/16T'`, `'1/8D'`.
Beats (4/4): 1bar=4, 1/2=2, 1/4=1, 1/8=0.5, 1/16=0.25, 1/32=0.125, 1/64=0.0625. `SLModel.rateBeats(id)` and the engine's own copy must agree.

### 1.6 Seeds (Y7: random stays the same until Re-roll)
`hash32(...ints)` = a fixed integer hash (e.g. murmur3 fmix of a combined value) and `rand01(h)` = h / 2^32.
Every random value is a pure function of (note id hash, lane or head index, step or jump index, a seed integer).
Re-roll = increment that seed (one undo step). Loop pass is NOT an input: every loop and every export plays the same.
`idHash(noteId)` = FNV-1a 32-bit of the id string. Model and engine both implement these identically (copy the code; the engine may not reference SLModel).

## 2. sl-model.js (`SLModel`)

### 2.1 Data
- Note (extends v3's): `{ id, p, s, l, vel:0.8, glide:null|0|beats, gc:0, glideTo:null|id, c:{ laneId: Node[] }, gen:{ laneId: Gen|null },
  sample:{ motion:null, travel:'speed', start:{ mode:'region', pos:0, range:0.25 }, tape:null, rev:null, jump:null, engine:null, lock:null, restart:false } }`
  `null` in `sample` = follow the instrument. `start.mode` is `'region'|'carry'|'song'|'random'`.
- Node: `{ t, v, shape:'smooth'|'linear'|'bend'|'step', bend:0 }`. `shape`/`bend` describe the segment AFTER this node.
  First node t=0, last t=1. 'smooth' = Fritsch-Carlson monotone cubic (v3's sampleLane); 'bend' = PulseLock's bend curve (port its
  math from `PulseLock/Source` pattern code; bend -1..1, 0 = straight); 'step' = hold until the next node. Max 128 nodes per lane.
  v3 nodes with `lin:true` load as `shape:'linear'`.
- Gen (rides on the drawing, Y9): `{ kind:'pattern'|'random', shape, rate:'1/16T', d0, d1, warp, lockToBeat, amount, smooth, perNote:'same'|'different', seed, invert:false, key:null }`.
  (integrator) `invert`: Flip mirrors the generator with the drawing (the table becomes exactly 1 - v). `key`: the note id
  whose hash 'different' uses (null = the note's own id); Chop sets it on the pieces so a 'different' random line keeps
  its values (Y7). Default rate is '1/16T'.
  Pattern shapes: `'squiggle','triangle','sawup','sawdown','square','randsteps','smoothrandom'`. Table value = clamp(drawn(t) + gen(t), 0, 1).
  Depth is in lane units; on the pitch lane full depth = +-2 semitones. lockToBeat: phase from song beat (n.s + t*n.l) so all notes
  line up; otherwise from note start, with warp (v3's speed-up/slow-down). Random: new value at each rate step, Amount 1 = +-0.5,
  Smooth 0 = steps, 1 = cosine glide across the step; 'same' = every note shares values at the same song step, 'different' = per note.
- Clip: `{ bars:4, notes:[], selection is NOT stored }`.

### 2.2 Functions (pure: take a clip, return a NEW clip; never mutate the input)
- `LANES`, `RATES`, `rateBeats(id)`, `defaultInst()`, `mkNote(p,s,l)`, `newClip()`, `idHash(id)`, `hash32(...)`, `rand01(h)`.
- `sampleNodes(nodes, M)` -> Float32Array M values (all segment shapes, never overshoots neighbours for 'smooth').
- `laneTable(note, laneId, res, inst)` -> Float32Array of max(2, ceil(note.l*res)+1) values: drawing + generator.
- `mapLane(laneId, v, ctx)` -> real value; `formatLane(laneId, v, ctx)` -> text like '+3 st', '0.5x backwards', '2.4 kHz', '-6.0 dB',
  'Refresh 1/16 (125 ms at 120 bpm)'. ctx = { inst, bpm, travelKind }.
- Shapes (Y2): `applyShape(clip, ids, laneId, key, scope, opts)`; scope `'each'|'across'`. Keys: up, down, swell, dip, squiggle, random,
  decay, reset; travel speed kind adds tapestop, tapestart, reversehalf, rewind; travel path kind adds playthrough, playthroughback, fit.
  'random' uses opts.seed. 'across' spans the selection's time range and gives each note its slice.
- Generators: `setGen(clip, ids, laneId, gen)`, `clearGen(...)`, `bakeGen(clip, ids, laneId)` (Turn into nodes: sample the table,
  simplify with RDP eps 0.004), `reroll(clip, ids, laneId|null)` (seed+1 on the gens, and on note random starts).
- Connect (Y5, decision D5 = 'next'): `connect(clip, ids, mode, seed)` with mode 'nearest'|'furthest'|'random'. For each selected note n
  (in start order): candidates = selected m with m.s > n.s, m.s >= n.s+n.l-0.13 and |m.p-n.p| <= 24; keep only those starting within
  0.05 beat of the earliest candidate start. Pick by smallest |dp| / largest |dp| / seeded random. A target takes at most one incoming
  link; if taken, use the next best; if none left, no link. Sets n.glideTo. `disconnect(clip, ids)` clears links among the selection.
- Tools: `walk(clip, ids, field, from, to)` (field = 'start' or a lane id: spreads a value first -> last note in time order),
  `chop(clip, ids, rate, stepStart, maxNotes=1024)` (returns {clip, refused:bool}), `reverseLane`, `flipLane`, `scaleLane(clip, ids, laneId, factor)`
  (More/Less around the lane default), `selectSimilar(clip, refId, kind)` -> ids (kind 'pitch'|'length'|'everyOther'|'bar'|'invert' with current ids),
  `humanize(clip, ids, {time, vel, lanes}, seed)`, `strum(clip, ids, rate, dir)`, `legato(clip, ids)`.
- Note settings (S7): `setNoteSample(clip, ids, patch)`, `getMixed(clip, ids)` -> per field the common value or 'mixed'.
- Lane-mode rule: `setLaneNodes(clip, id, laneId, nodes)` changes only that lane of that note (the mode-lock test uses it).
- Glides (E2): `computeGlides(clip, inst)` -> Map id -> { d, len, curve, toId, linked }. d aims at the target's REAL starting pitch
  (its p plus its first pitch node). glide 0 = instant jump (len 0) that still joins the chain; null = inst.glideTime.
  Auto glide Off/Legato(0.13 beat)/Always(4 beats), voice leading by rank, +-12 st for auto, explicit links override (port v3 computeGlides).
  `buildChains(clip, glides)` -> arrays of note ids played by one voice (a glide joins two notes unless the target has sample.restart).
  (integrator) Voice leading by rank counts as peers only the notes that end together, lead into the same next chord and
  share a candidate (a bass that can only reach the next bass note is not a peer of the chord above); the rank picks from
  the union of the peers' candidates. A note an explicit link already leads into is not an auto target.
  Legato groups starts within 0.05 beat as one chord (like Strum, Connect and the auto glides).
- Recipes (S8): `RECIPES` 1-8 `{ n, name, inst:{...patch}, notes:{ laneId: Gen | shapeKey } | null }`:
  1 Straight, 2 Backwards, 3 Frozen choir, 4 Tape scratch (Travel square 1/8 between +1x and -1x), 5 Stutter 1/16, 6 Slow bloom
  (Heads ramp up), 7 Scatter rain (Cloud, Scatter, jump 1/8T), 8 Octave shimmer. `applyRecipe(clip, inst, n, ids)` -> {clip, inst}.
  `dice(inst, seed, pins)` -> inst with Sample settings rolled within musical limits.
- Presets (U4, kept separate): `SOUND_PRESETS` (sound id + inst patch) and `CLIP_PRESETS` (bars + notes). Clip presets: 'Strand Demo'
  (v3 demo()), Pad Swell, Pluck Decay, Vowel Morph, Legato Lead, Hollow Drone, Saw Mirror Pad, Backward Vowel, Scatter 1/8T, Tape Stop Lead,
  Bloom, Walk Through. Loading one never touches the other.
  (integrator) A Sound preset starts from Straight's Sample settings, the full Region and centred macros, then applies its
  patch, so nothing leaks from the preset loaded before (U4). inst.polyphony is 1..32 in both modules.
- `compileSnapshot(clip, inst, { res = 256 })` -> the Snapshot in 3.2. Deterministic: same input, same output.
- `History` class: `push(clip, label)`, `undo()`, `redo()`, `canUndo/canRedo`; stores immutable clip versions, 200 steps.

## 3. sl-engine.js (`createStrandEngineModule`)

### 3.1 Shape
`function createStrandEngineModule() { ...; return { Engine, rateBeats, version }; }`. The function body must reference NO outside
names (no SLModel, no window, no require), because the page ships `createStrandEngineModule.toString()` into an AudioWorklet.
```
const SE = createStrandEngineModule();
const eng = new SE.Engine({ sampleRate });
eng.setSample({ channels:[Float32Array, Float32Array?], sampleRate, name })   // stereo kept
eng.setInst(inst)            // full inst (1.4); cheap, may be called often
eng.setSnapshot(snap)        // 3.2; voices of edited notes follow at once, deleted notes release
eng.setTransport({ playing, bpm, beat, loop })   // beat = song beat at the next sample; engine advances its own clock
eng.noteOn(p, vel) / eng.noteOff(p) / eng.allNotesOff()
eng.setController('mod1'|'mod2', 0..1) / ('bend', -1..1) / ('sustain', 0|1)
eng.process(outL, outR, n)   // any n >= 1
eng.getDisplay() -> { beat, playing, voices:[{ noteId, p, heads:[{ pos, dir, level }] }], grains: [{ pos, age }] (<= 128) }   // pos = 0..1 of the file
eng.stats() -> { voices, grains, thinnedHeads }
```
Also `src/sl-worklet-glue.js`-style code is NOT a separate file: export the glue as a second function inside the same file,
`function strandWorkletGlue(){ registerProcessor('strand-engine', class extends AudioWorkletProcessor { ... }) }`, defined after
`createStrandEngineModule`, also self-contained except for calling `createStrandEngineModule()`. Messages from the page:
`{type:'sample'|'inst'|'snapshot'|'transport'|'noteOn'|'noteOff'|'ctrl'|'allOff', ...}`; the processor posts `{type:'display', ...}` about 30 times a second.
module.exports for node = `{ createStrandEngineModule, strandWorkletGlue }`.

### 3.2 Snapshot (produced by SLModel.compileSnapshot, consumed by Engine)
```
{ version, bars, beatsPerBar:4, res,
  notes:[ { id, idHash, p, s, l, vel,
            engine, lock, motion, travel, start:{mode,pos,range,seed}, tape, rev, jump:beats, restart,   // all resolved (no nulls)
            glide: null | { d, len, curve, toId },
            chain: { id, index, prevId, nextId },
            lanes: { pitch, amp, vib, tone, formant, hollow, travel, heads, grain, refresh } } ] }   // Float32Array each, 0..1, length = max(2, ceil(l*res)+1)
```
Macros and inst knobs are NOT baked in (they come from setInst live). Generators ARE baked in.
(integrator) `start.seed` (integer, default 1) is part of the contract: Re-roll adds 1 to it on 'random' Starts, and the
engine hashes it into the random start. `tests/test-integration.js` checks every field name, type, range and table length
above on every clip preset and recipe.

### 3.3 Behaviour
- **Voices**: 16 (inst.polyphony), oldest stolen with a 5 ms fade. A chain plays on ONE voice: no new attack, the playhead, grain
  phase and vibrato phase carry on into the next note (D2). Envelope 8 ms attack, 60 ms release. Chase: starting the transport inside a
  note starts it part-way with a 10 ms fade-in at the right lane time and playhead position.
- **Control rate**: lanes, macros, heads and speeds update every 32 samples counted from voice start (block-size independent).
- **Pitch**: f = 440*2^((p - 69 + pitchLane + glide + vibrato + bend*bendRange + inst.pitch + headTune)/12). Root = inst.root + inst.cents/100.
- **Playhead per head**: start = Region start + note start (pos | carried from the chain's previous note | 'song': (song seconds at 1x) mod Region |
  seeded random within range) + Spread offset (head i of H: i/H*spread of the Region). Moves at the effective speed (1.3). Edges follow
  Region atEnd for every Motion: loop (wrap, crossfade inst.region.xfadeMs), hold (clamp), stop (20 ms fade, then silent); Bounce folds.
  (integrator) The loop period is exactly the Region length (a beat loop never drifts from the transport). The crossfade
  uses the file's material just outside the Region: after the Region end when there is some (each pass after the first
  starts by fading the Region start in while the continuation past the end fades out; the first pass keeps its attack),
  else before the Region start (each pass ends by fading into the lead-in). A Region that is the whole file wraps plainly,
  like v3's looped buffer (the starter sounds are seamless loops). Equal-power fades with GrainLock's correlation correction.
  Mirror: odd-numbered heads move backwards. Freeze: speed 0. Scatter: every jump-rate (beats, locked to song beat) each head jumps to a
  seeded spot in the Region, then moves at speed. Drift: head speed x (1 + drift*0.25*r), r seeded in [-1,1]. Spread also pans heads
  across +-(spread*0.75). Level per head 1/sqrt(H); fractional last head at its fraction. Tiny seeded offsets stop identical heads phasing.
- **Engines** (each note's own engine, D6):
  - Tape: Hermite read at rate = (f/rootHz)*(sample rate ratio)*speed (negative = reversed). Refresh (not smooth) = every refresh period jump
    back to where the last grab began (stutter / beat repeat), 5 ms crossfade. Scatter jumps crossfade 5 ms. Formant lane = moving peaking EQ
    250*2^(v*3.8) Hz, Q 4, gain 18*|v-0.5| dB (0 dB at centre; F2).
  - Voice: v3's renderGran made real-time per head: grains Hann, grainVoice*Ts long (Ts = sr/rootHz) divided by formant rate F=2^(formantSemis/12),
    placed every target period sr/f, read position = head position snapped to whole multiples of Ts from the Region start, normalised
    min(2, 2*Pout/grainLen). Tape switch: grain read rate x |speed|. Each grain backwards: read the grain reversed. Cap 8 grains per head; above
    that, shorten grains (never drop: spacing is the pitch).
  - Cloud: grains grainCloud ms (x grain lane x macro), Hann, a new grain every grain/3, read at 2^((pitch semis relative to root)/12) (x |speed| if
    Tape), from head position + seeded Spray (spray*min(1 s, Region/2)). Refresh (not smooth): the grab position holds between refreshes while the
    head moves on; Skip = seeded chance a refresh is skipped.
  - Cloud + Lock: GrainLock's trick. Each head captures N = grainLock wavelengths of the played note (period P = sr/f) from its position and plays
    y = (1/N) * sum over k of seg[(t + k*P) mod (N*P)], which repeats exactly every P, so any material plays exactly in tune. Re-grab at each
    Refresh (smooth = every N periods) with a one-period crossfade. Lock taps count against the budget.
- **Per voice, after heads are summed in stereo**: hollow peaking cut -> (Tape formant EQ) -> low-pass (tone) -> gain (amp, velocity with velSens,
  offsets) -> envelope. Vibrato depth from the vib lane at inst.vibRate. Mod wheels: when moved, they override their target lane on live voices
  (any lane, Sample sub-lanes included; on travel they set the lane value).
- **Budget (D3: up to 8 heads, thinned when busy)**: at most 192 active grains plus Lock taps per 48 kHz output sample, scaled by 48000/sr.
  Over budget: the fractional/top heads of the oldest and quietest voices fade out over 20 ms first; the newest note never thins.
  (integrator) Thinning never cuts: a head that has not sounded yet (a new note's extra heads, a head the Heads lane adds)
  waits until the fading tails leave room (at most 20 ms) and then fades in over 20 ms. It is not thinned. A new note's
  first head never waits. "Quietest": released voices first, then voices at -60 dB or less, then the oldest.
  Drift and the tiny head offsets are seeded from the chain's first note (chain.id), so a chain plays like one long note.
- **Output**: sum, outGain dB, soft limiter (peak <= 0.98, no NaN/inf ever; guard every division and every read index).
- **Live**: noteOn plays a voice with lanes at defaults (inst + macros + wheels). liveLegato: a new key while another is held carries its playhead.
- **Transport**: the clip loops over bars*4 beats when loop; notes start within 1 sample of their beat position at any bpm; stopping releases clip voices.

## 4. sl-sounds.js (`SLSounds`) and sl-analysis.js (`SLAnalysis`)
- `SLSounds.list` = `[{ id, name }]` in this order: `sawopen` "Saw that opens up" (DEFAULT, D9), `saw` "Saw", `square` "Square sweep",
  `pad` "Evolving pad" (v3's makeDefault, seeded), `vowel` "Vowel aah to ooh", `pluck` "Pluck arpeggio", `beat` "Beat loop" (120 bpm, 2 bars),
  `noise` "Noise". `SLSounds.make(id)` -> `{ id, name, sampleRate:44100, channels:[Float32Array, Float32Array], rootMidi:60 }`, 6 s (beat: 4 s),
  deterministic, peak 0.8, seamless loop, pitched ones exactly C3 (MIDI 60) except pluck/beat/noise (rootMidi 60 nominal).
  Every sound except `saw` changes across its length on purpose (so the Sample lane is audible).
- `SLAnalysis.detectPitch(channels, sr)` -> `{ midi, cents }` or `null` ("no clear pitch"); `detectKey(channels, sr)` -> `{ key, scale }`;
  `suggestParts(channels, sr)` -> up to 4 `{ kind:'steady'|'hit'|'quiet'|'loop', label, start, end }` (fractions of the file; a steady held
  tone, a big hit, a quiet texture, a one-bar loop at a detected or 120 bpm tempo). Region of 1-4 s each. Fast enough for a 5-minute file in < 1.5 s in node.

## 5. The page (shell.html + sl-ui.js)

Port v3's UI (layout, theme, roll drawing, gestures, menus, hint bar, MIDI, file load, detect) and add:
- Mode bar 1 Build ... 7 Hollow, **8 Sample** with sub-lane chips Travel / Heads / Grain / Refresh. Each lane button has a small menu arrow:
  with notes selected it opens Shape / Pattern / Random for that lane on all of them (Y2-Y4).
- **Box select (Y1)** in every mode: drag on empty space; Shift adds, Ctrl toggles. Double-click empty space still makes a note in Build.
  A **Nodes** switch in lane modes makes the box pick nodes of the selected notes, which then drag together.
- **Node editing (Y6)**: drag a node (first/last only vertically), double-click line = add node, double-click node = delete, a diamond in the
  middle of each segment: drag up/down = bend that segment, double-click = straighten. Live readout in real units while dragging
  (SLModel.formatLane). Pitch nodes snap to semitones unless Shift.
- **Selection bar** (always visible when notes are selected): Shapes with Each / Across; Pattern (rate picker 1.5, shape, depth start/end, warp,
  Lock to the beat); Random (rate, amount, smooth, same/different, Re-roll); Turn into nodes; Connect Nearest / Furthest / Random, Disconnect;
  Tools: Walk, Chop, Reverse, Flip, More, Less, Select similar, Humanize, Strum, Legato. Every click = one undo step. Hovering a shape/pattern/recipe
  shows a ghost of the result on the selected notes (U6).
- **Sample tab**: Engine Tape / Voice / Cloud + Lock switch; Motion buttons; Speed, Heads, Spread, Grain (unit follows engine), Refresh (Smooth /
  free ms / synced rate + Lock to the beat + Skip), Spray, Pitch, Tape, Each grain backwards, Drift, Head tune, At the end, Jump every;
  Recipes 1-8 and Dice; faded = does nothing with the current settings.
- **Note settings (S7)**: one panel for the selected notes (Motion, Travel kind, Start, Tape, Each grain backwards, Jump every, Engine, Lock,
  Restart sample here, velocity) showing 'mixed' where they differ; **inspector (T6)**: type start, length, pitch, velocity, and a selected
  node's time and real value.
- **Macros (A2)**: 8 knobs labelled Speed, Spread, Heads, Refresh, Grain, Spray, Tone, Formant; plus the 5 whole-clip offsets (A3).
- **Waveform strip = Sample editor**: whole file + zoom, Region handles and box (drag), At the end, part suggestions as chips (D10),
  live heads as numbered orchid lines with direction arrows and grains as dots (H4), click with notes selected = set their Start, hold mouse
  = audition that spot. Sound menu at its left (starter sounds, Browse, drop anywhere on the window, C3).
- **Roll**: each note shows chevrons for direction/speed and (U7) a faint thumbnail of the part it reads. Mode 8 shows the active Sample
  sub-lane as a ribbon like the other lanes.
- **Keys toggle (C2)**: shortcuts and the computer keyboard only work while it is on; every action also has a button. Never take Space.
- **Presets**: Sound menu and Clip menu, separate (U4). Strand Demo clip and the saw-that-opens-up sound load on first open.
- **Hint bar + Try this (U3)**: one line per mode, plus a suggestion per Motion.
- Audio: build a Blob whose text is `createStrandEngineModule.toString() + '\n' + strandWorkletGlue.toString() + '\nstrandWorkletGlue();'`,
  load it with `audioWorklet.addModule(URL.createObjectURL(blob))`; if that throws or is unavailable, use a ScriptProcessor on the main thread
  with the same Engine. Play/Stop + tempo box simulate Cubase's transport (as in v3).

## 6. Tests (node, numbers, no listening)
Each builder writes `tests/test-<module>.js` that prints one line per check (`PASS`/`FAIL name: measured value vs limit`) and exits non-zero on
any FAIL. `tests/run-all.js` runs every test file. Use measured numbers and print distributions, not single best cases.
Engine checks at least: tuning +-3 cents for Voice and Tape at C1, C3, C5 against a C3 root; Cloud+Lock +-3 cents from noise and from a chord;
Voice formant +6 st raises the spectral centroid > 25% with f0 within +-3 cents, -6 lowers it; legato glide C3->G3 lands within +-5 cents; a chain
does not restart its playhead; Backward output correlates with the reversed source; Freeze holds; Bounce folds; Mirror heads move opposite;
Scatter jumps land on the beat grid and two renders are identical; Re-roll (seed+1) changes them; random block sizes 1..700 give bit-identical
output with static settings; no NaN/inf and peak <= 0.98 on 16 voices x 8 heads; grain budget never exceeded; atEnd stop goes silent, hold holds;
Tone default is open (> 15 kHz); Tape formant EQ is 0 dB at centre; notes start within 1 sample at 120 and 87 bpm; loop seam has no click;
chase starts mid-note; speed: render 10 s of 8 voices x 2 heads in each engine and print x real time (target >= 4x in node).
