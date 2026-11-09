// tests/test-engine-extra.js: skeptic checks for src/sl-engine.js (node tests/test-engine-extra.js [section words]).
// Written by the engine reviewer, not the engine builder. Each check is meant to FAIL if a BLUEPRINT feature is
// missing, wrong or fragile: exact numbers from BLUEPRINT 1.2-1.4 and 3.3, boundary values, Float32 snapshot tables,
// determinism under the calls the page really makes, untouched inputs, NaN guards, budget and click behaviour,
// and features the builder's suite never measured (vibrato, hollow, tone cutoff, rev, macros, pan, envelope times).
// One line per check: PASS/FAIL name: measured vs limit. "  .. " lines are measurements, not checks.
// A few checks read engine internals (voices[], heads[], budget); those are named "(white-box)".
// Exits 1 on any FAIL. Cubase naming: MIDI 60 = C3.
'use strict';
const path = require('path');
const fs = require('fs');
const vm = require('vm');
const ENGINE_FILE = path.join(__dirname, '..', 'src', 'sl-engine.js');
const engMod = require(ENGINE_FILE);
const { createStrandEngineModule, strandWorkletGlue } = engMod;
const SE = createStrandEngineModule();

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
function dist(arr, d = 3) {
  const a = Array.from(arr).filter(Number.isFinite).sort((x, y) => x - y);
  if (!a.length) return '(none)';
  const q = p => a[Math.min(a.length - 1, Math.floor(p * (a.length - 1)))];
  return `n=${a.length} min ${f2(a[0], d)} p10 ${f2(q(0.1), d)} med ${f2(q(0.5), d)} p90 ${f2(q(0.9), d)} max ${f2(a[a.length - 1], d)}`;
}

// ------------------------------------------------------------------ material (seeded, no Math.random)
function lcg(seed) { let s = seed >>> 0 || 1; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }
function sine(f, secs, sr, amp = 0.5, ph = 0) { const n = Math.round(secs * sr), x = new Float32Array(n); for (let i = 0; i < n; i++) x[i] = amp * Math.sin(2 * Math.PI * f * i / sr + ph); return x; }
function noise(secs, sr, seed = 3, amp = 0.5) { const n = Math.round(secs * sr), r = lcg(seed), x = new Float32Array(n); for (let i = 0; i < n; i++) x[i] = (r() * 2 - 1) * amp; return x; }
function dc(secs, sr, v = 0.5) { return new Float32Array(Math.round(secs * sr)).fill(v); }
function rampSource(secs, sr) {   // L encodes the read position, R constant: pos = (L/R*0.5 - 0.05)/0.8
  const n = Math.round(secs * sr), L = new Float32Array(n), R = new Float32Array(n);
  for (let i = 0; i < n; i++) { L[i] = 0.05 + 0.8 * i / n; R[i] = 0.5; }
  return { channels: [L, R], sampleRate: sr, name: 'ramp' };
}
const decode = (l, r) => (Math.abs(r) > 1e-9 ? (l / r * 0.5 - 0.05) / 0.8 : NaN);
function decodeAt(out, i, w = 16) { let a = 0, b = 0; for (let j = Math.max(0, i - w); j <= Math.min(out.L.length - 1, i + w); j++) { a += out.L[j]; b += out.R[j]; } return decode(a, b); }

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
function mkEngine(src, instPatch, sr = SR, mod = SE) {
  const e = new mod.Engine({ sampleRate: sr });
  e.setInst(mkInst(instPatch));
  if (src) {
    if (src.channels) e.setSample(src);
    else e.setSample({ channels: Array.isArray(src) ? src : [src], sampleRate: sr, name: 'test' });
  }
  return e;
}
function render(e, n, block = 128, hook) {
  n = Math.round(n);
  const L = new Float32Array(n), R = new Float32Array(n);
  const bl = new Float32Array(8192), br = new Float32Array(8192);
  let i = 0;
  while (i < n) {
    const m = Math.min(typeof block === 'function' ? block() : block, n - i);
    e.process(bl, br, m);
    L.set(bl.subarray(0, m), i); R.set(br.subarray(0, m), i);
    i += m;
    if (hook) hook(i);
  }
  return { L, R };
}
const LANE_IDS = SE.LANE_IDS, LANE_DEF = SE.LANE_DEF;
function mkNote(o) {   // snapshot note as SLModel.compileSnapshot emits it (Float32Array tables)
  const res = o.res || 256, l = o.l, len = Math.max(2, Math.ceil(l * res) + 1);
  const lanes = {};
  LANE_IDS.forEach((id, i) => {
    const t = new Float32Array(len), spec = o.lanes && o.lanes[id];
    for (let j = 0; j < len; j++) { const u = j / (len - 1); t[j] = spec === undefined ? LANE_DEF[i] : typeof spec === 'function' ? spec(u) : spec; }
    lanes[id] = t;
  });
  const id = o.id || ('n' + o.p + '_' + o.s);
  return {
    id, idHash: SE.idHash(id), p: o.p, s: o.s, l, vel: o.vel == null ? 1 : o.vel,
    engine: o.engine || 'voice', lock: !!o.lock, motion: o.motion || 'forward', travel: o.travel || 'speed',
    start: Object.assign({ mode: 'region', pos: 0, range: 0, seed: 0 }, o.start || {}),
    tape: !!o.tape, rev: !!o.rev, jump: o.jump || 0.5, restart: !!o.restart,
    glide: o.glide || null, chain: o.chain || { id, index: 0, prevId: null, nextId: null }, lanes,
  };
}
const mkSnap = (notes, bars = 4) => ({ version: 1, bars, beatsPerBar: 4, res: 256, notes });
function chainNotes(list) { list.forEach((n, i) => { n.chain = { id: list[0].id, index: i, prevId: i ? list[i - 1].id : null, nextId: i < list.length - 1 ? list[i + 1].id : null }; }); return list; }
const beatSamples = (beat, bpm, sr = SR) => beat * 60 / bpm * sr;
function playClip(src, notes, instPatch, opts = {}) {
  const e = mkEngine(src, instPatch, opts.sr || SR);
  e.setSnapshot(mkSnap(notes, opts.bars || 4));
  e.setTransport({ playing: true, bpm: opts.bpm || 120, beat: opts.beat || 0, loop: !!opts.loop });
  return e;
}

// ------------------------------------------------------------------ measurement
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0; for (let j = 0; j < len / 2; j++) { const a = i + j, b = a + len / 2; const xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr; re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } }
  }
}
function spectrum(x, start, order) {
  const n = 1 << order, re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < n; i++) { const t = 2 * Math.PI * i / (n - 1); re[i] = (x[start + i] || 0) * (0.35875 - 0.48829 * Math.cos(t) + 0.14128 * Math.cos(2 * t) - 0.01168 * Math.cos(3 * t)); }
  fft(re, im);
  const m = new Float64Array(n / 2); for (let k = 0; k < n / 2; k++) m[k] = Math.hypot(re[k], im[k]);
  return m;
}
// strongest spectral peak in fLo..fHi (parabolic interpolation in dB)
function peakHz(x, sr, fLo = 40, fHi = 6000, order = 16, start) {
  const n = 1 << order, st = start == null ? x.length - n : start, m = spectrum(x, st, order), bin = sr / n;
  let bk = -1; for (let k = Math.max(2, Math.floor(fLo / bin)); k <= Math.min(n / 2 - 2, Math.ceil(fHi / bin)); k++) if (bk < 0 || m[k] > m[bk]) bk = k;
  const a = 20 * Math.log10(m[bk - 1] + 1e-30), b = 20 * Math.log10(m[bk] + 1e-30), c = 20 * Math.log10(m[bk + 1] + 1e-30), den = a - 2 * b + c;
  return (bk + (Math.abs(den) > 1e-12 ? 0.5 * (a - c) / den : 0)) * bin;
}
// zero-crossing instantaneous frequency of a sine-like signal: [{ t (samples), f (Hz) }]
function zcFreq(x, sr, a = 0, b = x.length) {
  const zc = [];
  for (let i = Math.max(1, a); i < b; i++) if (x[i - 1] < 0 && x[i] >= 0) zc.push(i - 1 + (-x[i - 1]) / (x[i] - x[i - 1]));
  const out = []; for (let k = 1; k < zc.length; k++) out.push({ t: 0.5 * (zc[k] + zc[k - 1]), f: sr / (zc[k] - zc[k - 1]) });
  return out;
}
function rms(x, a = 0, b = x.length) { a = Math.max(0, Math.round(a)); b = Math.min(x.length, Math.round(b)); let s = 0; for (let i = a; i < b; i++) s += x[i] * x[i]; return Math.sqrt(s / Math.max(1, b - a)); }
function peakAbs(x) { let p = 0; for (let i = 0; i < x.length; i++) { const v = Math.abs(x[i]); if (!(v <= p)) p = v; } return p; }
function badCount(o) { let b = 0; for (let i = 0; i < o.L.length; i++) if (!Number.isFinite(o.L[i]) || !Number.isFinite(o.R[i])) b++; return b; }
function maxDiff(a, b, from = 0) { if (a.length !== b.length) return Infinity; let d = 0; for (let i = from; i < a.length; i++) { const q = Math.abs(a[i] - b[i]); if (!(q <= d)) d = q; } return d; }
function max2nd(x, a = 2, b = x.length) { let m = 0; for (let i = Math.max(2, a); i < Math.min(x.length, b); i++) m = Math.max(m, Math.abs(x[i] - 2 * x[i - 1] + x[i - 2])); return m; }
const db = r => 20 * Math.log10(r);
function corr(a, b) { let ab = 0, aa = 0, bb = 0; for (let i = 0; i < a.length; i++) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; } return ab / Math.sqrt(Math.max(1e-30, aa * bb)); }
// JSON of anything, typed arrays expanded (for "input unmodified" checks)
const J = x => JSON.stringify(x, (k, v) => (ArrayBuffer.isView(v) ? Array.from(v) : (typeof v === 'number' && !Number.isFinite(v) ? String(v) : v)));
function deepFreeze(o) { if (o && typeof o === 'object' && !ArrayBuffer.isView(o) && !Object.isFrozen(o)) { Object.freeze(o); for (const k of Object.keys(o)) deepFreeze(o[k]); } return o; }

// speed bench, run in a fresh child process so the JIT has not seen the garbage-input tests (plain Arrays etc.)
function benchStereo() {
  const S = require(path.join(__dirname, '..', 'src', 'sl-sounds.js'));
  const pad = S.make('pad'), out = [];
  for (const [eng, lock] of [['tape', false], ['voice', false], ['cloud', false], ['cloud', true]]) {
    const xs = [];
    for (let rep = 0; rep < 2; rep++) {
      const e = new SE.Engine({ sampleRate: SR }); e.setInst(mkInst({ engine: eng, lock, heads: 2, spread: 0.3, refresh: { mode: 'free', ms: 80 } })); e.setSample(pad);
      for (let i = 0; i < 8; i++) e.noteOn(40 + i * 3, 1);
      const t0 = process.hrtime.bigint(); render(e, 5 * SR); xs.push(5 / (Number(process.hrtime.bigint() - t0) / 1e9));
    }
    out.push({ name: lock ? 'cloud+lock' : eng, xs });
  }
  return out;
}
if (process.argv.includes('--bench-child')) { console.log(JSON.stringify(benchStereo())); process.exit(0); }

// ================================================================== A. shared definitions against the blueprint text
section('A. shared definitions match BLUEPRINT 1.2-1.5 literally; the module runs with no outside names', () => {
  const BP = { engine: 'voice', lock: false, motion: 'forward', speed: 1, heads: 1, spread: 0.25,
    grainVoice: 2, grainCloud: 140, grainLock: 4,
    refresh: { mode: 'smooth', ms: 120, rate: '1/16', lockToBeat: true, skip: 0 },
    spray: 0, pitch: 0, tape: false, rev: false, drift: 0, headTune: 'same',
    region: { start: 0, end: 1, atEnd: 'loop', xfadeMs: 10 }, jump: '1/8',
    macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5], offsets: { amp: 0, vib: 0, tone: 0, formant: 0, hollow: 0 },
    glideMode: 'legato', glideTime: 0.5, vibRate: 5.5, root: 60, cents: 0, key: 0, scale: 'Chromatic', scaleLock: false,
    velSens: 0.7, polyphony: 16, outGain: 0, bendRange: 2, mod1: { target: 'vib', cc: 1 }, mod2: { target: 'tone', cc: 2 }, liveLegato: false, seed: 1 };
  const canon = o => JSON.stringify(o, (k, v) => (v && typeof v === 'object' && !Array.isArray(v) ? Object.keys(v).sort().reduce((a, kk) => (a[kk] = v[kk], a), {}) : v));
  check('defaultInst() = BLUEPRINT 1.4 literal (every key and value)', canon(SE.defaultInst()) === canon(BP), canon(SE.defaultInst()) === canon(BP) ? 'identical' : canon(SE.defaultInst()), 'identical');
  check('normInst(defaultInst()) keeps the blueprint defaults (no silent re-default)', canon(SE.normInst(SE.defaultInst())) === canon(BP), canon(SE.normInst(SE.defaultInst())) === canon(BP) ? 'identical' : 'differs', 'identical');
  const ids = ['pitch', 'amp', 'vib', 'tone', 'formant', 'hollow', 'travel', 'heads', 'grain', 'refresh'], defs = [0.5, 0.8, 0, 1.0, 0.5, 0, 0.75, 0.5, 0.5, 0.5];
  check('lane registry order and defaults = BLUEPRINT 1.2 table', ids.join() === Array.from(SE.LANE_IDS).join() && defs.join() === Array.from(SE.LANE_DEF).join(), Array.from(SE.LANE_IDS).join() + ' / ' + Array.from(SE.LANE_DEF).join(), ids.join() + ' / ' + defs.join());
  // travel 1.3, 100001 points, exact formula
  const f = v => (v >= 0.48 && v <= 0.52 ? 0 : v > 0.52 ? Math.min(8, 2 ** (((v - 0.75) / 0.23) * 3)) : -Math.min(8, 2 ** (((0.25 - v) / 0.23) * 3)));
  let worst = 0; for (let i = 0; i <= 100000; i++) { const v = i / 100000; worst = Math.max(worst, Math.abs(SE.travelSpeed(v) - f(v)) / Math.max(1, Math.abs(f(v)))); }
  check('travelSpeed = BLUEPRINT 1.3 formula at 100001 points', worst < 1e-12, 'max relative error ' + worst.toExponential(2), '< 1e-12');
  // rates 1.5
  const base = { '1bar': 4, '1/2': 2, '1/4': 1, '1/8': 0.5, '1/16': 0.25, '1/32': 0.125, '1/64': 0.0625 };
  let rw = 0; for (const [k, v] of Object.entries(base)) rw = Math.max(rw, Math.abs(SE.rateBeats(k) - v), Math.abs(SE.rateBeats(k + 'T') - v * 2 / 3), Math.abs(SE.rateBeats(k + 'D') - v * 1.5));
  const odd = [NaN, -1, 0, Infinity, null, undefined, 'x', '1/3', {}].map(v => SE.rateBeats(v));
  check('rateBeats: 21 ids exact; junk ids give a finite positive beat length', rw < 1e-15 && odd.every(v => Number.isFinite(v) && v > 0), 'max error ' + rw + '; junk -> ' + odd.join(','), 'exact; finite > 0');
  // the module text in an EMPTY context (only JS builtins): constructs, plays and renders
  let vmOk = 'not run';
  try {
    const ctx = vm.createContext({});
    const M = vm.runInContext('(' + createStrandEngineModule.toString() + ')()', ctx);
    const e = new M.Engine({ sampleRate: 48000 });
    e.setInst(M.defaultInst());
    e.setSample({ channels: [sine(261.6256, 1, 48000)], sampleRate: 48000 });
    e.noteOn(60, 1);
    const L = new Float32Array(4800), R = new Float32Array(4800); e.process(L, R, 4800);
    vmOk = rms(L) > 0.01 ? 'renders, rms ' + f2(rms(L), 3) : 'silent';
  } catch (err) { vmOk = 'threw: ' + err.message; }
  check('createStrandEngineModule source runs in an empty vm context (worklet-like: no window, no console, no require)', /^renders/.test(vmOk), vmOk, 'renders');
  let glueOk = 'not run';
  try {
    let reg = null;
    const ctx = vm.createContext({ registerProcessor: (n, c) => { reg = { n, c }; }, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: 44100 });
    vm.runInContext(createStrandEngineModule.toString() + '\n' + strandWorkletGlue.toString() + '\nstrandWorkletGlue();', ctx);
    const p = new reg.c(); const o = [[new Float32Array(128), new Float32Array(128)]]; p.process([], o);
    glueOk = reg.n === 'strand-engine' ? 'registered and processed' : 'wrong name ' + reg.n;
  } catch (err) { glueOk = 'threw: ' + err.message; }
  check('worklet glue runs in a vm context holding only registerProcessor / AudioWorkletProcessor / sampleRate', glueOk === 'registered and processed', glueOk, 'registered and processed');
  // setInst with a deep-frozen inst: no throw, no mutation
  const fz = deepFreeze(mkInst({ engine: 'cloud', lock: true, heads: 3.5, region: { start: 0.2, end: 0.6 } })), before = J(fz);
  let thr = null; try { const e = mkEngine(sine(261.6, 1, SR), {}); e.setInst(fz); e.noteOn(60, 1); render(e, 2400); } catch (err) { thr = err.message; }
  check('setInst accepts a deep-frozen inst and leaves it unchanged', thr === null && J(fz) === before, thr || 'ok, unchanged', 'no throw, unchanged');
});

// ================================================================== B. pitch formula (BLUEPRINT 3.3 Pitch)
section('B. pitch: inst.pitch, root + cents, pitch lane, bend, Speed vs pitch per engine, sample-rate ratios', () => {
  const rows = [];
  const meas = (label, out, fExp, tol, from) => {
    const f = peakHz(out.L, SR, 40, 6000, 16, from);
    const c = centsOf(f, fExp); rows.push(c);
    check(label, Math.abs(c) <= tol, `${f2(f, 3)} Hz = ${f2(c, 2)} cents from ${f2(fExp, 3)} Hz`, '+-' + tol + ' cents');
  };
  const c3 = sine(midiHz(60), 4, SR);
  { const e = mkEngine(c3, { engine: 'tape', pitch: 7 }); e.noteOn(60, 1); meas('Tape live, inst.pitch +7: plays G3', render(e, 1.6 * SR), midiHz(67), 3); }
  { const e = playClip(c3, [mkNote({ id: 'p', p: 60, s: 0, l: 8, engine: 'voice' })], { engine: 'voice', pitch: 7 }); meas('Voice clip, inst.pitch +7: plays G3', render(e, 1.6 * SR), midiHz(67), 3); }
  // root + cents: the source sits at C3 +50 cents; with root 60 cents +50, the played C3 must be a true C3
  const c3p = sine(midiHz(60.5), 4, SR);
  for (const eng of ['tape', 'voice']) { const e = mkEngine(c3p, { engine: eng, root: 60, cents: 50 }); e.noteOn(60, 1); meas(`${eng}: source at C3+50c, root C3 cents +50, key C3 -> true C3`, render(e, 1.6 * SR), midiHz(60), 3); }
  { const c4 = sine(midiHz(72), 4, SR); const e = mkEngine(c4, { engine: 'tape', root: 72 }); e.noteOn(60, 1); meas('Tape: C4 source with root 72, key C3 -> C3 (an octave down)', render(e, 1.6 * SR), midiHz(60), 3); }
  // pitch lane through a Float32 table: (v-0.5)*24 semitones
  for (const [v, st] of [[0.5 + 1 / 24, 1], [1, 12], [0, -12], [0.5 - 5 / 24, -5]]) {
    const e = playClip(c3, [mkNote({ id: 'pl', p: 60, s: 0, l: 8, engine: 'tape', lanes: { pitch: v } })], { engine: 'tape' });
    meas(`Tape clip: pitch lane ${f2(v, 4)} -> ${st > 0 ? '+' : ''}${st} st`, render(e, 1.6 * SR), midiHz(60 + st), 3);
  }
  // bend (live) = bend * bendRange semitones
  for (const [bend, range, st] of [[1, 2, 2], [-1, 2, -2], [1, 7, 7], [0.5, 12, 6]]) {
    const e = mkEngine(c3, { engine: 'tape', bendRange: range }); e.noteOn(60, 1); e.setController('bend', bend);
    meas(`Tape live: bend ${bend} x bendRange ${range} -> ${st} st`, render(e, 1.6 * SR), midiHz(60 + st), 3);
  }
  // Speed couples with pitch only in Tape (and in Cloud with the Tape switch)
  { const e = mkEngine(c3, { engine: 'tape', speed: 2 }); e.noteOn(60, 1); meas('Tape: inst.speed 2 plays an octave up (speed and pitch go together)', render(e, 1.0 * SR), midiHz(72), 3); }
  { const e = mkEngine(c3, { engine: 'tape', macros: [0.75, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }); e.noteOn(60, 1); meas('Tape: Speed macro 0.75 (x2) plays an octave up', render(e, 1.0 * SR), midiHz(72), 3); }
  { const e = mkEngine(sine(midiHz(60), 8, SR), { engine: 'voice', speed: 2 }); e.noteOn(60, 1); meas('Voice: inst.speed 2 keeps the pitch', render(e, 1.6 * SR), midiHz(60), 3); }
  // plain Cloud grains are not phase-aligned: with the head at 2x the strongest line sits on the comb f*speed + m/hop,
  // up to 1/(2*hop) = 10.7 Hz (about 70 cents at C3) from f. The check only asks "no octave jump" here.
  { const e = mkEngine(sine(midiHz(60), 8, SR), { engine: 'cloud', speed: 2, tape: false }); e.noteOn(60, 1); meas('Cloud, Tape switch off: speed 2 keeps the pitch (granular smear allowed)', render(e, 1.6 * SR), midiHz(60), 75); }
  { const e = mkEngine(sine(midiHz(60), 8, SR), { engine: 'cloud', speed: 2, tape: true }); e.noteOn(60, 1); meas('Cloud, Tape switch on: speed 2 reads grains x2 (octave up)', render(e, 1.6 * SR), midiHz(72), 10); }
  // a 96 kHz source in a 44.1 kHz engine (the opposite ratio from the builder's test)
  const s96 = sine(midiHz(60), 3, 96000);
  for (const eng of ['tape', 'voice']) {
    const e = new SE.Engine({ sampleRate: 44100 }); e.setInst(mkInst({ engine: eng })); e.setSample({ channels: [s96], sampleRate: 96000 }); e.noteOn(67, 1);
    const o = render(e, 1.6 * 44100); const f = peakHz(o.L, 44100, 40, 6000, 16); const c = centsOf(f, midiHz(67)); rows.push(c);
    check(`${eng}: 96 kHz source in a 44.1 kHz engine, key G3`, Math.abs(c) <= 3, `${f2(f, 3)} Hz = ${f2(c, 2)} cents`, '+-3 cents');
  }
  info('pitch error distribution (cents): ' + dist(rows, 3));
});

// ================================================================== C. vibrato (BLUEPRINT 1.2: depth v*1 semitone at inst.vibRate)
section('C. vibrato: lane depth in semitones at inst.vibRate', () => {
  const src = sine(midiHz(60), 4, SR);
  const vibOf = (e, secs = 1.6) => {
    const o = render(e, secs * SR);
    const zf = zcFreq(o.L, SR, Math.round(0.2 * SR), o.L.length).map(z => ({ t: z.t, c: centsOf(z.f, midiHz(60)) }));
    const cs = zf.map(z => z.c), lo = Math.min(...cs), hi = Math.max(...cs);
    const mean = cs.reduce((a, b) => a + b, 0) / cs.length;
    let ups = []; for (let k = 1; k < zf.length; k++) if (zf[k - 1].c < mean && zf[k].c >= mean) ups.push(zf[k].t);
    const rate = ups.length > 2 ? SR * (ups.length - 1) / (ups[ups.length - 1] - ups[0]) : 0;
    return { lo, hi, rate };
  };
  { const e = mkEngine(src, { engine: 'tape', offsets: { vib: 1 } }); e.noteOn(60, 1); const v = vibOf(e);
    check('live Tape, Vibrato lane 1 (offset +1): swings +-1 semitone', Math.abs(v.hi - 100) < 12 && Math.abs(v.lo + 100) < 12, `${f2(v.lo, 1)} .. ${f2(v.hi, 1)} cents`, '-100 .. +100 (+-12)');
    check('live Tape, Vibrato at inst.vibRate 5.5 Hz', Math.abs(v.rate - 5.5) < 0.15, f2(v.rate, 3) + ' Hz', '5.5 +- 0.15 Hz'); }
  { const e = mkEngine(src, { engine: 'tape', offsets: { vib: 1 }, vibRate: 2 }); e.noteOn(60, 1); const v = vibOf(e, 2.4);
    check('vibRate 2 Hz is followed', Math.abs(v.rate - 2) < 0.1, f2(v.rate, 3) + ' Hz', '2 +- 0.1 Hz'); }
  { const e = playClip(src, [mkNote({ id: 'v', p: 60, s: 0, l: 8, engine: 'tape', lanes: { vib: 0.25 } })], { engine: 'tape' }); const v = vibOf(e);
    check('clip Tape, Vibrato lane 0.25: swings +-25 cents', Math.abs(v.hi - 25) < 5 && Math.abs(v.lo + 25) < 5, `${f2(v.lo, 1)} .. ${f2(v.hi, 1)} cents`, '-25 .. +25 (+-5)'); }
  { const e = mkEngine(src, { engine: 'tape' }); e.noteOn(60, 1); e.setController('mod1', 0.5); const v = vibOf(e);
    check('mod1 (default target Vibrato) at 0.5 gives +-50 cents on a live note', Math.abs(v.hi - 50) < 7 && Math.abs(v.lo + 50) < 7, `${f2(v.lo, 1)} .. ${f2(v.hi, 1)} cents`, '-50 .. +50 (+-7)'); }
});

// ================================================================== D. level and filters (BLUEPRINT 1.2 amp/tone/formant/hollow, 1.4 macros, 3.3 output)
section('D. level, velocity and the voice filters at their blueprint numbers', () => {
  const level = (fHz, patch, vel = 1, lanes) => {
    const s = sine(fHz, 2, SR);
    const e = lanes ? playClip(s, [mkNote({ id: 'lv', p: 60, s: 0, l: 4, engine: 'tape', vel, lanes })], Object.assign({ engine: 'tape' }, patch)) : mkEngine(s, Object.assign({ engine: 'tape' }, patch));
    if (!lanes) e.noteOn(60, vel);
    const o = render(e, 1.2 * SR);
    return rms(o.L, 0.4 * SR, 1.2 * SR) / rms(s, 0.4 * SR, 1.2 * SR);
  };
  const g1 = level(1000, {});
  check('Volume default 0.8: gain v*v*1.3 = 0.832 at velocity 1', Math.abs(g1 - 0.832) < 0.004, f2(g1, 4), '0.832 +- 0.004');
  const gA = level(1000, { offsets: { amp: -0.3 } });
  check('Volume offset -0.3 (lane 0.5): gain 0.325', Math.abs(gA - 0.325) < 0.003, f2(gA, 4), '0.325 +- 0.003');
  const g0 = level(1000, {}, 1, { amp: 0 });
  check('Volume lane 0: silent', g0 < 1e-6, g0.toExponential(2), '< 1e-6');
  { const s = sine(1000, 2, SR, 0.3); const a = mkEngine(s, { engine: 'tape' }), b = mkEngine(s, { engine: 'tape', outGain: -20 * Math.log10(2) });
    a.noteOn(60, 1); b.noteOn(60, 1); const oa = render(a, SR), ob = render(b, SR); let w = 0; for (let i = 0; i < oa.L.length; i++) w = Math.max(w, Math.abs(ob.L[i] - 0.5 * oa.L[i]));
    check('outGain -6.02 dB halves the output exactly (below the limiter)', w < 1e-6, 'max |b - a/2| ' + w.toExponential(2), '< 1e-6'); }
  const v03s0 = level(1000, { velSens: 0 }, 0.3), v03s1 = level(1000, { velSens: 1 }, 0.3), v07 = level(1000, {}, 0.7), v03 = level(1000, {}, 0.3);
  check('velSens 0: velocity has no effect', Math.abs(v03s0 - g1) < 1e-4, `vel 0.3 ${f2(v03s0, 4)} vs vel 1 ${f2(g1, 4)}`, 'equal');
  check('velSens 0.7: softer velocity is quieter, and velSens 1 is quieter still', v03 < v07 && v07 < g1 && v03s1 < v03, `vel 0.3/0.7/1 = ${f2(v03, 3)}/${f2(v07, 3)}/${f2(g1, 3)}, velSens 1 vel 0.3 = ${f2(v03s1, 3)}`, 'monotonic');
  { const s = sine(1000, 1, SR); const a = mkEngine(s, { engine: 'tape' }), b = mkEngine(s, { engine: 'tape' }); a.noteOn(60, 64); b.noteOn(60, 64 / 127);
    check('noteOn velocity 64 (MIDI) = 64/127', maxDiff(render(a, 0.3 * SR).L, render(b, 0.3 * SR).L) === 0, 'identical', 'identical'); }
  // Hollow: peaking cut 950 Hz, Q 0.8, gain -v*28 dB
  const h1 = db(level(950, { offsets: { hollow: 1 } }) / level(950, {})), h05 = db(level(950, { offsets: { hollow: 0.5 } }) / level(950, {}));
  const hFar = db(level(7600, { offsets: { hollow: 1 } }) / level(7600, {}));
  check('Hollow lane 1: -28 dB at 950 Hz', Math.abs(h1 + 28) < 0.5, f2(h1, 2) + ' dB', '-28 +- 0.5 dB');
  check('Hollow lane 0.5: -14 dB at 950 Hz', Math.abs(h05 + 14) < 0.5, f2(h05, 2) + ' dB', '-14 +- 0.5 dB');
  check('Hollow is a peaking cut (3 octaves up the cut is small)', hFar > -6, f2(hFar, 2) + ' dB at 7.6 kHz', '> -6 dB');
  // Tone: low-pass 60*2^(v*8.3) Hz; lane 0.5 -> 1065 Hz; Butterworth-like -3 dB at the cutoff
  const fc = 60 * Math.pow(2, 0.5 * 8.3);
  const t05 = db(level(fc, { offsets: { tone: -0.5 } }) / level(fc, {})), t05m = db(level(fc, { macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0, 0.5] }) / level(fc, {}));
  const t2oct = db(level(4 * fc, { offsets: { tone: -0.5 } }) / level(4 * fc, {}));
  check(`Tone lane 0.5 (offset -0.5): -3 dB at its cutoff ${f2(fc, 0)} Hz`, Math.abs(t05 + 3.01) < 0.3, f2(t05, 2) + ' dB', '-3.0 +- 0.3 dB');
  check('Tone macro 0 (lane 1.0 - 0.5) = Tone lane 0.5', Math.abs(t05m - t05) < 0.05, f2(t05m, 2) + ' dB', f2(t05, 2) + ' dB');
  check('Tone lane 0.5: two octaves above the cutoff is well down (12 dB/oct)', t2oct < -20, f2(t2oct, 1) + ' dB at ' + f2(4 * fc, 0) + ' Hz', '< -20 dB');
  // Formant macro on Tape: lane 0.5 + (m - 0.5) -> peaking EQ 250*2^(v*3.8), gain 18*|v-0.5|
  const fm = 250 * Math.pow(2, 0.8 * 3.8);
  const fmac = db(level(fm, { macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.8] }) / level(fm, {}));
  check('Formant macro 0.8 on Tape: +5.4 dB at 250*2^(0.8*3.8) Hz', Math.abs(fmac - 5.4) < 0.3, f2(fmac, 2) + ' dB at ' + f2(fm, 0) + ' Hz', '5.4 +- 0.3 dB');
});

// ================================================================== E. heads, spread, pan
section('E. Heads: level 1/sqrt(H), fractional heads, Spread offsets i/H*spread, macros, Float32 lanes, pan', () => {
  const ramp = rampSource(8, SR);
  const disp = (patch, secs = 0.05, lanes) => {
    const e = lanes ? playClip(ramp, [mkNote({ id: 'hd', p: 60, s: 0, l: 8, engine: 'voice', lanes })], Object.assign({ engine: 'voice' }, patch)) : mkEngine(ramp, Object.assign({ engine: 'voice' }, patch));
    if (!lanes) e.noteOn(60, 1);
    render(e, secs * SR);
    return e.getDisplay().voices[0].heads;
  };
  { const h = disp({ heads: 4, spread: 0 }); const lv = h.map(x => x.level);
    check('4 heads, spread 0: each head at level 1/sqrt(4) = 0.5', h.length === 4 && lv.every(l => Math.abs(l - 0.5) < 1e-3), lv.map(l => f2(l, 4)).join(' '), '0.5 x4'); }
  { const h = disp({ heads: 2.5, spread: 0 }); const lv = h.map(x => x.level), s = 1 / Math.sqrt(2.5);
    check('Heads 2.5: 2 full heads + a third at half level (x 1/sqrt(2.5))', h.length === 3 && Math.abs(lv[0] - s) < 1e-3 && Math.abs(lv[1] - s) < 1e-3 && Math.abs(lv[2] - s / 2) < 1e-3, lv.map(l => f2(l, 4)).join(' '), `${f2(s, 4)} ${f2(s, 4)} ${f2(s / 2, 4)}`); }
  const offs = h => h.map(x => x.pos - h[0].pos);
  const tinyMax = 0.008 / 8 + 1e-4;   // heads 2..8 may sit up to 8 ms further in; the ramp file is 8 s
  { const o = offs(disp({ heads: 4, spread: 0.5 })), exp = [0, 0.125, 0.25, 0.375];
    const err = o.map((v, i) => v - exp[i]);
    check('Spread 0.5, 4 heads: head i starts i/4*0.5 of the Region further in', err.every(e => e >= -1e-4 && e <= tinyMax), 'offset errors ' + err.map(e => f2(e, 5)).join(' '), '0 .. ' + f2(tinyMax, 5)); }
  { const o = offs(disp({ heads: 4, spread: 0.25, macros: [0.5, 0.75, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5] })), exp = [0, 0.1875, 0.375, 0.5625];
    const err = o.map((v, i) => v - exp[i]);
    check('Spread macro 0.75 adds +0.5 (0.25 -> 0.75)', err.every(e => e >= -1e-4 && e <= tinyMax), 'offset errors ' + err.map(e => f2(e, 5)).join(' '), '0 .. ' + f2(tinyMax, 5)); }
  { const a = disp({ heads: 1, macros: [0.5, 0.5, 1, 0.5, 0.5, 0.5, 0.5, 0.5] }).length, b = disp({ heads: 8, macros: [0.5, 0.5, 0, 0.5, 0.5, 0.5, 0.5, 0.5] }).length;
    const c = disp({ heads: 8, macros: [0.5, 0.5, 1, 0.5, 0.5, 0.5, 0.5, 0.5] }, 0.05, { heads: 1 }).length;
    check('Heads macro: 1 -> +7 heads (1 -> 8), 0 -> -7 heads (8 -> 1); inst 8 + lane 1 + macro 1 clamps at 8', a === 8 && b === 1 && c === 8, `${a}, ${b}, ${c}`, '8, 1, 8'); }
  // a whole number of heads drawn on the Heads lane, as SLModel's Float32 tables carry it
  const counts = [];
  for (let k = 1; k <= 7; k++) { const h = disp({ heads: 1 }, 0.1, { heads: 0.5 + k / 14 }); counts.push(h.length); }
  const want = [2, 3, 4, 5, 6, 7, 8];
  check('Heads lane at a whole count (Float32 table, inst.heads 1): exactly that many heads sound', counts.every((c, i) => c === want[i]), 'heads shown ' + counts.join(' '), want.join(' '));
  { const h = disp({ heads: 1 }, 0.1, { heads: 0.5 + 1 / 14 }); const e = 1 + (Math.fround(0.5 + 1 / 14) - 0.5) * 14;
    info(`Heads lane "2 heads" reads back as H = ${e.toPrecision(10)} from the Float32 table; heads shown ${h.length}, levels ${h.map(x => x.level.toExponential(2)).join(' ')}`); }
  // pan: a mono source whose first half is 300 Hz and second half 700 Hz; 2 Tape heads, Spread 1 -> head 2 reads the 700 Hz half
  {
    const n = 4 * SR, s = new Float32Array(n);
    for (let i = 0; i < n; i++) s[i] = 0.4 * Math.sin(2 * Math.PI * (i < n / 2 ? 300 : 700) * i / SR);
    const band = (x, f) => { const m = spectrum(x, Math.round(0.1 * SR), 15), bin = SR / 32768; let e2 = 0; for (let k = Math.floor(f * 0.97 / bin); k <= Math.ceil(f * 1.03 / bin); k++) e2 += m[k] * m[k]; return Math.sqrt(e2); };
    const run = spread => { const e = mkEngine(s, { engine: 'tape', heads: 2, spread }); e.noteOn(60, 1); return render(e, 0.9 * SR); };
    const o = run(1), o0 = run(0);
    const d300 = db(band(o.L, 300) / band(o.R, 300)), d700 = db(band(o.R, 700) / band(o.L, 700));
    check('Spread 1 pans head 1 left and head 2 right (+-0.75)', d300 > 6 && d700 > 6, `300 Hz head: L-R ${f2(d300, 1)} dB; 700 Hz head: R-L ${f2(d700, 1)} dB`, 'both > 6 dB');
    check('Spread 0: heads centred (L == R on a mono source)', maxDiff(o0.L, o0.R) === 0, 'max |L-R| ' + maxDiff(o0.L, o0.R), '0');
  }
});

// ================================================================== F. travel speeds as the engine plays them
section('F. Travel: effective head speed = s(v) x inst.speed x macro, capped at 16x; dead band from Float32 tables', () => {
  const n = 30 * SR, src = sine(220, 30, SR, 0.3);
  const speedOf = (travelV, patch, engine = 'voice') => {
    const e = playClip(src, [mkNote({ id: 'tv', p: 60, s: 0, l: 8, engine, lanes: { travel: travelV } })], Object.assign({ engine }, patch));
    render(e, 0.1 * SR); const p0 = e.getDisplay().voices[0].heads[0].pos;
    render(e, 0.05 * SR); const p1 = e.getDisplay().voices[0].heads[0].pos;
    return (p1 - p0) * n / (0.05 * SR);
  };
  const rows = [
    ['lane 0.75, inst.speed 2', 0.75, { speed: 2 }, 2],
    ['lane 0.75, Speed macro 0.75 (x2)', 0.75, { macros: [0.75, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }, 2],
    ['lane 0.75, Speed macro 0.25 (x0.5)', 0.75, { macros: [0.25, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }, 0.5],
    ['lane 0.98 (8x) x inst.speed 4: capped at 16x', 0.98, { speed: 4 }, 16],
    ['lane 0.02 (-8x) x inst.speed 4: capped at -16x', 0.02, { speed: 4 }, -16],
    ['lane 0.53', 0.53, {}, Math.pow(2, ((0.53 - 0.75) / 0.23) * 3)],
    ['lane 0.49 (dead band)', 0.49, {}, 0],
    ['lane 0.52 (dead band edge)', 0.52, {}, 0],
    ['lane 0.48 (dead band edge, BLUEPRINT 1.3 says 0.48 <= v <= 0.52 is stopped)', 0.48, {}, 0],
  ];
  for (const [label, v, patch, exp] of rows) {
    const s = speedOf(v, patch);
    const ok = exp === 0 ? Math.abs(s) < 1e-6 : Math.abs(s / exp - 1) < 2e-3;
    check('Voice head speed, ' + label, ok, f2(s, 5) + 'x', f2(exp, 5) + 'x');
  }
  info('travelSpeed(Math.fround(0.48)) = ' + SE.travelSpeed(Math.fround(0.48)) + ' (the Float32 value of 0.48 is ' + Math.fround(0.48) + ')');
});

// ================================================================== G. envelope and fade times (BLUEPRINT 3.3: 8 ms, 60 ms, 5 ms steal, 20 ms stop)
section('G. envelope: 8 ms attack, 60 ms release, 5 ms steal, 20 ms stop fade', () => {
  const src = dc(2, SR, 0.5);
  { const e = mkEngine(src, { engine: 'tape' }); e.noteOn(60, 1); const o = render(e, 0.2 * SR);
    const plateau = o.L[Math.round(0.1 * SR)]; let i = 0; while (i < o.L.length && o.L[i] < 0.999 * plateau) i++;
    check('attack reaches the plateau after 8 ms (384 samples)', Math.abs(i - 384) <= 4, i + ' samples', '384 +- 4'); }
  { const e = mkEngine(src, { engine: 'tape' }); e.noteOn(60, 1); const a = render(e, 0.2 * SR); const plateau = a.L[a.L.length - 1];
    e.noteOff(60); const o = render(e, 0.2 * SR);
    let z = 0; while (z < o.L.length && Math.abs(o.L[z]) > 1e-7) z++;
    const half = o.L[1440] / plateau;
    check('release: silent 60 ms (2880 samples) after noteOff', Math.abs(z - 2880) <= 34, z + ' samples', '2880 +- 34');
    check('release is halfway down at 30 ms', Math.abs(half - 0.5) < 0.03, f2(half, 3), '0.5 +- 0.03');
    check('released voice is freed', e.stats().voices === 0, 'voices ' + e.stats().voices, '0'); }
  { const e = mkEngine(src, { engine: 'tape', polyphony: 1 }); e.noteOn(60, 1); render(e, 0.1 * SR); e.noteOn(64, 1);
    let doneAt = -1; render(e, 0.02 * SR, 1, i => { if (doneAt < 0 && e.voices.filter(v => !v.done).length === 1) doneAt = i; });
    check('polyphony 1: the stolen voice fades out in 5 ms (240 samples) (white-box: voice list)', doneAt >= 236 && doneAt <= 244, doneAt + ' samples', '240 +- 4'); }
  { const e = mkEngine(src, { engine: 'tape', region: { start: 0, end: 0.1, atEnd: 'stop' } }); e.noteOn(60, 1); const o = render(e, 0.4 * SR);
    const plateau = o.L[Math.round(0.1 * SR)]; let a = 0; while (a < o.L.length && o.L[a] < 0.999 * plateau) a++;
    let b = a; while (b < o.L.length && o.L[b] > 0.999 * plateau) b++; let c = b; while (c < o.L.length && o.L[c] > 1e-7) c++;
    check('atEnd stop: the fade after the Region end lasts 20 ms (960 samples)', Math.abs((c - b) - 960) <= 12 && Math.abs(b - 9600) <= 34, `fade starts at ${b}, lasts ${c - b} samples`, 'starts ~9600, 960 +- 12'); }
});

// ================================================================== H. loop seam
section('H. Region loop: period = Region length; crossfade level on correlated material; a real starter sound', () => {
  const ramp = rampSource(1, SR);
  { const e = mkEngine(ramp, { engine: 'tape' }); e.noteOn(60, 1); const o = render(e, 3.6 * SR);
    const pos = [0.5, 1.5, 2.5, 3.5].map(t => decodeAt(o, Math.round(t * SR), 8));
    const err = pos.map((p, i) => p - 0.5);
    check('a held note wraps with period = Region length (1 s Region at 1x: at 0.5 / 1.5 / 2.5 / 3.5 s the head is at 0.5)', err.every(x => Math.abs(x) < 0.001),
      'positions ' + pos.map(p => f2(p, 5)).join(' ') + ' (drift per pass ' + f2((pos[3] - pos[0]) / 3 * 1000, 2) + ' ms)', '0.5 +- 0.001 each pass'); }
  { const e = mkEngine(dc(1, SR, 0.5), { engine: 'tape' }); e.noteOn(60, 1); const o = render(e, 2.5 * SR);
    let mx = 0, mn = 9; for (let i = 0.1 * SR; i < o.L.length; i++) { mx = Math.max(mx, o.L[i]); mn = Math.min(mn, o.L[i]); }
    check('loop crossfade on correlated material (a DC source) keeps the level', db(mx / mn) < 1, f2(db(mx / mn), 2) + ' dB swing at the seam', '< 1 dB'); }
  let S = null; try { S = require(path.join(__dirname, '..', 'src', 'sl-sounds.js')); } catch (e) { info('sl-sounds.js not loadable: ' + e.message); }
  if (S) {
    for (const id of ['saw', 'pad']) {
      const s = S.make(id), secs = s.channels[0].length / s.sampleRate;
      const e = new SE.Engine({ sampleRate: SR }); e.setInst(mkInst({ engine: 'tape' })); e.setSample(s); e.noteOn(60, 1);
      const o = render(e, (secs + 0.6) * SR);
      const P = SR / midiHz(60), W = Math.round(4 * P);   // windows of ~4 periods
      const seam = Math.round(((s.channels[0].length - 0.01 * s.sampleRate) / s.sampleRate) * SR);
      const rw = k => rms(o.L, k, k + W);
      const steady = []; for (let k = seam - 1.0 * SR; k < seam - 0.1 * SR; k += W) steady.push(db(rw(k)));
      const near = []; for (let k = seam - 2 * W; k < seam + 0.02 * SR; k += Math.round(W / 4)) near.push(db(rw(k)));
      const sMed = steady.slice().sort((a, b) => a - b)[steady.length >> 1];
      const dev = near.map(v => v - sMed), dS = steady.map(v => v - sMed);
      const swing = Math.max(...dev.map(Math.abs)), sSwing = Math.max(...dS.map(Math.abs));
      check(`starter sound "${id}" (stereo, seamless loop): level around the first loop seam stays within 1.5 dB of its steady spread`, swing <= sSwing + 1.5,
        `seam ${f2(Math.min(...dev), 2)} .. ${f2(Math.max(...dev), 2)} dB; steady ${f2(Math.min(...dS), 2)} .. ${f2(Math.max(...dS), 2)} dB`, 'seam swing <= steady swing + 1.5 dB');
    }
  }
});

// ================================================================== I. timing in loop passes and after a tempo change
section('I. note starts: loop passes 1-3 at 87 and 133 bpm, after a tempo change, live Scatter on the free clock', () => {
  const src = dc(2, SR, 0.5);
  const firstSound = (o, exp) => { let i = Math.max(0, Math.floor(exp) - 8); while (i < o.L.length && o.L[i] === 0) i++; return i - exp; };
  const errs = [];
  for (const bpm of [87, 133]) {
    const ss = [0.37, 1.37, 2.71];
    const notes = ss.map((s, i) => mkNote({ id: 'q' + i, p: 60, s, l: 0.2, engine: ['tape', 'voice', 'cloud'][i] }));
    const e = playClip(src, notes, { engine: 'tape' }, { bpm, bars: 1, loop: true });
    const o = render(e, beatSamples(12, bpm), 100);
    for (let m = 0; m < 3; m++) for (const s of ss) errs.push(firstSound(o, beatSamples(s + 4 * m, bpm)));
  }
  check('loop passes 1-3 at 87 and 133 bpm: first sounding sample - exact beat time', errs.every(x => x >= 0 && x < 1), dist(errs, 3), '0 <= err < 1 sample');
  { // tempo change 120 -> 87 at beat 1.3 (no beat in the message): later notes start on the new grid
    const ss = [2.37, 3.71];
    const e = playClip(src, ss.map((s, i) => mkNote({ id: 't' + i, p: 60, s, l: 0.2, engine: 'tape' })), { engine: 'tape' }, { bpm: 120 });
    const k1 = Math.round(beatSamples(1.3, 120));
    const a = render(e, k1); e.setTransport({ playing: true, bpm: 87 });
    const b = render(e, beatSamples(3, 87));
    const er = ss.map(s => firstSound(b, (s - 1.3) * 60 / 87 * SR));
    check('after a tempo change (120 -> 87 bpm at beat 1.3), notes start on the new grid', er.every(x => x >= 0 && x < 1) && rms(a.L) === 0, er.map(x => f2(x, 3)).join(', ') + ' samples', '0 <= err < 1');
  }
  { // live Scatter: jumps every 1/8T (1/3 beat) on the free-running clock while stopped (120 bpm: 8000 samples)
    const ramp = rampSource(8, SR), rn = ramp.channels[0].length;
    const e = mkEngine(ramp, { engine: 'tape', motion: 'scatter', jump: '1/8T' });
    e.noteOn(60, 1);
    const pos = []; render(e, 2 * SR, 32, () => { const v = e.getDisplay().voices[0]; pos.push(v ? v.heads[0].pos : NaN); });
    const step = 32 / rn;   // 1x travel per 32-sample poll; anything else is a jump (a target can land close by)
    const jumps = []; for (let m = 1; m < pos.length; m++) if (Math.abs(pos[m] - pos[m - 1] - step) > 2e-5) jumps.push(32 * m);
    const grid = []; for (let k = 1; k * 8000 < 2 * SR - 32; k++) grid.push(k * 8000);
    const er = jumps.map(j => { let best = Infinity; for (const g of grid) if (Math.abs(j - g) < Math.abs(best)) best = j - g; return best; });
    check('live Scatter at 1/8T: one jump per 1/3 beat of the free clock, on the grid', jumps.length === grid.length && er.every(x => x >= 0 && x <= 32), `${jumps.length} jumps (grid ${grid.length}); error ${dist(er, 0)}`, 'one per grid line, 0..32 samples');
  }
});

// ================================================================== J. chase
section('J. chase: same sound as playing from the start; block-size independent; transport jumps', () => {
  { // Tape with moving pitch and Volume lanes: chased output = straight output after the 10 ms fade-in
    const x = new Float32Array(10 * SR); for (let i = 0; i < x.length; i++) x[i] = 0.5 * Math.sin(2 * Math.PI * 261.6256 * i / SR) + 0.2 * Math.sin(2 * Math.PI * 523.25 * i / SR + 1);
    const N = () => [mkNote({ id: 'c', p: 60, s: 0, l: 8, engine: 'tape', lanes: { pitch: u => 0.5 + 0.08 * u, amp: u => 0.3 + 0.6 * u } })];
    const a = playClip(x, N(), { engine: 'tape' }), oa = render(a, 2 * SR);
    const b = playClip(x, N(), { engine: 'tape' }, { beat: 3 }), ob = render(b, 0.5 * SR);
    const k0 = Math.round(1.5 * SR); let d = 0; for (let j = 960; j < ob.L.length; j++) d = Math.max(d, Math.abs(oa.L[k0 + j] - ob.L[j]));
    check('Tape chase at beat 3 with moving Pitch and Volume lanes = the straight render (after 20 ms)', d < 1e-4, 'max diff ' + d.toExponential(2), '< 1e-4');
  }
  { // chase, Lock, Scatter, Spray, Refresh skip, a chain: block sizes 1, random 1..700, 4096 vs 128 must agree bit for bit
    const t = new Float32Array(4 * 44100); const nz = noise(4, 44100, 8, 0.2); for (let i = 0; i < t.length; i++) t[i] = 0.4 * Math.sin(2 * Math.PI * 261.6 * i / 44100) + nz[i];
    const notes = () => chainNotes([mkNote({ id: 'a1', p: 60, s: 0, l: 2, engine: 'voice', glide: { d: 5, len: 0.5, curve: -0.2, toId: 'a2' }, lanes: { vib: 0.4 } }), mkNote({ id: 'a2', p: 65, s: 2, l: 1.5, engine: 'cloud', lock: true })])
      .concat([mkNote({ id: 'b', p: 72, s: 0.5, l: 3, engine: 'tape', motion: 'scatter', jump: 0.25 }), mkNote({ id: 'c', p: 48, s: 1, l: 2.5, engine: 'cloud', rev: true, lanes: { heads: 0.64, grain: 0.3 } })]);
    const go = block => { const e = mkEngine({ channels: [t], sampleRate: 44100 }, { refresh: { mode: 'sync', rate: '1/16T', skip: 0.4 }, spray: 0.2, drift: 0.3, heads: 2, spread: 0.3 });
      e.setSnapshot(mkSnap(notes(), 1)); e.setTransport({ playing: true, bpm: 97, beat: 1.7, loop: true }); return render(e, 2.5 * SR, block); };
    const ref = go(128), r = lcg(4242), rnd = go(() => 1 + Math.floor(r() * 700)), one = go(1), big = go(4096);
    const ds = [maxDiff(ref.L, rnd.L), maxDiff(ref.R, rnd.R), maxDiff(ref.L, one.L), maxDiff(ref.L, big.L)];
    check('chased start (beat 1.7, inside a chain): blocks random/1/4096 vs 128 bit-identical', ds.every(d => d === 0) && rms(ref.L) > 0.01, 'max diffs ' + ds.join(', ') + ', rms ' + f2(rms(ref.L), 3), '0, not silent');
  }
  { // a transport jump while playing (beat far away): the clip voice restarts at the new song position
    const ramp = rampSource(8, SR), rn = ramp.channels[0].length;
    const e = playClip(ramp, [mkNote({ id: 'j', p: 60, s: 0, l: 16, engine: 'tape' })], { engine: 'tape' });
    render(e, 0.5 * SR);
    e.setTransport({ playing: true, bpm: 120, beat: 6 });
    const o = render(e, 0.3 * SR);
    const got = decodeAt(o, Math.round(0.2 * SR), 50), exp = (3 * SR + 0.2 * SR) / rn;
    check('setTransport jump to beat 6 while playing: playhead lands where beat 6 + 0.2 s would be', Math.abs(got - exp) < 0.002, f2(got, 5), f2(exp, 5) + ' +- 0.002');
  }
});

// ================================================================== K. determinism and untouched inputs
section('K. determinism under the calls the page makes; inputs left unmodified', () => {
  const x = new Float32Array(4 * SR); { const nz = noise(4, SR, 5, 0.3); for (let i = 0; i < x.length; i++) x[i] = 0.4 * Math.sin(i * 0.034) + nz[i]; }
  const notes = () => [mkNote({ id: 'a', p: 60, s: 0, l: 2, engine: 'voice', lanes: { vib: 0.3 } }), mkNote({ id: 'b', p: 64, s: 0.5, l: 2, engine: 'cloud', lock: true }),
    mkNote({ id: 'c', p: 67, s: 1, l: 2, engine: 'tape', motion: 'scatter', jump: 0.25 }), mkNote({ id: 'd', p: 55, s: 0.25, l: 3, engine: 'cloud', motion: 'mirror', lanes: { heads: 0.6 } })];
  const I = mkInst({ heads: 2.5, refresh: { mode: 'free', ms: 90, skip: 0.2 }, spray: 0.1, drift: 0.3 });
  const go = (churn, pre = 0) => {
    const e = new SE.Engine({ sampleRate: SR }); e.setInst(I); e.setSample({ channels: [x], sampleRate: SR }); e.setSnapshot(mkSnap(notes(), 1));
    if (pre) render(e, pre);
    e.setTransport({ playing: true, bpm: 120, beat: 0, loop: true });
    return render(e, 6 * SR, 128, () => {
      if (churn & 1) e.setInst(JSON.parse(JSON.stringify(I)));
      if (churn & 2) e.setSnapshot(mkSnap(notes(), 1));
      if (churn & 4) e.setTransport({ playing: true, bpm: 120 });
    });
  };
  const A = go(0);
  const dInst = maxDiff(A.L, go(1).L), dSnap = maxDiff(A.L, go(2).L), dTr = maxDiff(A.L, go(4).L);
  check('setInst(same values) on every block changes nothing', dInst === 0, 'max diff ' + dInst, '0');
  check('setSnapshot(an identical recompiled snapshot) on every block changes nothing', dSnap === 0, 'max diff ' + dSnap, '0');
  check('setTransport({playing, bpm}) re-sent unchanged on every block changes nothing', dTr < 1e-6, 'max diff ' + dTr.toExponential(3), '< 1e-6');
  { // the transport starting 17 (or 1000) samples into the engine's life gives the same sound from the transport start
    const d17 = maxDiff(A.L, go(0, 17).L), d1000 = maxDiff(A.L, go(0, 1000).L);
    check('time-shift: starting the transport 17 / 1000 samples later (off the 32-sample grid) changes nothing after the start', d17 === 0 && d1000 === 0, `max diff ${d17} / ${d1000}`, '0'); }
  { // two separate module instances
    const SE2 = createStrandEngineModule();
    const e = new SE2.Engine({ sampleRate: SR }); e.setInst(I); e.setSample({ channels: [x], sampleRate: SR }); e.setSnapshot(mkSnap(notes(), 1)); e.setTransport({ playing: true, bpm: 120, beat: 0, loop: true });
    const C = render(e, 6 * SR);
    check('a second createStrandEngineModule() instance renders the same output', maxDiff(A.L, C.L) === 0, 'max diff ' + maxDiff(A.L, C.L), '0'); }
  { // pass 1 = pass 2 of a 1-bar loop when nothing overlaps (loop pass is not an input, BLUEPRINT 1.6). outGain -18 keeps the
    // limiter idle (an engaged limiter carries its gain into the next pass, which is a separate matter).
    const ramp = rampSource(6, SR);
    const passDiff = len => {
      const N = [mkNote({ id: 'p1', p: 60, s: 0.5, l: len, engine: 'cloud', motion: 'scatter', jump: 0.25, start: { mode: 'random', pos: 0.1, range: 0.6, seed: 3 } }),
        mkNote({ id: 'p2', p: 67, s: 2, l: len, engine: 'cloud', lock: true }), mkNote({ id: 'p3', p: 55, s: 2.2, l: len, engine: 'voice', motion: 'mirror' })];
      const e = playClip(ramp, N, { spray: 0.3, heads: 3, drift: 0.5, refresh: { mode: 'free', ms: 70, skip: 0.5 }, outGain: -18 }, { bars: 1, loop: true });
      const o = render(e, beatSamples(8, 120)); const bar = Math.round(beatSamples(4, 120));
      let d = 0, at = -1; for (let i = 0; i < bar; i++) { const q = Math.abs(o.L[i] - o.L[i + bar]); if (q > d) { d = q; at = i; } }
      return { d, at: at / beatSamples(1, 120), r: rms(o.L) };
    };
    const a = passDiff(1.013), b = passDiff(1);
    check('loop pass 1 = loop pass 2, notes 1.013 beats long (ends between control ticks)', a.d === 0 && a.r > 0.005, 'max diff ' + a.d, '0');
    check('loop pass 1 = loop pass 2, notes exactly 1 beat long (ends land on a control tick at 120 bpm)', b.d === 0 && b.r > 0.005, `max diff ${b.d.toExponential(2)} at beat ${f2(b.at, 3)} of the pass (a release starting one 32-sample tick apart)`, '0'); }
  { // inputs left unmodified
    const L = noise(2, 44100, 11, 0.4), R = noise(2, 44100, 12, 0.4), sJ = J([L, R]);
    const nts = notes(), snap = mkSnap(nts, 1), nJ = J(snap), inst = mkInst({ engine: 'cloud', lock: true, heads: 3, macros: [0.6, 0.5, 0.7, 0.5, 0.5, 0.5, 0.5, 0.5] }), iJ = J(inst);
    const e = new SE.Engine({ sampleRate: SR }); e.setSample({ channels: [L, R], sampleRate: 44100 }); e.setInst(inst); e.setSnapshot(snap);
    e.setTransport({ playing: true, bpm: 120, beat: 0.6, loop: true }); e.noteOn(62, 0.7); e.setController('mod1', 0.7); e.setController('bend', 0.3);
    render(e, 2 * SR); e.setSnapshot(mkSnap([], 1)); render(e, 0.1 * SR);
    check('sample channels, snapshot (notes + Float32 lanes) and inst are unchanged after playing', J([L, R]) === sJ && J(snap) === nJ && J(inst) === iJ,
      `sample ${J([L, R]) === sJ ? 'same' : 'CHANGED'}, snapshot ${J(snap) === nJ ? 'same' : 'CHANGED'}, inst ${J(inst) === iJ ? 'same' : 'CHANGED'}`, 'all same'); }
});

// ================================================================== L. garbage in, nothing bad out
section('L. NaN guards: bad samples, bad inst, bad snapshots, extreme pitch, tiny files, odd process calls', () => {
  const engines = [['tape', false], ['voice', false], ['cloud', false], ['cloud', true]];
  const verdict = o => ({ bad: badCount(o), pk: Math.max(peakAbs(o.L), peakAbs(o.R)), r: rms(o.L) });
  { // NaN and Inf sprinkled through the sample
    const s = sine(261.6256, 3, SR, 0.5); for (let i = 1000; i < s.length; i += 7919) s[i] = i % 2 ? NaN : Infinity; s[5] = -Infinity;
    for (const [eng, lock] of engines) {
      const e = mkEngine(s, { engine: eng, lock, heads: 3 }); e.noteOn(60, 1); const v = verdict(render(e, 1.5 * SR));
      check(`${lock ? 'lock' : eng}: NaN/Inf inside the sample -> finite output, peak <= 0.98, still sounding`, v.bad === 0 && v.pk <= 0.98 && v.r > 0.05, `bad ${v.bad}, peak ${f2(v.pk, 3)}, rms ${f2(v.r, 3)}`, '0, <= 0.98, rms > 0.05');
    }
  }
  { // garbage inst
    const G = { speed: NaN, heads: Infinity, spread: -5, grainVoice: 0, grainCloud: -1, grainLock: 1e9, refresh: { mode: 'free', ms: 0, skip: 7, rate: 'zz' }, spray: NaN, pitch: 1e9,
      drift: 'x', headTune: 'zzz', region: { start: 0.9, end: 0.1, atEnd: 'boing', xfadeMs: 1e9 }, jump: 'nope', macros: [NaN, -1, 2, 'a', null, undefined, Infinity], offsets: { amp: 5, vib: NaN, tone: -9 },
      vibRate: 1e9, root: NaN, cents: 1e9, velSens: -2, polyphony: 0, outGain: 1e9, bendRange: NaN, mod1: { target: 'nothing' }, mod2: null, seed: NaN, motion: 'scatter' };
    for (const [eng, lock] of engines) {
      let v, err = null;
      try { const e = mkEngine(sine(261.6, 2, SR), Object.assign({}, G, { engine: eng, lock })); e.noteOn(60, 1); e.noteOn(NaN, NaN); e.setController('bend', NaN); e.setController('mod1', Infinity); v = verdict(render(e, 1 * SR)); } catch (x) { err = x.message; }
      check(`${lock ? 'lock' : eng}: garbage inst (NaN, Infinity, strings, out of range)`, !err && v.bad === 0 && v.pk <= 0.98, err ? 'threw ' + err : `bad ${v.bad}, peak ${f2(v.pk, 3)}`, 'no throw, finite, <= 0.98');
    }
  }
  { // garbage snapshot
    let err = null, v;
    try {
      const nn = mkNote({ id: 'g1', p: 60, s: 0, l: 2, engine: 'voice' });
      nn.lanes.pitch = Float32Array.from([NaN, Infinity, -5, 7]); nn.lanes.amp = [1.5, -1]; delete nn.lanes.tone; nn.lanes.heads = new Float32Array(0);
      const g2 = mkNote({ id: 'g2', p: 64.5, s: 0.5, l: 1e-9, engine: 'warp', motion: 'spin', travel: 'teleport' });
      const g3 = mkNote({ id: 'g3', p: 67, s: 1, l: 2, engine: 'cloud', lock: true, glide: { d: Infinity, len: Infinity, curve: NaN, toId: 'nobody' } });
      g3.chain = { id: 'g3', index: 0, prevId: 'g4', nextId: 'g4' };
      const g4 = mkNote({ id: 'g4', p: 69, s: 1.5, l: 1, engine: 'tape' }); g4.chain = { id: 'g3', index: 1, prevId: 'g3', nextId: 'g3' };
      const g5 = mkNote({ id: 'g1', p: 48, s: -1, l: 3, engine: 'tape', start: { mode: 'random', pos: NaN, range: Infinity, seed: 'x' } });
      const g6 = Object.assign(mkNote({ id: 'g6', p: 50, s: 2, l: 1 }), { vel: NaN, jump: -1, start: null, lanes: null });
      const snap = { version: 1, bars: NaN, beatsPerBar: 0, res: -3, notes: [null, undefined, {}, { id: 'x', p: NaN, s: 0, l: 1 }, nn, g2, g3, g4, g5, g6] };
      const e = mkEngine(sine(261.6, 2, SR), { heads: 3 }); e.setSnapshot(snap); e.setTransport({ playing: true, bpm: 140, beat: 0.25, loop: true });
      v = verdict(render(e, 3 * SR));
    } catch (x) { err = x.message; }
    check('garbage snapshot (null notes, NaN/Inf/out-of-range/short/plain lanes, bad glide, cyclic chain, duplicate ids, missing fields)', !err && v.bad === 0 && v.pk <= 0.98, err ? 'threw ' + err : `bad ${v.bad}, peak ${f2(v.pk, 3)}, rms ${f2(v.r, 3)}`, 'no throw, finite, <= 0.98');
  }
  { // extreme pitch: key 0 and 127, pitch lane 0/1, inst.pitch +-48, bend +-1 x 48
    for (const [eng, lock] of engines) {
      let worst = { bad: 0, pk: 0 }, err = null;
      try {
        for (const [p, lane, ip, bend] of [[0, 0, -48, -1], [127, 1, 48, 1], [127, 0.5, 0, 0], [0, 0.5, 0, 0]]) {
          const e = playClip(sine(261.6, 2, SR), [mkNote({ id: 'x', p, s: 0, l: 2, engine: eng, lock, lanes: { pitch: lane } })], { engine: eng, lock, pitch: ip, heads: 2, bendRange: 48 });
          e.noteOn(p, 1); e.setController('bend', bend);
          const v = verdict(render(e, 0.6 * SR)); worst.bad += v.bad; worst.pk = Math.max(worst.pk, v.pk);
        }
      } catch (x) { err = x.message; }
      check(`${lock ? 'lock' : eng}: keys 0/127 with pitch lane, inst.pitch +-48 and bend +-48`, !err && worst.bad === 0 && worst.pk <= 0.98, err ? 'threw ' + err : `bad ${worst.bad}, peak ${f2(worst.pk, 3)}`, 'finite, <= 0.98');
    }
  }
  { // tiny files and degenerate Regions
    let err = null, bad = 0, pk = 0;
    try {
      for (const [len, region] of [[8, {}], [9, { start: 0.5, end: 0.5 }], [100, { start: 1, end: 1 }], [5000, { start: 0.7, end: 0.2, xfadeMs: 1000 }], [5000, { start: 0, end: 1e-9, atEnd: 'stop' }]]) {
        for (const [eng, lock] of engines) for (const motion of ['forward', 'bounce', 'scatter', 'backward']) {
          const e = mkEngine(noise(len / SR, SR, len, 0.5), { engine: eng, lock, region, motion, heads: 3 }); e.noteOn(60, 1); e.noteOn(96, 1); e.noteOn(24, 1);
          const o = render(e, 0.15 * SR, 61); bad += badCount(o); pk = Math.max(pk, peakAbs(o.L), peakAbs(o.R));
        }
      }
    } catch (x) { err = x.message; }
    check('8-frame files, zero-length / reversed Regions, 1000 ms crossfade, every engine and motion', !err && bad === 0 && pk <= 0.98, err ? 'threw ' + err : `bad ${bad}, peak ${f2(pk, 3)}`, 'no throw, finite');
  }
  { // odd process calls and calls before a sample exists
    let err = null, r = '';
    try {
      const e = new SE.Engine({ sampleRate: SR });
      const v0 = e.noteOn(60, 1); const L = new Float32Array(64), R = new Float32Array(64);
      e.process(L, R, 64); const silent = L.every(v => v === 0);
      e.setSample(null); e.setSample({ channels: [] }); e.setSample({ channels: [new Float32Array(3)] });
      e.setSample({ channels: [sine(261.6, 1, SR)], sampleRate: SR }); e.noteOn(60, 1);
      e.process(L, R, 0); e.process(L, R, -5); e.process(L, R, 2.7); e.process(L, R, 200); e.process(L, R, NaN);
      const A = new Array(128).fill(0), B = new Float64Array(128); e.process(A, B, 128);
      r = `noteOn before a sample -> ${v0}, silent ${silent}, plain Array/Float64Array outputs finite ${A.every(Number.isFinite) && B.every(Number.isFinite)}, oversize n left the arrays ${L.length}/${R.length}`;
      if (!(v0 === null && silent && A.every(Number.isFinite))) err = r;
    } catch (x) { err = x.message; }
    check('process(n = 0, -5, 2.7, > buffer, NaN), Array outputs, noteOn/setSample edge cases', !err, err || r, 'no throw');
  }
});

// ================================================================== M. budget (D3)
section('M. budget: an independent per-sample count, fades instead of cuts, newest clip note, quietest first', () => {
  let S = null; try { S = require(path.join(__dirname, '..', 'src', 'sl-sounds.js')); } catch (e) { /* synthetic fallback */ }
  const pad = S ? S.make('pad') : { channels: [noise(4, 44100, 1, 0.4), noise(4, 44100, 2, 0.4)], sampleRate: 44100 };
  const count = e => {   // sounding grains (0 < age < length) + Lock taps + Tape heads at the last rendered sample (white-box)
    let L = 0;
    for (const v of e.voices) { if (v.done) continue; for (const h of v.heads) { if (!h.on) continue;
      if (v.kc === 0) L += 1 + (h.xLeft > 0 ? 1 : 0);
      else if (v.kc === 3) L += h.nA + (h.xf >= 0 ? h.nB : 0);
      else for (let g = 0; g < h.gc; g++) { const a = v.vt - 1 - h.gT[g]; if (a > 0 && a < h.gN[g]) L++; } } }
    return L;
  };
  for (const [eng, lock, sr] of [['voice', false, 48000], ['cloud', false, 48000], ['cloud', true, 48000], ['cloud', true, 44100], ['voice', false, 96000]]) {
    const notes = []; for (let i = 0; i < 16; i++) notes.push(mkNote({ id: 'x' + i, p: 48 + ((i * 7) % 40), s: i * 0.05, l: 6, engine: eng, lock, motion: ['forward', 'mirror', 'scatter', 'bounce'][i % 4], lanes: { heads: 1, grain: 0.2 + (i % 3) * 0.3, pitch: u => 0.5 + 0.3 * Math.sin(u * 20 + i) } }));
    const e = new SE.Engine({ sampleRate: sr }); e.setInst(mkInst({ engine: eng, lock, spread: 1, spray: 0.3, grainLock: 12 })); e.setSample(pad);
    e.setSnapshot(mkSnap(notes, 4)); e.setTransport({ playing: true, bpm: 120, beat: 0, loop: true });
    render(e, 0.6 * sr);
    const vals = []; render(e, 0.25 * sr, 1, () => vals.push(count(e)));
    const mx = Math.max(...vals), B = e.stats().budget;
    check(`${lock ? 'lock' : eng} @${sr / 1000} kHz, 16 notes x 8 heads, stereo pad: independent per-sample count <= budget (white-box)`, mx <= B && B === Math.floor(192 * 48000 / sr), `max ${mx}, ${dist(vals, 0)}, budget ${B}`, '<= floor(192*48000/sr)');
  }
  // thinning must fade (20 ms), not cut: compare against the same render with an unlimited budget (white-box: e.budget)
  const x = sine(261.6256, 4, SR, 0.5);
  for (const [label, inst, notes] of [['Lock, 8 heads, Grain 4, arpeggio of 6', { engine: 'cloud', lock: true, heads: 8, spread: 0.5 }, [48, 52, 55, 60, 64, 67]],
    ['Voice, 8 heads, arpeggio C4..C5', { engine: 'voice', heads: 8, spread: 0.5 }, [72, 74, 76, 77, 79, 84]]]) {
    const run = big => {
      const e = mkEngine(x, Object.assign({ outGain: -12 }, inst)); if (big) e.budget = 1e9;
      let idx = 0; const o = render(e, 1.2 * SR, 32, i => { if (i % 4800 === 0 && idx < notes.length) e.noteOn(notes[idx++], 1); });
      return { m: max2nd(o.L), cuts: e.stats().cuts, thin: e.stats().thinnedHeads };
    };
    const A = run(false), B = run(true);
    check(`${label}: thinning adds no click (max |2nd difference| vs unlimited-budget render)`, A.m <= 2 * B.m + 1e-5, `${A.m.toExponential(2)} vs ${B.m.toExponential(2)} (x${f2(A.m / B.m, 1)}; fade tails cut ${A.cuts}, thinned heads ${A.thin})`, '<= 2x control');
  }
  { // the newest CLIP note never thins
    const nts = []; for (let i = 0; i < 8; i++) nts.push(mkNote({ id: 'cl' + i, p: 76 + i, s: i * 0.25, l: 6, engine: 'voice' }));
    const e = playClip(sine(261.6, 4, SR), nts, { engine: 'voice', heads: 8, spread: 0.5 });
    let bad = 0, checks = 0;
    render(e, beatSamples(2.2, 120), 32, () => { const live = e.voices.filter(v => !v.done && !v.released); if (!live.length) return; const nv = live.reduce((a, b) => (b.serial > a.serial ? b : a)); checks++; if (nv.heads.some(h => h.want && !h.allowed)) bad++; });
    check('newest clip note never thins (8 notes x 8 heads, Voice, C4..C5) (white-box)', bad === 0 && e.stats().thinnedHeads > 0, `${bad} of ${checks} ticks had the newest note thinned; thinned heads at the end ${e.stats().thinnedHeads}`, '0, some thinning');
  }
  { // "oldest and quietest first": a silent voice (Volume lane 0) should give up its heads before a louder voice does
    const nts = [mkNote({ id: 'loud', p: 84, s: 0, l: 6, engine: 'voice' }), mkNote({ id: 'silent', p: 83, s: 0.25, l: 6, engine: 'voice', lanes: { amp: 0 } }), mkNote({ id: 'new', p: 82, s: 0.5, l: 6, engine: 'voice' })];
    const e = playClip(sine(261.6, 4, SR), nts, { engine: 'voice', heads: 8, spread: 0.5 });
    render(e, beatSamples(1.2, 120));
    const th = id => { const v = e.voices.find(w => w.note.id === id); return v ? v.heads.filter(h => h.want && !h.allowed).length : -1; };
    check('quietest first (interpretive): a silent voice thins before an audible older one', th('silent') >= th('loud') && th('silent') > 0, `thinned heads: loud ${th('loud')}, silent ${th('silent')}, newest ${th('new')}`, 'silent >= loud, silent > 0');
  }
});

// ================================================================== N. features the builder's suite never measured
section('N. rev, Lock periodicity, chains with several heads, Path ignores Speed, Refresh scaling, polyphony, display', () => {
  { // Each grain backwards (Voice): a rising saw becomes a falling saw at the same pitch
    const f = midiHz(60), saw = new Float32Array(4 * SR); for (let k = 1; k <= 20; k++) for (let i = 0; i < saw.length; i++) saw[i] -= 0.3 * Math.sin(2 * Math.PI * f * k * i / SR) / k;
    const best = (y, rev) => { const W = 4096, st = SR; let b = -2; for (let lag = 0; lag < 200; lag++) { let ab = 0, aa = 0, bb = 0; for (let j = 0; j < W; j++) { const a = y[st + j], s = rev ? saw[10000 + W - j + lag] : saw[10000 + j + lag]; ab += a * s; aa += a * a; bb += s * s; } b = Math.max(b, ab / Math.sqrt(aa * bb)); } return b; };
    for (const [label, mk] of [['live, inst.rev', () => { const e = mkEngine(saw, { engine: 'voice', rev: true }); e.noteOn(60, 1); return e; }], ['clip, note rev (inst.rev off)', () => playClip(saw, [mkNote({ id: 'r', p: 60, s: 0, l: 8, engine: 'voice', rev: true })], { engine: 'voice', rev: false })]]) {
      const o = render(mk(), 1.5 * SR); const rR = best(o.L, true), rF = best(o.L, false);
      check(`Voice ${label}: output is the time-reversed waveform`, rR > 0.95 && rF < 0.8, `corr with reversed ${f2(rR, 3)}, with forward ${f2(rF, 3)}`, 'reversed > 0.95, forward < 0.8');
    }
  }
  { // Lock repeats exactly every period: integer periods via inst.pitch (48 kHz: 160 samples; 44.1 kHz engine with a 48 kHz source: 150 samples)
    for (const [sr, P, srcSr] of [[48000, 160, 48000], [44100, 150, 48000]]) {
      const fT = sr / P, ip = 12 * Math.log2(fT / midiHz(60));
      const e = new SE.Engine({ sampleRate: sr }); e.setInst(mkInst({ engine: 'cloud', lock: true, pitch: ip, heads: 2, spread: 0.3, refresh: { mode: 'free', ms: 400 } }));
      e.setSample({ channels: [noise(4, srcSr, 77, 0.5)], sampleRate: srcSr }); e.noteOn(60, 1);
      const caps = []; let last = null; const o = render(e, 1.4 * sr, 32, i => { const c = e.voices[0].heads.map(h => h.capA + ':' + h.xf).join(); if (c !== last) caps.push(i); last = c; });
      // stretches between re-grab activity at least 40 ms long
      const errs = [];
      for (let k = 1; k < caps.length; k++) { const a = caps[k - 1] + 2 * P + 64, b = caps[k] - 64; if (b - a < 0.04 * sr) continue; let m = 0; for (let i = a; i + P < b; i++) m = Math.max(m, Math.abs(o.L[i + P] - o.L[i])); errs.push(m / rms(o.L, a, b)); }
      check(`Lock at ${sr / 1000} kHz (${srcSr / 1000} kHz noise): output repeats every ${P} samples between re-grabs`, errs.length >= 2 && Math.max(...errs) < 1e-3, `max |y(t+P)-y(t)|/rms ${errs.length ? Math.max(...errs).toExponential(2) : '-'} over ${errs.length} stretches`, '< 1e-3');
    }
  }
  { // Lock with Refresh Smooth re-grabs "every N periods" (BLUEPRINT 3.3): measured interval between capture changes (white-box: head.capA)
    const e = mkEngine(noise(4, SR, 61, 0.5), { engine: 'cloud', lock: true, refresh: { mode: 'smooth' } }); e.noteOn(60, 1);
    let last = null; const ch = []; render(e, SR, 1, i => { const c = e.voices[0].heads[0].capA; if (last !== null && c !== last) ch.push(i); last = c; });
    const P = SR / midiHz(60), iv = ch.slice(1).map((c, i) => (c - ch[i]) / P);
    info('Lock Smooth, Grain 4: re-grab interval in periods ' + dist(iv, 3) + ' (blueprint: every N = 4; one crossfade period rides on top)');
  }
  { // a chain of two identical notes plays like one long note, with several heads and Drift (D2: the playhead carries on)
    const src = sine(midiHz(60), 8, SR);
    for (const eng of ['tape', 'cloud']) {
      for (const inst of [{ heads: 3, spread: 0.25, drift: 0 }, { heads: 3, spread: 0.25, drift: 0.6 }]) {
        const chain = () => chainNotes([mkNote({ id: 'A', p: 60, s: 0, l: 2, engine: eng, glide: { d: 0, len: 0, curve: 0, toId: 'B' } }), mkNote({ id: 'B', p: 60, s: 2, l: 2, engine: eng })]);
        const one = () => [mkNote({ id: 'A', p: 60, s: 0, l: 4, engine: eng })];
        const go = ns => { const e = playClip(src, ns, Object.assign({ engine: eng }, inst)); const pos = []; render(e, 1.5 * SR, 32, i => { if (i === 47488 || i === 48512) pos.push(e.getDisplay().voices[0].heads.map(h => h.pos)); }); return pos; };
        const C = go(chain()), O = go(one());
        const jumpMs = C[1].map((p, i) => ((p - C[0][i]) - (O[1][i] - O[0][i])) * 8 * 1000);
        check(`${eng}, heads 3, drift ${inst.drift}: every head's playhead carries across the chain hand-over`, jumpMs.every(v => Math.abs(v) < 0.05), 'travel difference vs one long note, ms of source: ' + jumpMs.map(v => f2(v, 3)).join(' '), '|.| < 0.05 ms each head');
      }
    }
  }
  { // Path kind: Speed and Motion are ignored (BLUEPRINT 1.3)
    const ramp = rampSource(4, SR);
    const N = [mkNote({ id: 'pa', p: 60, s: 0, l: 4, engine: 'voice', travel: 'path', motion: 'backward', lanes: { travel: u => 0.2 + 0.6 * u } })];
    const e = playClip(ramp, N, { engine: 'voice', speed: 4, macros: [0.9, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5] });
    const o = render(e, beatSamples(3.9, 120));
    const errs = []; for (let b = 0.5; b < 3.8; b += 0.25) errs.push(Math.abs(decodeAt(o, Math.round(beatSamples(b, 120)), 60) - (0.2 + 0.6 * b / 4)));
    check('Path kind follows the lane with inst.speed 4, Speed macro 0.9 and motion Backward', Math.max(...errs) < 0.003, '|error| ' + dist(errs, 4), '< 0.003');
  }
  { // Refresh period scaling: period / 2^((v-0.5)*6) / 2^((m-0.5)*6), Smooth below 0.06, 5 ms floor (white-box: voice.refIdx)
    const src = noise(4, SR, 9, 0.4);
    const period = (patch, laneV) => {
      const e = laneV == null ? mkEngine(src, Object.assign({ engine: 'cloud' }, patch)) : playClip(src, [mkNote({ id: 'rf', p: 60, s: 0, l: 8, engine: 'cloud', lanes: { refresh: laneV } })], Object.assign({ engine: 'cloud' }, patch));
      if (laneV == null) e.noteOn(60, 1);
      const ev = []; let last = null; render(e, 1.2 * SR, 32, i => { const v = e.voices[0]; if (!v) return; if (last !== null && v.refIdx !== last && v.refInit) ev.push(i); last = v.refInit ? v.refIdx : null; });
      return ev.length > 2 ? (ev[ev.length - 1] - ev[0]) / (ev.length - 1) / SR * 1000 : (ev.length ? NaN : 0);
    };
    const rows = [['free 120 ms', { refresh: { mode: 'free', ms: 120 } }, null, 120],
      ['free 120 ms, Refresh lane 0.5+1/6 (x2 faster)', { refresh: { mode: 'free', ms: 120 } }, 0.5 + 1 / 6, 60],
      ['free 120 ms, Refresh macro 1 (x8 faster)', { refresh: { mode: 'free', ms: 120 }, macros: [0.5, 0.5, 0.5, 1, 0.5, 0.5, 0.5, 0.5] }, null, 15],
      ['free 120 ms, Refresh lane 0.05 -> Smooth', { refresh: { mode: 'free', ms: 120 } }, 0.05, 0],
      ['Smooth inst, Refresh lane 1 and macro 1 stay Smooth', { refresh: { mode: 'smooth' }, macros: [0.5, 0.5, 0.5, 1, 0.5, 0.5, 0.5, 0.5] }, 1, 0],
      ['free 5 ms with macro 1 -> 5 ms floor', { refresh: { mode: 'free', ms: 5 }, macros: [0.5, 0.5, 0.5, 1, 0.5, 0.5, 0.5, 0.5] }, null, 5],
      ['sync 1/16 at 120 bpm (125 ms)', { refresh: { mode: 'sync', rate: '1/16', lockToBeat: true } }, null, 125],
      ['sync 1/8T at 120 bpm (166.7 ms)', { refresh: { mode: 'sync', rate: '1/8T', lockToBeat: true } }, null, 1000 / 6]];
    for (const [label, patch, lane, exp] of rows) {
      const p = period(patch, lane);
      check('Refresh ' + label + ' (white-box)', exp === 0 ? p === 0 : Math.abs(p - exp) <= Math.max(0.7, exp * 0.01), (p === 0 ? 'no refresh events' : f2(p, 2) + ' ms'), exp === 0 ? 'no refresh events' : f2(exp, 2) + ' ms');
    }
  }
  { // polyphony setting
    const e = mkEngine(sine(261.6, 2, SR), { engine: 'tape', polyphony: 4 }); let mx = 0;
    for (let i = 0; i < 7; i++) { e.noteOn(50 + i, 1); render(e, 0.01 * SR, 32, () => { mx = Math.max(mx, e.voices.filter(v => !v.done && v.st !== 4).length); }); }
    render(e, 0.02 * SR);
    check('polyphony 4: 7 keys give 4 voices and 3 steals', e.stats().voices === 4 && e.stats().steals === 3 && mx <= 4, `voices ${e.stats().voices}, steals ${e.stats().steals}, max un-stolen ${mx}`, '4, 3, <= 4');
  }
  { // display: heads stay inside the Region; dir follows the motion
    const ramp = rampSource(4, SR); let outside = 0, total = 0;
    for (const [eng, lock] of [['tape', false], ['voice', false], ['cloud', false], ['cloud', true]]) for (const motion of ['forward', 'backward', 'bounce', 'mirror', 'freeze', 'scatter']) {
      const e = mkEngine(ramp, { engine: eng, lock, motion, heads: 4, spread: 0.9, region: { start: 0.4, end: 0.6 }, spray: 0.5 }); e.noteOn(60, 1);
      render(e, 1.2 * SR, 256, () => { const d = e.getDisplay(); for (const v of d.voices) { for (const h of v.heads) { total++; if (!(h.pos >= 0.4 - 1e-9 && h.pos <= 0.6 + 1e-9)) outside++; } for (const g of d.grains) { total++; if (!(g.pos >= 0.4 - 1e-9 && g.pos <= 0.6 + 1e-9)) outside++; } } });
    }
    check('display: every head and grain position stays inside Region 0.4..0.6 (4 engines x 6 motions, Spray 0.5)', outside === 0 && total > 1000, `${outside} of ${total} outside`, '0');
    const dirOf = motion => { const e = mkEngine(ramp, { engine: 'voice', motion }); e.noteOn(60, 1); render(e, 0.1 * SR); return e.getDisplay().voices[0].heads[0].dir; };
    const d = ['forward', 'backward', 'freeze'].map(dirOf);
    check('display dir: forward 1, backward -1, freeze 0', d.join() === '1,-1,0', d.join(), '1,-1,0');
  }
});

// ================================================================== O. speed with the real (stereo) starter sounds
section('O. speed with a stereo 44.1 kHz starter sound (every built-in sound has L != R)', () => {
  let S = null; try { S = require(path.join(__dirname, '..', 'src', 'sl-sounds.js')); } catch (e) { info('sl-sounds.js not loadable: skipped'); return; }
  const pad = S.make('pad');
  let maxLR = 0; for (let i = 0; i < pad.channels[0].length; i++) maxLR = Math.max(maxLR, Math.abs(pad.channels[0][i] - pad.channels[1][i]));
  info('pad: max |L-R| ' + f2(maxLR, 3) + ' (stereo, so the engine\'s mono fast paths do not apply)');
  const { spawnSync } = require('child_process');
  const r = spawnSync(process.execPath, [__filename, '--bench-child'], { encoding: 'utf8', timeout: 5 * 60 * 1000 });
  let rows = null; try { rows = JSON.parse((r.stdout || '').trim().split('\n').pop()); } catch (e) { /* reported below */ }
  if (!rows) { check('stereo speed bench ran', false, String(r.stderr || r.error || 'no output').slice(0, 200), 'ran'); return; }
  for (const { name, xs } of rows) {
    const x = Math.max(...xs);
    check(`${name}: 8 voices x 2 heads on the stereo pad, 48 kHz (fresh process, best of 2)`, x >= 4, `${f2(x, 2)}x real time (runs ${xs.map(v => f2(v, 2)).join(', ')})`, '>= 4x (BLUEPRINT 6 target)');
  }
  info('run inside this long test process instead (JIT made polymorphic by section L), the same bench measured Cloud 3.2x and Lock 2.7x on 8 Nov; the worklet only ever passes Float32Arrays');
});

// ================================================================== P. worklet glue error path
section('P. worklet: a throwing message posts an error and the processor keeps playing', () => {
  const posted = []; let reg = null;
  class FakeAWP { constructor() { this.port = { postMessage: m => posted.push(m), onmessage: null }; } }
  new Function('registerProcessor', 'AudioWorkletProcessor', 'sampleRate', createStrandEngineModule.toString() + '\n' + strandWorkletGlue.toString() + '\nstrandWorkletGlue();')((n, c) => { reg = { n, c }; }, FakeAWP, 48000);
  const p = new reg.c(); const send = m => p.port.onmessage({ data: m });
  send({ type: 'sample', sample: { channels: [sine(261.6, 2, SR)], sampleRate: SR, name: 'nested form' } });
  send({ type: 'noteOn', p: 60, vel: 1 });
  const evil = {}; Object.defineProperty(evil, 'engine', { enumerable: true, get() { throw new Error('boom'); } });
  send({ type: 'inst', inst: evil });
  send({ type: 'bogus' }); send(null);
  let e2 = 0; for (let b = 0; b < 40; b++) { const o = [[new Float32Array(128), new Float32Array(128)]]; p.process([], o); for (let i = 0; i < 128; i++) e2 += o[0][0][i] ** 2; }
  const errs = posted.filter(m => m && m.type === 'error');
  check('worklet: nested {sample:{...}} form loads; a throwing inst posts {type:"error"}; unknown/null messages are ignored; sound continues', errs.length === 1 && /boom/.test(errs[0].message) && e2 > 1,
    `${errs.length} error message(s)${errs.length ? ' "' + errs[0].message + '"' : ''}, energy after ${f2(e2, 2)}`, '1 error, still sounding');
});

console.log(`\n${passed} passed, ${failed} failed`);
if (fails.length) { console.log('failures:'); fails.forEach(f => console.log('  - ' + f)); }
process.exit(failed ? 1 : 0);
