# NOTES-integrator (8 Nov 2026)

The integrator's file. It lists what changed after the three builders and their skeptics finished, why, and what the
UI builder must know. Where this file disagrees with NOTES-model.md, NOTES-engine.md or NOTES-sounds.md, this file
wins (those were written before these fixes). BLUEPRINT.md carries every contract change, marked "(integrator)".

Run everything with `node tests/run-all.js` (8 files). The new `tests/test-integration.js` drives the real path:
SLModel clip -> compileSnapshot -> Engine (loaded from `createStrandEngineModule.toString()`, as the worklet loads it)
-> output measured with SLAnalysis. `node tests/test-integration.js G` runs one section.

## Contract changes (BLUEPRINT.md)
1. **1.3 dead band at Float32 precision.** Stopped for 0.48 - 2e-8 <= v <= 0.52 + 2e-8 (`F32_EPS` in both modules).
   `Math.fround(0.48)` = 0.4799999893 used to play -1/8x. `travelSpeedToV(+-1/8)` now returns the first Float32 value
   outside the band, so 1/8x typed in the inspector plays 1/8x. The Refresh "< 0.06 = Smooth" test uses the same tolerance.
   `SLModel.mapLane` reads travel and refresh as `Math.fround(v)`, so the readout is what the table plays.
   The skeptic's "0.52 + 1e-12 -> 1/8x" limit check is now "0.52 + 1e-7" (it contradicted his own Float32 check).
2. **2.1 Gen fields** `invert` (Flip mirrors the generator too) and `key` (Chop keeps a 'Different' random line).
   The default rate is '1/16T', as 2.1 writes it.
3. **3.2 `start.seed`** is part of the snapshot (it already was; the blueprint listed only mode, pos, range).
4. **3.3 loop period = Region length.** See "Engine" below.
5. **3.3 budget**: thinning never cuts; heads that have not sounded yet wait (at most 20 ms) and fade in.
6. **2.2 glides**: voice-leading peers, explicit-link targets; Legato chord grouping; sound presets start clean;
   polyphony 1..32 in both modules.

## Engine (src/sl-engine.js)
- **Budget clicks (high).** A head that has not sounded yet (a new note's extra heads, a head the Heads lane adds, a
  thinned head coming back) gets its grant only when the fading tails leave room, newest voice first; it then fades in
  over 20 ms. A head thinned before it ever sounded (the other notes of a chord starting on the same sample) goes
  silent at once, since it has no tail. A new voice's first head never waits. Tail cuts remain only as a last resort
  and did not happen in any test: skeptic M (Lock arpeggio, Voice arpeggio) 0 cuts, Strand Demo with recipes 3/6/8 at
  8 heads 0 cuts.
- **Quietest first.** Thinning order is released, then silent (level x env < 1e-3), then oldest.
- **Chains re-seeded drift (medium).** Drift and the tiny head offsets are seeded once per voice from `chain.id`, so a
  chain plays exactly like one long note (0.000 ms difference on every head, Tape and Cloud, drift 0 and 0.6).
- **Loop period (medium).** The loop now repeats every L frames. The seam fade takes the file's material just outside
  the Region: after the Region end ("post", preferred), else before the Region start ("pre"). A whole-file Region
  wraps plainly (as v3's looped AudioBufferSource did); the starter sounds are seamless loops, so that is clean for
  them, but a user file with a whole-file Region and a hard edge will click at the wrap. The fade uses GrainLock's
  correlation correction, so a DC or steady source keeps its level (was +3 dB). `reg.Lp` is now equal to `reg.L`,
  `reg.post` says which side the fade uses.
- Grains and Lock captures in loop mode read at the head's unwrapped position (`grabPos`), so a grain that straddles
  a pass boundary goes through the seam fade instead of the first pass's hard start.
- **Phantom head (medium).** A head count within 1e-5 of a whole number is snapped, so "2 heads" drawn on the lane
  (2.000000358 from Float32) plays 2 heads, not 2 plus a silent third.
- **Determinism.** Note ends are compared in samples (`kAbs >= kOf(Uend)`), so pass 1 = pass 2 even when an end lands on
  a control tick. `setTransport` with unchanged bpm and loop (and no beat jump) changes nothing.
- **Speed with stereo sounds.** The inline Hermite fast path now covers stereo sources and every loop pass, not only
  mono in the first pass. Stereo pad, 8 voices x 2 heads at 48 kHz: Lock 3.8x -> 8.7x, Cloud 5.6x -> 9.1x real time.
- Not changed: noteOn velocity above 1 is read as MIDI 0..127, so MIDI velocity 1 reads as full. **The page must send
  vel/127.** Plain Cloud at 2x without the Tape switch reads +29 cents (unaligned grains; reported, not gated).

## Model (src/sl-model.js)
- **Voice leading (medium).** Peers are the auto notes that end together, lead into the same next chord (within 0.05
  beat) and share at least one candidate; the rank target comes from the union of the peers' candidates. A note an
  explicit link already leads into is not an auto target. Strand Demo first chord change now: G2->A2, C3->C3,
  E3->E3, G3->A3, and no fresh attack on A2. The run of eighths (glide 0) now plays as one chain, because its G4 no
  longer aims at the lead's linked G4.
- **Sound presets leak (medium).** `applySoundPreset` starts from Straight's Sample settings, the full Region and
  centred macros, then applies the patch: 0 leaks over 56 preset pairs (was 72).
- **Legato after Humanize (medium).** Starts within 0.05 beat of a chord's first start are one chord.
- Low items fixed: NaN guards in sampleNodes / evalNodes / laneTable / mapLane / formatLane; Flip mirrors the
  generator; More/Less on Travel (Speed kind) scale each direction separately and never cross the dead band; Chop
  keeps 'Different' random values; polyphony clamps at 32 (the engine's limit).
- (panels integration) `rateLabel('1barT')` read '1bar triplet'; it now reads '1 bar triplet' / '1 bar dotted'
  (`tests/test-ui-fixes.js`).
- Not changed: an explicit link still joins when its target starts well inside the source (154 of 874 joins in 400
  random clips overlap by more than 0.13 beat); the one voice then hands over at the target's start, cutting the
  source short. The UI could warn when a link is drawn backwards like that.

## Sounds and analysis (src/sl-sounds.js, src/sl-analysis.js)
- **Steady/quiet windows at 48 and 16 kHz (medium).** At least ceil(1 s / frame) frames: never under 1 s.
- **Hit at sample 0 (medium).** Onsets are measured from the first frame (before the file counts as silence). A
  rise in the first 3 frames is a hit only when the sound is 5 dB down 0.3-0.8 s later, so a file that simply starts
  loud (every held starter sound) gets no "Big hit 0:00".
- The Beat loop starter now offers its one-bar loop on the bar line (12/12 rotations in time, was 10/12). The hit
  choice leaves the best loop's bar free when a hit within 85 % of the top score allows it, and a 2-bar file compares
  its bars for repetition.
- NaN / inf samples read as 0 in every SLAnalysis entry point.
- detectPitch: when fewer than half of the 5 loudest frames are periodic, the next 10 loudest are examined and two
  periodic frames agreeing within 20 cents give the pitch. Held C3 under bursts: 47/54 found (v3 45/54, before 44/54);
  white noise still gives null.
- `suggestParts(..., {debug:true}).debug.loop` lists the top loop candidates with their score parts.
- Not changed: starter sounds are peak-normalised (RMS -8.7 to -18.1 dBFS), so switching sounds changes loudness by up
  to 9.4 dB. detectKey's C# with a C#1 bass reads G# (inherited from v3).

## Tests the integrator edited
- `tests/test-engine.js` "loop seam": the seam zones now sit at k*L (post fade after each pass boundary), and the check
  fails if the 10 ms case does not use a post fade, instead of locating zones at the old (L - X) period.
- `tests/test-model.js` "refresh lane": 62.5 ms within 1e-5 (the readout reads the Float32 value now).
- `tests/test-model-extra.js` "limits": band limits approached from 1e-7 (see contract change 1).
