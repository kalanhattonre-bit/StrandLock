// tools/fixtures/model.js: Tests/fixtures/model/*.json (PLUGIN-PLAN 5.3). The prototype saves and their import results
// are M8's, in tools/fixtures/state.js.
// Every expected value is what sl-model.js returns; this file only chooses inputs and writes results down.
'use strict';
const C = require('./common.js');

function deepFreeze(o) {
  if (o && typeof o === 'object' && !ArrayBuffer.isView(o) && !Object.isFrozen(o)) { Object.freeze(o); for (const k of Object.keys(o)) deepFreeze(o[k]); }
  return o;
}

module.exports = function model() {
  const { M } = C.load();
  const named = C.namedClips();
  for (const k of Object.keys(named)) deepFreeze(named[k]);   // an operation that wrote to its input would throw here
  const P = () => C.prototypeHashes();
  const files = {};
  files['model/seeds.json'] = Object.assign({ prototype: P() }, seeds(M));
  files['model/nodes.json'] = Object.assign({ prototype: P() }, nodesFile(M));
  files['model/shapes.json'] = Object.assign({ prototype: P() }, shapes(M, named));
  files['model/generators.json'] = Object.assign({ prototype: P() }, generators(M, named));
  files['model/tools.json'] = Object.assign({ prototype: P() }, tools(M, named));
  files['model/glides.json'] = Object.assign({ prototype: P() }, glides(M, named));
  files['model/snapshots.json'] = Object.assign({ prototype: P() }, snapshots(M, named));
  files['model/presets.json'] = Object.assign({ prototype: P() }, presets(M, named));
  files['model/format.json'] = Object.assign({ prototype: P() }, format(M));
  files['model/normalize.json'] = Object.assign({ prototype: P() }, normalize(M));
  return files;
};

// =================================================================================================== seeds.json
function seeds(M) {
  const cases = [];
  const R = C.rng(101);
  const i32 = () => Math.floor(R() * 4294967296) | 0;
  const u32 = () => Math.floor(R() * 4294967296);
  // hash32
  const H = args => cases.push({ kind: 'hash32', args, h: M.hash32(...args) });
  H([]);
  for (const x of [0, 1, -1, 2, 7, 255, 65535, 2147483647, -2147483648, 4294967295, 0x9747b28c, 0x5ca7, 0x51c1, 0x5b7a, 0x0d71, 0x7177, 0x57a7, 0x5a9e, 0x0d1ce]) H([x]);
  for (const a of [[0, 0], [1, 2], [2, 1], [0, 0, 0], [1, 2, 3, 4], [1, 2, 3, 4, 5], [1, 2, 3, 4, 5, 6], [-1, -1, -1, -1, -1, -1, -1, -1], [0x0d1ce, 1, 0, 1], [0x0d1ce, 19, 0, 1]]) H(a);
  for (let i = 0; i < 220; i++) H(Array.from({ length: 1 + Math.floor(R() * 5) }, i32));
  for (let i = 0; i < 50; i++) H(Array.from({ length: 1 + Math.floor(R() * 5) }, u32));
  // arguments outside int32 (counters past 2^31 in long notes) and non-integers: each goes through toInt32
  for (const a of [[2147483648], [4294967301], [-3000000000], [1099511627779], [9007199254740992], [1.5], [-0.5], [-1.9, 2.9], [123456789012, 5, 0x5ca7], [3e9, 1, 2, 3, 0x51c1]]) H(a);
  // toInt32 (JS x | 0)
  const T = x => cases.push({ kind: 'toInt32', x, i: x | 0 });
  for (const x of [0, -0, 0.5, -0.5, 1.9999, -1.9999, 2147483647, 2147483648, 2147483649, -2147483648, -2147483649, 4294967295, 4294967296, 4294967297,
    9007199254740992, -9007199254740992, 9007199254740994, 1e20, -1e20, 1.7976931348623157e308, 5e-324, NaN, Infinity, -Infinity, 3000000000.7, -3000000000.7,
    1e10 + 0.5, 6442450944, -6442450945, 2.5, -2.5, 255.99]) T(x);
  for (let i = 0; i < 48; i++) { const e = Math.floor(R() * 14); T((R() * 2 - 1) * Math.pow(10, e)); }
  // idHash (FNV-1a over UTF-16 code units)
  const strs = ['', 'a', 'foobar', 'sd-1', 'sd-30', 'live:60', 'live:0', 'live:127', 'n1abc', 'chop:sd-1', 'dup:sd-1:4', 'add:0:60:0:1', 'add:12:61.5:0.375:0.25',
    'Strand', 'STRAND', 'sd-1~1', 'sd-1~12', 'wt-16', 'pad-swell-3', 'cg-x', 'gn-1', 'ms-7', '12345', '0', '-1', 'NaN', 'undefined', ' ', 'a b', '\t', '"', '\\', '/',
    'ü', 'é', 'ß', 'Ω', 'ñandú', '日本語', 'Ä😀', '😀', 'a😀b', '😀😀', '𝄞 clef', 'ÿ', 'Ā', '߿', 'ࠀ', '￿', 'x'.repeat(200), 'ab'.repeat(77),
    'strand-demo', 'Saw that opens up', 'n' + (4294967295).toString(36), 'n0', 'nzzzzzz', 'p:60', 'id:with:colons', 'trailing ', 'ÀÁÂÃÄÅÆ'];
  for (const s of strs) cases.push({ kind: 'idHash', s, h: M.idHash(s) });
  // rates
  for (const id of M.RATES.concat(['', '1/3', '2bar', '1/16t', '1barTT', '1/128'])) cases.push({ kind: 'rate', id, beats: M.rateBeats(id), label: M.rateLabel(id) });
  for (const id of [0.75, 3, -1, 0]) cases.push({ kind: 'rate', id, beats: M.rateBeats(id), label: M.rateLabel(id) });
  const RF = (beats, tol) => cases.push({ kind: 'rateFromBeats', beats, tol: tol === undefined ? null : tol, id: M.rateFromBeats(beats, tol) });
  for (const id of M.RATES) { const b = M.rateBeats(id); RF(b); RF(b * 1.005, 0.01); RF(b * 1.02, 0.01); RF(b * 1.02, 0.03); }
  for (const b of [0.3, 3, 5, 0.01, 0.2, 7, 0.041, 1e-6, 100]) RF(b);
  RF(0.25, 0); RF(0.2500001, 0);
  // travelSpeed: a sweep, the Float32 values around both dead-band edges, and random values
  const TS = v => cases.push({ kind: 'travelSpeed', v, s: M.travelSpeed(v) });
  for (let i = 0; i <= 100; i++) TS(i / 100);
  for (const e of [0.48, 0.52, 0.48 - M.F32_EPS, 0.52 + M.F32_EPS]) {
    let f = Math.fround(e); TS(e); TS(f);
    for (let k = 1; k <= 3; k++) { TS(C.f32Step(f, k)); TS(C.f32Step(f, -k)); }
  }
  TS(M.V_FWD_MIN); TS(M.V_BACK_MAX); TS(C.f32Step(M.V_FWD_MIN, -1)); TS(C.f32Step(M.V_BACK_MAX, 1));
  for (const v of [-0.5, -0.01, 1.01, 1.5, 0.25, 0.75, 0.0499, 0.9501, 0.02, 0.98]) TS(v);
  while (cases.filter(c => c.kind === 'travelSpeed').length < 300) TS(R());
  const TV = x => cases.push({ kind: 'travelSpeedToV', x, v: M.travelSpeedToV(x) });
  for (const x of [0, -0, NaN, Infinity, -Infinity, 0.125, -0.125, 0.25, -0.25, 0.5, -0.5, 1, -1, 2, -2, 4, -4, 8, -8, 16, -16, 0.01, -0.01, 0.1249, -0.1249, 7.99, -7.99]) TV(x);
  for (let i = 0; i < 60; i++) TV((R() * 2 - 1) * 10);
  cases.push({ kind: 'constants', F32_EPS: M.F32_EPS, V_FWD_MIN: M.V_FWD_MIN, V_BACK_MAX: M.V_BACK_MAX });
  for (let i = 0; i <= 20; i++) for (let j = 0; j <= 20; j++) { const u = i / 20, b = j / 10 - 1; cases.push({ kind: 'bendShape', u, bend: b, y: M.bendShape(u, b) }); }
  for (let i = 0; i <= 20; i++) for (let j = 0; j <= 20; j++) { const u = i / 20, c = j / 10 - 1; cases.push({ kind: 'gShape', u, c, y: M.gShape(u, c) }); }
  // String(number): ECMAScript Number::toString
  const JN = x => cases.push({ kind: 'jsNumber', x, s: String(x) });
  const nums = [0, -0, 1, -1, 123, 2147483648, 4294967296, 9007199254740992, 9007199254740994, 1e21, 999999999999999868928, 1e20, 123456789012345680000,
    0.1, 0.2, 0.30000000000000004, 1 / 3, 2 / 3, -1 / 3, 1e-7, 9.99e-8, 1.5e-7, 1e-6, 1.234e-7, 5e-324, 2.2250738585072014e-308, 1.7976931348623157e308, 1e300, 1.23e25,
    0.37, 8.5, 12.25, 0.0625, 0.041666666666666664, 61.5, 0.375, 1e-5, 0.000001, 100, 1e6, 1.5e300, -1.5e-300, NaN, Infinity, -Infinity, 4.35, 0.1 + 0.7, 5e-7, 1e21 + 1e6];
  for (const x of nums) JN(x);
  while (cases.filter(c => c.kind === 'jsNumber').length < 500) {
    const e = Math.floor(R() * 44) - 22, sgn = R() < 0.3 ? -1 : 1, kind = R();
    JN(kind < 0.2 ? sgn * Math.round(R() * Math.pow(10, Math.abs(e) % 16)) : kind < 0.4 ? sgn * Math.round(R() * 1e6) / 1e3 : sgn * R() * Math.pow(10, e));
  }
  // toFixed / toPrecision (ties go to the larger candidate: 0.125.toFixed(2) = '0.13')
  const FX = (x, d) => cases.push({ kind: 'toFixed', x, d, s: x.toFixed(d) });
  for (const x of [0.125, 1.005, 2.5, -2.5, 0.5, -0.5, 1.45, 8.345, 1e21, 2e21, 1e-10, 123.456, -0.0001, 0, -0, 0.05, 0.15, 0.25, 0.35, 99.995, 1.5, 4.35, 2.675, 1234.5678, 0.0049, -1.005])
    for (let d = 0; d <= 6; d++) FX(x, d);
  for (let i = 0; i < 40; i++) FX((R() * 2 - 1) * Math.pow(10, Math.floor(R() * 8) - 3), Math.floor(R() * 7));
  const PR = (x, d) => cases.push({ kind: 'toPrecision', x, d, s: x.toPrecision(d) });
  for (const x of [123.456, 0.000123, 1e21, 1.5, 2.5e-7, 99.95, 0.5, 1, 1.0049999, 1.005, 2.83, 0.125, -0.125, 4096, 1e-7, 123456789, 0.999999, 1.41421356, 8, 0.0625])
    for (let d = 1; d <= 6; d++) PR(x, d);
  // toString(36) of a uint32
  for (const u of [0, 1, 35, 36, 1295, 1296, 46655, 46656, 2147483647, 2147483648, 4294967295]) cases.push({ kind: 'base36', u, s: u.toString(36) });
  for (let i = 0; i < 49; i++) { const u = M.hash32(i, 36); cases.push({ kind: 'base36', u, s: u.toString(36) }); }
  // Math.round (ties toward +inf)
  const RO = x => cases.push({ kind: 'jsRound', x, r: Math.round(x) });
  for (const x of [0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999999999999994, -0.49999999999999994, 4503599627370495.5, -4503599627370495.5, 1e20, -1e20, 0, -0, 0.4, -0.4,
    NaN, Infinity, -Infinity, 59.5, 60.5, -60.5, 99.99999999999999, 123456.5, -123456.5, 1e-300]) RO(x);
  for (let i = 0; i < 74; i++) RO((R() * 2 - 1) * 1000 * (R() < 0.5 ? 1 : 0.001));
  // scales and names
  for (let key = 0; key < 12; key++) for (const scale of Object.keys(M.SCALES)) {
    let s = ''; const snap = [];
    for (let p = 0; p < 128; p++) { s += M.inScale(p, key, scale) ? '1' : '0'; snap.push(M.snapScale(p, key, scale)); }
    cases.push({ kind: 'scale', key, scale, inScale: s, snap });
  }
  for (let p = 0; p < 128; p++) cases.push({ kind: 'noteName', p, name: M.noteName(p) });
  for (const p of [59.5, 60.4, 60.5, -0.4, 127.2]) cases.push({ kind: 'noteName', p, name: M.noteName(p) });
  return {
    tolerance: { text: 'integers and strings exact; the dead-band decision (s == 0) exact; arithmetic doubles exact; bendShape, gShape, travelSpeed, travelSpeedToV within 4e-16 relative',
      default: 'exact', relative: { bendShape: 4e-16, gShape: 4e-16, travelSpeed: 4e-16, travelSpeedToV: 4e-16 }, exact: ['travelSpeed s == 0'] },
    notes: ['hash32 args may be integers outside int32 and a few non-integers: every argument goes through toInt32 (plan 1.6 rule 4)',
      'rate: an unknown id reads 0.25 beats and labels as itself; a numeric id passes through rateBeats when > 0 and labels as String(x)',
      'rateFromBeats: tol null = the default 0.01'],
    cases,
  };
}

// =================================================================================================== nodes.json
function nodesFile(M) {
  const R = C.rng(202);
  const inputs = [];
  const add = (name, input, def) => inputs.push({ name, input, def: def == null ? 0.5 : def });
  const ob = list => list.map(a => { const o = { t: a[0], v: a[1] }; if (a[2]) o.shape = a[2]; if (a[3] != null) o.bend = a[3]; return o; });
  for (const d of [0.5, 0.8, 0, 1]) add('flat-' + d, ob([[0, d], [1, d]]), d);
  add('empty', [], 0.8); add('single', ob([[0.3, 0.7]]), 0.5); add('not-an-array', null, 0);
  for (const sh of ['smooth', 'linear', 'step']) { add('ramp-up-' + sh, ob([[0, 0, sh], [1, 1]])); add('ramp-down-' + sh, ob([[0, 0.9, sh], [1, 0.1]]), 1); }
  for (const b of [0.5, -0.5, 1, -1, 0.0005, 0.24, -0.24]) add('bend-' + b, ob([[0, 0.1, 'bend', b], [1, 0.95]]), 0);
  add('v3-demo-amp', ob([[0, 0.35], [0.6, 0.9], [1, 0.75]]), 0.8);
  add('zigzag', ob([[0, 0], [0.25, 1], [0.5, 0], [0.75, 1], [1, 0]]), 0);
  add('steep-monotone', ob([[0, 0], [0.1, 0.05], [0.15, 0.9], [0.5, 0.95], [1, 1]]), 0);
  add('near-flat', ob([[0, 0.5], [0.3, 0.5000001], [0.6, 0.4999], [1, 0.5]]));
  add('vertical-edge', ob([[0, 0.2, 'linear'], [0.5, 0.2, 'linear'], [0.5, 0.8, 'linear'], [1, 0.8]]), 0.5);
  add('edge-on-grid', ob([[0, 0.1], [0.25, 0.1, 'linear'], [0.25, 0.9], [0.75, 0.4, 'step'], [1, 0.6]]));
  add('steps', ob([[0, 0.1, 'step'], [0.25, 0.5, 'step'], [0.75, 0.9, 'step'], [1, 0.3]]), 0);
  add('step-to-end', ob([[0, 0.2, 'smooth'], [0.6, 0.7, 'step'], [1, 0.1]]), 0.5);
  add('unsorted-ties', ob([[0.7, 0.2], [0.2, 0.9], [0.5, 0.5], [0.2, 0.1, 'linear'], [0.5, 0.6]]));
  add('v3-lin', [{ t: 0, v: 0.2, lin: true }, { t: 0.5, v: 0.8, lin: false }, { t: 0.75, v: 0.3, lin: true }, { t: 1, v: 0.5, lin: true }]);
  add('v3-lin-and-shape', [{ t: 0, v: 0.2, lin: true, shape: 'step' }, { t: 1, v: 0.8 }]);
  add('nan-t', [{ t: NaN, v: 0.5 }, { t: 0.4, v: 0.9 }, { t: 1, v: 0.1 }], 0.8);
  add('nan-v', [{ t: 0, v: 0.3 }, { t: 0.5, v: NaN }, { t: 1, v: 0.6 }], 0.8);
  add('out-of-range', [{ t: -0.2, v: 1.5, shape: 'bend', bend: 3 }, { t: 0.5, v: -0.4 }, { t: 1.3, v: 0.7, shape: 'warp' }], 0.5);
  add('missing-fields', [{}, { v: 0.4 }, { t: 1 }], 0.8);
  add('strings', [{ t: '0', v: '0.5' }, { t: 1, v: 0.9, bend: '1' }], 0.5);
  add('two-at-zero', ob([[0, 0.3], [0, 0.7], [1, 0.5]]));
  add('two-at-one', ob([[0, 0.3], [1, 0.7], [1, 0.1]]));
  // more than 128 nodes: capNodes simplifies them
  { let v = 0.5; const L = []; for (let i = 0; i < 200; i++) { v = Math.min(1, Math.max(0, v + (R() - 0.5) * 0.2)); L.push([i / 199, v]); } add('walk-200', ob(L)); }
  { const L = []; for (let i = 0; i < 300; i++) L.push([i / 299, 0.5 + 0.45 * Math.sin(i / 299 * 2 * Math.PI * 7), 'linear']); add('sine-300', ob(L)); }
  { const L = []; for (let i = 0; i < 129; i++) L.push([i / 128, i % 2 ? 0.9 : 0.1]); add('alt-129', ob(L)); }
  // the prototype's own generators of node lists
  const raw = nd => nd.map(q => ({ t: q.t, v: q.v, shape: q.shape, bend: q.bend }));
  for (const [sh, cyc, acc] of [['sine', 3, 0], ['tri', 2, 0.5], ['saw', 4, 0], ['square', 3, -0.3], ['random', 5, 0], ['sine', 8, 0.6], ['random', 2.5, 1]])
    add('pattern-' + sh + '-' + cyc + '-' + acc, raw(M.patternNodes({ shape: sh, cycles: cyc, center: 0.5, d0: 0.3, d1: 0.8, acc, seed: 7 })));
  for (const [k, lane, o] of [['tapestop', 'travel', { kind: 'speed' }], ['tapestart', 'travel', { kind: 'speed' }], ['rewind', 'travel', { kind: 'speed' }], ['reversehalf', 'travel', { kind: 'speed' }],
    ['decay', 'amp', {}], ['swell', 'hollow', {}], ['random', 'pitch', { seed: 3, idh: 12345 }], ['fit', 'travel', { kind: 'path', beats: 7, bpm: 120, sampleSeconds: 2 }], ['squiggle', 'travel', { kind: 'path' }]])
    add('shape-' + k + '-' + lane, raw(M.shapeNodes(k, lane, o)), M.laneDef(lane) === 0.75 ? 0.5 : M.laneDef(lane));
  const SH = ['smooth', 'linear', 'bend', 'step'];
  while (inputs.length < 80) {
    const n = 3 + Math.floor(R() * 10), L = [];
    for (let i = 0; i < n; i++) { const sh = SH[Math.floor(R() * 4)]; L.push([i === 0 ? 0 : i === n - 1 ? 1 : R(), R(), sh, sh === 'bend' ? Math.round((R() * 2 - 1) * 100) / 100 : 0]); }
    L.sort((a, b) => a[0] - b[0]);
    add('random-' + inputs.length, ob(L), [0.5, 0.8, 0, 1][inputs.length % 4]);
  }
  const SLICES = [[0, 1], [0, 0.5], [0.25, 0.75], [0.1, 0.13], [0.3, 0.3], [0.6, 0.4], [0.33, 0.9]];
  const laneFor = def => ({ 0.5: 'pitch', 0.8: 'amp', 0: 'vib', 1: 'tone' })[def];
  const cases = inputs.map(({ name, input, def }) => {
    const nd = M.normalizeNodes(input, def);
    const ev = left => Array.from({ length: 33 }, (_, j) => M.evalNodes(nd, j / 32, left));
    const lane = laneFor(def);
    const one = { bars: 4, notes: [Object.assign(M.mkNote(60, 0, 1, 'x'), {})] };
    one.notes[0].c[lane] = nd;
    const flipped = M.flipLane(one, null, lane).notes[0].c[lane];
    const scaled = [0.5, 2].map(f => ({ f, nodes: C.nodes(M.scaleLane(one, null, lane, f).notes[0].c[lane]) }));
    const o = { name, input, def, normalized: C.nodes(nd), sample257: M.sampleNodes(nd, 257), evalRight: ev(false), evalLeft: ev(true),
      slices: SLICES.map(([u0, u1]) => ({ u0, u1, nodes: C.nodes(M.sliceNodes(nd, u0, u1)) })),
      reversed: C.nodes(M.reverseNodes(nd)), flipped: C.nodes(flipped), scaled };
    if (Array.isArray(input) && input.length > M.MAX_NODES) o.capped = true;
    return o;
  });
  return {
    tolerance: { text: 'node lists: same length and shapes, t/v/bend within 1e-12; tables: every value within 2 Float32 ulps (printed: share bit-identical); eval within 1e-12',
      nodes: { t: 1e-12, v: 1e-12, bend: 1e-12 }, tableUlps: 2, eval: 1e-12 },
    notes: ['every operation reads the normalized list (what a note holds), never the raw input',
      'flipped = flipLane and scaled = scaleLane on a one-note clip whose lane has this def (0.5 pitch, 0.8 amp, 0 vib, 1 tone): flipNodes / scaleNodes(nd, def, f)',
      'evalRight[j] = evalNodes(nd, j/32, false), evalLeft[j] = evalNodes(nd, j/32, true)'],
    cases,
  };
}

// =================================================================================================== shapes.json
function shapes(M, named) {
  const clip = named.shapes4, cases = [];
  const ALL = clip.notes.map(n => n.id), SUB = ['s4-b', 's4-c'];
  const run = (lane, key, scope, ids, opts) => {
    const out = M.applyShape(clip, ids, lane, key, scope, opts);
    const notes = {};
    for (const n of out.notes) if (!ids || ids.includes(n.id)) notes[n.id] = C.nodes(n.c[lane]);
    cases.push({ clip: 'shapes4', lane, key, scope, ids, opts, notes });
  };
  for (const lane of M.LANE_IDS) {
    const keys = lane === 'travel' ? M.SHAPE_KEYS.common.concat(M.SHAPE_KEYS.speed, M.SHAPE_KEYS.path) : M.shapeKeysFor(lane);
    for (const key of keys) for (const scope of ['each', 'across']) for (const ids of [ALL, SUB]) {
      if (key === 'fit') continue;
      run(lane, key, scope, ids, { seed: 1 });
      if (key === 'random') run(lane, key, scope, ids, { seed: 7 });
    }
  }
  // Fit (Path kind): looping and holding Regions, two tempos, two sample lengths
  for (const region of [{ start: 0.2, end: 0.5, atEnd: 'loop' }, { start: 0.2, end: 0.5, atEnd: 'hold' }, { start: 0, end: 1, atEnd: 'loop' }])
    for (const [bpm, secs] of [[120, 6], [87, 2]]) for (const scope of ['each', 'across']) run('travel', 'fit', scope, ALL, { bpm, sampleSeconds: secs, region });
  run('travel', 'fit', 'each', ALL, { bpm: 120, sampleSeconds: 6, inst: { region: { start: 0.1, end: 0.15, atEnd: 'stop' } } });
  run('pitch', 'wobble', 'each', ALL, { seed: 1 });
  return {
    tolerance: { text: 'as nodes', nodes: { t: 1e-12, v: 1e-12, bend: 1e-12 } },
    notes: ['notes holds the lane of every selected note after applyShape (unchanged notes too, e.g. a Speed-only key on the Path-kind note s4-c)',
      'ids null would mean every note; here ids is always explicit', 'opts.inst (one Fit case) is a partial inst that fitNodes normalizes'],
    clips: { shapes4: C.clip(clip) },
    cases,
  };
}

// =================================================================================================== generators.json
function generators(M, named) {
  const cases = [];
  const R = C.rng(303);
  const res = 256;
  // the random value of each step k, by the seed recipe of plan 1.7 (checked below against the generator itself)
  const stepRange = (g, n) => {
    const P = M.rateBeats(g.rate), ph = t => (g.lockToBeat ? (n.s + t * n.l) / P : (n.l * (g.warp === 0 ? t : Math.pow(t, 1 + g.warp))) / P);
    return [Math.floor(ph(0) + 1e-9) - 1, Math.floor(ph(1) + 1e-9) + 1];
  };
  const stepVal = (g, n, lane, k) => {
    const L = M.laneIndex(lane), diff = g.perNote === 'different', idh = diff ? M.idHash(g.key != null ? g.key : n.id) : 0;
    return M.rand01(diff ? M.hash32(idh, L, k, g.seed) : M.hash32(L, k, g.seed)) * 2 - 1;
  };
  let selfChecked = 0;
  const one = (lane, g, s, l, id, base) => {
    const n = M.mkNote(64, s, l, id);
    if (base) n.c[lane] = M.normalizeNodes(base, M.laneDef(lane));
    n.gen[lane] = M.normalizeGen(g);
    const ng = n.gen[lane];
    const o = { note: C.note(n), lane, res, table: M.laneTable(n, lane, res) };
    if (ng.kind === 'random' || ng.shape === 'randsteps' || ng.shape === 'smoothrandom') {
      const [k0, k1] = stepRange(ng, n);
      o.k0 = k0; o.steps = [];
      for (let k = k0; k <= k1; k++) o.steps.push(stepVal(ng, n, lane, k));
      if (ng.kind === 'random' && ng.smooth === 0) {   // the recipe must equal what genEval plays, sample for sample
        const scale = lane === 'pitch' ? M.PITCH_GEN_SCALE : 1, sg = ng.invert ? -1 : 1, P = M.rateBeats(ng.rate);
        for (let j = 0; j <= 64; j++) {
          const t = j / 64, ph = ng.lockToBeat ? (n.s + t * n.l) / P : (n.l * (ng.warp === 0 ? t : Math.pow(t, 1 + ng.warp))) / P, k = Math.floor(ph + 1e-9);
          const want = sg * o.steps[k - k0] * ng.amount * 0.5 * scale, got = M.genOffset(ng, n, lane, t);
          if (want !== got) throw new Error('generators: step recipe disagrees with genEval at ' + id + ' t=' + t + ': ' + want + ' vs ' + got);
        }
        selfChecked++;
      }
    }
    cases.push(o);
  };
  const SH = M.GEN_SHAPES, RATES = ['1/16T', '1/8', '1/4D', '1bar', '1/64'], S0 = [0, 0.37, 5.5];
  let k = 0;
  for (const shape of SH) for (const rate of RATES) {
    { const lane = M.LANE_IDS[k % 10]; one(lane, { kind: 'pattern', shape, rate, lockToBeat: true, d0: 0.3, d1: 0.7, seed: 1 + (k % 3) }, S0[k % 3], 1, 'g' + (k++)); }
    for (const warp of [0, 0.8, -0.5]) { const lane = M.LANE_IDS[k % 10]; one(lane, { kind: 'pattern', shape, rate, lockToBeat: false, warp, d0: 0.5, d1: 0.2, perNote: k % 2 ? 'different' : 'same' }, S0[k % 3], 1, 'g' + (k++)); }
  }
  for (const smooth of [0, 0.4, 1]) for (const perNote of ['same', 'different']) for (const amount of [0.5, 1]) for (const seed of [1, 2]) for (const key of [null, 'other']) {
    const lane = M.LANE_IDS[k % 10];
    one(lane, { kind: 'random', rate: k % 2 ? '1/8' : '1/16T', smooth, perNote, amount, seed, key }, S0[k % 3], 1, 'g' + (k++));
  }
  // the pitch lane: depth 1 = +-2 semitones (1/6 scale), on a drawn line that the generator pushes past 0..1
  for (const shape of ['squiggle', 'square', 'randsteps']) one('pitch', { kind: 'pattern', shape, rate: '1/8', d0: 1, d1: 1 }, 0.25, 2, 'gp-' + shape);
  one('pitch', { kind: 'random', rate: '1/4', amount: 1, seed: 4 }, 0, 4, 'gp-random');
  one('amp', { kind: 'pattern', shape: 'sawup', rate: '1/8', d0: 1, d1: 1 }, 0, 2, 'gc-amp', [{ t: 0, v: 0.95 }, { t: 1, v: 0.05 }]);
  // invert (Flip mirrors the generator)
  for (const [shape, kind] of [['squiggle', 'pattern'], ['randsteps', 'pattern'], ['smoothrandom', 'pattern'], [null, 'random']]) {
    one('travel', { kind, shape: shape || undefined, rate: '1/16', d0: 0.4, invert: true, smooth: 0.3, seed: 6 }, 1, 1.5, 'gi-' + (shape || kind));
    one('tone', { kind, shape: shape || undefined, rate: '1/16', d0: 0.4, invert: true, smooth: 0.3, seed: 6 }, 1, 1.5, 'gi2-' + (shape || kind), [{ t: 0, v: 0.6 }, { t: 1, v: 0.6 }]);
  }
  // long and short notes, odd resolutions
  one('formant', { kind: 'pattern', shape: 'triangle', rate: '1/32T', lockToBeat: true }, 0.1, 16, 'g-long');
  one('vib', { kind: 'pattern', shape: 'square', rate: '1/4' }, 3.999, 0.01, 'g-short');
  // Re-roll
  const RR = (clip, cname, ids, lane) => {
    const out = M.reroll(clip, ids, lane), after = {};
    for (const n of out.notes) {
      if (ids && !ids.includes(n.id)) continue;
      const gens = {}; for (const L of M.LANE_IDS) if (n.gen[L]) gens[L] = n.gen[L].seed;
      after[n.id] = { gens, startSeed: n.sample.start.seed };
    }
    cases.push({ op: 'reroll', clip: cname, ids, lane, after });
  };
  RR(named.gens, 'gens', null, null); RR(named.gens, 'gens', null, 'amp'); RR(named.gens, 'gens', ['gn-2', 'gn-3'], null); RR(named.gens, 'gens', ['gn-1'], 'tone');
  RR(named['chop-src'], 'chop-src', null, null); RR(named['chop-src'], 'chop-src', null, 'pitch');
  // Turn into nodes
  const BK = (clip, cname, ids, lane, r) => {
    const out = M.bakeGen(clip, ids, lane, { res: r });
    for (const n of out.notes) if (ids.includes(n.id)) cases.push({ op: 'bakeGen', clip: cname, note: n.id, lane, res: r, nodes: C.nodes(n.c[lane]), genAfter: n.gen[lane] });
  };
  BK(named['chop-src'], 'chop-src', ['cs-a'], 'amp', 256); BK(named['chop-src'], 'chop-src', ['cs-b'], 'tone', 256); BK(named['chop-src'], 'chop-src', ['cs-d'], 'pitch', 64);
  BK(named.gens, 'gens', ['gn-1'], 'amp', 256); BK(named.gens, 'gens', ['gn-2'], 'formant', 32); BK(named.gens, 'gens', ['gn-4'], 'vib', 256); BK(named.gens, 'gens', ['gn-4'], 'travel', 8);
  BK(named.gens, 'gens', ['gn-5'], 'amp', 256);   // no generator: unchanged
  {   // the 1/16 Squiggle over 4 beats: more than 128 nodes at eps 0.004, so the eps is raised
    const n = M.mkNote(60, 0, 4, 'bake-sq'); n.gen.amp = M.makeGen('pattern', { shape: 'squiggle', rate: '1/16', d0: 0.6, d1: 0.6 });
    const clip = M.normalizeClip({ bars: 4, notes: [n] }), out = M.bakeGen(clip, null, 'amp', { res: 256 }).notes[0];
    const tab = M.laneTable(clip.notes[0], 'amp', 256), N = tab.length, ts = [], vs = [];
    for (let j = 0; j < N; j++) { ts.push(j / (N - 1)); vs.push(tab[j]); }
    const atEps = M.rdp(ts.map((t, j) => ({ t, v: vs[j] })), M.BAKE_EPS).length;
    cases.push({ op: 'bakeGen', clip: { bars: 4, notes: [C.note(clip.notes[0])] }, note: 'bake-sq', lane: 'amp', res: 256, nodes: C.nodes(out.c.amp), genAfter: out.gen.amp, nodesAtBaseEps: atEps });
  }
  return {
    tolerance: { text: 'tables within 2 Float32 ulps; steps (the seeded random value per step) exact; reroll seeds exact; baked nodes as nodes',
      tableUlps: 2, steps: 'exact', reroll: 'exact', nodes: { t: 1e-12, v: 1e-12, bend: 1e-12 } },
    notes: ['table = laneTable(note, lane, res): the drawing sampled to Float32, plus the generator, clamped, Float32 again',
      'steps[i] is the random value of step k = k0 + i (rand01(hash) * 2 - 1, plan 1.7); k0 is one step before the note\'s first, the list runs one step past its last',
      'the generator cases put the generator on one lane of a note at p 64 whose other lanes are flat; note.gen holds it as normalizeGen returns it',
      'pattern cases: 7 shapes x 5 rates with lockToBeat on (s cycling through 0, 0.37, 5.5), and with lockToBeat off x warp 0, 0.8, -0.5 (warp does nothing when locked, so the full product would repeat itself); random cases are 1-beat notes',
      'bakeGen: nodesAtBaseEps = how many points RDP keeps at BAKE_EPS 0.004 (more than 128 means the raised eps was used); genAfter is null after a bake',
      selfChecked + ' random-kind cases (smooth 0) were checked at generation time: genOffset equals the step recipe exactly'],
    clips: { gens: C.clip(named.gens), 'chop-src': C.clip(named['chop-src']) },
    cases,
  };
}

// =================================================================================================== tools.json
function tools(M, named) {
  const clips = { 'connect-grid': named['connect-grid'], 'voice-lead': named['voice-lead'], 'chop-src': named['chop-src'], 'humanized-chord': named['humanized-chord'],
    shapes4: named.shapes4, gens: named.gens, 'mixed-settings': named['mixed-settings'], empty: deepFreeze(M.newClip(2)) };
  const cases = [];
  const changed = (inp, out) => { const set = new Set(inp.notes); return out.notes.filter(n => !set.has(n)).map(C.note); };
  const res = (inp, out, extra) => Object.assign({ notes: changed(inp, out), order: out.notes.map(n => n.id) }, extra || {});
  const T = (op, cname, args, fn) => { const inp = clips[cname]; cases.push({ op, clip: cname, args, result: fn(inp) }); };
  const allBut = (cname, ex) => clips[cname].notes.map(n => n.id).filter(id => !ex.includes(id));
  // connect / disconnect
  const cgSel = allBut('connect-grid', ['cg-x']);
  for (const [mode, seed] of [['nearest', 1], ['furthest', 1], ['random', 1], ['random', 2], ['random', 3], ['random', 4]]) T('connect', 'connect-grid', { ids: cgSel, mode, seed }, c => res(c, M.connect(c, cgSel, mode, seed)));
  T('connect', 'connect-grid', { ids: null, mode: 'nearest', seed: 1 }, c => res(c, M.connect(c, null, 'nearest', 1)));
  T('connect', 'connect-grid', { ids: ['cg-11', 'cg-12', 'cg-21', 'cg-22', 'cg-23'], mode: 'nearest', seed: 1 }, c => res(c, M.connect(c, ['cg-11', 'cg-12', 'cg-21', 'cg-22', 'cg-23'], 'nearest', 1)));
  for (const [mode, seed] of [['nearest', 1], ['furthest', 1], ['random', 1], ['random', 2]]) T('connect', 'voice-lead', { ids: null, mode, seed }, c => res(c, M.connect(c, null, mode, seed)));
  T('connect', 'humanized-chord', { ids: null, mode: 'nearest', seed: 1 }, c => res(c, M.connect(c, null, 'nearest', 1)));
  T('connect', 'shapes4', { ids: null, mode: 'furthest', seed: 1 }, c => res(c, M.connect(c, null, 'furthest', 1)));
  T('disconnect', 'voice-lead', { ids: null }, c => res(c, M.disconnect(c, null)));
  T('disconnect', 'voice-lead', { ids: ['vl-d3'] }, c => res(c, M.disconnect(c, ['vl-d3'])));
  T('disconnect', 'voice-lead', { ids: ['vl-d3', 'vl-e4', 'vl-f1'] }, c => res(c, M.disconnect(c, ['vl-d3', 'vl-e4', 'vl-f1'])));
  T('disconnect', 'connect-grid', { ids: ['cg-x', 'cg-21'] }, c => res(c, M.disconnect(c, ['cg-x', 'cg-21'])));
  // chop
  const chopR = (c, ids, rate, ss, mx) => { const r = M.chop(c, ids, rate, ss, mx); return res(c, r.clip, { refused: r.refused, count: r.count }); };
  for (const rate of ['1/16', '1/8T', '1/4']) for (const ss of [0, 0.125, true]) T('chop', 'chop-src', { ids: null, rate, stepStart: ss, maxNotes: null }, c => chopR(c, null, rate, ss));
  T('chop', 'chop-src', { ids: null, rate: '1/16', stepStart: 0, maxNotes: 20 }, c => chopR(c, null, '1/16', 0, 20));
  T('chop', 'chop-src', { ids: null, rate: '1/16', stepStart: 0, maxNotes: 21 }, c => chopR(c, null, '1/16', 0, 21));
  T('chop', 'chop-src', { ids: ['cs-a'], rate: '1/8', stepStart: 0.3, maxNotes: null }, c => chopR(c, ['cs-a'], '1/8', 0.3));
  T('chop', 'voice-lead', { ids: ['vl-d3', 'vl-e4', 'vl-f1'], rate: '1/4', stepStart: 0, maxNotes: null }, c => chopR(c, ['vl-d3', 'vl-e4', 'vl-f1'], '1/4', 0));
  T('chop', 'gens', { ids: ['gn-2', 'gn-4'], rate: '1/2T', stepStart: true, maxNotes: null }, c => chopR(c, ['gn-2', 'gn-4'], '1/2T', true));
  T('chop', 'shapes4', { ids: null, rate: 0.3, stepStart: 0, maxNotes: null }, c => chopR(c, null, 0.3, 0));
  // walk
  T('walk', 'humanized-chord', { ids: null, field: 'start', from: 0, to: 0.9 }, c => res(c, M.walk(c, null, 'start', 0, 0.9)));
  T('walk', 'humanized-chord', { ids: null, field: 'vel', from: 0.2, to: 1 }, c => res(c, M.walk(c, null, 'vel', 0.2, 1)));
  T('walk', 'humanized-chord', { ids: null, field: 'tone', from: 1, to: 0.3 }, c => res(c, M.walk(c, null, 'tone', 1, 0.3)));
  T('walk', 'shapes4', { ids: ['s4-c'], field: 'start', from: 0.4, to: 0.8 }, c => res(c, M.walk(c, ['s4-c'], 'start', 0.4, 0.8)));
  T('walk', 'voice-lead', { ids: null, field: 'travel', from: 0.25, to: 1.2 }, c => res(c, M.walk(c, null, 'travel', 0.25, 1.2)));
  // humanize
  for (const seed of [1, 2, 3]) T('humanize', 'humanized-chord', { ids: null, amounts: { time: 0.05, vel: 0.2, lanes: 0.1 }, seed }, c => res(c, M.humanize(c, null, { time: 0.05, vel: 0.2, lanes: 0.1 }, seed)));
  T('humanize', 'chop-src', { ids: null, amounts: { lanes: { pitch: 0.02, travel: 0.1, refresh: 0.3 } }, seed: 1 }, c => res(c, M.humanize(c, null, { lanes: { pitch: 0.02, travel: 0.1, refresh: 0.3 } }, 1)));
  T('humanize', 'voice-lead', { ids: ['vl-b1', 'vl-a1'], amounts: { time: 2, vel: 1 }, seed: 9 }, c => res(c, M.humanize(c, ['vl-b1', 'vl-a1'], { time: 2, vel: 1 }, 9)));
  // strum / legato
  for (const dir of ['up', 'down']) for (const rate of ['1/32', '1/16T']) T('strum', 'humanized-chord', { ids: null, rate, dir }, c => res(c, M.strum(c, null, rate, dir)));
  T('strum', 'voice-lead', { ids: null, rate: '1/4', dir: 'up' }, c => res(c, M.strum(c, null, '1/4', 'up')));
  T('strum', 'connect-grid', { ids: null, rate: '1/64', dir: 'down' }, c => res(c, M.strum(c, null, '1/64', 'down')));
  for (const cname of ['humanized-chord', 'voice-lead', 'shapes4', 'connect-grid']) T('legato', cname, { ids: null }, c => res(c, M.legato(c, null)));
  // lane edits
  for (const lane of M.LANE_IDS) for (const f of [1.5, 0.5]) T('scaleLane', 'chop-src', { ids: null, lane, factor: f }, c => res(c, M.scaleLane(c, null, lane, f)));
  for (const f of [0, 3, 1.5]) T('scaleLane', 'mixed-settings', { ids: null, lane: 'travel', factor: f }, c => res(c, M.scaleLane(c, null, 'travel', f)));
  T('scaleLane', 'gens', { ids: null, lane: 'amp', factor: 4 }, c => res(c, M.scaleLane(c, null, 'amp', 4)));
  for (const [cname, lane] of [['chop-src', 'tone'], ['chop-src', 'travel'], ['chop-src', 'vib'], ['shapes4', 'pitch'], ['shapes4', 'travel'], ['voice-lead', 'pitch']])
    T('reverseLane', cname, { ids: null, lane }, c => res(c, M.reverseLane(c, null, lane)));
  for (const [cname, lane] of [['chop-src', 'amp'], ['chop-src', 'tone'], ['chop-src', 'travel'], ['gens', 'vib'], ['shapes4', 'amp']])
    T('flipLane', cname, { ids: null, lane }, c => res(c, M.flipLane(c, null, lane)));
  // selection
  const SS = (cname, refId, kind, ids) => T('selectSimilar', cname, { refId, kind, ids }, c => ({ ids: M.selectSimilar(c, refId, kind, ids) }));
  SS('voice-lead', 'vl-a3', 'pitch', null); SS('voice-lead', 'vl-b1', 'length', null); SS('voice-lead', 'vl-d1', 'bar', null); SS('humanized-chord', 'hc-1', 'length', null);
  SS('voice-lead', 'vl-a2', 'everyOther', null); SS('voice-lead', 'vl-c1', 'everyOther', ['vl-a1', 'vl-c1', 'vl-d1', 'vl-e1', 'vl-f1']); SS('voice-lead', 'zzz', 'everyOther', null);
  SS('voice-lead', null, 'invert', ['vl-a1', 'vl-b2']); SS('voice-lead', null, 'invert', null); SS('humanized-chord', 'hc-11', 'bar', null); SS('connect-grid', 'nope', 'pitch', null);
  // note settings
  const NS = (cname, ids, patch) => T('setNoteSample', cname, { ids, patch }, c => res(c, M.setNoteSample(c, ids, patch)));
  NS('shapes4', null, { engine: 'cloud', lock: true }); NS('mixed-settings', null, { motion: null }); NS('shapes4', ['s4-a'], { start: { mode: 'random', seed: 5 } });
  NS('mixed-settings', ['ms-1', 'ms-2'], { jump: '1/16', travel: 'path', restart: true }); NS('shapes4', null, { vel: 0.5, glide: 0.25, gc: -0.3 });
  NS('mixed-settings', null, { engine: 'mixed', motion: 'bounce', start: { mode: 'mixed', pos: 0.75, range: 'mixed' } });
  NS('mixed-settings', null, { engine: 'grain', start: { pos: 2, range: -1, seed: 3.7 }, vel: 4, glide: -2, gc: 9, tape: 'yes', jump: '1/7' });
  NS('mixed-settings', ['ms-3'], { glide: null, tape: null, rev: null, lock: null, engine: null, jump: null });
  const GM = (cname, ids) => T('getMixed', cname, { ids }, c => ({ mixed: M.getMixed(c, ids) }));
  GM('mixed-settings', null); GM('mixed-settings', ['ms-5', 'ms-7']); GM('mixed-settings', ['ms-7']); GM('shapes4', null); GM('shapes4', []); GM('chop-src', ['cs-c', 'cs-a']);
  // nodes, add, remove, update, duplicate
  T('setLaneNodes', 'shapes4', { id: 's4-b', lane: 'tone', nodes: [{ t: 0.8, v: 0.2 }, { t: 0.1, v: NaN }, { t: 0.5, v: 0.9, shape: 'step' }] },
    c => res(c, M.setLaneNodes(c, 's4-b', 'tone', [{ t: 0.8, v: 0.2 }, { t: 0.1, v: NaN }, { t: 0.5, v: 0.9, shape: 'step' }])));
  T('setLaneNodes', 'shapes4', { id: 'none', lane: 'amp', nodes: [] }, c => res(c, M.setLaneNodes(c, 'none', 'amp', [])));
  T('setLaneNodes', 'shapes4', { id: 's4-d', lane: 'refresh', nodes: [] }, c => res(c, M.setLaneNodes(c, 's4-d', 'refresh', [])));
  T('setLaneNodesMany', 'shapes4', { lane: 'heads', map: { 's4-a': [{ t: 0, v: 0.6 }, { t: 1, v: 0.9 }], 's4-c': [{ t: 0.5, v: 0.1, lin: true }] } },
    c => res(c, M.setLaneNodesMany(c, 'heads', { 's4-a': [{ t: 0, v: 0.6 }, { t: 1, v: 0.9 }], 's4-c': [{ t: 0.5, v: 0.1, lin: true }] })));
  const DU = (cname, ids, off) => T('duplicateNotes', cname, { ids, offset: off }, c => { const r = M.duplicateNotes(c, ids, off); return res(c, r.clip, { ids: r.ids }); });
  DU('voice-lead', ['vl-f1', 'vl-f2'], null); DU('voice-lead', ['vl-d3', 'vl-e4', 'vl-b4'], 4); DU('shapes4', null, -1); DU('chop-src', ['cs-a'], 0.5); DU('shapes4', [], null);
  const AD = (cname, p, s, l, patch) => T('addNote', cname, { p, s, l, patch }, c => { const r = M.addNote(c, p, s, l, patch); return res(c, r.clip, { ids: [r.id] }); });
  AD('shapes4', 61.5, 0.375, 0.25, null); AD('empty', 60, 0, 1, { engine: 'tape', start: { mode: 'song' }, vel: 0.3 }); AD('voice-lead', 48, 12.5, 4, { motion: 'scatter', jump: '1/8T' });
  T('removeNotes', 'voice-lead', { ids: ['vl-e4'] }, c => res(c, M.removeNotes(c, ['vl-e4'])));
  T('removeNotes', 'connect-grid', { ids: ['cg-21', 'cg-01', 'nope'] }, c => res(c, M.removeNotes(c, ['cg-21', 'cg-01', 'nope'])));
  T('updateNotes', 'shapes4', { ids: ['s4-a'], patch: { p: 200, s: -1, l: 0, vel: 2 } }, c => res(c, M.updateNotes(c, ['s4-a'], { p: 200, s: -1, l: 0, vel: 2 })));
  T('updateNotes', 'shapes4', { ids: null, patch: { glideTo: 's4-d', glide: 0.125, gc: 0.5, s: 2 } }, c => res(c, M.updateNotes(c, null, { glideTo: 's4-d', glide: 0.125, gc: 0.5, s: 2 })));
  T('setGen', 'shapes4', { ids: ['s4-b'], lane: 'refresh', gen: { kind: 'random', seed: 11, rate: '1/32' } }, c => res(c, M.setGen(c, ['s4-b'], 'refresh', { kind: 'random', seed: 11, rate: '1/32' })));
  T('clearGen', 'gens', { ids: null, lane: null }, c => res(c, M.clearGen(c, null, null)));
  T('clearGen', 'gens', { ids: ['gn-1'], lane: 'tone' }, c => res(c, M.clearGen(c, ['gn-1'], 'tone')));
  const out = {};
  for (const k of Object.keys(clips)) out[k] = C.clip(clips[k]);
  return {
    tolerance: { text: 'ids, order, links, flags, integers exact; p, s, l exact; node values within 1e-12', exact: ['ids', 'order', 'glideTo', 'flags', 'integers', 'p', 's', 'l'],
      nodes: { t: 1e-12, v: 1e-12, bend: 1e-12 } },
    notes: ['result.notes = every note of the result clip that is a new object (copy-on-write: the C++ must share every other note with the input), in clip order',
      'result.order = every id of the result clip in order; args.ids null = every note; chop maxNotes null = the default 1024',
      'chop with a numeric rate (0.3) reads it as beats (rateBeats passes a positive number through)', 'setGen/clearGen added: the generator edits behind Pattern, Random and Reset'],
    clips: out,
    cases,
  };
}

// =================================================================================================== glides.json
function glides(M, named) {
  const cases = [];
  const list = M.CLIP_PRESETS.map(p => ['preset:' + p.id, M.loadClipPreset(p.id)]).concat([['voice-lead', named['voice-lead']]]);
  for (const [name, clip] of list) for (const glideMode of ['off', 'legato', 'always']) for (const glideTime of [0.5, 0.25]) {
    const inst = { glideMode, glideTime }, G = M.computeGlides(clip, inst);
    cases.push({ clip: name, inst, glides: [...G].map(([sid, g]) => [sid, g.d, g.len, g.curve, g.toId, g.linked]), chains: M.buildChains(clip, G) });
  }
  return {
    tolerance: { text: 'ids and order exact; d, len within 1e-12', exact: ['ids', 'order', 'curve', 'linked'], d: 1e-12, len: 1e-12 },
    notes: ['glides are in computeGlides\' Map order (explicit links first, then the auto glides cluster by cluster): buildChains breaks ties by it',
      'inst is the partial inst computeGlides normalizes; "preset:<id>" names SLModel.loadClipPreset(id), checked node for node in model/presets.json'],
    clips: { 'voice-lead': C.clip(named['voice-lead']) },
    cases,
  };
}

// =================================================================================================== snapshots.json
function snapshotOut(snap) {
  return {
    version: snap.version, bars: snap.bars, beatsPerBar: snap.beatsPerBar, res: snap.res,
    notes: snap.notes.map(n => {
      const lanes = {}; for (const id of Object.keys(n.lanes)) lanes[id] = C.table(n.lanes[id]);
      return { id: n.id, idHash: n.idHash, p: n.p, s: n.s, l: n.l, vel: n.vel, engine: n.engine, lock: n.lock, motion: n.motion, travel: n.travel,
        start: n.start, tape: n.tape, rev: n.rev, jump: n.jump, restart: n.restart, glide: n.glide, chain: n.chain, lanes };
    }),
  };
}
function snapshots(M, named) {
  const cases = [], clips = {};
  const INIT = M.applySoundPreset(M.defaultInst(), 'init').inst;
  const add = (cname, clip, inst, res) => cases.push({ clip: cname, inst, res, snapshot: snapshotOut(M.compileSnapshot(clip, inst, { res })) });
  const demo = M.loadClipPreset('strand-demo');
  add('preset:strand-demo', demo, INIT, 256);
  for (const p of M.CLIP_PRESETS) if (p.id !== 'strand-demo') add('preset:' + p.id, M.loadClipPreset(p.id), INIT, 64);
  for (const gm of ['off', 'always']) add('preset:strand-demo', demo, M.normalizeInst(M.mergeInst(INIT, { glideMode: gm })), 32);
  for (const R of M.RECIPES) {
    const r = M.applyRecipe(demo, INIT, R.n, null), viaRef = C.clipRef('preset:strand-demo', {}, R.n);
    if (C.S(C.clip(viaRef)) !== C.S(C.clip(r.clip))) throw new Error('snapshots: the recipe clip depends on the inst');
    cases.push({ clip: 'preset:strand-demo', recipe: R.n, inst: r.inst, res: 32, snapshot: snapshotOut(M.compileSnapshot(r.clip, r.inst, { res: 32 })) });
  }
  add('mixed-settings', named['mixed-settings'], INIT, 64);
  add('mixed-settings', named['mixed-settings'], M.normalizeInst(M.mergeInst(INIT, { engine: 'cloud', lock: true, motion: 'scatter', tape: true, rev: true, jump: '1/4', glideMode: 'always', glideTime: 3 })), 32);
  clips['mixed-settings'] = C.clip(named['mixed-settings']);
  return {
    tolerance: { text: 'C++ compiles the same clip, resolves with the same inst (core::resolve) and compares field by field exact; tables within 2 Float32 ulps; table lengths exact',
      default: 'exact', tableUlps: 2, tableLength: 'exact' },
    notes: ['inst is the full instrument the snapshot was compiled with (the JS snapshot bakes its defaults; the C++ resolves its follow markers with it)',
      'lanes are Tables: {n, flat} when every binary32 value is the same (the C++ stores 2 values; compare n with the JS length)',
      'a case with recipe: N compiles applyRecipe(that clip, inst, N, every note).clip (the clip half of a recipe does not depend on the inst; model/presets.json checks the step); its inst is the recipe\'s inst'],
    clips,
    cases,
  };
}

// =================================================================================================== presets.json
function presets(M, named) {
  const cases = [];
  for (const p of M.CLIP_PRESETS) cases.push({ kind: 'clipPreset', id: p.id, name: p.name, clip: C.clip(M.loadClipPreset(p.id)) });
  const WEIRD = M.normalizeInst({ engine: 'cloud', lock: true, motion: 'scatter', speed: 3, heads: 5, spread: 0.9, grainVoice: 7, grainCloud: 500, grainLock: 9,
    refresh: { mode: 'free', ms: 300, rate: '1/8', lockToBeat: false, skip: 0.4 }, spray: 0.6, pitch: 7, tape: true, rev: true, drift: 0.5, headTune: 'fifths',
    region: { start: 0.2, end: 0.7, atEnd: 'hold', xfadeMs: 50 }, jump: '1/4T', macros: [0.1, 0.9, 0.3, 0.7, 0.2, 0.8, 0.4, 0.6], offsets: { amp: 0.2, vib: -0.1, tone: 0.3, formant: -0.4, hollow: 0.5 },
    glideMode: 'always', glideTime: 2, vibRate: 7, root: 48, cents: -20, key: 5, scale: 'Dorian', scaleLock: true, velSens: 0.3, polyphony: 8, outGain: -6, bendRange: 12,
    mod1: { target: 'heads', cc: 11 }, mod2: { target: 'off', cc: 74 }, liveLegato: true, seed: 42 });
  const DEF = M.defaultInst();
  const INIT = M.applySoundPreset(DEF, 'init').inst, demo = M.loadClipPreset('strand-demo'), ms = named['mixed-settings'];
  const insts = { default: DEF, weird: WEIRD, init: INIT }, byName = { default: DEF, weird: WEIRD, init: INIT };
  for (const from of ['default', 'weird']) for (const p of M.SOUND_PRESETS) cases.push({ kind: 'soundPreset', id: p.id, from, result: M.applySoundPreset(byName[from], p.id) });
  cases.push({ kind: 'soundPreset', id: 'Vowel voice', from: 'default', result: M.applySoundPreset(DEF, 'Vowel voice') });
  const changed = (inp, out) => { const set = new Set(inp.notes); return out.notes.filter(n => !set.has(n)).map(C.note); };
  for (const R of M.RECIPES) {
    const a = M.applyRecipe(demo, INIT, R.n, null);
    cases.push({ kind: 'recipe', n: R.n, clip: 'preset:strand-demo', inst: 'init', ids: null, result: { inst: a.inst, notes: changed(demo, a.clip), order: a.clip.notes.map(n => n.id) } });
    const ids = ['ms-1', 'ms-4', 'ms-6'], b = M.applyRecipe(ms, WEIRD, R.n, ids);
    cases.push({ kind: 'recipe', n: R.n, clip: 'mixed-settings', inst: 'weird', ids, result: { inst: b.inst, notes: changed(ms, b.clip), order: b.clip.notes.map(n => n.id) } });
  }
  const PINS = [[], ['engine', 'grain'], { motion: true, spray: true, heads: false }];
  for (let seed = 1; seed <= 20; seed++) for (const pins of PINS) {
    const inst = seed % 2 ? 'default' : 'weird';
    cases.push({ kind: 'dice', inst, seed, pins, result: M.dice(byName[inst], seed, pins) });
  }
  return {
    tolerance: { text: 'exact (node values within 1e-12)', default: 'exact', nodes: { t: 1e-12, v: 1e-12, bend: 1e-12 } },
    notes: ['clipPreset.clip = loadClipPreset(id); every other file that names "preset:<id>" means this clip',
      'recipe: result.notes = the notes applyRecipe made new (copy-on-write), result.order = every id; ids null = every note',
      'dice pins are given as the prototype accepts them: an array of field names, or an object of field: true/false',
      'soundPreset.from, recipe.inst and dice.inst name an entry of insts (default = defaultInst(), init = the init sound preset\'s inst, weird = every field moved off its default)'],
    clips: { 'mixed-settings': C.clip(ms) },
    insts,
    cases,
  };
}

// =================================================================================================== format.json
function format(M) {
  const cases = [];
  const vals = Array.from({ length: 41 }, (_, i) => i / 40);
  const F = (lane, v, ctx) => cases.push({ lane, v, ctx, value: M.mapLane(lane, v, ctx), text: M.formatLane(lane, v, ctx) });
  const base = { inst: {}, bpm: 120, travelKind: 'speed' };
  for (const lane of ['pitch', 'amp', 'vib', 'tone', 'formant', 'hollow']) { for (const v of vals) F(lane, v, base); F(lane, NaN, base); }
  const travelExtra = [M.V_FWD_MIN, M.V_BACK_MAX, Math.fround(0.48), Math.fround(0.52), 0.52 + M.F32_EPS, 0.48 - M.F32_EPS, M.travelSpeedToV(0.125), M.travelSpeedToV(-0.125),
    C.f32Step(M.V_FWD_MIN, -1), C.f32Step(M.V_BACK_MAX, 1), M.travelSpeedToV(0.13), 0.999, NaN];
  for (const kind of ['speed', 'path']) for (const v of vals.concat(travelExtra)) F('travel', v, { inst: {}, bpm: 120, travelKind: kind });
  for (const heads of [1, 3, 7.5]) for (const v of vals) F('heads', v, { inst: { heads }, bpm: 120, travelKind: 'speed' });
  for (const eng of [{ engine: 'voice' }, { engine: 'cloud' }, { engine: 'cloud', lock: true }, { engine: 'tape' }, { engine: 'voice', grainVoice: 0.25 }, { engine: 'cloud', grainCloud: 2000 }])
    for (const v of vals) F('grain', v, { inst: eng, bpm: 120, travelKind: 'speed' });
  const refreshExtra = [0.06, Math.fround(0.06), 0.06 - M.F32_EPS, C.f32Step(Math.fround(0.06), -1), C.f32Step(Math.fround(0.06), 1), 0.0601, NaN];
  for (const refresh of [{ mode: 'smooth' }, { mode: 'free', ms: 120 }, { mode: 'free', ms: 7 }, { mode: 'sync', rate: '1/16' }, { mode: 'sync', rate: '1/8T' }])
    for (const bpm of [120, 87]) for (const v of vals.concat(refreshExtra)) F('refresh', v, { inst: { refresh }, bpm, travelKind: 'speed' });
  return {
    tolerance: { text: 'text exact; value within 1e-12 relative', strings: 'exact', valueRelative: 1e-12 },
    notes: ['ctx.inst is a partial inst: mapLane / formatLane normalize it themselves (normalizeInst), as the prototype does', 'v "NaN" reads as the lane default'],
    cases,
  };
}

// =================================================================================================== normalize.json
function normalize(M) {
  const cases = [];
  const CL = (input, label) => cases.push({ kind: 'clip', label, input, result: C.clip(M.normalizeClip(input)) });
  const IN = (input, label) => cases.push({ kind: 'inst', label, input, result: M.normalizeInst(input) });
  CL([{ id: 'v3a', p: 60, s: 0, l: 2, glide: null, gc: 0, glideTo: 'v3b', c: { pitch: [{ t: 0, v: 0.5, lin: true }, { t: 1, v: 0.6 }], tone: [{ t: 0, v: 0.75 }, { t: 1, v: 0.75 }] } },
    { id: 'v3b', p: 67, s: 2, l: 2, glide: 0.5, gc: -0.4, glideTo: null, c: { amp: [{ t: 0, v: 0.2, lin: false }, { t: 0.5, v: 0.9, lin: true }, { t: 1, v: 0.3 }] } }], 'v3 notes array');
  CL({ bars: 8, notes: [{ p: 60, s: 0, l: 1 }, { p: 60, s: 0, l: 1 }, { p: 62.5, s: 1.25, l: 0.333 }] }, 'missing ids (made from a hash of index and position)');
  CL({ bars: 4, notes: [{ id: 7, p: 60, s: 0, l: 1, glideTo: 8 }, { id: 8, p: 62, s: 1, l: 1 }, { id: 'a', p: 64, s: 2, l: 1 }, { id: 'a', p: 65, s: 3, l: 1 }, { id: 'a', p: 66, s: 4, l: 1 }, { id: 'a~1', p: 67, s: 5, l: 1 }] }, 'numeric and duplicate ids');
  CL({ bars: 4, notes: [{ id: 'x', p: 60, s: 0, l: 1, glideTo: 'zzz' }, { id: 'y', p: 62, s: 1, l: 1, glideTo: 'y' }, { id: 'z', p: 64, s: 2, l: 1, glideTo: '' }] }, 'dangling and self links');
  CL({ bars: 2, notes: [{ id: 'n', p: 60, s: 0, l: 1, c: { pitch: [{ t: NaN, v: 0.6 }, { t: 0.5, v: NaN }], amp: [], vib: [{ t: 0.4, v: 0.3 }], tone: [{ t: 0.9, v: 0.1 }, { t: 0.1, v: 0.8 }], travel: 'fast', heads: [{ t: 0, v: 9 }, { t: 1, v: -2 }] } }] }, 'NaN, empty, single, unsorted and out-of-range lanes');
  CL({ bars: 4, notes: [{ id: 'g', p: 300, s: -4, l: 0, vel: 'loud', gc: 5, glide: -1,
    sample: { motion: 'spin', travel: 'path', start: { mode: 'x', pos: 3, range: -1, seed: 2.7 }, tape: 'yes', rev: 1, jump: '1/7', engine: 'grain', lock: null, restart: '1' },
    gen: { amp: { kind: 'random', shape: 'zig', rate: 'x', d0: 5, warp: -3, amount: 'a', seed: 3.9, key: '' }, tone: 'notagen', pitch: null, refresh: { d0: 0.2, invert: 1, lockToBeat: 0 } } }] }, 'garbage note fields');
  CL({ bars: 0, notes: [null, { id: 'q' }] }, 'bars 0, a null note');
  CL({ bars: 100, notes: 'none' }, 'bars 100, notes not an array');
  CL({ bars: '4', notes: [] }, 'bars as a string');
  CL(null, 'null'); CL(42, 'a number');
  CL({ bars: 3.6, notes: [{ id: 'big', p: 59.7, s: 1e9, l: 1e-9, vel: -1 }] }, 'extreme numbers');
  IN({ engine: 'formant', lock: 'yes', motion: 'spin', speed: 99, heads: 0, refresh: 'fast', region: { start: 0.9, end: 0.1 }, macros: [2, -1], offsets: [], glideMode: 'auto',
    root: 200.7, key: 13.6, scale: 'Lydian', polyphony: 40.4, mod1: { target: 'pitch', cc: 300 }, mod2: { target: 'bogus' }, seed: 3.9e9, outGain: -100, bendRange: 49, jump: '1/7' }, 'garbage inst');
  IN(null, 'null'); IN({}, 'empty');
  IN({ speed: NaN, heads: Infinity, spread: -Infinity, refresh: { mode: 'sync', ms: NaN, rate: '1/32T', lockToBeat: 0, skip: 2 }, region: { start: NaN, end: 0.5, atEnd: 'stop', xfadeMs: 9999 }, macros: 'x', offsets: { amp: 5, vib: NaN }, cents: 1e9, key: -3, polyphony: 0.4, seed: -2.5 }, 'non-finite values');
  IN({ engine: 'tape', macros: [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9], mod1: 'vib', mod2: { target: 'off', cc: 64.6 }, glideTime: 20, vibRate: 0, grainLock: 20, grainCloud: 1, grainVoice: 0, liveLegato: 'no', scaleLock: 1 }, 'edges and wrong types');
  IN({ seed: 2147483648, root: -5, velSens: 2, pitch: -49, drift: 0.5, headTune: 'thirds', tape: 0, rev: 'false', region: 'all' }, 'seed past int32 and clamps');
  return {
    tolerance: { text: 'exact', default: 'exact' },
    notes: ['input is the raw JSON the loader gets ("NaN" stands for a NaN number)',
      'the prototype saves (v3 and v4) and what the M8 import makes of them are in state/import.json (tools/fixtures/state.js)'],
    cases,
  };
}
