/* StrandLock prototype v4 - sl-analysis.js (SLAnalysis)
   Pitch and key detection (ported from v3) and part suggestions for long files.
   Plain script: defines one global, no imports, no randomness. Naming follows Cubase: MIDI 60 = C3.

   detectPitch(channels, sr) -> { midi, cents, hz } | null      ("no clear pitch")
   detectKey(channels, sr)   -> { key: 0..11 (C=0), scale: 'Major'|'Minor', r } | null (silence)
   suggestParts(channels, sr) -> up to 4 { kind:'steady'|'hit'|'quiet'|'loop', label, start, end } (fractions of the file;
                                the loop part also carries bpm and tempoFound) */
var SLAnalysis = (function () {   // var: also reachable as window.SLAnalysis
  'use strict';

  function mono(channels) {
    const n = channels[0].length, nc = channels.length, d = new Float32Array(n);
    for (let c = 0; c < nc; c++) { const x = channels[c]; for (let i = 0; i < n; i++) d[i] += x[i]; }
    if (nc > 1) for (let i = 0; i < n; i++) d[i] /= nc;
    return d;
  }
  function validInput(channels, sr) {
    return channels && channels.length > 0 && channels[0] && channels[0].length > 0 && sr > 0;
  }
  // A NaN or inf sample (a float WAV from a misbehaving plugin goes through decodeAudioData unchanged) reads as 0,
  // so one bad sample cannot poison the prefix sums, the tempo or the chroma. Clean input is returned as is.
  function clean(channels) {
    let bad = false;
    for (const x of channels) { for (let i = 0; i < x.length; i++) { const v = x[i]; if (!(v > -1e30 && v < 1e30)) { bad = true; break; } } if (bad) break; }
    if (!bad) return channels;
    return channels.map((x) => { const y = new Float32Array(x.length); for (let i = 0; i < x.length; i++) { const v = x[i]; y[i] = v > -1e30 && v < 1e30 ? v : 0; } return y; });
  }

  /* ---------------- pitch: YIN on the 5 loudest 2048-sample frames, median (v3 detectPitch) ---------------- */
  // Change from v3: at least half of the examined frames must be periodic (v3 needed one), and a silent file is null.
  function detectPitch(channels, sr) {
    if (!validInput(channels, sr)) return null;
    const d = mono(clean(channels)), W = 2048, maxT = Math.floor(sr / 45), minT = Math.floor(sr / 1500);
    if (d.length < W + maxT + 10) return null;
    const blocks = [];
    for (let s = 0; s + W + maxT < d.length; s += W) {
      let e = 0;
      for (let i = s; i < s + W; i += 4) e += d[i] * d[i];
      blocks.push([e, s]);
    }
    blocks.sort((a, b) => b[0] - a[0] || a[1] - b[1]);
    if (!(blocks[0][0] > 1e-9)) return null;                       // digital silence
    const loud = blocks.filter((b) => b[0] > blocks[0][0] * 1e-4), top = loud.slice(0, 5);
    const res = [], df = new Float64Array(maxT + 2), cm = new Float64Array(maxT + 2);
    // the 5 loudest frames; when fewer than half of them are periodic (loud bursts over a held note), the next 10
    // loudest are examined too, and the fallback below decides
    let frames = top;
    for (let pass = 0; pass < 2; pass++) {
    if (pass === 1) { if (res.length >= Math.ceil(top.length / 2)) break; frames = loud.slice(5, 15); }
    for (const [, st] of frames) {
      for (let tau = 1; tau <= maxT + 1; tau++) {
        let s = 0;
        for (let j = 0; j < W; j++) { const x = d[st + j] - d[st + j + tau]; s += x * x; }
        df[tau] = s;
      }
      let run = 0, best = -1;
      cm[0] = 1;
      for (let tau = 1; tau <= maxT + 1; tau++) { run += df[tau]; cm[tau] = df[tau] * tau / (run || 1); }
      for (let tau = minT; tau <= maxT; tau++) {
        if (cm[tau] < 0.15) { while (tau + 1 <= maxT && cm[tau + 1] < cm[tau]) tau++; best = tau; break; }
      }
      if (best < 0) {
        let m = 1e9;
        for (let tau = minT; tau <= maxT; tau++) if (cm[tau] < m) { m = cm[tau]; best = tau; }
        if (m > 0.45) continue;
      }
      const a = cm[best - 1], b = cm[best], c = cm[best + 1], den = a - 2 * b + c, sh = den ? 0.5 * (a - c) / den : 0;
      res.push(sr / (best + Math.max(-1, Math.min(1, sh))));
    }
    }
    if (!res.length) return null;
    res.sort((a, b) => a - b);
    let hz = res[Math.floor(res.length / 2)];
    if (res.length < Math.ceil(top.length / 2)) {
      // fewer than half the frames are periodic (loud bursts over a held note): still a pitch when at least two
      // periodic frames agree within 20 cents (v3 accepted any single frame; noise rarely gives two that agree)
      let bi = -1, bn = 1;
      for (let i = 0; i < res.length; i++) { let j = i; while (j + 1 < res.length && 1200 * Math.log2(res[j + 1] / res[i]) <= 20) j++; if (j - i + 1 > bn) { bn = j - i + 1; bi = i; } }
      if (bi < 0) return null;
      hz = res[bi + Math.floor(bn / 2)];
    }
    const m = 69 + 12 * Math.log2(hz / 440), midi = Math.round(m);
    return { midi, cents: Math.round((m - midi) * 1000) / 10, hz };
  }

  /* ---------------- key: Goertzel chroma over MIDI 36-95, ~40 frames, Krumhansl-Kessler (v3 detectKey) ---------------- */
  const KK_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
  const KK_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
  function corr(a, b) {
    let ma = 0, mb = 0;
    for (let i = 0; i < 12; i++) { ma += a[i] / 12; mb += b[i] / 12; }
    let n = 0, da = 0, db = 0;
    for (let i = 0; i < 12; i++) { n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) ** 2; db += (b[i] - mb) ** 2; }
    return n / Math.sqrt(da * db || 1);
  }
  function detectKey(channels, sr) {
    if (!validInput(channels, sr)) return null;
    const d = mono(clean(channels)), N = 4096, frames = Math.min(40, Math.floor(d.length / N));
    if (frames < 1) return null;
    const chroma = new Float64Array(12), win = new Float32Array(N);
    for (let i = 0; i < N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N);
    const coefs = [];
    for (let m = 36; m < 96; m++) coefs.push(2 * Math.cos(2 * Math.PI * 440 * Math.pow(2, (m - 69) / 12) / sr));
    const xw = new Float32Array(N);
    for (let f = 0; f < frames; f++) {
      const st = Math.floor(f * (d.length - N) / Math.max(1, frames - 1));
      for (let i = 0; i < N; i++) xw[i] = d[st + i] * win[i];
      for (let m = 36; m < 96; m++) {
        const co = coefs[m - 36];
        let s1 = 0, s2 = 0;
        for (let i = 0; i < N; i++) { const s0 = xw[i] + co * s1 - s2; s2 = s1; s1 = s0; }
        chroma[m % 12] += Math.sqrt(Math.max(0, s1 * s1 + s2 * s2 - co * s1 * s2));
      }
    }
    let tot = 0;
    for (let i = 0; i < 12; i++) tot += chroma[i];
    if (!(tot > 1e-6)) return null;
    let best = { r: -2, key: 0, scale: 'Major' };
    const rot = new Float64Array(12);
    for (let k = 0; k < 12; k++) {
      for (let i = 0; i < 12; i++) rot[i] = chroma[(i + k) % 12];
      for (const [pr, sc] of [[KK_MAJOR, 'Major'], [KK_MINOR, 'Minor']]) {
        const r = corr(rot, pr);
        if (r > best.r) best = { r, key: k, scale: sc };
      }
    }
    return { key: best.key, scale: best.scale, r: Math.round(best.r * 1000) / 1000 };
  }

  /* ---------------- FFT (radix 2, in place) ---------------- */
  const plans = new Map();
  function plan(n) {
    let p = plans.get(n);
    if (p) return p;
    const rev = new Uint32Array(n), bits = Math.log2(n) | 0;
    for (let i = 0; i < n; i++) { let r = 0; for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b); rev[i] = r; }
    const cs = new Float64Array(n / 2), sn = new Float64Array(n / 2);
    for (let i = 0; i < n / 2; i++) { cs[i] = Math.cos(2 * Math.PI * i / n); sn[i] = -Math.sin(2 * Math.PI * i / n); }
    p = { n, rev, cs, sn };
    plans.set(n, p);
    return p;
  }
  function fft(re, im, p) {
    const n = p.n, rev = p.rev;
    for (let i = 0; i < n; i++) { const j = rev[i]; if (j > i) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (let size = 2; size <= n; size <<= 1) {
      const half = size >> 1, step = n / size;
      for (let s = 0; s < n; s += size) {
        for (let j = 0, k = 0; j < half; j++, k += step) {
          const a = s + j, b = a + half, wr = p.cs[k], wi = p.sn[k];
          const tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
          re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        }
      }
    }
  }

  /* ---------------- part suggestions ---------------- */
  function pct(arr, q) {
    const a = Array.from(arr).filter(Number.isFinite).sort((x, y) => x - y);
    if (!a.length) return 0;
    return a[Math.min(a.length - 1, Math.max(0, Math.round(q * (a.length - 1))))];
  }
  function clamp(x, a, b) { return x < a ? a : x > b ? b : x; }
  function fmtTime(s) { const m = Math.floor(s / 60), r = Math.floor(s - m * 60); return m + ':' + (r < 10 ? '0' : '') + r; }
  function overlapOk(a, chosen) {
    for (const b of chosen) {
      const ov = Math.min(a.e, b.e) - Math.max(a.s, b.s);
      if (ov > 0.5 * Math.min(a.e - a.s, b.e - b.s) + 1e-9) return false;
    }
    return true;
  }

  // Features on a ~11 kHz mono copy: fine frames (128 samples, ~11.6 ms) give level, high band level and onsets;
  // coarse frames (512 samples, ~46 ms) add spectral flatness and spectral flux from one FFT each.
  function features(channels, sr) {
    const N = channels[0].length, nCh = channels.length;
    const D = Math.max(1, Math.round(sr / 11025)), srd = sr / D, M = Math.floor(N / D);
    const y = new Float32Array(M), g = 1 / (D * nCh);
    for (let c = 0; c < nCh; c++) {
      const x = channels[c];
      if (D === 4) {
        for (let j = 0, i = 0; j < M; j++, i += 4) y[j] += (x[i] + x[i + 1] + x[i + 2] + x[i + 3]) * g;
      } else {
        for (let j = 0, i = 0; j < M; j++) { let s = 0; for (let q = 0; q < D; q++) s += x[i++]; y[j] += s * g; }
      }
    }
    const H = 128, nf = Math.floor(M / H), tf = H / srd;
    const Lf = new Float64Array(nf), Hf = new Float64Array(nf);
    let prev = 0;
    for (let i = 0, p = 0; i < nf; i++) {
      let e = 0, eh = 0;
      for (let j = 0; j < H; j++, p++) { const v = y[p], dv = v - prev; e += v * v; eh += dv * dv; prev = v; }
      Lf[i] = 10 * Math.log10(e / H + 1e-12);
      Hf[i] = 10 * Math.log10(eh / H + 1e-12);
    }
    // coarse frames
    const C = 4, nc = Math.floor(nf / C), F = H * C, P = plan(F);
    const win = new Float64Array(F);
    for (let i = 0; i < F; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / F);
    const binHz = srd / F, b0 = Math.max(1, Math.ceil(60 / binHz)), b1 = Math.min(F / 2 - 1, Math.floor(5000 / binHz));
    const NB = 16, bandEdge = [];
    for (let k = 0; k <= NB; k++) bandEdge.push(Math.round(b0 * Math.pow(b1 / b0, k / NB)));
    const Lc = new Float64Array(nc), flat = new Float64Array(nc), flux = new Float64Array(nc);
    const re = new Float64Array(F), im = new Float64Array(F), bands = new Float64Array(NB), prevB = new Float64Array(NB);
    for (let c = 0; c < nc; c++) {
      let e = 0;
      for (let q = 0; q < C; q++) e += Math.pow(10, Lf[c * C + q] / 10);
      Lc[c] = 10 * Math.log10(e / C + 1e-12);
      const off = c * F;
      for (let i = 0; i < F; i++) { re[i] = y[off + i] * win[i]; im[i] = 0; }
      fft(re, im, P);
      // flatness with the spectrum floored 50 dB under its peak, so a faint noise floor between harmonics does not
      // make a held tone look noisy (noise itself stays ~0.5)
      let pmax = 1e-14;
      for (let b = b0; b <= b1; b++) { const pw = re[b] * re[b] + im[b] * im[b]; if (pw > pmax) pmax = pw; }
      const pfl = pmax * 1e-5;
      let lsum = 0, asum = 0;
      for (let b = b0; b <= b1; b++) { const pw = Math.max(pfl, re[b] * re[b] + im[b] * im[b]); lsum += Math.log(pw); asum += pw; }
      const nb = b1 - b0 + 1;
      flat[c] = Math.exp(lsum / nb) / (asum / nb);
      // flux: dB change per band, weighted by each band's share of the energy (faint bands barely count)
      let fx = 0, wsum = 0;
      for (let k = 0; k < NB; k++) {
        let s = 0;
        for (let b = bandEdge[k]; b < Math.max(bandEdge[k] + 1, bandEdge[k + 1]); b++) s += re[b] * re[b] + im[b] * im[b];
        bands[k] = s;
        if (c > 0) { const w = s + prevB[k]; fx += w * Math.abs(10 * Math.log10((s + 1e-14) / (prevB[k] + 1e-14))); wsum += w; }
      }
      for (let k = 0; k < NB; k++) prevB[k] = bands[k];
      flux[c] = c > 0 && wsum > 0 ? fx / wsum : 0;
    }
    return { y, srd, H, nf, tf, Lf, Hf, C, nc, tc: tf * C, Lc, flat, flux };
  }

  // opts.debug: return { parts, debug } where debug holds every candidate's time and score (tests print margins).
  function suggestParts(channels, sr, opts) {
    const wantDebug = !!(opts && opts.debug);
    const out = (parts, debug) => (wantDebug ? { parts, debug: debug || {} } : parts);
    if (!validInput(channels, sr)) return out([]);
    const N = channels[0].length, dur = N / sr;
    if (dur < 1.2) return out([]);
    const ft = features(clean(channels), sr);
    const { nf, tf, Lf, Hf, C, nc, tc, Lc, flat, flux } = ft;
    if (nc < 8) return out([]);

    // levels relative to the loud parts of the file
    const Lref = pct(Lc, 0.98);
    if (!(Lref > -100)) return out([]);
    const floorDb = Lref - 70, HrefFloor = pct(Hf, 0.98) - 70;
    const sil = new Uint8Array(nc);
    for (let c = 0; c < nc; c++) sil[c] = Lc[c] < Lref - 60 || Lc[c] < -100 ? 1 : 0;

    // onsets (dB rise over the quietest of the three previous fine frames, in either band)
    const rise = new Float64Array(nf), on = new Float64Array(nf);
    for (let i = 0; i < nf; i++) {
      const l = Math.max(Lf[i], floorDb), h = Math.max(Hf[i], HrefFloor);
      let ml = Infinity, mh = Infinity;
      for (let q = 1; q <= 3; q++) { ml = Math.min(ml, i - q >= 0 ? Math.max(Lf[i - q], floorDb) : floorDb); mh = Math.min(mh, i - q >= 0 ? Math.max(Hf[i - q], HrefFloor) : HrefFloor); }
      rise[i] = Math.max(l - ml, h - mh);
      on[i] = Math.max(0, rise[i] - 3);
    }

    // prefix sums on the coarse grid
    const pre = (f) => { const a = new Float64Array(nc + 1); for (let c = 0; c < nc; c++) a[c + 1] = a[c] + f(c); return a; };
    const onC = new Float64Array(nc), bigC = new Float64Array(nc);
    for (let c = 0; c < nc; c++) for (let q = 0; q < C; q++) { const v = on[c * C + q]; onC[c] += v; if (rise[c * C + q] >= 15) bigC[c] += 1; }
    const sL = pre((c) => Lc[c] - Lref), sL2 = pre((c) => (Lc[c] - Lref) ** 2), sF = pre((c) => (sil[c] ? 1 : Math.min(1, flat[c])));
    const sX = pre((c) => flux[c]), sO = pre((c) => onC[c]), sB = pre((c) => bigC[c]), sS = pre((c) => sil[c]);
    function win(c, K) {
      const m = (a) => (a[c + K] - a[c]) / K;
      const meanRel = m(sL), std = Math.sqrt(Math.max(0, m(sL2) - meanRel * meanRel));
      return { meanRel, std, flat: m(sF), flux: m(sX), onRate: (sO[c + K] - sO[c]) / (K * tc), big: sB[c + K] - sB[c], sil: m(sS) };
    }

    const parts = [], chosen = [];
    const take = (kind, s, e, label, extra) => {
      const p = Object.assign({ kind, label, start: clamp(s / dur, 0, 1), end: clamp(e / dur, 0, 1) }, extra || {});
      parts.push(p); chosen.push({ s, e });
    };
    // place a region of length len (s) so it starts at t0 if possible, inside the file
    const place = (t0, len) => { const s = clamp(t0, 0, Math.max(0, dur - len)); return { s, e: Math.min(dur, s + len) }; };

    // refine an onset to ~1.5 ms: first 16-sample step whose energy reaches halfway (dB) from the floor before it
    function refineOnset(i) {
      const y = ft.y, H = ft.H, st = Math.max(0, (i - 2) * H), en = Math.min(y.length - 16, (i + 1) * H);
      const before = Math.max(Math.min(Lf[Math.max(0, i - 3)], Lf[Math.max(0, i - 2)]), floorDb), peak = Math.max(Lf[i], Lf[Math.min(nf - 1, i + 1)]);
      const thr = before + 0.5 * (peak - before);
      for (let p = st; p < en; p += 16) {
        let e = 0;
        for (let q = 0; q < 16; q++) e += y[p + q] * y[p + q];
        if (10 * Math.log10(e / 16 + 1e-12) >= thr) return p / ft.srd;
      }
      return i * tf;
    }

    // ---- loop: one bar at the tempo found from the onset autocorrelation (120 bpm if none is clear)
    const tempo = estimateTempo(on, tf);
    let loopScored = [];
    const bar = 240 / tempo.bpm, B = bar / tf;
    if (bar <= dur) {
      const oRef = Math.max(1, pct(on.filter((v) => v > 0), 0.9));
      const cands = [];
      for (let i = 0; i + 1 < nf; i++) if (on[i] >= 6 && (i === 0 || on[i] >= on[i - 1]) && on[i] > on[i + 1]) cands.push(i);
      cands.sort((a, b) => on[b] - on[a] || a - b);
      const grid = (s, j) => { const c = Math.round(s + j * B / 8); let m = 0; for (let q = -1; q <= 1; q++) if (c + q >= 0 && c + q < nf) m = Math.max(m, on[c + q]); return m; };
      const scored = [];
      for (const s of cands.slice(0, 400)) {
        if (s + B > nf) continue;
        let al = 0, all = 0, gA = [], gB = [];
        for (let j = 0; j < 8; j++) { const v = grid(s, j); gA.push(v); al += v; }
        for (let i = s; i < Math.min(nf, Math.round(s + B)); i++) all += on[i];
        let rep = 0;
        if (s + B + 7 * B / 8 + 2 <= nf) {   // the next bar's 8 grid points fit (a file of exactly 2 bars, rotated, still compares)
          let dsum = 0, ssum = 0;
          for (let j = 0; j < 8; j++) { const v = grid(s + B, j); gB.push(v); dsum += Math.abs(v - gA[j]); ssum += v + gA[j]; }
          rep = ssum > 0 ? 1 - dsum / ssum : 0;
        }
        const c0 = Math.floor(s / C), K2 = Math.max(1, Math.min(nc - c0, Math.round(B / C)));
        const lvl = c0 < nc ? clamp((win(c0, K2).meanRel + 30) / 24, 0, 1) : 0;
        const score = Math.min(1.5, al / (8 * oRef)) + 0.8 * rep + 0.3 * Math.min(1.5, on[s] / oRef) + 0.3 * lvl - 0.3 * Math.min(1, Math.max(0, all - al) / (8 * oRef));
        scored.push({ s, score, al: al / (8 * oRef), rep, on: on[s] / oRef, lvl });
      }
      scored.sort((a, b) => b.score - a.score || a.s - b.s);
      loopScored = scored;
    }
    // the best loop's region: the hit avoids it when an almost as good hit does not (a two-bar beat would otherwise
    // lose its downbeat loop to whichever drum hit scored a point higher)
    let loopBestR = null;
    if (loopScored.length) loopBestR = place(refineOnset(loopScored[0].s) - 0.005, bar);

    // ---- hit: the loudest transient. Peak loudness counts most; the rise counts up to 18 dB (beyond that a jump from
    // near silence is no bigger a hit).
    const hitCands = [];
    const decays = (i, pk) => { let m = -Infinity; for (let j = i + Math.round(0.3 / tf); j < Math.min(nf, i + Math.round(0.8 / tf)); j++) m = Math.max(m, Lf[j]); return m <= pk - 5; };   // 5 dB down 0.3-0.8 s later
    for (let i = 0; i < nf; i++) {
      if (rise[i] < 9) continue;
      let isMax = true;
      for (let q = -3; q <= 3 && isMax; q++) if (q && i + q >= 0 && i + q < nf && rise[i + q] > rise[i]) isMax = false;
      if (!isMax) continue;
      let pk = -Infinity;
      for (let q = 0; q < 9 && i + q < nf; q++) pk = Math.max(pk, Lf[i + q]);
      // a rise in the first frames (the file starts on it) is a hit only when it is a transient that decays, so a
      // file that simply starts loud (a held tone) does not get a "Big hit 0:00"
      if (i < 3 && !decays(i, pk)) continue;
      hitCands.push({ i, score: Math.min(rise[i], 18) + 4 * (pk - Lref), pk });
    }
    hitCands.sort((a, b) => b.score - a.score || a.i - b.i);
    const hitTop = hitCands.length ? hitCands[0].score : 0;
    const hitOrder = hitCands.slice(0, 200);
    if (loopBestR) {   // first the near-best (>= 85 % of the top score) hits that leave the best loop free, then the rest
      const hitRegion = (h) => { let j = h.i + Math.round(0.3 / tf); for (; j < nf && (j - h.i) * tf < 3; j++) if (Lf[j] < h.pk - 30 || rise[j] >= 12) break; return place(refineOnset(h.i) - 0.04, clamp((j - h.i) * tf + 0.04, 1, 3)); };
      const free = hitOrder.filter((h) => h.score >= 0.85 * hitTop && overlapOk(hitRegion(h), [loopBestR]));
      if (free.length && free[0] !== hitOrder[0]) { hitOrder.splice(hitOrder.indexOf(free[0]), 1); hitOrder.unshift(free[0]); }
    }
    for (const h of hitOrder) {
      const t = refineOnset(h.i);
      // the part lasts until the sound falls 30 dB below the hit or the next strong onset (1 - 3 s)
      let j = h.i + Math.round(0.3 / tf);
      for (; j < nf && (j - h.i) * tf < 3; j++) if (Lf[j] < h.pk - 30 || rise[j] >= 12) break;
      const len = clamp((j - h.i) * tf + 0.04, 1, 3);
      const r = place(t - 0.04, len);
      if (r.e - r.s >= 1 && t >= r.s && overlapOk(r, chosen)) { take('hit', r.s, r.e, 'Big hit ' + fmtTime(t)); break; }
    }

    // ---- steady: a held, tonal, level stretch
    const Wsec = clamp(dur / 4, 1, 3), K = Math.max(2, Math.min(nc, Math.max(Math.round(Wsec / tc), Math.ceil(1 / tc - 1e-9))));   // >= 1 s at every sample rate
    const steady = [], quiet = [];
    for (let c = 0; c + K <= nc; c++) {
      const w = win(c, K);
      const tonal = 1 - Math.min(1, w.flat / 0.3);
      const sScore = 3 * tonal - 0.25 * w.std - 0.5 * w.flux - 0.05 * w.onRate - 1.5 * w.big + 0.04 * w.meanRel - 0.2 * Math.max(0, -18 - w.meanRel) - 5 * w.sil;
      if (w.flat < 0.25 && w.meanRel > -30 && w.sil < 0.05 && w.std < 4 && w.big === 0 && sScore > 1) steady.push({ c, score: sScore });
      const q = clamp((-w.meanRel - 6) / 24, 0, 1), texture = 0.5 * Math.min(1, w.flat / 0.3) + 0.5 * Math.min(1, w.flux / 2);
      const qScore = 2 * q + texture - 1.0 * w.big - 0.1 * w.std;
      if (w.meanRel < -9 && w.meanRel > -55 && w.sil < 0.2 && w.big === 0) quiet.push({ c, score: qScore });
    }
    steady.sort((a, b) => b.score - a.score || a.c - b.c);
    for (const s of steady) {
      const r = { s: s.c * tc, e: Math.min(dur, (s.c + K) * tc) };
      if (overlapOk(r, chosen)) { take('steady', r.s, r.e, 'Steady note ' + fmtTime(r.s)); break; }
    }

    // ---- loop: one bar at the tempo found from the onset autocorrelation (120 bpm if none is clear)
    if (bar <= dur) {
      const scored = loopScored;
      const label = 'One-bar loop ' + (Math.round(tempo.bpm * 10) / 10) + ' bpm' + (tempo.found ? '' : ' (guess)');
      let done = false;
      for (const c of scored) {
        const t = refineOnset(c.s) - 0.005, r = place(t, bar);
        if (overlapOk(r, chosen)) { take('loop', r.s, r.e, label + ' ' + fmtTime(r.s), { bpm: tempo.bpm, tempoFound: tempo.found }); done = true; break; }
      }
      if (!done) {   // no usable onsets: a level, slowly changing bar
        const Kb = Math.max(1, Math.round(bar / tc)), alt = [];
        for (let c = 0; c + Kb <= nc; c += 2) { const w = win(c, Kb); alt.push({ c, score: -w.flux - 0.2 * w.std - 3 * w.sil + 0.05 * w.meanRel }); }
        alt.sort((a, b) => b.score - a.score || a.c - b.c);
        for (const a of alt) {
          const r = place(a.c * tc, bar);
          if (overlapOk(r, chosen)) { take('loop', r.s, r.e, label + ' ' + fmtTime(r.s), { bpm: tempo.bpm, tempoFound: tempo.found }); break; }
        }
      }
    }

    // ---- quiet: a quiet, textured stretch (not silence)
    quiet.sort((a, b) => b.score - a.score || a.c - b.c);
    for (const q of quiet) {
      const r = { s: q.c * tc, e: Math.min(dur, (q.c + K) * tc) };
      if (overlapOk(r, chosen)) { take('quiet', r.s, r.e, 'Quiet texture ' + fmtTime(r.s)); break; }
    }

    const order = { steady: 0, hit: 1, quiet: 2, loop: 3 };
    parts.sort((a, b) => order[a.kind] - order[b.kind]);
    if (!wantDebug) return parts;
    return out(parts, {
      steady: steady.map((x) => ({ s: x.c * tc, e: (x.c + K) * tc, score: x.score })),
      quiet: quiet.map((x) => ({ s: x.c * tc, e: (x.c + K) * tc, score: x.score })),
      hit: hitCands.map((x) => ({ t: x.i * tf, score: x.score })),
      tempo, Lref, loop: loopScored.slice(0, 12).map((x) => Object.assign({}, x, { t: x.s * tf })),
    });
  }

  // Tempo from the autocorrelation of the onset curve, 60-200 bpm, weighted towards 120; refined on the bar lag.
  function estimateTempo(on, tf) {
    const n = on.length;
    let mean = 0, strong = 0;
    for (let i = 0; i < n; i++) { mean += on[i] / n; if (on[i] >= 6) strong++; }
    const o = new Float64Array(n);
    let e0 = 0;
    for (let i = 0; i < n; i++) { o[i] = on[i] - mean; e0 += o[i] * o[i]; }
    const fallback = { bpm: 120, found: false };
    if (strong < 8 || !(e0 > 0)) return fallback;
    const ac = (lag) => { let s = 0; for (let i = 0; i + lag < n; i++) s += o[i] * o[i + lag]; return s / e0; };
    const lagMin = Math.floor(60 / 200 / tf), lagMax = Math.min(n - 2, Math.ceil(60 / 60 / tf));
    if (lagMax <= lagMin + 2) return fallback;
    const r = new Float64Array(lagMax + 2);
    for (let l = lagMin - 1; l <= lagMax + 1; l++) r[l] = ac(l);
    let best = -1, bestW = -Infinity;
    for (let l = lagMin; l <= lagMax; l++) {
      if (!(r[l] >= r[l - 1] && r[l] >= r[l + 1])) continue;
      const bpm = 60 / (l * tf), w = r[l] * Math.exp(-0.5 * Math.pow(Math.log2(bpm / 120) / 0.7, 2));
      if (w > bestW) { bestW = w; best = l; }
    }
    if (best < 0 || r[best] < 0.08) return fallback;
    const interp = (l, a, b, c) => { const den = a - 2 * b + c; return l + (den ? 0.5 * (a - c) / den : 0); };
    let lag = interp(best, r[best - 1], r[best], r[best + 1]);
    // refine on the lag of 4 beats (one bar) when the file is long enough
    const l4 = Math.round(4 * lag);
    if (l4 + 6 < n / 2) {
      let bl = l4, bv = -Infinity;
      for (let l = l4 - 4; l <= l4 + 4; l++) { const v = ac(l); if (v > bv) { bv = v; bl = l; } }
      if (bv > 0.5 * r[best]) lag = interp(bl, ac(bl - 1), bv, ac(bl + 1)) / 4;
    }
    let bpm = 60 / (lag * tf);
    while (bpm < 60) bpm *= 2;
    while (bpm > 200) bpm /= 2;
    if (Math.abs(bpm - Math.round(bpm)) < 0.25) bpm = Math.round(bpm);
    return { bpm: Math.round(bpm * 100) / 100, found: true };
  }

  return { detectPitch, detectKey, suggestParts, _estimateTempo: estimateTempo, _features: features };
})();
if (typeof module !== 'undefined') module.exports = SLAnalysis;
