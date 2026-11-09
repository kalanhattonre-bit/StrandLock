// tests/test-sounds-extra.js - skeptic's checks for SLSounds + SLAnalysis (node tests/test-sounds-extra.js).
// Written by the reviewer, not the builder. One line per check (PASS/FAIL name: measured vs limit); exits 1 on any FAIL.
// Every check here is meant to FAIL if a blueprint feature (BLUEPRINT.md section 4) is missing, wrong or fragile.
// No Math.random: test noise comes from a seeded hash.
'use strict';
const path = require('path');
const SLSounds = require(path.join(__dirname, '..', 'src', 'sl-sounds.js'));
const SLAnalysis = require(path.join(__dirname, '..', 'src', 'sl-analysis.js'));

let fails = 0, passes = 0;
function check(name, ok, msg) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name + ': ' + msg);
  if (ok) passes++; else fails++;
}
function info(msg) { console.log('INFO ' + msg); }
const f2 = (x, d = 2) => (Number.isFinite(x) ? x.toFixed(d) : String(x));
function dist(arr, d = 2) {
  const a = arr.filter((v) => v === v).slice().sort((x, y) => x - y);
  if (!a.length) return 'none';
  const q = (p) => a[Math.min(a.length - 1, Math.round(p * (a.length - 1)))];
  return 'min ' + f2(a[0], d) + ' / median ' + f2(q(0.5), d) + ' / p90 ' + f2(q(0.9), d) + ' / max ' + f2(a[a.length - 1], d);
}
function hash01(a, b) {
  let h = (Math.imul(a | 0, 0x9e3779b1) ^ Math.imul(b | 0, 0x85ebca6b)) >>> 0;
  h ^= h >>> 16; h = Math.imul(h, 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35); h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}
function fnv(arrs) {             // checksum of Float32Array contents (bit-exact)
  let h = 0x811c9dc5;
  for (const a of arrs) {
    const u = new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
    for (let i = 0; i < u.length; i++) { h ^= u[i]; h = Math.imul(h, 0x01000193); }
  }
  return h >>> 0;
}
function sameBits(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (!Object.is(a[i], b[i])) return false;
  return true;
}
const SR = 44100;
const IDS = ['sawopen', 'saw', 'square', 'pad', 'vowel', 'pluck', 'beat', 'noise'];
const PITCHED = ['sawopen', 'saw', 'square', 'pad', 'vowel'];
const C3 = 440 * Math.pow(2, -9 / 12);

/* ================= A. SLSounds API contract ================= */
{
  const L = SLSounds.list;
  const keysOk = Array.isArray(L) && L.length === 8 && L.every((e) => JSON.stringify(Object.keys(e).sort()) === '["id","name"]');
  check('A list entries are exactly {id,name} x 8', keysOk, `${L.length} entries, keys ${JSON.stringify(L.map((e) => Object.keys(e).join('+')))}`);
  check('A ROOT_HZ is C3 = 440*2^(-9/12)', Math.abs(SLSounds.ROOT_HZ - C3) < 1e-9, `${SLSounds.ROOT_HZ} vs ${C3}`);
  check('A PITCHED set = the blueprint pitched ones', JSON.stringify((SLSounds.PITCHED || []).slice().sort()) === JSON.stringify(PITCHED.slice().sort()),
    JSON.stringify(SLSounds.PITCHED));
  // editing the exported list must not change what make() returns
  const old = L[0].name;
  L[0].name = 'HACKED';
  let leaked;
  try { leaked = SLSounds.make('sawopen').name; } finally { L[0].name = old; }
  check('A list is a copy (editing it does not leak into make)', leaked === 'Saw that opens up', `make('sawopen').name after editing list[0].name = "${leaked}"`);
}
{
  const bad = [undefined, null, '', 'SAW', 'Saw', ' saw', 'constructor', '__proto__', 'toString', 'hasOwnProperty', 42, {}];
  const res = bad.map((id) => { try { const r = SLSounds.make(id); return 'returned ' + (r && r.id); } catch (e) { return 'throws'; } });
  check('A make() throws on every non-id', res.every((r) => r === 'throws'), bad.map((b, i) => JSON.stringify(b === undefined ? 'undefined' : b) + ' ' + res[i]).join(', '));
}

const made = {}, mk = {};
for (const id of IDS) {
  const t0 = process.hrtime.bigint();
  made[id] = SLSounds.make(id);
  mk[id] = Number(process.hrtime.bigint() - t0) / 1e6;
}
{
  // exact numbers from the blueprint and from the builder's own claims
  const rows = [], errs = [];
  for (const id of IDS) {
    const s = made[id], N = s.channels[0].length;
    const periods = N * SLSounds.ROOT_HZ / SR;
    rows.push(`${id} ${N}` + (PITCHED.includes(id) ? ` (${f2(periods, 4)} C3 periods)` : ''));
    if (s.sampleRate !== 44100 || s.rootMidi !== 60 || s.channels.length !== 2) errs.push(id + ' fields');
    if (!(s.channels[0] instanceof Float32Array) || !(s.channels[1] instanceof Float32Array)) errs.push(id + ' not Float32Array');
    if (s.channels[0] === s.channels[1] || s.channels[0].buffer === s.channels[1].buffer) errs.push(id + ' L and R share one buffer');
    if (s.channels[1].length !== N) errs.push(id + ' L/R lengths differ');
    if (id === 'beat' && N !== 4 * SR) errs.push('beat not exactly 4 s');
    if ((id === 'pluck' || id === 'noise') && N !== 6 * SR) errs.push(id + ' not exactly 6 s');
    if (PITCHED.includes(id) && (Math.abs(periods - Math.round(periods)) > 1e-3 || Math.abs(N / SR - 6) > 0.005)) errs.push(id + ' not whole C3 periods within 5 ms of 6 s');
  }
  check('A exact lengths and fields', errs.length === 0, rows.join(', ') + (errs.length ? '; ' + errs.join(', ') : ''));
}
{
  // peak exactly 0.8 (Float32 of 0.8), nothing above it, no NaN/inf, no DC, balanced L/R
  const pk = [], dc = [], bal = [], errs = [];
  for (const id of IDS) {
    const [L, R] = made[id].channels, N = L.length;
    let m = 0, bad = 0, sL = 0, sR = 0, eL = 0, eR = 0;
    for (let i = 0; i < N; i++) {
      const a = L[i], b = R[i];
      if (!Number.isFinite(a) || !Number.isFinite(b)) { bad++; continue; }
      m = Math.max(m, Math.abs(a), Math.abs(b)); sL += a; sR += b; eL += a * a; eR += b * b;
    }
    pk.push(m); dc.push(Math.max(Math.abs(sL / N), Math.abs(sR / N))); bal.push(10 * Math.log10(eL / eR));
    if (bad || Math.abs(m - Math.fround(0.8)) > 1e-6) errs.push(`${id} peak ${m}`);
  }
  check('A peak is 0.8 to 1e-6 on every sound', errs.length === 0, `peaks ${pk.map((p) => p.toFixed(7)).join(', ')}` + (errs.length ? '; ' + errs.join(', ') : ''));
  check('A no DC offset (|mean| < 0.004)', dc.every((d) => d < 0.004), IDS.map((id, i) => id + ' ' + dc[i].toExponential(1)).join(', '));
  check('A L/R balance within 1.5 dB', bal.every((b) => Math.abs(b) <= 1.5), IDS.map((id, i) => id + ' ' + f2(bal[i])).join(', ') + ' dB (L over R)');
}
{
  // default seed: make(id) must equal make(id, {seed:1}) and every "no seed" spelling; a frozen opts must not be written
  const errs = [];
  for (const id of ['saw', 'noise', 'beat']) {
    const a = made[id];
    const variants = [undefined, null, {}, Object.freeze({ seed: 1 }), { seed: NaN }, { seed: Infinity }, { seed: '1' }, { seed: 1.4 }];
    variants.forEach((o, k) => {
      let b;
      try { b = SLSounds.make(id, o); } catch (e) { errs.push(`${id} opts#${k} threw ${e.message}`); return; }
      if (!sameBits(a.channels[0], b.channels[0]) || !sameBits(a.channels[1], b.channels[1])) errs.push(`${id} opts#${k} ${JSON.stringify(o)} differs from default`);
    });
  }
  check('A default seed is 1 for every "no seed" spelling (frozen opts ok)', errs.length === 0, errs.length ? errs.join('; ') : 'undefined, null, {}, frozen {seed:1}, NaN, Infinity, "1", 1.4 all bit-identical to make(id)');
}
{
  // no hidden state: pad made fresh, after every other sound, and after a seeded make, is identical
  const before = fnv(made.pad.channels);
  for (const id of IDS) SLSounds.make(id, { seed: 7 });
  const after = fnv(SLSounds.make('pad').channels);
  // the returned arrays are the caller's: scribbling on one must not change the next make
  const s = SLSounds.make('saw'); s.channels[0].fill(0); s.channels[1][5] = 99;
  const again = fnv(SLSounds.make('saw').channels), orig = fnv(made.saw.channels);
  check('A no hidden state between makes', before === after && again === orig, `pad checksum ${before.toString(16)} -> ${after.toString(16)}; saw after scribbling on a returned copy ${again === orig ? 'unchanged' : 'CHANGED'}`);
}
{
  // different seeds give different sounds that still meet the contract (length, peak, root)
  const errs = [], sums = {};
  for (const id of IDS) {
    const seen = new Set();
    for (const seed of [0, 2, 3, -5, 123456]) {
      const s = SLSounds.make(id, { seed }), N = s.channels[0].length;
      if (N !== made[id].channels[0].length) errs.push(`${id} seed ${seed} length ${N}`);
      let m = 0, bad = 0;
      for (const ch of s.channels) for (let i = 0; i < N; i++) { const v = ch[i]; if (!Number.isFinite(v)) bad++; else if (Math.abs(v) > m) m = Math.abs(v); }
      if (bad || Math.abs(m - Math.fround(0.8)) > 1e-6) errs.push(`${id} seed ${seed} peak ${m} bad ${bad}`);
      seen.add(fnv(s.channels));
      if (PITCHED.includes(id) && seed === 123456) {
        const p = SLAnalysis.detectPitch(s.channels, SR);
        if (!p || p.midi !== 60 || Math.abs(p.cents) > 3) errs.push(`${id} seed ${seed} pitch ${JSON.stringify(p)}`);
      }
    }
    seen.add(fnv(made[id].channels));
    sums[id] = seen.size;
    if (seen.size !== 6) errs.push(`${id}: only ${seen.size}/6 distinct over seeds 1,0,2,3,-5,123456`);
  }
  check('A every seed gives a distinct, valid sound', errs.length === 0, IDS.map((id) => `${id} ${sums[id]}/6 distinct`).join(', ') + (errs.length ? '; ' + errs.join('; ') : ''));
}
info('make() ms (first call): ' + IDS.map((id) => id + ' ' + f2(mk[id], 0)).join(', '));

/* ================= B. seam: a circular high-pass (> 19.6 kHz) sees any click ================= */
// Lowpass: windowed sinc, 1023 taps, cutoff 19.4 kHz, 4-term Blackman-Harris (stopband ~ -92 dB). HP = x - LP(x), read circularly
// (the loop as the engine plays it). Every partial in the pitched sounds and pluck is below 19 kHz, so the HP output is ~0
// everywhere a loop is clean; a seam click is broadband and shows up only at the seam.
const FIR = (() => {
  const N = 1023, M = 511, fc = 19400 / SR, h = new Float64Array(N);
  let s = 0;
  for (let n = 0; n < N; n++) {
    const t = n - M, x = 2 * Math.PI * n / (N - 1);
    const w = 0.35875 - 0.48829 * Math.cos(x) + 0.14128 * Math.cos(2 * x) - 0.01168 * Math.cos(3 * x);
    h[n] = (t === 0 ? 2 * fc : Math.sin(2 * Math.PI * fc * t) / (Math.PI * t)) * w; s += h[n];
  }
  for (let n = 0; n < N; n++) h[n] /= s;
  return { h, N, M };
})();
function hpAt(x, i) {
  const L = x.length, { h, N, M } = FIR;
  let acc = 0;
  for (let n = 0; n < N; n++) { let j = (i - n + M) % L; if (j < 0) j += L; acc += h[n] * x[j]; }
  let j0 = i % L; if (j0 < 0) j0 += L;
  return x[j0] - acc;
}
function hpMax(x, centre, half) { let m = 0; for (let i = centre - half; i <= centre + half; i++) m = Math.max(m, Math.abs(hpAt(x, i))); return m; }
function seamHF(x, id) {
  const L = x.length, seam = hpMax(x, 0, 200);   // the wrap from L-1 to 0 sits inside +-200
  let ref = 0;
  if (id === 'beat') {         // compare with every other 16th-note slot (hits look like hits)
    for (let st = 1; st < 32; st++) ref = Math.max(ref, hpMax(x, Math.round(st * SR * 0.125), 200));
  } else if (id === 'pluck') { // compare with every other note start
    for (let k = 1; k < 24; k++) ref = Math.max(ref, hpMax(x, Math.round(k * L / 24), 200));
  } else {
    for (let k = 0; k < 48; k++) ref = Math.max(ref, hpMax(x, Math.round((k + 0.5) * L / 48), 40));
  }
  return { seam, ref, ratio: seam / Math.max(ref, 1e-12) };
}
{
  const rows = [], bad = [], broad = [];
  for (const id of IDS) {
    const tonal = id !== 'beat' && id !== 'noise';
    for (let c = 0; c < 2; c++) {
      const r = seamHF(made[id].channels[c], id);
      const txt = `${id} ${f2(20 * Math.log10(r.seam + 1e-12), 0)} dBFS vs ${f2(20 * Math.log10(r.ref + 1e-12), 0)} (x${f2(r.ratio, 2)})`;
      if (c === 0) (tonal ? rows : broad).push(txt);
      const ok = r.ratio <= 1.5 || r.seam < 1e-5;   // 1e-5 = -100 dBFS: inaudible whatever the ratio
      if (tonal && !ok) bad.push(id + (c ? ' R' : ' L') + ' x' + f2(r.ratio, 2));
    }
  }
  check('B seam high band (> 19.6 kHz, circular) no worse than the rest of the file', bad.length === 0, rows.join('; ') + ' (L channel; limit x1.5, or under -100 dBFS)' + (bad.length ? '; bad: ' + bad.join(', ') : ''));
  info('beat/noise seam high band ' + broad.join('; ') + ': not judged, their own content is broadband (a beat cut short by 0.45 s still measures x0.5 here, ' +
    'and the builder\'s "beat seam step" passes it too at 0.98)');
  // control: the same loops cut short by 1 and 3 samples (the builder's own control starts at 17) must be flagged
  const ctl = [];
  let flagged = 0, total = 0;
  for (const id of ['saw', 'sawopen', 'pad', 'vowel', 'square']) for (const cut of [1, 3]) {
    const x = made[id].channels[0].subarray(0, made[id].channels[0].length - cut), r = seamHF(x, id);
    total++; if (r.ratio > 1.5 && r.seam >= 1e-5) flagged++;
    ctl.push(`${id}-${cut} x${f2(r.ratio, 0)}`);
  }
  check('B control: 1- and 3-sample cuts are caught', flagged === total, `${flagged}/${total}: ${ctl.join(', ')}`);
}

/* ================= C. each sound does what its name says ================= */
// single-frequency DFT with a Hann window (any f, not just bins)
function toneMag(x, start, n, f) {
  let re = 0, im = 0;
  const w = 2 * Math.PI * f / SR;
  for (let i = 0; i < n; i++) { const win = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / n), v = x[start + i] * win; re += v * Math.cos(w * i); im -= v * Math.sin(w * i); }
  return Math.hypot(re, im);
}
{
  // Square sweep: at 50% duty the even harmonics vanish; a width sweep must bring H2 up and back
  const x = made.square.channels[0], N = x.length, n = 8192;
  const h21 = (frac) => { const st = Math.min(N - n, Math.max(0, Math.round(frac * N - n / 2))); return 20 * Math.log10(toneMag(x, st, n, 2 * C3) / toneMag(x, st, n, C3)); };
  const fr = [0.02, 0.25, 0.5, 0.75, 0.98], v = fr.map(h21);
  check('C square: pulse width really sweeps (H2/H1)', v[0] < -30 && v[2] > -10 && v[4] < -30 && v[1] > v[0] + 10,
    fr.map((f, i) => `${Math.round(f * 100)}% ${f2(v[i], 1)} dB`).join(', ') + ' (ends < -30 dB = square, middle > -10 dB = narrow pulse)');
}
// onsets: 3 ms RMS envelope of x (or of its first difference, which favours attacks) at 1 ms hop; an onset is a frame
// riseDb above the loudest frame 8-30 ms earlier, read circularly (so a hit at 0 s counts); 40 ms dead time after each.
function onsets(ch, riseDb, diff) {
  const N = ch.length, hop = 44, W = 132, nf = Math.floor(N / hop), env = new Float64Array(nf);
  const at = (i) => ch[((i % N) + N) % N];
  for (let f = 0; f < nf; f++) { let e = 0; for (let j = 0; j < W; j++) { const i = f * hop + j, v = diff ? at(i) - at(i - 1) : at(i); e += v * v; } env[f] = Math.sqrt(e / W); }
  let pk = 0; for (const v of env) pk = Math.max(pk, v);
  const E = (f) => env[((f % nf) + nf) % nf], g = Math.pow(10, riseDb / 20), out = [];
  let lastT = -1;
  for (let f = 0; f < nf; f++) {
    let m = 0; for (let q = 8; q <= 30; q++) m = Math.max(m, E(f - q));
    if (E(f) >= g * m && E(f) > 0.02 * pk) { const t = f * hop / SR; if (lastT < 0 || t - lastT > 0.04) { out.push({ t }); lastT = t; } }
  }
  return out;
}
{
  // Beat loop: 120 bpm, 2 bars. Every onset on the 16th grid (0.125 s), a hit on both downbeats, and bar 2 is not a copy of bar 1.
  const s = made.beat, mono = new Float32Array(s.channels[0].length);
  for (let i = 0; i < mono.length; i++) mono[i] = 0.5 * (s.channels[0][i] + s.channels[1][i]);
  const LEN = mono.length / SR, wrapT = (t) => (LEN - t < 0.005 ? t - LEN : t);
  const os = onsets(mono, 9, true).concat(onsets(mono, 6, false)).map((o) => ({ t: wrapT(o.t) }));
  const off = os.map((o) => (o.t - Math.round(o.t / 0.125) * 0.125) * 1000);
  const offGrid = os.filter((o, i) => Math.abs(off[i]) > 6);   // 6 ms = the detector's 3 ms window + a kick's slow low-end rise
  const slots = new Set(os.map((o) => ((Math.round(o.t / 0.125) % 32) + 32) % 32));
  const broad = onsets(mono, 6, false).map((o) => wrapT(o.t));
  const has = (t) => broad.some((x) => Math.abs(x - t) < 0.005);
  let c = 0, e1 = 0, e2 = 0;
  const H = 2 * SR;
  for (let i = 0; i < H; i++) { c += mono[i] * mono[i + H]; e1 += mono[i] * mono[i]; e2 += mono[i + H] * mono[i + H]; }
  const r = c / Math.sqrt(e1 * e2);
  check('C beat: 120 bpm 16th grid, both downbeats, 2 different bars', slots.size >= 12 && offGrid.length === 0 && has(0) && has(2) && r < 0.98,
    `${slots.size} grid slots hit (>= 12), offsets from the 16th grid ${dist(off.map(Math.abs), 1)} ms (all <= 6), off-grid ${offGrid.map((o) => f2(o.t, 3)).join(' ') || 'none'}; ` +
    `onset at 0 s ${has(0)}, at 2 s ${has(2)}; bar 1 vs bar 2 correlation ${f2(r, 3)} (< 0.98)`);
}
{
  // Pluck arpeggio: notes start on a steady grid and the pitch actually moves (detectPitch per note)
  const s = made.pluck, ch = s.channels[0], N = ch.length;
  const os = onsets(ch, 4, true), step = N / 24 / SR;
  const offs = os.map((o) => Math.abs(o.t - Math.round(o.t / step) * step) * 1000);
  const slots = new Set(os.filter((o, i) => offs[i] <= 5).map((o) => Math.round(o.t / step) % 24));
  const det = [];
  for (let k = 0; k < 24; k++) {
    const a = Math.round((k * step + 0.015) * SR), b = Math.round((k * step + 0.24) * SR);
    const p = SLAnalysis.detectPitch([ch.subarray(a, b)], SR);
    det.push(p ? p.midi : null);
  }
  const distinct = new Set(det.filter((m) => m != null).map((m) => m % 12));
  check('C pluck: notes on a 0.25 s grid and the pitch moves', slots.size >= 20 && os.every((o, i) => offs[i] <= 5) && distinct.size >= 4,
    `${slots.size}/24 note slots have an onset within 5 ms (>= 20; ${os.length} onsets, off-grid ${os.filter((o, i) => offs[i] > 5).length}); ` +
    `${distinct.size} distinct pitch classes detected per note (>= 4)`);
  info(`pluck per-note detectPitch (15-240 ms after each start): ${det.map((m) => (m == null ? '-' : m)).join(' ')}; ` +
    'the notes ring 0.3-0.6 s into each other, so many windows read as a chord or a sub-octave');
}
{
  // Evolving pad = v3's makeDefault: 18 partials (plus a +0.35% copy) and nothing above them
  const x = made.pad.channels[0], n = 1 << 15, st = 40000;
  let tot = 0, above = 0;
  const re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < n; i++) re[i] = x[st + i] * (0.35875 - 0.48829 * Math.cos(2 * Math.PI * i / n) + 0.14128 * Math.cos(4 * Math.PI * i / n) - 0.01168 * Math.cos(6 * Math.PI * i / n));
  // plain DFT via the builder-independent FFT below
  fftIP(re, im);
  for (let b = 1; b < n / 2; b++) { const p = re[b] * re[b] + im[b] * im[b], hz = b * SR / n; tot += p; if (hz > 18.6 * C3) above += p; }
  const db = 10 * Math.log10(above / tot + 1e-30);
  const h1 = toneMag(x, st, 8192, C3), h18 = toneMag(x, st, 8192, 18 * C3), h19 = toneMag(x, st, 8192, 19 * C3);
  check('C pad: 18 partials like v3, nothing above the 18th', db < -80 && h18 > h19 * 100,
    `energy above 18.6 x C3 (${f2(18.6 * C3, 0)} Hz) ${f2(db, 1)} dB of total (< -80); H18 ${f2(20 * Math.log10(h18 / h1), 1)} dB, H19 ${f2(20 * Math.log10(h19 / h1 + 1e-30), 1)} dB re H1`);
}
function fftIP(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, half = len >> 1;
    for (let i = 0; i < n; i += len) for (let k = 0; k < half; k++) {
      const wr = Math.cos(ang * k), wi = Math.sin(ang * k), a = i + k, b = a + half;
      const tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
      re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
    }
  }
}
{
  // "every sound except saw changes across its length": octave-band spectra of 300 ms at 10/30/50/70/90% must differ by > 3 dB;
  // saw must stay within 0.3 dB. (The builder's own check accepts a 1 dB change.)
  const n = 1 << 13, bandsHz = [125, 250, 500, 1000, 2000, 4000, 8000, 16000];
  function bandDb(x, st) {
    const out = [];
    for (const fc of bandsHz) {
      let e = 0;
      for (const f of [fc / Math.SQRT2 * 1.1, fc, fc * Math.SQRT2 * 0.9]) e += toneMag(x, st, n, f) ** 2;
      out.push(10 * Math.log10(e + 1e-20));
    }
    return out;
  }
  const rows = [], bad = [];
  for (const id of IDS) {
    const x = made[id].channels[0], N = x.length, prof = [0.1, 0.3, 0.5, 0.7, 0.9].map((f) => bandDb(x, Math.round(f * N) - n / 2));
    let worst = 0;
    for (let a = 0; a < prof.length; a++) for (let b = a + 1; b < prof.length; b++) {
      const top = Math.max(...prof[a], ...prof[b]);
      let s = 0, k = 0;
      for (let i = 0; i < bandsHz.length; i++) if (Math.max(prof[a][i], prof[b][i]) > top - 50) { s += (prof[a][i] - prof[b][i]) ** 2; k++; }
      worst = Math.max(worst, Math.sqrt(s / Math.max(1, k)));
    }
    rows.push(`${id} ${f2(worst, 2)}`);
    if (id === 'saw' ? worst > 0.3 : worst < 3) bad.push(id);
  }
  check('C every sound but saw changes (> 3 dB band change), saw static (< 0.3 dB)', bad.length === 0, rows.join(', ') + ' dB (largest RMS band difference between two of 5 places)' + (bad.length ? '; bad: ' + bad.join(', ') : ''));
}

/* ================= D. pitched sounds are exactly C3 (independent: phase advance of the fundamental) ================= */
{
  // DFT at exactly ROOT_HZ over a rectangular window of L/5 samples, at three places. If the fundamental is at f0 + df, its
  // phase drifts 2*pi*df per second. L/5 also makes the pad's chorus copy (+5 cycles per loop) orthogonal to the window.
  const cents = [], rows = [];
  for (const id of PITCHED) {
    const x = made[id].channels[0], N = x.length, Lw = Math.round(N / 5);
    const ph = (s) => { let re = 0, im = 0; const w = 2 * Math.PI * C3 / SR; for (let i = s; i < s + Lw; i++) { re += x[i] * Math.cos(w * i); im -= x[i] * Math.sin(w * i); } return Math.atan2(im, re); };
    const wrap = (a) => a - 2 * Math.PI * Math.round(a / (2 * Math.PI));
    const p0 = ph(0), d1 = Math.round(0.05 * SR), d2 = N - Lw;
    const df1 = wrap(ph(d1) - p0) / (2 * Math.PI * d1 / SR);
    const exp2 = 2 * Math.PI * df1 * d2 / SR, df = df1 + wrap(ph(d2) - p0 - exp2) / (2 * Math.PI * d2 / SR);
    const c = 1200 * Math.log2((C3 + df) / C3);
    cents.push(c); rows.push(`${id} ${f2(c, 4)}`);
  }
  check('D pitched sounds within 0.1 cent of C3 (phase method)', cents.every((c) => Math.abs(c) <= 0.1), rows.join(', ') + ' cents (the builder checks only +-10)');
}

/* ================= E. detectPitch ================= */
function tone(midi, sr, secs, harm, amp = 0.4, seed = 1) {
  const n = Math.round(secs * sr), x = new Float32Array(n), f = 440 * Math.pow(2, (midi - 69) / 12);
  for (let k = 1; k <= harm; k++) {
    if (k * f > 0.45 * sr) break;
    const w = 2 * Math.PI * k * f / sr, ph = hash01(seed, k) * 2 * Math.PI;
    for (let i = 0; i < n; i++) x[i] += amp * Math.sin(w * i + ph) / k;
  }
  return x;
}
// v3's detectPitch, verbatim except it takes the mono array (reference for "everything v3 can do, v4 can do")
function v3Pitch(d, sr) {
  const W = 2048, maxT = Math.floor(sr / 45), minT = Math.floor(sr / 1500); if (d.length < W + maxT + 10) return null;
  const blocks = []; for (let s = 0; s + W + maxT < d.length; s += W) { let e = 0; for (let i = s; i < s + W; i += 4) e += d[i] * d[i]; blocks.push([e, s]); }
  blocks.sort((a, b) => b[0] - a[0]); const res = [];
  for (const [, st] of blocks.slice(0, 5)) {
    const df = new Float32Array(maxT + 2); for (let tau = 1; tau <= maxT + 1; tau++) { let s = 0; for (let j = 0; j < W; j++) { const x = d[st + j] - d[st + j + tau]; s += x * x; } df[tau] = s; }
    let run = 0, best = -1; const cm = new Float32Array(maxT + 2); cm[0] = 1; for (let tau = 1; tau <= maxT + 1; tau++) { run += df[tau]; cm[tau] = df[tau] * tau / (run || 1); }
    for (let tau = minT; tau <= maxT; tau++) { if (cm[tau] < 0.15) { while (tau + 1 <= maxT && cm[tau + 1] < cm[tau]) tau++; best = tau; break; } }
    if (best < 0) { let m = 1e9; for (let tau = minT; tau <= maxT; tau++) if (cm[tau] < m) { m = cm[tau]; best = tau; } if (m > 0.45) continue; }
    const a = cm[best - 1], b = cm[best], c = cm[best + 1], den = a - 2 * b + c, sh = den ? 0.5 * (a - c) / den : 0; res.push(sr / (best + sh));
  }
  if (!res.length) return null; res.sort((a, b) => a - b); const f = res[Math.floor(res.length / 2)]; return 69 + 12 * Math.log2(f / 440);
}
{
  // range the builder claims (45-1500 Hz) at three sample rates, plus shape of the result
  const errs = [], abs = [];
  let shapeOk = true;
  for (const sr of [44100, 48000, 96000]) for (const m of [30, 31, 33, 36, 48, 60, 72, 84, 86, 88, 89, 90]) for (const c of [0, 31, -44]) {
    const x = tone(m + c / 100, sr, 1.5, 10, 0.4, m + c), p = SLAnalysis.detectPitch([x], sr);
    if (!p) { errs.push(`${m}${c ? (c > 0 ? '+' : '') + c : ''}@${sr} null`); continue; }
    if (!Number.isInteger(p.midi) || !(p.cents >= -50 && p.cents <= 50) || !Number.isFinite(p.hz)) shapeOk = false;
    const e = (p.midi + p.cents / 100 - (m + c / 100)) * 100; abs.push(Math.abs(e));
    if (Math.abs(e) > 3) errs.push(`${m}${c ? (c > 0 ? '+' : '') + c : ''}@${sr} ${f2(e, 1)}c`);
  }
  check('E detectPitch 45-1507 Hz (MIDI 29.56-90.31) at 44.1/48/96 kHz within 3 cents', errs.length === 0 && shapeOk,
    `${abs.length} detected, |error| ${dist(abs, 2)} cents; midi integer + cents in [-50,50]: ${shapeOk}` + (errs.length ? '; off: ' + errs.slice(0, 10).join(', ') : ''));
}
{
  // port fidelity: v4 must agree with v3 on clean tones (only the 0.1-cent rounding may differ)
  const d = [];
  for (const sr of [44100, 48000]) for (const m of [36, 43, 50, 57, 64, 71, 78, 85]) for (const c of [0, 17, -29]) {
    const x = tone(m + c / 100, sr, 1.2, 12, 0.4, m * 3 + c), p = SLAnalysis.detectPitch([x, x], sr), q = v3Pitch(x, sr);
    d.push(p && q != null ? Math.abs((p.midi + p.cents / 100 - q) * 100) : Infinity);
  }
  check('E detectPitch agrees with v3 on clean tones (<= 0.1 cent)', d.every((v) => v <= 0.1), `|v4 - v3| ${dist(d, 3)} cents over ${d.length} tones`);
}
{
  // v3 parity on a held note with loud noise bursts: wherever v3 found the right pitch, v4 must too (not null)
  const sr = SR, lost = [], rows = [];
  let v3ok = 0, v4ok = 0, n = 0;
  for (const nb of [1, 2, 3, 4, 5, 6]) for (const lvl of [0.3, 0.6, 1.0]) for (const len of [0.01, 0.03, 0.06]) {
    const x = tone(60, sr, 3, 12, 0.25, nb);
    for (let c = 0; c < nb; c++) {
      const at = Math.round((0.3 + c * 2.4 / nb) * sr), L = Math.round(len * sr);
      for (let i = 0; i < L && at + i < x.length; i++) x[at + i] += lvl * (hash01(c * 31 + nb, i) * 2 - 1) * Math.exp(-i / (L / 3));
    }
    const p = SLAnalysis.detectPitch([x], sr), q = v3Pitch(x, sr); n++;
    const g4 = p && Math.abs(p.midi + p.cents / 100 - 60) < 0.1, g3 = q != null && Math.abs(q - 60) < 0.1;
    v4ok += g4 ? 1 : 0; v3ok += g3 ? 1 : 0;
    if (g3 && !g4) lost.push(`${nb} bursts x${lvl} ${len * 1000} ms -> ${p ? p.midi + ' ' + p.cents + 'c' : 'null'}`);
  }
  check('E detectPitch v3 parity with loud transients (no correct v3 pitch lost)', lost.length === 0,
    `C3 found: v4 ${v4ok}/${n}, v3 ${v3ok}/${n}; lost by v4: ${lost.join('; ') || 'none'}`);
}
{
  // a single bad sample must not change the answer, nor put NaN in it
  const rows = [];
  let ok = true;
  for (const bad of [NaN, Infinity]) for (const where of [0.1, 0.5, 0.9]) {
    const x = tone(60, SR, 3, 12); x[Math.round(where * x.length)] = bad;
    let p; try { p = SLAnalysis.detectPitch([x], SR); } catch (e) { p = { err: e.message }; }
    const good = p && p.midi === 60 && Math.abs(p.cents) <= 3;
    if (!good) ok = false;
    rows.push(`${bad}@${where} ${p ? (p.err ? 'throws' : p.midi + '/' + p.cents) : 'null'}`);
  }
  check('E detectPitch survives one NaN/inf sample', ok, rows.join(', '));
}
{
  // the three functions never modify their input; mono [x] and stereo [x, x] give the same answer
  const song = tone(55, SR, 8, 10), copy = Float32Array.from(song), song2 = Float32Array.from(song);
  const before = fnv([song, song2]);
  const a = SLAnalysis.detectPitch([song, song2], SR), b = SLAnalysis.detectKey([song, song2], SR), c = SLAnalysis.suggestParts([song, song2], SR);
  SLAnalysis.suggestParts([song, song2], SR, { debug: true });
  const after = fnv([song, song2]);
  const m1 = JSON.stringify([SLAnalysis.detectPitch([copy], SR), SLAnalysis.detectKey([copy], SR), SLAnalysis.suggestParts([copy], SR)]);
  check('E inputs unmodified; mono = stereo', before === after && m1 === JSON.stringify([a, b, c]), `checksum ${before.toString(16)} -> ${after.toString(16)}; mono vs stereo ${m1 === JSON.stringify([a, b, c]) ? 'identical' : 'DIFFER'}`);
}

/* ================= F. detectKey ================= */
function prog(chords, sr, secs) {
  const n = Math.round(secs * sr), x = new Float32Array(n * chords.length);
  chords.forEach((ch, ci) => {
    for (const m of ch) {
      const f = 440 * Math.pow(2, (m - 69) / 12);
      for (let k = 1; k <= 6; k++) {
        if (k * f > 0.45 * sr) break;
        const w = 2 * Math.PI * k * f / sr, a = 0.3 / k, ph = hash01(m, k) * 6.28;
        for (let i = 0; i < n; i++) x[ci * n + i] += a * Math.sin(w * i + ph) * Math.min(1, i / 400, (n - i) / 400);
      }
    }
  });
  return x;
}
function v3Key(d, sr) {
  const N = 4096, frames = Math.min(40, Math.floor(d.length / N)); if (frames < 1) return null;
  const chroma = new Float32Array(12), win = new Float32Array(N); for (let i = 0; i < N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N);
  for (let f = 0; f < frames; f++) {
    const st = Math.floor(f * (d.length - N) / Math.max(1, frames - 1));
    for (let m = 36; m < 96; m++) { const w = 2 * Math.PI * 440 * Math.pow(2, (m - 69) / 12) / sr, co = 2 * Math.cos(w); let s1 = 0, s2 = 0; for (let i = 0; i < N; i++) { const s0 = d[st + i] * win[i] + co * s1 - s2; s2 = s1; s1 = s0; } chroma[m % 12] += Math.sqrt(Math.max(0, s1 * s1 + s2 * s2 - co * s1 * s2)); }
  }
  const MA = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88], MI = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
  const corr = (a, b) => { const ma = a.reduce((x, y) => x + y) / 12, mb = b.reduce((x, y) => x + y) / 12; let n = 0, da = 0, db = 0; for (let i = 0; i < 12; i++) { n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) ** 2; db += (b[i] - mb) ** 2; } return n / Math.sqrt(da * db || 1); };
  let best = { r: -2 }; for (let k = 0; k < 12; k++) for (const [pr, sc] of [[MA, 'Major'], [MI, 'Minor']]) { const rot = Array.from({ length: 12 }, (_, i) => chroma[(i + k) % 12]); const r = corr(rot, pr); if (r > best.r) best = { r, key: k, scale: sc }; }
  return best;
}
const KEYN = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
function cadence(root, scale) {
  const r = root, t = scale === 'Major' ? 4 : 3;
  return [[r - 12, r, r + t, r + 7], [r - 7, r + 5, r + 5 + t, r + 12], [r - 5, r + 7, r + 11, r + 14], [r - 12, r, r + t, r + 7]];
}
{
  // all 24 keys, chords around C3, at 44.1 and 48 kHz
  const miss = [];
  let n = 0, shape = true;
  for (const sr of [44100, 48000]) for (const scale of ['Major', 'Minor']) for (let key = 0; key < 12; key++) {
    const x = prog(cadence(60 + key, scale), sr, 1.5), k = SLAnalysis.detectKey([x], sr); n++;
    if (!k || !Number.isInteger(k.key) || k.key < 0 || k.key > 11 || (k.scale !== 'Major' && k.scale !== 'Minor')) shape = false;
    if (!k || k.key !== key || k.scale !== scale) miss.push(`${KEYN[key]} ${scale}@${sr} -> ${k ? KEYN[k.key] + ' ' + k.scale : 'null'}`);
  }
  check('F detectKey finds all 24 keys (I-IV-V-I around C3)', miss.length === 0 && shape, `${n - miss.length}/${n} right; result shape ok ${shape}` + (miss.length ? '; ' + miss.join(', ') : ''));
  // port fidelity, including the low register where v3 itself is weak
  let agree = 0, m = 0, right = 0;
  const lows = [];
  for (const scale of ['Major', 'Minor']) for (let key = 0; key < 12; key++) {
    const x = prog(cadence(48 + key, scale), SR, 1.5), k = SLAnalysis.detectKey([x], SR), q = v3Key(x, SR); m++;
    if (k && q && k.key === q.key && k.scale === q.scale) agree++;
    if (k && k.key === key && k.scale === scale) right++; else lows.push(`${KEYN[key]} ${scale} -> ${k ? KEYN[k.key] + ' ' + k.scale : 'null'}`);
  }
  check('F detectKey agrees with v3 (chords an octave lower)', agree === m, `${agree}/${m} identical to v3`);
  info(`detectKey with the bass around C1-B1: ${right}/${m} right (v3 behaves the same); misses ${lows.join(', ') || 'none'}`);
}
{
  const x = prog(cadence(60, 'Major'), SR, 1.5);
  const y = Float32Array.from(x); y[1000] = NaN;
  const k1 = SLAnalysis.detectKey([y], SR);
  const tiny = SLAnalysis.detectKey([x.subarray(0, 4095)], SR);
  check('F detectKey: one NaN sample does not erase the key', !!k1 && k1.key === 0 && k1.scale === 'Major', `C major with one NaN -> ${JSON.stringify(k1)}`);
  info(`detectKey on 4095 samples -> ${JSON.stringify(tiny)}`);
}

/* ================= G. suggestParts ================= */
function constraints(ps, dur) {
  const errs = [];
  if (!Array.isArray(ps)) return { errs: ['not an array'], lens: [] };
  if (ps.length > 4) errs.push(ps.length + ' parts');
  const kinds = new Set(), order = { steady: 0, hit: 1, quiet: 2, loop: 3 }, lens = [];
  let last = -1;
  for (const p of ps) {
    if (!(p.kind in order)) errs.push('kind ' + p.kind);
    if (kinds.has(p.kind)) errs.push('duplicate ' + p.kind);
    kinds.add(p.kind);
    if (order[p.kind] < last) errs.push('order');
    last = order[p.kind];
    if (typeof p.label !== 'string' || !p.label) errs.push('label');
    if (![p.start, p.end].every(Number.isFinite) || !(p.start >= 0 && p.end <= 1 && p.end > p.start)) errs.push(p.kind + ' start/end ' + p.start + '..' + p.end);
    const len = (p.end - p.start) * dur; lens.push(len);
    if (!(len >= 1 - 1e-6 && len <= 4 + 1e-6)) errs.push(`${p.kind} ${f2(len, 4)} s`);
    if (p.kind === 'loop') {
      if (!(p.bpm >= 60 && p.bpm <= 200)) errs.push('loop bpm ' + p.bpm);
      if (Math.abs(len - 240 / p.bpm) > 2 / SR) errs.push(`loop ${f2(len, 4)} s is not one bar at ${p.bpm}`);
      if (!p.tempoFound && (p.bpm !== 120 || !/\(guess\)/.test(p.label))) errs.push('guessed tempo not 120 / not labelled (guess)');
      if (p.tempoFound && /\(guess\)/.test(p.label)) errs.push('found tempo labelled (guess)');
    }
    const prefix = { steady: 'Steady note ', hit: 'Big hit ', quiet: 'Quiet texture ', loop: 'One-bar loop ' }[p.kind];
    if (prefix && p.label.indexOf(prefix) !== 0) errs.push(`label "${p.label}"`);
    const mt = /(\d+):(\d\d)$/.exec(p.label || '');
    if (!mt) errs.push(`label has no m:ss "${p.label}"`);
    else { const ts = +mt[1] * 60 + +mt[2]; if (ts < Math.floor(p.start * dur + 1e-9) - 0 || ts > Math.ceil(p.end * dur)) errs.push(`label time ${mt[0]} outside ${f2(p.start * dur, 2)}-${f2(p.end * dur, 2)} s`); }
  }
  for (let i = 0; i < ps.length; i++) for (let j = i + 1; j < ps.length; j++) {
    const a = ps[i], b = ps[j], ov = Math.min(a.end, b.end) - Math.max(a.start, b.start);
    if (Math.max(0, ov) / Math.min(a.end - a.start, b.end - b.start) > 0.5 + 1e-9) errs.push(`${a.kind}/${b.kind} overlap`);
  }
  return { errs, lens };
}
function resample(x, srFrom, srTo) {
  const n = Math.floor(x.length * srTo / srFrom), y = new Float32Array(n), r = srFrom / srTo;
  for (let i = 0; i < n; i++) { const p = i * r, i0 = Math.floor(p), fr = p - i0; y[i] = x[i0] * (1 - fr) + (i0 + 1 < x.length ? x[i0 + 1] : x[i0]) * fr; }
  return y;
}
{
  // short files (1.2-4 s) at common sample rates: every Region must be 1-4 s (blueprint section 4)
  const bad = [], seen = [];
  const beat44 = made.beat.channels[0];
  for (const sr of [44100, 48000, 96000, 32000, 22050, 16000]) {
    for (const secs of [1.2, 1.5, 2, 3, 4]) {
      const files = [['tone', tone(57, sr, secs, 12)], ['beat', resample(beat44, SR, sr).subarray(0, Math.round(secs * sr))]];
      for (const [nm, x] of files) {
        const ps = SLAnalysis.suggestParts([x], sr), c = constraints(ps, x.length / sr);
        c.lens.forEach((l) => seen.push(l));
        if (c.errs.length) bad.push(`${nm} ${secs}s@${sr}: ${c.errs.join(' ')}`);
      }
    }
  }
  check('G short files at 6 sample rates: every part 1-4 s and valid', bad.length === 0, `part lengths ${dist(seen, 4)} s` + (bad.length ? `; ${bad.length} bad files, e.g. ${bad.slice(0, 6).join('; ')}` : ''));
  // just under / at the 1.2 s threshold
  const a = SLAnalysis.suggestParts([tone(57, SR, 1.19, 8)], SR), b = SLAnalysis.suggestParts([tone(57, SR, 1.2, 8)], SR);
  info(`1.19 s -> ${a.length} parts, 1.20 s -> ${b.length} parts (${b.map((p) => p.kind).join(' ')})`);
}
{
  // the starter sounds themselves (what the UI shows first): constraints incl. one-bar loop length and labels
  const rows = [], bad = [];
  for (const id of IDS) {
    const s = made[id], dur = s.channels[0].length / SR, ps = SLAnalysis.suggestParts(s.channels, SR), c = constraints(ps, dur);
    rows.push(`${id} [${ps.map((p) => p.kind + (p.kind === 'loop' ? ' ' + p.bpm + (p.tempoFound ? '' : '?') : '')).join(', ')}]`);
    if (c.errs.length) bad.push(id + ': ' + c.errs.join(' '));
  }
  check('G starter sounds: parts valid incl. loop = one bar, label format', bad.length === 0, rows.join('; ') + (bad.length ? '; bad: ' + bad.join('; ') : ''));
}
{
  // The 120 bpm Beat loop, as shipped, rotated (downbeat at 0.05/0.1/0.3 s) and repeated 1/2/4 times. A one-bar loop must start
  // in time: 0-10 ms before a 16th-note slot (the module adds a 5 ms pre-roll; starting after the slot cuts the attack off). How many start on the bar line is printed.
  const rows = [];
  let inTime = 0, onBar = 0, n = 0;
  for (const rot of [0, 0.05, 0.1, 0.3]) for (const reps of [1, 2, 4]) {
    const n0 = Math.round(rot * SR), N = made.beat.channels[0].length;
    const ch = made.beat.channels.map((c) => { const y = new Float32Array(N * reps); for (let r = 0; r < reps; r++) for (let i = 0; i < N; i++) y[r * N + i] = c[(i - n0 + N) % N]; return y; });
    const dur = N * reps / SR, lp = SLAnalysis.suggestParts(ch, SR).find((p) => p.kind === 'loop');
    n++;
    if (!lp) { rows.push(`rot ${rot} x${reps}: none`); continue; }
    const t = (((lp.start * dur - rot) % 2) + 2) % 2;                 // position inside the bar, s
    const g16 = t - Math.round(t / 0.125) * 0.125, gBar = Math.min(t, 2 - t);   // signed: negative = before the slot
    const okT = lp.bpm === 120 && g16 >= -0.010 && g16 <= 0.001 && Math.abs((lp.end - lp.start) * dur - 2) < 1e-4;
    if (okT) inTime++;
    if (gBar <= 0.010) onBar++;
    rows.push(`rot ${rot} x${reps}: ${f2(t, 4)} s into the bar` + (okT ? '' : ` (OUT OF TIME: ${f2(g16 * 1000, 1)} ms from the 16th slot)`));
  }
  check('G beat starter: one-bar loop starts in time (0-10 ms before a 16th slot)', inTime === n, `${inTime}/${n} in time, ${onBar}/${n} on the bar line; ${rows.join(', ')}`);
}
{
  // A one-shot (crash + kick) trimmed to start at sample 0, as sample files usually are, must still offer its "Big hit"
  const rows = [];
  let ok = true;
  for (const pre of [0, 0.005, 0.05, 0.2]) for (const secs of [1.5, 3, 6]) {
    const n = Math.round(secs * SR), x = new Float32Array(n), a = Math.round(pre * SR);
    for (let i = a; i < n; i++) { const t = (i - a) / SR; x[i] = (hash01(1, i) * 2 - 1) * 0.6 * Math.exp(-t / 0.5) + 0.5 * Math.sin(2 * Math.PI * (50 * t + 3 * (1 - Math.exp(-t / 0.03)))) * Math.exp(-t / 0.3); }
    const hp = SLAnalysis.suggestParts([x], SR).find((p) => p.kind === 'hit');
    const good = !!hp && pre - hp.start * secs >= -0.001 && pre - hp.start * secs <= 0.1;
    if (!good) ok = false;
    rows.push(`hit at ${pre} s in ${secs} s: ${hp ? 'part ' + f2(hp.start * secs, 3) + '-' + f2(hp.end * secs, 3) : 'NONE'}`);
  }
  check('G one-shot starting at sample 0 still gets its "Big hit"', ok, rows.join(', '));
}

// an independent song (different arrangement from the builder's): 100 bpm beat, a hit ON a held saw, quiet tail at the end
const snd = {};
for (const id of ['beat', 'pad', 'saw', 'pluck', 'noise']) snd[id] = made[id];
const beat100 = [resample(snd.beat.channels[0], SR, SR * 120 / 100), resample(snd.beat.channels[1], SR, SR * 120 / 100)];  // 100 bpm, 4.8 s
function buildSong() {
  const T = 120, N = T * SR, L = new Float32Array(N), R = new Float32Array(N), fade = Math.round(0.02 * SR);
  const add = (src, gain, t0, t1) => {
    const len = src[0].length, a = Math.round(t0 * SR), b = Math.round(t1 * SR);
    for (let i = a; i < b; i++) { const j = (i - a) % len, g = gain * Math.min(1, (i - a) / fade, (b - i) / fade); L[i] += src[0][j] * g; R[i] += src[1][j] * g; }
  };
  add(beat100, 0.85, 0, 25); add(snd.pad.channels, 0.12, 0, 25);
  add(snd.saw.channels, 0.5, 25, 60);
  add(snd.pluck.channels, 0.6, 60, 90);
  add(snd.noise.channels, 0.025, 90, 120);
  // the hit: a kick + seeded crash at 41.000 s, on top of the held saw
  const a = 41 * SR, k = snd.beat.channels[0];
  for (let i = 0; i < 2.5 * SR; i++) {
    const fi = Math.min(1, i / 88);
    let v = (hash01(7, i) * 2 - 1) * 0.6 * Math.exp(-i / SR / 0.4) * fi;
    if (i < 0.45 * SR) v += k[i] * 1.5 * fi * Math.min(1, (0.45 * SR - i) / 400);
    L[a + i] += v; R[a + i] += v;
  }
  let pk = 0;
  for (let i = 0; i < N; i++) pk = Math.max(pk, Math.abs(L[i]), Math.abs(R[i]));
  for (let i = 0; i < N; i++) { L[i] *= 0.9 / pk; R[i] *= 0.9 / pk; }
  return [L, R];
}
const song = buildSong(), SONG_T = 120;
function judge(ps, dur, shift) {
  const k = {};
  for (const p of ps) k[p.kind] = { s: p.start * dur - shift, e: p.end * dur - shift, bpm: p.bpm, found: p.tempoFound };
  const inside = (r, a, b) => (r ? (Math.min(r.e, b) - Math.max(r.s, a)) / (r.e - r.s) : 0);
  const res = {
    steady: !!k.steady && inside(k.steady, 25, 60) >= 0.95 && (k.steady.e <= 41 || k.steady.s >= 41.5),
    hit: !!k.hit && 41 - k.hit.s >= 0 && 41 - k.hit.s <= 0.1,
    quiet: !!k.quiet && inside(k.quiet, 90, 120) >= 0.95,
    loop: !!k.loop && k.loop.found && ((Math.abs(k.loop.bpm - 100) <= 1 && inside(k.loop, 0, 25) >= 0.99) || (Math.abs(k.loop.bpm - 120) <= 1 && inside(k.loop, 60, 90) >= 0.99)),
  };
  const txt = ['steady', 'hit', 'quiet', 'loop'].map((n) => `${n} ${k[n] ? f2(k[n].s, 3) + '-' + f2(k[n].e, 3) + (n === 'loop' ? ' @' + k[n].bpm + (k[n].found ? '' : '?') : '') : 'none'}${res[n] ? '' : ' (WRONG)'}`).join(', ');
  return { ok: res.steady && res.hit && res.quiet && res.loop, txt, k };
}
let base;
{
  const ps = SLAnalysis.suggestParts(song, SR), c = constraints(ps, SONG_T);
  base = judge(ps, SONG_T, 0);
  check('G new song: steady in the saw (not across the hit), hit at 41.000 s, quiet tail, loop at 100 or 120 bpm', base.ok && c.errs.length === 0,
    base.txt + (c.errs.length ? '; ' + c.errs.join(' ') : ''));
}
{
  // same song at other sample rates
  const rows = [];
  let ok = true;
  for (const sr of [48000, 96000, 22050]) {
    const s2 = [resample(song[0], SR, sr), resample(song[1], SR, sr)], dur = s2[0].length / sr;
    const t = process.hrtime.bigint(), ps = SLAnalysis.suggestParts(s2, sr), ms = Number(process.hrtime.bigint() - t) / 1e6;
    const j = judge(ps, dur, 0), c = constraints(ps, dur);
    if (!j.ok || c.errs.length) ok = false;
    rows.push(`${sr}: ${j.txt}${c.errs.length ? ' ' + c.errs.join(' ') : ''} [${f2(ms, 0)} ms]`);
  }
  check('G new song at 48/96/22.05 kHz: same parts', ok, rows.join(' | '));
}
{
  // gain and time shift: everything is relative to the file, so -26 dB and 0.7 s of leading silence change nothing but the offset
  const rows = [];
  let ok = true;
  for (const [g, pre] of [[0.05, 0], [1, 0.7], [0.05, 0.7]]) {
    const n0 = Math.round(pre * SR), s2 = song.map((ch) => { const y = new Float32Array(ch.length + n0); for (let i = 0; i < ch.length; i++) y[i + n0] = ch[i] * g; return y; });
    const dur = s2[0].length / SR, ps = SLAnalysis.suggestParts(s2, SR), j = judge(ps, dur, pre);
    let drift = 0;
    if (j.k.hit && base.k.hit) drift = Math.abs(j.k.hit.s - base.k.hit.s); else drift = Infinity;
    if (!j.ok || drift > 0.01) ok = false;
    rows.push(`gain ${g}, +${pre} s: ${j.txt}; hit moved ${f2(drift * 1000, 1)} ms`);
  }
  check('G gain and leading silence: same parts, hit moves <= 10 ms', ok, rows.join(' | '));
}
{
  // a single NaN in the song: no phantom "Big hit" at the bad sample, and the rest still found
  const rows = [];
  let ok = true;
  for (const t of [12.3, 50.5, 100.25]) {
    const s2 = song.map((ch) => Float32Array.from(ch)); s2[0][Math.round(t * SR)] = NaN;
    let ps;
    try { ps = SLAnalysis.suggestParts(s2, SR); } catch (e) { ok = false; rows.push(`NaN@${t}: throws ${e.message}`); continue; }
    const c = constraints(ps, SONG_T), j = judge(ps, SONG_T, 0);
    if (c.errs.length || !j.ok) ok = false;
    rows.push(`NaN@${t}s: ${j.txt}${c.errs.length ? ' ' + c.errs.join(' ') : ''}`);
  }
  check('G one NaN sample in the song changes nothing', ok, rows.join(' | '));
}
{
  // debug option must not change the answer; repeated calls (FFT plan cache warm) identical
  const a = JSON.stringify(SLAnalysis.suggestParts(song, SR)), b = JSON.stringify(SLAnalysis.suggestParts(song, SR, { debug: true }).parts);
  SLAnalysis.suggestParts([tone(60, 32000, 7, 4)], 32000);
  const c = JSON.stringify(SLAnalysis.suggestParts(song, SR));
  check('G debug option and call order do not change parts', a === b && a === c, `plain vs debug ${a === b ? 'same' : 'DIFFER'}, after another rate ${a === c ? 'same' : 'DIFFER'}`);
}
{
  // other tempos, 8 bars of the beat loop resampled (the builder tests 92-140)
  const rows = [];
  let ok = true;
  for (const bpm of [70, 87, 160, 174]) {
    const one = resample(snd.beat.channels[0], SR, SR * 120 / bpm), x = new Float32Array(one.length * 4);
    for (let r = 0; r < 4; r++) x.set(one, r * one.length);   // 4 loops = 8 bars (the builder uses 16 bars)
    const ps = SLAnalysis.suggestParts([x], SR), lp = ps.find((p) => p.kind === 'loop');
    const got = lp ? lp.bpm : NaN, rel = got / bpm;
    const octaveOk = lp && lp.tempoFound && [0.5, 1, 2].some((m) => Math.abs(rel - m) * bpm <= 1 * m);
    if (!octaveOk) ok = false;
    rows.push(`${bpm} -> ${lp ? got + (lp.tempoFound ? '' : ' (guess)') : 'none'}`);
  }
  check('G tempo 70/87/160/174 bpm found (or its double/half)', ok, rows.join(', '));
}
{
  // speed: 5 minutes at 96 kHz stereo, and 5 minutes of dense onsets (worst case for the candidate loops)
  const T = 300;
  const big = [new Float32Array(T * 96000), new Float32Array(T * 96000)];
  const src = [resample(song[0], SR, 96000), resample(song[1], SR, 96000)];
  for (let c = 0; c < 2; c++) for (let i = 0; i < big[c].length; i++) big[c][i] = src[c][i % src[c].length];
  let t = process.hrtime.bigint();
  const p96 = SLAnalysis.suggestParts(big, 96000);
  const ms96 = Number(process.hrtime.bigint() - t) / 1e6;
  const dense = new Float32Array(T * SR);
  for (let i = 0; i < dense.length; i++) { const ph = i % 2205; dense[i] = (hash01(i, 3) * 2 - 1) * Math.exp(-ph / 300) * (0.3 + 0.7 * hash01(Math.floor(i / 2205), 9)); }
  t = process.hrtime.bigint();
  const pd = SLAnalysis.suggestParts([dense, dense], SR);
  const msd = Number(process.hrtime.bigint() - t) / 1e6;
  const c1 = constraints(p96, T), c2 = constraints(pd, T);
  check('G 5 minutes < 1.5 s at 96 kHz and with an onset every 50 ms', ms96 < 1500 && msd < 1500 && !c1.errs.length && !c2.errs.length,
    `96 kHz stereo ${f2(ms96, 0)} ms (${p96.map((p) => p.kind).join(' ')}), dense onsets ${f2(msd, 0)} ms (${pd.map((p) => p.kind + (p.bpm ? ' ' + p.bpm : '')).join(' ')})` +
    (c1.errs.length || c2.errs.length ? '; ' + c1.errs.concat(c2.errs).join(' ') : ''));
  // D10 on the picks page: "Files up to 10 minutes load in full". All three analyses on 10 minutes of 48 kHz stereo.
  const ten = [new Float32Array(600 * 48000), new Float32Array(600 * 48000)];
  const s48 = [resample(song[0], SR, 48000), resample(song[1], SR, 48000)];
  for (let c = 0; c < 2; c++) for (let i = 0; i < ten[c].length; i++) ten[c][i] = s48[c][i % s48[c].length];
  t = process.hrtime.bigint(); const pt = SLAnalysis.suggestParts(ten, 48000); const a = Number(process.hrtime.bigint() - t) / 1e6;
  t = process.hrtime.bigint(); SLAnalysis.detectPitch(ten, 48000); const b = Number(process.hrtime.bigint() - t) / 1e6;
  t = process.hrtime.bigint(); SLAnalysis.detectKey(ten, 48000); const c = Number(process.hrtime.bigint() - t) / 1e6;
  info(`10 minutes 48 kHz stereo: suggestParts ${f2(a, 0)} ms (${pt.length} parts), detectPitch ${f2(b, 0)} ms, detectKey ${f2(c, 0)} ms`);
}

/* ================= H. plain-script contract (BLUEPRINT section 0) ================= */
{
  const fs = require('fs'), vm = require('vm'), rows = [];
  let ok = true;
  for (const [file, name] of [['sl-sounds.js', 'SLSounds'], ['sl-analysis.js', 'SLAnalysis']]) {
    const src = fs.readFileSync(path.join(__dirname, '..', 'src', file), 'utf8');
    const code = src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
    const sandbox = {};                    // no module, no window, no require: a browser-like script context
    let err = '';
    try { vm.runInNewContext(src, sandbox, { filename: file }); } catch (e) { err = e.message; }
    const globals = Object.keys(sandbox);
    const bad = [];
    if (err) bad.push('throws without module: ' + err);
    if (JSON.stringify(globals) !== JSON.stringify([name])) bad.push('globals ' + JSON.stringify(globals));
    if (/Math\.random/.test(code)) bad.push('uses Math.random');
    if (/\brequire\s*\(|^\s*import\s|^\s*export\s/m.test(code)) bad.push('import/export/require');
    if (!/if \(typeof module !== 'undefined'\) module\.exports = /.test(src)) bad.push('no module.exports line');
    if (bad.length) ok = false;
    rows.push(`${file}: ${bad.length ? bad.join(', ') : 'one global ' + name + ', no Math.random, no imports'}`);
  }
  check('H plain scripts with exactly one global each', ok, rows.join('; '));
}
{
  const rms = IDS.map((id) => { let e = 0; const [L, R] = made[id].channels; for (let i = 0; i < L.length; i++) e += L[i] * L[i] + R[i] * R[i]; return id + ' ' + f2(10 * Math.log10(e / (2 * L.length)), 1); });
  info('RMS level (dBFS) at peak 0.8: ' + rms.join(', ') + ' (peak-normalised, so switching sounds changes loudness by this much)');
}

console.log(`\ntest-sounds-extra: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
