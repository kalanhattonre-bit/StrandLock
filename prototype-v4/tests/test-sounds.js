// tests/test-sounds.js - SLSounds checks (node tests/test-sounds.js). One line per check; exits 1 on any FAIL.
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
  const a = arr.slice().sort((x, y) => x - y), q = (p) => a[Math.min(a.length - 1, Math.round(p * (a.length - 1)))];
  return 'min ' + f2(a[0], d) + ' / median ' + f2(q(0.5), d) + ' / max ' + f2(a[a.length - 1], d);
}

/* ---------- independent FFT (test-local; does not use SLAnalysis) ---------- */
function fftInPlace(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr0 = Math.cos(ang), wi0 = Math.sin(ang), half = len >> 1;
    for (let i = 0; i < n; i += len) {
      let wr = 1, wi = 0;
      for (let k = 0; k < half; k++) {
        const a = i + k, b = a + half;
        const tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const t = wr * wr0 - wi * wi0; wi = wr * wi0 + wi * wr0; wr = t;
      }
    }
  }
}
// Power spectrum of the first 2^18 samples of the mono mix, 4-term Blackman-Harris window (sidelobes -92 dB).
function spectrum(channels, sr) {
  const n = 1 << 18, re = new Float64Array(n), im = new Float64Array(n);
  const a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
  for (let i = 0; i < n; i++) {
    const x = 2 * Math.PI * i / (n - 1);
    const w = a0 - a1 * Math.cos(x) + a2 * Math.cos(2 * x) - a3 * Math.cos(3 * x);
    let s = 0; for (const ch of channels) s += ch[i]; re[i] = (s / channels.length) * w;
  }
  fftInPlace(re, im);
  const pw = new Float64Array(n / 2);
  for (let i = 0; i < n / 2; i++) pw[i] = re[i] * re[i] + im[i] * im[i];
  return { pw, binHz: sr / n };
}
// Fundamental from the spectrum: strongest peak 40-2000 Hz, then the lowest sub-multiple that still has a peak
// within -26 dB of it, refined by parabolic interpolation on the log power.
function fftF0(sp) {
  const { pw, binHz } = sp, lo = Math.ceil(40 / binHz), hi = Math.floor(2000 / binHz);
  let pk = lo;
  for (let i = lo; i <= hi; i++) if (pw[i] > pw[pk]) pk = i;
  const peakNear = (hz) => {
    const c = Math.round(hz / binHz), w = Math.max(3, Math.round(c * 0.01));
    let b = c - w; for (let i = c - w; i <= c + w; i++) if (pw[i] > pw[b]) b = i;
    return b;
  };
  let f0bin = pk;
  for (let n = 8; n >= 2; n--) {
    const hz = pk * binHz / n;
    if (hz < 40) continue;
    const b = peakNear(hz);
    if (pw[b] >= pw[pk] * Math.pow(10, -2.6) && pw[b] >= pw[b - 1] && pw[b] >= pw[b + 1]) { f0bin = b; break; }
  }
  const la = Math.log(pw[f0bin - 1] + 1e-300), lb = Math.log(pw[f0bin] + 1e-300), lc = Math.log(pw[f0bin + 1] + 1e-300);
  const den = la - 2 * lb + lc, sh = den ? 0.5 * (la - lc) / den : 0;
  return (f0bin + sh) * binHz;
}
// Energy not on the harmonic lines of f0 (each line +-10 Hz, plus +0.4% for the pad's chorus copy), in dB of the total.
function inharmonicDb(sp, f0, allowChorus) {
  const { pw, binHz } = sp;
  let tot = 0, off = 0;
  const lo = Math.ceil(20 / binHz);
  for (let i = lo; i < pw.length; i++) {
    const hz = i * binHz, k = Math.max(1, Math.round(hz / f0));
    let d = Math.abs(hz - k * f0);
    if (allowChorus) { const kc = Math.max(1, Math.round(hz / (f0 * 1.0035))); d = Math.min(d, Math.abs(hz - kc * f0 * 1.0035)); }
    tot += pw[i];
    if (d > 10) off += pw[i];
  }
  return 10 * Math.log10(off / tot + 1e-30);
}
function bandDb(sp, fromHz) {
  const { pw, binHz } = sp;
  let tot = 0, hi = 0;
  for (let i = 1; i < pw.length; i++) { tot += pw[i]; if (i * binHz >= fromHz) hi += pw[i]; }
  return 10 * Math.log10(hi / tot + 1e-30);
}

/* ---------- seam ---------- */
// The wrap from the last sample to the first, measured against the 10 ms on each side of it.
function seam(ch, sr) {
  const N = ch.length, W = Math.round(0.01 * sr);
  const at = (i) => ch[((i % N) + N) % N];
  let maxStep = 0, maxD2 = 0;
  for (let i = -W; i < W; i++) {          // step i -> i+1 for i in the window, excluding the seam step (-1 -> 0)
    if (i === -1) continue;
    maxStep = Math.max(maxStep, Math.abs(at(i + 1) - at(i)));
    if (i !== -2 && i !== 0) maxD2 = Math.max(maxD2, Math.abs(at(i + 1) - 2 * at(i) + at(i - 1)));
  }
  const step = Math.abs(at(0) - at(-1));
  const d2 = Math.max(Math.abs(at(0) - 2 * at(-1) + at(-2)), Math.abs(at(1) - 2 * at(0) + at(-1)));
  const rms = (a, b) => { let s = 0; for (let i = a; i < b; i++) s += at(i) ** 2; return Math.sqrt(s / (b - a)); };
  const drms = (a, b) => { let s = 0; for (let i = a; i < b; i++) s += (at(i + 1) - at(i)) ** 2; return Math.sqrt(s / (b - a)); };
  const lastR = rms(-W, 0), firstR = rms(0, W);
  const lastB = drms(-W, -1) / lastR, firstB = drms(0, W - 1) / firstR;
  // where the seam step ranks among every step in the file
  let bigger = 0;
  for (let i = 0; i + 1 < N; i++) if (Math.abs(ch[i + 1] - ch[i]) > step) bigger++;
  // how much neighbouring 10 ms windows differ anywhere in the file (noise differs a lot by nature)
  const lv = [], bv = [];
  for (let st = 0; st + W + 1 < N; st += W) {
    let e = 0, d = 0;
    for (let i = st; i < st + W; i++) { e += ch[i] * ch[i]; d += (ch[i + 1] - ch[i]) ** 2; }
    lv.push(10 * Math.log10(e / W + 1e-20)); bv.push(10 * Math.log10(d / (e + 1e-20)));
  }
  const p95 = (v) => { const d = []; for (let i = 1; i < v.length; i++) d.push(Math.abs(v[i] - v[i - 1])); d.sort((a, b) => a - b); return d[Math.round(0.95 * (d.length - 1))]; };
  return { stepRatio: step / (maxStep || 1e-12), d2Ratio: d2 / (maxD2 || 1e-12), levelDb: Math.abs(20 * Math.log10(lastR / firstR)),
    brightDb: Math.abs(20 * Math.log10(lastB / firstB)), stepPct: 100 * (1 - bigger / (N - 1)), step, lvP95: p95(lv), brP95: p95(bv) };
}

// Pitched loops repeat every period, across the seam too: compare each sample with the one 16 periods later
// (16 C3 periods = 2696.98 samples, so a 2697-sample lag is off by only 0.016 sample) for the pairs that straddle the
// seam, against the same residual just before and just after it. A cut or mis-phased loop jumps far above both.
function periodicSeam(ch, sr) {
  const N = ch.length, D = Math.round(16 * sr / SLSounds.ROOT_HZ);
  const at = (i) => ch[((i % N) + N) % N];
  const res = (j0) => { let e = 0, s = 0; for (let j = j0; j < j0 + D; j++) { const a = at(j), b = at(j + D); e += (b - a) ** 2; s += a * a; } return 20 * Math.log10(Math.sqrt(e / s) + 1e-12); };
  return { seam: res(N - D), near: Math.max(res(N - 2 * D), res(0)) };
}

/* ---------- change across the length ---------- */
function changeDb(ch, sr) {
  const W = Math.round(0.1 * sr), lv = [], br = [];
  for (let s = 0; s + W < ch.length; s += W) {
    let e = 0, d = 0;
    for (let i = s; i < s + W; i++) { e += ch[i] * ch[i]; d += (ch[i + 1] - ch[i]) ** 2; }
    lv.push(10 * Math.log10(e / W + 1e-20)); br.push(10 * Math.log10(d / (e + 1e-20)));
  }
  const rng = (a) => { const b = a.slice().sort((x, y) => x - y); return b[Math.round(0.95 * (b.length - 1))] - b[Math.round(0.05 * (b.length - 1))]; };
  return { level: rng(lv), bright: rng(br) };
}

/* ================= checks ================= */
const IDS = ['sawopen', 'saw', 'square', 'pad', 'vowel', 'pluck', 'beat', 'noise'];
const NAMES = ['Saw that opens up', 'Saw', 'Square sweep', 'Evolving pad', 'Vowel aah to ooh', 'Pluck arpeggio', 'Beat loop', 'Noise'];
const PITCHED = ['saw', 'sawopen', 'square', 'pad', 'vowel'];
const SUSTAINED = ['saw', 'sawopen', 'square', 'pad', 'vowel', 'noise'];

check('list order and names', JSON.stringify(SLSounds.list.map((s) => s.id)) === JSON.stringify(IDS) &&
  JSON.stringify(SLSounds.list.map((s) => s.name)) === JSON.stringify(NAMES), SLSounds.list.map((s) => s.id).join(', '));
check('default sound is sawopen', SLSounds.defaultId === 'sawopen' && SLSounds.list[0].id === 'sawopen', SLSounds.defaultId);
let threw = false;
try { SLSounds.make('nope'); } catch (e) { threw = true; }
check('unknown id throws', threw, String(threw));

const made = {}, times = {};
for (const id of IDS) {
  const t0 = process.hrtime.bigint();
  const a = SLSounds.make(id);
  times[id] = Number(process.hrtime.bigint() - t0) / 1e6;
  const b = SLSounds.make(id), c = SLSounds.make(id, { seed: 2 });
  made[id] = a;
  const [L, R] = a.channels, N = L.length, sr = a.sampleRate;

  check(id + ' shape', a.id === id && a.name === NAMES[IDS.indexOf(id)] && sr === 44100 && a.rootMidi === 60 &&
    a.channels.length === 2 && L instanceof Float32Array && R instanceof Float32Array && R.length === N,
    `sr ${sr}, ${a.channels.length} ch Float32Array x ${N}, rootMidi ${a.rootMidi}`);
  const secs = N / sr;
  if (id === 'beat') check(id + ' length', N === 4 * 44100, f2(secs, 5) + ' s vs exactly 4 s (2 bars at 120 bpm)');
  else check(id + ' length', Math.abs(secs - 6) <= 0.01, f2(secs, 5) + ' s vs 6 +-0.01 s');

  let same = true, diff = false;
  for (let k = 0; k < 2; k++) for (let i = 0; i < N; i++) {
    if (a.channels[k][i] !== b.channels[k][i]) same = false;
    if (c.channels[k][i] !== a.channels[k][i]) diff = true;
  }
  check(id + ' deterministic', same, 'two makes ' + (same ? 'bit-identical' : 'DIFFER'));
  check(id + ' seeded', diff, 'seed 2 ' + (diff ? 'differs from' : 'equals') + ' seed 1');

  let pk = 0, bad = 0;
  for (const ch of a.channels) for (let i = 0; i < N; i++) { const v = ch[i]; if (!Number.isFinite(v)) bad++; else pk = Math.max(pk, Math.abs(v)); }
  check(id + ' peak', bad === 0 && Math.abs(pk - 0.8) <= 0.01, 'peak ' + f2(pk, 5) + ' vs 0.8 +-0.01, non-finite ' + bad);

  // stereo: a little width, mono-safe
  let sLL = 0, sRR = 0, sLR = 0, sM = 0;
  for (let i = 0; i < N; i++) { sLL += L[i] * L[i]; sRR += R[i] * R[i]; sLR += L[i] * R[i]; sM += ((L[i] + R[i]) / 2) ** 2; }
  const corr = sLR / Math.sqrt(sLL * sRR), monoLoss = 10 * Math.log10(((sLL + sRR) / 2) / sM);
  check(id + ' stereo', corr > 0.6 && corr < 0.9999 && monoLoss < 3, 'L/R correlation ' + f2(corr, 4) + ' (0.6..0.9999), mono loss ' + f2(monoLoss) + ' dB (< 3)');

  // seam
  const sm = [seam(L, sr), seam(R, sr)];
  const worst = (k) => Math.max(sm[0][k], sm[1][k]);
  check(id + ' seam step', worst('stepRatio') <= 1 && worst('d2Ratio') <= 1,
    `wrap step ${f2(worst('stepRatio'), 3)} x and curvature ${f2(worst('d2Ratio'), 3)} x the largest in the 10 ms either side (limit 1); ` +
    `wrap step ${f2(Math.max(sm[0].step, sm[1].step), 4)}, smaller than ${f2(Math.max(sm[0].stepPct, sm[1].stepPct), 1)}% of all steps`);
  if (PITCHED.includes(id)) {
    const ps = [periodicSeam(L, sr), periodicSeam(R, sr)], d = Math.max(ps[0].seam - ps[0].near, ps[1].seam - ps[1].near);
    check(id + ' seam periodic', d <= 1, `residual vs 16 periods later across the seam ${f2(ps[0].seam, 1)} dB, just before/after ${f2(ps[0].near, 1)} dB (L); worst seam minus neighbours ${f2(d, 1)} dB (limit +1)`);
  }
  if (SUSTAINED.includes(id)) {
    const limL = Math.max(1.5, Math.min(sm[0].lvP95, sm[1].lvP95)), limB = Math.max(1.5, Math.min(sm[0].brP95, sm[1].brP95));
    check(id + ' seam match', worst('levelDb') <= limL && worst('brightDb') <= limB,
      `last vs first 10 ms: level ${f2(worst('levelDb'))} dB (limit ${f2(limL)}), brightness ${f2(worst('brightDb'))} dB (limit ${f2(limB)}); ` +
      `limit = 1.5 dB or what 95% of neighbouring 10 ms windows in the file differ by, if larger`);
  } else {
    info(`${id} seam level last vs first 10 ms ${f2(worst('levelDb'))} dB (starts on its downbeat, so level is not compared)`);
  }

  // changes across the length (every sound but saw)
  const ch = changeDb(L, sr), amount = Math.max(ch.level, ch.bright);
  if (id === 'saw') check(id + ' static', amount < 0.2, `level range ${f2(ch.level, 3)} dB, brightness range ${f2(ch.bright, 3)} dB (5-95%, 100 ms windows) < 0.2`);
  else check(id + ' changes', amount > 1, `level range ${f2(ch.level)} dB, brightness range ${f2(ch.bright)} dB (5-95%, 100 ms windows), max > 1`);
}

// pitch and band-limiting
const sweepCents = [], yinCents = [];
for (const id of PITCHED) {
  const s = made[id], sp = spectrum(s.channels, s.sampleRate);
  const p = SLAnalysis.detectPitch(s.channels, s.sampleRate);
  const yc = p ? (p.midi - 60) * 100 + p.cents : NaN;
  check(id + ' detectPitch C3', !!p && p.midi === 60 && Math.abs(p.cents) <= 10, p ? `midi ${p.midi} (C3=60) ${f2(p.cents, 1)} cents, ${f2(p.hz, 3)} Hz vs +-10 cents` : 'null');
  const f0 = fftF0(sp), fc = 1200 * Math.log2(f0 / SLSounds.ROOT_HZ);
  check(id + ' FFT f0 C3', Math.abs(fc) <= 10, `${f2(f0, 4)} Hz = ${f2(fc, 3)} cents from 261.6256 Hz vs +-10`);
  yinCents.push(yc); sweepCents.push(fc);
  const inh = inharmonicDb(sp, SLSounds.ROOT_HZ, id === 'pad'), hi = bandDb(sp, 19500);
  check(id + ' band-limited', inh < -60 && hi < -90, `energy off the harmonic lines ${f2(inh, 1)} dB (< -60), above 19.5 kHz ${f2(hi, 1)} dB (< -90)`);
}
info('detectPitch cents over the pitched sounds: ' + dist(yinCents, 2) + '; FFT cents: ' + dist(sweepCents, 3));
{
  const s = made.pluck, sp = spectrum(s.channels, s.sampleRate), hi = bandDb(sp, 19500);
  check('pluck band-limited', hi < -60, `energy above 19.5 kHz ${f2(hi, 1)} dB (< -60)`);
}
// the band-limit metric must catch real aliasing: a naive (non band-limited) saw at C3
{
  const sr = 44100, N = 1 << 18, x = new Float32Array(N), f = SLSounds.ROOT_HZ;
  for (let i = 0; i < N; i++) { const ph = (i * f / sr) % 1; x[i] = 1 - 2 * ph; }
  const sp = spectrum([x], sr), inh = inharmonicDb(sp, f, false);
  check('control: naive saw shows aliasing', inh > -40, `naive saw energy off the harmonic lines ${f2(inh, 1)} dB (must be > -40, else the metric is blind)`);
}
// sawopen really opens: spectral centroid along the length (4096-sample Hann frames)
{
  const L = made.sawopen.channels[0], N = L.length, n = 4096;
  const centroid = (frac) => {
    const st = Math.min(N - n, Math.round(frac * N)), re = new Float64Array(n), im = new Float64Array(n);
    for (let i = 0; i < n; i++) re[i] = L[st + i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / n));
    fftInPlace(re, im);
    let a = 0, b = 0;
    for (let i = 1; i < n / 2; i++) { const m = Math.hypot(re[i], im[i]); a += m * i * 44100 / n; b += m; }
    return a / b;
  };
  const fr = [0.02, 0.2, 0.4, 0.6, 0.8, 0.9, 0.97], cs = fr.map(centroid);
  info('sawopen spectral centroid (Hz) at ' + fr.map((f, i) => Math.round(f * 100) + '%: ' + Math.round(cs[i])).join(', '));
  let rising = true;
  for (let i = 1; i < 6; i++) if (!(cs[i] > cs[i - 1])) rising = false;
  check('sawopen opens up', rising && cs[5] / cs[0] > 4 && cs[6] < cs[5], `centroid rises every step 2%..90% (${rising}), 90%/2% = ${f2(cs[5] / cs[0], 1)} x (> 4), then closes for the seam (97%: ${Math.round(cs[6])} Hz)`);
}
// vowel moves from "aah" (first formant ~700 Hz) to "ooh" (~320 Hz): the strongest harmonic under 1.2 kHz moves down
{
  const L = made.vowel.channels[0], N = L.length, n = 8192, f0 = SLSounds.ROOT_HZ;
  const strongest = (frac) => {
    const st = Math.min(N - n, Math.round(frac * N)), re = new Float64Array(n), im = new Float64Array(n);
    for (let i = 0; i < n; i++) re[i] = L[st + i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / n));
    fftInPlace(re, im);
    let bestK = 1, bestM = 0;
    for (let k = 1; k * f0 < 1200; k++) { const b = Math.round(k * f0 * n / 44100); const m = Math.hypot(re[b], im[b]); if (m > bestM) { bestM = m; bestK = k; } }
    return bestK * f0;
  };
  const a = strongest(0.03), u = strongest(0.85);
  check('vowel aah -> ooh', a >= 600 && u <= 550, `strongest harmonic under 1.2 kHz: ${Math.round(a)} Hz at 3% (aah, >= 600), ${Math.round(u)} Hz at 85% (ooh, <= 550)`);
}
// the seam checks must catch a real click: the same sounds cut short by a few samples (not a whole period)
{
  const flagged = [], ratios = [];
  for (const id of ['saw', 'sawopen', 'square', 'pad', 'vowel']) {
    for (const cut of [17, 50, 83, 120]) {
      const L = made[id].channels[0], x = L.subarray(0, L.length - cut), sm = seam(x, 44100), ps = periodicSeam(x, 44100);
      const r = Math.max(sm.stepRatio, sm.d2Ratio); ratios.push(r);
      if (r > 1 || ps.seam - ps.near > 1) flagged.push(id + '-' + cut);
    }
  }
  check('control: seam checks catch cut loops', flagged.length === 20, `${flagged.length}/20 loops cut short by 17/50/83/120 samples flagged by the step/curvature or periodic check (need 20); step/curvature ratio alone ${dist(ratios, 2)}`);
}
info('make() ms: ' + IDS.map((id) => id + ' ' + f2(times[id], 0)).join(', '));
for (const id of ['pluck', 'beat', 'noise']) {
  const s = made[id], p = SLAnalysis.detectPitch(s.channels, s.sampleRate), k = SLAnalysis.detectKey(s.channels, s.sampleRate);
  info(`${id}: detectPitch ${p ? p.midi + ' ' + f2(p.cents, 1) + 'c' : 'null'}, detectKey ${k ? k.key + ' ' + k.scale + ' r ' + k.r : 'null'} (nominal root 60)`);
}

console.log(`\ntest-sounds: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
