// tests/test-analysis.js - SLAnalysis checks (node tests/test-analysis.js). One line per check; exits 1 on any FAIL.
'use strict';
const path = require('path');
const SLAnalysis = require(path.join(__dirname, '..', 'src', 'sl-analysis.js'));
const SLSounds = require(path.join(__dirname, '..', 'src', 'sl-sounds.js'));

let fails = 0, passes = 0;
function check(name, ok, msg) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name + ': ' + msg);
  if (ok) passes++; else fails++;
}
function info(msg) { console.log('INFO ' + msg); }
const f2 = (x, d = 2) => (Number.isFinite(x) ? x.toFixed(d) : String(x));
function dist(arr, d = 2) {
  const a = arr.slice().sort((x, y) => x - y), q = (p) => a[Math.min(a.length - 1, Math.round(p * (a.length - 1)))];
  return 'min ' + f2(a[0], d) + ' / median ' + f2(q(0.5), d) + ' / p90 ' + f2(q(0.9), d) + ' / max ' + f2(a[a.length - 1], d);
}
// seeded noise for the test (no Math.random)
function hash(a, b) {
  let h = (Math.imul(a | 0, 0x9e3779b1) ^ Math.imul(b | 0, 0x85ebca6b)) >>> 0;
  h ^= h >>> 16; h = Math.imul(h, 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35); h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const nameOf = (m) => NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 2);   // Cubase: 60 = C3

/* ---------- detectPitch on synthetic tones ---------- */
function tone(midi, cents, sr, secs, harmonics, seed) {
  const n = Math.round(secs * sr), x = new Float32Array(n), f = 440 * Math.pow(2, (midi + cents / 100 - 69) / 12);
  for (let k = 1; k <= harmonics; k++) {
    if (k * f > 0.45 * sr) break;
    const a = 1 / k, ph = hash(seed, k) * 2 * Math.PI, w = 2 * Math.PI * k * f / sr;
    for (let i = 0; i < n; i++) x[i] += a * Math.sin(w * i + ph);
  }
  for (let i = 0; i < n; i++) x[i] *= 0.5 * Math.min(1, i / 441, (n - i) / 441);
  return x;
}
{
  const errs = [], wrong = [];
  let count = 0;
  const midis = [36, 40, 45, 48, 52, 57, 60, 64, 67, 69, 72, 76, 79, 84];
  for (const sr of [44100, 48000]) for (const harm of [1, 8, 24]) for (const m of midis) for (const c of [0, 23, -37]) {
    const x = tone(m, c, sr, 1.2, harm, m * 7 + harm), p = SLAnalysis.detectPitch([x, x], sr);
    count++;
    if (!p) { wrong.push(`${nameOf(m)}${c ? (c > 0 ? '+' : '') + c : ''} h${harm} @${sr}: null`); continue; }
    const err = (p.midi + p.cents / 100 - (m + c / 100)) * 100;
    errs.push(Math.abs(err));
    if (Math.abs(err) > 10) wrong.push(`${nameOf(m)}${c ? (c > 0 ? '+' : '') + c : ''} h${harm} @${sr}: ${f2(err, 1)}c`);
  }
  check('detectPitch synthetic tones', wrong.length === 0, `${count - wrong.length}/${count} within +-10 cents (C1..C5, offsets 0/+23/-37 c, 1/8/24 harmonics, 44.1/48 kHz); |error| cents ${dist(errs, 2)}` + (wrong.length ? '; off: ' + wrong.slice(0, 8).join(', ') : ''));
}
// pitch must also survive a decaying note and a little noise
{
  const sr = 44100, n = sr * 2, errs = [];
  for (const m of [43, 55, 60, 67, 74]) {
    const x = tone(m, 0, sr, 2, 12, m);
    for (let i = 0; i < n; i++) x[i] = x[i] * Math.exp(-i / sr / 0.7) + 0.02 * (hash(m, i) * 2 - 1);
    const p = SLAnalysis.detectPitch([x], sr);
    errs.push(p ? Math.abs((p.midi + p.cents / 100 - m) * 100) : Infinity);
  }
  check('detectPitch decaying + noisy notes', errs.every((e) => e <= 10), `|error| cents ${dist(errs, 2)} vs +-10 (G1, G2, C3, G3, D4; -34 dB noise)`);
}
// no clear pitch
{
  const sr = 44100, res = [];
  for (let seed = 1; seed <= 6; seed++) {
    const x = new Float32Array(sr * 3);
    for (let i = 0; i < x.length; i++) x[i] = (hash(seed, i) * 2 - 1) * 0.5;
    const p = SLAnalysis.detectPitch([x, x], sr);
    res.push(p ? `${p.midi}/${f2(p.cents, 0)}` : 'null');
  }
  check('white noise -> null', res.every((r) => r === 'null'), `6 seeds: ${res.join(', ')}`);
  const sil = SLAnalysis.detectPitch([new Float32Array(sr * 2)], sr), short = SLAnalysis.detectPitch([tone(60, 0, sr, 0.03, 4, 1)], sr);
  check('silence and too-short -> null', sil === null && short === null, `silence ${JSON.stringify(sil)}, 30 ms ${JSON.stringify(short)}`);
}

/* ---------- detectKey ---------- */
function progression(chords, sr, secs) {
  const n = Math.round(secs * sr), x = new Float32Array(n * chords.length);
  chords.forEach((ch, ci) => {
    for (const m of ch) {
      const f = 440 * Math.pow(2, (m - 69) / 12);
      for (let k = 1; k <= 6; k++) {
        const w = 2 * Math.PI * k * f / sr, a = 0.3 / k, ph = hash(m, k) * 6.28;
        for (let i = 0; i < n; i++) x[ci * n + i] += a * Math.sin(w * i + ph) * Math.min(1, i / 400, (n - i) / 400);
      }
    }
  });
  return x;
}
{
  const sr = 44100;
  const tests = [
    { name: 'C major I-IV-V-I', chords: [[48, 60, 64, 67], [53, 60, 65, 69], [55, 59, 62, 67], [48, 60, 64, 67]], key: 0, scale: 'Major' },
    { name: 'G major I-IV-V-I', chords: [[43, 55, 59, 62], [48, 60, 64, 67], [50, 57, 62, 66], [43, 55, 59, 62]], key: 7, scale: 'Major' },
    { name: 'A minor i-iv-V-i', chords: [[45, 57, 60, 64], [50, 57, 62, 65], [52, 56, 59, 64], [45, 57, 60, 64]], key: 9, scale: 'Minor' },
  ];
  for (const t of tests) {
    const x = progression(t.chords, sr, 1.5), k = SLAnalysis.detectKey([x, x], sr);
    check('detectKey ' + t.name, !!k && k.key === t.key && k.scale === t.scale,
      k ? `${NAMES[k.key]} ${k.scale} (r ${k.r}) vs ${NAMES[t.key]} ${t.scale}` : 'null');
  }
  check('detectKey silence -> null', SLAnalysis.detectKey([new Float32Array(sr)], sr) === null, 'null');
}

/* ---------- suggestParts on a synthetic song ---------- */
const SR = 44100;
const snd = {};
for (const id of ['noise', 'beat', 'pad', 'saw', 'pluck']) snd[id] = SLSounds.make(id);
// sections: [sound, gain, from s, to s]; hits: [time s, gain]
function buildSong(totalSec, sections, hits) {
  const N = Math.round(totalSec * SR), L = new Float32Array(N), R = new Float32Array(N), fade = Math.round(0.02 * SR);
  for (const [id, gain, t0, t1] of sections) {
    const s = snd[id].channels, len = s[0].length, a = Math.round(t0 * SR), b = Math.min(N, Math.round(t1 * SR));
    for (let i = a; i < b; i++) {
      const j = (i - a) % len, g = gain * Math.min(1, (i - a) / fade, (b - i) / fade);
      L[i] += s[0][j] * g; R[i] += s[1][j] * g;
    }
  }
  // a loud hit: the beat's first kick (a 2 ms fade-in) plus a seeded noise crash
  const beat = snd.beat.channels, hl = Math.round(0.45 * SR), cl = Math.round(2.5 * SR);
  for (const [t, gain] of hits) {
    const a = Math.round(t * SR);
    for (let i = 0; i < Math.max(hl, cl) && a + i < N; i++) {
      const fi = Math.min(1, i / 88);
      let v = 0;
      if (i < hl) v += beat[0][i] * fi * Math.min(1, (hl - i) / 400) * 1.6;
      if (i < cl) v += (hash(a, i) * 2 - 1) * 0.55 * Math.exp(-i / SR / 0.45) * fi;
      L[a + i] += v * gain; R[a + i] += v * gain;
    }
  }
  let pk = 0;
  for (let i = 0; i < N; i++) pk = Math.max(pk, Math.abs(L[i]), Math.abs(R[i]));
  for (let i = 0; i < N; i++) { L[i] *= 0.95 / pk; R[i] *= 0.95 / pk; }
  return [L, R];
}
const SONG3 = {
  sections: [['noise', 0.06, 0, 30], ['beat', 0.9, 30, 90], ['pad', 0.12, 30, 90], ['saw', 0.55, 90, 130], ['noise', 0.01, 90, 130],
    ['pluck', 0.8, 130, 160], ['pad', 0.25, 160, 180]],
  hits: [[165, 1]],
};
const song = buildSong(180, SONG3.sections, SONG3.hits);
const DUR = 180;
const t0 = process.hrtime.bigint();
const parts = SLAnalysis.suggestParts(song, SR);
const ms3 = Number(process.hrtime.bigint() - t0) / 1e6;
const secs = (p) => [p.start * DUR, p.end * DUR];
info('3-minute song (quiet noise 0-30 s, beat + quiet pad 30-90, held saw 90-130, pluck 130-160, pad 160-180, loud hit at 165.000 s): ' +
  `suggestParts ${f2(ms3, 0)} ms`);
for (const p of parts) info(`  ${p.kind.padEnd(6)} ${f2(p.start * DUR, 3)} - ${f2(p.end * DUR, 3)} s  "${p.label}"` + (p.bpm ? ` bpm ${p.bpm} found ${p.tempoFound}` : ''));
// margins: the best candidate in the right place against the best candidate anywhere else
const dbg = SLAnalysis.suggestParts(song, SR, { debug: true }).debug;
function margin(list, isRight, isElsewhere) {
  let a = -Infinity, b = -Infinity;
  for (const c of list) { if (isRight(c)) a = Math.max(a, c.score); else if (isElsewhere(c)) b = Math.max(b, c.score); }
  return `best score there ${f2(a)} vs best elsewhere ${f2(b)} (margin ${f2(a - b)})`;
}
const frac = (c, a, b) => (Math.min(c.e, b) - Math.max(c.s, a)) / (c.e - c.s);
const mSteady = margin(dbg.steady, (c) => frac(c, 90, 130) >= 0.9, (c) => frac(c, 90, 130) < 0.5);
const mHit = margin(dbg.hit, (c) => Math.abs(c.t - 165) < 0.1, (c) => Math.abs(c.t - 165) > 0.5);
const mQuiet = margin(dbg.quiet, (c) => frac(c, 0, 30) >= 0.9, (c) => frac(c, 0, 30) < 0.5);
const byKind = {};
for (const p of parts) byKind[p.kind] = p;
function inside(p, a, b) { const [s, e] = secs(p); return (Math.min(e, b) - Math.max(s, a)) / (e - s); }
{
  const p = byKind.steady;
  check('song: held tone is "steady"', !!p && inside(p, 90, 130) >= 0.9, p ? `${f2(secs(p)[0], 2)}-${f2(secs(p)[1], 2)} s, ${f2(100 * inside(p, 90, 130), 0)}% inside the held saw 90-130 s (need 90%); ${mSteady}` : 'missing');
}
{
  const p = byKind.hit;
  const off = p ? 165 - secs(p)[0] : NaN;
  check('song: the loud hit is "hit"', !!p && off >= 0 && off <= 0.1, p ? `part starts ${f2(off * 1000, 1)} ms before the hit at 165.000 s (0..100 ms), lasts ${f2(secs(p)[1] - secs(p)[0], 2)} s; ${mHit}` : 'missing');
}
{
  const p = byKind.quiet;
  check('song: quiet intro is "quiet"', !!p && inside(p, 0, 30) >= 0.9, p ? `${f2(secs(p)[0], 2)}-${f2(secs(p)[1], 2)} s, ${f2(100 * inside(p, 0, 30), 0)}% inside the intro 0-30 s; ${mQuiet}` : 'missing');
}
{
  const p = byKind.loop;
  const inRhythm = p ? Math.max(inside(p, 30, 90), inside(p, 130, 160)) : 0, len = p ? secs(p)[1] - secs(p)[0] : 0;
  check('song: one-bar loop at 120 bpm', !!p && p.tempoFound && Math.abs(p.bpm - 120) <= 1 && inRhythm >= 0.99 && Math.abs(len - 240 / p.bpm) < 0.002,
    p ? `bpm ${p.bpm} (120 +-1, found ${p.tempoFound}), ${f2(len, 3)} s long (one bar = ${f2(240 / p.bpm, 3)}), ${f2(100 * inRhythm, 0)}% inside the beat or pluck section` : 'missing');
}
function constraints(ps, dur) {
  const errs = [];
  if (ps.length > 4) errs.push(ps.length + ' parts');
  const kinds = new Set();
  for (const p of ps) {
    if (!['steady', 'hit', 'quiet', 'loop'].includes(p.kind)) errs.push('kind ' + p.kind);
    if (kinds.has(p.kind)) errs.push('duplicate ' + p.kind);
    kinds.add(p.kind);
    if (typeof p.label !== 'string' || !p.label) errs.push('label');
    if (!(p.start >= 0 && p.end <= 1 && p.end > p.start)) errs.push(p.kind + ' outside 0..1');
    const len = (p.end - p.start) * dur;
    if (len < 1 - 1e-6 || len > 4 + 1e-6) errs.push(`${p.kind} ${f2(len, 3)} s`);
  }
  let worst = 0;
  for (let i = 0; i < ps.length; i++) for (let j = i + 1; j < ps.length; j++) {
    const a = ps[i], b = ps[j], ov = Math.min(a.end, b.end) - Math.max(a.start, b.start);
    const r = Math.max(0, ov) / Math.min(a.end - a.start, b.end - b.start);
    worst = Math.max(worst, r);
    if (r > 0.5 + 1e-9) errs.push(`${a.kind}/${b.kind} overlap ${f2(100 * r, 0)}%`);
  }
  return { errs, worst };
}
{
  const c = constraints(parts, DUR);
  check('song: parts valid', c.errs.length === 0 && parts.length === 4, `${parts.length} parts, each 1-4 s inside the file, worst pair overlap ${f2(100 * c.worst, 0)}% (<= 50%)` + (c.errs.length ? '; ' + c.errs.join(', ') : ''));
  const again = SLAnalysis.suggestParts(song, SR);
  check('song: deterministic', JSON.stringify(again) === JSON.stringify(parts), 'two runs ' + (JSON.stringify(again) === JSON.stringify(parts) ? 'identical' : 'DIFFER'));
}

/* ---------- the same song with other held sounds, hit levels and hit places ---------- */
{
  for (const id of ['vowel', 'sawopen', 'square']) snd[id] = SLSounds.make(id);
  const peak10 = (L, a, b) => { const W = 441; let m = 0; for (let i = Math.round(a * SR); i + W < Math.round(b * SR); i += 110) { let e = 0; for (let j = i; j < i + W; j += 2) e += L[j] * L[j]; m = Math.max(m, e); } return 10 * Math.log10(m / (W / 2) + 1e-20); };
  let steadyOk = 0, total = 0;
  const hitOk = { 1: 0, 0.7: 0, 0.5: 0 }, hitN = { 1: 0, 0.7: 0, 0.5: 0 }, louder = { 1: [], 0.7: [], 0.5: [] }, misses = [];
  for (const held of ['saw', 'pad', 'vowel', 'square', 'sawopen']) for (const hg of [1, 0.7, 0.5]) for (const hitT of [165, 45.3]) {
    const sections = SONG3.sections.map((x) => (x[2] === 90 && x[0] === 'saw' ? [held, x[1], x[2], x[3]] : x));
    const sg = buildSong(180, sections, [[hitT, hg]]), ps = SLAnalysis.suggestParts(sg, SR), k = {};
    for (const p of ps) k[p.kind] = p;
    total++;
    if (k.steady && inside(k.steady, 90, 130) >= 0.9) steadyOk++; else misses.push(`steady ${held}`);
    hitN[hg]++;
    // how much louder (10 ms RMS) the hit is than the loudest moment of the beat section away from it
    const hp = peak10(sg[0], hitT, hitT + 0.2), bp = Math.max(peak10(sg[0], 30, Math.min(hitT, 90) - 0.05), hitT < 90 ? peak10(sg[0], hitT + 1, 90) : -200);
    louder[hg].push(hp - bp);
    if (k.hit && hitT - k.hit.start * 180 >= 0 && hitT - k.hit.start * 180 <= 0.1) hitOk[hg]++; else misses.push(`hit ${held} x${hg} @${hitT} -> ${k.hit ? f2(k.hit.start * 180, 2) : 'none'}`);
  }
  check('variants: held tone found as steady', steadyOk === total, `${steadyOk}/${total} (held saw, pad, vowel, square, sawopen x hit level 1/0.7/0.5 x hit at 165 or 45.3 s)`);
  check('variants: a clearly loudest hit found as hit', hitOk[1] === hitN[1], `hit level 1: ${hitOk[1]}/${hitN[1]}; it is ${dist(louder[1], 1)} dB louder (10 ms RMS) than the loudest beat moment`);
  info(`variants, hit level 0.7: found ${hitOk[0.7]}/${hitN[0.7]} (hit ${dist(louder[0.7], 1)} dB over the beat); level 0.5: ${hitOk[0.5]}/${hitN[0.5]} (hit ${dist(louder[0.5], 1)} dB over the beat, so a beat kick is as big)`);
  if (misses.length) info('variant misses: ' + misses.join('; '));
}

/* ---------- speed on 5 minutes ---------- */
{
  const S5 = {
    sections: SONG3.sections.concat([['beat', 0.9, 180, 240], ['pad', 0.12, 180, 240], ['saw', 0.5, 240, 270], ['pluck', 0.7, 270, 300]]),
    hits: SONG3.hits.concat([[250, 0.6]]),
  };
  const song5 = buildSong(300, S5.sections, S5.hits), times = [];
  let p5 = null;
  for (let r = 0; r < 3; r++) { const t = process.hrtime.bigint(); p5 = SLAnalysis.suggestParts(song5, SR); times.push(Number(process.hrtime.bigint() - t) / 1e6); }
  check('suggestParts 5 minutes < 1.5 s', Math.max(...times) < 1500, `3 runs on 5:00 stereo 44.1 kHz: ${times.map((t) => f2(t, 0)).join(' / ')} ms (worst must be < 1500)`);
  const c = constraints(p5, 300);
  check('5-minute parts valid', c.errs.length === 0 && p5.length >= 3, `${p5.length} parts: ${p5.map((p) => p.kind + ' ' + f2(p.start * 300, 1) + '-' + f2(p.end * 300, 1) + 's').join(', ')}` + (c.errs.length ? '; ' + c.errs.join(', ') : ''));
  let t = process.hrtime.bigint();
  const pp = SLAnalysis.detectPitch(song5, SR);
  const tp = Number(process.hrtime.bigint() - t) / 1e6;
  t = process.hrtime.bigint();
  const kk = SLAnalysis.detectKey(song5, SR);
  const tk = Number(process.hrtime.bigint() - t) / 1e6;
  info(`5-minute song: detectPitch ${f2(tp, 0)} ms -> ${pp ? nameOf(pp.midi) + ' ' + f2(pp.cents, 1) + 'c' : 'null'}, detectKey ${f2(tk, 0)} ms -> ${kk ? NAMES[kk.key] + ' ' + kk.scale + ' r ' + kk.r : 'null'}`);
}

/* ---------- tempo at other speeds (the beat loop resampled) ---------- */
{
  const res = [];
  let ok = true;
  for (const bpm of [92, 100, 108, 128, 140]) {
    const src = snd.beat.channels[0], ratio = 120 / bpm, n = Math.round(src.length * ratio), reps = 8, x = new Float32Array(n * reps);
    for (let r = 0; r < reps; r++) for (let i = 0; i < n; i++) {
      const p = i / ratio, i0 = Math.floor(p), fr = p - i0;
      x[r * n + i] = src[i0 % src.length] * (1 - fr) + src[(i0 + 1) % src.length] * fr;
    }
    const ps = SLAnalysis.suggestParts([x], SR), lp = ps.find((p) => p.kind === 'loop');
    res.push(`${bpm}->${lp ? lp.bpm : 'none'}`);
    if (!lp || !lp.tempoFound || Math.abs(lp.bpm - bpm) > 1) ok = false;
  }
  check('tempo of resampled beat loops', ok, res.join(', ') + ' (each +-1 bpm)');
}

/* ---------- suggestParts on the starter sounds and edge cases ---------- */
{
  const rows = [];
  let ok = true;
  for (const s of SLSounds.list) {
    const m = SLSounds.make(s.id), dur = m.channels[0].length / m.sampleRate, ps = SLAnalysis.suggestParts(m.channels, m.sampleRate);
    const c = constraints(ps, dur);
    if (c.errs.length) ok = false;
    rows.push(s.id + ' [' + ps.map((p) => p.kind).join(' ') + ']');
  }
  check('starter sounds: parts valid', ok, rows.join(', '));
  const tiny = SLAnalysis.suggestParts([new Float32Array(SR / 2)], SR), sil = SLAnalysis.suggestParts([new Float32Array(SR * 10)], SR);
  const mono = SLAnalysis.suggestParts([song[0]], SR), c = constraints(mono, DUR);
  check('edge cases', tiny.length === 0 && sil.length === 0 && c.errs.length === 0 && mono.length === 4,
    `0.5 s file -> ${tiny.length} parts, 10 s of silence -> ${sil.length}, mono song -> ${mono.length} valid parts`);
}

console.log(`\ntest-analysis: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
