# NOTES-sounds (sl-sounds.js, sl-analysis.js)

These are the choices I made where BLUEPRINT section 4 left room. Each one is the simplest reading that keeps model and engine consistent.

## SLSounds

- **Pitched lengths are 5.997 s, not exactly 6 s.** saw, sawopen, square, pad and vowel are 264473 samples long, which is
  1569 whole C3 periods. Every partial is exactly periodic over the file, so the loop has no seam and needs no crossfade. The root stays
  exact: 261.62559 Hz, 0.00005 cents from 261.6256. Making them exactly 264600 samples would have meant either a crossfade (the 2nd harmonic
  nearly cancels halfway through it) or a 0.28-cent detune. pluck and noise are exactly 6 s. beat is exactly 4 s. Read the length
  from `channels[0].length`; never assume 6*44100.
- **The sweeps return at the end so the loop is seamless.** sawopen's low-pass opens from 300 Hz to 14 kHz over 92% of the length,
  then closes over the last 0.48 s. vowel glides from aah to ooh over 88% of the length, then back over the last 0.72 s. square's pulse width
  goes 50% -> 10% -> 50%. pad's swell rates are rounded to whole cycles per loop: 1-5 cycles, where v3 had 0.13-0.80 Hz.
- **Band-limited by construction.** The pitched sounds are additive (partials below 19 kHz only). pluck is additive with decays (< 18 kHz).
  Energy off the harmonic lines measures about -101 to -107 dB. A naive saw measures -21 dB on the same metric.
- **Stereo.** Each partial gets a small seeded L/R phase difference: up to +-0.45 rad, scaled down for partials 1-3. That leaves the pitch
  untouched and keeps the sum mono-safe (loss <= 0.03 dB). The pad's chorus copy is wider. pluck pans per note, hats pan slightly,
  noise mixes mid/side.
- `make(id, { seed })` takes an optional seed (default 1). A different seed gives a different but equally valid sound.
- Extra exports: `defaultId` ('sawopen'), `ROOT_HZ` (261.6256), `PITCHED` (ids that are exactly C3).
- saw-type sounds start half a period into the wave, so sample 0 is mid-ramp and not on the saw's edge.
- pluck: C - Am - F G eighths at 120 bpm around C3. detectPitch on it returns G1 (nominal root 60 is what make() reports), so don't
  run detect on the built-ins. beat: detectPitch finds the kick's ~49 Hz. noise: null.

## SLAnalysis

- **detectPitch** is v3's YIN unchanged (5 loudest 2048 frames, threshold 0.15, fallback minimum <= 0.45, parabolic step, median).
  There are two additions:
  1. At least half of the examined frames must be periodic (v3 needed only one). With fewer, the result is `null`.
  2. Digital silence returns `null`. v3 reported sr/minT for it.

  It returns `{ midi, cents, hz }`. cents is rounded to 0.1, not to whole cents. Range: 45-1500 Hz, as in v3.
- **detectKey** is v3's code. It returns `{ key: 0..11 (C = 0), scale: 'Major'|'Minor', r }`, or `null` for silence. The scale names
  match v3's SCALES keys.
- **suggestParts(channels, sr, opts?)** works on a ~11 kHz mono copy:
  - 11.6 ms frames give level, high-band level and onsets (dB rise over the quietest of the 3 previous frames).
  - 46 ms frames add one FFT each, for spectral flatness (floored 50 dB under the frame peak) and energy-weighted spectral flux.

  How each part is chosen:
  - **hit** = the highest score of `4*(peak dB over the file's 98th-percentile level) + min(rise, 18)`. The rise must be at least 9 dB.
    The part starts 40 ms before the onset (refined to about 1.5 ms) and lasts until the level drops 30 dB or the next strong onset,
    clamped to 1-3 s.
  - **steady** = a tonal, level, slowly changing window of `clamp(duration/4, 1, 3)` s. It may contain no strong onset (>= 15 dB) and
    no level spread above 4 dB.
  - **quiet** = the quietest textured window at least 9 dB under the loud parts, not silence, with no strong onset.
  - **loop** = one bar at the tempo from the onset autocorrelation (60-200 bpm, weighted towards 120, refined on the bar lag, snapped to a
    whole bpm within 0.25). It is 120 bpm with `tempoFound:false` when there is no clear beat. The bar starts on a strong onset, scored on
    the 8th-note grid and on how much the next bar repeats it.

  Parts are picked in the order hit, steady, loop, quiet. Each skips candidates that overlap an earlier pick by more than 50% of the
  shorter one. They are returned in the order steady, hit, quiet, loop. The loop part also carries `bpm` and `tempoFound`. Labels include
  the time, e.g. 'Steady note 1:32' and 'One-bar loop 120 bpm 0:29'.

  `opts.debug` returns `{ parts, debug }`, with every candidate's score; the tests use it to print margins. Files under 1.2 s, or silent
  files, return [].
- Known limit: if a "big hit" is no louder than the drums around it (+1 dB or less) and sits on a loud held sound, a drum hit can win.
  The test sweep shows this: 5/10 found at that level, 10/10 at +2.3 dB, 10/10 at +4 dB.
