/* StrandLock prototype v4 - sl-sounds.js (SLSounds)
   The 8 built-in starter sounds, made in code so nothing goes missing on another computer.
   Plain script: defines one global, no imports. No Math.random: every random value is a seeded hash.
   Naming follows Cubase: MIDI 60 = C3.

   Every sound is stereo, 44.1 kHz, peak 0.8 and loops seamlessly (the last sample leads straight into the first).
   Pitched sounds (saw, sawopen, square, pad, vowel) are additive, so nothing aliases: only partials below 19 kHz exist.
   Their length is the whole number of C3 periods nearest 6 s (1569 periods = 264473 samples = 5.997 s), so every
   partial is exactly periodic over the file: the loop is seamless with no crossfade and the root stays exact
   (0.00005 cents from 261.6256 Hz). pluck and noise are exactly 6 s, beat exactly 4 s (2 bars at 120 bpm); those
   are rendered circularly (tails that run past the end wrap into the start), which makes their seams seamless too. */
var SLSounds = (function () {   // var: also reachable as window.SLSounds
  'use strict';

  const SR = 44100;
  const TWO_PI = Math.PI * 2;
  const C3_HZ = 440 * Math.pow(2, (60 - 69) / 12);   // 261.6256 Hz
  const PEAK = 0.8;
  const TOP_HZ = 19000;                               // highest partial any additive sound may contain
  const CTL = 64;                                     // amplitude control step (samples) for additive sounds

  const LIST = [
    { id: 'sawopen', name: 'Saw that opens up' },
    { id: 'saw', name: 'Saw' },
    { id: 'square', name: 'Square sweep' },
    { id: 'pad', name: 'Evolving pad' },
    { id: 'vowel', name: 'Vowel aah to ooh' },
    { id: 'pluck', name: 'Pluck arpeggio' },
    { id: 'beat', name: 'Beat loop' },
    { id: 'noise', name: 'Noise' },
  ];

  /* ---------------- seeded hashes ---------------- */
  function fmix32(h) {
    h ^= h >>> 16; h = Math.imul(h, 0x85ebca6b);
    h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35);
    h ^= h >>> 16;
    return h >>> 0;
  }
  function hash32() {
    let h = 0x9e3779b9;
    for (let i = 0; i < arguments.length; i++) h = fmix32(((h ^ (arguments[i] | 0)) + Math.imul(i + 1, 0x632be5ab)) | 0);
    return h >>> 0;
  }
  function rand01(h) { return h / 4294967296; }
  function strHash(s) {                       // FNV-1a 32-bit
    let h = 0x811c9dc5;
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193); }
    return h >>> 0;
  }
  // Seeded white noise in [-1, 1): one value per (stream, index).
  function noiseAt(base, i) { return rand01(fmix32((base + Math.imul(i, 0x9e3779b1)) | 0)) * 2 - 1; }

  /* ---------------- loop length for pitched sounds ---------------- */
  // The whole number of periods n of hz nearest `seconds` whose length L (samples) holds n periods almost exactly.
  function pitchedLoop(hz, seconds) {
    const target = seconds * SR, n0 = Math.round(hz * seconds);
    let best = null;
    for (let n = n0 - 8; n <= n0 + 8; n++) {
      const L = Math.round(n * SR / hz);
      if (Math.abs(L - target) > 0.02 * SR) continue;
      const err = Math.abs(L * hz / SR - n);
      const score = err + Math.abs(L - target) / SR * 0.01;
      if (!best || score < best.score) best = { n, L, score };
    }
    return { n: best.n, L: best.L, f0: best.n * SR / best.L };
  }

  /* ---------------- additive renderer ---------------- */
  // part = { cyc: whole cycles over the loop, amp: Float64Array(nCtl+1) control values every CTL samples,
  //          phL, phR: start phases (radians) for the left and right channel; partial = amp * sin(phase) }
  function renderPartials(L, parts, outL, outR) {
    const nCtl = Math.ceil(L / CTL);
    for (let p = 0; p < parts.length; p++) {
      const P = parts[p], A = P.amp;
      const w = TWO_PI * P.cyc / L, cw = Math.cos(w), sw = Math.sin(w);
      let re = Math.cos(P.phL), im = Math.sin(P.phL);
      const dR = P.phR - P.phL, cd = Math.cos(dR), sd = Math.sin(dR);
      let n = 0;
      for (let c = 0; c < nCtl; c++) {
        const m = 1 / Math.sqrt(re * re + im * im); re *= m; im *= m;   // keep the rotor on the unit circle
        const a0 = A[c], da = (A[c + 1] - a0) / CTL, end = Math.min(L, n + CTL);
        let a = a0;
        for (; n < end; n++) {
          outL[n] += a * im;
          outR[n] += a * (im * cd + re * sd);
          a += da;
          const t = re * cw - im * sw; im = re * sw + im * cw; re = t;
        }
      }
    }
  }
  function ctlCount(L) { return Math.ceil(L / CTL) + 1; }
  // x position (0..1, wrapped) of control point c
  function ctlX(c, L) { const x = (c * CTL) / L; return x - Math.floor(x); }

  // Scale the amplitude tables so the summed power at each control point follows P^(1-gamma) of the raw power
  // (gamma 1 = constant loudness, 0 = untouched).
  function levelPower(tables, gamma) {
    if (!gamma) return;
    const nC = tables[0].length;
    let mean = 0;
    const pw = new Float64Array(nC);
    for (let c = 0; c < nC; c++) { let s = 0; for (const T of tables) s += T[c] * T[c]; pw[c] = s; mean += s / nC; }
    for (let c = 0; c < nC; c++) {
      const g = Math.pow(mean / Math.max(pw[c], 1e-20), gamma / 2);
      for (const T of tables) T[c] *= g;
    }
  }

  // Small seeded L/R phase spread per partial (a little width; pitch is untouched). Low partials stay centred.
  function spread(tag, k, width) { return (rand01(hash32(tag, k, 0x5157)) - 0.5) * width * Math.min(1, k / 4); }

  function harmonicSound(tag, ampFn, opts) {
    const { n, L, f0 } = pitchedLoop(C3_HZ, 6);
    const kMax = Math.floor(TOP_HZ / f0), nC = ctlCount(L);
    const tables = [], parts = [];
    for (let k = 1; k <= kMax; k++) {
      const T = new Float64Array(nC);
      for (let c = 0; c < nC; c++) T[c] = ampFn(k, ctlX(c, L), k * f0);
      tables.push(T);
    }
    levelPower(tables, opts.gamma || 0);
    for (let k = 1; k <= kMax; k++) {
      const base = opts.phase ? opts.phase(k) : 0, d = spread(tag, k, opts.width);
      parts.push({ cyc: k * n, amp: tables[k - 1], phL: base + d / 2, phR: base - d / 2 });
    }
    const outL = new Float64Array(L), outR = new Float64Array(L);
    renderPartials(L, parts, outL, outR);
    return [outL, outR];
  }

  /* ---------------- shapes used by the sweeps ---------------- */
  // Rises 0 -> 1 linearly over [0, 1-tail), then a cosine back to 0 over the tail, so the loop seam is seamless.
  function riseAndReturn(x, tail) {
    const xo = 1 - tail;
    return x < xo ? x / xo : 0.5 + 0.5 * Math.cos(Math.PI * (x - xo) / tail);
  }
  function lp2mag2(r, q) { const a = 1 - r * r; return a * a + (r / q) * (r / q); }   // 1/|H|^2 of a 2-pole low-pass

  /* ---------------- the sounds ---------------- */
  // Saw-like sounds start half a period into the wave (a pure time shift), so sample 0 sits mid-ramp, not on the edge.
  const HALF = (k) => k * Math.PI;
  // Plain band-limited saw: sum sin(k t)/k, static.
  function makeSaw(seed) {
    const tag = hash32(seed, strHash('saw'));
    return harmonicSound(tag, (k) => 1 / k, { width: 0.9, phase: HALF });
  }

  // Saw whose 24 dB low-pass (slightly resonant) opens from 300 Hz to 14 kHz across 92% of the length, then
  // closes again over the last 0.48 s so the end joins the start.
  function makeSawOpen(seed) {
    const tag = hash32(seed, strHash('sawopen'));
    const F_LO = 300, OCT = Math.log2(14000 / F_LO);
    return harmonicSound(tag, (k, x, hz) => {
      const fc = F_LO * Math.pow(2, OCT * riseAndReturn(x, 0.08)), r = hz / fc;
      return (1 / k) / Math.sqrt(lp2mag2(r, 0.54) * lp2mag2(r, 1.6));
    }, { width: 0.9, gamma: 0.35, phase: HALF });
  }

  // Band-limited pulse whose width sweeps 50% -> 10% -> 50% across the length (a PWM sweep), loudness half-kept.
  function makeSquare(seed) {
    const tag = hash32(seed, strHash('square'));
    return harmonicSound(tag, (k, x) => {
      const d = 0.5 - 0.4 * (0.5 - 0.5 * Math.cos(TWO_PI * x));
      const g = Math.pow(0.25 / (d * (1 - d)), 0.25);
      return g * (2 / (Math.PI * k)) * Math.sin(Math.PI * k * d);
    }, { width: 0.9, phase: () => Math.PI / 2 });
  }

  // Saw through a 4-formant cascade (vocal tract) gliding from "aah" to "ooh" over 88% of the length and back over
  // the last 0.72 s. Loudness kept constant.
  const VOWEL_A = { F: [700, 1150, 2650, 3350], B: [90, 110, 160, 250] };
  const VOWEL_U = { F: [320, 680, 2400, 3200], B: [70, 80, 140, 240] };
  function makeVowel(seed) {
    const tag = hash32(seed, strHash('vowel'));
    return harmonicSound(tag, (k, x, hz) => {
      const m = riseAndReturn(x, 0.12);
      let g = 1 / k;
      for (let i = 0; i < 4; i++) {
        const F = VOWEL_A.F[i] * Math.pow(VOWEL_U.F[i] / VOWEL_A.F[i], m);
        const B = VOWEL_A.B[i] * Math.pow(VOWEL_U.B[i] / VOWEL_A.B[i], m);
        const a = F * F - hz * hz;
        g *= (F * F) / Math.sqrt(a * a + (hz * B) * (hz * B));
      }
      return g;
    }, { width: 0.9, gamma: 1, phase: HALF });
  }

  // v3's makeDefault (18 partials 1/k^0.9, each with its own slow swell, plus a copy detuned x1.0035 at 0.55),
  // with seeded phases, 6 s long, and every rate rounded to whole cycles over the loop so it is exactly periodic.
  function makePad(seed) {
    const tag = hash32(seed, strHash('pad'));
    const { n, L } = pitchedLoop(C3_HZ, 6), secs = L / SR, nC = ctlCount(L);
    const parts = [];
    for (let k = 1; k <= 18; k++) {
      const rateCyc = Math.max(1, Math.round((0.13 + k * 0.037) * secs));
      const T = new Float64Array(nC);
      for (let c = 0; c < nC; c++) {
        const x = ctlX(c, L);
        T[c] = (1 / Math.pow(k, 0.9)) * (0.5 + 0.5 * Math.sin(TWO_PI * rateCyc * x + k * 1.7));
      }
      const T2 = T.map((v) => v * 0.55);
      const ph = rand01(hash32(tag, k, 1)) * TWO_PI, ph2 = rand01(hash32(tag, k, 2)) * TWO_PI;
      const d = spread(tag, k, 0.6), d2 = (rand01(hash32(tag, k, 3)) - 0.5) * 1.6;
      parts.push({ cyc: k * n, amp: T, phL: ph + d / 2, phR: ph - d / 2 });
      parts.push({ cyc: k * n + Math.max(1, Math.round(k * n * 0.0035)), amp: T2, phL: ph2 + d2 / 2, phR: ph2 - d2 / 2 });
    }
    const outL = new Float64Array(L), outR = new Float64Array(L);
    renderPartials(L, parts, outL, outR);
    return [outL, outR];
  }

  // Add a decaying partial circularly into out (index wraps at L).
  function addDecaying(outL, outR, start, len, hz, amp, tau, ph, gL, gR, attack) {
    const L = outL.length, w = TWO_PI * hz / SR, rho = Math.exp(-1 / (tau * SR));
    const cw = Math.cos(w) * rho, sw = Math.sin(w) * rho;
    let re = Math.cos(ph) * amp, im = Math.sin(ph) * amp;
    for (let i = 0; i < len; i++) {
      let v = im;
      if (i < attack) v *= 0.5 - 0.5 * Math.cos(Math.PI * i / attack);
      let j = start + i; if (j >= L) j -= L;
      outL[j] += v * gL; outR[j] += v * gR;
      const t = re * cw - im * sw; im = re * sw + im * cw; re = t;
    }
  }

  // Plucked arpeggio, 120 bpm eighth notes: C - Am - F G, around C3. Notes get longer and brighter across the loop.
  const PLUCK_NOTES = [60, 64, 67, 72, 67, 64, 60, 55, 57, 60, 64, 69, 64, 60, 57, 52, 53, 57, 60, 65, 55, 59, 62, 67];
  function makePluck(seed) {
    const tag = hash32(seed, strHash('pluck'));
    const L = 6 * SR, step = L / PLUCK_NOTES.length;
    const outL = new Float64Array(L), outR = new Float64Array(L);
    PLUCK_NOTES.forEach((p, i) => {
      const f = 440 * Math.pow(2, (p - 69) / 12), prog = i / (PLUCK_NOTES.length - 1);
      const vel = i % 4 === 0 ? 1 : 0.72;
      const tau1 = 0.28 + 0.3 * prog, tilt = 1.5 - 0.6 * prog;
      const pan = (rand01(hash32(tag, i, 11)) - 0.5) * 0.6, ang = (pan + 1) * Math.PI / 4;
      const gL = Math.cos(ang) * Math.SQRT2, gR = Math.sin(ang) * Math.SQRT2;
      const start = Math.round(i * step);
      for (let k = 1; k <= 32; k++) {
        const hz = k * f * Math.sqrt(1 + 0.00006 * k * k);
        if (hz > 18000) break;
        const amp = vel * Math.pow(k, -tilt) * (0.4 + 0.6 * Math.abs(Math.sin(Math.PI * k * 0.19)));
        const tau = tau1 / (1 + 0.35 * (k - 1));
        const len = Math.min(L, Math.ceil(tau * 11.5 * SR));
        addDecaying(outL, outR, start, len, hz, amp, tau, rand01(hash32(tag, i, k)) * TWO_PI, gL, gR, Math.round(0.003 * SR));
      }
    });
    return [outL, outR];
  }

  // Biquad high-pass (RBJ) run over an array in place.
  function highpass(x, fc, q) {
    const w = TWO_PI * fc / SR, al = Math.sin(w) / (2 * q), cs = Math.cos(w), a0 = 1 + al;
    const b0 = (1 + cs) / 2 / a0, b1 = -(1 + cs) / a0, b2 = b0, a1 = -2 * cs / a0, a2 = (1 - al) / a0;
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < x.length; i++) {
      const v = x[i], y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = v; y2 = y1; y1 = y; x[i] = y;
    }
  }
  function addCirc(out, start, seg, g) {
    const L = out.length;
    for (let i = 0; i < seg.length; i++) { let j = start + i; while (j >= L) j -= L; out[j] += seg[i] * g; }
  }
  function taper(seg, ms) {           // fade the last `ms` to zero so a truncated tail never steps
    const m = Math.min(seg.length, Math.round(ms * 0.001 * SR));
    for (let i = 0; i < m; i++) seg[seg.length - 1 - i] *= 0.5 - 0.5 * Math.cos(Math.PI * i / m);
  }

  function kick(tag, idx, level) {
    const len = Math.round(1.6 * SR), seg = new Float64Array(len), atk = Math.round(0.001 * SR);
    const nb = hash32(tag, idx, 21);
    let lp = 0;
    for (let i = 0; i < len; i++) {
      const t = i / SR;
      const ph = TWO_PI * (47 * t + 125 * 0.035 * (1 - Math.exp(-t / 0.035)));
      let v = Math.sin(ph) * Math.exp(-t / 0.3);
      if (i < atk) v *= i / atk;
      lp += 0.35 * (noiseAt(nb, i) - lp);
      if (t < 0.006) v += 0.25 * lp * (1 - t / 0.006);
      seg[i] = v * level;
    }
    taper(seg, 60);
    return seg;
  }
  // bodyLevel 1 = the full snare (tone + noise); 0 = only a second, independent noise layer (used for width).
  function snare(tag, idx, level, side, bodyLevel) {
    const len = Math.round(0.7 * SR), body = new Float64Array(len), nz = new Float64Array(len);
    const nb = hash32(tag, idx, 31 + side), atk = Math.round(0.0015 * SR);
    for (let i = 0; i < len; i++) {
      const t = i / SR;
      body[i] = Math.sin(TWO_PI * 182 * t) * Math.exp(-t / 0.06) + 0.5 * Math.sin(TWO_PI * 331 * t) * Math.exp(-t / 0.04);
      nz[i] = noiseAt(nb, i);
    }
    highpass(nz, 1600, 0.7);
    const seg = new Float64Array(len);
    for (let i = 0; i < len; i++) {
      const t = i / SR;
      let v = 0.55 * bodyLevel * body[i] + 0.9 * nz[i] * Math.exp(-t / 0.12);
      if (i < atk) v *= i / atk;
      seg[i] = v * level;
    }
    taper(seg, 40);
    return seg;
  }
  function hat(tag, idx, level, open, side) {
    const len = Math.round((open ? 0.9 : 0.18) * SR), seg = new Float64Array(len);
    const nb = hash32(tag, idx, 41 + side), tau = open ? 0.17 : 0.028, atk = Math.round(0.0008 * SR);
    for (let i = 0; i < len; i++) seg[i] = noiseAt(nb, i);
    highpass(seg, 7200, 0.8);
    highpass(seg, 5200, 0.6);
    for (let i = 0; i < len; i++) {
      let v = seg[i] * Math.exp(-i / SR / tau);
      if (i < atk) v *= i / atk;
      seg[i] = v * level;
    }
    taper(seg, open ? 50 : 15);
    return seg;
  }

  // Two bars at 120 bpm on a 16th grid; bar 2 varies (extra kicks, a ghost snare, an open hat).
  const BEAT_BARS = [
    { k: [0, 8, 10], s: [4, 12], h: [0, 2, 4, 6, 8, 10, 12, 14], o: [] },
    { k: [0, 7, 8, 11], s: [4, 12, 15], h: [0, 2, 4, 6, 8, 10, 12], o: [14] },
  ];
  function makeBeat(seed) {
    const tag = hash32(seed, strHash('beat'));
    const L = 4 * SR, s16 = SR * 0.5 / 4;      // 120 bpm: a beat is 0.5 s, a 16th is 5512.5 samples
    const outL = new Float64Array(L), outR = new Float64Array(L);
    let idx = 0;
    BEAT_BARS.forEach((bar, b) => {
      const at = (st) => Math.round((b * 16 + st) * s16);
      for (const st of bar.k) { const seg = kick(tag, idx++, 1.0); addCirc(outL, at(st), seg, 1); addCirc(outR, at(st), seg, 1); }
      for (const st of bar.s) {
        const lv = st === 15 ? 0.35 : 0.8, i0 = idx++;
        const a = snare(tag, i0, lv, 0, 1), c = snare(tag, i0, lv, 1, 0);
        addCirc(outL, at(st), a, 0.85); addCirc(outL, at(st), c, 0.15);
        addCirc(outR, at(st), a, 0.85); addCirc(outR, at(st), c, -0.15);
      }
      for (const st of bar.h.concat(bar.o)) {
        const open = bar.o.indexOf(st) >= 0, lv = open ? 0.33 : (st % 4 === 2 ? 0.34 : 0.22), i0 = idx++;
        const pan = st % 4 === 2 ? 0.3 : 0.12, a = hat(tag, i0, lv, open, 0);
        addCirc(outL, at(st), a, Math.cos((pan + 1) * Math.PI / 4) * Math.SQRT2);
        addCirc(outR, at(st), a, Math.sin((pan + 1) * Math.PI / 4) * Math.SQRT2);
      }
    });
    return [outL, outR];
  }

  // Seeded white noise through a moving state-variable filter (band-pass + some low-pass). The cutoff sweeps
  // between ~200 Hz and ~7 kHz and the resonance breathes, all periodic over the loop. The filter runs twice round
  // the loop and keeps the second pass, so its state at the end matches the start: the seam is seamless.
  function makeNoise(seed) {
    const tag = hash32(seed, strHash('noise'));
    const L = 6 * SR;
    const mid = new Float64Array(L), side = new Float64Array(L);
    [[mid, 1], [side, 2]].forEach(([out, stream]) => {
      const nb = hash32(tag, stream, 51);
      let ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, kq = 1, gbp = 1;
      for (let pass = 0; pass < 2; pass++) {
        for (let i = 0; i < L; i++) {
          if (i % 32 === 0) {
            const x = i / L;
            const fc = 250 * Math.pow(2, 4.3 * (0.5 - 0.5 * Math.cos(TWO_PI * x)) + 0.6 * Math.sin(TWO_PI * 3 * x + 0.7));
            const q = 1.4 + 1.6 * (0.5 - 0.5 * Math.cos(TWO_PI * 2 * x + 1.1));
            const g = Math.tan(Math.PI * Math.min(fc, 0.45 * SR) / SR);
            kq = 1 / q; a1 = 1 / (1 + g * (g + kq)); a2 = g * a1; a3 = g * a2;
            gbp = kq * Math.pow(900 / fc, 0.3);     // narrow low bands carry less power: lift them part-way
          }
          const v0 = noiseAt(nb, i);
          const v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
          ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
          if (pass === 1) out[i] = 0.8 * gbp * v1 + 0.3 * v2;
        }
      }
    });
    const outL = new Float64Array(L), outR = new Float64Array(L);
    for (let i = 0; i < L; i++) {
      const x = i / L, a = 0.75 + 0.25 * Math.sin(TWO_PI * 2 * x);
      outL[i] = a * (mid[i] + 0.4 * side[i]);
      outR[i] = a * (mid[i] - 0.4 * side[i]);
    }
    return [outL, outR];
  }

  const MAKERS = { sawopen: makeSawOpen, saw: makeSaw, square: makeSquare, pad: makePad, vowel: makeVowel,
    pluck: makePluck, beat: makeBeat, noise: makeNoise };

  function finish(chans) {
    let m = 0;
    for (const ch of chans) for (let i = 0; i < ch.length; i++) { const a = Math.abs(ch[i]); if (a > m) m = a; }
    const g = m > 0 ? PEAK / m : 0;
    return chans.map((ch) => { const out = new Float32Array(ch.length); for (let i = 0; i < ch.length; i++) out[i] = ch[i] * g; return out; });
  }

  /** make(id, opts?) -> { id, name, sampleRate, channels:[L, R], rootMidi:60 }. opts.seed (default 1). */
  function make(id, opts) {
    const entry = LIST.find((s) => s.id === id);
    if (!entry) throw new Error('SLSounds.make: unknown sound "' + id + '"');
    const seed = opts && Number.isFinite(opts.seed) ? opts.seed | 0 : 1;
    const channels = finish(MAKERS[id](seed));
    return { id, name: entry.name, sampleRate: SR, channels, rootMidi: 60 };
  }

  return {
    list: LIST.map((s) => ({ id: s.id, name: s.name })),
    defaultId: 'sawopen',
    make,
    ROOT_HZ: C3_HZ,
    PITCHED: ['sawopen', 'saw', 'square', 'pad', 'vowel'],
  };
})();
if (typeof module !== 'undefined') module.exports = SLSounds;
