# NOTES-model: sl-model.js decisions

This is the model builder's file (BLUEPRINT section 0). It records where the blueprint left room and
which reading I chose, what the engine and UI builders need to know, and the limits I measured.
Tests: `node tests/test-model.js` (146 checks), or `node tests/run-all.js` for everything.

## A. For the engine builder (things that must agree)

**1. Seeds (1.6).** These are the exact definitions, so copy them as written:
```js
function fmix32(h){ h^=h>>>16; h=Math.imul(h,0x85ebca6b); h^=h>>>13; h=Math.imul(h,0xc2b2ae35); h^=h>>>16; return h>>>0; }
function hash32(){ let h=0x9747b28c|0;                       // MurmurHash3 x86_32 over 32-bit ints
  for(let i=0;i<arguments.length;i++){ let k=Math.imul(arguments[i]|0,0xcc9e2d51); k=(k<<15)|(k>>>17); k=Math.imul(k,0x1b873593);
    h^=k; h=(h<<13)|(h>>>19); h=(Math.imul(h,5)+0xe6546b64)|0; }
  h^=arguments.length*4; return fmix32(h); }
function rand01(h){ return (h>>>0)/4294967296; }
function idHash(id){ const s=String(id); let h=0x811c9dc5; for(let i=0;i<s.length;i++){ h^=s.charCodeAt(i); h=Math.imul(h,0x01000193); } return h>>>0; }
```
The test checks `hash32` byte for byte against a reference MurmurHash3 (seed 0x9747b28c, 4 little-endian bytes per int).
It also checks `idHash` against the published FNV-1a vectors.

**2. `start.seed` (an extension).** `note.sample.start` and the snapshot's `start` carry a fourth field, `seed` (an
integer, default 1). Re-roll with `laneId === null` adds 1 to it on notes whose Start mode is 'random'. The
blueprint lists Start as `{mode,pos,range}` but also says Re-roll changes "note random starts", and those starts
had no seed to change. Suggestion: mix it into the engine's random-start hash, e.g.
`rand01(hash32(note.idHash, headIndex, start.seed, inst.seed))`.

**3. Float32 tables and the travel dead band.** `Math.fround(0.52)` = 0.51999998 falls inside the band (stopped),
but `Math.fround(0.48)` = 0.47999999 falls outside it and plays -1/8x. The engine reads Float32 tables, so a lane
value of exactly 0.48 plays -1/8x. Two things follow:
- `mapLane('travel', v)` applies `Math.fround(v)` before the mapping, so the readout matches what plays.
- No built-in Speed shape puts a node on 0.48 or 0.52. A test checks this.

`travelSpeed(v)` itself is exactly the blueprint's formula (inclusive band). If you would rather widen the band by
1e-6 on both sides, tell me, and I will make `travelSpeed` match you.

**4. `rateBeats`.** It takes the 21 ids, and a positive number passes through as beats. An unknown id reads as
0.25 (1/16). Triplet is x 2/3 and dotted is x 1.5, computed as `base * (2/3)` and `base * 1.5`.

**5. Snapshot details (3.2).**
- `version` = 1 (`SLModel.SNAPSHOT_VERSION`).
- Notes are sorted by (s, p, id).
- `lock` is false unless the resolved engine is 'cloud'.
- `travel` is left as the note's kind, even on a Tape note. Your 1.3 rule handles it ("Tape treats Path as speed
  kind at 1x"); if I rewrote it as 'speed', the Path values would be read as speeds.
- Motion 'freeze' on Tape is likewise left for you to treat as forward.
- `chain.id` = the id of the chain's first note.
- `glide` is present even when the target restarts (`chain.nextId` is then null): the source bends, then releases.
- Notes past the clip end are still included.

**6. Glide size.** `d` runs from the source's real END (its p plus its last pitch node) to the target's real START
(its p plus its first pitch node), clamped to +-48 st. Adding `d` to the source's own pitch lane therefore lands
exactly on the target's drawn start; through the tables the measured gap was 4.8e-7 st. Generators on the pitch
lane are not part of `d`. The blueprint names only the target's first node; I added the source's last node so the
slide lands where the line is drawn (v3 research, code.md 1.3).

## B. For the UI builder (how to use the module beyond the blueprint)

- **Clips are immutable.** Every function returns a new clip and shares the notes it did not touch, so never mutate
  a clip or a note you got back. (v3 mutated `S.notes` in place; do that only on a `cloneClip` copy.)
- **`ids`** can be an array, a Set or one id string. `null`/`undefined` means every note in the clip.
- **Build-mode helpers** (extras, not in the blueprint):
  - `addNote(clip,p,s,l,patch)` returns `{clip,id}` with a hashed id that is unique in the clip.
  - `removeNotes` also clears links that pointed at the removed notes.
  - `updateNotes(clip, ids, {p,s,l,vel,glide,gc,glideTo} | fn)`.
  - `duplicateNotes(clip, ids, offset?)` returns `{clip, ids}` with links remapped. The default offset is the
    selection's own length, as in v3.
  - `setLaneNodesMany(clip, laneId, {id: nodes})` is for the Nodes-box drag.
  - `mkNote(p,s,l,id?)` makes ids from a counter plus a load-time salt (identity only, not randomness).
- **Drawing.**
  - `sampleNodes(nodes, M)` (alias `sampleLane`) gives the drawn line.
  - `laneTable(note, laneId, res)` gives the drawing plus its generator, for ribbons and ghosts.
  - `evalNodes(nodes, t, fromLeft?)` evaluates one point.
  - `bendShape(u, bend)` is for the diamonds.
  - `computeGlides(clip, inst)` returns `{d,len,curve,toId,linked}`, and `gShape(u, curve)` is v3's glide curve.
- **Ghost preview (U6).** Call the tool on the clip, draw the result faintly, and throw it away. That is cheap:
  the Strand Demo compiles in about 3 ms.
- **Shapes.**
  - `shapeKeysFor(laneId, travelKind)`, `SHAPE_NAMES`, `SHAPE_KEYS`.
  - 'fit' needs `opts = { bpm, sampleSeconds, inst }` (or `region`); it defaults to 120 bpm, 6 s and the full Region.
  - 'random' takes `opts.seed`.
- **Readouts.** `formatLane(laneId, v, { inst, bpm, travelKind })`. Heads, Grain and Refresh resolve against the
  instrument's own knobs. Macros, offsets and wheels are not included (they are live).
- **Generators.** `makeGen(kind, patch)` fills the defaults:
  `{shape:'squiggle', rate:'1/16', d0:0.3, d1:0.3, warp:0, lockToBeat:true, amount:0.5, smooth:0, perNote:'same', seed:1}`.
  Then call `setGen`, `clearGen(clip, ids, laneId|null)`, `bakeGen(clip, ids, laneId, {res})` or
  `reroll(clip, ids, laneId|null)`.
- **Note settings.**
  - `setNoteSample` also accepts `vel`, `glide` and `gc` (the S7 panel has them). It ignores 'mixed' and invalid
    values, and `null` means "follow the instrument".
  - `getMixed` returns `{count, motion, travel, start:{mode,pos,range}, tape, rev, jump, engine, lock, restart, vel, glide, gc}`,
    or null for an empty selection.
- **Undo.** `new History(initialState)`, then `push(state, label)` after every edit (one click = one push).
  - `undo()` and `redo()` return a fresh copy, or null.
  - It also has `canUndo()`, `canRedo()`, `undoLabel()`, `redoLabel()` and `current()`.
  - The state can be a clip or `{clip, inst, ...}`, so Re-roll, Dice and recipes can be one step.
  - Pushing the same state twice is ignored.
  - Unchanged notes share their stored text: 200 one-note edits on a 1024-note clip hold 1.64 MB instead of 275 MB.
  - Each push costs about 8.5 ms at 1024 notes and about 2 ms at 256 notes, so push on mouse-up, not on every move.
- **Presets.**
  - `loadClipPreset(id|name)` returns a fresh `{bars, notes}`.
  - `applySoundPreset(inst, id|name)` returns `{sound, inst}`.
  - `CLIP_PRESETS` and `SOUND_PRESETS` are deep-frozen; read them, do not edit them.
  - Strand Demo note ids are 'sd-1' to 'sd-26' in v3's order.
- **Loading v3 saves.** `normalizeClip(v3notesArray or {bars, notes})` turns `lin:true` into 'linear', fills
  missing lanes, gens and sample settings, drops dangling links and makes missing ids deterministically.
- Also exported: `LANE`, `MODES` (1 Build to 8 Sample, with the sub-lanes), `SCALES`, `inScale`, `snapScale`,
  `noteName` (60 = C3), `rateLabel`, `travelSpeedToV`, `mergeInst` and `normalizeInst`.

## C. Ambiguities and my reading

1. **RATES** is an array of 21 id strings, in base order with T and D after each: '1bar','1barT','1barD','1/2',...
2. **Generator depth (pitch).** Depth is peak-to-peak in lane units, so depth 1 = +-0.5. On the pitch lane, both
   pattern depth and random Amount are scaled by 1/6, so full depth = +-2 st, as v3 did.
3. **Wave shapes** (phase in cycles, f = its fraction):
   - squiggle = sin(2*pi*f), starting at the centre and rising.
   - triangle starts at 0 and rises.
   - sawup = 2f-1, sawdown = 1-2f.
   - square is high in the first half.
   - randsteps and smoothrandom (a cosine from one value to the next) use the same seeded values as Random and
     honour `perNote`.
4. **Random grid.** With `lockToBeat` (the default), steps sit on the song grid; without it they count from the
   note start, warped like patterns. Smooth s: the first s of each step is a cosine glide from the previous value,
   then it holds. 0 = steps; 1 = a continuous glide across the whole step.
5. **Warp** clamps to -0.9..3. Unlocked phase = l * t^(1+warp) / rate, so the cycle count over the note does not
   change (v3's warp).
6. **Shapes never touch generators.** Reset resets the drawing only; use clearGen to remove a pattern. This follows
   Y9: a Ramp plus a Squiggle is one move.
7. **Travel shapes.**
   - Speed-kind generic shapes work around 1x (0.75): up = 1x to 8x, down = 1x to 0.13x.
   - Tape stop is a bent line (bend 0.24, fitted so speed falls about linearly), stopped from 7/8 of the note.
   - Tape start is its mirror. Reverse halfway = +1x then -1x. Rewind = 1x, then -4x slowing to a stop.
   - Path-kind generic shapes use 0..1. Path 'random' = 5 steps (jumps through the sound).
   - A key that belongs to the other kind leaves that note alone.
8. **Across** spans the selection's time range, and each note gets the exact slice of that one curve.
   - Linear, step and bend cuts are exact.
   - Smooth pieces are refitted (any segment more than 0.002 from the original is split) and stay within 0.002.
   - The same slicer serves Chop.
9. **Connect.**
   - Links among the selection are replaced. A link coming in from outside the selection still marks its target
     as taken.
   - Ties: Nearest and Furthest break ties by start time, then pitch, then id. Random orders the candidates by
     `rand01(hash32(idHash(n), idHash(m), seed))`.
   - Notes are processed in start order, so lower and earlier notes choose first.
10. **Walk.** A lane becomes flat at the walked value. 'start' sets mode 'region' plus pos. 'vel' is supported too.
11. **Chop.**
    - Pieces run from the note's start; the last piece may be short.
    - Piece 0 keeps the id, and the note's link moves to the last piece.
    - Pieces after the first get Restart sample here, so a chop retriggers. Without it, auto-legato would chain the
      pieces back into one voice.
    - `stepStart` is a number (the Region fraction each piece steps its Start), or `true` (spread the pieces evenly
      across the Region).
    - The return value is `{clip, refused, count}`. Piece ids come from hashes, so chop is deterministic.
12. **Reverse** mirrors in time; a bend becomes -bend (exact), and a step becomes a vertical edge then a hold.
    The generator stays as it is.
13. **Flip** is v -> 1-v (PulseLock's `inverted`). For Travel speed this exactly flips the direction.
    **More/Less** scales around the lane default, and scales generator depth and Amount by the same factor.
14. **selectSimilar(clip, refId, kind, ids).**
    - 'bar' = notes starting in the ref's bar.
    - 'everyOther' keeps every other note of `ids` in time order, with the ref's parity.
    - 'invert' = notes not in `ids`.
    - The result is in time order.
15. **Humanize.** `lanes` is `{laneId: amount}`; a bare number means Volume and Tone. Offsets are
    `(rand01(hash32(idHash, k, seed))*2-1) * amount`, start >= 0, and velocity is clamped to 0.05..1.
16. **Strum** groups notes that start within 0.05 beat of each other, spreads them by the rate (low first for 'up')
    and keeps every end. **Legato** makes each note reach the next later start in the selection, extending or
    shortening it.
17. **Glide 0 and auto glides.** Glide 0 joins the chain in the auto modes too (v3 excluded glide-0 notes from auto
    glides). Consequence in the Strand Demo with default Legato, measured:
    - The 8-note run (glide 0) now plays as two chains with instant pitch jumps, with no new attacks inside a chain:
      C4 > E4 > G4, then E4 > D4 > C4 > B3 > G3 (the last into the bar-4 chord). Each note still re-articulates
      through its own Volume decay.
    - The split happens because G4@9.5's nearest next note is the lead's G4@10, which already has an explicit
      incoming link, so G4@9.5 bends 0 st and releases.
    - The bass now chains C1 > A0 > G0 > G0, because b2's glide 0 no longer stops it.
    - The lead chains C5 > G4 > D4 (G4 auto-glides into the run's last note, as in v3).
    - If a pluck should restart instead, set Restart sample here on it.
    - **Inherited v3 quirk:** the voice-leading "peers" are every note ending at the same time, including notes whose
      candidates differ (the bass). In the demo's first chord change, C3 and E3 therefore both aim at E3; E3 wins
      the chain, C3 bends +4 st and releases, and A2 starts fresh. This is ported as v3 had it. A fix would rank
      only the peers whose candidate group is the same. Tell me if you want that.
    - `buildChains` only joins a target that starts after its source (this guards against cycles). A target with
      several sources takes the explicit link first, then the smallest |d|, then the earliest source.
18. **Recipes.** Each recipe is the full Sample settings of 1 Straight plus its own changes, so the result does not
    depend on the previous state. Region, macros and the sound knobs are untouched.
    - `notes` entries are a Gen, a shape key or null:
      - A Gen may carry `base`, which first sets the drawing flat.
      - A shape key applies the shape and clears that lane's generator.
      - null resets the lane.
    - `applyRecipe(..., ids)` with null ids applies to every note.
    - My choices:
      - 3 Frozen choir: Voice, Freeze, 4 heads, detune, drift 0.3, grain 3.
      - 4 Tape scratch: Tape; Travel drawn flat at 0.5 plus a Square 1/8 of depth 0.5. That is a forward and back
        cycle every 1/8 note, 1/16 in each direction.
      - 5 Stutter 1/16: Tape plus synced Refresh 1/16.
      - 6 Slow bloom: Cloud, 4 heads, Heads lane Ramp up (1 to 8 heads), speed 0.5.
      - 7 Scatter rain: Cloud, Scatter, jump 1/8T, 60 ms grains, 3 heads.
      - 8 Octave shimmer: Cloud + Lock, 3 heads, Octaves.
19. **Dice** rolls engine, lock (Cloud only), motion (no Freeze on Tape), speed, heads (1-6), spread (<= 0.6),
    the three grain sizes, refresh, spray (<= 0.5), tape (20%), rev (15%), drift (<= 0.4), head tune and jump.
    - Every field has its own seeded roll, so pinning one never changes another. The exceptions: lock and motion
      depend on the engine, and the 'grain' pin covers all three grain sizes.
    - Dice does not change `inst.seed`. Re-rolling Scatter and Spray is the UI's `inst.seed + 1`.
20. **`laneTable`'s `inst` argument** is accepted and not used, because macros, offsets and wheels are live.
21. **Table length** is the blueprint's plain formula, `max(2, ceil(l*res)+1)`, with no epsilon.
22. **Smooth = v3's sampleLane verbatim**, including its left-to-right Fritsch-Carlson limit. A test compares it
    with v3's code on 500 random sets: they match to Float32 rounding. The cost: Reverse of a smooth curve is exact
    in 198/200 random sets, and off by up to 0.0004 in the other 2. I tried an order-independent limit that makes
    Reverse exact, but it differed from v3 in 13/500 sets by up to 0.024, so v3 stays.
23. **The value at a vertical edge** is the value after it (PulseLock). Segment lookup moves to the next segment
    when t reaches the next node's t.

## D. Measured limits

- **bakeGen.** 89 of 90 test cases fit in 128 nodes, with error 0.0040 or less (median 0.0031).
  - The one exception is a 1/16 Squiggle over 4 beats (16 sine cycles). It needs 219 nodes at eps 0.004, so eps
    is raised until 128 fit, which leaves an error of 0.0097.
  - Longer or faster sine patterns will exceed 0.01. That is the 128-node cap, not a bug.
- **compileSnapshot** at res 256:
  - Strand Demo (26 notes): about 3 ms.
  - 256 chopped notes with a 1/32 pattern: about 11 ms.
  - 128 four-beat notes with a random generator: 24 to 41 ms (the spread is between runs).
- **History push:** about 2 ms at 256 notes and about 8.5 ms at 1024 notes.
