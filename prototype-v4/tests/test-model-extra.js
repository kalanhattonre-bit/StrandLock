// tests/test-model-extra.js - skeptic checks for src/sl-model.js (node tests/test-model-extra.js).
// Written by the model reviewer, not the model builder. Each check is meant to FAIL if a blueprint feature
// is missing, wrong or fragile: edge cases, boundary values, determinism, NaN guards, untouched inputs,
// the exact numbers in BLUEPRINT.md, and agreement with the engine's own copies of the shared definitions.
// One line per check (PASS/FAIL name: measured vs limit); INFO lines are measurements, not checks.
// Exits 1 on any FAIL.
'use strict';
const path = require('path');
const fs = require('fs');
const vm = require('vm');
const SRC = path.join(__dirname, '..', 'src', 'sl-model.js');
const M = require(SRC);

let fails = 0, passes = 0;
function check(name, ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + name + ': ' + msg); if (ok) passes++; else fails++; }
function info(msg) { console.log('INFO ' + msg); }
function section(s) { console.log('--- ' + s); }
const fx = (x, d = 4) => (Number.isFinite(x) ? (x !== 0 && Math.abs(x) < 10 ** -d ? x.toExponential(2) : x.toFixed(d)) : String(x));
function dist(arr, d = 4) {
  if (!arr.length) return '(none)';
  const a = arr.slice().sort((x, y) => x - y), q = p => a[Math.min(a.length - 1, Math.round(p * (a.length - 1)))];
  return 'min ' + fx(a[0], d) + ' / median ' + fx(q(0.5), d) + ' / max ' + fx(a[a.length - 1], d) + ' (n=' + a.length + ')';
}
function rng(seed) { let i = 0; return () => M.rand01(M.hash32(seed, i++)); }
function deepFreeze(o) { if (o && typeof o === 'object' && !Object.isFrozen(o)) { Object.freeze(o); for (const k of Object.keys(o)) deepFreeze(o[k]); } return o; }
const J = x => JSON.stringify(x, (k, v) => (ArrayBuffer.isView(v) ? Array.from(v) : v));
const N = (p, s, l, id) => M.mkNote(p, s, l, id);
const clipOf = (notes, bars = 4) => ({ bars, notes });
const byId = (clip, id) => clip.notes.find(n => n.id === id);
const maxAbsDiff = (a, b) => { let m = 0; for (let i = 0; i < Math.min(a.length, b.length); i++) m = Math.max(m, Math.abs(a[i] - b[i])); return a.length === b.length ? m : Infinity; };
const allFinite = a => Array.from(a).every(Number.isFinite);
const SAMPLE_FIELDS = ['engine', 'lock', 'motion', 'speed', 'heads', 'spread', 'grainVoice', 'grainCloud', 'grainLock', 'refresh', 'spray', 'tape', 'rev', 'drift', 'headTune', 'jump'];

// =========================================================================== A. model vs engine copies
section('A. shared definitions agree with the engine (BLUEPRINT 1: "model and engine must agree exactly")');
{
  let SE = null;
  try { const E = require(path.join(__dirname, '..', 'src', 'sl-engine.js')); SE = E.createStrandEngineModule(); } catch (e) { info('engine not loadable: ' + e.message); }
  if (SE) {
    const odd = ['1/7', 0.3, 1e-9, -1, 0, NaN, null, undefined, '1/16t', ' 1/16', '1bar ', 4];
    const rbad = M.RATES.concat(odd).filter(x => !Object.is(SE.rateBeats(x), M.rateBeats(x)));
    check('rateBeats: model = engine on all 21 ids and 12 odd inputs', !rbad.length, `${M.RATES.length + odd.length} inputs, ${rbad.length} differ${rbad.length ? ': ' + rbad.map(String).join(',') : ''}`);
    if (SE.hash32 && SE.idHash && SE.rand01) {
      const R = rng(808); let hb = 0, ib = 0, rb = 0;
      for (let i = 0; i < 20000; i++) {
        const n = 1 + (i % 6), a = Array.from({ length: n }, () => Math.floor(R() * 4294967296) - 2147483648);
        if (SE.hash32(...a) !== M.hash32(...a)) hb++;
        const h = M.hash32(i); if (SE.rand01(h) !== M.rand01(h)) rb++;
      }
      const strs = ['', 'a', 'sd-1', 'né', '\u{1F600}x', 'live:60', 'x'.repeat(500)].concat(Array.from({ length: 500 }, (_, i) => 'n' + M.hash32(i).toString(36)));
      for (const s of strs) if (SE.idHash(s) !== M.idHash(s)) ib++;
      check('hash32 / rand01 / idHash: model = engine', hb === 0 && ib === 0 && rb === 0, `20000 random 1-6 int keys: ${hb} hash mismatches; ${rb} rand01 mismatches; ${strs.length} ids (unicode included): ${ib} idHash mismatches`);
    } else info('engine does not export hash32/idHash/rand01; not compared');
    if (SE.travelSpeed) {
      let tb = 0; for (let i = 0; i <= 200000; i++) { const v = i / 200000; if (!Object.is(SE.travelSpeed(v), M.travelSpeed(v)) || !Object.is(SE.travelSpeed(Math.fround(v)), M.travelSpeed(Math.fround(v)))) tb++; }
      check('travelSpeed: model = engine on 200001 doubles and their Float32 roundings', tb === 0, `${tb} differ`);
    }
    if (SE.gShape) { let gb = 0; for (let i = 0; i <= 2000; i++) for (const c of [-1, -0.37, 0, 0.5, 1]) if (SE.gShape(i / 2000, c) !== M.gShape(i / 2000, c)) gb++; check('gShape (glide curve): model = engine', gb === 0, `${gb} of 10005 points differ`); }
    if (SE.LANE_IDS && SE.LANE_DEF) {
      const ids = Array.from(SE.LANE_IDS).join(','), defs = Array.from(SE.LANE_DEF).map(Number).join(',');
      check('lane order and defaults: model = engine', ids === M.LANE_IDS.join(',') && defs === M.LANES.map(L => L.def).join(','), `engine ${ids} / ${defs}`);
    }
    if (SE.defaultInst) check('defaultInst: model = engine', J(SE.defaultInst()) === J(M.defaultInst()), J(SE.defaultInst()) === J(M.defaultInst()) ? 'identical' : 'differ');
  }
}

// =========================================================================== B. travel numbers
section('B. Travel 1.3: the exact formula, both clamps, and the dead band as the engine reads it');
{
  const f = v => (v >= 0.48 && v <= 0.52 ? 0 : v > 0.52 ? Math.min(8, 2 ** (((v - 0.75) / 0.23) * 3)) : -Math.min(8, 2 ** (((0.25 - v) / 0.23) * 3)));
  const errs = []; let worst = 0;
  for (let i = 0; i <= 10000; i++) { const v = i / 10000, e = Math.abs(M.travelSpeed(v) - f(v)) / Math.max(1, Math.abs(f(v))); errs.push(e); worst = Math.max(worst, e); }
  check('travelSpeed = blueprint formula at 10001 points', worst < 1e-12, 'relative error ' + dist(errs, 12));
  // (integrator) the band is inclusive at Float32 precision (BLUEPRINT 1.3: 2e-8, so Math.fround(0.48) is stopped,
  // which section B's next check and engine-extra F demand). The limits are therefore approached from 1e-7 outside,
  // the next Float32 step and beyond, instead of 1e-12; the tolerance follows (1/8 x (1 + 9e-7)).
  const lim = [M.travelSpeed(0.52 + 1e-7), M.travelSpeed(0.48 - 1e-7), M.travelSpeed(0.98), M.travelSpeed(0.99), M.travelSpeed(0.02), M.travelSpeed(0.005), M.travelSpeed(0.6), M.travelSpeed(0.9)];
  check('limits: 0.52+ -> 1/8x, 0.48- -> -1/8x, 8x clamp above 0.98 and below 0.02', Math.abs(lim[0] - 0.125) < 2e-7 && Math.abs(lim[1] + 0.125) < 2e-7 && Math.abs(lim[2] - 8) < 1e-9 && lim[3] === 8 && Math.abs(lim[4] + 8) < 1e-9 && lim[5] === -8 && Math.abs(lim[6] - 2 ** ((-0.15 / 0.23) * 3)) < 1e-12 && Math.abs(lim[7] - 2 ** ((0.15 / 0.23) * 3)) < 1e-12,
    lim.map(x => fx(x, 7)).join(' ') + ' (band edges at Float32 precision: ' + M.travelSpeed(0.48 - 2e-8) + ' at 0.48-2e-8, ' + fx(M.travelSpeed(0.48 - 3e-8), 4) + ' at 0.48-3e-8)');
  // what actually plays: the snapshot's Float32 table. A node drawn on either band edge must play stopped (1.3 says 0.48 <= v <= 0.52).
  const edge = v => { const n = N(60, 0, 1, 'tv'); n.c.travel = [M.mkNode(0, v, 'linear'), M.mkNode(1, v)]; const t = M.compileSnapshot(clipOf([n], 1), M.defaultInst()).notes[0].lanes.travel; return M.travelSpeed(t[0]); };
  const s48 = edge(0.48), s52 = edge(0.52), s50 = edge(0.5);
  check('a Travel node drawn exactly on a dead-band edge plays stopped through the snapshot table', s48 === 0 && s52 === 0 && s50 === 0,
    `0.48 -> ${fx(s48, 6)}x, 0.50 -> ${s50}x, 0.52 -> ${s52}x (Float32 0.48 = ${Math.fround(0.48)} falls outside the inclusive band; readout says '${M.formatLane('travel', 0.48)}')`);
  const inv = []; for (let i = 0; i <= 400; i++) { const x = 2 ** (-3 + 6 * i / 400); for (const sg of [1, -1]) inv.push(Math.abs(M.travelSpeed(M.travelSpeedToV(sg * x)) - sg * x) / x); }
  const e8 = [0.125, -0.125, 8, -8].map(x => `${x}x -> v ${fx(M.travelSpeedToV(x), 6)} -> plays ${fx(M.travelSpeed(Math.fround(M.travelSpeedToV(x))), 4)}x`);
  check('travelSpeedToV round trip incl. both ends (1/8x and 8x, as the inspector T6 would type them)', Math.max(...inv) < 1e-6 && M.travelSpeedToV(0) === 0.5, `relative error ${dist(inv, 9)}; ${e8.join('; ')}`);
}

// =========================================================================== C. readouts vs the Float32 table
section('C. readouts agree with the value the engine reads (Float32 tables)');
{
  const inst = M.mergeInst(M.defaultInst(), { refresh: { mode: 'free', ms: 120 } });
  const n = N(60, 0, 1, 'rf'); n.c.refresh = [M.mkNode(0, 0.06, 'linear'), M.mkNode(1, 0.06)];
  const tv = M.compileSnapshot(clipOf([n], 1), inst).notes[0].lanes.refresh[0];
  const readout = M.mapLane('refresh', 0.06, { inst }), played = M.mapLane('refresh', tv, { inst });
  check("Refresh node on the Smooth threshold (v = 0.06): readout = what the table plays", readout === played,
    `readout ${fx(readout, 1)} ms ('${M.formatLane('refresh', 0.06, { inst })}'), table holds ${tv} (< 0.06 -> Smooth) -> ${played}; travel got Math.fround in mapLane, refresh did not`);
  const fmts = [M.formatLane('travel', 0.98), M.formatLane('travel', 0.02), M.formatLane('travel', 0.52), M.formatLane('travel', 0.5 + 1e-7), M.formatLane('tone', 1), M.formatLane('hollow', 0), M.formatLane('pitch', 0.5 - 3 / 24), M.formatLane('travel', 0.25, { travelKind: 'path' })];
  const want = ['8x forwards', '8x backwards', 'stopped', 'stopped', '18.9 kHz', '0.0 dB', '-3 st', '25% of the Region'];
  check('readouts at the ends of each range', J(fmts) === J(want), fmts.map((s, i) => `'${s}'` + (s === want[i] ? '' : ` (want '${want[i]}')`)).join(', '));
}

// =========================================================================== D. NaN guards
section('D. NaN guards on the public drawing and readout functions');
{
  const nanV = [{ t: 0, v: NaN, shape: 'smooth', bend: 0 }, { t: 0.5, v: 0.7, shape: 'smooth', bend: 0 }, { t: 1, v: 0.5, shape: 'smooth', bend: 0 }];
  const nanT = [{ t: 0, v: 0.2, shape: 'smooth', bend: 0 }, { t: NaN, v: 0.9, shape: 'smooth', bend: 0 }, { t: 1, v: 0.5, shape: 'smooth', bend: 0 }];
  const a = M.sampleNodes(nanV, 9), b = M.sampleNodes(nanT, 9), e = M.evalNodes(nanV, 0.25);
  const ln = N(60, 0, 1, 'nl'); ln.l = NaN; const lt = M.laneTable(ln, 'amp', 256);
  const nn = N(60, 0, 1, 'nv'); nn.c.tone = nanV; const nt = M.laneTable(nn, 'tone', 64);
  check('sampleNodes / evalNodes / laneTable stay finite (and >= 2 long) on a NaN node value, a NaN node time or a NaN length', allFinite(a) && allFinite(b) && Number.isFinite(e) && lt.length >= 2 && allFinite(nt),
    `NaN v -> ${Array.from(a).filter(x => !Number.isFinite(x)).length}/9 NaN; NaN t -> ${Array.from(b).filter(x => !Number.isFinite(x)).length}/9 NaN (one bad node poisons the whole lane); evalNodes ${e}; laneTable(l=NaN) length ${lt.length}; laneTable with NaN node ${Array.from(nt).filter(x => !Number.isFinite(x)).length}/${nt.length} NaN`);
  const txt = M.LANE_IDS.map(id => { try { return M.formatLane(id, NaN, { inst: M.defaultInst() }); } catch (err) { return 'threw'; } });
  check('formatLane never prints NaN', !txt.some(s => /NaN|threw/.test(s)), txt.join(' | '));
  // the snapshot path must be fully guarded (this is what reaches the engine)
  const bad = N(NaN, NaN, NaN, 'bad'); bad.vel = NaN; bad.glide = NaN; bad.gc = NaN; bad.c.pitch = nanT; bad.c.amp = nanV; bad.gen.tone = { kind: 'pattern', rate: '1/16', d0: NaN, d1: Infinity, warp: NaN, seed: NaN };
  bad.sample.start = { mode: 'random', pos: NaN, range: Infinity, seed: NaN };
  const snap = M.compileSnapshot(clipOf([bad, N(62, 1, 1, 'ok')]), { speed: NaN, glideTime: NaN, heads: NaN, macros: [NaN], region: { start: NaN, end: NaN } });
  const finiteSnap = o => { for (const k of Object.keys(o)) { const v = o[k]; if (typeof v === 'number' && !Number.isFinite(v)) return k; if (ArrayBuffer.isView(v)) { if (!allFinite(v) || v.length < 2) return k; } else if (v && typeof v === 'object') { const r = finiteSnap(v); if (r) return k + '.' + r; } } return null; };
  const where = finiteSnap(snap);
  check('compileSnapshot output is finite even from a NaN-laden note and inst', !where, where ? 'non-finite at ' + where : `${snap.notes.length} notes, every number finite, every table >= 2 long`);
}

// =========================================================================== E. generators: exact math
section('E. generators: exact values, warp, clamp, depth, Smooth, perNote');
{
  const R = rng(5); let e1 = 0, e2 = 0;
  for (let i = 0; i < 300; i++) {
    const rate = M.RATES[Math.floor(R() * 21)], P = M.rateBeats(rate), s = Math.round(R() * 200) / 16, l = 0.25 + R() * 4, d = R();
    const n = N(60, s, l, 'q' + i); n.c.formant = M.flat(0.5); n.gen.formant = M.makeGen('pattern', { shape: 'squiggle', rate, d0: d, d1: d, lockToBeat: true });
    const tab = M.laneTable(n, 'formant', 256);
    for (let j = 0; j < tab.length; j++) { const b = s + j / (tab.length - 1) * l; e1 = Math.max(e1, Math.abs(tab[j] - (0.5 + 0.5 * d * Math.sin(2 * Math.PI * b / P)))); }
    const u = N(60, s, l, 'u' + i); u.c.formant = M.flat(0.5); u.gen.formant = M.makeGen('pattern', { shape: 'squiggle', rate, d0: d, d1: d, lockToBeat: false });
    const tu = M.laneTable(u, 'formant', 256);
    for (let j = 0; j < tu.length; j++) { const t = j / (tu.length - 1); e2 = Math.max(e2, Math.abs(tu[j] - (0.5 + 0.5 * d * Math.sin(2 * Math.PI * t * l / P)))); }
  }
  check('locked squiggle = 0.5*d*sin(2 pi songBeat/rate); unlocked = phase from the note start', e1 < 1e-6 && e2 < 1e-6, `300 random notes x 21 rates: locked max error ${fx(e1, 9)}, unlocked ${fx(e2, 9)}`);
  const a = N(60, 1.3, 3, 'w'); a.c.tone = M.flat(0.5); a.gen.tone = M.makeGen('pattern', { rate: '1/16T', d0: 0.5, lockToBeat: true, warp: 2 });
  const b = M.cloneNote(a); b.gen.tone = Object.assign({}, a.gen.tone, { warp: 0 });
  check('warp is ignored when the pattern is locked to the beat', maxAbsDiff(M.laneTable(a, 'tone', 256), M.laneTable(b, 'tone', 256)) === 0, 'max diff ' + maxAbsDiff(M.laneTable(a, 'tone', 256), M.laneTable(b, 'tone', 256)));
  const c = N(60, 0, 2, 'cl'); c.c.amp = M.flat(0.95); c.gen.amp = M.makeGen('pattern', { shape: 'square', rate: '1/4', d0: 1, d1: 1 });
  const ct = Array.from(M.laneTable(c, 'amp', 128));
  check('table = clamp(drawn + gen, 0, 1): 0.95 + square depth 1 gives 1.0 and 0.45', Math.max(...ct) === 1 && Math.abs(Math.min(...ct) - 0.45) < 1e-6, `max ${Math.max(...ct)}, min ${fx(Math.min(...ct), 6)}`);
  let mx = 0; const peaks = [];
  for (let sd = 1; sd <= 80; sd++) { const p = N(60, 0, 4, 'pr' + sd); p.gen.pitch = M.makeGen('random', { rate: '1/16', amount: 1, seed: sd }); let pk = 0; for (const v of M.laneTable(p, 'pitch', 64)) pk = Math.max(pk, Math.abs((v - 0.5) * 24)); peaks.push(pk); mx = Math.max(mx, pk); }
  check('pitch lane: random Amount 1 stays within +-2 st and reaches it', mx <= 2 + 1e-5 && mx > 1.9, 'peak per note ' + dist(peaks, 4) + ' st');
  const sm = N(60, 0, 4, 'sm'); sm.c.formant = M.flat(0.5); sm.gen.formant = M.makeGen('random', { rate: '1/4', amount: 1, smooth: 0.5, seed: 3 });
  const st = M.laneTable(sm, 'formant', 1000); let holdDev = 0, moving = 0;
  for (let k = 1; k < 4; k++) { const base = k * 1000; for (let j = base + 520; j < base + 990; j++) holdDev = Math.max(holdDev, Math.abs(st[j] - st[base + 990])); if (Math.abs(st[base + 250] - st[base + 990]) > 1e-3) moving++; }
  check('random Smooth 0.5: glides during the first half of each step, holds the second half', holdDev < 1e-6 && moving === 3, `hold deviation ${holdDev}; ${moving}/3 steps still moving at a quarter step`);
  const r1 = N(60, 0, 4, 'rx1'), r2 = N(67, 0, 4, 'rx2'), r3 = N(64, 0, 4, 'rx3');
  for (const q of [r1, r2]) { q.c.formant = M.flat(0.5); q.gen.formant = M.makeGen('pattern', { shape: 'randsteps', rate: '1/8', d0: 0.8, perNote: 'different' }); }
  r3.c.formant = M.flat(0.5); r3.gen.formant = M.makeGen('pattern', { shape: 'randsteps', rate: '1/8', d0: 0.8, perNote: 'same' });
  const r4 = M.cloneNote(r3); r4.id = 'rx4'; r4.p = 72;
  const dd = maxAbsDiff(M.laneTable(r1, 'formant', 64), M.laneTable(r2, 'formant', 64)), ds = maxAbsDiff(M.laneTable(r3, 'formant', 64), M.laneTable(r4, 'formant', 64));
  check("pattern 'randsteps' honours Same / Different", dd > 0.2 && ds === 0, `different: max diff ${fx(dd, 3)}; same: ${ds}`);
  check("Gen default rate is the blueprint's 1/16T (2.1 lists rate:'1/16T')", M.makeGen('pattern').rate === '1/16T' && M.makeGen('random').rate === '1/16T', `makeGen default rate '${M.makeGen('pattern').rate}'`);
}

// =========================================================================== F. bake and re-roll
section('F. Turn into nodes (eps 0.004 exactly) and Re-roll scope');
{
  const rows = [];
  let i = 0;
  for (const shape of M.GEN_SHAPES) for (const rate of ['1/4', '1/8T', '1/16']) for (const l of [0.5, 1, 2]) {
    const n = N(60, 0.125 * (i % 8), l, 'bk' + i); n.c.tone = M.shapeNodes('dip', 'tone');
    n.gen.tone = M.makeGen('pattern', { shape, rate, d0: 0.3, d1: 0.6, seed: i, lockToBeat: i % 2 === 0, warp: i % 2 ? 0.5 : 0 });
    const before = M.laneTable(n, 'tone', 256), q = M.bakeGen(clipOf([n]), null, 'tone').notes[0], after = M.laneTable(q, 'tone', 256);
    const needed = M.rdp(Array.from(before, (v, j) => ({ t: j / (before.length - 1), v })), M.BAKE_EPS).length;
    rows.push({ err: maxAbsDiff(before, after), needed, nodes: q.c.tone.length, lin: q.c.tone.every(x => x.shape === 'linear'), other: J(q.c.amp) === J(n.c.amp) && J(q.gen) === J(Object.assign({}, n.gen, { tone: null })) });
    i++;
  }
  const fit = rows.filter(r => r.needed <= 128);
  check('bakeGen error <= BAKE_EPS (0.004) whenever 128 nodes suffice', fit.every(r => r.err <= M.BAKE_EPS + 1e-6), `${fit.length}/${rows.length} cases fit: error ${dist(fit.map(r => r.err), 5)} vs limit 0.004 (the builder's own check allows 0.01)`);
  check('bakeGen writes linear nodes, drops only that generator, leaves other lanes', rows.every(r => r.lin && r.other && r.nodes <= 128), `${rows.length} cases`);
  let c = clipOf([N(60, 0, 1, 'a'), N(62, 1, 1, 'b'), N(64, 2, 1, 'c')]);
  c = M.setGen(c, null, 'tone', M.makeGen('random', { seed: 10 })); c = M.setGen(c, null, 'formant', M.makeGen('pattern', { seed: 20 }));
  c = M.setNoteSample(c, ['a', 'c'], { start: { mode: 'random' } });
  const one = M.reroll(c, ['a'], 'tone'), all = M.reroll(c, null, null);
  const seeds = cl => cl.notes.map(n => `${n.id}:${n.gen.tone.seed}/${n.gen.formant.seed}/${n.sample.start.seed}`).join(' ');
  check('reroll(ids, lane) bumps only that lane of those notes; reroll(null, null) bumps every gen and every random Start', seeds(one) === 'a:11/20/1 b:10/20/1 c:10/20/1' && seeds(all) === 'a:11/21/2 b:11/21/1 c:11/21/2', `one lane: ${seeds(one)}; all: ${seeds(all)}`);
}

// =========================================================================== G. shapes
section('G. shapes (Y2): key lists, travel keys, generators untouched, across');
{
  const common = 'up,down,swell,dip,squiggle,random,decay,reset';
  check('shape keys: common 8, Speed adds 4, Path adds 3 (blueprint list, in order)', M.shapeKeysFor('amp').join(',') === common && M.shapeKeysFor('travel', 'speed').join(',') === common + ',tapestop,tapestart,reversehalf,rewind' && M.shapeKeysFor('travel', 'path').join(',') === common + ',playthrough,playthroughback,fit',
    `speed: ${M.shapeKeysFor('travel', 'speed').join(',')}; path: ${M.shapeKeysFor('travel', 'path').join(',')}`);
  const sp = clipOf([N(60, 0, 4, 'sp')]);
  const speeds = key => Array.from(M.laneTable(M.applyShape(sp, null, 'travel', key, 'each').notes[0], 'travel', 64), M.travelSpeed);
  const s2 = speeds('tapestart'); let falls = 0; for (let j = 1; j < s2.length; j++) if (s2[j] < s2[j - 1] - 1e-9) falls++;
  check('Tape start: from stopped up to 1x, never slowing', s2[0] === 0 && Math.abs(s2[s2.length - 1] - 1) < 1e-6 && falls === 0, `speed ${s2[0]} -> ${fx(s2[s2.length - 1], 4)}, ${falls} falls`);
  const s3 = speeds('rewind'), K = s3.length - 1, at = f => s3[Math.round(f * K)], half = Math.round(K / 2), back = s3.slice(half + 1);
  check('Rewind: forwards, then backwards and slowing to a stop', s3.slice(0, half - 1).every(x => Math.abs(x - 1) < 1e-6) && back.every((x, j) => x <= 0 && (j === 0 || Math.abs(x) <= Math.abs(back[j - 1]) + 1e-9)) && back.some(x => x < -1) && s3[K] === 0,
    `${s3.length} samples: speed at 1/4 ${fx(at(0.25), 2)}, just after 1/2 ${fx(s3[half + 1], 2)}, at 3/4 ${fx(at(0.75), 2)}, end ${s3[K]}`);
  const s1 = speeds('tapestop'), lin = s1.map((x, j) => { const t = j / (s1.length - 1); return Math.abs(x - (t < 0.875 ? 1 - t / 0.875 : 0)); });
  info(`Tape stop speed vs a straight fall to 0 at 7/8: deviation ${dist(lin, 3)} (builder: 'about linearly')`);
  const pn = N(60, 0, 2, 'pa'); pn.sample.travel = 'path';
  const pb = M.laneTable(M.applyShape(clipOf([pn]), null, 'travel', 'playthroughback', 'each').notes[0], 'travel', 64), pf = M.laneTable(M.applyShape(clipOf([pn]), null, 'travel', 'playthrough', 'each').notes[0], 'travel', 64);
  let ep = 0; for (let j = 0; j < pb.length; j++) ep = Math.max(ep, Math.abs(pb[j] - (1 - j / (pb.length - 1))), Math.abs(pf[j] - j / (pf.length - 1)));
  check('Play through / Play through backwards = straight 0->1 / 1->0 across the note', ep < 1e-6, `max error ${fx(ep, 9)}`);
  const gn = N(60, 0, 2, 'g'); gn.gen.tone = M.makeGen('pattern', { rate: '1/8' }); gn.gen.travel = M.makeGen('random', {});
  const gk = ['up', 'reset', 'random', 'squiggle'].every(k => { const o = M.applyShape(clipOf([gn]), null, 'tone', k, 'each', { seed: 2 }).notes[0]; return J(o.gen) === J(gn.gen); }) && J(M.applyShape(clipOf([gn]), null, 'travel', 'tapestop', 'across').notes[0].gen) === J(gn.gen);
  check('shapes write the drawing only (Y9): generators survive every key, Reset included', gk, String(gk));
  const four = clipOf([0, 1, 2, 3].map(k => N(60 + k, k, 1, 'r' + k)));
  const A1 = M.applyShape(four, null, 'tone', 'random', 'across', { seed: 4 }), A2 = M.applyShape(four, null, 'tone', 'random', 'across', { seed: 4 }), A3 = M.applyShape(four, null, 'tone', 'random', 'across', { seed: 5 });
  const joinA = cl => { let mxj = 0; for (let k = 1; k < 4; k++) { const p = M.laneTable(cl.notes[k - 1], 'tone', 64), q = M.laneTable(cl.notes[k], 'tone', 64); mxj = Math.max(mxj, Math.abs(p[p.length - 1] - q[0])); } return mxj; };
  check("'random' across: seeded, re-seedable, one continuous curve over the selection", J(A1) === J(A2) && J(A1) !== J(A3) && joinA(A1) < 1e-6, `same seed identical ${J(A1) === J(A2)}, seed+1 differs ${J(A1) !== J(A3)}, largest jump between notes ${fx(joinA(A1), 8)}`);
  const edge = [M.mkNode(0, 0.2, 'linear'), M.mkNode(0.5, 0.2, 'linear'), M.mkNode(0.5, 0.9, 'linear'), M.mkNode(1, 0.9)];
  const L = M.sliceNodes(edge, 0, 0.5), Rr = M.sliceNodes(edge, 0.5, 1), Cc = M.sliceNodes(edge, 0.25, 0.75);
  check('sliceNodes at a vertical edge: the left piece ends before it, the right piece starts after it', L.every(q => q.v === 0.2) && Rr.every(q => q.v === 0.9) && M.evalNodes(Cc, 0.49) === 0.2 && M.evalNodes(Cc, 0.51) === 0.9, `left ${J(L.map(q => q.v))}, right ${J(Rr.map(q => q.v))}, middle ${J(Cc.map(q => [q.t, q.v]))}`);
}

// =========================================================================== H. connect
section('H. Connect (Y5, D5): seeded random is fair, boundaries');
{
  const mk = () => clipOf([N(60, 0, 1, 'S'), N(55, 1, 1, 'a'), N(62, 1, 1, 'b'), N(71, 1, 1, 'c')]);
  const cnt = { a: 0, b: 0, c: 0 }; let none = 0;
  for (let s = 1; s <= 900; s++) { const t = byId(M.connect(mk(), null, 'random', s), 'S').glideTo; if (t in cnt) cnt[t]++; else none++; }
  const chi = Object.values(cnt).reduce((x, k) => x + (k - 300) ** 2 / 300, 0);
  check("connect 'random' picks each candidate about equally (900 seeds, 3 candidates)", chi < 13.8 && none === 0, `${J(cnt)}, chi2 ${fx(chi, 2)} vs < 13.8 (df 2, p=0.001), no link ${none}`);
  const up = byId(M.connect(clipOf([N(60, 0, 1, 'A'), N(84, 1, 1, 'B')]), null, 'nearest', 1), 'A').glideTo, dn = byId(M.connect(clipOf([N(60, 0, 1, 'A'), N(36, 1, 1, 'B')]), null, 'nearest', 1), 'A').glideTo;
  const grp = byId(M.connect(clipOf([N(60, 0, 1, 'A'), N(70, 1, 1, 'B'), N(61, 1.05, 1, 'C'), N(62, 1.06, 1, 'D')]), null, 'nearest', 1), 'A').glideTo;
  check('connect reaches exactly 24 st both ways; the 0.05-beat window keeps a note at +0.05', up === 'B' && dn === 'B' && grp === 'C', `+24 -> ${up}, -24 -> ${dn}; window pick ${grp} (want C: starts 0.05 after the earliest, closest pitch; D at +0.06 is out)`);
  const one = M.connect(clipOf([N(60, 0, 1, 'A'), N(62, 1, 1, 'B')]), ['A'], 'nearest', 1);
  check('connect with one selected note makes no link (candidates are selected notes only)', !one.notes.some(n => n.glideTo), J(one.notes.map(n => n.glideTo)));
}

// =========================================================================== I. glides and chains
section('I. glides (E2): voice leading by rank, auto boundaries, chains on random clips');
{
  const inst = M.defaultInst();
  const vl = clipOf([N(60, 0, 1, 'v1'), N(64, 0, 1, 'v2'), N(67, 0, 1, 'v3'), N(36, 0, 1, 'bass'), N(62, 1, 1, 'u1'), N(65, 1, 1, 'u2'), N(69, 1, 1, 'u3'), N(38, 1, 1, 'b2')]);
  const g = M.computeGlides(vl, inst), map = ['v1', 'v2', 'v3', 'bass'].map(id => id + '>' + (g.get(id) ? g.get(id).toId : '-')).join(' ');
  check('voice leading by rank survives a bass note ending at the same time', map === 'v1>u1 v2>u2 v3>u3 bass>b2', `${map} (want v1>u1 v2>u2 v3>u3 bass>b2; the bass is counted as a rank peer, so two voices aim at one note)`);
  const demo = M.loadClipPreset('Strand Demo'), gd = M.computeGlides(demo, inst);
  const tg = ['sd-1', 'sd-2', 'sd-3', 'sd-4'].map(id => gd.get(id) && gd.get(id).toId), named = id => { const n = byId(demo, id); return n ? M.noteName(n.p) : '-'; };
  const chains = M.buildChains(demo, gd), freshA2 = chains.some(ch => ch[0] === 'sd-5');
  check('Strand Demo, first chord change: four voices lead into four different notes', new Set(tg).size === 4 && !freshA2,
    ['sd-1', 'sd-2', 'sd-3', 'sd-4'].map((id, k) => `${named(id)}->${tg[k] ? named(tg[k]) : '-'}`).join(' ') + `; A2 (sd-5) starts a fresh voice: ${freshA2}`);
  const at = s => M.computeGlides(clipOf([N(60, 0, 1, 'a'), N(62, s, 1, 'b')]), inst).size;
  check('auto Legato boundary: starts up to 0.13 beat after the end glide, 0.14 does not; 0.12 before the end glides, 0.14 before does not', at(1.13) === 1 && at(1.14) === 0 && at(0.88) === 1 && at(0.86) === 0, `+0.13 ${at(1.13)}, +0.14 ${at(1.14)}, -0.12 ${at(0.88)}, -0.14 ${at(0.86)}`);
  // chains on random clips with random links, glide 0, restarts and all three auto modes
  const R = rng(4242); const bad = []; let joins = 0; const overlaps = [];
  for (let t = 0; t < 400; t++) {
    const n = 4 + Math.floor(R() * 14), notes = [];
    for (let i = 0; i < n; i++) { const q = N(48 + Math.floor(R() * 30), Math.floor(R() * 24) / 4, 0.25 + Math.floor(R() * 12) / 4, 'c' + i); if (R() < 0.2) q.glide = 0; if (R() < 0.15) q.sample.restart = true; notes.push(q); }
    for (const q of notes) if (R() < 0.25) { const m = notes[Math.floor(R() * n)]; if (m !== q) q.glideTo = m.id; }
    const clip = clipOf(notes, 8), I = M.mergeInst(inst, { glideMode: ['off', 'legato', 'always'][t % 3] });
    const gl = M.computeGlides(clip, I), ch = M.buildChains(clip, gl), flat = [].concat(...ch), bid = new Map(notes.map(q => [q.id, q]));
    if (flat.length !== n || new Set(flat).size !== n) bad.push('cover');
    for (const c of ch) for (let i = 1; i < c.length; i++) {
      const a = bid.get(c[i - 1]), b = bid.get(c[i]); joins++;
      if (!(b.s > a.s)) bad.push('order'); if (b.sample.restart) bad.push('restart'); if (!gl.get(a.id) || gl.get(a.id).toId !== b.id) bad.push('no glide');
      const ov = a.s + a.l - b.s; if (ov > 0.13 + 1e-9) overlaps.push(ov);
    }
    const sn = M.compileSnapshot(clip, I), sid = new Map(sn.notes.map(x => [x.id, x]));
    for (const x of sn.notes) if (x.chain.nextId) { const y = sid.get(x.chain.nextId); if (y.chain.prevId !== x.id || y.chain.id !== x.chain.id || y.chain.index !== x.chain.index + 1) bad.push('snapshot chain'); }
  }
  check('buildChains on 400 random clips: every note once, links follow glides in time order, restart breaks', !bad.length && joins > 300, `${joins} joins, ${bad.length} breaks${bad.length ? ': ' + [...new Set(bad)].join(',') : ''}`);
  info(`explicit links still join when the target starts inside the source: ${overlaps.length}/${joins} joins overlap by more than 0.13 beat (${dist(overlaps, 2)} beats); one voice must then cut the source short`);
}

// =========================================================================== J. tools
section('J. tools: chop limits and generators, legato after humanize, flip and More on Travel, empty selection');
{
  const n64 = clipOf(Array.from({ length: 64 }, (_, i) => N(60, i, 4, 'b' + i)), 32);
  const ok = M.chop(n64, null, '1/16', 0), over = M.chop(clipOf(n64.notes.concat([N(40, 0, 0.25, 'x')]), 32), null, '1/16', 0);
  check('chop: exactly 1024 notes allowed, 1025 refused (maxNotes=1024)', !ok.refused && ok.clip.notes.length === 1024 && over.refused && over.count === 1025, `64x16 -> ${ok.clip.notes.length} refused ${ok.refused}; +1 -> count ${over.count} refused ${over.refused}`);
  const chopErr = gen => {
    const n = N(60, 1.25, 2, 'chg'); n.c.tone = M.flat(0.5); n.gen.tone = gen;
    const whole = M.laneTable(n, 'tone', 512), ch = M.chop(clipOf([n], 2), null, '1/16', 0).clip; let e = 0;
    for (const q of ch.notes) { const t = M.laneTable(q, 'tone', 512); for (let j = 0; j < t.length - 1; j++) { const b = q.s + j / (t.length - 1) * q.l; e = Math.max(e, Math.abs(t[j] - whole[Math.round((b - n.s) / n.l * (whole.length - 1))])); } }
    return e;
  };
  const eLock = chopErr(M.makeGen('pattern', { shape: 'triangle', rate: '1/8T', d0: 0.5 })), eSame = chopErr(M.makeGen('random', { rate: '1/16', perNote: 'same' }));
  const eDiff = chopErr(M.makeGen('random', { rate: '1/16', perNote: 'different' })), eFree = chopErr(M.makeGen('pattern', { shape: 'squiggle', rate: '1/8T', d0: 0.5, lockToBeat: false }));
  check('chop keeps locked patterns and Same random exactly', eLock < 1e-6 && eSame < 1e-6, `locked triangle ${fx(eLock, 8)}, random Same ${fx(eSame, 8)}`);
  check("chop keeps a 'Different' random generator's values (Y7: random stays until Re-roll)", eDiff < 1e-6, `max change ${fx(eDiff, 3)} (pieces get new ids, and Different hashes the id, so chopping re-deals the random line)`);
  info(`chop with an unlocked pattern restarts it on every piece (by definition of 'from note start'): max change ${fx(eFree, 3)}`);
  const chord = clipOf([N(60, 0, 0.5, 'a'), N(64, 0, 0.5, 'b'), N(67, 0, 0.5, 'c'), N(62, 2, 0.5, 'd'), N(65, 2, 0.5, 'e'), N(69, 2, 0.5, 'f')]);
  const hz = M.humanize(chord, null, { time: 0.02 }, 7), lg = M.legato(hz, null);
  const reach = ['a', 'b', 'c'].map(id => { const q = byId(lg, id); return q.s + q.l; });
  check('legato after humanize: every note of a humanized chord still reaches the next chord', reach.every(e => e > 1.95), `chord 1 starts ${['a', 'b', 'c'].map(id => fx(byId(hz, id).s, 3)).join(',')}; ends after legato ${reach.map(e => fx(e, 3)).join(',')} (want ~2; strum and connect group starts within 0.05 beat, legato does not)`);
  const sc = M.applyRecipe(clipOf([N(60, 0, 2, 's')]), M.defaultInst(), 4, null).clip;
  const before = Array.from(M.laneTable(sc.notes[0], 'travel', 64), M.travelSpeed), after = Array.from(M.laneTable(M.flipLane(sc, null, 'travel').notes[0], 'travel', 64), M.travelSpeed);
  const flipped = before.filter((s, j) => Math.abs(s + after[j]) < 1e-6 && s !== 0).length;
  check('Flip on Travel reverses what plays, generator included (Tape scratch)', flipped >= before.length * 0.9, `${flipped}/${before.length} samples change direction; ${before.filter((s, j) => s === after[j]).length} unchanged (Flip mirrors the drawing only)`);
  const sp = [0.25, 0.5, 2, 4, -0.25, -0.5, -1, -2].map((x, k) => { const q = N(60, k, 1, 'm' + k); q.c.travel = M.flat(M.travelSpeedToV(x)); return q; });
  const sgn = (cl, k) => Math.sign(M.travelSpeed(Math.fround(cl.notes[k].c.travel[0].v)));
  const more = M.scaleLane(clipOf(sp), null, 'travel', 2), less = M.scaleLane(clipOf(sp), null, 'travel', 0.5);
  const flips = sp.map((q, k) => [sgn(clipOf(sp), k), sgn(more, k), sgn(less, k)]).filter(([a, b, c]) => a !== b || a !== c).length;
  check('More / Less on Travel (Speed) never change direction or stop a moving note', flips === 0,
    `${flips}/8 notes change; e.g. 0.25x fwd x2 -> '${M.formatLane('travel', more.notes[0].c.travel[0].v)}', -1x x0.5 -> '${M.formatLane('travel', less.notes[6].c.travel[0].v)}' (scaling v around 0.75 crosses the dead band)`);
  const c = clipOf([N(60, 0, 1, 'a'), N(64, 1, 1, 'b')]), cj = J(c), inst = M.defaultInst();
  const empties = {
    applyShape: () => M.applyShape(c, [], 'tone', 'up', 'each'), setGen: () => M.setGen(c, [], 'tone', M.makeGen('pattern')), bakeGen: () => M.bakeGen(c, [], 'tone'), reroll: () => M.reroll(c, [], null),
    connect: () => M.connect(c, [], 'nearest', 1), walk: () => M.walk(c, [], 'tone', 0, 1), chop: () => M.chop(c, [], '1/16', 0).clip, reverseLane: () => M.reverseLane(c, [], 'amp'),
    flipLane: () => M.flipLane(c, [], 'amp'), scaleLane: () => M.scaleLane(c, [], 'amp', 2), humanize: () => M.humanize(c, [], { time: 0.1, vel: 0.2, lanes: 0.1 }, 1), strum: () => M.strum(c, [], '1/16', 'up'),
    legato: () => M.legato(c, []), setNoteSample: () => M.setNoteSample(c, [], { motion: 'bounce' }), applyRecipe: () => M.applyRecipe(c, inst, 4, []).clip, removeNotes: () => M.removeNotes(c, []), updateNotes: () => M.updateNotes(c, [], { p: 1 }),
  };
  const changed = Object.entries(empties).filter(([, f]) => J(f()) !== cj).map(([k]) => k);
  check('an empty selection ([]) changes nothing in any tool (null means every note, [] means none)', !changed.length, changed.length ? 'changed: ' + changed.join(',') : `${Object.keys(empties).length} tools checked`);
}

// =========================================================================== K. recipes, dice, presets
section('K. recipes, Dice, sound presets (S8, U4)');
{
  const inst = M.defaultInst(), clip = clipOf([N(60, 0, 2, 'a'), N(64, 2, 2, 'b')]);
  const messy = M.mergeInst(M.dice(inst, 77), { macros: [0.1, 0.9, 0.2, 0.8, 0.3, 0.7, 0.4, 0.6] });
  let dep = [], nonIdem = [];
  for (const R of M.RECIPES) {
    const a = M.applyRecipe(clip, inst, R.n, null), b = M.applyRecipe(clip, messy, R.n, null), c = M.applyRecipe(a.clip, a.inst, R.n, null);
    for (const k of SAMPLE_FIELDS) if (J(a.inst[k]) !== J(b.inst[k])) dep.push(R.n + ':' + k);
    if (J(c) !== J(a) || J(a.clip) !== J(b.clip)) nonIdem.push(R.n);
  }
  check('every recipe gives the same Sample settings whatever came before, and applying it twice = once', !dep.length && !nonIdem.length, `${M.RECIPES.length} recipes x ${SAMPLE_FIELDS.length} fields: ${dep.length} depend on the previous inst${dep.length ? ' (' + dep.join(',') + ')' : ''}; not idempotent ${nonIdem.join(',') || 'none'}`);
  const base = M.mergeInst(inst, { glideMode: 'always', root: 57, key: 3, scale: 'Minor', polyphony: 8, outGain: -3, vibRate: 7, velSens: 0.2, pitch: 5, seed: 9, mod1: { target: 'travel', cc: 11 }, bendRange: 7, offsets: { tone: -0.2 }, macros: [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8], region: { start: 0.2, end: 0.7, atEnd: 'hold', xfadeMs: 30 }, liveLegato: true, glideTime: 1.5, cents: -20, scaleLock: true });
  const nonS = Object.keys(inst).filter(k => !SAMPLE_FIELDS.includes(k)); let ch = [];
  for (let s = 1; s <= 200; s++) { const d = M.dice(base, s); for (const k of nonS) if (J(d[k]) !== J(base[k])) ch.push(k); }
  check('Dice rolls only Sample settings (every other inst field untouched over 200 seeds)', !ch.length, `${nonS.length} fields (${nonS.join(',')}): ${ch.length} changes${ch.length ? ' in ' + [...new Set(ch)].join(',') : ''}`);
  const leaks = [];
  for (const A of M.SOUND_PRESETS) for (const B of M.SOUND_PRESETS) {
    if (A === B) continue;
    const viaA = M.applySoundPreset(M.applySoundPreset(inst, A.id).inst, B.id).inst, fresh = M.applySoundPreset(inst, B.id).inst;
    for (const k of SAMPLE_FIELDS.concat(['region', 'macros'])) if (J(viaA[k]) !== J(fresh[k])) leaks.push(`${A.id}->${B.id}:${k}`);
  }
  const ex = M.applySoundPreset(M.applySoundPreset(inst, 'beat-scatter').inst, 'vowel-voice').inst;
  check('a sound preset sets the same engine and Sample settings whatever was loaded before (U4)', !leaks.length, `${leaks.length} leaks over ${M.SOUND_PRESETS.length * (M.SOUND_PRESETS.length - 1)} preset pairs, e.g. ${leaks.slice(0, 4).join(', ')}; 'Vowel voice' after 'Beat loop scatter' plays motion '${ex.motion}'`);
}

// =========================================================================== L. presets and loading
section('L. Strand Demo = v3 demo() lane for lane; v3 saves with numeric ids');
{
  // v3's demo(), shapeNodes and patternNodes (sine branch), verbatim from prototype-v3-strandlock.html
  const clampV = (v, a, b) => Math.max(a, Math.min(b, v));
  const V3L = [{ id: 'pitch', def: 0.5 }, { id: 'amp', def: 0.8 }, { id: 'vib', def: 0 }, { id: 'tone', def: 0.75 }, { id: 'formant', def: 0.5 }, { id: 'hollow', def: 0 }];
  const V3LANE = Object.fromEntries(V3L.map(l => [l.id, l])), vflat = v => [{ t: 0, v }, { t: 1, v }];
  function patternNodes({ shape, cycles, center, d0, d1, acc }) {
    const nodes = [], amp = t => (d0 + (d1 - d0) * t) / 2, warpInv = x => (acc === 0 ? x : Math.pow(x, 1 / (1 + acc))), C = Math.max(0.5, cycles);
    const push = (x, v, lin) => { const t = clampV(warpInv(clampV(x, 0, 1)), 0, 1); nodes.push({ t, v: clampV(v, 0, 1), lin: !!lin }); };
    if (shape === 'sine' || shape === 'tri') { const steps = Math.max(2, Math.round(C * 4)), ph = [0, 1, 0, -1]; for (let i = 0; i <= steps; i++) { const x = i / steps; push(x, center + ph[i % 4] * amp(x), shape === 'tri'); } }
    nodes.sort((a, b) => a.t - b.t); nodes[0].t = 0; nodes[nodes.length - 1].t = 1;
    for (let i = 1; i < nodes.length; i++) if (nodes[i].t <= nodes[i - 1].t) nodes[i].t = Math.min(1, nodes[i - 1].t + 1e-4);
    return nodes;
  }
  function shapeNodes(type, lane) {
    const P = lane === 'pitch', def = V3LANE[lane].def, sm = P ? 1 / 24 : 1;
    switch (type) {
      case 'up': return P ? [{ t: 0, v: 0.5 }, { t: 1, v: 0.5 + 2 * sm }] : [{ t: 0, v: 0 }, { t: 1, v: 1 }];
      case 'down': return P ? [{ t: 0, v: 0.5 }, { t: 1, v: 0.5 - 2 * sm }] : [{ t: 0, v: 1 }, { t: 1, v: 0 }];
      case 'swell': return P ? [{ t: 0, v: 0.5 }, { t: 0.5, v: 0.5 + sm }, { t: 1, v: 0.5 }] : [{ t: 0, v: 0.05 }, { t: 0.5, v: 1 }, { t: 1, v: 0.05 }];
      case 'decay': return P ? [{ t: 0, v: 0.5 + sm }, { t: 0.12, v: 0.5 }, { t: 1, v: 0.5 }] : [{ t: 0, v: 1 }, { t: 0.15, v: 0.55 }, { t: 1, v: 0 }];
    }
    return vflat(def);
  }
  const notes = []; let uid = 0;
  const add = (p, s, l) => { const n = { id: 'v' + (++uid), p, s, l, glide: null, gc: 0, glideTo: null, c: {} }; V3L.forEach(L => (n.c[L.id] = vflat(L.def))); notes.push(n); return n; };
  [55, 60, 64, 67].forEach((p, k) => { const n = add(p, 0, 4); n.c.tone = shapeNodes('up', 'tone'); n.c.amp = [{ t: 0, v: 0.35 }, { t: 0.6, v: 0.9 }, { t: 1, v: 0.75 }]; if (k === 3) n.c.vib = [{ t: 0, v: 0 }, { t: 0.4, v: 0 }, { t: 1, v: 0.55 }]; });
  [57, 60, 64, 69].forEach((p, k) => { const n = add(p, 4, 4); n.c.hollow = shapeNodes('swell', 'hollow'); if (k === 3) n.c.vib = [{ t: 0, v: 0.3 }, { t: 1, v: 0.6 }]; });
  const b1 = add(36, 0, 4), b2 = add(33, 4, 4); b1.c.formant = patternNodes({ shape: 'sine', cycles: 4, center: 0.5, d0: 0.1, d1: 0.7, acc: 0 }); b2.glide = 0;
  [72, 76, 79, 76, 74, 72, 71, 74].forEach((p, k) => { const n = add(p, 8.5 + k * 0.5, 0.5); n.c.amp = shapeNodes('decay', 'amp'); n.c.tone = shapeNodes('down', 'tone'); n.glide = 0; });
  const c3 = [55, 59, 62, 67].map(p => add(p, 12, 4)); c3.forEach(n => { n.c.tone = patternNodes({ shape: 'sine', cycles: 8, center: 0.55, d0: 0.05, d1: 0.8, acc: 0.6 }); n.c.amp = shapeNodes('down', 'amp').map(q => ({ t: q.t, v: 0.3 + 0.6 * q.v })); });
  const b3 = add(31, 8, 4), b4 = add(31, 12, 4); b3.c.pitch = [{ t: 0, v: 0.5 - 5 / 24 }, { t: 0.25, v: 0.5 }, { t: 1, v: 0.5 }]; b4.c.vib = [{ t: 0, v: 0 }, { t: 1, v: 0.4 }];
  const lead = add(84, 6, 2), lead2 = add(79, 10, 2); lead.glideTo = lead2.id; lead.glide = 1; lead2.c.vib = [{ t: 0, v: 0.2 }, { t: 1, v: 0.5 }]; lead.c.pitch = [{ t: 0, v: 0.5 - 2 / 24 }, { t: 0.3, v: 0.5 }, { t: 1, v: 0.5 }]; lead.c.vib = [{ t: 0, v: 0 }, { t: 0.35, v: 0 }, { t: 1, v: 0.7 }]; lead.c.amp = shapeNodes('swell', 'amp');
  const d = M.loadClipPreset('Strand Demo'); const diffs = [];
  notes.forEach((v, i) => {
    const n = d.notes[i];
    for (const L of M.LANES) {
      const v3 = v.c[L.id], isV3Default = !v3 || (v3.length === 2 && v3.every(q => q.v === V3LANE[L.id].def));
      const want = isV3Default ? [{ t: 0, v: L.def, lin: false }, { t: 1, v: L.def, lin: false }] : v3;   // untouched lanes take v4's default (Tone 1.0 = open)
      const got = n.c[L.id];
      if (!(got.length === want.length && got.every((q, j) => Math.abs(q.t - want[j].t) < 1e-12 && Math.abs(q.v - want[j].v) < 1e-12 && q.shape === (want[j].lin ? 'linear' : 'smooth')))) diffs.push(`${n.id}.${L.id}`);
    }
    if (v.glide !== n.glide || (v.glideTo ? 'sd-' + v.glideTo.slice(1) : null) !== n.glideTo || v.p !== n.p || v.s !== n.s || v.l !== n.l) diffs.push(n.id + ' note fields');
  });
  check('Strand Demo = v3 demo() on every lane node of all 26 notes (untouched lanes at v4 defaults)', !diffs.length, `${notes.length} notes x ${M.LANES.length} lanes, ${diffs.length} differ${diffs.length ? ': ' + diffs.slice(0, 6).join(', ') : ''}`);
  const v3save = { bars: 2, notes: [{ id: 5, p: 60, s: 0, l: 1, glideTo: 7, c: { tone: [{ t: 0, v: 0.75 }, { t: 1, v: 0.75 }] } }, { id: 7, p: 62, s: 1, l: 1, glideTo: 5, c: {} }, { id: 5, p: 64, s: 1.5, l: 0.5, c: {} }] };
  const lc = M.normalizeClip(v3save);
  check('v3 save with numeric ids and a duplicate id loads with unique string ids and its links', lc.notes[0].id === '5' && lc.notes[0].glideTo === '7' && lc.notes[1].glideTo === '5' && new Set(lc.notes.map(n => n.id)).size === 3 && lc.notes[0].c.tone[0].v === 0.75 && lc.bars === 2,
    `ids ${lc.notes.map(n => n.id).join(',')}; links ${lc.notes.map(n => n.glideTo).join(',')}; saved v3 Tone 0.75 kept ${lc.notes[0].c.tone[0].v}`);
}

// =========================================================================== M. snapshot
section('M. compileSnapshot: tables, determinism across fresh loads, size');
{
  const inst = M.defaultInst();
  let c = M.loadClipPreset('Strand Demo');
  c = M.setGen(c, ['sd-1', 'sd-9'], 'tone', M.makeGen('random', { perNote: 'different', smooth: 0.3 }));
  c = M.setNoteSample(c, ['sd-3'], { engine: 'cloud', lock: true, motion: 'scatter', jump: '1/16T', start: { mode: 'random', range: 0.4 } });
  c = M.setNoteSample(c, ['sd-4'], { engine: 'tape', lock: true });
  const snap = M.compileSnapshot(c, inst);
  let typeBad = 0, valBad = 0;
  const nc = M.normalizeClip(c);
  for (const sn of snap.notes) { const n = byId(nc, sn.id); for (const id of M.LANE_IDS) { if (!(sn.lanes[id] instanceof Float32Array)) typeBad++; else if (maxAbsDiff(sn.lanes[id], M.laneTable(n, id, 256)) !== 0) valBad++; } }
  const s3 = byId(snap, 'sd-3'), s4 = byId(snap, 'sd-4');
  check('snapshot lanes are Float32Arrays equal to laneTable of each note; per-note settings resolve', typeBad === 0 && valBad === 0 && s3.engine === 'cloud' && s3.lock === true && s3.motion === 'scatter' && Math.abs(s3.jump - 1 / 6) < 1e-12 && s3.start.mode === 'random' && s4.lock === false && J(Object.keys(s3.start).slice(0, 3)) === J(['mode', 'pos', 'range']),
    `${snap.notes.length * 10} tables: ${typeBad} not Float32Array, ${valBad} differ; sd-3 ${s3.engine}+lock ${s3.lock}/${s3.motion}/jump ${fx(s3.jump, 4)}; sd-4 tape lock -> ${s4.lock}`);
  const src = fs.readFileSync(SRC, 'utf8');
  const fresh = () => { const ctx = { module: { exports: {} } }; vm.createContext(ctx); vm.runInContext(src, ctx); return ctx.module.exports; };
  const build = X => { let q = X.loadClipPreset('Strand Demo'); q = X.applyShape(q, null, 'tone', 'random', 'each', { seed: 4 }); q = X.setGen(q, null, 'formant', X.makeGen('random', { perNote: 'different' })); q = X.chop(q, ['sd-1'], '1/8', true).clip; q = X.humanize(q, null, { time: 0.02, vel: 0.1, lanes: 0.05 }, 3); q = X.addNote(q, 61, 3, 1).clip; q = X.duplicateNotes(q, ['sd-2', 'sd-3']).clip; q = X.connect(q, null, 'random', 5); return J(X.compileSnapshot(q, X.defaultInst())); };
  const A = fresh(), B = fresh();
  check('the same edits in two fresh loads of sl-model.js give identical snapshots (no load-time salt reaches the sound)', build(A) === build(B) && build(A) === build(M), `two vm contexts + this one: identical ${build(A) === build(B) && build(A) === build(M)}`);
  const big = M.chop(clipOf(Array.from({ length: 64 }, (_, i) => N(48 + (i % 24), (i % 16) * 4, 4, 'b' + i)), 16), null, '1/16', 0).clip;
  const ts = []; for (let i = 0; i < 5; i++) { const t0 = process.hrtime.bigint(); M.compileSnapshot(big, inst); ts.push(Number(process.hrtime.bigint() - t0) / 1e6); }
  info(`compileSnapshot of the largest clip chop allows (${big.notes.length} notes, res 256): ${dist(ts, 1)} ms per compile`);
  check('compileSnapshot at the 1024-note chop limit stays under 250 ms', Math.min(...ts) < 250, dist(ts, 1) + ' ms');
}

// =========================================================================== N. registry and defaults
section('N. registry and note defaults');
{
  const modes = M.MODES.map(m => `${m.mode}:${m.name}:${m.lanes.join('/')}`).join(' ');
  check('modes 1 Build ... 7 Hollow, 8 Sample with Travel/Heads/Grain/Refresh', modes === '1:Build: 2:Pitch:pitch 3:Volume:amp 4:Vibrato:vib 5:Tone:tone 6:Formant:formant 7:Hollow:hollow 8:Sample:travel/heads/grain/refresh' && M.LANES.map(L => L.mode).join('') === '2345678888', modes);
  const n = M.mkNote(60, 1, 2, 'x'), sm = n.sample;
  check('mkNote defaults = BLUEPRINT 2.1', n.vel === 0.8 && n.glide === null && n.gc === 0 && n.glideTo === null && sm.motion === null && sm.travel === 'speed' && sm.start.mode === 'region' && sm.start.pos === 0 && sm.start.range === 0.25 && sm.tape === null && sm.rev === null && sm.jump === null && sm.engine === null && sm.lock === null && sm.restart === false && M.LANE_IDS.every(id => n.gen[id] === null && n.c[id].length === 2 && n.c[id].every(q => q.v === M.LANE[id].def && q.shape === 'smooth' && q.bend === 0)) && J(M.newClip()) === J({ bars: 4, notes: [] }),
    J(sm));
}

// =========================================================================== O. purity of the helpers
section('O. purity: helpers outside the clip tools leave frozen inputs alone');
{
  const nd = deepFreeze([M.mkNode(0, 0.1, 'bend', 0.4), M.mkNode(0.3, 0.8, 'step'), M.mkNode(0.6, 0.3, 'linear'), M.mkNode(1, 0.6)]);
  const unsorted = deepFreeze([{ t: 0.7, v: 0.2 }, { t: 0.1, v: 0.9, lin: true }, { t: 0.4, v: 0.5 }]);
  const inst = deepFreeze(M.defaultInst()), note = deepFreeze(M.loadClipPreset('Vowel Morph').notes[0]);
  const snapIn = deepFreeze(M.loadClipPreset('Strand Demo'));
  const calls = {
    sampleNodes: () => M.sampleNodes(nd, 50), sampleNodesUnsorted: () => M.sampleNodes(unsorted, 50), evalNodes: () => M.evalNodes(unsorted, 0.5), normalizeNodes: () => M.normalizeNodes(unsorted),
    sliceNodes: () => M.sliceNodes(nd, 0.2, 0.7), reverseNodes: () => M.reverseNodes(nd), rdp: () => M.rdp(nd, 0.1), laneTable: () => M.laneTable(note, 'formant', 128, inst),
    mapLane: () => M.mapLane('refresh', 0.3, { inst }), formatLane: () => M.formatLane('grain', 0.3, { inst }), mergeInst: () => M.mergeInst(inst, { refresh: { mode: 'sync' } }), normalizeInst: () => M.normalizeInst(inst),
    normalizeNote: () => M.normalizeNote(note, 0), cloneNote: () => M.cloneNote(note), shapeNodesFit: () => M.shapeNodes('fit', 'travel', { kind: 'path', beats: 8, inst }),
    history: () => { const h = new M.History({ clip: snapIn, inst }); h.push({ clip: M.applyShape(snapIn, null, 'tone', 'up', 'each'), inst }, 'x'); return h.undo(); },
    compileTwice: () => [M.compileSnapshot(snapIn, inst), M.compileSnapshot(snapIn, inst)],
  };
  const broke = [];
  for (const [k, f] of Object.entries(calls)) { try { f(); } catch (e) { broke.push(k + ': ' + e.message); } }
  const cn = M.cloneNote(note); cn.c.formant.push({ t: 1, v: 0 }); cn.gen.formant && (cn.gen.formant.seed = 99);
  check('helpers never write to frozen inputs, and cloneNote shares no mutable lane or gen object', !broke.length && note.c.formant.length !== cn.c.formant.length, broke.length ? broke.join('; ') : `${Object.keys(calls).length} helpers on deep-frozen inputs; clone edits stay in the clone`);
}

console.log(`\n${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
