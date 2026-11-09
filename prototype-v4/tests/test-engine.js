// tests/test-engine.js: node tests for src/sl-engine.js (BLUEPRINT sections 1, 3 and 6). Owner: engine builder.
// One line per check: PASS/FAIL name: measured vs limit. Exits non-zero on any FAIL.
// Synthetic material only (harmonic tones, noise, chords, a position-encoding ramp). Cubase naming: MIDI 60 = C3.
'use strict';
const path = require('path');
const fs = require('fs');
const ENGINE_FILE = path.join(__dirname, '..', 'src', 'sl-engine.js');
const engMod = require(ENGINE_FILE);
const { createStrandEngineModule, strandWorkletGlue } = engMod;

// The page ships createStrandEngineModule.toString() into a worklet, so the tests use exactly that text,
// evaluated where no outside name exists. If the module referenced anything outside itself, this would throw.
const SE = new Function('"use strict";\n' + createStrandEngineModule.toString() + '\nreturn createStrandEngineModule();')();

const ONLY = process.argv.slice(2);
let passed = 0, failed = 0;
const fails = [];
function check(name, ok, measured, limit) {
  const line = (ok ? 'PASS ' : 'FAIL ') + name + ': ' + measured + ' vs ' + limit;
  console.log(line);
  if (ok) passed++; else { failed++; fails.push(line); }
}
function info(s) { console.log('  .. ' + s); }
function section(name, fn) {
  if (ONLY.length && !ONLY.some(o => name.toLowerCase().includes(o.toLowerCase()))) return;
  console.log('\n== ' + name);
  const t0 = Date.now();
  try { fn(); } catch (e) { check(name + ' (ran without throwing)', false, String(e && e.stack || e).split('\n').slice(0, 4).join(' | '), 'no exception'); }
  info('section time ' + ((Date.now() - t0) / 1000).toFixed(2) + ' s');
}
const f2 = (x, d = 2) => (x === undefined || x === null || Number.isNaN(x) ? String(x) : Number(x).toFixed(d));
const centsOf = (f, ref) => 1200 * Math.log2(f / ref);
const midiHz = m => 440 * Math.pow(2, (m - 69) / 12);
function stats(arr) {
  const a = Array.from(arr).filter(Number.isFinite).sort((x, y) => x - y);
  if (!a.length) return { n: 0, min: NaN, med: NaN, max: NaN, mean: NaN };
  const q = p => a[Math.min(a.length - 1, Math.floor(p * (a.length - 1)))];
  return { n: a.length, min: a[0], p10: q(0.1), med: q(0.5), p90: q(0.9), max: a[a.length - 1], mean: a.reduce((s, x) => s + x, 0) / a.length };
}
const fmtStats = (s, d = 2) => `n=${s.n} min ${f2(s.min, d)} p10 ${f2(s.p10, d)} med ${f2(s.med, d)} p90 ${f2(s.p90, d)} max ${f2(s.max, d)}`;

// ------------------------------------------------------------------ seeded test material
function lcg(seed) { let s = seed >>> 0 || 1; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }
function tone(f0, secs, sr, opts = {}) {
  const n = Math.round(secs * sr), K = opts.partials || 12, amp = opts.amp || 0.5, r = lcg(opts.seed || 7);
  const x = new Float32Array(n), ph = [], a = [];
  let norm = 0;
  for (let k = 1; k <= K; k++) { ph.push(r() * 2 * Math.PI); a.push(1 / k); norm += 1 / k; }
  for (let k = 1; k <= K; k++) {
    if (f0 * k >= sr * 0.45) break;
    const w = 2 * Math.PI * f0 * k / sr, ak = a[k - 1] / norm * 2;
    for (let i = 0; i < n; i++) x[i] += ak * Math.sin(w * i + ph[k - 1]);
  }
  let pk = 0; for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(x[i]));
  for (let i = 0; i < n; i++) x[i] *= amp / pk;
  return x;
}
function noise(secs, sr, seed = 3, amp = 0.5) {
  const n = Math.round(secs * sr), r = lcg(seed), x = new Float32Array(n);
  for (let i = 0; i < n; i++) x[i] = (r() * 2 - 1) * amp;
  return x;
}
// harmonic tone whose partials follow a formant peak at fc (Gaussian width bw Hz) over a weak 1/k floor
function formantTone(f0, secs, sr, fc, bw) {
  const n = Math.round(secs * sr), x = new Float32Array(n), r = lcg(17);
  for (let k = 1; k * f0 < sr * 0.4 && k <= 40; k++) {
    const a = Math.exp(-Math.pow((k * f0 - fc) / bw, 2)) + 0.05 / k, w = 2 * Math.PI * f0 * k / sr, ph = r() * 2 * Math.PI;
    for (let i = 0; i < n; i++) x[i] += a * Math.sin(w * i + ph);
  }
  let pk = 0; for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(x[i]));
  for (let i = 0; i < n; i++) x[i] *= 0.5 / pk;
  return x;
}
// noise low-passed by two 4-tap moving averages (first null 12 kHz at 48 kHz)
function smoothNoise(secs, sr, seed) {
  const a = noise(secs, sr, seed, 1), b = new Float32Array(a.length), c = new Float32Array(a.length);
  for (let i = 3; i < a.length; i++) b[i] = (a[i] + a[i - 1] + a[i - 2] + a[i - 3]) / 4;
  for (let i = 3; i < a.length; i++) c[i] = (b[i] + b[i - 1] + b[i - 2] + b[i - 3]) / 4;
  let pk = 0; for (let i = 0; i < c.length; i++) pk = Math.max(pk, Math.abs(c[i]));
  for (let i = 0; i < c.length; i++) c[i] *= 0.5 / pk;
  return c;
}
function chord(midis, secs, sr) {
  const n = Math.round(secs * sr), x = new Float32Array(n);
  midis.forEach((m, j) => { const t = tone(midiHz(m), secs, sr, { partials: 8, seed: 11 + j, amp: 1 }); for (let i = 0; i < n; i++) x[i] += t[i]; });
  let pk = 0; for (let i = 0; i < n; i++) pk = Math.max(pk, Math.abs(x[i]));
  for (let i = 0; i < n; i++) x[i] *= 0.5 / pk;
  return x;
}
// Ramp: left channel encodes the read position, right is constant. pos = (L/R*0.5 - 0.05)/0.8 cancels every gain
// that acts on both channels alike (amp, envelope, grain overlap ripple, filters at DC).
function rampSource(secs, sr) {
  const n = Math.round(secs * sr), L = new Float32Array(n), R = new Float32Array(n);
  for (let i = 0; i < n; i++) { L[i] = 0.05 + 0.8 * i / n; R[i] = 0.5; }
  return { channels: [L, R], sampleRate: sr, name: 'ramp' };
}
const decode = (l, r) => (Math.abs(r) > 1e-6 ? (l / r * 0.5 - 0.05) / 0.8 : NaN);
function decodeAt(out, i, w = 16) {   // average over a few samples (grain ripple cancels in the ratio anyway)
  let a = 0, b = 0;
  for (let j = Math.max(0, i - w); j <= Math.min(out.L.length - 1, i + w); j++) { a += out.L[j]; b += out.R[j]; }
  return decode(a, b);
}

// ------------------------------------------------------------------ engine helpers
const SR = 48000;
function mkInst(patch) {
  const I = SE.defaultInst();
  for (const k of Object.keys(patch || {})) {
    const v = patch[k];
    if (v && typeof v === 'object' && !Array.isArray(v) && I[k] && typeof I[k] === 'object') I[k] = Object.assign({}, I[k], v);
    else I[k] = v;
  }
  return I;
}
function mkEngine(srcOrChannels, instPatch, sr = SR) {
  const e = new SE.Engine({ sampleRate: sr });
  e.setInst(mkInst(instPatch));
  if (srcOrChannels) {
    if (srcOrChannels.channels) e.setSample(srcOrChannels);
    else e.setSample({ channels: Array.isArray(srcOrChannels) ? srcOrChannels : [srcOrChannels], sampleRate: sr, name: 'test' });
  }
  return e;
}
function render(e, n, block = 128, hook) {
  const L = new Float32Array(n), R = new Float32Array(n);
  let i = 0;
  const bl = new Float32Array(Math.max(block, 1)), br = new Float32Array(Math.max(block, 1));
  while (i < n) {
    const m = Math.min(typeof block === 'function' ? block() : block, n - i);
    const tl = m <= bl.length ? bl : new Float32Array(m), tr = m <= br.length ? br : new Float32Array(m);
    e.process(tl, tr, m);
    L.set(tl.subarray(0, m), i); R.set(tr.subarray(0, m), i);
    i += m;
    if (hook) hook(i);
  }
  return { L, R };
}
// Minimal snapshot builder (dense lane tables, every setting resolved), independent of SLModel.
const LANE_IDS = SE.LANE_IDS, LANE_DEF = SE.LANE_DEF;
function mkNote(o) {
  const res = o.res || 256, l = o.l, len = Math.max(2, Math.ceil(l * res) + 1);
  const lanes = {};
  LANE_IDS.forEach((id, i) => {
    const t = new Float32Array(len), spec = o.lanes && o.lanes[id];
    for (let j = 0; j < len; j++) {
      const u = j / (len - 1);
      t[j] = spec === undefined ? LANE_DEF[i] : typeof spec === 'function' ? spec(u) : spec;
    }
    lanes[id] = t;
  });
  const id = o.id || ('n' + Math.round(o.p) + '_' + o.s);
  return {
    id, idHash: SE.idHash(id), p: o.p, s: o.s, l, vel: o.vel == null ? 1 : o.vel,
    engine: o.engine || 'voice', lock: !!o.lock, motion: o.motion || 'forward', travel: o.travel || 'speed',
    start: Object.assign({ mode: 'region', pos: 0, range: 0, seed: 0 }, o.start || {}),
    tape: !!o.tape, rev: !!o.rev, jump: o.jump || 0.5, restart: !!o.restart,
    glide: o.glide || null, chain: o.chain || { id, index: 0, prevId: null, nextId: null }, lanes,
  };
}
function mkSnap(notes, bars = 4) { return { version: 1, bars, beatsPerBar: 4, res: 256, notes }; }
function chainNotes(list) {   // link notes into one chain in order
  list.forEach((n, i) => { n.chain = { id: list[0].id, index: i, prevId: i ? list[i - 1].id : null, nextId: i < list.length - 1 ? list[i + 1].id : null }; });
  return list;
}
const beatSamples = (beat, bpm, sr = SR) => beat * 60 / bpm * sr;

// ------------------------------------------------------------------ spectrum and pitch
function fft(re, im) {   // in place, radix 2
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let j = 0; j < len / 2; j++) {
        const a = i + j, b = a + len / 2;
        const xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
        const ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
      }
    }
  }
}
// Blackman-Harris magnitude spectrum of x[start .. start + 2^order)
function spectrum(x, start, order) {
  const n = 1 << order, re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const t = 2 * Math.PI * i / (n - 1);
    const w = 0.35875 - 0.48829 * Math.cos(t) + 0.14128 * Math.cos(2 * t) - 0.01168 * Math.cos(3 * t);
    re[i] = (x[start + i] || 0) * w;
  }
  fft(re, im);
  const m = new Float64Array(n / 2);
  for (let k = 0; k < n / 2; k++) m[k] = Math.hypot(re[k], im[k]);
  return m;
}
// parabolic peak interpolation in dB around bin k
function interpPeak(m, k) {
  const a = 20 * Math.log10(Math.max(1e-20, m[k - 1])), b = 20 * Math.log10(Math.max(1e-20, m[k])), c = 20 * Math.log10(Math.max(1e-20, m[k + 1]));
  const den = a - 2 * b + c;
  return k + (Math.abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0);
}
// GrainLock's FFT fundamental: lowest spectral peak within 40 dB of the strongest, parabolic interpolation.
function fftFundamental(x, sr, order, fLo = 20, fHi = 8000, start) {
  const n = 1 << order, st = start == null ? x.length - n : start, m = spectrum(x, st, order), bin = sr / n;
  const lo = Math.max(2, Math.ceil(fLo / bin)), hi = Math.min(n / 2 - 2, Math.floor(fHi / bin));
  let best = 0; for (let k = lo; k <= hi; k++) best = Math.max(best, m[k]);
  for (let k = lo + 1; k < hi; k++) if (m[k] >= best * 0.01 && m[k] >= m[k - 1] && m[k] >= m[k + 1]) return interpPeak(m, k) * bin;
  return 0;
}
// Harmonic-refined f0: every harmonic k*f within +-60 cents of k*fExp and within 40 dB of the strongest gives
// f_k/k; the magnitude-weighted mean is the estimate (sub-cent with long windows). Also returns the per-harmonic spread.
function harmonicF0(x, sr, fExp, order = 16, start, maxK = 12) {
  const n = 1 << order, st = start == null ? x.length - n : start, m = spectrum(x, st, order), bin = sr / n;
  let strongest = 0; const cands = [];
  for (let k = 1; k <= maxK; k++) {
    const fc = fExp * k; if (fc > sr * 0.45) break;
    const lo = Math.max(2, Math.floor(fc * Math.pow(2, -0.6 / 12) / bin)), hi = Math.min(n / 2 - 2, Math.ceil(fc * Math.pow(2, 0.6 / 12) / bin));
    let bk = -1; for (let q = lo; q <= hi; q++) if (bk < 0 || m[q] > m[bk]) bk = q;
    if (bk <= lo || bk >= hi) continue;   // a true peak must be inside the window
    cands.push({ k, f: interpPeak(m, bk) * bin, mag: m[bk] }); strongest = Math.max(strongest, m[bk]);
  }
  let sw = 0, sf = 0; const each = [];
  for (const c of cands) if (c.mag >= strongest * 0.01) { sw += c.mag; sf += c.mag * c.f / c.k; each.push(c.f / c.k); }
  return { f0: sw > 0 ? sf / sw : 0, used: each.length, spreadCents: each.length > 1 ? 1200 * Math.log2(Math.max(...each) / Math.min(...each)) : 0 };
}
// GrainLock's autocorrelation fundamental (shortest lag within 90% of the best, parabolic interpolation).
function autocorrFundamental(x, sr, fMin, fMax, length = 16384) {
  const minLag = Math.floor(sr / fMax), maxLag = Math.ceil(sr / fMin), start = x.length - (length + maxLag + 2);
  const r = new Float64Array(maxLag + 2);
  for (let lag = minLag - 1; lag <= maxLag + 1; lag++) {
    let xy = 0, xx = 0, yy = 0;
    for (let i = 0; i < length; i++) { const a = x[start + i], b = x[start + i + lag]; xy += a * b; xx += a * a; yy += b * b; }
    r[lag] = xy / Math.sqrt(Math.max(1e-30, xx * yy));
  }
  let best = -1; for (let lag = minLag; lag <= maxLag; lag++) best = Math.max(best, r[lag]);
  for (let lag = minLag; lag <= maxLag; lag++) {
    const v = r[lag];
    if (v >= 0.9 * best && v >= r[lag - 1] && v >= r[lag + 1]) {
      const a = r[lag - 1], c = r[lag + 1], den = a - 2 * v + c;
      return sr / (lag + (Math.abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0));
    }
  }
  return 0;
}
// Period estimator for signals that repeat exactly (Lock): minimise the mean squared difference between x(t) and
// x(t + tau) over fractional tau (Hermite-interpolated), golden-section search around the expected period.
function hermiteAt(x, p) { const i = Math.floor(p), f = p - i, a = x[i - 1] || 0, b = x[i] || 0, c = x[i + 1] || 0, d = x[i + 2] || 0;
  return (((0.5 * (d - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * d)) * f + 0.5 * (c - a)) * f + b; }
function periodF0(x, sr, fExp, length = 32768) {
  const P0 = sr / fExp, start = x.length - length - Math.ceil(P0 * 1.1) - 4;
  const d = tau => { let s = 0; for (let t = 0; t < length; t++) { const e = x[start + t] - hermiteAt(x, start + t + tau); s += e * e; } return s; };
  let bestT = P0, bestD = Infinity;   // coarse scan +-3% in 0.25-sample steps, then golden section
  for (let tau = P0 * 0.97; tau <= P0 * 1.03; tau += 0.25) { const v = d(tau); if (v < bestD) { bestD = v; bestT = tau; } }
  let a = bestT - 0.3, b = bestT + 0.3; const g = (Math.sqrt(5) - 1) / 2;
  let c = b - g * (b - a), e = a + g * (b - a), dc = d(c), de = d(e);
  for (let it = 0; it < 40; it++) { if (dc < de) { b = e; e = c; de = dc; c = b - g * (b - a); dc = d(c); } else { a = c; c = e; dc = de; e = a + g * (b - a); de = d(e); } }
  return sr / ((a + b) / 2);
}
function centroid(x, sr, order = 15, start, fMax = 12000) {
  const n = 1 << order, st = start == null ? x.length - n : start, m = spectrum(x, st, order), bin = sr / n;
  let a = 0, b = 0; for (let k = 1; k < Math.min(n / 2, fMax / bin); k++) { a += k * bin * m[k] * m[k]; b += m[k] * m[k]; }
  return b > 0 ? a / b : 0;
}
function bandEnergy(x, sr, f1, f2, order = 15, start) {
  const n = 1 << order, st = start == null ? x.length - n : start, m = spectrum(x, st, order), bin = sr / n;
  let e = 0; for (let k = Math.max(1, Math.floor(f1 / bin)); k <= Math.min(n / 2 - 1, Math.ceil(f2 / bin)); k++) e += m[k] * m[k];
  return e;
}
function rms(x, a = 0, b = x.length) { let s = 0; for (let i = a; i < b; i++) s += x[i] * x[i]; return Math.sqrt(s / Math.max(1, b - a)); }
function peakAbs(x, a = 0, b = x.length) { let p = 0; for (let i = a; i < b; i++) p = Math.max(p, Math.abs(x[i])); return p; }
function maxStep(x, a, b) { let p = 0; for (let i = Math.max(1, a); i < Math.min(x.length, b); i++) p = Math.max(p, Math.abs(x[i] - x[i - 1])); return p; }
function corr(a, b) { let ab = 0, aa = 0, bb = 0; for (let i = 0; i < a.length; i++) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; } return ab / Math.sqrt(Math.max(1e-30, aa * bb)); }
function identical(a, b) { if (a.length !== b.length) return { same: false, diff: Infinity, at: -1 }; let d = 0, at = -1; for (let i = 0; i < a.length; i++) { const q = Math.abs(a[i] - b[i]); if (q > d) { d = q; at = i; } } return { same: d === 0, diff: d, at }; }

// ================================================================== tests
section('module shape and shared definitions', () => {
  check('module exports { createStrandEngineModule, strandWorkletGlue }', typeof createStrandEngineModule === 'function' && typeof strandWorkletGlue === 'function',
    Object.keys(engMod).join(','), 'both functions');
  check('createStrandEngineModule.toString() runs with no outside names', typeof SE.Engine === 'function' && typeof SE.rateBeats === 'function' && typeof SE.version === 'string',
    'version ' + SE.version, 'Engine, rateBeats, version');
  const src = createStrandEngineModule.toString().replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
  const bad = ['Math.random', 'SLModel', 'window.', 'require(', 'document.'].filter(w => src.includes(w));
  check('engine source uses no Math.random / SLModel / window / require / document', bad.length === 0, bad.join(',') || 'none', 'none');
  const ids = ['1bar', '1/2', '1/4', '1/8', '1/16', '1/32', '1/64'];
  const want = [4, 2, 1, 0.5, 0.25, 0.125, 0.0625];
  let worst = 0;
  ids.forEach((id, i) => { worst = Math.max(worst, Math.abs(SE.rateBeats(id) - want[i]), Math.abs(SE.rateBeats(id + 'T') - want[i] * 2 / 3), Math.abs(SE.rateBeats(id + 'D') - want[i] * 1.5)); });
  check('rateBeats table (straight, T x2/3, D x1.5)', worst < 1e-12, 'max error ' + worst, '< 1e-12');
  const ts = [[0.5, 0], [0.48, 0], [0.52, 0], [0.75, 1], [0.98, 8], [0.25, -1], [0.02, -8], [0.53, Math.pow(2, ((0.53 - 0.75) / 0.23) * 3)], [1, 8]];
  const terr = Math.max(...ts.map(([v, s]) => Math.abs(SE.travelSpeed(v) - s)));
  check('travel speed s(v): dead band, 1x at 0.75, 8x at 0.98, -1x at 0.25, -8x at 0.02', terr < 1e-9, 'max error ' + terr.toExponential(2), '< 1e-9');
  const modelFile = path.join(__dirname, '..', 'src', 'sl-model.js');
  if (fs.existsSync(modelFile)) {
    let SLModel = null;
    try { SLModel = require(modelFile); } catch (e) { info('sl-model.js present but failed to load: ' + e.message); }
    if (SLModel && SLModel.hash32) {
      let mism = 0;
      for (let i = 0; i < 200; i++) {
        const a = [i, i * 7919 - 3, -i, 0x5ca7];
        if (SLModel.hash32(...a) !== SE.hash32(...a)) mism++;
        if (SLModel.idHash('n' + i + 'x') !== SE.idHash('n' + i + 'x')) mism++;
      }
      check('hash32 / idHash identical to SLModel (400 cases)', mism === 0, mism + ' mismatches', '0');
      if (SLModel.RATES) {
        const rm = SLModel.RATES.filter(r => SLModel.rateBeats(r) !== SE.rateBeats(r));
        check('rateBeats identical to SLModel for every rate id', rm.length === 0, rm.length + ' mismatches of ' + SLModel.RATES.length, '0');
      }
      if (SLModel.travelSpeed) {
        let d = 0; for (let v = 0; v <= 1.0001; v += 0.01) d = Math.max(d, Math.abs(SLModel.travelSpeed(v) - SE.travelSpeed(v)));
        check('travelSpeed identical to SLModel', d === 0, 'max diff ' + d, '0');
      }
    }
  } else info('sl-model.js not present: model cross-checks skipped');
});

section('tuning: Voice and Tape at C1, C3, C5 against a C3 root (+-3 cents)', () => {
  // source at 44.1 kHz, engine at 48 kHz: the sample-rate ratio is part of the test
  const srcSr = 44100, src = tone(midiHz(60), 6, srcSr, { partials: 12 });
  const rows = [];
  for (const eng of ['voice', 'tape', 'cloud']) {
    for (const p of [36, 60, 84]) {
      const e = mkEngine({ channels: [src], sampleRate: srcSr }, { engine: eng, root: 60 });
      e.noteOn(p, 1);
      const out = render(e, 2.0 * SR);
      const fExp = midiHz(p);
      const h = harmonicF0(out.L, SR, fExp, 16);
      const fa = fftFundamental(out.L, SR, 16, fExp * 0.7, fExp * 1.4 > 100 ? 8000 : 8000);
      const c = centsOf(h.f0, fExp), ca = fa ? centsOf(fa, fExp) : NaN;
      const name = ['C1', 'C3', 'C5'][[36, 60, 84].indexOf(p)];
      const ac = autocorrFundamental(out.L, SR, fExp * 0.8, fExp * 1.25);
      const detail = `${f2(c, 3)} cents (harmonic FFT, ${h.used} harmonics, spread ${f2(h.spreadCents, 2)} c; lowest-peak FFT ${f2(ca, 2)} c; autocorr ${f2(centsOf(ac, fExp), 2)} c)`;
      if (eng === 'cloud') { info(`cloud ${name} f0 (reported, not a blueprint gate: plain Cloud grains are not phase-aligned): ${detail}`); continue; }
      rows.push(c);
      check(`${eng} ${name} f0`, Math.abs(c) <= 3, detail, '+-3 cents');
    }
  }
  info('tuning error distribution (cents): ' + fmtStats(stats(rows), 3));
});

section('Cloud + Lock: in tune from noise and from a chord (+-3 cents)', () => {
  const nz = noise(6, SR, 5), ch = chord([60, 64, 67], 6, SR);
  const errs = [], errsA = [], errsF = [];
  // calibrate the estimators on a signal that repeats exactly at a known fractional period (one random cycle, looped)
  for (const p of [48, 60, 72]) {
    const fE = midiHz(p), P = SR / fE, cyc = noise(P / SR + 0.01, SR, 21), y = new Float32Array(2 * SR);
    for (let i = 0; i < y.length; i++) y[i] = hermiteAt(cyc, 1 + ((i % P) + P) % P);
    info(`estimator calibration at ${p}: period ${f2(centsOf(periodF0(y, SR, fE), fE), 3)} c, autocorr ${f2(centsOf(autocorrFundamental(y, SR, fE * 0.8, fE * 1.25), fE), 3)} c`);
  }
  for (const [name, src] of [['noise', nz], ['chord C3-E3-G3', ch]]) {
    for (const p of [48, 60, 72]) {
      for (const refresh of [{ mode: 'smooth' }, { mode: 'free', ms: 120 }]) {
        const e = mkEngine(src, { engine: 'cloud', lock: true, refresh });
        e.noteOn(p, 1);
        const out = render(e, 2.0 * SR);
        const fExp = midiHz(p), h = harmonicF0(out.L, SR, fExp, 16, undefined, 8);
        const ac = autocorrFundamental(out.L, SR, fExp * 0.8, fExp * 1.25);
        const c = centsOf(periodF0(out.L, SR, fExp), fExp), ca = centsOf(ac, fExp), cf = centsOf(h.f0, fExp);
        errs.push(c); errsA.push(ca); errsF.push(cf);
        check(`lock ${name} ${['C2', 'C3', 'C4'][[48, 60, 72].indexOf(p)]} refresh ${refresh.mode}${refresh.ms ? ' ' + refresh.ms + ' ms' : ''}`, Math.abs(c) <= 3,
          `period ${f2(c, 3)} cents (autocorr ${f2(ca, 2)} c; harmonic FFT ${f2(cf, 2)} c on speckled lines)`, '+-3 cents');
      }
    }
  }
  info('Lock cents distribution (period): ' + fmtStats(stats(errs), 3));
  info('Lock cents distribution (autocorr): ' + fmtStats(stats(errsA), 3));
  info('Lock cents distribution (harmonic FFT, for reference): ' + fmtStats(stats(errsF), 3));
  // a no-op guard: plain Cloud (no Lock) on noise has no clear pitch at the played note
  const e = mkEngine(nz, { engine: 'cloud', lock: false });
  e.noteOn(60, 1);
  const out = render(e, 2.0 * SR);
  const harmonicity = x => {   // energy within +-1.5% of k*f vs energy in the gaps (0.3..0.7 of the way to the next harmonic), k = 1..8
    const m = spectrum(x, x.length - 65536, 16), bin = SR / 65536, f = midiHz(60);
    let on = 0, offE = 0, cOn = 0, cOff = 0;
    for (let k = 1; k <= 8; k++) {
      for (let q = Math.floor(k * f * 0.985 / bin); q <= Math.ceil(k * f * 1.015 / bin); q++) { on += m[q] * m[q]; cOn++; }
      for (let q = Math.floor((k + 0.3) * f / bin); q <= Math.ceil((k + 0.7) * f / bin); q++) { offE += m[q] * m[q]; cOff++; }
    }
    return 10 * Math.log10((on / cOn) / (offE / cOff));
  };
  const lineVsFloor = harmonicity(out.L);
  const hs = [{ mode: 'smooth' }, { mode: 'free', ms: 120 }, { mode: 'free', ms: 400 }].map(refresh => {
    const e2 = mkEngine(nz, { engine: 'cloud', lock: true, refresh }); e2.noteOn(60, 1);
    return harmonicity(render(e2, 2.0 * SR).L);
  });
  check('Lock makes a pitch from noise (harmonic lines over the gaps; worst of smooth / 120 ms / 400 ms refresh)', Math.min(...hs) > lineVsFloor + 10,
    `Lock ${hs.map(h => f2(h, 1)).join(' / ')} dB, plain Cloud ${f2(lineVsFloor, 1)} dB`, 'Lock >= plain Cloud + 10 dB');
});

section('Voice formant: +6 st raises the centroid > 25% with f0 in tune; -6 lowers it', () => {
  const src = formantTone(midiHz(60), 6, SR, 1000, 350);
  const res = {};
  for (const [label, off] of [['0', 0], ['+6', 0.25], ['-6', -0.25]]) {
    const e = mkEngine(src, { engine: 'voice', offsets: { formant: off } });
    e.noteOn(60, 1);
    const out = render(e, 2.0 * SR);
    const h = harmonicF0(out.L, SR, midiHz(60), 16);
    res[label] = { c: centroid(out.L, SR, 15), cents: centsOf(h.f0, midiHz(60)) };
  }
  const up = res['+6'].c / res['0'].c - 1, dn = res['-6'].c / res['0'].c - 1;
  check('formant +6 st centroid rise', up > 0.25, `${f2(up * 100, 1)} % (${f2(res['0'].c, 0)} -> ${f2(res['+6'].c, 0)} Hz)`, '> +25 %');
  check('formant +6 st f0', Math.abs(res['+6'].cents) <= 3, f2(res['+6'].cents, 3) + ' cents', '+-3 cents');
  check('formant -6 st centroid falls', dn < -0.1, `${f2(dn * 100, 1)} % (${f2(res['-6'].c, 0)} Hz)`, '< -10 %');
  check('formant -6 st f0', Math.abs(res['-6'].cents) <= 3, f2(res['-6'].cents, 3) + ' cents', '+-3 cents');
});

section('legato glide C3 -> G3 on one voice (lands within +-5 cents, no new attack)', () => {
  const src = tone(midiHz(60), 6, SR, { partials: 12 });
  for (const eng of ['voice', 'tape']) {
    const A = mkNote({ id: 'A', p: 60, s: 0, l: 2, engine: eng, glide: { d: 7, len: 0.5, curve: 0, toId: 'B' } });
    const B = mkNote({ id: 'B', p: 67, s: 2, l: 3, engine: eng });
    chainNotes([A, B]);
    const e = mkEngine(src, { engine: eng });
    e.setSnapshot(mkSnap([A, B]));
    e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
    let maxVoices = 0, minEnv = 1;
    const kB = beatSamples(2, 120);
    const out = render(e, Math.round(beatSamples(4.9, 120)), 64, i => {
      const live = e.voices.filter(v => !v.done);
      maxVoices = Math.max(maxVoices, live.length);
      if (i > kB - 4800 && i < kB + 4800 && live[0]) minEnv = Math.min(minEnv, live[0].env);
    });
    const hA = harmonicF0(out.L, SR, midiHz(60), 15, 4800);
    const hB = harmonicF0(out.L, SR, midiHz(67), 15, Math.round(beatSamples(2.6, 120)));
    const cA = centsOf(hA.f0, midiHz(60)), cB = centsOf(hB.f0, midiHz(67));
    check(`${eng}: note A steady at C3`, Math.abs(cA) <= 3, f2(cA, 3) + ' cents', '+-3 cents');
    check(`${eng}: glide lands on G3`, Math.abs(cB) <= 5, f2(cB, 3) + ' cents', '+-5 cents');
    check(`${eng}: the chain plays on one voice`, maxVoices === 1, 'max voices ' + maxVoices, '1');
    check(`${eng}: no new attack at the hand-over (envelope stays up)`, minEnv > 0.999, 'min envelope ' + f2(minEnv, 4), '> 0.999');
  }
});

section('chain carries the playhead (no restart); unchained notes restart', () => {
  const ramp = rampSource(4, SR), n = ramp.channels[0].length;
  for (const eng of ['tape', 'voice']) {
    const run = chained => {
      const A = mkNote({ id: 'A', p: 60, s: 0, l: 1, engine: eng, glide: chained ? { d: 0, len: 0, curve: 0, toId: 'B' } : null });
      const B = mkNote({ id: 'B', p: 60, s: 1, l: 1, engine: eng });
      if (chained) chainNotes([A, B]);
      const e = mkEngine(ramp, { engine: eng });
      e.setSnapshot(mkSnap([A, B]));
      e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
      const out = render(e, Math.round(beatSamples(1.6, 120)));
      const k = Math.round(beatSamples(1, 120));
      return { before: decodeAt(out, k - 400), after: decodeAt(out, k + 4000) };
    };
    const c = run(true), u = run(false);
    const expected = 4400 / n;   // 4400 frames of travel at 1x between the two probes
    check(`${eng}: chained B continues A's playhead`, Math.abs((c.after - c.before) - expected) < 0.002,
      `moved ${f2(c.after - c.before, 5)} (before ${f2(c.before, 4)}, after ${f2(c.after, 4)})`, `${f2(expected, 5)} +- 0.002`);
    check(`${eng}: unchained B restarts at the Region start (proves the probe sees restarts)`, u.after < 0.03 && u.before > 0.1,
      `before ${f2(u.before, 4)} after ${f2(u.after, 4)} (restart from 0 lands at ${f2(4000 / n, 4)})`, 'after < 0.03');
  }
});

section('motions: Backward, Freeze, Bounce, Mirror', () => {
  // Backward (Tape at the root reads 1:1): output correlates with the reversed source
  const nz = smoothNoise(4, SR, 9), n = nz.length;
  const corrRev = motion => {
    const N = mkNote({ id: 'b', p: 60, s: 0, l: 3, engine: 'tape', motion, start: { pos: 0.5 } });
    const e = mkEngine(nz, { engine: 'tape' });
    e.setSnapshot(mkSnap([N]));
    e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
    const out = render(e, SR);
    const k0 = 4800, len = 24000, a = out.L.subarray(k0, k0 + len);
    let best = -1, bestLag = 0;
    for (let lag = -6; lag <= 6; lag++) {
      const b = new Float32Array(len);
      for (let j = 0; j < len; j++) b[j] = nz[Math.max(0, Math.round(n / 2) - k0 - j + lag)];
      const r = corr(a, b); if (r > best) { best = r; bestLag = lag; }
    }
    return { best, bestLag };
  };
  const bw = corrRev('backward'), fw = corrRev('forward');
  check('Backward output correlates with the reversed source', bw.best > 0.95, `r = ${f2(bw.best, 4)} at lag ${bw.bestLag}`, '> 0.95');
  check('Forward output does not (contrast)', Math.abs(fw.best) < 0.1, `r = ${f2(fw.best, 4)}`, '|r| < 0.1');
  // Backward in Voice: the decoded position falls at 1x
  const ramp = rampSource(4, SR), rn = ramp.channels[0].length;
  {
    const N = mkNote({ id: 'v', p: 60, s: 0, l: 3, engine: 'voice', motion: 'backward', start: { pos: 0.5 } });
    const e = mkEngine(ramp, { engine: 'voice' }); e.setSnapshot(mkSnap([N])); e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
    const out = render(e, SR);
    const d = decodeAt(out, 36000, 200) - decodeAt(out, 12000, 200), exp = -24000 / rn;
    check('Voice Backward: position falls at 1x', Math.abs(d - exp) < 0.003, f2(d, 5), f2(exp, 5) + ' +- 0.003');
  }
  // Freeze holds (Voice); Tape treats Freeze as Forward
  for (const eng of ['voice', 'cloud', 'tape']) {
    const e = mkEngine(ramp, { engine: eng, motion: 'freeze', grainCloud: 30 });
    e.noteOn(60, 1, { startPos: 0.5 });
    const out = render(e, 1.5 * SR);
    const probes = [0.2, 0.6, 1.0, 1.4].map(t => decodeAt(out, Math.round(t * SR), 200));
    const span = Math.max(...probes) - Math.min(...probes);
    if (eng === 'tape') check('Tape: Freeze plays as Forward (moves at 1x)', Math.abs((probes[3] - probes[0]) - 1.2 * SR / rn) < 0.003, f2(probes[3] - probes[0], 5), f2(1.2 * SR / rn, 5));
    else check(`${eng}: Freeze holds the playhead`, span < 0.001, 'position spread ' + span.toExponential(2) + ' at ' + probes.map(p => f2(p, 4)).join(', '), '< 0.001');
  }
  // Bounce folds at the Region edges
  for (const eng of ['voice', 'tape']) {
    const e = mkEngine(ramp, { engine: eng, motion: 'bounce', region: { start: 0.2, end: 0.3 } });
    e.noteOn(60, 1);
    const out = render(e, 2.0 * SR);
    const pos = []; for (let t = 0.05; t < 2.0; t += 0.01) pos.push(decodeAt(out, Math.round(t * SR), 60));
    let rev = 0, dir = 0;
    for (let i = 1; i < pos.length; i++) { const d = Math.sign(pos[i] - pos[i - 1]); if (d && dir && d !== dir) rev++; if (d) dir = d; }
    const lo = Math.min(...pos), hi = Math.max(...pos);
    check(`${eng}: Bounce stays inside the Region`, lo > 0.195 && hi < 0.305, `range ${f2(lo, 4)} .. ${f2(hi, 4)}`, '0.2 .. 0.3');
    check(`${eng}: Bounce turns at each end (0.4 s Region, 2 s)`, rev >= 4 && rev <= 6, rev + ' reversals', '4..6');
  }
  // Mirror: odd-numbered heads (2nd, 4th ...) move backwards
  {
    const e = mkEngine(ramp, { engine: 'voice', motion: 'mirror', heads: 4, spread: 0.5 });
    e.noteOn(60, 1);
    render(e, 0.3 * SR);
    const d0 = e.getDisplay().voices[0].heads.map(h => h.pos);
    render(e, 0.1 * SR);
    const d1 = e.getDisplay().voices[0].heads;
    const moves = d1.map((h, i) => h.pos - d0[i]);
    const ok = moves.length === 4 && moves[0] > 0 && moves[1] < 0 && moves[2] > 0 && moves[3] < 0 && d1[1].dir === -1 && d1[0].dir === 1;
    check('Mirror: heads 1,3 forward, heads 2,4 backward', ok, 'moves ' + moves.map(m => f2(m * rn / (0.1 * SR), 3) + 'x').join(' '), '+1 -1 +1 -1');
  }
});
section('Scatter: jumps land on the beat grid, renders repeat, Re-roll changes them', () => {
  const ramp = rampSource(6, SR), rn = ramp.channels[0].length, bpm = 87, jb = 0.5;
  const N = mkNote({ id: 'sc', p: 60, s: 0, l: 8, engine: 'tape', motion: 'scatter', jump: jb });
  const run = seed => {
    const e = mkEngine(ramp, { engine: 'tape', seed });
    e.setSnapshot(mkSnap([N]));
    e.setTransport({ playing: true, bpm, beat: 0, loop: false });
    const pos = [];
    const out = render(e, Math.round(beatSamples(7.9, bpm)), 32, () => { const v = e.getDisplay().voices[0]; pos.push(v ? v.heads[0].pos : NaN); });
    const G = e.voices[0] ? e.voices[0].reg : { Lp: rn, X: 0 };
    const jumps = [];
    for (let m = 1; m < pos.length; m++) {
      const d = pos[m] - pos[m - 1];
      if (Math.abs(d) > 0.005 && Math.abs(d + G.Lp / rn) > 0.01) jumps.push({ k: 32 * m, to: pos[m] });
    }
    return { out, jumps };
  };
  const a = run(1), b = run(1), c = run(2);
  const grid = []; for (let j = 1; j * jb < 7.9; j++) grid.push(beatSamples(j * jb, bpm));
  const errs = a.jumps.map(J => { let best = Infinity; for (const g of grid) { const d = J.k - g; if (Math.abs(d) < Math.abs(best)) best = d; } return best; });
  check('Scatter: one jump per grid line', a.jumps.length === grid.length, a.jumps.length + ' jumps', grid.length + ' grid lines (1/8 at 87 bpm)');
  const es = stats(errs);
  check('Scatter: jumps land on the grid (samples after the grid line)', es.min >= 0 && es.max < 32, fmtStats(es, 1), '0 <= err < 32 (one control block)');
  const id = identical(a.out.L, b.out.L);
  check('Scatter: two renders are identical', id.same, 'max diff ' + id.diff, '0');
  let moved = 0; a.jumps.forEach((J, i) => { if (c.jumps[i] && Math.abs(c.jumps[i].to - J.to) > 0.01) moved++; });
  check('Scatter: Re-roll (seed+1) moves the jump targets', moved >= Math.floor(a.jumps.length * 0.8), moved + ' of ' + a.jumps.length + ' moved', '>= 80 %');
  info('jump targets (seed 1): ' + fmtStats(stats(a.jumps.map(J => J.to)), 3));
  // the 5 ms crossfade keeps jumps click-free relative to the ramp's own content
  const steps = a.jumps.map(J => maxStep(a.out.L, J.k - 64, J.k + 400));
  info('Tape jump crossfade: largest sample step around a jump ' + f2(Math.max(...steps), 4) + ' (ramp jumps are up to ' + f2(0.8 * 0.832, 3) + ' without the fade)');
  check('Scatter jumps are crossfaded (largest step around a jump)', Math.max(...steps) < 0.05, f2(Math.max(...steps), 4), '< 0.05');
});

section('random block sizes 1..700 give bit-identical output (static settings)', () => {
  const srcSr = 44100, n = 4 * srcSr, Lc = new Float32Array(n), Rc = new Float32Array(n);
  const t1 = tone(midiHz(60), 4, srcSr), nz = noise(4, srcSr, 4, 0.2);
  for (let i = 0; i < n; i++) { Lc[i] = t1[i] + nz[i]; Rc[i] = t1[i] - nz[i]; }
  const notes = chainNotes([
    mkNote({ id: 'a1', p: 60, s: 0, l: 1.5, engine: 'voice', glide: { d: 7, len: 0.4, curve: 0.3, toId: 'a2' }, lanes: { vib: 0.3 } }),
    mkNote({ id: 'a2', p: 67, s: 1.5, l: 2, engine: 'voice' }),
  ]).concat([
    mkNote({ id: 'b', p: 72, s: 0.25, l: 3, engine: 'tape', motion: 'scatter', jump: 0.25 }),
    mkNote({ id: 'c', p: 48, s: 0.5, l: 2.5, engine: 'cloud', lock: true, lanes: { heads: u => 0.5 + 0.2 * u } }),
    mkNote({ id: 'd', p: 64, s: 1, l: 2.5, engine: 'cloud', rev: true, lanes: { travel: u => (Math.floor(u * 8) % 2 ? 0.25 : 0.75), grain: 0.3 } }),
    mkNote({ id: 'e', p: 55, s: 2, l: 1.9, engine: 'voice', motion: 'mirror', travel: 'path', lanes: { travel: u => u, heads: 0.64 } }),
  ]);
  const patch = { refresh: { mode: 'free', ms: 90, skip: 0.3 }, spray: 0.1, drift: 0.5, headTune: 'detune', spread: 0.4, heads: 2 };
  const go = block => {
    const e = mkEngine({ channels: [Lc, Rc], sampleRate: srcSr }, patch);
    e.setSnapshot(mkSnap(notes, 1));
    e.setTransport({ playing: true, bpm: 101, beat: 0, loop: true });
    e.noteOn(64, 0.9);
    return render(e, 3 * SR, block);
  };
  const ref = go(128);
  const r = lcg(99), rnd = go(() => 1 + Math.floor(r() * 700)), one = go(1), big = go(4096);
  for (const [name, o] of [['random 1..700', rnd], ['block 1', one], ['block 4096', big]]) {
    const dl = identical(ref.L, o.L), dr = identical(ref.R, o.R);
    check('block size ' + name + ' vs 128: bit-identical', dl.same && dr.same, `max diff L ${dl.diff} R ${dr.diff}`, '0');
  }
  check('the scenario is not silent (a no-op is not a pass)', rms(ref.L) > 0.01, 'rms ' + f2(rms(ref.L), 4), '> 0.01');
});

section('16 voices x 8 heads: no NaN/inf, peak <= 0.98, grain budget never exceeded', () => {
  const n = 4 * SR, Ls = new Float32Array(n), Rs = new Float32Array(n);
  const t1 = tone(midiHz(60), 4, SR, { amp: 0.6 }), nz = noise(4, SR, 6, 0.4);
  for (let i = 0; i < n; i++) { Ls[i] = t1[i] + nz[i]; Rs[i] = t1[i] - nz[i] * 0.5; }
  const rows = [];
  for (const [eng, lock, sr] of [['voice', false, SR], ['tape', false, SR], ['cloud', false, SR], ['cloud', true, SR], ['voice', false, 96000], ['cloud', true, 44100]]) {
    const notes = [];
    for (let i = 0; i < 16; i++) {
      notes.push(mkNote({ id: 'x' + i, p: 36 + ((i * 7) % 60), s: i * 0.05, l: 6, engine: eng, lock, motion: ['forward', 'mirror', 'scatter', 'bounce'][i % 4],
        lanes: { heads: 1, amp: 1, pitch: u => 0.5 + 0.4 * Math.sin(u * 20 + i), formant: u => 0.5 + 0.5 * Math.sin(u * 13 + i), grain: 0.2 + (i % 3) * 0.3 } }));
    }
    const e = mkEngine({ channels: [Ls, Rs], sampleRate: SR }, { engine: eng, lock, outGain: 12, spread: 1, spray: 0.3, grainLock: 12 }, sr);
    e.setSnapshot(mkSnap(notes, 4));
    e.setTransport({ playing: true, bpm: 120, beat: 0, loop: true });
    let bad = 0, maxRun = 0;
    const out = render(e, Math.round(3 * sr), 128, () => { const s = e.stats(); maxRun = Math.max(maxRun, s.grains); });
    for (let i = 0; i < out.L.length; i++) if (!Number.isFinite(out.L[i]) || !Number.isFinite(out.R[i])) bad++;
    const pk = Math.max(peakAbs(out.L), peakAbs(out.R)), s = e.stats();
    const name = `${lock ? 'lock' : eng} @${sr / 1000} kHz`;
    check(`${name}: no NaN/inf`, bad === 0, bad + ' bad samples', '0');
    check(`${name}: peak <= 0.98`, pk <= 0.98, f2(pk, 4), '<= 0.98');
    check(`${name}: grain budget never exceeded (max per-segment sum of head peaks)`, s.maxGrains <= s.budget,
      `max ${s.maxGrains} of ${s.budget} (thinned heads now ${s.thinnedHeads}, fade tails cut ${s.cuts}, voices ${s.voices})`, '<= budget');
    rows.push(s.maxGrains / s.budget);
  }
  info('budget use (max / budget) across engines: ' + fmtStats(stats(rows), 3));
});

section('budget thinning: oldest/quietest thin first, the newest note never thins', () => {
  const src = tone(midiHz(60), 4, SR);
  const e = mkEngine(src, { engine: 'voice', heads: 8, spread: 0.5 });
  const newestOk = [], thinnedCounts = [];
  for (let i = 0; i < 6; i++) {
    e.noteOn(84 + i, 1);
    render(e, Math.round(0.08 * SR));
    const d = e.getDisplay(), live = d.voices.filter(v => !v.released);
    const newest = live[live.length - 1];
    newestOk.push(newest && newest.heads.length === 8 && newest.heads.every(h => !h.thinned && h.level > 0.3));
    thinnedCounts.push(e.stats().thinnedHeads);
  }
  check('the newest note keeps all 8 heads after every note-on', newestOk.every(Boolean), newestOk.map(b => (b ? 'ok' : 'THIN')).join(' '), 'ok x6');
  check('older notes thin when busy (8 heads x 8 grains each at C5)', thinnedCounts[5] > 0, 'thinned heads after each note-on: ' + thinnedCounts.join(' '), '> 0 at the end');
  const d = e.getDisplay();
  info('heads sounding per voice, oldest first: ' + d.voices.map(v => v.heads.filter(h => !h.thinned).length).join(' ') + ' (budget ' + e.stats().budget + ', max ' + e.stats().maxGrains + ')');
  const per = d.voices.map(v => v.heads.filter(h => !h.thinned).length);
  check('thinning takes the oldest first (sounding heads never decrease toward newer voices)', per.every((x, i) => i === 0 || x >= per[i - 1]), per.join(' '), 'non-decreasing');
});

section('Region at the end: stop goes silent, hold holds', () => {
  const ramp = rampSource(2, SR), rn = ramp.channels[0].length;
  for (const eng of ['tape', 'voice', 'cloud']) {
    const e = mkEngine(ramp, { engine: eng, region: { start: 0, end: 0.1, atEnd: 'stop' }, grainCloud: 30 });
    e.noteOn(60, 1);
    const out = render(e, 0.6 * SR);
    const before = rms(out.L, 0.05 * SR, 0.15 * SR), after = rms(out.L, 0.3 * SR, 0.6 * SR);
    check(`${eng}: atEnd stop goes silent after the Region end (0.2 s)`, before > 0.05 && after < 1e-5, `rms before ${f2(before, 4)}, after ${after.toExponential(2)}`, 'after < 1e-5');
  }
  for (const eng of ['tape', 'voice']) {
    const e = mkEngine(ramp, { engine: eng, region: { start: 0, end: 0.1, atEnd: 'hold' } });
    e.noteOn(60, 1);
    const out = render(e, 0.6 * SR);
    const p1 = decodeAt(out, Math.round(0.35 * SR), 100), p2 = decodeAt(out, Math.round(0.55 * SR), 100);
    check(`${eng}: atEnd hold holds the last bit`, Math.abs(p1 - 0.1) < 0.003 && Math.abs(p2 - p1) < 0.0005, `positions ${f2(p1, 4)}, ${f2(p2, 4)}`, '0.1 and steady');
  }
});

section('filters: Tone default is open; Tape formant EQ is 0 dB at centre', () => {
  const nz = noise(3, SR, 12);
  const e = mkEngine(nz, { engine: 'tape' });
  e.noteOn(60, 1);
  const out = render(e, 2 * SR);
  const st = out.L.length - 32768;
  const hf = bandEnergy(out.L, SR, 14000, 16000, 15, st) / bandEnergy(nz, SR, 14000, 16000, 15, st);
  const lf = bandEnergy(out.L, SR, 1000, 2000, 15, st) / bandEnergy(nz, SR, 1000, 2000, 15, st);
  const rel = 10 * Math.log10(hf / lf), lp = e.voices[0].lpHz;
  check('Tone default open: 14-16 kHz vs 1-2 kHz response', rel > -3, f2(rel, 2) + ' dB (low-pass at ' + f2(lp, 0) + ' Hz)', '> -3 dB, cutoff > 15 kHz');
  check('Tone default cutoff', lp > 15000, f2(lp, 0) + ' Hz', '> 15000 Hz');
  const eqAt = (offset, fHz) => {
    const s = new Float32Array(2 * SR); for (let i = 0; i < s.length; i++) s[i] = 0.5 * Math.sin(2 * Math.PI * fHz * i / SR);
    const en = mkEngine(s, { engine: 'tape', offsets: { formant: offset } });
    en.noteOn(60, 1);
    const o = render(en, 1.5 * SR);
    return 20 * Math.log10(rms(o.L, 0.5 * SR, 1.4 * SR) / (rms(s, 0.5 * SR, 1.4 * SR) * 0.8 * 0.8 * 1.3));
  };
  const c0 = eqAt(0, 250 * Math.pow(2, 0.5 * 3.8));
  check('Tape formant EQ at the centre (lane 0.5): gain at its own frequency', Math.abs(c0) < 0.05, f2(c0, 3) + ' dB', '0 +- 0.05 dB');
  const cUp = eqAt(0.3, 250 * Math.pow(2, 0.8 * 3.8)), cDn = eqAt(-0.3, 250 * Math.pow(2, 0.2 * 3.8));
  check('Tape formant EQ moves and boosts off-centre (lane 0.8 and 0.2: +5.4 dB expected)', Math.abs(cUp - 5.4) < 0.3 && Math.abs(cDn - 5.4) < 0.3, `${f2(cUp, 2)} dB at ${f2(250 * Math.pow(2, 0.8 * 3.8), 0)} Hz, ${f2(cDn, 2)} dB at ${f2(250 * Math.pow(2, 0.2 * 3.8), 0)} Hz`, '5.4 +- 0.3 dB');
});

section('notes start within 1 sample of their beat (120 and 87 bpm)', () => {
  const dc = new Float32Array(SR * 2).fill(0.5);
  const errs = [];
  for (const bpm of [120, 87]) {
    const ss = [0.37, 1.37, 2.71, 3.13, 4.0, 5.555];
    const notes = ss.map((s, i) => mkNote({ id: 'q' + i, p: 60, s, l: 0.2, engine: ['tape', 'voice', 'cloud'][i % 3] }));
    const e = mkEngine(dc, { engine: 'tape' });
    e.setSnapshot(mkSnap(notes, 2));
    e.setTransport({ playing: true, bpm, beat: 0, loop: false });
    const out = render(e, Math.round(beatSamples(6, bpm)), 100);
    for (const s of ss) {
      const exp = beatSamples(s, bpm);
      let i = Math.max(0, Math.floor(exp) - 8); while (i < out.L.length && out.L[i] === 0) i++;
      errs.push(i - exp);
    }
  }
  const es = stats(errs);
  check('first sounding sample minus the exact beat time', es.min >= 0 && es.max < 1, fmtStats(es, 3), '0 <= err < 1 sample');
});

section('loop seam: the crossfade leaves no click (Tape and Voice)', () => {
  const src = tone(midiHz(60), 2, SR, { partials: 2 });   // smooth wave: a hard wrap would show as a step
  const seamRatio = (eng, xfadeMs) => {
    const e = mkEngine(src, { engine: eng, region: { start: 0.1, end: 0.35, xfadeMs } });
    e.noteOn(60, 1);
    const out = render(e, 2.5 * SR);
    // (integrator) the loop period is the Region length L; the seam fade sits after each pass boundary k*L when
    // the file goes on past the Region end (G.post), before it otherwise
    const G = e.voices[0].reg, L = G.L, X = G.X;   // the engine's own Region (frames)
    const zones = []; for (let k = 1; k * L < out.L.length; k++) { const a = G.post ? k * L : k * L - X; const mg = eng === 'voice' ? 600 : 60; zones.push([Math.floor(a) - mg, Math.ceil(a + X) + mg]); }
    if (xfadeMs > 0 && !(X > 0 && G.post && G.Lp === L)) return { r: Infinity, zones: 0, seam: NaN, steady: NaN };   // Region 0.1..0.35 has material after it: a post fade is expected
    let seam = 0, steady = 0;
    for (const [a, b] of zones) seam = Math.max(seam, maxStep(out.L, a, b));
    const inZone = i => zones.some(([a, b]) => i >= a && i < b);
    for (let i = 4800; i < out.L.length; i++) if (!inZone(i) && !inZone(i - 1)) steady = Math.max(steady, Math.abs(out.L[i] - out.L[i - 1]));
    return { r: seam / steady, zones: zones.length, seam, steady };
  };
  for (const eng of ['tape', 'voice']) {
    const w = seamRatio(eng, 10), wo = seamRatio(eng, 0);
    check(`${eng}: max step at the seams / steady max step (10 ms crossfade)`, w.r < 1.5, `${f2(w.r, 3)} over ${w.zones} seams (seam ${f2(w.seam, 4)}, steady ${f2(w.steady, 4)}; no crossfade: ${f2(wo.r, 3)} = ${f2(wo.seam, 4)} / ${f2(wo.steady, 4)})`, '< 1.5');
  }
  const ramp = rampSource(2, SR);
  const e = mkEngine(ramp, { engine: 'tape', region: { start: 0, end: 0.5, xfadeMs: 10 } });
  e.noteOn(60, 1);
  const o = render(e, 1.5 * SR);
  const ms = maxStep(o.L, 4800, o.L.length);
  check('ramp (worst case: 0.85 -> 0.05 at the seam) crossfades instead of jumping', ms < 0.01, 'largest step ' + f2(ms, 5), '< 0.01 (a hard wrap would be ~0.33)');
});

section('chase: starting the transport inside a note starts it part-way', () => {
  const ramp = rampSource(8, SR), rn = ramp.channels[0].length;
  for (const eng of ['tape', 'voice']) {
    const N = mkNote({ id: 'ch', p: 60, s: 0, l: 16, engine: eng });
    const e = mkEngine(ramp, { engine: eng });
    e.setSnapshot(mkSnap([N]));
    e.setTransport({ playing: true, bpm: 120, beat: 4, loop: false });
    const out = render(e, 0.2 * SR);
    const exp = (2 * SR + 2400) / rn, got = decodeAt(out, 2400, 100);
    check(`${eng}: chased note sounds from the first sample`, out.L[0] !== 0 && e.stats().voices === 1, 'first sample ' + out.L[0].toExponential(2), 'non-zero');
    const fadeRatio = rms(out.L, 0, 120) / rms(out.L, 600, 720);
    check(`${eng}: chase fades in over 10 ms`, fadeRatio > 0.02 && fadeRatio < 0.3, 'rms 0-2.5 ms / rms 12.5-15 ms = ' + f2(fadeRatio, 3), '0.02 .. 0.3');
    check(`${eng}: chased playhead is where the note would be`, Math.abs(got - exp) < 0.002, f2(got, 5), f2(exp, 5) + ' +- 0.002');
  }
  // chased chain: B carries A's playhead even when the transport starts inside B
  const A = mkNote({ id: 'A', p: 60, s: 0, l: 4, engine: 'tape', glide: { d: 0, len: 0, curve: 0, toId: 'B' } });
  const B = mkNote({ id: 'B', p: 60, s: 4, l: 4, engine: 'tape' });
  chainNotes([A, B]);
  const e = mkEngine(ramp, { engine: 'tape' });
  e.setSnapshot(mkSnap([A, B]));
  e.setTransport({ playing: true, bpm: 120, beat: 6, loop: false });
  const out = render(e, 0.1 * SR);
  const got = decodeAt(out, 2400, 100), exp = (3 * SR + 2400) / rn;
  check('chased chain carries A\'s playhead into B', Math.abs(got - exp) < 0.002, f2(got, 5) + ' (B alone would be at ' + f2((1 * SR + 2400) / rn, 5) + ')', f2(exp, 5) + ' +- 0.002');
});
section('per-note engine: each note plays its own engine', () => {
  const src = formantTone(midiHz(60), 6, SR, 1000, 350);
  const A = mkNote({ id: 'tp', p: 72, s: 0, l: 3, engine: 'tape' }), B = mkNote({ id: 'vc', p: 72, s: 4, l: 3, engine: 'voice' });
  const e = mkEngine(src, { engine: 'cloud' });   // the inst engine differs from both notes on purpose
  e.setSnapshot(mkSnap([A, B]));
  e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
  let engA = '', engB = '';
  const out = render(e, Math.round(beatSamples(7, 120)), 128, i => {
    if (!engA && i > 0.5 * SR) engA = (e.getDisplay().voices[0] || {}).engine;
    if (!engB && i > 2.5 * SR) engB = (e.getDisplay().voices[0] || {}).engine;
  });
  const c0 = centroid(src, SR, 15, 0), cA = centroid(out.L, SR, 15, Math.round(0.3 * SR)), cB = centroid(out.L, SR, 15, Math.round(2.3 * SR));
  check('note 1 plays Tape (+12 st moves the formant up ~2x)', engA === 'tape' && cA / c0 > 1.7, `engine ${engA}, centroid x${f2(cA / c0, 2)}`, 'tape, > x1.7');
  check('note 2 plays Voice (+12 st keeps the formant)', engB === 'voice' && cB / c0 < 1.25, `engine ${engB}, centroid x${f2(cB / c0, 2)}`, 'voice, < x1.25');
});

section('Cloud Refresh: the grab holds between refreshes while the head moves on; Skip is seeded', () => {
  const ramp = rampSource(8, SR), rn = ramp.channels[0].length;
  const run = (refresh, seed = 1) => {
    const e = mkEngine(ramp, { engine: 'cloud', grainCloud: 20, speed: 4, refresh, seed });
    e.noteOn(60, 1);
    const out = render(e, 1.6 * SR);
    const mids = [], within = [];
    for (let k = 1; k < 15; k++) {   // refresh ticks every 100 ms from the note start
      const a = decodeAt(out, Math.round((k * 0.1 + 0.035) * SR), 24), b = decodeAt(out, Math.round((k * 0.1 + 0.095) * SR), 24);
      mids.push(a); within.push(Math.abs(b - a));
    }
    return { out, mids, within };
  };
  const hold = run({ mode: 'free', ms: 100 }), smooth = run({ mode: 'smooth' });
  const wHold = stats(hold.within), wSmooth = stats(smooth.within);
  check('Refresh 100 ms: the read position holds inside each period', wHold.max < 0.002, 'movement inside a period: ' + fmtStats(wHold, 4), '< 0.002 (Smooth moves ' + f2(wSmooth.med, 4) + ')');
  const stepsHold = hold.mids.slice(1).map((m, i) => m - hold.mids[i]);
  check('Refresh 100 ms: each refresh grabs where the head is now (+0.05 per period at 4x)', stats(stepsHold).min > 0.04 && stats(stepsHold).max < 0.06, fmtStats(stats(stepsHold), 4), '0.05 +- 0.01');
  const sk = run({ mode: 'free', ms: 100, skip: 0.5 }), sk2 = run({ mode: 'free', ms: 100, skip: 0.5 }), sk3 = run({ mode: 'free', ms: 100, skip: 0.5 }, 2);
  const grabs = r => r.mids.slice(1).filter((m, i) => m - r.mids[i] > 0.02).length;
  check('Skip 0.5: about half the refreshes are skipped', grabs(sk) >= 3 && grabs(sk) <= 10, grabs(sk) + ' of 13 refreshes grabbed', '3..10');
  check('Skip: two renders are identical', identical(sk.out.L, sk2.out.L).same, 'max diff ' + identical(sk.out.L, sk2.out.L).diff, '0');
  check('Skip: Re-roll (seed+1) changes which refreshes are skipped', !identical(sk.out.L, sk3.out.L).same, grabs(sk3) + ' grabbed with seed 2', 'differs');
});

section('Spray: seeded scatter of each grain around the head', () => {
  const ramp = rampSource(8, SR), rn = ramp.channels[0].length;
  const dev = (spray, seed = 1) => {
    const e = mkEngine(ramp, { engine: 'cloud', grainCloud: 20, spray, seed });
    e.noteOn(60, 1, { startPos: 0.25 });
    const out = render(e, 1.0 * SR);
    const d = []; for (let t = 0.1; t < 0.95; t += 0.005) { const i = Math.round(t * SR); d.push(decodeAt(out, i, 4) - (0.25 + i / rn)); }
    return { out, s: stats(d.map(Math.abs)) };
  };
  const a = dev(0), b = dev(0.25), b2 = dev(0.25), c = dev(0.25, 2);
  check('Spray 0.25 scatters the read position (spray x min(1 s, Region/2) = 0.25 s = 0.031 of the file)', b.s.med > 4 * a.s.med + 0.002 && b.s.max < 0.04,
    `|deviation| spray 0: med ${f2(a.s.med, 5)}; spray 0.25: ${fmtStats(b.s, 4)}`, 'clearly larger, max < 0.04');
  check('Spray: two renders identical', identical(b.out.L, b2.out.L).same, 'max diff ' + identical(b.out.L, b2.out.L).diff, '0');
  check('Spray: seed+1 changes it', !identical(b.out.L, c.out.L).same, 'differs', 'differs');
});

section('Drift and Head tune', () => {
  const ramp = rampSource(8, SR), rn = ramp.channels[0].length;
  const speeds = drift => {
    const e = mkEngine(ramp, { engine: 'voice', heads: 4, spread: 0, drift });
    e.noteOn(60, 1, { startPos: 0.1 });
    render(e, 0.25 * SR); const p0 = e.getDisplay().voices[0].heads.map(h => h.pos);
    render(e, 0.5 * SR); const p1 = e.getDisplay().voices[0].heads.map(h => h.pos);
    return p1.map((p, i) => (p - p0[i]) * rn / (0.5 * SR));
  };
  const s1 = speeds(1), s0 = speeds(0);
  check('Drift 1: heads run at different seeded speeds within x(1 +- 0.25)', Math.min(...s1) >= 0.75 && Math.max(...s1) <= 1.25 && Math.max(...s1) - Math.min(...s1) > 0.05,
    s1.map(x => f2(x, 3)).join(' '), 'in 0.75..1.25, not all equal');
  check('Drift 0: every head at exactly 1x', s0.every(x => Math.abs(x - 1) < 1e-6), s0.map(x => f2(x, 6)).join(' '), '1.000000');
  // Head tune fifths: head 2 sounds a fifth up (G3 over C3)
  const src = tone(midiHz(60), 6, SR, { partials: 8 });
  const e = mkEngine(src, { engine: 'voice', heads: 2, spread: 0, headTune: 'fifths' });
  e.noteOn(60, 1);
  const out = render(e, 2 * SR);
  const h = harmonicF0(out.L, SR, midiHz(67), 16, undefined, 1), h0 = harmonicF0(out.L, SR, midiHz(60), 16, undefined, 1);
  const m = spectrum(out.L, out.L.length - 65536, 16), bin = SR / 65536;
  const lvl = 20 * Math.log10(m[Math.round(midiHz(67) / bin)] / m[Math.round(midiHz(60) / bin)]);
  check('Head tune fifths: head 2 at G3 (+7 st) in tune', Math.abs(centsOf(h.f0, midiHz(67))) <= 3 && lvl > -12,
    `${f2(centsOf(h.f0, midiHz(67)), 3)} cents, level ${f2(lvl, 1)} dB vs head 1 (head 1 ${f2(centsOf(h0.f0, midiHz(60)), 3)} c)`, '+-3 cents, > -12 dB');
});

section('fractional heads: no click when Heads goes 2 -> 2.5 -> 3', () => {
  const src = tone(midiHz(60), 4, SR, { partials: 6 });
  const vOf = H => 0.5 + (H - 1) / 14;
  const ratios = [];
  for (const eng of ['tape', 'voice', 'cloud']) {
    const N = mkNote({ id: 'fh', p: 60, s: 0, l: 6, engine: eng, lanes: { heads: u => (u < 1 / 3 ? vOf(2) : u < 2 / 3 ? vOf(2.5) : vOf(3)) } });
    const e = mkEngine(src, { engine: eng, spread: 0.25 });
    e.setSnapshot(mkSnap([N]));
    e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
    const out = render(e, Math.round(2.95 * SR));
    const steady = Math.max(maxStep(out.L, 0.4 * SR, 0.95 * SR), maxStep(out.L, 2.4 * SR, 2.95 * SR));
    const trans = Math.max(maxStep(out.L, 0.98 * SR, 1.35 * SR), maxStep(out.L, 1.98 * SR, 2.35 * SR));
    const r = trans / steady; ratios.push(r);
    check(`${eng}: max step around the head changes / steady max step`, r < 1.5, `${f2(r, 3)} (transitions ${f2(trans, 4)}, steady ${f2(steady, 4)})`, '< 1.5');
  }
  info('fractional-heads click ratios: ' + ratios.map(r => f2(r, 3)).join(' '));
});

section('Tape stutter Refresh: jumps back to where the grab began', () => {
  const ramp = rampSource(4, SR), rn = ramp.channels[0].length;
  const run = skip => {
    const N = mkNote({ id: 'st', p: 60, s: 0, l: 4, engine: 'tape' });
    const e = mkEngine(ramp, { engine: 'tape', refresh: { mode: 'sync', rate: '1/16', lockToBeat: true, skip } });
    e.setSnapshot(mkSnap([N]));
    e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
    const out = render(e, Math.round(beatSamples(3.9, 120)));
    const per = beatSamples(0.25, 120);   // 6000 frames
    const mids = []; for (let k = 0; k < 12; k++) mids.push(decodeAt(out, Math.round(k * per + per / 2), 50));
    return mids;
  };
  const rep = run(0), cont = run(1);
  const sp = Math.max(...rep) - Math.min(...rep);
  check('Refresh 1/16, Skip 0: every period replays the first grab', sp < 0.001, `mid-period positions spread ${sp.toExponential(2)} (at ${f2(rep[0], 4)})`, '< 0.001');
  const inc = cont.slice(1).map((m, i) => m - cont[i]);
  check('Skip 1 (every refresh skipped): the tape keeps going', inc.every(d => Math.abs(d - 6000 / rn) < 0.001), fmtStats(stats(inc), 5), f2(6000 / rn, 5) + ' per period');
});

section('mod wheels override their target lane on live voices (incl. a Sample sub-lane)', () => {
  const nz = noise(4, SR, 13);
  const hf = (wheel, clip) => {
    const e = mkEngine(nz, { engine: 'tape', mod2: { target: 'tone', cc: 2 } });
    if (clip) { e.setSnapshot(mkSnap([mkNote({ id: 'cl', p: 60, s: 0, l: 4, engine: 'tape' })])); e.setTransport({ playing: true, bpm: 120, beat: 0 }); }
    else e.noteOn(60, 1);
    if (wheel != null) e.setController('mod2', wheel);
    const out = render(e, 1.2 * SR);
    return bandEnergy(out.L, SR, 4000, 8000, 15, out.L.length - 32768);
  };
  const open = hf(null), closed = hf(0.3), clipClosed = hf(0.3, true), clipOpen = hf(null, true);
  check('mod2 -> Tone at 0.3 closes the low-pass on a live voice', 10 * Math.log10(open / closed) > 30, f2(10 * Math.log10(open / closed), 1) + ' dB less 4-8 kHz energy', '> 30 dB');
  check('clip voices follow their lanes, not the wheel', Math.abs(10 * Math.log10(clipOpen / clipClosed)) < 0.5, f2(10 * Math.log10(clipOpen / clipClosed), 2) + ' dB', '|.| < 0.5 dB');
  const ramp = rampSource(4, SR), rn = ramp.channels[0].length;
  const travelRun = w => {
    const e = mkEngine(ramp, { engine: 'voice', mod1: { target: 'travel', cc: 1 } });
    e.noteOn(60, 1, { startPos: 0.5 });
    e.setController('mod1', w);
    const out = render(e, 0.8 * SR);
    return (decodeAt(out, Math.round(0.7 * SR), 200) - decodeAt(out, Math.round(0.2 * SR), 200)) * rn / (0.5 * SR);
  };
  const sFreeze = travelRun(0.5), sBack = travelRun(0.25), sFwd = travelRun(0.75);
  check('mod1 -> Travel (Sample sub-lane): 0.5 stops, 0.25 backwards 1x, 0.75 forwards 1x', Math.abs(sFreeze) < 0.01 && Math.abs(sBack + 1) < 0.02 && Math.abs(sFwd - 1) < 0.02,
    `speeds ${f2(sFreeze, 3)} / ${f2(sBack, 3)} / ${f2(sFwd, 3)}`, '0 / -1 / +1');
  const e = mkEngine(ramp, { engine: 'voice', mod1: { target: 'heads', cc: 1 } });
  e.noteOn(60, 1); render(e, 0.05 * SR);
  const h1 = e.getDisplay().voices[0].heads.length;
  e.setController('mod1', 1); render(e, 0.05 * SR);
  const h8 = e.getDisplay().voices[0].heads.length;
  e.setController('mod1', null); render(e, 0.1 * SR);
  const hBack = e.getDisplay().voices[0].heads.filter(h => h.level > 0.01).length;
  check('mod1 -> Heads (Sample sub-lane): wheel 1 gives 8 heads; letting go returns to the lane', h1 === 1 && h8 === 8 && hBack === 1, `${h1} -> ${h8} -> ${hBack}`, '1 -> 8 -> 1');
});

section('live: liveLegato carries the playhead; polyphony steals the oldest', () => {
  const ramp = rampSource(4, SR), rn = ramp.channels[0].length;
  const e = mkEngine(ramp, { engine: 'voice', liveLegato: true });
  e.noteOn(60, 1);
  const a = render(e, 0.5 * SR);
  const before = decodeAt(a, a.L.length - 300, 200);
  e.noteOn(64, 1);
  let minEnv = 1; const b = render(e, 0.2 * SR, 64, () => { if (e.voices[0]) minEnv = Math.min(minEnv, e.voices[0].env); });
  const after = decodeAt(b, 300, 200), exp = before + 600 / rn;
  const d = e.getDisplay();
  check('liveLegato: the new key takes over the held voice (one voice, no new attack)', d.voices.length === 1 && d.voices[0].p === 64 && minEnv > 0.999, `voices ${d.voices.length}, p ${d.voices[0].p}, min envelope ${f2(minEnv, 4)}`, '1, 64, > 0.999');
  check('liveLegato: the playhead carries on', Math.abs(after - exp) < 0.002, f2(after, 5), f2(exp, 5) + ' +- 0.002');
  e.noteOff(64); render(e, 0.02 * SR);
  const back = e.getDisplay().voices.filter(v => !v.released);
  check('liveLegato: letting go of the new key returns to the held one', back.length === 1 && back[0].p === 60, back.map(v => v.p).join(','), '60');
  const e2 = mkEngine(ramp, { engine: 'voice', liveLegato: false });
  e2.noteOn(60, 1); render(e2, 0.5 * SR); e2.noteOn(64, 1); render(e2, 0.01 * SR);
  const v2 = e2.getDisplay().voices;
  check('liveLegato off: a second key is a second voice starting at the Region start', v2.length === 2 && v2[1].heads[0].pos < 0.01, `voices ${v2.length}, new head at ${f2(v2[1].heads[0].pos, 4)}`, '2, < 0.01');
  const e3 = mkEngine(tone(midiHz(60), 2, SR), { engine: 'tape' });
  for (let i = 0; i < 20; i++) { e3.noteOn(40 + i, 1); render(e3, 0.01 * SR); }
  render(e3, 0.02 * SR);
  const st = e3.stats();
  check('polyphony 16: 20 keys steal the 4 oldest (5 ms fade)', st.voices === 16 && st.steals === 4, `voices ${st.voices}, steals ${st.steals}`, '16, 4');
  const e4 = mkEngine(tone(midiHz(60), 2, SR), { engine: 'tape' });
  e4.setController('sustain', 1); e4.noteOn(60, 1); render(e4, 0.05 * SR); e4.noteOff(60); render(e4, 0.2 * SR);
  const held = e4.stats().voices; e4.setController('sustain', 0); render(e4, 0.2 * SR);
  check('sustain pedal holds a released key until the pedal comes up', held === 1 && e4.stats().voices === 0, `${held} while held, ${e4.stats().voices} after`, '1, 0');
});

section('stereo: a stereo source keeps L != R; a mono source gives L == R at spread 0', () => {
  const tl = tone(midiHz(60), 3, SR), tr = tone(midiHz(64), 3, SR, { seed: 9 });
  const e = mkEngine([tl, tr], { engine: 'voice', heads: 3, spread: 0 });
  e.noteOn(60, 1);
  const o = render(e, 1 * SR);
  const r = corr(o.L.subarray(4800), o.R.subarray(4800));
  check('stereo source: L and R stay different', !identical(o.L, o.R).same && Math.abs(r) < 0.5, 'corr(L,R) ' + f2(r, 3), '|r| < 0.5');
  for (const eng of ['tape', 'voice', 'cloud']) {
    const m = mkEngine(tl, { engine: eng, heads: 3, spread: 0 });
    m.noteOn(60, 1);
    const om = render(m, 0.5 * SR);
    check(`${eng}: mono source, spread 0, 3 heads: L == R`, identical(om.L, om.R).same && rms(om.L) > 0.01, 'max |L-R| ' + identical(om.L, om.R).diff, '0');
  }
  // noise, so the heads carry different material (a periodic tone gives every snapped Voice head the same waveform)
  const p = mkEngine(noise(3, SR, 31), { engine: 'voice', heads: 3, spread: 0.6 });
  p.noteOn(60, 1);
  const op = render(p, 0.5 * SR);
  const rp = corr(op.L.subarray(4800), op.R.subarray(4800));
  check('mono source with spread 0.6: heads pan apart (L != R)', rp < 0.9 && identical(op.L, op.R).diff > 0.05, 'corr(L,R) ' + f2(rp, 3) + ', max |L-R| ' + f2(identical(op.L, op.R).diff, 4), 'corr < 0.9');
});

section('snapshot edits: voices follow at once; deleted notes release', () => {
  const src = tone(midiHz(60), 4, SR);
  const N = mkNote({ id: 'ed', p: 60, s: 0, l: 8, engine: 'tape' });
  const e = mkEngine(src, { engine: 'tape' });
  e.setSnapshot(mkSnap([N]));
  e.setTransport({ playing: true, bpm: 120, beat: 0 });
  const a = render(e, 0.5 * SR);
  e.setSnapshot(mkSnap([mkNote({ id: 'ed', p: 60, s: 0, l: 8, engine: 'tape', lanes: { amp: 0.2 } })]));
  const b = render(e, 0.3 * SR);
  const ratio = rms(b.L, 0.05 * SR, 0.3 * SR) / rms(a.L, 0.2 * SR, 0.5 * SR), exp = (0.2 * 0.2) / (0.8 * 0.8);
  check('editing a playing note\'s Volume lane takes effect at once', Math.abs(ratio - exp) < 0.01 && e.stats().voices === 1, f2(ratio, 4), f2(exp, 4) + ' (same voice)');
  e.setSnapshot(mkSnap([]));
  const c = render(e, 0.2 * SR);
  check('deleting a playing note releases its voice', e.stats().voices === 0 && rms(c.L, 0.1 * SR, 0.2 * SR) === 0, `voices ${e.stats().voices}`, '0, silent');
  e.setSnapshot(mkSnap([N]));
  e.setTransport({ playing: false });
  render(e, 0.2 * SR);
  check('stopping the transport releases clip voices', e.stats().voices === 0, 'voices ' + e.stats().voices, '0');
});

section('display: heads, directions and at most 128 grains', () => {
  const src = tone(midiHz(60), 4, SR);
  const e = mkEngine(src, { engine: 'cloud', heads: 8, grainCloud: 300 });
  for (let i = 0; i < 16; i++) e.noteOn(48 + i, 1);
  render(e, 0.3 * SR);
  const d = e.getDisplay();
  const okHeads = d.voices.every(v => v.heads.every(h => h.pos >= 0 && h.pos <= 1 && [-1, 0, 1].includes(h.dir)));
  check('display grains capped', d.grains.length <= 128 && d.grains.length > 0, d.grains.length + ' grains', '1..128');
  check('display heads: pos 0..1 of the file, dir -1/0/1', okHeads && d.voices.length === 16, d.voices.length + ' voices', '16, valid');
  check('display beat advances while stopped (free clock for live Scatter)', d.beat > 0.5 && d.playing === false, 'beat ' + f2(d.beat, 3), '> 0.5 after 0.3 s at 120 bpm');
});

section('AudioWorklet glue (simulated worklet scope)', () => {
  const posted = []; let reg = null;
  class FakeAWP { constructor() { this.port = { postMessage: m => posted.push(m), onmessage: null }; } }
  const boot = new Function('registerProcessor', 'AudioWorkletProcessor', 'sampleRate',
    createStrandEngineModule.toString() + '\n' + strandWorkletGlue.toString() + '\nstrandWorkletGlue();');
  boot((name, cls) => { reg = { name, cls }; }, FakeAWP, 48000);
  check('registers the strand-engine processor', reg && reg.name === 'strand-engine', reg ? reg.name : 'nothing', 'strand-engine');
  const p = new reg.cls();
  const send = m => p.port.onmessage({ data: m });
  send({ type: 'sample', channels: [tone(midiHz(60), 2, SR)], sampleRate: SR, name: 'tone' });
  send({ type: 'inst', inst: mkInst({ engine: 'voice' }) });
  send({ type: 'snapshot', snapshot: mkSnap([mkNote({ id: 'w', p: 60, s: 0, l: 2 })]) });
  send({ type: 'transport', playing: true, bpm: 120, beat: 0, loop: true });
  send({ type: 'noteOn', p: 67, vel: 100 });
  send({ type: 'ctrl', name: 'mod1', value: 0.4 });
  let energy = 0, energyMono = 0;
  for (let b = 0; b < 375; b++) {   // 1 s of 128-sample quanta
    const o = [[new Float32Array(128), new Float32Array(128)]];
    p.process([], o);
    for (let i = 0; i < 128; i++) energy += o[0][0][i] * o[0][0][i];
  }
  send({ type: 'noteOff', p: 67 });
  for (let b = 0; b < 20; b++) { const o = [[new Float32Array(128)]]; p.process([], o); for (let i = 0; i < 128; i++) energyMono += o[0][0][i] ** 2; }
  send({ type: 'allOff' });
  const disp = posted.filter(m => m.type === 'display'), errs = posted.filter(m => m.type === 'error');
  check('worklet renders sound and posts display ~30 times a second', energy > 1 && disp.length >= 28 && disp.length <= 32 && errs.length === 0,
    `energy ${f2(energy, 1)}, ${disp.length} display messages in 1 s, ${errs.length} errors, last: ${disp.length ? disp[disp.length - 1].voices.length + ' voices' : '-'}`, '~30, no errors');
  check('worklet handles a mono output bus', energyMono > 0, 'energy ' + f2(energyMono, 3), '> 0');
});

section('SLModel snapshots play (if sl-model.js is present)', () => {
  const modelFile = path.join(__dirname, '..', 'src', 'sl-model.js');
  if (!fs.existsSync(modelFile)) { info('sl-model.js not present: skipped'); return; }
  let M; try { M = require(modelFile); } catch (e) { info('sl-model.js failed to load: ' + e.message); return; }
  if (!M.compileSnapshot || !M.newClip || !M.mkNote || !M.defaultInst) { info('SLModel lacks compileSnapshot/newClip/mkNote/defaultInst: skipped'); return; }
  let clip = M.newClip(), inst = M.defaultInst();
  const notes = [M.mkNote(60, 0, 2), M.mkNote(64, 2, 2), M.mkNote(67, 4, 4)];
  clip = Object.assign({}, clip, { notes: (clip.notes || []).concat(notes) });
  let snap;
  try { snap = M.compileSnapshot(clip, inst, { res: 256 }); } catch (err) { check('SLModel.compileSnapshot runs', false, String(err.message), 'no exception'); return; }
  const e = mkEngine(tone(midiHz(60), 4, SR), inst);
  e.setSnapshot(snap);
  e.setTransport({ playing: true, bpm: 120, beat: 0, loop: true });
  let maxV = 0; const out = render(e, 4 * SR, 128, () => { maxV = Math.max(maxV, e.stats().voices); });
  let bad = 0; for (let i = 0; i < out.L.length; i++) if (!Number.isFinite(out.L[i])) bad++;
  check('a compiled SLModel snapshot plays (3 notes)', rms(out.L) > 0.01 && bad === 0 && snap.notes.length === 3, `rms ${f2(rms(out.L), 4)}, notes ${snap.notes.length}, max voices ${maxV}, chains ${snap.notes.map(n => n.chain && n.chain.index).join(',')}`, 'sound, finite');
});

section('speed (x real time, node, 48 kHz, 128-sample blocks)', () => {
  const src = tone(midiHz(60), 6, SR, { partials: 12 }), nz = noise(6, SR, 2, 0.3);
  const mix = new Float32Array(src.length); for (let i = 0; i < mix.length; i++) mix[i] = src[i] + nz[i];
  const rows = [];
  const bench = (eng, lock, voices, heads, secs, sr = SR) => {
    const e = mkEngine(mix, { engine: eng, lock, heads, spread: 0.3, refresh: { mode: 'free', ms: 80 } }, sr);
    for (let i = 0; i < voices; i++) e.noteOn(40 + i * 3, 1);   // distinct keys (a repeated key would release its voice)
    const sounding = e.stats().voices;
    let maxG = 0;
    const t0 = process.hrtime.bigint();
    render(e, Math.round(secs * sr), 128, () => { const g = e.stats().grains; if (g > maxG) maxG = g; });
    const sec = Number(process.hrtime.bigint() - t0) / 1e9;
    return { x: secs / sec, maxG, thinned: e.stats().thinnedHeads, sounding, end: e.stats().voices };
  };
  for (const [eng, lock] of [['tape', false], ['voice', false], ['cloud', false], ['cloud', true]]) {
    const name = lock ? 'cloud+lock' : eng;
    const r = bench(eng, lock, 8, 2, 10);
    rows.push(r.x);
    check(`${name}: 8 voices x 2 heads, 10 s`, r.x >= 4 && r.end === 8, `${f2(r.x, 1)}x real time (${r.end} voices sounding, load ${r.maxG} grains/taps)`, '>= 4x (blueprint target in node)');
  }
  for (const [eng, lock] of [['tape', false], ['voice', false], ['cloud', false], ['cloud', true]]) {
    const name = lock ? 'cloud+lock' : eng;
    const r = bench(eng, lock, 16, 8, 5);
    info(`${name}: 16 voices x 8 heads, 5 s: ${f2(r.x, 2)}x real time (${r.end} voices, budget-limited: max load ${r.maxG}, thinned heads ${r.thinned})`);
    rows.push(r.x);
  }
  const r96 = bench('voice', false, 8, 2, 5, 96000);
  info(`voice at 96 kHz, 8 voices x 2 heads: ${f2(r96.x, 2)}x real time (the suite's CPU tests run at 96 kHz)`);
});
section('Travel path kind and note start modes', () => {
  const ramp = rampSource(4, SR), rn = ramp.channels[0].length;
  // path: the lane is head 1's place in the Region (0 = start, 1 = end); rising plays forward, falling backward
  for (const eng of ['voice', 'cloud']) {
    const N = mkNote({ id: 'pa', p: 60, s: 0, l: 4, engine: eng, travel: 'path', lanes: { travel: u => 0.2 + 0.6 * u } });
    const e = mkEngine(ramp, { engine: eng, grainCloud: 20 });
    e.setSnapshot(mkSnap([N]));
    e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
    const out = render(e, Math.round(beatSamples(3.9, 120)));
    const errs = []; for (let b = 0.5; b < 3.8; b += 0.25) { const i = Math.round(beatSamples(b, 120)); errs.push(decodeAt(out, i, 60) - (0.2 + 0.6 * b / 4)); }
    check(`${eng}: Path follows the lane (position = lane value of the Region)`, stats(errs.map(Math.abs)).max < 0.003, '|error| ' + fmtStats(stats(errs.map(Math.abs)), 4), '< 0.003');
  }
  {
    const N = mkNote({ id: 'pt', p: 60, s: 0, l: 4, engine: 'tape', travel: 'path', lanes: { travel: 0.9 } });
    const e = mkEngine(ramp, { engine: 'tape' });
    e.setSnapshot(mkSnap([N])); e.setTransport({ playing: true, bpm: 120, beat: 0, loop: false });
    const out = render(e, SR);
    const d = (decodeAt(out, 36000, 100) - decodeAt(out, 12000, 100)) * rn / 24000;
    check('Tape treats Path as speed at 1x', Math.abs(d - 1) < 0.01, f2(d, 4) + 'x', '1x');
  }
  // start modes
  const startOf = (start, s = 0, seed = 1, bpm = 120) => {
    const N = mkNote({ id: 'sm', p: 60, s, l: 1, engine: 'tape', start });
    const e = mkEngine(ramp, { engine: 'tape', seed });
    e.setSnapshot(mkSnap([N]));
    e.setTransport({ playing: true, bpm, beat: 0, loop: false });
    const out = render(e, Math.round(beatSamples(s, bpm)) + 2400);
    return decodeAt(out, Math.round(beatSamples(s, bpm)) + 1200, 100) - 1200 / rn;
  };
  const pReg = startOf({ mode: 'region', pos: 0.3 });
  check('start Region pos 0.3', Math.abs(pReg - 0.3) < 0.001, f2(pReg, 5), '0.3');
  const pSong = startOf({ mode: 'song' }, 2), expSong = ((2 * 0.5 * SR) % rn) / rn;
  check('start Song: (song seconds at 1x) mod Region', Math.abs(pSong - expSong) < 0.001, f2(pSong, 5), f2(expSong, 5));
  const r1 = startOf({ mode: 'random', pos: 0.2, range: 0.5, seed: 0 }), r1b = startOf({ mode: 'random', pos: 0.2, range: 0.5, seed: 0 });
  const r2 = startOf({ mode: 'random', pos: 0.2, range: 0.5, seed: 1 }), r3 = startOf({ mode: 'random', pos: 0.2, range: 0.5, seed: 0 }, 0, 2);
  check('start Random: inside pos .. pos+range, same every render', r1 >= 0.2 && r1 <= 0.7 && Math.abs(r1 - r1b) < 1e-9, `${f2(r1, 5)} twice`, '0.2..0.7, identical');
  check('start Random: Re-roll (note start seed+1 or inst seed+1) moves it', Math.abs(r2 - r1) > 0.001 && Math.abs(r3 - r1) > 0.001, `${f2(r2, 4)}, ${f2(r3, 4)} vs ${f2(r1, 4)}`, 'both differ');
  // every loop pass plays the same (loop pass is not an input)
  const N = mkNote({ id: 'lp', p: 60, s: 1, l: 1, engine: 'cloud', motion: 'scatter', jump: 0.25, start: { mode: 'song' } });
  const e = mkEngine(ramp, { engine: 'cloud', spray: 0.2, grainCloud: 40 });
  e.setSnapshot(mkSnap([N], 1));
  e.setTransport({ playing: true, bpm: 120, beat: 0, loop: true });
  const out = render(e, Math.round(beatSamples(12, 120)));
  const bar = Math.round(beatSamples(4, 120));
  const p1 = out.L.subarray(bar, 2 * bar), p2 = out.L.subarray(2 * bar, 3 * bar);
  check('every loop pass plays the same (Scatter + Spray + Song start)', identical(p1, p2).same && rms(p1) > 0.01, 'max diff between pass 2 and 3: ' + identical(p1, p2).diff, '0');
});
//@@TESTS@@
console.log(`
${passed} passed, ${failed} failed`); if (fails.length) { console.log("failures:"); fails.forEach(f => console.log("  " + f)); } process.exit(failed ? 1 : 0);
