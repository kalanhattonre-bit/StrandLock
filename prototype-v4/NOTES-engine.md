# NOTES-engine

These notes come from the engine builder (`src/sl-engine.js`, `tests/test-engine.js`). They list what I decided where
BLUEPRINT sections 1 and 3 were open or silent, what I added, and what other builders need to know. Run
`node tests/test-engine.js` (or `node tests/test-engine.js lock seam` for some sections only).

## Shared definitions (must stay identical to SLModel)
- `hash32`, `rand01` and `idHash` are copied word for word from `src/sl-model.js` (MurmurHash3 x86_32 over ints,
  seed 0x9747b28c; FNV-1a for ids). The test cross-checks 400 cases, plus `rateBeats` for every rate and `travelSpeed`.
- The engine's random streams use fixed salts: jump 0x5ca7, skip 0x51c1, spray 0x5b7a, drift 0x0d71, tiny head
  offsets 0x7177, random start 0x57a7. Every value is a function of (note idHash, head, step/jump/grain index,
  `inst.seed`). The loop pass is never an input, and a test proves passes 2 and 3 are bit-identical.
- Random start uses `start.seed` from the snapshot (SLModel already emits it) plus `inst.seed`, so both Re-roll paths move it.

## Readings of the blueprint (simplest consistent choice)
- **Loop crossfade**: the first pass plays the Region plainly from its true start, so attacks stay intact. After
  that the loop is `[X, L)`, and its last X frames are an equal-power fade from the Region's end into its first X
  frames, landing exactly on frame X. Nothing outside the Region is read. Cost: a head that moves backwards across
  the Region start enters the loop at its end with one discontinuity, the first time only. A fade at the start
  would have smeared every note's attack with the Region's tail.
- **Grains at the Region start**: in the first pass a grain never reads before frame 0 (that would be the Region's
  end). Voice shifts by whole root cycles so it stays in phase.
- **Grain latency**: a causal grain's middle plays half a grain after it starts, so each grain is aimed where the
  head will be at that moment. Without this, Cloud (140 ms) sounded 70 ms of material behind the head shown on screen.
- **Mirror**: the 2nd, 4th, 6th and 8th heads (index 1, 3, 5, 7) move backwards; the 1st head moves forwards.
- **Backward from pos 0** in loop mode wraps straight into the loop's end; with hold or stop it stays at the start.
- **Spread offsets** follow Heads/Spread changes with a 20 ms approach, capped at 0.2 frames per frame (a Tape head
  bends at most 20 %, no chirp). Head gains (level, pan, thinning) are slew-limited, full scale in 20 ms.
  Measured: Heads 2 -> 2.5 -> 3 gives step ratios 0.86 (Tape), 1.00 (Voice), 0.81 (Cloud) against the limit 1.5.
- **Head tune** (semitones per head 1..8): detune `0 +.07 -.07 +.14 -.14 +.21 -.21 +.28`; octaves `0 12 -12 24 0 12 -12 24`;
  fifths `0 7 12 19 -5 7 12 19`.
- **Tiny head offsets**: heads 2..8 sit a seeded 0..8 ms further into the source.
- **Tape Refresh (stutter)**: the grab starts at the note start. At each refresh the head jumps back to the grab
  start (5 ms crossfade). A skipped refresh starts a new grab where the head is now. With Skip 0 it is a pure beat
  repeat, and the measured spread of replayed positions is 8e-5.
- **Refresh period**: free `ms`, or synced `rateBeats(rate)` (beat-locked when `lockToBeat`), divided by the lane
  and macro factors, with a minimum of 5 ms. A Refresh lane below 0.06 forces Smooth. An inst at Smooth stays
  Smooth whatever the lane says.
- **Scatter** jumps happen at the first 32-sample control tick at or after the grid line. Measured error: 0..32
  samples (0.67 ms) at 87 bpm. Notes start sample-accurately; only lane, jump and refresh changes run at control rate.
- **Chain gap**: when a chained note ends before its target starts (glide mode Always, up to 4 beats), the voice
  holds its last values, glide fully applied, until the target starts. Chains do not cross the loop end.
- **Start 'carry'** on a voice's first note (no previous note) falls back to `pos`. Inside a chain the playhead always carries.
- **Engine change inside a chain** (per-note engines): the grain schedulers restart at the hand-over; the playhead carries.
- **Velocity gain** = `1 - velSens * (1 - vel)`. `noteOn` accepts velocity 0..1, or 0..127 (anything above 1 is divided by 127).
- **Limiter**: instant attack, 80 ms release, ceiling 0.9, then a hard clamp at 0.98. Never NaN: every voice
  filter and the output check for non-finite values.
- **Transport**: a `setTransport` beat more than 0.1 beat away from the engine's own clock counts as a jump
  (clip voices release, then chase). A smaller difference re-anchors the tempo and keeps the song position continuous.
  While stopped, a free beat clock keeps running for live Scatter and synced Refresh.
- **Chase** fast-forwards control ticks from the chain's first note (capped at 2^17 ticks, about 87 s at 48 kHz).

## Additions beyond the blueprint (tell me if you want them gone)
- **Refresh also works in Voice**: the read position holds between refreshes, as in Cloud. The blueprint only
  names Tape, Cloud and Lock. UI: Refresh is live in every engine.
- **Formant in Cloud (and Lock) uses the Tape formant EQ**, so the Formant lane and macro do something in every
  engine. Voice keeps its grain formant, with the EQ at 0 dB. Removing it is one line (`fdb` in `tick`).
- **Spray also scatters Lock grabs**.
- **Lock level and crossfade**: taps are Hann-weighted across the N-period capture (no seam), then scaled by sqrt(2/N),
  which gives +3 dB on pure tones at N=4 and -1.2 dB on noise. Re-grab crossfades use GrainLock's correlation
  correction `1/sqrt(1 + 2 rho sin cos)`; without it tonal material bumped +3 dB at every re-grab.
  N is capped at 12, so 8 heads x 2 captures x 12 = 192, which is the 48 kHz budget.
- **Wheels and pitch bend act on live (keyboard) voices only**, as in v3, where the wheels are recorded into lanes.
  Clip voices follow their lanes (tested: 0.00 dB change). `setController('mod1', null)` lets go, so the lane rules again.
- **liveLegato** is mono legato: a new key takes over the newest held live voice (no attack, same playhead, instant
  pitch change). Releasing it returns to the last key still held.
- **Sustain**: `setController('sustain', 1|0)` holds released live keys.
- `noteOn(p, vel, { startPos })`: `startPos` (0..1 of the Region) is for "hold mouse = audition that spot".

## Budget (D3), exactly as implemented
- Load units: Voice and Cloud = active grains; Lock = taps (N, or N + N' during a re-grab crossfade);
  Tape = 1 per head (2 during a jump or stutter crossfade). Budget B = floor(192 x 48000 / sr).
- The planner runs every 32 samples and on every note start. Steady heads are planned to 75 % of B, so that 20 ms
  fade tails fit under B. Thinning removes the top (fractional) heads of the oldest voices first, released voices
  first and quietest as tie-break, down to one head each, then their last heads. The newest voice is touched only
  if it alone exceeds B (for example Lock N=12 at 96 kHz). If fade tails plus steady load would still exceed B,
  the oldest fade tails are cut at once (`stats().cuts`).
- A head sounds only with a grant from the planner. A grant caps its grains (when full, the oldest grain gives way)
  and its Lock taps. A new voice grants its own heads at its first tick, and the planner runs before any sample.
- Measured load = the sum of each head's peak over each stretch between planner actions. That is an upper bound
  of the true per-sample sum. Worst measured: 192/192 at 48 kHz (Lock, 16 x 8 heads), 197/208 at 44.1 kHz, 96/96 at 96 kHz.

## Display and stats (extra fields, all optional to use)
- `getDisplay().voices[]` = `{ noteId, p, kind:'clip'|'live', engine:'tape'|'voice'|'cloud'|'lock', motion, env, released, heads }`;
  `heads[]` = `{ i, pos, dir, level, thinned }`. A Lock head shows its capture as one entry in `grains`.
- `stats()` = `{ voices, grains, thinnedHeads, maxGrains, budget, cuts, steals }`; `resetStats()` clears the maxima.
- The module also returns `hash32, rand01, idHash, travelSpeed, gShape, defaultInst, normInst, LANE_IDS, LANE_DEF, HEAD_TUNE, CTRL, MAX_HEADS, BUDGET_48K, LOCK_MAX_N`.

## For the UI builder
- Worklet messages (any of these key spellings work): `{type:'sample', channels, sampleRate, name}`,
  `{type:'inst', inst}`, `{type:'snapshot', snapshot}` (or `snap`), `{type:'transport', playing, bpm, beat, loop}`,
  `{type:'noteOn', p, vel, opts}`, `{type:'noteOff', p}`, `{type:'ctrl', name, value}`, `{type:'allOff'}`.
  The processor posts `{type:'display', beat, playing, voices, grains, stats}` about 30 times a second, and
  `{type:'error', message}` if a message throws. A mono output bus works.
- `setSample` fades out every voice (a new file means new positions). Stereo is detected by content: identical
  channels count as mono, so L == R exactly at spread 0.
- `setInst` is cheap; call it on every knob move.

## Not done / known limits
- Plain Cloud (no Lock) far below the root (C1 against a C3 root) has no stable pitch (-10 cents, 79 cents spread
  across harmonics). That is expected for unaligned grains and is reported, not gated.
- Voice grain positions snap to the root grid (BLUEPRINT): samples whose pitch moves will sound rough in Voice.
  Pitch marks (CycleLock's PitchMarks) would fix that later.
- No anti-alias pre-filtering for Tape transposed far up, or for Cloud read rates.
