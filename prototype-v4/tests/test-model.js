// tests/test-model.js - SLModel checks (node tests/test-model.js). One line per check; exits 1 on any FAIL.
// Every check prints the measured value against its limit; distributions are printed as min / median / max.
'use strict';
const path = require('path');
const M = require(path.join(__dirname, '..', 'src', 'sl-model.js'));

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
const semis = v => (v - 0.5) * 24;

// =========================================================================== seeds
section('1.6 seeds');
{
  // reference MurmurHash3 x86_32 over bytes (little-endian), to confirm hash32 is murmur3 over 4-byte ints
  function murmurBytes(bytes, seed) {
    let h = seed | 0; const n = bytes.length, nb = n >> 2;
    const mixk = k => { k = Math.imul(k, 0xcc9e2d51); k = (k << 15) | (k >>> 17); return Math.imul(k, 0x1b873593); };
    for (let i = 0; i < nb; i++) {
      const k = bytes[4 * i] | (bytes[4 * i + 1] << 8) | (bytes[4 * i + 2] << 16) | (bytes[4 * i + 3] << 24);
      h ^= mixk(k); h = (h << 13) | (h >>> 19); h = (Math.imul(h, 5) + 0xe6546b64) | 0;
    }
    let k = 0; const tail = n & 3, i0 = nb * 4;
    if (tail === 3) k ^= bytes[i0 + 2] << 16;
    if (tail >= 2) k ^= bytes[i0 + 1] << 8;
    if (tail >= 1) { k ^= bytes[i0]; h ^= mixk(k); }
    h ^= n; h ^= h >>> 16; h = Math.imul(h, 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35); h ^= h >>> 16;
    return h >>> 0;
  }
  const str = s => Array.from(Buffer.from(s, 'utf8'));
  const v1 = murmurBytes([], 1), v2 = murmurBytes(str('test'), 0);
  check('murmur3 reference vectors', v1 === 0x514e28b7 && v2 === 0xba6bd213, `('',1)=0x${v1.toString(16)} vs 0x514e28b7, ('test',0)=0x${v2.toString(16)} vs 0xba6bd213`);
  const R = rng(5); let mism = 0;
  for (let i = 0; i < 2000; i++) {
    const n = 1 + Math.floor(R() * 5), ints = Array.from({ length: n }, () => (Math.floor(R() * 4294967296) | 0));
    const bytes = []; for (const x of ints) bytes.push(x & 255, (x >>> 8) & 255, (x >>> 16) & 255, (x >>> 24) & 255);
    if (M.hash32(...ints) !== murmurBytes(bytes, 0x9747b28c)) mism++;
  }
  check('hash32 = murmur3_32(ints as LE bytes, seed 0x9747b28c)', mism === 0, `${mism} mismatches in 2000 random 1-5 int keys`);
  const f0 = M.idHash(''), fa = M.idHash('a'), ff = M.idHash('foobar');
  check('idHash = FNV-1a 32', f0 === 0x811c9dc5 && fa === 0xe40c292c && ff === 0xbf9cf968, `''=0x${f0.toString(16)} 'a'=0x${fa.toString(16)} 'foobar'=0x${ff.toString(16)}`);
  const B = new Array(10).fill(0), K = 50000;
  for (let i = 0; i < K; i++) B[Math.floor(M.rand01(M.hash32(i, 7)) * 10)]++;
  const chi = B.reduce((a, b) => a + (b - K / 10) ** 2 / (K / 10), 0);
  check('rand01 uniform (10 buckets)', chi < 27.9, `chi2 ${fx(chi, 2)} vs < 27.9 (df 9, p=0.001); buckets ${B.join(',')}`);
}

// =========================================================================== instrument
section('1.4 instrument defaults');
{
  const want = { engine: 'voice', lock: false, motion: 'forward', speed: 1, heads: 1, spread: 0.25, grainVoice: 2, grainCloud: 140, grainLock: 4,
    refresh: { mode: 'smooth', ms: 120, rate: '1/16', lockToBeat: true, skip: 0 }, spray: 0, pitch: 0, tape: false, rev: false, drift: 0, headTune: 'same',
    region: { start: 0, end: 1, atEnd: 'loop', xfadeMs: 10 }, jump: '1/8', macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5],
    offsets: { amp: 0, vib: 0, tone: 0, formant: 0, hollow: 0 }, glideMode: 'legato', glideTime: 0.5, vibRate: 5.5, root: 60, cents: 0, key: 0, scale: 'Chromatic', scaleLock: false,
    velSens: 0.7, polyphony: 16, outGain: 0, bendRange: 2, mod1: { target: 'vib', cc: 1 }, mod2: { target: 'tone', cc: 2 }, liveLegato: false, seed: 1 };
  check('defaultInst() = BLUEPRINT 1.4', J(M.defaultInst()) === J(want), J(M.defaultInst()) === J(want) ? `${Object.keys(want).length} fields` : J(M.defaultInst()));
  check('normalizeInst keeps a valid inst unchanged and repairs a broken one', J(M.normalizeInst(M.defaultInst())) === J(want) && M.normalizeInst({ refresh: null, heads: 99, engine: 'x', mod1: { target: 'refresh' } }).heads === 8 && M.normalizeInst({ refresh: null }).refresh.mode === 'smooth' && M.normalizeInst({ mod1: { target: 'refresh' } }).mod1.target === 'refresh',
    `heads 99 -> ${M.normalizeInst({ heads: 99 }).heads}, engine 'x' -> ${M.normalizeInst({ engine: 'x' }).engine}, refresh null -> ${M.normalizeInst({ refresh: null }).refresh.mode}`);
}

// =========================================================================== lanes
section('1.2 lanes');
{
  const ids = M.LANES.map(L => L.id).join(',');
  check('lane registry order', ids === 'pitch,amp,vib,tone,formant,hollow,travel,heads,grain,refresh', ids);
  const keys = M.LANES.map(L => L.key).join('');
  check('mode keys', keys === '2345678888', keys);
  const defs = M.LANES.map(L => L.def).join(',');
  check('lane defaults (Tone 1.0 = open)', defs === '0.5,0.8,0,1,0.5,0,0.75,0.5,0.5,0.5', defs);
  const col = M.LANES.map(L => L.color).join(',');
  check('lane colours (vib moved, Sample sand)', col === '#2ECF98,#4F8BFF,#E05CFF,#FF9F1C,#FF4D8D,#22C3E6,#E2C275,#E2C275,#E2C275,#E2C275', col);
  check('pitch dark/light line colours', M.LANE.pitch.colorDark === '#3EE6A8' && M.LANE.pitch.colorLight === '#0B9E74', M.LANE.pitch.colorDark + ' ' + M.LANE.pitch.colorLight);
  const grp = M.LANES.map(L => L.group).join(',');
  check('lane groups', grp === 'pitch,level,pitch,tone,tone,tone,sample,sample,sample,sample', grp);
  check('MIDI 60 = C3 (Cubase)', M.noteName(60) === 'C3' && M.noteName(69) === 'A3' && M.noteName(48) === 'C2' && M.noteName(61) === 'C#3', [60, 69, 48, 61].map(M.noteName).join(' '));

  const inst = M.defaultInst();
  const cases = [
    ['pitch', 0, -12], ['pitch', 0.5, 0], ['pitch', 1, 12], ['pitch', 0.625, 3],
    ['amp', 0.8, 0.8 * 0.8 * 1.3], ['amp', 1, 1.3], ['amp', 0, 0],
    ['vib', 0.5, 0.5], ['vib', 1, 1],
    ['tone', 1, 60 * Math.pow(2, 8.3)], ['tone', 0, 60], ['tone', 0.5, 60 * Math.pow(2, 4.15)],
    ['formant', 0, -12], ['formant', 0.75, 6],
    ['hollow', 1, -28], ['hollow', 0.5, -14],
    ['heads', 0.5, 1], ['heads', 1, 8], ['heads', 0.7, 1 + 0.2 * 14], ['heads', 0, 1],
    ['grain', 0.5, 2], ['grain', 1, 8], ['grain', 0, 0.5],
    ['refresh', 0.5, 0], ['refresh', 0.03, 0],
  ];
  let worst = 0, bad = [];
  for (const [id, v, want] of cases) { const got = M.mapLane(id, v, { inst }); const e = Math.abs(got - want); worst = Math.max(worst, e); if (e > 1e-9) bad.push(`${id}(${v})=${got} vs ${want}`); }
  check('mapLane table 1.2 (default inst)', !bad.length, `${cases.length} points, max error ${fx(worst, 12)}${bad.length ? '; ' + bad.join('; ') : ''}`);
  check('Tone default open (> 15 kHz)', M.mapLane('tone', M.LANE.tone.def) > 15000, fx(M.mapLane('tone', 1), 1) + ' Hz');
  const cloud = M.mergeInst(inst, { engine: 'cloud' }), lock = M.mergeInst(inst, { engine: 'cloud', lock: true }), tape = M.mergeInst(inst, { engine: 'tape' });
  const gv = [M.mapLane('grain', 0.5, { inst: cloud }), M.mapLane('grain', 0.75, { inst: cloud }), M.mapLane('grain', 0.5, { inst: lock }), M.mapLane('grain', 0.5, { inst: tape })];
  check('grain unit follows engine', Math.abs(gv[0] - 140) < 1e-9 && Math.abs(gv[1] - 280) < 1e-9 && Math.abs(gv[2] - 4) < 1e-9 && Math.abs(gv[3] - 1) < 1e-9, `cloud ${gv[0]} ms, cloud@0.75 ${gv[1]} ms, lock ${gv[2]} wavelengths, tape x${gv[3]}`);
  const free = M.mergeInst(inst, { refresh: { mode: 'free', ms: 120 } }), sync = M.mergeInst(inst, { refresh: { mode: 'sync', rate: '1/16' } });
  const rv = [M.mapLane('refresh', 0.5, { inst: free }), M.mapLane('refresh', 1, { inst: free }), M.mapLane('refresh', 0, { inst: free }), M.mapLane('refresh', 0.5, { inst: sync, bpm: 120 }), M.mapLane('refresh', 0.5 + 1 / 6, { inst: sync, bpm: 120 })];
  check('refresh lane: period / 2^((v-0.5)*6), v<0.06 = Smooth', Math.abs(rv[0] - 120) < 1e-9 && Math.abs(rv[1] - 15) < 1e-9 && rv[2] === 0 && Math.abs(rv[3] - 125) < 1e-9 && Math.abs(rv[4] - 62.5) < 1e-5,   // (integrator) the readout reads the Float32 table value (as the engine), so 0.5+1/6 is 62.5 within 1e-5
    `free@0.5 ${rv[0]} ms, free@1 ${rv[1]} ms, @0 ${rv[2]} (Smooth), sync 1/16@120bpm ${rv[3]} ms, @0.667 ${fx(rv[4], 3)} ms`);
  const fmt = [
    [M.formatLane('pitch', 0.625), '+3 st'],
    [M.formatLane('travel', M.travelSpeedToV(-0.5)), '0.5x backwards'],
    [M.formatLane('tone', Math.log2(2400 / 60) / 8.3), '2.4 kHz'],
    [M.formatLane('amp', Math.sqrt(Math.pow(10, -6 / 20) / 1.3)), '-6.0 dB'],
    [M.formatLane('refresh', 0.5, { inst: sync, bpm: 120 }), 'Refresh 1/16 (125 ms at 120 bpm)'],
    [M.formatLane('travel', 0.5), 'stopped'],
    [M.formatLane('travel', 0.75), '1x forwards'],
    [M.formatLane('refresh', 0.5), 'Smooth'],
  ];
  const fbad = fmt.filter(([a, b]) => a !== b);
  check('formatLane readouts', !fbad.length, fmt.map(([a, b]) => `'${a}'` + (a === b ? '' : ` (want '${b}')`)).join(', '));
}

// =========================================================================== travel
section('1.3 travel speed mapping');
{
  const pts = [[0.02, -8], [0.25, -1], [0.48, 0], [0.5, 0], [0.52, 0], [0.75, 1], [0.98, 8], [0, -8], [1, 8], [0.4799999, -0.125], [0.5200001, 0.125]];
  let worst = 0; const rows = [];
  for (const [v, want] of pts) { const got = M.travelSpeed(v); const e = Math.abs(got - want) / Math.max(1, Math.abs(want)); worst = Math.max(worst, e); rows.push(`${v}->${fx(got, 4)}`); }
  check('travel speed s(v) at 0.02/0.25/0.48/0.5/0.52/0.75/0.98 (+ clamps, band edges)', worst < 1e-4, `max rel error ${fx(worst, 8)}; ` + rows.join(' '));
  let nonMono = 0, prev = -Infinity;
  for (let i = 0; i <= 10000; i++) { const s = M.travelSpeed(i / 10000); if (s < prev - 1e-12) nonMono++; prev = s; }
  check('s(v) non-decreasing over 0..1', nonMono === 0, `${nonMono} decreasing steps in 10001 samples`);
  const errs = [];
  for (let i = 0; i <= 200; i++) { const x = Math.pow(2, -3 + 6 * i / 200); for (const sg of [1, -1]) errs.push(Math.abs(M.travelSpeed(M.travelSpeedToV(sg * x)) - sg * x) / x); }
  const inner = errs.filter((_, i) => i > 1 && i < errs.length - 2);
  check('travelSpeedToV inverts s(v) for |x| in (1/8, 8)', Math.max(...inner) < 1e-9, 'relative error ' + dist(inner, 12));
  // Float32 tables: 0.52 rounds into the dead band, 0.48 rounds out of it. mapLane reads what the engine reads.
  const f52 = Math.fround(0.52), f48 = Math.fround(0.48);
  check('mapLane travel reads the Float32 table value at the dead-band edges', M.mapLane('travel', 0.52) === M.travelSpeed(f52) && M.mapLane('travel', 0.48) === M.travelSpeed(f48) && M.travelSpeed(f52) === 0,
    `fround(0.52)=${f52} -> ${M.travelSpeed(f52)}, fround(0.48)=${f48} -> ${fx(M.travelSpeed(f48), 4)} (exact 0.48 -> ${M.travelSpeed(0.48)})`);
  const edgeNodes = [];
  for (const kind of ['speed']) for (const key of M.shapeKeysFor('travel', kind)) for (const q of M.shapeNodes(key, 'travel', { kind, seed: 1, idh: 0 })) if (Math.abs(q.v - 0.48) < 1e-6 || Math.abs(q.v - 0.52) < 1e-6) edgeNodes.push(key);
  check('no Speed shape puts a node on a dead-band edge', !edgeNodes.length, edgeNodes.length ? edgeNodes.join(',') : `${M.shapeKeysFor('travel', 'speed').length} Speed keys checked`);
}

// =========================================================================== nodes and segment shapes
section('2.1 nodes: segment shapes and the monotone cubic');
{
  // bendShape = PulseLock's (exp(8b u)-1)/(exp(8b)-1)
  const b1 = M.bendShape(0.5, 1), bm1 = M.bendShape(0.5, -1), b0 = M.bendShape(0.37, 0), bs = M.bendShape(0.37, 0.0009);
  check('bendShape matches PulseLock', Math.abs(b1 - 1 / (Math.exp(4) + 1)) < 1e-12 && Math.abs(bm1 - (1 - 1 / (Math.exp(4) + 1))) < 1e-12 && b0 === 0.37 && bs === 0.37,
    `b=1 mid ${fx(b1, 5)} (code.md: ~0.018), b=-1 mid ${fx(bm1, 5)}, b=0 straight ${b0}, |b|<1e-3 straight ${bs}`);
  const M2 = 1001, ts = j => j / (M2 - 1);
  const lin = M.sampleNodes([M.mkNode(0, 0.2, 'linear'), M.mkNode(1, 0.8)], M2);
  let el = 0; for (let j = 0; j < M2; j++) el = Math.max(el, Math.abs(lin[j] - (0.2 + 0.6 * ts(j))));
  check('linear segment', el < 1e-6, `max error vs straight line ${fx(el, 9)}`);
  const bendv = M.sampleNodes([M.mkNode(0, 0.2, 'bend', 0.5), M.mkNode(1, 0.8)], M2);
  let eb = 0; for (let j = 0; j < M2; j++) eb = Math.max(eb, Math.abs(bendv[j] - (0.2 + 0.6 * M.bendShape(ts(j), 0.5))));
  check('bend segment (bend 0.5)', eb < 1e-6 && bendv[500] < 0.2 + 0.6 * 0.5 - 0.1, `max error ${fx(eb, 9)}, mid ${fx(bendv[500], 4)} (straight would be 0.5000)`);
  const st = M.sampleNodes([M.mkNode(0, 0.2, 'step'), M.mkNode(0.5, 0.8, 'step'), M.mkNode(1, 0.4)], M2);
  const before = Math.max(...Array.from(st.slice(0, 500)).map(x => Math.abs(x - 0.2))), after = Math.max(...Array.from(st.slice(500, 1000)).map(x => Math.abs(x - 0.8)));
  check('step segment holds until the next node', before < 1e-6 && after < 1e-6 && Math.abs(st[1000] - 0.4) < 1e-6, `t<0.5 dev ${fx(before, 8)}, 0.5<=t<1 dev ${fx(after, 8)}, t=1 ${fx(st[1000], 4)}`);
  const sm2 = M.sampleNodes([M.mkNode(0, 0.1), M.mkNode(1, 0.9)], M2);
  let es = 0; for (let j = 0; j < M2; j++) es = Math.max(es, Math.abs(sm2[j] - (0.1 + 0.8 * ts(j))));
  check('smooth with two nodes is the straight line', es < 1e-6, `max error ${fx(es, 9)}`);
  const edge = [M.mkNode(0, 0.2, 'linear'), M.mkNode(0.5, 0.2, 'linear'), M.mkNode(0.5, 0.9, 'linear'), M.mkNode(1, 0.9)];
  const ev = [M.evalNodes(edge, 0.49), M.evalNodes(edge, 0.5), M.evalNodes(edge, 0.5, true), M.evalNodes(edge, 0.51)];
  check('vertical edge (two nodes on one t)', ev[0] === 0.2 && ev[1] === 0.9 && ev[2] === 0.2 && ev[3] === 0.9, `0.49 -> ${ev[0]}, 0.5 -> ${ev[1]} (after), 0.5 from the left -> ${ev[2]}, 0.51 -> ${ev[3]}`);
  const v3 = M.normalizeNodes([{ t: 0, v: 0.3, lin: true }, { t: 1, v: 0.6 }]);
  check('v3 lin:true loads as linear', v3[0].shape === 'linear' && v3[1].shape === 'smooth' && v3[0].bend === 0, `${v3[0].shape}, ${v3[1].shape}`);
  const many = M.normalizeNodes(Array.from({ length: 500 }, (_, i) => ({ t: i / 499, v: 0.5 + 0.4 * Math.sin(i / 7) })));
  check('max 128 nodes per lane', many.length <= 128 && many[0].t === 0 && many[many.length - 1].t === 1, `500 nodes in -> ${many.length} out`);
  const messy = M.normalizeNodes([{ t: 0.7, v: 2 }, { t: 0.2, v: -1 }, { t: 0.5, v: 0.5, shape: 'bogus' }]);
  check('normalizeNodes clamps, sorts, pins ends', messy.map(q => q.t).join(',') === '0,0.5,1' && messy[0].v === 0 && messy[2].v === 1 && messy[1].shape === 'smooth', J(messy.map(q => [q.t, q.v, q.shape])));

  // monotone smooth: random node sets, every sample stays between its segment's two nodes
  const R = rng(77), worstD = [], worstF = [], naive = [];
  let samples = 0;
  for (let trial = 0; trial < 600; trial++) {
    const n = 2 + Math.floor(R() * 23);
    const tt = [0, 1]; for (let i = 0; i < n - 2; i++) tt.push(R()); tt.sort((a, b) => a - b);
    const raw = tt.map(t => ({ t, v: R(), shape: 'smooth', bend: 0 }));
    for (let i = 1; i < raw.length - 1; i++) { if (R() < 0.08) raw[i].t = raw[i - 1].t; if (R() < 0.12) raw[i].v = raw[i - 1].v; }
    const nd = M.normalizeNodes(raw);
    const Ms = 1500, tab = M.sampleNodes(nd, Ms);
    let k = 0, wd = 0, wf = 0, wn = 0;
    // naive cubic Hermite with plain averaged tangents (no Fritsch-Carlson limiting): proves this check can see overshoot
    const d = [], mt = [];
    for (let i = 0; i < nd.length - 1; i++) d.push((nd[i + 1].v - nd[i].v) / Math.max(1e-6, nd[i + 1].t - nd[i].t));
    for (let i = 0; i < nd.length; i++) mt.push(i === 0 ? d[0] : i === nd.length - 1 ? d[d.length - 1] : (d[i - 1] + d[i]) / 2);
    for (let j = 0; j < Ms; j++) {
      const t = j / (Ms - 1);
      while (k < nd.length - 2 && t >= nd[k + 1].t) k++;
      const A = nd[k], B = nd[k + 1], lo = Math.min(A.v, B.v), hi = Math.max(A.v, B.v);
      const vd = M.evalNodes(nd, t);
      wd = Math.max(wd, lo - vd, vd - hi); wf = Math.max(wf, lo - tab[j], tab[j] - hi);
      const h = B.t - A.t;
      if (h > 1e-6) { const u = (t - A.t) / h, u2 = u * u, u3 = u2 * u; const vn = (2 * u3 - 3 * u2 + 1) * A.v + (u3 - 2 * u2 + u) * h * mt[k] + (-2 * u3 + 3 * u2) * B.v + (u3 - u2) * h * mt[k + 1]; wn = Math.max(wn, lo - vn, vn - hi); }
      samples++;
    }
    worstD.push(Math.max(0, wd)); worstF.push(Math.max(0, wf)); naive.push(Math.max(0, wn));
  }
  check('smooth never overshoots its neighbours (600 random node sets, double)', Math.max(...worstD) <= 1e-12, `${samples} samples; worst overshoot per set ${dist(worstD, 12)}`);
  check('smooth never overshoots (Float32 table)', Math.max(...worstF) <= 1e-6, `worst per set ${dist(worstF, 9)} (float32 rounding only)`);
  check('...and the check can see overshoot (naive cubic on the same sets)', Math.max(...naive) > 0.01, `naive Hermite worst per set ${dist(naive, 4)}`);
  // against v3's sampleLane verbatim (prototype-v3-strandlock.html lines 174-190)
  {
    const clampV = (v, a, b) => Math.max(a, Math.min(b, v));
    function v3sampleLane(nd, M2) {
      const n = nd.length, o = new Float64Array(M2);
      if (n === 1) { o.fill(nd[0].v); return o; }
      const d = new Array(n - 1), m = new Array(n);
      for (let k = 0; k < n - 1; k++) d[k] = (nd[k + 1].v - nd[k].v) / Math.max(1e-6, nd[k + 1].t - nd[k].t);
      m[0] = d[0]; m[n - 1] = d[n - 2];
      for (let k = 1; k < n - 1; k++) m[k] = d[k - 1] * d[k] <= 0 ? 0 : (d[k - 1] + d[k]) / 2;
      for (let k = 0; k < n - 1; k++) { if (d[k] === 0) { m[k] = 0; m[k + 1] = 0; continue; } const a = m[k] / d[k], b = m[k + 1] / d[k], s = a * a + b * b; if (s > 9) { const t = 3 / Math.sqrt(s); m[k] = t * a * d[k]; m[k + 1] = t * b * d[k]; } }
      let k = 0;
      for (let j = 0; j < M2; j++) {
        const t = M2 === 1 ? 0 : j / (M2 - 1); while (k < n - 2 && t > nd[k + 1].t) k++;
        const A = nd[k], B = nd[k + 1], h = B.t - A.t; if (h <= 1e-6) { o[j] = B.v; continue; }
        const u = clampV((t - A.t) / h, 0, 1); let v;
        if (A.lin) v = A.v + (B.v - A.v) * u; else { const u2 = u * u, u3 = u2 * u; v = (2 * u3 - 3 * u2 + 1) * A.v + (u3 - 2 * u2 + u) * h * m[k] + (-2 * u3 + 3 * u2) * B.v + (u3 - u2) * h * m[k + 1]; }
        o[j] = clampV(v, 0, 1);
      }
      return o;
    }
    const R2 = rng(123), diffs = []; let differ = 0;
    for (let trial = 0; trial < 500; trial++) {
      const n = 2 + Math.floor(R2() * 20), tt = [0, 1]; for (let i = 0; i < n - 2; i++) tt.push(R2()); tt.sort((a, b) => a - b);
      const raw = tt.map(t => ({ t, v: R2(), lin: R2() < 0.2 }));
      const ours = M.sampleNodes(M.normalizeNodes(raw), 1000), ref = v3sampleLane(raw, 1000);
      const e = maxAbsDiff(ours, ref); diffs.push(e); if (e > 1e-6) differ++;
    }
    check('smooth = v3 sampleLane (500 random v3 node sets, 20% lin)', differ === 0, `${differ} sets differ by > 1e-6; difference ${dist(diffs, 9)} (Float32 rounding only)`);
  }
  // monotone data -> monotone curve
  let worstDrop = 0;
  for (let trial = 0; trial < 200; trial++) {
    const n = 3 + Math.floor(R() * 15), tt = [0, 1], vv = [];
    for (let i = 0; i < n - 2; i++) tt.push(R()); tt.sort((a, b) => a - b);
    for (let i = 0; i < n; i++) vv.push(R()); vv.sort((a, b) => a - b);
    const tab = M.sampleNodes(tt.map((t, i) => M.mkNode(t, vv[i])), 2000);
    for (let j = 1; j < tab.length; j++) worstDrop = Math.max(worstDrop, tab[j - 1] - tab[j]);
  }
  check('rising nodes give a rising curve', worstDrop <= 1e-7, `largest drop ${fx(worstDrop, 9)} in 200 rising sets`);
}

// =========================================================================== rates
section('1.5 rates');
{
  const want = { '1bar': 4, '1/2': 2, '1/4': 1, '1/8': 0.5, '1/16': 0.25, '1/32': 0.125, '1/64': 0.0625 };
  let bad = [], n = 0;
  for (const b of Object.keys(want)) for (const [s, f] of [['', 1], ['T', 2 / 3], ['D', 1.5]]) {
    n++; const got = M.rateBeats(b + s), w = want[b] * f;
    if (Math.abs(got - w) > 1e-12) bad.push(`${b + s}=${got} vs ${w}`);
  }
  check('rate table incl. T (x2/3) and D (x1.5)', !bad.length && M.RATES.length === 21 && n === 21, `${M.RATES.length} ids; 1/16T=${fx(M.rateBeats('1/16T'), 6)} 1/8D=${M.rateBeats('1/8D')} 1barT=${fx(M.rateBeats('1barT'), 6)} 1/64T=${fx(M.rateBeats('1/64T'), 6)}${bad.length ? '; ' + bad.join('; ') : ''}`);
  check('unknown rate reads as 1/16, numbers pass through', M.rateBeats('1/7') === 0.25 && M.rateBeats(0.3) === 0.3 && !M.isRate('1/7') && M.isRate('1/32D'), `'1/7'->${M.rateBeats('1/7')}, 0.3->${M.rateBeats(0.3)}`);
}

// =========================================================================== generators
section('2.1 generators: pattern and random');
{
  // pattern period equals the rate in beats: saw resets and squiggle up-crossings
  const res = 512, perRate = [];
  for (const rate of ['1/2T', '1/4', '1/8T', '1/8D', '1/16', '1/16T', '1/32', '1/64T']) {
    const P = M.rateBeats(rate);
    for (const lock of [true, false]) {
      let n = N(60, 0, 8, 'per'); n.c.formant = M.flat(0.5);
      n.gen.formant = M.makeGen('pattern', { shape: 'sawup', rate, d0: 0.5, d1: 0.5, lockToBeat: lock });
      const tab = M.laneTable(n, 'formant', res);
      const jumps = []; for (let j = 1; j < tab.length; j++) if (tab[j] < tab[j - 1] - 0.2) jumps.push(j / res);
      const sp = []; for (let i = 1; i < jumps.length; i++) sp.push(jumps[i] - jumps[i - 1]);
      n = Object.assign({}, n, { gen: Object.assign({}, n.gen, { formant: M.makeGen('pattern', { shape: 'squiggle', rate, d0: 0.5, d1: 0.5, lockToBeat: lock }) }) });
      const tb2 = M.laneTable(n, 'formant', res), xs = [];
      for (let j = 1; j < tb2.length; j++) if (tb2[j - 1] < 0.5 && tb2[j] >= 0.5) xs.push((j - 1 + (0.5 - tb2[j - 1]) / (tb2[j] - tb2[j - 1])) / res);
      const sp2 = []; for (let i = 1; i < xs.length; i++) sp2.push(xs[i] - xs[i - 1]);
      const e1 = Math.max(...sp.map(x => Math.abs(x - P))), e2 = Math.max(...sp2.map(x => Math.abs(x / P - 1)));
      perRate.push({ rate, lock, P, n1: sp.length, e1, n2: sp2.length, e2 });
    }
  }
  const okP = perRate.every(r => r.n1 >= Math.floor(8 / r.P) - 2 && r.e1 <= 1.01 / res && r.e2 < 0.01);
  check('pattern period = rate in beats (8 rates, locked and unlocked)', okP,
    perRate.map(r => `${r.rate}${r.lock ? '' : '(free)'}: ${r.n1} saw periods max |err| ${fx(r.e1, 4)} beat, squiggle max rel ${fx(r.e2, 4)}`).join('; ') + ` (limit 1 sample = ${fx(1 / res, 4)} beat, 1%)`);
  // warp: unlocked squiggle speeds up when warp > 0
  {
    const n = N(60, 0, 4, 'warp'); n.gen.formant = M.makeGen('pattern', { shape: 'squiggle', rate: '1/16', d0: 0.6, d1: 0.6, lockToBeat: false, warp: 1 });
    const tab = M.laneTable(n, 'formant', 512), xs = [];
    for (let j = 1; j < tab.length; j++) if (tab[j - 1] < 0.5 && tab[j] >= 0.5) xs.push(j / 512);
    const sp = []; for (let i = 1; i < xs.length; i++) sp.push(xs[i] - xs[i - 1]);
    let shrinking = 0; for (let i = 1; i < sp.length; i++) if (sp[i] < sp[i - 1] + 1e-9) shrinking++;
    check('warp > 0 speeds the pattern up across the note (same cycle count)', sp.length >= 10 && sp[0] > 2 * sp[sp.length - 1] && shrinking === sp.length - 1 && xs.length === 16,
      `${xs.length} up-crossings (want 16 = 16 cycles, the last at the note end); periods ${fx(sp[0], 3)} -> ${fx(sp[sp.length - 1], 3)} beat, ${shrinking}/${sp.length - 1} shrinking`);
  }
  // depth ramps d0 -> d1; pitch lane full depth = +-2 semitones
  {
    const n = N(60, 0, 4, 'depth'); n.gen.formant = M.makeGen('pattern', { shape: 'square', rate: '1/4', d0: 0.2, d1: 1 });
    const tab = M.laneTable(n, 'formant', 256), first = Math.abs(tab[1] - 0.5), last = Math.abs(tab[tab.length - 2] - 0.5);
    const p = N(60, 0, 4, 'pdepth'); p.gen.pitch = M.makeGen('pattern', { shape: 'square', rate: '1/4', d0: 1, d1: 1 });
    const pt = M.laneTable(p, 'pitch', 256); let mx = 0; for (const v of pt) mx = Math.max(mx, Math.abs(semis(v)));
    check('depth runs d0 -> d1; pitch full depth = +-2 st', Math.abs(first - 0.1) < 0.005 && Math.abs(last - 0.5) < 0.005 && Math.abs(mx - 2) < 1e-4, `start half-depth ${fx(first, 4)} (want 0.1), end ${fx(last, 4)} (want 0.5), pitch peak ${fx(mx, 5)} st`);
  }
  // lockToBeat keeps phase continuous across adjacent notes
  {
    const R = rng(11), locked = [], free = [];
    for (let i = 0; i < 200; i++) {
      const rate = M.RATES[Math.floor(R() * M.RATES.length)], s0 = Math.round(R() * 64) / 16, l0 = 0.3 + Math.round(R() * 40) / 13, l1 = 0.5 + R() * 2;
      for (const lock of [true, false]) {
        const g = M.makeGen('pattern', { shape: ['squiggle', 'triangle', 'smoothrandom'][i % 3], rate, d0: 0.6, d1: 0.6, lockToBeat: lock, seed: 3 });
        const a = N(60, s0, l0, 'A' + i), b = N(64, s0 + l0, l1, 'B' + i); a.gen.formant = g; b.gen.formant = g;
        const ta = M.laneTable(a, 'formant', 256), tb = M.laneTable(b, 'formant', 256);
        (lock ? locked : free).push(Math.abs(ta[ta.length - 1] - tb[0]));
      }
    }
    check('lockToBeat: no jump where one note ends and the next begins', Math.max(...locked) < 1e-6, `200 random pairs: locked |jump| ${dist(locked, 7)}; unlocked (restarts) ${dist(free, 3)}`);
    check('...and unlocked patterns do restart (the check can fail)', free.filter(x => x > 0.05).length > 50, `${free.filter(x => x > 0.05).length}/200 unlocked joins jump > 0.05`);
  }
  // random generator
  {
    const mk = (id, p, s, l, gen) => { const n = N(p, s, l, id); n.gen.formant = M.makeGen('random', gen); return n; };
    const rate = '1/8T', P = M.rateBeats(rate), res = 512;
    const viol = [], changes = [], bounds = [];
    for (const [s, l] of [[0.3, 3], [0, 4], [1.17, 2.5], [5.5, 1]]) {
      const n = mk('r' + s, 60, s, l, { rate, amount: 1, smooth: 0, seed: 9 });
      const tab = M.laneTable(n, 'formant', res), Nn = tab.length;
      let ch = 0, v = 0;
      for (let j = 1; j < Nn; j++) {
        if (tab[j] === tab[j - 1]) continue;
        ch++;
        const b0 = s + (j - 1) / (Nn - 1) * l, b1 = s + j / (Nn - 1) * l;
        const k = Math.floor(b0 / P + 1e-9) + 1;            // the first grid line after sample j-1
        if (!(k * P <= b1 + 1e-9)) v++;
      }
      viol.push(v); changes.push(ch);
      bounds.push(Math.floor((s + l) / P + 1e-9) - Math.floor(s / P + 1e-9));
    }
    check('random, smooth 0: values change only on the rate grid (song beats)', viol.every(x => x === 0) && changes.every((c, i) => c >= bounds[i] - 1 && c <= bounds[i]),
      `changes per note ${changes.join(',')} vs grid lines crossed ${bounds.join(',')}; off-grid changes ${viol.join(',')}`);
    const a1 = M.laneTable(mk('rs', 60, 0, 4, { rate: '1/16', seed: 21 }), 'formant', 256), a2 = M.laneTable(mk('rs', 60, 0, 4, { rate: '1/16', seed: 21 }), 'formant', 256);
    check('random: same seed -> identical tables', maxAbsDiff(a1, a2) === 0, `max diff ${maxAbsDiff(a1, a2)}`);
    let clip = clipOf([mk('rr', 60, 0, 4, { rate: '1/16', seed: 21, amount: 0.8 })]);
    const rr = M.reroll(clip, null, 'formant'), b1 = M.laneTable(rr.notes[0], 'formant', 256);
    const steps = 16; let diffSteps = 0;
    for (let k = 0; k < steps; k++) if (Math.abs(a1[k * 64 + 32] - b1[k * 64 + 32]) > 1e-6) diffSteps++;
    check('re-roll (seed+1) changes the values', rr.notes[0].gen.formant.seed === 22 && diffSteps >= 14, `seed ${clip.notes[0].gen.formant.seed} -> ${rr.notes[0].gen.formant.seed}; ${diffSteps}/${steps} steps changed`);
    const same = [mk('x1', 60, 0, 4, { rate: '1/8', perNote: 'same', seed: 4 }), mk('x2', 67, 0, 4, { rate: '1/8', perNote: 'same', seed: 4 })];
    const diff = [mk('x1', 60, 0, 4, { rate: '1/8', perNote: 'different', seed: 4 }), mk('x2', 67, 0, 4, { rate: '1/8', perNote: 'different', seed: 4 })];
    const dSame = maxAbsDiff(M.laneTable(same[0], 'formant', 256), M.laneTable(same[1], 'formant', 256));
    const dDiff = maxAbsDiff(M.laneTable(diff[0], 'formant', 256), M.laneTable(diff[1], 'formant', 256));
    check("random 'same' moves a chord together, 'different' per note", dSame === 0 && dDiff > 0.1, `same: max diff ${dSame}; different: max diff ${fx(dDiff, 3)}`);
    // later notes share 'same' values at the same song step
    const late = mk('x3', 72, 2, 1, { rate: '1/8', perNote: 'same', seed: 4 });
    const tl = M.laneTable(late, 'formant', 256), t0 = M.laneTable(same[0], 'formant', 256);
    const dl = Math.max(...[0.1, 0.3, 0.6, 0.9].map(f => Math.abs(tl[Math.round(f * 256)] - t0[512 + Math.round(f * 256)])));
    check("random 'same': a note starting later reads the same song steps", dl < 1e-6, `max diff at shared beats ${dl}`);
    const sm = M.laneTable(mk('sm', 60, 0, 4, { rate: '1/8', smooth: 1, amount: 1, seed: 5 }), 'formant', 512), st0 = M.laneTable(mk('sm', 60, 0, 4, { rate: '1/8', smooth: 0, amount: 1, seed: 5 }), 'formant', 512);
    let jSm = 0, jSt = 0; for (let j = 1; j < sm.length; j++) { jSm = Math.max(jSm, Math.abs(sm[j] - sm[j - 1])); jSt = Math.max(jSt, Math.abs(st0[j] - st0[j - 1])); }
    check('random smooth 1 glides (no steps)', jSm < 0.02 && jSt > 0.2, `largest sample-to-sample move: smooth 1 ${fx(jSm, 4)}, smooth 0 ${fx(jSt, 4)}`);
    const amps = [];
    for (let sd = 1; sd <= 40; sd++) { const t = M.laneTable(mk('amt' + sd, 60, 0, 2, { rate: '1/16', amount: 0.6, seed: sd }), 'formant', 128); for (const v of t) amps.push(Math.abs(v - 0.5)); }
    check('random Amount 1 = +-0.5 (Amount 0.6 stays within 0.3)', Math.max(...amps) <= 0.3 + 1e-6 && Math.max(...amps) > 0.27, `|offset| ${dist(amps, 4)}`);
  }
}

// =========================================================================== shapes
section('Y2 shapes: each vs across');
{
  const seq = [0, 1, 2, 3].map(k => N(60 + k, k, 1, 'q' + k));
  const c = clipOf(seq);
  const each = M.applyShape(c, null, 'tone', 'up', 'each'), across = M.applyShape(c, null, 'tone', 'up', 'across');
  const te = each.notes.map(n => M.laneTable(n, 'tone', 64)), ta = across.notes.map(n => M.laneTable(n, 'tone', 64));
  check('each: every note gets the whole ramp', te.every(t => Math.abs(t[0]) < 1e-6 && Math.abs(t[t.length - 1] - 1) < 1e-6), te.map(t => `${fx(t[0], 2)}->${fx(t[t.length - 1], 2)}`).join(' '));
  const firsts = ta.map(t => t[0]), lasts = ta.map(t => t[t.length - 1]);
  const ok = [0, 1, 2, 3].every(k => Math.abs(firsts[k] - k / 4) < 1e-6 && Math.abs(lasts[k] - (k + 1) / 4) < 1e-6);
  let drop = 0; const joined = [].concat(...ta.map(t => Array.from(t))); for (let j = 1; j < joined.length; j++) drop = Math.max(drop, joined[j - 1] - joined[j]);
  check('across: one ramp spans the selection', ok && drop < 1e-6, ta.map(t => `${fx(t[0], 3)}->${fx(t[t.length - 1], 3)}`).join(' ') + `, largest drop ${fx(drop, 8)}`);
  const chord = clipOf([N(60, 0, 4, 'c1'), N(64, 2, 2, 'c2')]);
  const ac = M.applyShape(chord, null, 'amp', 'up', 'across'), t2 = M.laneTable(byId(ac, 'c2'), 'amp', 64);
  check('across: an overlapping note gets its own time slice', Math.abs(t2[0] - 0.5) < 1e-6 && Math.abs(t2[t2.length - 1] - 1) < 1e-6, `note starting half-way: ${fx(t2[0], 4)} -> ${fx(t2[t2.length - 1], 4)}`);
  // slices reproduce one long note's curve (exact for linear/bend/step, close for smooth)
  const sliceErr = [];
  for (const [laneId, key, kind] of [['travel', 'tapestop', 'speed'], ['travel', 'reversehalf', 'speed'], ['travel', 'rewind', 'speed'], ['amp', 'swell', null], ['tone', 'squiggle', null], ['amp', 'decay', null], ['pitch', 'dip', null]]) {
    const parts = clipOf([0, 1, 2, 3, 4].map(k => N(60, k * 0.8, 0.8, 'p' + k)));
    const one = clipOf([N(60, 0, 4, 'one')]);
    const ap = M.applyShape(parts, null, laneId, key, 'across'), ao = M.applyShape(one, null, laneId, key, 'each');
    const longNodes = ao.notes[0].c[laneId];
    let e = 0, pts = 0;
    ap.notes.forEach((n, k) => {
      for (let i = 1; i < 400; i++) {
        const t = i / 400, u = (k * 0.8 + t * 0.8) / 4;
        if (longNodes.some(q => Math.abs(q.t - u) < 1e-9)) continue;   // exactly on a node of the long curve (edges)
        e = Math.max(e, Math.abs(M.evalNodes(n.c[laneId], t) - M.evalNodes(longNodes, u))); pts++;
      }
    });
    sliceErr.push({ key, e, pts, exact: key !== 'swell' && key !== 'squiggle' && key !== 'decay' && key !== 'dip' });
  }
  check('across slices = one long note (linear/bend/step exact)', sliceErr.filter(s => s.exact).every(s => s.e < 1e-5), sliceErr.filter(s => s.exact).map(s => `${s.key} ${fx(s.e, 8)}`).join(', '));
  check('across slices = one long note (smooth within 0.003; refit tolerance 0.002)', sliceErr.filter(s => !s.exact).every(s => s.e < 0.003), sliceErr.filter(s => !s.exact).map(s => `${s.key} ${fx(s.e, 5)}`).join(', '));
  // slices at every split count (cuts on nodes included), and node counts per note
  {
    const rows = []; let worst = 0;
    for (const parts of [2, 3, 5, 8, 16]) for (const [lane, key] of [['amp', 'swell'], ['tone', 'squiggle'], ['amp', 'decay'], ['pitch', 'dip'], ['amp', 'up'], ['tone', 'random']]) {
      const long = M.shapeNodes(key, lane, { seed: 3, idh: 0 });
      let e = 0, nodes = 0;
      for (let k = 0; k < parts; k++) {
        const u0 = k / parts, u1 = (k + 1) / parts, sl = M.sliceNodes(long, u0, u1); nodes += sl.length;
        for (let i = 1; i < 400; i++) { const t = i / 400; e = Math.max(e, Math.abs(M.evalNodes(sl, t) - M.evalNodes(long, u0 + t * (u1 - u0)))); }
      }
      worst = Math.max(worst, e); rows.push({ e, npn: nodes / parts, key, parts });
    }
    const up = rows.filter(r => r.key === 'up');
    check('sliceNodes on 2/3/5/8/16 parts (cut on a node included)', worst < 0.003 && up.every(r => r.npn === 2 && r.e < 1e-9), `error ${dist(rows.map(r => r.e), 5)}; nodes per note ${dist(rows.map(r => r.npn), 1)}; Ramp up stays 2 nodes per note`);
  }
  // every key on every lane: valid nodes
  const notes = [N(60, 0, 2, 'k1'), N(64, 2, 1, 'k2')]; notes[1].sample.travel = 'path';
  const base = clipOf(notes);
  let n = 0, badK = [];
  for (const L of M.LANES) for (const kind of (L.id === 'travel' ? ['speed', 'path'] : [null])) for (const key of M.shapeKeysFor(L.id, kind)) for (const scope of ['each', 'across']) {
    const out = M.applyShape(base, null, L.id, key, scope, { seed: 3, bpm: 120, sampleSeconds: 6 });
    for (const q of out.notes) { const nd = q.c[L.id]; n++; if (!(nd.length >= 2 && nd.length <= 128 && nd[0].t === 0 && nd[nd.length - 1].t === 1 && nd.every(x => x.v >= 0 && x.v <= 1 && M.NODE_SHAPES.includes(x.shape)) && J(M.normalizeNodes(nd)) === J(nd))) badK.push(L.id + ':' + key); }
  }
  check('every shape key on every lane gives valid nodes', !badK.length, `${n} note-lanes checked${badK.length ? '; bad ' + badK.join(',') : ''}`);
  // travel keys
  const sp = N(60, 0, 4, 'sp'); const cs = clipOf([sp]);
  const ts = M.laneTable(M.applyShape(cs, null, 'travel', 'tapestop', 'each').notes[0], 'travel', 64).map(M.travelSpeed);
  let rises = 0; for (let j = 1; j < ts.length; j++) if (ts[j] > ts[j - 1] + 1e-9) rises++;
  check('Tape stop: 1x slowing to a stop', Math.abs(ts[0] - 1) < 1e-6 && ts[ts.length - 1] === 0 && rises === 0, `speed ${fx(ts[0], 3)} -> ${fx(ts[Math.floor(ts.length / 2)], 3)} (mid) -> ${ts[ts.length - 1]}, ${rises} rises`);
  const tr = M.laneTable(M.applyShape(cs, null, 'travel', 'reversehalf', 'each').notes[0], 'travel', 64).map(M.travelSpeed);
  check('Reverse halfway: +1x then -1x', Math.abs(tr[10] - 1) < 1e-6 && Math.abs(tr[tr.length - 10] + 1) < 1e-6, `speed at 1/8: ${fx(tr[10], 3)}, at 7/8: ${fx(tr[tr.length - 10], 3)}`);
  const pathN = N(60, 0, 2, 'pa'); pathN.sample.travel = 'path';
  const fit = M.applyShape(clipOf([pathN]), null, 'travel', 'fit', 'each', { bpm: 120, sampleSeconds: 6 }).notes[0].c.travel;
  const fitLast = M.evalNodes(fit, 1);
  const longP = N(60, 0, 16, 'pl'); longP.sample.travel = 'path';
  const fit2 = M.applyShape(clipOf([longP]), null, 'travel', 'fit', 'each', { bpm: 60, sampleSeconds: 6 }).notes[0].c.travel;
  const ft = M.laneTable(Object.assign({}, longP, { c: Object.assign({}, longP.c, { travel: fit2 }) }), 'travel', 64);
  let wraps = 0, slope = []; for (let j = 1; j < ft.length; j++) { if (ft[j] < ft[j - 1] - 0.5) wraps++; else slope.push((ft[j] - ft[j - 1]) * 64); }
  check('Fit: Path at exactly 1x (wraps when the Region loops)', Math.abs(fitLast - 1 / 6) < 1e-9 && wraps === 2 && Math.max(...slope.map(x => Math.abs(x - 1 / 6))) < 0.01,
    `2 beats at 120 bpm over 6 s: ends at ${fx(fitLast, 5)} (want 0.16667); 16 beats at 60 bpm: ${wraps} wraps (want 2), slope per beat ${dist(slope, 4)} (want 0.1667)`);
  const pathOnly = M.applyShape(clipOf([N(60, 0, 1, 'sk')]), null, 'travel', 'playthrough', 'each');
  check('a Path key leaves Speed-kind notes alone', J(pathOnly.notes[0].c.travel) === J(M.flat(0.75)), J(pathOnly.notes[0].c.travel.map(q => q.v)));
  const r1 = M.applyShape(c, null, 'tone', 'random', 'each', { seed: 5 }), r2 = M.applyShape(c, null, 'tone', 'random', 'each', { seed: 5 }), r3 = M.applyShape(c, null, 'tone', 'random', 'each', { seed: 6 });
  const perNoteDiff = J(r1.notes[0].c.tone) !== J(r1.notes[1].c.tone);
  check("shape 'random': seeded, per note in 'each'", J(r1) === J(r2) && J(r1) !== J(r3) && perNoteDiff, `same seed identical ${J(r1) === J(r2)}, seed+1 differs ${J(r1) !== J(r3)}, notes differ ${perNoteDiff}`);
  let threw = false; try { M.applyShape(c, null, 'amp', 'tapestop', 'each'); } catch (e) { threw = true; }
  check('unknown shape for a lane throws', threw, 'tapestop on Volume -> ' + (threw ? 'Error' : 'no error'));
}

// =========================================================================== bake
section('Y9 Turn into nodes (bakeGen)');
{
  const res = 256, rows = [];
  const cases = [];
  for (const shape of M.GEN_SHAPES) for (const rate of ['1/4', '1/8', '1/16']) for (const l of [1, 2, 4]) cases.push({ kind: 'pattern', shape, rate, l });
  for (const smooth of [0, 0.5, 1]) for (const rate of ['1/4', '1/8T', '1/16']) for (const l of [1, 2, 4]) cases.push({ kind: 'random', smooth, rate, l });
  cases.forEach((cs, i) => {
    const n = N(60, 0.25 * (i % 4), cs.l, 'bk' + i);
    if (i % 3 === 0) n.c.formant = M.shapeNodes('swell', 'formant').map(q => M.mkNode(q.t, 0.3 + 0.4 * q.v, q.shape));
    n.gen.formant = M.makeGen(cs.kind, { shape: cs.shape, rate: cs.rate, d0: 0.4, d1: 0.2, amount: 0.6, smooth: cs.smooth, seed: i, lockToBeat: i % 2 === 0 });
    const before = M.laneTable(n, 'formant', res);
    const out = M.bakeGen(clipOf([n]), null, 'formant');
    const q = out.notes[0], after = M.laneTable(q, 'formant', res);
    const pts = Array.from(before, (v, j) => ({ t: j / (before.length - 1), v }));
    const needed = M.rdp(pts, M.BAKE_EPS).length;
    rows.push({ name: `${cs.kind}/${cs.shape || 's' + cs.smooth}/${cs.rate}/${cs.l}b`, err: maxAbsDiff(before, after), nodes: q.c.formant.length, needed, genGone: q.gen.formant === null });
  });
  const fit = rows.filter(r => r.needed <= 128), capped = rows.filter(r => r.needed > 128);
  check('bakeGen reproduces the table within 0.01 (cases that fit in 128 nodes)', fit.every(r => r.err <= 0.01) && fit.length > 60, `${fit.length} cases: error ${dist(fit.map(r => r.err), 5)}; nodes ${dist(fit.map(r => r.nodes), 0)}`);
  check('bakeGen removes the generator and keeps <= 128 nodes', rows.every(r => r.genGone && r.nodes <= 128), `${rows.length} cases, max nodes ${Math.max(...rows.map(r => r.nodes))}`);
  info(`bakeGen cases that need more than 128 nodes at eps 0.004 (eps is raised): ${capped.length}; error ${dist(capped.map(r => r.err), 4)}; ` + capped.map(r => `${r.name} needs ${r.needed} -> err ${fx(r.err, 4)}`).join('; '));
}

// =========================================================================== connect
section('Y5 connect / disconnect');
{
  const mk = () => clipOf([N(60, 0, 1, 'A'), N(64, 0, 1, 'B'), N(67, 0, 1, 'C'), N(62, 1, 1, 'D'), N(65, 1, 1, 'E'), N(69, 1, 1, 'F'), N(72, 2, 1, 'G')]);
  const links = c => c.notes.filter(n => n.glideTo).map(n => n.id + '>' + n.glideTo).sort().join(' ');
  const near = M.connect(mk(), null, 'nearest', 1), far = M.connect(mk(), null, 'furthest', 1);
  check('connect nearest (next notes only, one incoming per target)', links(near) === 'A>D B>E C>F D>G', links(near) + ' (want A>D B>E C>F D>G)');
  check('connect furthest', links(far) === 'A>F B>D C>E D>G', links(far) + ' (want A>F B>D C>E D>G)');
  const rA = M.connect(mk(), null, 'random', 7), rB = M.connect(mk(), null, 'random', 7);
  const seeds = []; for (let s = 1; s <= 12; s++) seeds.push(links(M.connect(mk(), null, 'random', s)));
  check('connect random: seeded and deterministic', links(rA) === links(rB) && new Set(seeds).size > 1, `seed 7 twice: ${links(rA)} | ${new Set(seeds).size} distinct link sets over seeds 1-12`);
  check('no link skips past the next notes', ![near, far, rA].some(c => c.notes.some(n => ['A', 'B', 'C'].includes(n.id) && n.glideTo === 'G')), 'chord 1 never links to G');
  // overlap tolerance 0.13 and the 24-semitone reach
  const ov = clipOf([N(60, 0, 1, 'a'), N(62, 0.9, 1, 'b'), N(60, 4, 1, 'c'), N(62, 4.8, 1, 'd'), N(60, 8, 1, 'e'), N(85, 9, 1, 'f')]);
  const ovl = links(M.connect(ov, null, 'nearest', 1));
  check('candidates: start >= end - 0.13 beat, within 24 st', ovl === 'a>b b>c c>e', `${ovl || '(none)'} (want a>b b>c c>e: b starts 0.10 before a ends = in; d starts 0.20 before c ends = out, so c reaches e; e>f is 25 st = out; d's next note e is taken)`);
  // an incoming link from an unselected note keeps its target taken
  const ext = clipOf([N(60, 0, 1, 'X'), N(61, 0, 1, 'Y'), N(62, 1, 1, 'Z')]); ext.notes[0].glideTo = 'Z';
  const extl = links(M.connect(ext, ['Y', 'Z'], 'nearest', 1));
  check('a target with an outside incoming link is taken', extl === 'X>Z', extl);
  // property test on random clips
  const R = rng(99); let maxIn = 0, rule = 0, made = 0, clips = 0;
  for (let t = 0; t < 300; t++) {
    const n = 6 + Math.floor(R() * 10), notes = [];
    for (let i = 0; i < n; i++) notes.push(N(48 + Math.floor(R() * 40), Math.floor(R() * 32) / 4, 0.25 + Math.floor(R() * 8) / 4, 'm' + i));
    const sel = notes.filter(() => R() < 0.8).map(q => q.id), mode = ['nearest', 'furthest', 'random'][t % 3];
    const out = M.connect(clipOf(notes), sel, mode, t);
    clips++;
    const inc = {}; for (const q of out.notes) if (q.glideTo) inc[q.glideTo] = (inc[q.glideTo] || 0) + 1;
    maxIn = Math.max(maxIn, ...Object.values(inc), 0);
    for (const q of out.notes) {
      if (!q.glideTo) continue; made++;
      const m = byId(out, q.glideTo), cand = out.notes.filter(x => sel.includes(x.id) && x !== q && x.s > q.s && x.s >= q.s + q.l - 0.13 && Math.abs(x.p - q.p) <= 24);
      const s0 = Math.min(...cand.map(x => x.s));
      if (!sel.includes(q.id) || !cand.includes(m) || m.s > s0 + 0.05) rule++;
    }
  }
  check('random clips: <= 1 incoming link per target, every link obeys the next-notes rule', maxIn <= 1 && rule === 0 && made > 300, `${clips} clips, ${made} links, max incoming ${maxIn}, rule breaks ${rule}`);
  const dc = M.disconnect(near, ['A', 'D', 'B']);
  check('disconnect clears links among the selection only', links(dc) === 'B>E C>F D>G', `${links(dc)} (A>D removed; B>E, C>F, D>G kept: E, F, G are not selected)`);
}

// =========================================================================== tools
section('tools: walk, chop, reverse, flip, more/less, select similar, humanize, strum, legato');
{
  const shuffled = clipOf([N(60, 3, 1, 'w4'), N(60, 0, 1, 'w1'), N(60, 4, 1, 'w5'), N(60, 1, 1, 'w2'), N(60, 2, 1, 'w3')]);
  const ws = M.walk(shuffled, null, 'start', 0, 1);
  const pos = ['w1', 'w2', 'w3', 'w4', 'w5'].map(id => byId(ws, id).sample.start.pos);
  check('walk start: first -> last note in time order', J(pos) === J([0, 0.25, 0.5, 0.75, 1]) && byId(ws, 'w3').sample.start.mode === 'region', J(pos));
  const wl = M.walk(shuffled, null, 'tone', 0.2, 0.6), tv = ['w1', 'w2', 'w3', 'w4', 'w5'].map(id => byId(wl, id).c.tone[0].v);
  check('walk a lane (flat values)', tv.every((v, i) => Math.abs(v - (0.2 + 0.1 * i)) < 1e-9) && byId(wl, 'w2').c.tone.length === 2, J(tv.map(v => +v.toFixed(3))));

  // chop
  const src = N(60, 2, 1, 'ch'); src.c.tone = M.shapeNodes('up', 'tone'); src.glideTo = 'tgt';
  const cc = clipOf([src, N(64, 3, 1, 'tgt')]);
  const r = M.chop(cc, ['ch'], '1/16', 0.25);
  const pieces = r.clip.notes.filter(n => n.p === 60).sort((a, b) => a.s - b.s);
  const tt = pieces.map(n => { const t = M.laneTable(n, 'tone', 64); return [t[0], t[t.length - 1]]; });
  check('chop 1 beat at 1/16 -> 4 pieces',!r.refused && pieces.length === 4 && pieces.every((n, k) => Math.abs(n.s - (2 + k * 0.25)) < 1e-12 && Math.abs(n.l - 0.25) < 1e-12), pieces.map(n => `${n.s}+${n.l}`).join(' '));
  check('chop keeps the drawing continuous across pieces', tt.every((x, k) => Math.abs(x[0] - k / 4) < 1e-6 && Math.abs(x[1] - (k + 1) / 4) < 1e-6), tt.map(x => `${fx(x[0], 3)}->${fx(x[1], 3)}`).join(' '));
  check('chop: first piece keeps id, link moves to last piece, later pieces restart, Start steps',
    pieces[0].id === 'ch' && pieces[3].glideTo === 'tgt' && pieces.slice(0, 3).every(n => !n.glideTo) && !pieces[0].sample.restart && pieces.slice(1).every(n => n.sample.restart) && J(pieces.map(n => n.sample.start.pos)) === J([0, 0.25, 0.5, 0.75]) && new Set(r.clip.notes.map(n => n.id)).size === r.clip.notes.length,
    `ids ${pieces.map(n => n.id).join(',')}; links ${pieces.map(n => n.glideTo || '-').join(',')}; restart ${pieces.map(n => +n.sample.restart).join('')}; start ${pieces.map(n => n.sample.start.pos).join(',')}`);
  const tri = M.chop(clipOf([N(60, 0, 1, 't')]), null, '1/8T', 0);
  check('chop at 1/8T -> 3 pieces of 1/3 beat', tri.clip.notes.length === 3 && tri.clip.notes.every(n => Math.abs(n.l - 1 / 3) < 1e-9), tri.clip.notes.map(n => fx(n.l, 4)).join(','));
  const big = clipOf(Array.from({ length: 70 }, (_, i) => N(60, i * 4 % 16, 4, 'b' + i)));
  const rf = M.chop(big, null, '1/64', 0);
  check('chop refuses over maxNotes (1024)', rf.refused && rf.clip === big && rf.count === 70 * 64, `70 notes x 64 pieces = ${rf.count}: refused ${rf.refused}, clip unchanged ${rf.clip === big}`);
  const rf2 = M.chop(clipOf([N(60, 0, 1, 'one')]), null, '1/16', 0, 3);
  check('chop refuses over a custom maxNotes', rf2.refused && rf2.count === 4, `4 pieces vs max 3: refused ${rf2.refused}`);
  const det = J(M.chop(cc, ['ch'], '1/16', 0).clip) === J(M.chop(cc, ['ch'], '1/16', 0).clip);
  check('chop is deterministic (piece ids from hashes)', det, String(det));

  // reverse / flip / scale
  const rn = N(60, 0, 2, 'rv'); rn.c.amp = [M.mkNode(0, 0.1, 'linear'), M.mkNode(0.3, 0.9, 'bend', 0.6), M.mkNode(0.6, 0.4, 'step'), M.mkNode(0.8, 0.7, 'smooth'), M.mkNode(1, 0.2)];
  const rc = clipOf([rn]), rev = M.reverseLane(rc, null, 'amp');
  const A0 = M.laneTable(rn, 'amp', 500), A1 = M.laneTable(rev.notes[0], 'amp', 500), L = A0.length - 1;
  const errs = []; for (let j = 1; j < L; j++) { const e = Math.abs(A1[j] - A0[L - j]); if (![0.3, 0.6, 0.8].some(x => Math.abs(j / L - (1 - x)) < 1.5 / L)) errs.push(e); }
  const back = M.reverseLane(rev, null, 'amp'), A2 = M.laneTable(back.notes[0], 'amp', 500);
  check('reverseLane mirrors the lane in time (linear, bend, step, smooth)', Math.max(...errs) < 0.002 && maxAbsDiff(A0.subarray(1, L), A2.subarray(1, L)) < 0.002,
    `|reversed(t) - original(1-t)| ${dist(errs, 6)}; reverse twice vs original ${fx(maxAbsDiff(A0.subarray(1, L), A2.subarray(1, L)), 6)}`);
  {
    const R = rng(31), es = [];
    for (let trial = 0; trial < 200; trial++) {
      const n = 3 + Math.floor(R() * 12), tt = [0, 1]; for (let i = 0; i < n - 2; i++) tt.push(R()); tt.sort((a, b) => a - b);
      const nd = M.normalizeNodes(tt.map(t => ({ t, v: R(), shape: ['smooth', 'smooth', 'linear', 'bend'][Math.floor(R() * 4)], bend: R() * 2 - 1 })));
      const rv = M.reverseNodes(nd); let e = 0;
      for (let i = 0; i <= 500; i++) { const t = i / 500; e = Math.max(e, Math.abs(M.evalNodes(rv, t) - M.evalNodes(nd, 1 - t))); }
      es.push(e);
    }
    const exact = es.filter(e => e < 1e-9).length;
    check('reverseNodes on 200 random smooth/linear/bend sets', Math.max(...es) < 1e-3 && exact >= 180,
      `max |reversed(t) - original(1-t)| per set ${dist(es, 9)}; ${exact}/200 exact (the rest: v3's left-to-right FC limit, kept on purpose)`);
  }
  const fl = M.flipLane(rc, null, 'amp'), F1 = M.laneTable(fl.notes[0], 'amp', 500);
  let ef = 0; for (let j = 0; j <= L; j++) ef = Math.max(ef, Math.abs(F1[j] - (1 - A0[j])));
  const tvN = N(60, 0, 1, 'tf'); tvN.c.travel = M.shapeNodes('tapestop', 'travel', { kind: 'speed' });
  const tfl = M.flipLane(clipOf([tvN]), null, 'travel'), sp0 = M.laneTable(tvN, 'travel', 64).map(M.travelSpeed), sp1 = M.laneTable(tfl.notes[0], 'travel', 64).map(M.travelSpeed);
  check('flipLane: v -> 1 - v (Travel: speed changes sign)', ef < 1e-6 && Math.max(...sp0.map((s, j) => Math.abs(s + sp1[j]))) < 1e-4, `max |flip - (1-v)| ${fx(ef, 8)}; speed ${fx(sp0[0], 3)} -> ${fx(sp1[0], 3)}`);
  const sn = N(60, 0, 1, 'sc'); sn.c.formant = [M.mkNode(0, 0.6), M.mkNode(1, 0.3)]; sn.gen.formant = M.makeGen('pattern', { d0: 0.2, d1: 0.4 });
  const more = M.scaleLane(clipOf([sn]), null, 'formant', 2).notes[0], less = M.scaleLane(clipOf([sn]), null, 'formant', 0).notes[0];
  check('More/Less scales around the lane default (and generator depth)', Math.abs(more.c.formant[0].v - 0.7) < 1e-9 && Math.abs(more.c.formant[1].v - 0.1) < 1e-9 && Math.abs(more.gen.formant.d1 - 0.8) < 1e-9 && less.c.formant.every(q => q.v === 0.5) && less.gen.formant.d0 === 0,
    `x2: ${more.c.formant.map(q => fx(q.v, 2))} depth ${more.gen.formant.d0}/${more.gen.formant.d1}; x0: ${less.c.formant.map(q => q.v)} depth ${less.gen.formant.d0}`);

  // select similar
  const ss = clipOf([N(60, 0, 1, 's1'), N(62, 1, 0.5, 's2'), N(60, 2, 1, 's3'), N(64, 4, 1, 's4'), N(60, 5, 0.5, 's5'), N(67, 3.5, 2, 's6')]);
  const res = {
    pitch: M.selectSimilar(ss, 's1', 'pitch').join(','), length: M.selectSimilar(ss, 's2', 'length').join(','), bar: M.selectSimilar(ss, 's2', 'bar').join(','),
    everyOther: M.selectSimilar(ss, 's1', 'everyOther', ['s1', 's2', 's3', 's4', 's5']).join(','), invert: M.selectSimilar(ss, 's1', 'invert', ['s1', 's3']).join(','),
  };
  const want = { pitch: 's1,s3,s5', length: 's2,s5', bar: 's1,s2,s3,s6', everyOther: 's1,s3,s5', invert: 's2,s6,s4,s5' };
  check('selectSimilar pitch / length / bar / everyOther / invert', Object.keys(want).every(k => res[k] === want[k]), Object.keys(want).map(k => `${k}: ${res[k]}${res[k] === want[k] ? '' : ' (want ' + want[k] + ')'}`).join('; '));

  // humanize
  const hz = clipOf(Array.from({ length: 64 }, (_, i) => N(60 + (i % 12), i * 0.5, 0.5, 'h' + i)));
  const hp = { time: 0.02, vel: 0.1, lanes: { formant: 0.05 } };
  const h1 = M.humanize(hz, null, hp, 3), h2 = M.humanize(hz, null, hp, 3), h3 = M.humanize(hz, null, hp, 4);
  const dts = h1.notes.map((n, i) => n.s - hz.notes[i].s).filter((_, i) => i > 0), dvs = h1.notes.map((n, i) => n.vel - hz.notes[i].vel), dfm = h1.notes.map(n => n.c.formant[0].v - 0.5);
  const spread = a => Math.max(...a) - Math.min(...a);
  check('humanize: seeded, deterministic, within its amounts, and spread across them', J(h1) === J(h2) && J(h1) !== J(h3) && Math.max(...dts.map(Math.abs)) <= 0.02 && Math.max(...dvs.map(Math.abs)) <= 0.1 + 1e-12 && Math.max(...dfm.map(Math.abs)) <= 0.05 + 1e-12 && spread(dts) > 0.03 && spread(dfm) > 0.08,
    `64 notes: time ${dist(dts, 4)} beat; vel ${dist(dvs, 3)}; formant ${dist(dfm, 3)}; same seed identical ${J(h1) === J(h2)}, seed+1 differs ${J(h1) !== J(h3)}`);
  // strum
  const chord = clipOf([N(67, 0, 2, 'g'), N(60, 0, 2, 'c'), N(64, 0, 2, 'e'), N(72, 0, 2, 'cc')]);
  const up = M.strum(chord, null, '1/16', 'up'), dn = M.strum(chord, null, '1/16', 'down');
  const su = ['c', 'e', 'g', 'cc'].map(id => byId(up, id).s), sd = ['cc', 'g', 'e', 'c'].map(id => byId(dn, id).s), ends = up.notes.map(n => n.s + n.l);
  check('strum up / down, ends kept', J(su) === J([0, 0.25, 0.5, 0.75]) && J(sd) === J([0, 0.25, 0.5, 0.75]) && ends.every(e => Math.abs(e - 2) < 1e-12) && J(M.strum(chord, null, '1/16', 'up')) === J(up),
    `up starts ${su.join(',')}; down starts ${sd.join(',')}; ends ${ends.join(',')}`);
  // legato
  const lg = clipOf([N(60, 0, 0.5, 'l1'), N(64, 0, 0.25, 'l2'), N(62, 1, 0.5, 'l3'), N(65, 3, 2, 'l4'), N(67, 2.5, 1, 'l5')]);
  const lo = M.legato(lg, null), lens = ['l1', 'l2', 'l3', 'l5', 'l4'].map(id => byId(lo, id).l);
  check('legato: each note reaches the next start', J(lens) === J([1, 1, 1.5, 0.5, 2]), `lengths ${lens.join(',')} (want 1,1,1.5,0.5,2)`);
}

// =========================================================================== note settings and lane mode
section('S7 note settings, lane-mode rule');
{
  const c = clipOf([N(60, 0, 1, 'a'), N(62, 1, 1, 'b'), N(64, 2, 1, 'c')]);
  const s1 = M.setNoteSample(c, ['a', 'b'], { motion: 'backward', start: { mode: 'random', range: 0.5 }, tape: true, vel: 0.6, engine: 'cloud', jump: '1/8T' });
  const m3 = M.getMixed(s1, ['a', 'b', 'c']), m2 = M.getMixed(s1, ['a', 'b']);
  check("getMixed: 'mixed' where notes differ", m3.motion === 'mixed' && m3.travel === 'speed' && m3.start.mode === 'mixed' && m3.start.pos === 0 && m3.tape === 'mixed' && m3.vel === 'mixed' && m3.restart === false && m3.count === 3,
    `3 notes: motion ${m3.motion}, travel ${m3.travel}, start.mode ${m3.start.mode}, start.pos ${m3.start.pos}, tape ${m3.tape}, vel ${m3.vel}, restart ${m3.restart}`);
  check('setNoteSample sets every field on the selection', m2.motion === 'backward' && m2.start.mode === 'random' && m2.start.range === 0.5 && m2.tape === true && m2.vel === 0.6 && m2.engine === 'cloud' && m2.jump === '1/8T',
    `2 notes: ${J(m2)}`);
  const s2 = M.setNoteSample(s1, null, { motion: 'mixed', tape: 'yes', jump: '1/7', engine: null, start: { pos: 'mixed' } });
  check("'mixed' and invalid values in a patch are ignored; null = follow the instrument", byId(s2, 'a').sample.motion === 'backward' && byId(s2, 'a').sample.tape === true && byId(s2, 'a').sample.jump === '1/8T' && byId(s2, 'a').sample.engine === null,
    `a: motion ${byId(s2, 'a').sample.motion}, tape ${byId(s2, 'a').sample.tape}, jump ${byId(s2, 'a').sample.jump}, engine ${byId(s2, 'a').sample.engine}`);
  check('getMixed of nothing is null', M.getMixed(c, []) === null, String(M.getMixed(c, [])));

  // setLaneNodes changes nothing but that lane of that note
  const base = M.loadClipPreset('Strand Demo'), id = base.notes[5].id;
  const before = J(base);
  const out = M.setLaneNodes(base, id, 'formant', [{ t: 0, v: 0.1 }, { t: 0.5, v: 0.9, shape: 'bend', bend: 0.3 }, { t: 1, v: 0.2 }]);
  let otherDiff = 0, laneDiff = 0;
  for (let i = 0; i < base.notes.length; i++) {
    const a = base.notes[i], b = out.notes[i];
    if (a.id !== id) { if (J(a) !== J(b)) otherDiff++; continue; }
    for (const L of M.LANE_IDS) if (L !== 'formant' && J(a.c[L]) !== J(b.c[L])) laneDiff++;
    const rest = x => J(Object.assign({}, x, { c: null }));
    if (rest(a) !== rest(b)) laneDiff++;
  }
  check('setLaneNodes changes only that lane of that note (mode-lock rule)', otherDiff === 0 && laneDiff === 0 && J(base) === before && byId(out, id).c.formant[1].shape === 'bend' && out.notes.length === base.notes.length,
    `${base.notes.length} notes: other notes changed ${otherDiff}, other fields of the note changed ${laneDiff}, input unchanged ${J(base) === before}`);
}

// =========================================================================== glides and chains
section('E2 glides and chains');
{
  const inst = M.defaultInst();
  const n = N(60, 0, 1, 'n'), m = N(64, 1, 1, 'm');
  m.c.pitch = [M.mkNode(0, 0.5 + 3 / 24, 'linear'), M.mkNode(0.3, 0.5), M.mkNode(1, 0.5)];
  let g = M.computeGlides(clipOf([n, m]), inst).get('n');
  check('glide aims at the target\'s bent start', g && Math.abs(g.d - 7) < 1e-9 && g.toId === 'm' && !g.linked, `d ${g && g.d} st (target p 64 + 3 bend - 60; v3 aimed at ${64 - 60})`);
  const n2 = M.cloneNote(n); n2.c.pitch = [M.mkNode(0, 0.5), M.mkNode(1, 0.5 + 1 / 24)];
  g = M.computeGlides(clipOf([n2, m]), inst).get('n');
  const endReal = n2.p + semis(n2.c.pitch[1].v) + g.d, startReal = m.p + semis(m.c.pitch[0].v);
  const tn = M.laneTable(n2, 'pitch', 256), tm = M.laneTable(m, 'pitch', 256);
  const tabGap = (n2.p + semis(tn[tn.length - 1]) + g.d) - (m.p + semis(tm[0]));
  check('...and lands there when the source end is bent too', Math.abs(endReal - startReal) < 1e-9 && Math.abs(tabGap) < 1e-4, `source end ${n2.p}+1 st, d ${g.d}: lands ${endReal} vs target start ${startReal}; via tables gap ${fx(tabGap, 6)} st`);
  const z = M.cloneNote(n); z.glide = 0;
  const gz = M.computeGlides(clipOf([z, m]), inst), chz = M.buildChains(clipOf([z, m]), gz);
  check('glide 0 = instant jump (len 0) that still joins the chain', gz.get('n') && gz.get('n').len === 0 && J(chz) === J([['n', 'm']]), `len ${gz.get('n') && gz.get('n').len}, chains ${J(chz)}`);
  const gn = M.computeGlides(clipOf([n, m]), inst).get('n');
  check('glide null = inst.glideTime (capped at 95% of the note)', gn.len === 0.5 && M.computeGlides(clipOf([n, m]), M.mergeInst(inst, { glideTime: 4 })).get('n').len === 0.95, `len ${gn.len} at glideTime 0.5; ${M.computeGlides(clipOf([n, m]), M.mergeInst(inst, { glideTime: 4 })).get('n').len} at 4`);
  const off = M.mergeInst(inst, { glideMode: 'off' });
  const ln = M.cloneNote(n); ln.glideTo = 'far'; const far = N(79, 3, 1, 'far');
  const gOff = M.computeGlides(clipOf([ln, m, far]), off), chOff = M.buildChains(clipOf([ln, m, far]), gOff);
  check('explicit links work with auto glide Off', gOff.size === 1 && gOff.get('n').linked && gOff.get('n').toId === 'far' && Math.abs(gOff.get('n').d - 19) < 1e-9 && chOff.some(ch => J(ch) === J(['n', 'far'])),
    `glides ${J([...gOff].map(([k, v]) => [k, v.toId, v.d, v.linked]))}, chains ${J(chOff)}`);
  const gap = clipOf([N(60, 0, 1, 'g1'), N(62, 3, 1, 'g2')]);
  check('auto Legato reaches 0.13 beat, Always 4 beats', M.computeGlides(gap, inst).size === 0 && M.computeGlides(gap, M.mergeInst(inst, { glideMode: 'always' })).size === 1,
    `2-beat gap: legato ${M.computeGlides(gap, inst).size} glides, always ${M.computeGlides(gap, M.mergeInst(inst, { glideMode: 'always' })).size}`);
  const wide = clipOf([N(60, 0, 1, 'w1'), N(73, 1, 1, 'w2')]), ok12 = clipOf([N(60, 0, 1, 'w1'), N(72, 1, 1, 'w2')]);
  check('auto glides reach +-12 st', M.computeGlides(wide, inst).size === 0 && M.computeGlides(ok12, inst).size === 1, `13 st: ${M.computeGlides(wide, inst).size}, 12 st: ${M.computeGlides(ok12, inst).size}`);
  const vl = clipOf([N(60, 0, 1, 'v1'), N(64, 0, 1, 'v2'), N(67, 0, 1, 'v3'), N(62, 1, 1, 'u1'), N(65, 1, 1, 'u2'), N(69, 1, 1, 'u3')]);
  const gvl = M.computeGlides(vl, inst), map = ['v1', 'v2', 'v3'].map(id => id + '>' + gvl.get(id).toId).join(' ');
  check('auto voice leading by rank', map === 'v1>u1 v2>u2 v3>u3', map);
  // buildChains honours sample.restart
  const r0 = clipOf([N(60, 0, 1, 'x'), N(62, 1, 1, 'y'), N(64, 2, 1, 'z')]);
  const ch0 = M.buildChains(r0, M.computeGlides(r0, inst));
  const r1 = M.setNoteSample(r0, ['y'], { restart: true }), ch1 = M.buildChains(r1, M.computeGlides(r1, inst));
  check('buildChains: a glide joins notes unless the target has Restart sample here', J(ch0) === J([['x', 'y', 'z']]) && J(ch1) === J([['x'], ['y', 'z']]) && M.computeGlides(r1, inst).has('x'),
    `no restart ${J(ch0)}; restart on y ${J(ch1)} (x still bends towards y)`);
  const two = clipOf([N(60, 0, 1, 'lo'), N(67, 0, 1, 'hi'), N(62, 1, 1, 't')]);
  const chT = M.buildChains(two, M.computeGlides(two, inst));
  const twoL = clipOf([N(60, 0, 1, 'lo'), Object.assign(N(67, 0, 1, 'hi'), { glideTo: 't' }), N(62, 1, 1, 't')]);
  const chL = M.buildChains(twoL, M.computeGlides(twoL, inst));
  check('a target joins one source: explicit link first, else the smallest glide', chT.some(c => J(c) === J(['lo', 't'])) && chL.some(c => J(c) === J(['hi', 't'])), `auto ${J(chT)}; with hi>t linked ${J(chL)}`);
  const demo = M.loadClipPreset('Strand Demo'), chD = M.buildChains(demo, M.computeGlides(demo, inst)), flatD = [].concat(...chD);
  check('every note sits in exactly one chain (Strand Demo)', flatD.length === demo.notes.length && new Set(flatD).size === demo.notes.length, `${demo.notes.length} notes, ${chD.length} chains, longest ${Math.max(...chD.map(c => c.length))}`);
}

// =========================================================================== recipes and dice
section('S8 recipes and dice');
{
  const inst = M.defaultInst();
  const clip = clipOf([N(60, 0, 2, 'a'), N(64, 2, 2, 'b'), N(67, 4, 2, 'c')]);
  check('RECIPES 1-8 named as the blueprint', M.RECIPES.map(r => r.n + ' ' + r.name).join(', ') === '1 Straight, 2 Backwards, 3 Frozen choir, 4 Tape scratch, 5 Stutter 1/16, 6 Slow bloom, 7 Scatter rain, 8 Octave shimmer', M.RECIPES.map(r => r.n + ' ' + r.name).join(', '));
  const outs = {};
  let allOk = true; const rows = [];
  for (const R of M.RECIPES) {
    const ci = J(clip), ii = J(inst);
    const o = M.applyRecipe(clip, inst, R.n, null);
    outs[R.n] = o;
    const fieldsOk = Object.keys(R.inst).every(k => J(o.inst[k]) === J(R.inst[k]));
    const valid = J(M.normalizeInst(o.inst)) === J(o.inst);
    const pure = J(clip) === ci && J(inst) === ii;
    const snap = M.compileSnapshot(o.clip, o.inst);
    if (!(fieldsOk && valid && pure && snap.notes.length === 3)) allOk = false;
    rows.push(`${R.n}: ${o.inst.engine}${o.inst.lock ? '+lock' : ''}/${o.inst.motion}/heads ${o.inst.heads}/refresh ${o.inst.refresh.mode}`);
  }
  check('every recipe applies (settings set, inst valid, input untouched, snapshot compiles)', allOk, rows.join('; '));
  const t4 = M.compileSnapshot(outs[4].clip, outs[4].inst, { res: 256 }).notes[0].lanes.travel;
  const sp4 = Array.from(t4, M.travelSpeed), vals = new Set(sp4.map(s => Math.round(s * 1000) / 1000));
  let flips = 0; for (let j = 1; j < sp4.length; j++) if (sp4[j] !== sp4[j - 1]) flips++;
  check('4 Tape scratch: Travel square 1/8 between +1x and -1x', outs[4].inst.engine === 'tape' && J([...vals].sort()) === J([-1, 1]) && flips === 8, `speeds ${J([...vals])}, ${flips} direction changes over beats 0..2 inclusive (want 8: a forward/back cycle per 1/8 note, the 8th at the last sample = next cycle)`);
  const h6 = Array.from(M.compileSnapshot(outs[6].clip, outs[6].inst).notes[0].lanes.heads, v => M.mapLane('heads', v, { inst: outs[6].inst }));
  check('6 Slow bloom: Heads ramp up', Math.abs(h6[0] - 1) < 1e-6 && Math.abs(h6[h6.length - 1] - 8) < 1e-6 && h6[256] > h6[64], `heads ${fx(h6[0], 2)} -> ${fx(h6[256], 2)} -> ${fx(h6[h6.length - 1], 2)}`);
  const s7 = M.compileSnapshot(outs[7].clip, outs[7].inst).notes[0];
  check('7 Scatter rain: Cloud, Scatter, jump 1/8T', s7.engine === 'cloud' && s7.motion === 'scatter' && Math.abs(s7.jump - 1 / 3) < 1e-12, `${s7.engine}/${s7.motion}/jump ${fx(s7.jump, 4)} beat`);
  const s8 = M.compileSnapshot(outs[8].clip, outs[8].inst).notes[0];
  check('8 Octave shimmer: Cloud + Lock, octave heads', s8.engine === 'cloud' && s8.lock === true && outs[8].inst.headTune === 'octaves', `${s8.engine} lock ${s8.lock} headTune ${outs[8].inst.headTune}`);
  check('5 Stutter 1/16: synced refresh 1/16', outs[5].inst.refresh.mode === 'sync' && outs[5].inst.refresh.rate === '1/16', J(outs[5].inst.refresh));
  const back = M.applyRecipe(outs[4].clip, outs[4].inst, 1, null);
  check('1 Straight resets the Sample lanes and clears their generators', back.clip.notes.every(n => n.gen.travel === null && J(n.c.travel) === J(M.flat(0.75))) && back.inst.engine === 'voice' && back.inst.motion === 'forward', `travel gen ${back.clip.notes[0].gen.travel}, engine ${back.inst.engine}`);
  const only = M.applyRecipe(clip, inst, 4, ['b']);
  check('a recipe with ids touches only those notes', only.clip.notes.filter(n => n.gen.travel).map(n => n.id).join(',') === 'b', only.clip.notes.filter(n => n.gen.travel).map(n => n.id).join(','));

  // dice
  const d1 = M.dice(inst, 5), d2 = M.dice(inst, 5);
  let differ = 0; for (let s = 1; s <= 100; s++) if (J(M.dice(inst, s)) !== J(M.dice(inst, s + 1))) differ++;
  check('dice: same seed identical, seed+1 different', J(d1) === J(d2) && differ === 100, `identical ${J(d1) === J(d2)}; ${differ}/100 consecutive seeds differ`);
  const pins = ['engine', 'heads', 'refresh', 'grain'];
  const start = M.mergeInst(inst, { engine: 'tape', heads: 7, refresh: { mode: 'free', ms: 333 }, grainCloud: 999, grainVoice: 9 });
  let pinBreak = 0, indep = 0; const engines = {}, motions = {};
  for (let s = 1; s <= 200; s++) {
    const p = M.dice(start, s, pins), u = M.dice(start, s);
    if (p.engine !== 'tape' || p.heads !== 7 || J(p.refresh) !== J(M.normalizeInst(start).refresh) || p.grainCloud !== 999 || p.grainVoice !== 9) pinBreak++;
    // lock and motion depend on the (pinned) engine and grainLock is covered by the 'grain' pin, so they are left out
    for (const k of ['speed', 'spread', 'spray', 'tape', 'rev', 'drift', 'headTune', 'jump']) if (J(p[k]) !== J(u[k])) indep++;
    engines[u.engine] = (engines[u.engine] || 0) + 1; motions[u.motion] = (motions[u.motion] || 0) + 1;
  }
  check('dice: pinned fields stay, other fields roll as without pins', pinBreak === 0 && indep === 0, `200 seeds: pin breaks ${pinBreak}, unpinned fields that changed because of pins ${indep}`);
  let limits = 0;
  for (let s = 1; s <= 500; s++) {
    const d = M.dice(inst, s);
    if (d.heads < 1 || d.heads > 6 || d.spread > 0.6 || d.spray > 0.5 || d.drift > 0.4 || d.grainCloud < 40 || d.grainCloud > 400 || (d.lock && d.engine !== 'cloud') || (d.engine === 'tape' && d.motion === 'freeze')) limits++;
    if (J(d.region) !== J(inst.region) || J(d.macros) !== J(inst.macros)) limits++;
  }
  check('dice stays within musical limits (and leaves Region and macros alone)', limits === 0, `${limits} out-of-limit rolls in 500; engines ${J(engines)}; motions ${J(motions)}`);
}

// =========================================================================== snapshot
section('3.2 compileSnapshot');
{
  const inst = M.defaultInst();
  const demo = M.loadClipPreset('Strand Demo');
  const t0 = process.hrtime.bigint();
  const s1 = M.compileSnapshot(demo, inst);
  const ms = Number(process.hrtime.bigint() - t0) / 1e6;
  const s2 = M.compileSnapshot(JSON.parse(JSON.stringify(demo)), JSON.parse(JSON.stringify(inst)));
  check('compileSnapshot is deterministic', J(s1) === J(s2), `two compiles (one from a JSON copy) identical: ${J(s1) === J(s2)}; ${s1.notes.length} notes in ${fx(ms, 1)} ms`);
  const nulls = [];
  const walkObj = (o, p) => {
    for (const k of Object.keys(o)) {
      const v = o[k], q = p + '.' + k;
      if (v === null) { if (!/\.glide$|\.prevId$|\.nextId$/.test(q)) nulls.push(q); }
      else if (v === undefined || (typeof v === 'number' && !Number.isFinite(v))) nulls.push(q + '=' + v);
      else if (ArrayBuffer.isView(v)) { for (const x of v) if (!Number.isFinite(x) || x < 0 || x > 1) { nulls.push(q + ' value ' + x); break; } }
      else if (typeof v === 'object') walkObj(v, q);
    }
  };
  const odd = M.loadClipPreset('Scatter 1/8T'); odd.notes[0].sample.lock = null; odd.notes[1].sample.engine = null;
  const s3 = M.compileSnapshot(odd, M.mergeInst(inst, { engine: 'tape', motion: 'bounce' }));
  s1.notes.concat(s3.notes).forEach((n, i) => walkObj(n, 'notes[' + i + ']'));
  check('compileSnapshot resolves every null (sample fields follow inst)', !nulls.length && s3.notes[1].engine === 'tape' && s3.notes[1].lock === false && s1.notes[0].motion === 'forward' && s1.notes.every(n => typeof n.tape === 'boolean' && typeof n.rev === 'boolean' && typeof n.jump === 'number'),
    nulls.length ? nulls.slice(0, 5).join(', ') : `no nulls outside glide/prevId/nextId; engine null -> ${s3.notes[1].engine}, motion null -> ${s1.notes[0].motion}, jump '1/8' -> ${s1.notes[0].jump}`);
  check('snapshot shape', s1.version === M.SNAPSHOT_VERSION && s1.bars === 4 && s1.beatsPerBar === 4 && s1.res === 256 && s1.notes.every((n, i) => i === 0 || n.s >= s1.notes[i - 1].s) && s1.notes.every(n => n.idHash === M.idHash(n.id) && Object.keys(n.lanes).join(',') === M.LANE_IDS.join(',')),
    `version ${s1.version}, bars ${s1.bars}, res ${s1.res}, notes sorted by start, idHash = FNV-1a(id), 10 lanes each`);
  let lenBad = 0, lenN = 0;
  for (const res of [32, 64, 256, 1000]) for (const l of [0.0625, 1 / 6, 0.5, 1, 4.3, 16, 0.001]) {
    const c = clipOf([N(60, 0, l, 'L')]); const sn = M.compileSnapshot(c, inst, { res });
    for (const id of M.LANE_IDS) { lenN++; if (sn.notes[0].lanes[id].length !== Math.max(2, Math.ceil(l * res) + 1)) lenBad++; }
  }
  check('table length = max(2, ceil(l*res)+1)', lenBad === 0, `${lenN} tables (4 res x 7 lengths x 10 lanes), ${lenBad} wrong`);
  const gnote = N(60, 0.5, 2, 'gen'); gnote.gen.tone = M.makeGen('pattern', { shape: 'triangle', rate: '1/8', d0: 0.6, d1: 0.6 }); gnote.c.tone = M.flat(0.5);
  const plain = M.cloneNote(gnote); plain.gen.tone = null;
  const sg = M.compileSnapshot(clipOf([gnote]), inst).notes[0].lanes.tone, sp = M.compileSnapshot(clipOf([plain]), inst).notes[0].lanes.tone;
  check('generators are baked into the tables', maxAbsDiff(sg, M.laneTable(gnote, 'tone', 256)) === 0 && maxAbsDiff(sg, sp) > 0.25, `table = laneTable (drawing + generator): diff ${maxAbsDiff(sg, M.laneTable(gnote, 'tone', 256))}; vs no generator ${fx(maxAbsDiff(sg, sp), 3)}`);
  const sm = M.compileSnapshot(demo, M.mergeInst(inst, { macros: [0.9, 0.1, 1, 0, 0.3, 0.7, 0.2, 0.8], offsets: { tone: -0.5 }, speed: 3 }));
  check('macros, offsets and inst knobs are not baked', J(sm.notes.map(n => n.lanes)) === J(s1.notes.map(n => n.lanes)), 'lane tables identical with every macro moved');
  let chainBad = 0;
  const byIdS = new Map(s1.notes.map(n => [n.id, n]));
  for (const n of s1.notes) {
    const c = n.chain; if (!c || typeof c.id !== 'string' || typeof c.index !== 'number') { chainBad++; continue; }
    if (c.nextId && byIdS.get(c.nextId).chain.prevId !== n.id) chainBad++;
    if (c.prevId && byIdS.get(c.prevId).chain.nextId !== n.id) chainBad++;
    if (c.index === 0 && (c.prevId || c.id !== n.id)) chainBad++;
    if (n.glide && !byIdS.has(n.glide.toId)) chainBad++;
  }
  const linked = s1.notes.filter(n => n.chain.nextId).length;
  check('chain fields are consistent', chainBad === 0 && linked > 0, `${chainBad} inconsistencies; ${linked} notes hand over to a next note; ${s1.notes.filter(n => n.glide).length} glides`);
  const lead = byIdS.get('sd-25');
  check('Strand Demo lead glides into its linked note', lead && lead.glide && lead.glide.toId === 'sd-26' && Math.abs(lead.glide.len - 1) < 1e-12 && lead.chain.nextId === 'sd-26', lead ? J(lead.glide) + ' ' + J(lead.chain) : 'missing');
}

// =========================================================================== history
section('History');
{
  const c0 = M.newClip(4), a = M.addNote(c0, 60, 0, 1), b = M.addNote(a.clip, 64, 1, 1), cc = M.addNote(b.clip, 67, 2, 1);
  const h = new M.History(c0);
  h.push(a.clip, 'add C3'); h.push(b.clip, 'add E3'); h.push(cc.clip, 'add G3');
  const pushedSame = h.push(cc.clip, 'same again');
  const u1 = h.undo(), u2 = h.undo(), r1 = h.redo();
  const ok1 = J(u1) === J(b.clip) && J(u2) === J(a.clip) && J(r1) === J(b.clip) && !pushedSame;
  u1.notes.push('junk');
  const r2 = h.redo();
  const ok2 = J(r2) === J(cc.clip);
  h.undo(); h.push(M.addNote(b.clip, 50, 3, 1).clip, 'branch');
  const ok3 = !h.canRedo() && h.canUndo();
  let n = 0; while (h.canUndo()) { h.undo(); n++; }
  const atStart = J(h.current()) === J(c0);
  check('History undo/redo round trip', ok1 && ok2 && ok3 && atStart && n === 3 && h.undo() === null, `undo,undo,redo ok ${ok1}; returned copies are safe ${ok2}; push after undo drops redo ${ok3}; ${n} undos back to start ${atStart}; duplicate push ignored ${!pushedSame}`);
  const big = new M.History(c0);
  let clip = c0; for (let i = 0; i < 250; i++) { clip = M.addNote(clip, 60, i % 16, 0.25).clip; big.push(clip, 'n' + i); }
  let undos = 0; while (big.canUndo()) { big.undo(); undos++; }
  check('History keeps 200 steps', undos === 200 && big.current().notes.length === 50, `250 pushes -> ${undos} undos possible, oldest kept has ${big.current().notes.length} notes`);
  // memory: a 1024-note clip, 200 one-note edits
  {
    let bigClip = M.chop(clipOf(Array.from({ length: 64 }, (_, i) => N(48 + (i % 24), (i % 4) * 4, 4, 'big' + i)), 8), null, '1/16', 0).clip;
    const hm = new M.History(bigClip), one = J(bigClip).length;
    const t0 = process.hrtime.bigint();
    for (let i = 0; i < 200; i++) { const id = bigClip.notes[(i * 37) % bigClip.notes.length].id; bigClip = M.setLaneNodes(bigClip, id, 'tone', [{ t: 0, v: (i % 10) / 10 }, { t: 1, v: 1 }]); hm.push(bigClip, 'edit ' + i); }
    const ms = Number(process.hrtime.bigint() - t0) / 1e6;
    const held = hm.storedChars(), naive = one * 201;
    const back = []; while (hm.canUndo()) back.push(hm.undo());
    check('History memory: unchanged notes are shared between versions', held < 2 * one && back.length === 200 && J(back[back.length - 1]) === J(hm.current()),
      `${bigClip.notes.length}-note clip (${(one / 1e6).toFixed(2)} MB text): 201 versions hold ${(held / 1e6).toFixed(2)} MB of unique text vs ${(naive / 1e6).toFixed(0)} MB as whole copies; 200 pushes in ${fx(ms, 0)} ms (${fx(ms / 200, 1)} ms each)`);
  }
  const hs = new M.History({ clip: c0, inst: M.defaultInst() });
  hs.push({ clip: c0, inst: M.dice(M.defaultInst(), 3) }, 'dice');
  check('History also holds { clip, inst } steps (Re-roll / Dice)', J(hs.undo().inst) === J(M.defaultInst()) && hs.undoLabel() === null && hs.redoLabel() === 'dice', 'dice undone');
}

// =========================================================================== editing helpers
section('editing helpers, setGen / clearGen, re-roll of random starts, speed');
{
  const c0 = clipOf([N(60, 0, 1, 'a'), Object.assign(N(62, 1, 1, 'b'), { glideTo: 'c' }), N(64, 2, 1, 'c')]);
  const ad = M.addNote(c0, 67, 3, 1, { motion: 'scatter', vel: 0.5 }), added = byId(ad.clip, ad.id);
  const rm = M.removeNotes(c0, ['c']);
  const up = M.updateNotes(c0, ['a'], { p: 61, s: 0.5, l: -3 });
  const du = M.duplicateNotes(c0, ['b', 'c']), dupB = du.clip.notes.find(n => n.id === du.ids[0]);
  check('addNote / removeNotes / updateNotes / duplicateNotes', added && added.sample.motion === 'scatter' && added.vel === 0.5 && ad.clip.notes.length === 4 && !c0.notes.some(n => n.id === ad.id) &&
    rm.notes.length === 2 && byId(rm, 'b').glideTo === null && byId(up, 'a').p === 61 && byId(up, 'a').l === 1e-3 && du.ids.length === 2 && dupB.s === 3 && dupB.glideTo === du.ids[1],
    `add -> ${ad.id} (${added.sample.motion}); remove c clears b's link: ${byId(rm, 'b').glideTo}; update l -3 -> ${byId(up, 'a').l}; duplicate b,c -> ${du.ids.join(',')} at ${du.clip.notes.slice(3).map(n => n.s).join(',')}, link remapped ${dupB.glideTo === du.ids[1]}`);
  const g = M.makeGen('random', { rate: '1/8' });
  const sg = M.setGen(c0, ['a', 'b'], 'vib', g), cg = M.clearGen(sg, ['a'], 'vib'), ca = M.clearGen(sg, null, null);
  check('setGen / clearGen', J(byId(sg, 'a').gen.vib) === J(g) && byId(sg, 'c').gen.vib === null && byId(cg, 'a').gen.vib === null && byId(cg, 'b').gen.vib !== null && ca.notes.every(n => M.LANE_IDS.every(l => n.gen[l] === null)),
    `set on a,b; cleared on a; clear all lanes of all notes`);
  const rs = M.setNoteSample(c0, ['a'], { start: { mode: 'random', range: 0.3 } });
  const r1 = M.reroll(rs, null, null), r2 = M.reroll(rs, null, 'vib');
  check('re-roll bumps the seed of random Starts (lane null only)', byId(r1, 'a').sample.start.seed === 2 && byId(r1, 'b').sample.start.seed === 1 && byId(r2, 'a').sample.start.seed === 1 && M.compileSnapshot(r1, M.defaultInst()).notes[0].start.seed === 2,
    `a: ${byId(rs, 'a').sample.start.seed} -> ${byId(r1, 'a').sample.start.seed} (lane null), ${byId(r2, 'a').sample.start.seed} (lane vib); b (region start) stays ${byId(r1, 'b').sample.start.seed}`);
  // compile speed on a dense clip
  let dense = M.chop(clipOf(Array.from({ length: 64 }, (_, i) => N(48 + (i % 24), (i % 16), 1, 'd' + i)), 4), null, '1/16', 0).clip;
  dense = M.setGen(dense, null, 'tone', M.makeGen('pattern', { shape: 'squiggle', rate: '1/32' }));
  const long = clipOf(Array.from({ length: 128 }, (_, i) => N(36 + (i % 48), (i % 4) * 4, 4, 'L' + i)).map(n => { n.gen.formant = M.makeGen('random', { rate: '1/16', smooth: 0.5 }); return n; }), 4);
  const time = f => { const t0 = process.hrtime.bigint(); f(); return Number(process.hrtime.bigint() - t0) / 1e6; };
  M.compileSnapshot(dense, M.defaultInst());
  const tDense = time(() => M.compileSnapshot(dense, M.defaultInst())), tLong = time(() => M.compileSnapshot(long, M.defaultInst()));
  const demo = M.loadClipPreset('Strand Demo'), tDemo = time(() => M.compileSnapshot(demo, M.defaultInst()));
  info(`compileSnapshot speed (res 256): Strand Demo (26 notes) ${fx(tDemo, 1)} ms; ${dense.notes.length} chopped 1/16 notes with a tone pattern ${fx(tDense, 1)} ms; 128 notes x 4 beats with random formant ${fx(tLong, 1)} ms`);
  check('compileSnapshot fast enough to run on every edit', tDemo < 50 && tLong < 500, `Strand Demo ${fx(tDemo, 1)} ms (limit 50), 128 x 4-beat notes ${fx(tLong, 1)} ms (limit 500)`);
}

// =========================================================================== presets and loading
section('U4 presets, loading v3 clips');
{
  const names = M.CLIP_PRESETS.map(p => p.name).join(', ');
  check('clip presets (blueprint list, in order)', names === 'Strand Demo, Pad Swell, Pluck Decay, Vowel Morph, Legato Lead, Hollow Drone, Saw Mirror Pad, Backward Vowel, Scatter 1/8T, Tape Stop Lead, Bloom, Walk Through', names);
  let bad = [];
  for (const p of M.CLIP_PRESETS) {
    const c = M.loadClipPreset(p.id);
    const snap = M.compileSnapshot(c, M.defaultInst());
    if (!c.notes.length || J(M.normalizeClip(c)) !== J(c) || snap.notes.length !== c.notes.length || new Set(c.notes.map(n => n.id)).size !== c.notes.length || c.notes.some(n => n.s + n.l > c.bars * 4 + 1e-9)) bad.push(p.name);
  }
  check('every clip preset loads, is normalized, fits its bars and compiles', !bad.length, `${M.CLIP_PRESETS.length} presets${bad.length ? '; bad: ' + bad.join(', ') : ''}`);
  const d1 = M.loadClipPreset('Strand Demo'); d1.notes[0].p = 1; d1.notes.length = 2;
  const d2 = M.loadClipPreset('Strand Demo');
  check('loadClipPreset returns a fresh copy', d2.notes.length === 26 && d2.notes[0].p === 55, `after mutating one copy: next load has ${d2.notes.length} notes, first p ${d2.notes[0].p}`);
  const v3 = [[55, 0, 4], [60, 0, 4], [64, 0, 4], [67, 0, 4], [57, 4, 4], [60, 4, 4], [64, 4, 4], [69, 4, 4], [36, 0, 4], [33, 4, 4]]
    .concat([72, 76, 79, 76, 74, 72, 71, 74].map((p, k) => [p, 8.5 + k * 0.5, 0.5])).concat([[55, 12, 4], [59, 12, 4], [62, 12, 4], [67, 12, 4], [31, 8, 4], [31, 12, 4], [84, 6, 2], [79, 10, 2]]);
  const got = d2.notes.map(n => [n.p, n.s, n.l]);
  check('Strand Demo = v3 demo() notes', J(got) === J(v3) && byId(d2, 'sd-25').glideTo === 'sd-26' && byId(d2, 'sd-25').glide === 1, `${got.length} notes, same pitches/starts/lengths ${J(got) === J(v3)}`);
  const wt = M.loadClipPreset('Walk Through'), wpos = wt.notes.slice().sort((a, b) => a.s - b.s).map(n => n.sample.start.pos);
  check('Walk Through walks the Start through the sample', wpos.length === 16 && wpos.every((p, i) => i === 0 || p > wpos[i - 1]) && wpos[0] === 0, `start ${wpos.map(p => fx(p, 3)).join(',')}`);
  let soundIds = ['sawopen', 'saw', 'square', 'pad', 'vowel', 'pluck', 'beat', 'noise'], src = 'blueprint 4';
  try { const S = require(path.join(__dirname, '..', 'src', 'sl-sounds.js')); if (S && Array.isArray(S.list)) { soundIds = S.list.map(x => x.id); src = 'SLSounds.list'; } } catch (e) { /* other builder's file not ready */ }
  const sp = M.SOUND_PRESETS.map(p => p.sound);
  check('sound presets use the starter sounds', sp.every(s => soundIds.includes(s)) && sp.length === 8 && sp[0] === 'sawopen', `${sp.join(',')} vs ${src}`);
  const inst = M.defaultInst(), clip = M.loadClipPreset('Pad Swell'), cj = J(clip);
  const sres = M.applySoundPreset(inst, 'noise-lock');
  const lres = M.loadClipPreset('Bloom');
  check('sound and clip presets stay separate', Object.keys(sres).join(',') === 'sound,inst' && sres.inst.lock === true && J(clip) === cj && !('inst' in lres) && J(inst) === J(M.defaultInst()), `applySoundPreset -> {${Object.keys(sres)}}, loadClipPreset -> {${Object.keys(lres)}}`);
  // a v3 saved clip loads
  const v3clip = [{ id: 'abc', p: 60, s: 0, l: 2, glide: null, gc: 0, glideTo: 'gone', c: { pitch: [{ t: 0, v: 0.5 }, { t: 1, v: 0.6, lin: true }], tone: [{ t: 0, v: 0.2, lin: true }, { t: 1, v: 0.9 }] } }, { p: 62, s: 2, l: 1, c: {} }];
  const lc = M.normalizeClip(v3clip), q = lc.notes[0];
  check('v3 notes load (lin -> linear, missing lanes default, dangling link dropped, ids made)', q.c.tone[0].shape === 'linear' && q.c.travel.length === 2 && q.c.travel[0].v === 0.75 && q.c.tone.length === 2 && q.vel === 0.8 && q.glideTo === null && q.sample.travel === 'speed' && typeof lc.notes[1].id === 'string' && J(M.normalizeClip(v3clip)) === J(lc),
    `tone[0] ${q.c.tone[0].shape}, travel ${J(q.c.travel.map(x => x.v))}, vel ${q.vel}, glideTo ${q.glideTo}, made id ${lc.notes[1].id}`);
}

// =========================================================================== purity
section('purity: every pure function leaves its input unchanged');
{
  const inst = deepFreeze(M.defaultInst());
  const base = M.loadClipPreset('Strand Demo');
  base.notes[3].gen.tone = M.makeGen('random', { rate: '1/16', amount: 0.4 });
  base.notes[4].sample.start.mode = 'random';
  const clip = deepFreeze(base), before = J(clip), ids = clip.notes.slice(0, 8).map(n => n.id), id0 = ids[0];
  const calls = {
    applyShapeEach: () => M.applyShape(clip, ids, 'tone', 'swell', 'each'),
    applyShapeAcross: () => M.applyShape(clip, ids, 'travel', 'tapestop', 'across'),
    setGen: () => M.setGen(clip, ids, 'formant', M.makeGen('pattern', {})),
    clearGen: () => M.clearGen(clip, ids, null),
    bakeGen: () => M.bakeGen(clip, null, 'tone'),
    reroll: () => M.reroll(clip, null, null),
    connect: () => M.connect(clip, null, 'random', 2),
    disconnect: () => M.disconnect(clip, null),
    walk: () => M.walk(clip, ids, 'start', 0, 1),
    chop: () => M.chop(clip, ids, '1/8', 0.1).clip,
    reverseLane: () => M.reverseLane(clip, ids, 'amp'),
    flipLane: () => M.flipLane(clip, ids, 'tone'),
    scaleLane: () => M.scaleLane(clip, ids, 'tone', 1.5),
    humanize: () => M.humanize(clip, null, { time: 0.01, vel: 0.1, lanes: 0.05 }, 1),
    strum: () => M.strum(clip, null, '1/32', 'down'),
    legato: () => M.legato(clip, null),
    setNoteSample: () => M.setNoteSample(clip, ids, { motion: 'bounce', start: { pos: 0.3 } }),
    setLaneNodes: () => M.setLaneNodes(clip, id0, 'hollow', [{ t: 0, v: 1 }, { t: 1, v: 0 }]),
    setLaneNodesMany: () => M.setLaneNodesMany(clip, 'hollow', { [id0]: [{ t: 0, v: 1 }, { t: 1, v: 0 }] }),
    applyRecipe: () => M.applyRecipe(clip, inst, 4, ids).clip,
    addNote: () => M.addNote(clip, 61, 1, 1).clip,
    removeNotes: () => M.removeNotes(clip, ids),
    updateNotes: () => M.updateNotes(clip, ids, { p: 40 }),
    duplicateNotes: () => M.duplicateNotes(clip, ids).clip,
    normalizeClip: () => M.normalizeClip(clip),
    compileSnapshot: () => M.compileSnapshot(clip, inst),
    computeGlides: () => M.computeGlides(clip, inst),
    buildChains: () => M.buildChains(clip, M.computeGlides(clip, inst)),
    getMixed: () => M.getMixed(clip, ids),
    selectSimilar: () => M.selectSimilar(clip, id0, 'everyOther', ids),
    laneTable: () => M.laneTable(clip.notes[3], 'tone', 256),
    dice: () => M.dice(inst, 9),
    applySoundPreset: () => M.applySoundPreset(inst, 'pad-drift').inst,
  };
  const broke = [], same = [], changedNothing = [];
  for (const [name, fn] of Object.entries(calls)) {
    let out;
    try { out = fn(); } catch (e) { broke.push(name + ' threw ' + e.message); continue; }
    if (J(clip) !== before || J(inst) !== J(M.defaultInst())) broke.push(name + ' changed its input');
    if (out === clip) same.push(name);
    if (out && out.notes && J(out) === before && !['disconnect', 'normalizeClip', 'clearGen', 'legato'].includes(name)) changedNothing.push(name);
  }
  check('no function writes to its (deep-frozen) input clip or inst', !broke.length, `${Object.keys(calls).length} functions${broke.length ? '; ' + broke.join('; ') : ''}`);
  check('clip functions return a new clip object', !same.length, same.length ? same.join(',') : 'all new');
  check('...and the edits really happen (no silent no-ops)', !changedNothing.length, changedNothing.length ? 'unchanged: ' + changedNothing.join(',') : 'every editing call changed its output');
}

console.log(`\n${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
