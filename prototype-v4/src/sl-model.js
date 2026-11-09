/* StrandLock prototype v4: sl-model.js
   Global: SLModel. The data model, lanes, generators, every selection tool, glides and chains, undo,
   recipes, presets and the snapshot compiler. No DOM, no audio, no Math.random: every random value is
   a seeded hash (BLUEPRINT 1.6). Note names follow Cubase: MIDI 60 = C3.

   Clips are treated as immutable. Every function that takes a clip returns a NEW clip and never writes
   to its input. Untouched notes are shared between the old and the new clip, so callers must not
   mutate a clip (or a note inside one) after handing it to SLModel or to History. */
var SLModel = (function () {
  'use strict';

  // ======================================================================== constants
  const VERSION = '4.0.0';
  const SNAPSHOT_VERSION = 1;
  const MAX_NODES = 128;
  const BEATS_PER_BAR = 4;
  const MIN_LEN = 0.0625;                // shortest note the tools make: a 1/64 note, in beats
  const PITCH_GEN_SCALE = 1 / 6;         // generator depth 1 on the pitch lane = +-2 semitones
  const BAKE_EPS = 0.004;                // Turn into nodes: RDP tolerance
  const NODE_SHAPES = ['smooth', 'linear', 'bend', 'step'];
  const ENGINES = ['tape', 'voice', 'cloud'];
  const ENGINE_NAMES = { tape: 'Tape', voice: 'Voice', cloud: 'Cloud' };
  const MOTIONS = ['forward', 'backward', 'bounce', 'mirror', 'freeze', 'scatter'];
  const START_MODES = ['region', 'carry', 'song', 'random'];
  const TRAVEL_KINDS = ['speed', 'path'];
  const HEAD_TUNES = ['same', 'detune', 'octaves', 'fifths'];
  const AT_END = ['loop', 'hold', 'stop'];
  const REFRESH_MODES = ['smooth', 'free', 'sync'];
  const GLIDE_MODES = ['off', 'legato', 'always'];
  const GEN_KINDS = ['pattern', 'random'];
  const GEN_SHAPES = ['squiggle', 'triangle', 'sawup', 'sawdown', 'square', 'randsteps', 'smoothrandom'];
  const MACRO_NAMES = ['Speed', 'Spread', 'Heads', 'Refresh', 'Grain', 'Spray', 'Tone', 'Formant'];
  const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const SCALES = {
    'Chromatic': [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11], 'Major': [0, 2, 4, 5, 7, 9, 11], 'Minor': [0, 2, 3, 5, 7, 8, 10],
    'Dorian': [0, 2, 3, 5, 7, 9, 10], 'Mixolydian': [0, 2, 4, 5, 7, 9, 10], 'Harm. minor': [0, 2, 3, 5, 7, 8, 11],
    'Pent. major': [0, 2, 4, 7, 9], 'Pent. minor': [0, 3, 5, 7, 10],
  };

  // ======================================================================== small helpers
  const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
  const num = (x, d) => (typeof x === 'number' && Number.isFinite(x) ? x : d);
  const has = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
  const boolOrNull = x => (x === true || x === false ? x : null);
  function deepFreeze(o) {
    if (o && typeof o === 'object' && !Object.isFrozen(o)) { Object.freeze(o); for (const k of Object.keys(o)) deepFreeze(o[k]); }
    return o;
  }
  function deepClone(x) { return x === undefined ? undefined : JSON.parse(JSON.stringify(x)); }
  const noteName = p => NAMES[((Math.round(p) % 12) + 12) % 12] + (Math.floor(Math.round(p) / 12) - 2);   // 60 -> C3
  const inScale = (p, key, scale) => (SCALES[scale] || SCALES.Chromatic).includes((((Math.round(p) - (key | 0)) % 12) + 12) % 12);
  function snapScale(p, key, scale) {
    if (inScale(p, key, scale)) return p;
    for (let d = 1; d < 7; d++) { if (inScale(p - d, key, scale)) return p - d; if (inScale(p + d, key, scale)) return p + d; }
    return p;
  }

  // ======================================================================== seeds (BLUEPRINT 1.6)
  // hash32(...ints): MurmurHash3 x86_32 over 32-bit integers (seed 0x9747b28c, length = 4 bytes per int),
  // finished with fmix32. Each argument is taken as (x | 0). rand01(h) = h / 2^32.
  function fmix32(h) {
    h ^= h >>> 16; h = Math.imul(h, 0x85ebca6b);
    h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35);
    h ^= h >>> 16;
    return h >>> 0;
  }
  function hash32() {
    let h = 0x9747b28c | 0;
    for (let i = 0; i < arguments.length; i++) {
      let k = Math.imul(arguments[i] | 0, 0xcc9e2d51);
      k = (k << 15) | (k >>> 17);
      k = Math.imul(k, 0x1b873593);
      h ^= k;
      h = (h << 13) | (h >>> 19);
      h = (Math.imul(h, 5) + 0xe6546b64) | 0;
    }
    h ^= arguments.length * 4;
    return fmix32(h);
  }
  function rand01(h) { return (h >>> 0) / 4294967296; }
  // FNV-1a 32-bit over the UTF-16 code units of the id string.
  function idHash(id) {
    const s = String(id);
    let h = 0x811c9dc5;
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193); }
    return h >>> 0;
  }

  // ======================================================================== lanes (BLUEPRINT 1.2)
  const SAND = '#E2C275';
  const LANES = [
    { id: 'pitch', key: '2', mode: 2, name: 'Pitch', def: 0.5, group: 'pitch', color: '#2ECF98', colorDark: '#3EE6A8', colorLight: '#0B9E74', sub: false },
    { id: 'amp', key: '3', mode: 3, name: 'Volume', def: 0.8, group: 'level', color: '#4F8BFF', sub: false },
    { id: 'vib', key: '4', mode: 4, name: 'Vibrato', def: 0, group: 'pitch', color: '#E05CFF', sub: false },
    { id: 'tone', key: '5', mode: 5, name: 'Tone', def: 1.0, group: 'tone', color: '#FF9F1C', sub: false },
    { id: 'formant', key: '6', mode: 6, name: 'Formant', def: 0.5, group: 'tone', color: '#FF4D8D', sub: false },
    { id: 'hollow', key: '7', mode: 7, name: 'Hollow', def: 0, group: 'tone', color: '#22C3E6', sub: false },
    { id: 'travel', key: '8', mode: 8, name: 'Travel', def: 0.75, group: 'sample', color: SAND, sub: true },
    { id: 'heads', key: '8', mode: 8, name: 'Heads', def: 0.5, group: 'sample', color: SAND, sub: true },
    { id: 'grain', key: '8', mode: 8, name: 'Grain', def: 0.5, group: 'sample', color: SAND, sub: true },
    { id: 'refresh', key: '8', mode: 8, name: 'Refresh', def: 0.5, group: 'sample', color: SAND, sub: true },
  ];
  LANES.forEach(Object.freeze); Object.freeze(LANES);
  const LANE_IDS = Object.freeze(LANES.map(L => L.id));
  const LANE = Object.freeze(Object.fromEntries(LANES.map(L => [L.id, L])));
  const SAMPLE_LANES = Object.freeze(['travel', 'heads', 'grain', 'refresh']);
  const MODES = Object.freeze([
    { mode: 1, key: '1', name: 'Build', lanes: [] }, ...LANES.filter(L => !L.sub).map(L => ({ mode: L.mode, key: L.key, name: L.name, lanes: [L.id] })),
    { mode: 8, key: '8', name: 'Sample', lanes: SAMPLE_LANES.slice() },
  ].map(Object.freeze));
  function laneIndex(id) { const i = LANE_IDS.indexOf(id); if (i < 0) throw new Error('SLModel: unknown lane ' + id); return i; }
  function laneDef(id) { return LANE[id] ? LANE[id].def : 0.5; }

  // ======================================================================== rates (BLUEPRINT 1.5)
  const RATE_BASES = [['1bar', 4], ['1/2', 2], ['1/4', 1], ['1/8', 0.5], ['1/16', 0.25], ['1/32', 0.125], ['1/64', 0.0625]];
  const RATE_SUFFIX = [['', 1], ['T', 2 / 3], ['D', 1.5]];
  const RATES = [];
  const RATE_BEATS = {};
  for (const [b, beats] of RATE_BASES) for (const [s, f] of RATE_SUFFIX) { RATES.push(b + s); RATE_BEATS[b + s] = beats * f; }
  Object.freeze(RATES); Object.freeze(RATE_BEATS);
  const isRate = id => typeof id === 'string' && has(RATE_BEATS, id);
  // Beats per rate step. A positive number passes through as beats; an unknown id reads as 1/16.
  function rateBeats(id) {
    if (typeof id === 'number' && Number.isFinite(id) && id > 0) return id;
    return isRate(id) ? RATE_BEATS[id] : 0.25;
  }
  function rateLabel(id) {
    if (!isRate(id)) return String(id);
    const s = id[id.length - 1], base = s === 'T' || s === 'D' ? id.slice(0, -1) : id;
    const name = base === '1bar' ? '1 bar' : base;   // (integrator) '1barT' read '1bar triplet'
    return s === 'T' ? name + ' triplet' : s === 'D' ? name + ' dotted' : name;
  }
  function rateFromBeats(beats, tol) {
    let best = null, be = Infinity;
    for (const id of RATES) { const e = Math.abs(RATE_BEATS[id] / beats - 1); if (e < be) { be = e; best = id; } }
    return be <= (tol == null ? 0.01 : tol) ? best : null;
  }

  // ======================================================================== instrument (BLUEPRINT 1.4)
  function defaultInst() {
    return {
      engine: 'voice', lock: false, motion: 'forward', speed: 1, heads: 1, spread: 0.25,
      grainVoice: 2, grainCloud: 140, grainLock: 4,
      refresh: { mode: 'smooth', ms: 120, rate: '1/16', lockToBeat: true, skip: 0 },
      spray: 0, pitch: 0, tape: false, rev: false, drift: 0, headTune: 'same',
      region: { start: 0, end: 1, atEnd: 'loop', xfadeMs: 10 },
      jump: '1/8',
      macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5],
      offsets: { amp: 0, vib: 0, tone: 0, formant: 0, hollow: 0 },
      glideMode: 'legato', glideTime: 0.5, vibRate: 5.5, root: 60, cents: 0, key: 0, scale: 'Chromatic', scaleLock: false,
      velSens: 0.7, polyphony: 16, outGain: 0, bendRange: 2,
      mod1: { target: 'vib', cc: 1 }, mod2: { target: 'tone', cc: 2 },
      liveLegato: false, seed: 1,
    };
  }
  const INST_OBJECT_KEYS = ['refresh', 'region', 'offsets', 'mod1', 'mod2'];
  // Merge a partial patch into an instrument: nested objects merge one level deep, arrays replace.
  function mergeInst(inst, patch) {
    const out = deepClone(inst || defaultInst());
    if (!patch) return out;
    for (const k of Object.keys(patch)) {
      const v = patch[k];
      if (v === undefined) continue;
      if (INST_OBJECT_KEYS.includes(k) && v && typeof v === 'object' && !Array.isArray(v)) out[k] = Object.assign({}, out[k] || {}, deepClone(v));
      else out[k] = deepClone(v);
    }
    return out;
  }
  // A full, valid instrument from any partial one (unknown values fall back to the defaults).
  function normalizeInst(inst) {
    const d = defaultInst();
    const o = mergeInst(d, inst || {});
    for (const k of INST_OBJECT_KEYS) if (!o[k] || typeof o[k] !== 'object' || Array.isArray(o[k])) o[k] = deepClone(d[k]);
    const pick = (v, list, dv) => (list.includes(v) ? v : dv);
    o.engine = pick(o.engine, ENGINES, d.engine);
    o.lock = !!o.lock;
    o.motion = pick(o.motion, MOTIONS, d.motion);
    o.speed = clamp(num(o.speed, 1), 0, 16);
    o.heads = clamp(num(o.heads, 1), 1, 8);
    o.spread = clamp(num(o.spread, 0.25), 0, 1);
    o.grainVoice = clamp(num(o.grainVoice, 2), 0.25, 16);
    o.grainCloud = clamp(num(o.grainCloud, 140), 5, 2000);
    o.grainLock = clamp(num(o.grainLock, 4), 1, 16);
    o.refresh = {
      mode: pick(o.refresh.mode, REFRESH_MODES, 'smooth'), ms: clamp(num(o.refresh.ms, 120), 5, 2000),
      rate: isRate(o.refresh.rate) ? o.refresh.rate : '1/16', lockToBeat: o.refresh.lockToBeat !== false,
      skip: clamp(num(o.refresh.skip, 0), 0, 1),
    };
    o.spray = clamp(num(o.spray, 0), 0, 1);
    o.pitch = clamp(num(o.pitch, 0), -48, 48);
    o.tape = !!o.tape; o.rev = !!o.rev;
    o.drift = clamp(num(o.drift, 0), 0, 1);
    o.headTune = pick(o.headTune, HEAD_TUNES, 'same');
    const rs = clamp(num(o.region.start, 0), 0, 1), re = clamp(num(o.region.end, 1), 0, 1);
    o.region = { start: Math.min(rs, re), end: Math.max(rs, re), atEnd: pick(o.region.atEnd, AT_END, 'loop'), xfadeMs: clamp(num(o.region.xfadeMs, 10), 0, 500) };
    o.jump = isRate(o.jump) ? o.jump : '1/8';
    o.macros = d.macros.map((m, i) => clamp(num(Array.isArray(o.macros) ? o.macros[i] : m, m), 0, 1));
    const off = {}; for (const k of Object.keys(d.offsets)) off[k] = clamp(num(o.offsets[k], 0), -1, 1); o.offsets = off;
    o.glideMode = pick(o.glideMode, GLIDE_MODES, 'legato');
    o.glideTime = clamp(num(o.glideTime, 0.5), 0, 16);
    o.vibRate = clamp(num(o.vibRate, 5.5), 0.1, 20);
    o.root = clamp(num(o.root, 60), 0, 127); o.cents = clamp(num(o.cents, 0), -100, 100);
    o.key = clamp(Math.round(num(o.key, 0)), 0, 11); o.scale = SCALES[o.scale] ? o.scale : 'Chromatic'; o.scaleLock = !!o.scaleLock;
    o.velSens = clamp(num(o.velSens, 0.7), 0, 1); o.polyphony = clamp(Math.round(num(o.polyphony, 16)), 1, 32);   // the engine plays at most 32
    o.outGain = clamp(num(o.outGain, 0), -60, 12); o.bendRange = clamp(num(o.bendRange, 2), 0, 48);
    for (const m of ['mod1', 'mod2']) {
      const tg = o[m].target;
      o[m] = { target: tg === 'off' || LANE[tg] ? tg : d[m].target, cc: clamp(Math.round(num(o[m].cc, d[m].cc)), 0, 127) };
    }
    o.liveLegato = !!o.liveLegato; o.seed = Math.trunc(num(o.seed, 1)) | 0;
    return o;
  }

  // ======================================================================== travel (BLUEPRINT 1.3)
  // Thresholds on lane values hold at Float32 precision: the snapshot tables are Float32, and Math.fround(0.48)
  // = 0.4799999893 lies 1.1e-8 below the edge. F32_EPS (2e-8) is more than half a Float32 step near 0.5
  // (1.5e-8) and less than a whole one (3e-8), so a value drawn exactly on an edge plays as the edge in both
  // directions, and the next Float32 value outside it plays outside. The engine uses the same constant.
  const F32_EPS = 2e-8;
  // Speed kind: v -> signed speed multiplier. Dead band 0.48..0.52 (inclusive, at Float32 precision) = stopped.
  function travelSpeed(v) {
    if (v >= 0.48 - F32_EPS && v <= 0.52 + F32_EPS) return 0;
    if (v > 0.52) return Math.min(8, Math.pow(2, ((v - 0.75) / 0.23) * 3));
    return -Math.min(8, Math.pow(2, ((0.25 - v) / 0.23) * 3));
  }
  // The first Float32 values outside the dead band (they play +-1/8x within 4e-7).
  const f32Next = (x, dir) => { const f = new Float32Array([x]), i = new Int32Array(f.buffer); i[0] += dir; return f[0]; };
  const V_FWD_MIN = (() => { let f = Math.fround(0.52 + F32_EPS); while (f <= 0.52 + F32_EPS) f = f32Next(f, 1); return f; })();
  const V_BACK_MAX = (() => { let f = Math.fround(0.48 - F32_EPS); while (f >= 0.48 - F32_EPS) f = f32Next(f, -1); return f; })();
  // Inverse of travelSpeed for |x| in 1/8..8 (0 -> 0.5). +-1/8x land just outside the dead band, so they play.
  function travelSpeedToV(x) {
    if (!x || !Number.isFinite(x)) return 0.5;
    const a = clamp(Math.abs(x), 0.125, 8), o = (Math.log2(a) / 3) * 0.23;
    return x > 0 ? Math.max(V_FWD_MIN, 0.75 + o) : Math.min(V_BACK_MAX, 0.25 - o);
  }
  // Refresh lane below 0.06 = Smooth (BLUEPRINT 1.2), at Float32 precision.
  const refreshSmooth = v => v < 0.06 - F32_EPS;

  // ======================================================================== nodes
  const mkNode = (t, v, shape, bend) => ({ t, v, shape: shape || 'smooth', bend: bend || 0 });
  const flat = v => [mkNode(0, v), mkNode(1, v)];
  const nodesOf = list => list.map(a => mkNode(a[0], a[1], a[2], a[3]));   // [[t, v, shape?, bend?], ...]
  // PulseLock's bend curve (PulseLock/Source/pattern/Pattern.cpp bendShape): bend > 0 starts slow and
  // finishes fast, bend < 0 the reverse, |bend| < 1e-3 is straight.
  function bendShape(u, bend) {
    if (Math.abs(bend) < 1.0e-3) return u;
    const k = bend * 8.0;
    return (Math.exp(k * u) - 1.0) / (Math.exp(k) - 1.0);
  }
  // Clean any node list: shapes resolved (v3 lin:true -> 'linear'), values clamped, sorted by t (stable),
  // first t = 0, last t = 1, at most 128 nodes.
  function normalizeNodes(nodes, def) {
    const dv = def == null ? 0.5 : def;
    if (!Array.isArray(nodes) || !nodes.length) return flat(dv);
    let out = nodes.map((q, i) => {
      q = q || {};
      const shape = NODE_SHAPES.includes(q.shape) ? q.shape : (q.lin ? 'linear' : 'smooth');
      return [{ t: clamp(num(q.t, 0), 0, 1), v: clamp(num(q.v, dv), 0, 1), shape, bend: clamp(num(q.bend, 0), -1, 1) }, i];
    });
    out.sort((a, b) => a[0].t - b[0].t || a[1] - b[1]);
    out = out.map(x => x[0]);
    if (out.length === 1) out = [Object.assign({}, out[0], { t: 0 }), Object.assign({}, out[0], { t: 1 })];
    out[0].t = 0; out[out.length - 1].t = 1;
    return capNodes(out);
  }
  function cloneNodes(nd) { return nd.map(q => ({ t: q.t, v: q.v, shape: q.shape, bend: q.bend })); }
  // true when the list can be used as is: sorted by t, every t and v finite (else it goes through normalizeNodes,
  // which turns a NaN into the lane default / 0, so one bad node never poisons a lane)
  function nodesSorted(nd) {
    for (let i = 0; i < nd.length; i++) {
      const q = nd[i];
      if (!q || !Number.isFinite(q.t) || !Number.isFinite(q.v) || (i > 0 && q.t < nd[i - 1].t)) return false;
    }
    return true;
  }

  // Fritsch-Carlson tangents over the whole node list: v3's sampleLane, verbatim. (Its circle limit is
  // applied segment by segment from the left, so a reversed curve can differ by ~1e-4 where two
  // neighbouring segments both hit the limit; see NOTES-model.md.)
  function tangents(nd) {
    const n = nd.length, d = new Array(Math.max(1, n - 1)), m = new Array(n);
    for (let k = 0; k < n - 1; k++) d[k] = (nd[k + 1].v - nd[k].v) / Math.max(1e-6, nd[k + 1].t - nd[k].t);
    m[0] = d[0]; m[n - 1] = d[n - 2];
    for (let k = 1; k < n - 1; k++) m[k] = d[k - 1] * d[k] <= 0 ? 0 : (d[k - 1] + d[k]) / 2;
    for (let k = 0; k < n - 1; k++) {
      if (d[k] === 0) { m[k] = 0; m[k + 1] = 0; continue; }
      const a = m[k] / d[k], b = m[k + 1] / d[k], s = a * a + b * b;
      if (s > 9) { const t = 3 / Math.sqrt(s); m[k] = t * a * d[k]; m[k + 1] = t * b * d[k]; }
    }
    return m;
  }
  function evalSeg(nd, m, k, t) {
    const A = nd[k], B = nd[k + 1], h = B.t - A.t;
    if (h <= 1e-9) return B.v;
    const u = clamp((t - A.t) / h, 0, 1);
    switch (A.shape) {
      case 'linear': return A.v + (B.v - A.v) * u;
      case 'bend': return A.v + (B.v - A.v) * bendShape(u, A.bend);
      case 'step': return u >= 1 ? B.v : A.v;
      default: {
        const u2 = u * u, u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * A.v + (u3 - 2 * u2 + u) * h * m[k] + (-2 * u3 + 3 * u2) * B.v + (u3 - u2) * h * m[k + 1];
      }
    }
  }
  // Segment holding t. right=true: at a node's exact t, the segment after it (PulseLock: the value at a
  // vertical edge is the value after it). right=false: the segment before it.
  function segIndex(nd, t, right) {
    const n = nd.length;
    let lo = 0, hi = n - 2;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (right ? nd[mid].t <= t : nd[mid].t < t) lo = mid; else hi = mid - 1;
    }
    return lo;
  }
  function evalNodes(nodes, t, left) {
    const nd = nodesSorted(nodes) ? nodes : normalizeNodes(nodes);
    if (nd.length === 1) return nd[0].v;
    const m = tangents(nd);
    return clamp(evalSeg(nd, m, segIndex(nd, t, !left), t), 0, 1);
  }
  // M samples at t = j/(M-1). All four segment shapes; 'smooth' is monotone (never overshoots its two nodes).
  function sampleNodes(nodes, M) {
    M = Math.max(1, M | 0);
    const o = new Float32Array(M);
    if (!Array.isArray(nodes) || !nodes.length) return o;
    const nd = nodesSorted(nodes) ? nodes : normalizeNodes(nodes);
    const n = nd.length;
    if (n === 1) { o.fill(nd[0].v); return o; }
    const m = tangents(nd);
    let k = 0;
    for (let j = 0; j < M; j++) {
      const t = M === 1 ? 0 : j / (M - 1);
      while (k < n - 2 && t >= nd[k + 1].t) k++;
      o[j] = clamp(evalSeg(nd, m, k, t), 0, 1);
    }
    return o;
  }

  // Ramer-Douglas-Peucker on parallel t/v arrays; returns kept indices (first and last always kept).
  function rdpIdx(ts, vs, eps) {
    const n = ts.length;
    if (n < 3) return Array.from({ length: n }, (_, i) => i);
    const keep = new Uint8Array(n); keep[0] = 1; keep[n - 1] = 1;
    const stack = [0, n - 1];
    while (stack.length) {
      const b = stack.pop(), a = stack.pop();
      let idx = -1, dm = 0;
      const ta = ts[a], va = vs[a], dt = (ts[b] - ta) || 1, dvv = vs[b] - va;
      for (let i = a + 1; i < b; i++) {
        const dv = Math.abs(vs[i] - (va + dvv * ((ts[i] - ta) / dt)));
        if (dv > dm) { dm = dv; idx = i; }
      }
      if (dm > eps && idx > 0) { keep[idx] = 1; stack.push(a, idx, idx, b); }
    }
    const out = []; for (let i = 0; i < n; i++) if (keep[i]) out.push(i);
    return out;
  }
  // v3's rdp(pts, eps): points {t, v, ...} in, the kept points out (copies).
  function rdp(pts, eps) {
    if (pts.length < 3) return pts.map(p => Object.assign({}, p));
    return rdpIdx(pts.map(p => p.t), pts.map(p => p.v), eps).map(i => Object.assign({}, pts[i]));
  }
  function capNodes(nd) {
    if (nd.length <= MAX_NODES) return nd;
    const ts = nd.map(q => q.t), vs = nd.map(q => q.v);
    let eps = 0.001, idx = rdpIdx(ts, vs, eps);
    while (idx.length > MAX_NODES) { eps *= 1.25; idx = rdpIdx(ts, vs, eps); }
    return idx.map(i => nd[i]);
  }

  // The part of a node list between u0 and u1, remapped to 0..1. Linear, step and bend segments are cut
  // exactly (a cut bend keeps its curve: bend' = bend * covered fraction). Smooth segments are re-fitted:
  // a monotone cubic through fewer points has different tangents, so any smooth segment that strays more
  // than SLICE_TOL from the original curve is split at its middle (a node on the original curve), until
  // all fit (or 128 nodes). Vertical edges inside the range survive; at u0 the value after an edge is
  // used, at u1 the value before it.
  const SLICE_TOL = 0.002;
  function sliceNodes(nodes, u0, u1) {
    const nd = nodesSorted(nodes) ? nodes : normalizeNodes(nodes);
    u0 = clamp(u0, 0, 1); u1 = clamp(u1, 0, 1);
    if (nd.length < 2) return flat(nd.length ? nd[0].v : 0.5);
    if (u1 - u0 < 1e-9) return flat(evalNodes(nd, u0));
    if (u0 <= 0 && u1 >= 1) return cloneNodes(nd);
    const m = tangents(nd), span = u1 - u0, out = [];
    const valAt = (t, left) => clamp(evalSeg(nd, m, segIndex(nd, t, !left), t), 0, 1);
    const piece = (k, x0, x1, v0) => {     // the part of segment k from x0 to x1, starting at value v0
      const A = nd[k], h = nd[k + 1].t - A.t;
      const f = h > 1e-9 ? clamp((x1 - x0) / h, 0, 1) : 1;
      out.push({ t: (x0 - u0) / span, v: v0, shape: A.shape, bend: A.shape === 'bend' ? clamp(A.bend * f, -1, 1) : A.bend, _k: k });
    };
    let k = segIndex(nd, u0, true), x = u0, v = valAt(u0, false);
    for (;;) {
      const segEnd = nd[k + 1].t;
      piece(k, x, Math.min(segEnd, u1), v);
      if (segEnd >= u1 - 1e-12 || k >= nd.length - 2) break;
      k++; x = nd[k].t; v = nd[k].v;
    }
    out.push({ t: 1, v: valAt(u1, true), shape: 'smooth', bend: 0, _k: -1 });
    out[0].t = 0;
    // refit smooth pieces against the original curve (each out segment lies inside one original segment _k)
    const orig = (sk, t) => clamp(evalSeg(nd, m, sk, u0 + t * span), 0, 1);
    for (let iter = 0; iter < 10; iter++) {
      const mo = tangents(out), split = [];
      for (let j = 0; j < out.length - 1; j++) {
        const A = out[j], B = out[j + 1];
        if (A.shape !== 'smooth' || A._k < 0 || B.t - A.t < 1e-4) continue;
        let worst = 0;
        for (let i = 1; i < 8; i++) { const t = A.t + (B.t - A.t) * i / 8; worst = Math.max(worst, Math.abs(clamp(evalSeg(out, mo, j, t), 0, 1) - orig(A._k, t))); }
        if (worst > SLICE_TOL) split.push(j);
      }
      if (!split.length || out.length + split.length > MAX_NODES) break;
      for (let i = split.length - 1; i >= 0; i--) {
        const j = split[i], A = out[j], t = (A.t + out[j + 1].t) / 2;
        out.splice(j + 1, 0, { t, v: orig(A._k, t), shape: 'smooth', bend: 0, _k: A._k });
      }
    }
    return capNodes(out.map(q => ({ t: q.t, v: q.v, shape: q.shape, bend: q.bend })));
  }
  // Mirror in time. bend -> -bend (exact). A step becomes a vertical edge then a hold (exact between nodes).
  function reverseNodes(nodes) {
    const nd = nodesSorted(nodes) ? nodes : normalizeNodes(nodes);
    const n = nd.length, out = [];
    for (let i = n - 1; i >= 0; i--) {
      const q = nd[i], t = 1 - q.t;
      if (i === 0) { out.push({ t, v: q.v, shape: 'smooth', bend: 0 }); break; }
      const P = nd[i - 1];
      if (P.shape === 'step') { out.push({ t, v: q.v, shape: 'linear', bend: 0 }); out.push({ t, v: P.v, shape: 'linear', bend: 0 }); }
      else out.push({ t, v: q.v, shape: P.shape, bend: P.shape === 'bend' ? -P.bend : P.bend });
    }
    // the edge node made for a step that ended at the last node sits at t=0 next to the first node: drop the duplicate
    if (out.length > 2 && out[0].t === 0 && out[1].t === 0) out.shift();
    out[0].t = 0; out[out.length - 1].t = 1;
    return capNodes(out);
  }
  const flipNodes = nd => nd.map(q => ({ t: q.t, v: clamp(1 - q.v, 0, 1), shape: q.shape, bend: q.bend }));
  const scaleNodes = (nd, def, f) => nd.map(q => ({ t: q.t, v: clamp(def + (q.v - def) * f, 0, 1), shape: q.shape, bend: q.bend }));

  // ======================================================================== notes and clips
  let idCounter = 0;
  const ID_SALT = (Date.now() % 2176782336).toString(36);   // identity only, never randomness
  function freshId() { return 'n' + ID_SALT + (++idCounter).toString(36); }
  function defaultSample() {
    return { motion: null, travel: 'speed', start: { mode: 'region', pos: 0, range: 0.25, seed: 1 }, tape: null, rev: null, jump: null, engine: null, lock: null, restart: false };
  }
  function mkNote(p, s, l, id) {
    const n = { id: id || freshId(), p, s, l, vel: 0.8, glide: null, gc: 0, glideTo: null, c: {}, gen: {}, sample: defaultSample() };
    for (const L of LANES) { n.c[L.id] = flat(L.def); n.gen[L.id] = null; }
    return n;
  }
  function newClip(bars) { return { bars: clamp(Math.round(num(bars, 4)), 1, 64), notes: [] }; }
  function normalizeSample(s) {
    s = s || {};
    const st = s.start || {};
    return {
      motion: MOTIONS.includes(s.motion) ? s.motion : null,
      travel: s.travel === 'path' ? 'path' : 'speed',
      start: { mode: START_MODES.includes(st.mode) ? st.mode : 'region', pos: clamp(num(st.pos, 0), 0, 1), range: clamp(num(st.range, 0.25), 0, 1), seed: Math.trunc(num(st.seed, 1)) | 0 },
      tape: boolOrNull(s.tape), rev: boolOrNull(s.rev), jump: isRate(s.jump) ? s.jump : null,
      engine: ENGINES.includes(s.engine) ? s.engine : null, lock: boolOrNull(s.lock), restart: !!s.restart,
    };
  }
  function normalizeGen(g) {
    if (!g || typeof g !== 'object') return null;
    const d0 = clamp(num(g.d0, 0.3), 0, 1);
    return {
      kind: g.kind === 'random' ? 'random' : 'pattern',
      shape: GEN_SHAPES.includes(g.shape) ? g.shape : 'squiggle',
      rate: isRate(g.rate) ? g.rate : '1/16T',   // BLUEPRINT 2.1 default
      d0, d1: clamp(num(g.d1, d0), 0, 1),
      warp: clamp(num(g.warp, 0), -0.9, 3),
      lockToBeat: g.lockToBeat !== false,
      amount: clamp(num(g.amount, 0.5), 0, 1),
      smooth: clamp(num(g.smooth, 0), 0, 1),
      perNote: g.perNote === 'different' ? 'different' : 'same',
      seed: Math.trunc(num(g.seed, 1)) | 0,
      // invert: Flip mirrors the generator with the drawing (table value 1 - v). key: the note id whose hash
      // 'Different' uses (null = the note's own id); Chop sets it so the pieces keep the original's values (Y7).
      invert: g.invert === true,
      key: typeof g.key === 'string' && g.key ? g.key : null,
    };
  }
  function makeGen(kind, patch) { return normalizeGen(Object.assign({ kind: kind || 'pattern' }, patch || {})); }
  // Any note-like object (v3 notes included) -> a full v4 note. Missing ids are made from a hash of the
  // note's index and position, so normalizing is deterministic.
  function normalizeNote(n, i) {
    n = n || {};
    const p = clamp(num(n.p, 60), 0, 127), s = Math.max(0, num(n.s, 0)), l = Math.max(1e-3, num(n.l, 1));
    const id = (typeof n.id === 'string' && n.id) || (typeof n.id === 'number' && String(n.id)) ||
      ('n' + hash32(i | 0, Math.round(p * 100), Math.round(s * 1e6), Math.round(l * 1e6)).toString(36));
    const out = {
      id, p, s, l, vel: clamp(num(n.vel, 0.8), 0, 1),
      glide: n.glide == null ? null : Math.max(0, num(n.glide, 0)),
      gc: clamp(num(n.gc, 0), -1, 1),
      glideTo: (typeof n.glideTo === 'string' && n.glideTo) || (typeof n.glideTo === 'number' && String(n.glideTo)) || null,
      c: {}, gen: {}, sample: normalizeSample(n.sample),
    };
    const c = n.c || {}, g = n.gen || {};
    for (const L of LANES) { out.c[L.id] = normalizeNodes(c[L.id], L.def); out.gen[L.id] = normalizeGen(g[L.id]); }
    return out;
  }
  // A clip from anything: a v4 clip, a v3 notes array, or a {bars, notes} object. Duplicate ids are renamed.
  function normalizeClip(data) {
    const src = Array.isArray(data) ? { notes: data } : (data || {});
    const clip = newClip(num(src.bars, 4));
    const seen = new Set();
    (Array.isArray(src.notes) ? src.notes : []).forEach((n, i) => {
      const q = normalizeNote(n, i);
      if (seen.has(q.id)) { let k = 1; while (seen.has(q.id + '~' + k)) k++; q.id = q.id + '~' + k; }
      seen.add(q.id); clip.notes.push(q);
    });
    const ids = new Set(clip.notes.map(n => n.id));
    for (const n of clip.notes) if (n.glideTo && (!ids.has(n.glideTo) || n.glideTo === n.id)) n.glideTo = null;
    return clip;
  }
  function cloneNote(n) {
    const c = {}, g = {};
    for (const L of LANES) {
      const nd = n.c && n.c[L.id];
      c[L.id] = Array.isArray(nd) && nd.length ? cloneNodes(nd) : flat(L.def);
      g[L.id] = n.gen && n.gen[L.id] ? Object.assign({}, n.gen[L.id]) : null;
    }
    const sm = n.sample ? Object.assign({}, n.sample, { start: Object.assign({}, n.sample.start) }) : defaultSample();
    return Object.assign({}, n, { c, gen: g, sample: sm });
  }
  function cloneClip(clip) { return Object.assign({}, clip, { notes: clip.notes.map(cloneNote) }); }
  const noteLane = (n, laneId) => (n.c && Array.isArray(n.c[laneId]) && n.c[laneId].length ? n.c[laneId] : flat(laneDef(laneId)));
  const timeCmp = (a, b) => a.s - b.s || a.p - b.p || (a.id < b.id ? -1 : a.id > b.id ? 1 : 0);
  const timeOrder = notes => notes.slice().sort(timeCmp);
  function idSet(clip, ids) {
    if (ids == null) return new Set(clip.notes.map(n => n.id));
    if (typeof ids === 'string') return new Set([ids]);
    return new Set(ids);
  }
  // Copy-on-write edit: fn(copy, original) edits a fresh copy of every note in ids.
  function editNotes(clip, ids, fn) {
    const set = idSet(clip, ids);
    const notes = clip.notes.map(n => {
      if (!set.has(n.id)) return n;
      const c = cloneNote(n), r = fn(c, n);
      return r || c;
    });
    return Object.assign({}, clip, { notes });
  }
  function selected(clip, ids) { const set = idSet(clip, ids); return clip.notes.filter(n => set.has(n.id)); }
  function uniqueId(taken, base, k) {
    let salt = 0, id;
    do { id = 'n' + hash32(idHash(base), k | 0, salt++).toString(36); } while (taken.has(id));
    taken.add(id);
    return id;
  }
  // Small editing helpers for Build mode.
  function addNote(clip, p, s, l, patch) {
    const taken = new Set(clip.notes.map(n => n.id));
    const id = uniqueId(taken, 'add:' + clip.notes.length + ':' + p + ':' + s + ':' + l, clip.notes.length);
    const n = mkNote(p, s, l, id);
    if (patch) applyNotePatch(n, patch);
    return { clip: Object.assign({}, clip, { notes: clip.notes.concat([n]) }), id };
  }
  function removeNotes(clip, ids) {
    const set = idSet(clip, ids);
    const notes = clip.notes.filter(n => !set.has(n.id)).map(n => (n.glideTo && set.has(n.glideTo) ? Object.assign(cloneNote(n), { glideTo: null }) : n));
    return Object.assign({}, clip, { notes });
  }
  // patch: { p, s, l, vel, glide, gc, glideTo } as values, or a function (note copy) => void.
  function updateNotes(clip, ids, patch) {
    return editNotes(clip, ids, n => {
      if (typeof patch === 'function') { patch(n); return; }
      for (const k of ['p', 's', 'l', 'vel', 'glide', 'gc', 'glideTo']) if (patch && has(patch, k)) n[k] = patch[k];
      n.p = clamp(num(n.p, 60), 0, 127); n.s = Math.max(0, num(n.s, 0)); n.l = Math.max(1e-3, num(n.l, 1));
      n.vel = clamp(num(n.vel, 0.8), 0, 1);
    });
  }
  // Copies of the notes, moved by `offset` beats (default: the selection's own length, as v3's duplicate).
  function duplicateNotes(clip, ids, offset) {
    const src = timeOrder(selected(clip, ids));
    if (!src.length) return { clip, ids: [] };
    const s0 = Math.min(...src.map(n => n.s)), s1 = Math.max(...src.map(n => n.s + n.l));
    const off = offset == null ? s1 - s0 : offset;
    const taken = new Set(clip.notes.map(n => n.id)), map = new Map();
    src.forEach((n, k) => map.set(n.id, uniqueId(taken, 'dup:' + n.id + ':' + off, k)));
    const made = src.map(n => { const c = cloneNote(n); c.id = map.get(n.id); c.s = Math.max(0, n.s + off); c.glideTo = n.glideTo ? map.get(n.glideTo) || null : null; return c; });
    return { clip: Object.assign({}, clip, { notes: clip.notes.concat(made) }), ids: made.map(n => n.id) };
  }

  // ======================================================================== generators (BLUEPRINT 2.1, Y3, Y4, Y9)
  // The generator as a function of note time t (0..1) -> offset in lane units. g must be normalized.
  // Phase (in rate steps): lockToBeat = song beat / rate, so every note lines up; otherwise from the note
  // start, warped (v3: warp > 0 speeds up across the note, < 0 slows down). Random values are a pure
  // function of (lane index, step, seed), plus the note id hash for 'different'.
  function genEval(g, note, laneId) {
    const L = laneIndex(laneId), P = rateBeats(g.rate), s = num(note.s, 0), l = num(note.l, 1);
    const diff = g.perNote === 'different', idh = diff ? idHash(g.key != null ? g.key : note.id) : 0, seed = g.seed;
    const sg = g.invert ? -1 : 1;
    let ck = NaN, cv = 0;
    const rnd = kk => {
      if (kk === ck) return cv;
      const v = rand01(diff ? hash32(idh, L, kk, seed) : hash32(L, kk, seed)) * 2 - 1;
      ck = kk; cv = v; return v;
    };
    const rndNoCache = kk => rand01(diff ? hash32(idh, L, kk, seed) : hash32(L, kk, seed)) * 2 - 1;
    const scale = laneId === 'pitch' ? PITCH_GEN_SCALE : 1;
    const warp = g.warp;
    return t => {
      const phase = g.lockToBeat ? (s + t * l) / P : (l * (warp === 0 ? t : Math.pow(t, 1 + warp))) / P;
      const k = Math.floor(phase + 1e-9), f = clamp(phase - k, 0, 1);
      if (g.kind === 'random') {
        const cur = rnd(k);
        let val = cur;
        if (g.smooth > 0 && f < g.smooth) { const prev = rndNoCache(k - 1), w = (1 - Math.cos(Math.PI * f / g.smooth)) / 2; val = prev + (cur - prev) * w; }
        return sg * val * g.amount * 0.5 * scale;
      }
      let w;
      switch (g.shape) {
        case 'triangle': w = f < 0.25 ? 4 * f : f < 0.75 ? 2 - 4 * f : 4 * f - 4; break;
        case 'sawup': w = 2 * f - 1; break;
        case 'sawdown': w = 1 - 2 * f; break;
        case 'square': w = f < 0.5 ? 1 : -1; break;
        case 'randsteps': w = rnd(k); break;
        case 'smoothrandom': { const a = rnd(k), b = rndNoCache(k + 1); w = a + (b - a) * (1 - Math.cos(Math.PI * f)) / 2; break; }
        default: w = Math.sin(2 * Math.PI * f);
      }
      return sg * w * (g.d0 + (g.d1 - g.d0) * t) * 0.5 * scale;
    };
  }
  // Offset (in lane units) that generator g adds to the drawing of `note` on `laneId` at note time t (0..1).
  function genOffset(g, note, laneId, t) { return genEval(normalizeGen(g), note, laneId)(t); }
  const tableLength = (l, res) => { const n = Math.ceil(l * res) + 1; return Number.isFinite(n) ? Math.max(2, n) : 2; };
  // The lane as the engine plays it: drawing + generator, clamped 0..1. inst is accepted for symmetry
  // with the engine and is not needed (macros and offsets are applied live, not baked).
  function laneTable(note, laneId, res, inst) {   // eslint-disable-line no-unused-vars
    const r = Math.max(1, num(res, 256));
    const N = tableLength(note.l, r);
    const out = sampleNodes(noteLane(note, laneId), N);
    const g0 = note.gen && note.gen[laneId];
    if (g0) {
      const ev = genEval(normalizeGen(g0), note, laneId);
      for (let j = 0; j < N; j++) out[j] = clamp(out[j] + ev(j / (N - 1)), 0, 1);
    }
    return out;
  }
  function setGen(clip, ids, laneId, gen) {
    laneIndex(laneId);
    const g = normalizeGen(gen);
    return editNotes(clip, ids, n => { n.gen[laneId] = g ? Object.assign({}, g) : null; });
  }
  function clearGen(clip, ids, laneId) {
    const lanes = laneId == null ? LANE_IDS : [laneId];
    lanes.forEach(laneIndex);
    return editNotes(clip, ids, n => { for (const id of lanes) n.gen[id] = null; });
  }
  // Turn into nodes: sample the table, simplify with RDP (eps 0.004; raised only if more than 128 nodes
  // would be needed), drop the generator.
  function bakeGen(clip, ids, laneId, opts) {
    laneIndex(laneId);
    const res = Math.max(8, num(opts && opts.res, 256));
    return editNotes(clip, ids, n => {
      if (!n.gen[laneId]) return;
      const tab = laneTable(n, laneId, res), N = tab.length;
      const ts = new Array(N), vs = new Array(N);
      for (let j = 0; j < N; j++) { ts[j] = j / (N - 1); vs[j] = tab[j]; }
      let eps = BAKE_EPS, idx = rdpIdx(ts, vs, eps);
      while (idx.length > MAX_NODES) { eps *= 1.25; idx = rdpIdx(ts, vs, eps); }
      n.c[laneId] = idx.map(i => mkNode(ts[i], vs[i], 'linear'));
      n.gen[laneId] = null;
    });
  }
  // Re-roll: seed + 1 on the generators (one lane, or every lane when laneId is null) and, when laneId is
  // null, on the seed of every 'random' Start.
  function reroll(clip, ids, laneId) {
    const lanes = laneId == null ? LANE_IDS : [laneId];
    lanes.forEach(laneIndex);
    return editNotes(clip, ids, n => {
      for (const id of lanes) if (n.gen[id]) n.gen[id].seed = (n.gen[id].seed + 1) | 0;
      if (laneId == null && n.sample.start.mode === 'random') n.sample.start.seed = (n.sample.start.seed + 1) | 0;
    });
  }

  // ======================================================================== shapes (Y2)
  const SHAPE_KEYS = Object.freeze({
    common: Object.freeze(['up', 'down', 'swell', 'dip', 'squiggle', 'random', 'decay', 'reset']),
    speed: Object.freeze(['tapestop', 'tapestart', 'reversehalf', 'rewind']),
    path: Object.freeze(['playthrough', 'playthroughback', 'fit']),
  });
  const SHAPE_NAMES = Object.freeze({
    up: 'Ramp up', down: 'Ramp down', swell: 'Swell', dip: 'Dip', squiggle: 'Squiggle', random: 'Random', decay: 'Decay', reset: 'Reset',
    tapestop: 'Tape stop', tapestart: 'Tape start', reversehalf: 'Reverse halfway', rewind: 'Rewind',
    playthrough: 'Play through', playthroughback: 'Play through backwards', fit: 'Fit (1x)',
  });
  // v3's patternNodes, with its random branch seeded instead of Math.random.
  function patternNodes(o) {
    const shape = o.shape, center = num(o.center, 0.5), d0 = num(o.d0, 0.3), d1 = num(o.d1, d0), acc = num(o.acc, 0);
    const nodes = [], amp = t => (d0 + (d1 - d0) * t) / 2;
    const warpInv = x => (acc === 0 ? x : Math.pow(x, 1 / (1 + acc)));
    const C = Math.max(0.5, num(o.cycles, 3));
    const push = (x, v, lin) => { const t = clamp(warpInv(clamp(x, 0, 1)), 0, 1); nodes.push(mkNode(t, clamp(v, 0, 1), lin ? 'linear' : 'smooth')); };
    if (shape === 'sine' || shape === 'tri') {
      const steps = Math.max(2, Math.round(C * 4)), ph = [0, 1, 0, -1];
      for (let i = 0; i <= steps; i++) { const x = i / steps; push(x, center + ph[i % 4] * amp(x), shape === 'tri'); }
    } else if (shape === 'saw') {
      const n = Math.max(1, Math.round(C));
      for (let c = 0; c < n; c++) { const x0 = c / n, x1 = (c + 1) / n; push(x0, center - amp(x0), true); push(x1 - 0.004, center + amp(x1), true); }
      push(1, center - amp(1), true);
    } else if (shape === 'square') {
      const n = Math.max(1, Math.round(C * 2));
      for (let c = 0; c < n; c++) { const x0 = c / n, x1 = (c + 1) / n, s = c % 2 ? -1 : 1; push(x0, center + s * amp(x0), true); push(x1 - 0.004, center + s * amp(x1), true); }
      push(1, center + (n % 2 ? -1 : 1) * amp(1), true);
    } else {
      const n = Math.max(2, Math.round(C * 2));
      let seed = hash32(num(o.seed, 1) | 0) % 10000;
      for (let c = 0; c <= n; c++) { const x = c / n; seed = (seed * 9301 + 49297) % 233280; push(x, center + (seed / 233280 * 2 - 1) * amp(x)); }
    }
    nodes.sort((a, b) => a.t - b.t); nodes[0].t = 0; nodes[nodes.length - 1].t = 1;
    for (let i = 1; i < nodes.length; i++) if (nodes[i].t <= nodes[i - 1].t) nodes[i].t = Math.min(1, nodes[i - 1].t + 1e-4);
    return nodes;
  }
  const seededList = (count, idh, L, seed) => Array.from({ length: count }, (_, i) => rand01(hash32(idh | 0, L, i, seed | 0, 0x5a9e)));
  // Path kind 'fit': the line rises so the head moves through the Region at exactly 1x for `beats`
  // beats at opts.bpm, wrapping (vertical edges) when the Region loops, holding at its end otherwise.
  function fitNodes(beats, opts) {
    const o = opts || {}, inst = o.inst ? normalizeInst(o.inst) : null;
    const region = o.region || (inst && inst.region) || { start: 0, end: 1, atEnd: 'loop' };
    const bpm = clamp(num(o.bpm, 120), 20, 400), secs = Math.max(0.01, num(o.sampleSeconds, 6));
    const regionSec = Math.max(1e-3, (clamp(num(region.end, 1), 0, 1) - clamp(num(region.start, 0), 0, 1)) * secs);
    const rate = (beats * 60 / bpm) / regionSec;          // Region fractions per note
    let v = clamp(num(o.from, 0), 0, 1);
    const out = [mkNode(0, v, 'linear')];
    if (v + rate <= 1) { out.push(mkNode(1, v + rate)); return out; }
    let t = (1 - v) / rate;
    if (region.atEnd !== 'loop') { out.push(mkNode(t, 1, 'linear')); out.push(mkNode(1, 1)); return out; }
    while (t < 1 && out.length < MAX_NODES - 2) {
      out.push(mkNode(t, 1, 'linear')); out.push(mkNode(t, 0, 'linear'));
      t += 1 / rate;
    }
    const last = out[out.length - 1];
    out.push(mkNode(1, clamp(last.v + (1 - last.t) * rate, 0, 1)));
    return normalizeNodes(out);
  }
  // Nodes for one shape key on one lane (v3's shapeNodes, extended). opts: { kind:'speed'|'path' (travel),
  // seed, idh (note id hash, for 'random'), beats (note or span length, for 'fit'), bpm, sampleSeconds,
  // region | inst, count (random node count) }.
  function shapeNodes(type, laneId, opts) {
    const o = opts || {}, L = LANE[laneId] ? laneIndex(laneId) : 0, def = laneDef(laneId);
    if (type === 'wobble') type = 'squiggle';
    if (laneId === 'travel' && o.kind === 'path') return pathShape(type, o, L);
    if (laneId === 'travel') return speedShape(type, o, L);
    const P = laneId === 'pitch', sm = 1 / 24;
    const rcount = Math.max(3, num(o.count, 5) | 0);
    switch (type) {
      case 'reset': return flat(def);
      case 'up': return P ? nodesOf([[0, 0.5], [1, 0.5 + 2 * sm]]) : nodesOf([[0, 0], [1, 1]]);
      case 'down': return P ? nodesOf([[0, 0.5], [1, 0.5 - 2 * sm]]) : nodesOf([[0, 1], [1, 0]]);
      case 'swell': return P ? nodesOf([[0, 0.5], [0.5, 0.5 + sm], [1, 0.5]]) : nodesOf([[0, 0.05], [0.5, 1], [1, 0.05]]);
      case 'dip': return P ? nodesOf([[0, 0.5], [0.5, 0.5 - sm], [1, 0.5]]) : nodesOf([[0, 1], [0.5, 0.12], [1, 1]]);
      case 'decay': return P ? nodesOf([[0, 0.5 + sm], [0.12, 0.5], [1, 0.5]]) : nodesOf([[0, 1], [0.15, 0.55], [1, 0]]);
      case 'squiggle': return patternNodes({ shape: 'sine', cycles: num(o.cycles, 3), center: 0.5, d0: P ? 0.06 : 0.7, d1: P ? 0.06 : 0.7, acc: 0 });
      case 'random': {
        const r = seededList(rcount, o.idh, L, num(o.seed, 1));
        return r.map((x, i) => mkNode(i / (rcount - 1), P ? (i === 0 ? 0.5 : 0.5 + (x * 2 - 1) * sm) : 0.1 + 0.8 * x));
      }
    }
    return null;
  }
  function speedShape(type, o, L) {
    const R = 0.75, rcount = Math.max(3, num(o.count, 5) | 0);
    switch (type) {
      case 'reset': return flat(R);
      // no node sits on a dead-band edge (0.48 / 0.52): in a Float32 table those round to either side
      case 'up': return nodesOf([[0, R], [1, 0.98]]);
      case 'down': return nodesOf([[0, R], [1, 0.53]]);
      case 'swell': return nodesOf([[0, R], [0.5, 0.87], [1, R]]);
      case 'dip': return nodesOf([[0, R], [0.5, 0.63], [1, R]]);
      case 'decay': return nodesOf([[0, travelSpeedToV(4)], [0.25, R], [1, R]]);
      case 'squiggle': return patternNodes({ shape: 'sine', cycles: num(o.cycles, 3), center: R, d0: 0.24, d1: 0.24, acc: 0 });
      case 'random': { const r = seededList(rcount, o.idh, L, num(o.seed, 1)); return r.map((x, i) => mkNode(i / (rcount - 1), i === 0 ? R : R + (x * 2 - 1) * 0.15)); }
      // speed falls about linearly from 1x (a bent line in v), reaches the dead band at 7/8, then holds stopped
      case 'tapestop': return nodesOf([[0, R, 'bend', 0.24], [0.875, 0.5, 'step'], [1, 0.5]]);
      case 'tapestart': return nodesOf([[0, 0.5, 'step'], [0.125, 0.5, 'bend', -0.24], [1, R]]);
      case 'reversehalf': return nodesOf([[0, R, 'step'], [0.5, 0.25, 'step'], [1, 0.25]]);
      case 'rewind': return nodesOf([[0, R, 'step'], [0.5, travelSpeedToV(-4), 'linear'], [1, 0.5]]);
    }
    return null;
  }
  function pathShape(type, o, L) {
    const rcount = Math.max(3, num(o.count, 5) | 0);
    switch (type) {
      case 'reset': return flat(laneDef('travel'));
      case 'up': case 'playthrough': return nodesOf([[0, 0, 'linear'], [1, 1]]);
      case 'down': case 'playthroughback': return nodesOf([[0, 1, 'linear'], [1, 0]]);
      case 'swell': return nodesOf([[0, 0.05], [0.5, 1], [1, 0.05]]);
      case 'dip': return nodesOf([[0, 1], [0.5, 0.12], [1, 1]]);
      case 'decay': return nodesOf([[0, 1], [0.15, 0.55], [1, 0]]);
      case 'squiggle': return patternNodes({ shape: 'sine', cycles: num(o.cycles, 3), center: 0.5, d0: 0.4, d1: 0.4, acc: 0 });
      case 'random': { const r = seededList(rcount, o.idh, L, num(o.seed, 1)); return r.map((x, i) => mkNode(i / (rcount - 1), x, 'step')); }
      case 'fit': return fitNodes(Math.max(1e-3, num(o.beats, 1)), o);
    }
    return null;
  }
  function shapeKeyKind(key) { return SHAPE_KEYS.speed.includes(key) ? 'speed' : SHAPE_KEYS.path.includes(key) ? 'path' : null; }
  function shapeKeysFor(laneId, kind) { return laneId === 'travel' ? SHAPE_KEYS.common.concat(SHAPE_KEYS[kind === 'path' ? 'path' : 'speed']) : SHAPE_KEYS.common.slice(); }
  // Y2: one shape on every selected note. scope 'each' fits the shape to every note; 'across' spans the
  // selection's time range and gives each note its slice. Travel keys that belong to the other travel
  // kind leave that note alone. Generators are not touched (Reset resets the drawing only).
  function applyShape(clip, ids, laneId, key, scope, opts) {
    laneIndex(laneId);
    const o = opts || {};
    const known = SHAPE_KEYS.common.includes(key) || key === 'wobble' || (laneId === 'travel' && shapeKeyKind(key));
    if (!known) throw new Error('SLModel.applyShape: unknown shape ' + key + ' for lane ' + laneId);
    const sel = selected(clip, ids);
    if (!sel.length) return Object.assign({}, clip, { notes: clip.notes.slice() });
    const kindOf = n => (laneId === 'travel' ? n.sample.travel : null);
    const needKind = shapeKeyKind(key);
    if (scope === 'across') {
      const S0 = Math.min(...sel.map(n => n.s)), S1 = Math.max(...sel.map(n => n.s + n.l)), span = Math.max(1e-6, S1 - S0);
      const cache = {};
      const curveFor = kind => cache[kind] || (cache[kind] = shapeNodes(key, laneId, Object.assign({}, o, {
        kind, idh: 0, beats: span, count: key === 'random' ? clamp(Math.round(span) + 1, 5, 33) : o.count,
      })));
      return editNotes(clip, sel.map(n => n.id), n => {
        const kind = kindOf(n);
        if (needKind && kind !== needKind) return;
        n.c[laneId] = sliceNodes(curveFor(kind), (n.s - S0) / span, (n.s + n.l - S0) / span);
      });
    }
    return editNotes(clip, sel.map(n => n.id), n => {
      const kind = kindOf(n);
      if (needKind && kind !== needKind) return;
      n.c[laneId] = shapeNodes(key, laneId, Object.assign({}, o, { kind, idh: idHash(n.id), beats: n.l }));
    });
  }

  // ======================================================================== lane values to real units
  function grainBase(inst) {
    if (inst.engine === 'voice') return { base: inst.grainVoice, unit: 'cycles' };
    if (inst.engine === 'cloud') return inst.lock ? { base: inst.grainLock, unit: 'wavelengths' } : { base: inst.grainCloud, unit: 'ms' };
    return { base: 1, unit: 'x' };
  }
  // Lane value -> real value. ctx = { inst, bpm, travelKind }. The relative Sample lanes (heads, grain,
  // refresh) resolve against the instrument's own settings; macros, offsets and wheels are live and are
  // NOT included. Units: pitch/formant/vib semitones, amp gain, tone Hz, hollow dB, travel signed speed
  // (speed kind) or 0..1 of the Region (path kind), heads count, grain in the engine's unit (Tape: the
  // bare multiplier), refresh period in ms (0 = Smooth).
  function mapLane(laneId, v, ctx) {
    const c = ctx || {};
    if (!Number.isFinite(v)) v = laneDef(laneId);   // a NaN node reads as the lane default (never prints NaN)
    switch (laneId) {
      case 'pitch': return (v - 0.5) * 24;
      case 'amp': return v * v * 1.3;
      case 'vib': return v * 1;
      case 'tone': return 60 * Math.pow(2, v * 8.3);
      case 'formant': return (v - 0.5) * 24;
      case 'hollow': return -v * 28;
      // speed: read the Float32 value the engine's table will hold, so the dead-band edges agree with it
      case 'travel': return c.travelKind === 'path' ? clamp(v, 0, 1) : travelSpeed(Math.fround(v));
    }
    const inst = normalizeInst(c.inst);
    switch (laneId) {
      case 'heads': return clamp(inst.heads + (v - 0.5) * 14, 1, 8);
      case 'grain': return grainBase(inst).base * Math.pow(2, (v - 0.5) * 4);
      case 'refresh': {
        // read the Float32 value the engine's table will hold, as for travel
        const vf = Math.fround(v);
        if (refreshSmooth(vf) || inst.refresh.mode === 'smooth') return 0;
        const bpm = clamp(num(c.bpm, 120), 1, 1000);
        const baseMs = inst.refresh.mode === 'sync' ? rateBeats(inst.refresh.rate) * 60000 / bpm : inst.refresh.ms;
        return baseMs / Math.pow(2, (vf - 0.5) * 6);
      }
    }
    throw new Error('SLModel.mapLane: unknown lane ' + laneId);
  }
  const trimNum = s => (s.indexOf('.') >= 0 ? s.replace(/0+$/, '').replace(/\.$/, '') : s);
  function fmtMag(x) { return x >= 10 ? x.toFixed(0) : x >= 1 ? trimNum(x.toFixed(1)) : trimNum(x.toFixed(2)); }
  function fmtSemis(x) {
    const r = Math.round(x * 10) / 10;
    if (Math.abs(r) < 0.05) return '0 st';
    return (r > 0 ? '+' : '-') + (Number.isInteger(r) ? String(Math.abs(r)) : Math.abs(r).toFixed(1)) + ' st';
  }
  function fmtDb(db) { if (!Number.isFinite(db)) return '-inf dB'; const r = Math.round(db * 10) / 10; return (r > 0 ? '+' : r < 0 ? '-' : '') + Math.abs(r).toFixed(1) + ' dB'; }
  // Lane value -> readout text, e.g. '+3 st', '0.5x backwards', '2.4 kHz', '-6.0 dB', 'Refresh 1/16 (125 ms at 120 bpm)'.
  function formatLane(laneId, v, ctx) {
    if (!Number.isFinite(v)) v = laneDef(laneId);
    const c = ctx || {}, x = mapLane(laneId, v, c);
    switch (laneId) {
      case 'pitch': case 'formant': return fmtSemis(x);
      case 'amp': return fmtDb(x > 0 ? 20 * Math.log10(x) : -Infinity);
      case 'vib': return x.toFixed(2) + ' st vibrato';
      case 'tone': return x >= 1000 ? (x / 1000).toFixed(1) + ' kHz' : Math.round(x) + ' Hz';
      case 'hollow': return fmtDb(x);
      case 'travel':
        if (c.travelKind === 'path') return Math.round(x * 100) + '% of the Region';
        if (x === 0) return 'stopped';
        return fmtMag(Math.abs(x)) + 'x ' + (x > 0 ? 'forwards' : 'backwards');
      case 'heads': { const r = Math.round(x * 10) / 10; return (Number.isInteger(r) ? String(r) : r.toFixed(1)) + (r === 1 ? ' head' : ' heads'); }
      case 'grain': {
        const g = grainBase(normalizeInst(c.inst));
        if (g.unit === 'x') return 'Grain x' + fmtMag(x) + ' (no grains in Tape)';
        return g.unit === 'ms' ? Math.round(x) + ' ms' : trimNum(x.toFixed(1)) + ' ' + g.unit;
      }
      case 'refresh': {
        if (x === 0) return 'Smooth';
        const inst = normalizeInst(c.inst), ms = Math.round(x);
        if (inst.refresh.mode === 'sync') {
          const bpm = clamp(num(c.bpm, 120), 1, 1000), beats = x * bpm / 60000, id = rateFromBeats(beats, 0.01);
          return 'Refresh ' + (id || trimNum(beats.toFixed(3)) + ' beat') + ' (' + ms + ' ms at ' + Math.round(bpm) + ' bpm)';
        }
        return 'Refresh ' + ms + ' ms';
      }
    }
    return String(x);
  }

  // ======================================================================== connect (Y5, D5 = 'next')
  function connect(clip, ids, mode, seed) {
    const set = idSet(clip, ids);
    const sel = timeOrder(clip.notes.filter(n => set.has(n.id)));
    const sd = num(seed, 1) | 0;
    // existing links among the selection are replaced; links from outside the selection still count
    const taken = new Set();
    for (const n of clip.notes) if (n.glideTo && !(set.has(n.id) && set.has(n.glideTo))) taken.add(n.glideTo);
    const link = new Map();
    for (const n of sel) {
      let cand = sel.filter(m => m !== n && m.s > n.s && m.s >= n.s + n.l - 0.13 && Math.abs(m.p - n.p) <= 24);
      if (!cand.length) continue;
      const s0 = Math.min(...cand.map(m => m.s));
      cand = cand.filter(m => m.s <= s0 + 0.05);
      let order;
      if (mode === 'random') {
        const key = m => rand01(hash32(idHash(n.id), idHash(m.id), sd));
        order = cand.slice().sort((a, b) => key(a) - key(b) || timeCmp(a, b));
      } else if (mode === 'furthest') {
        order = cand.slice().sort((a, b) => Math.abs(b.p - n.p) - Math.abs(a.p - n.p) || timeCmp(a, b));
      } else {
        order = cand.slice().sort((a, b) => Math.abs(a.p - n.p) - Math.abs(b.p - n.p) || timeCmp(a, b));
      }
      const tgt = order.find(m => !taken.has(m.id));
      if (!tgt) continue;
      taken.add(tgt.id); link.set(n.id, tgt.id);
    }
    return editNotes(clip, sel.map(n => n.id), n => {
      if (link.has(n.id)) n.glideTo = link.get(n.id);
      else if (n.glideTo && set.has(n.glideTo)) n.glideTo = null;
    });
  }
  function disconnect(clip, ids) {
    const set = idSet(clip, ids);
    return editNotes(clip, [...set].filter(id => clip.notes.some(n => n.id === id && n.glideTo && set.has(n.glideTo))), n => { n.glideTo = null; });
  }

  // ======================================================================== tools
  // Walk: spreads a value from the first to the last selected note in time order. field = 'start'
  // (Start position in the Region, mode set to 'region'), 'vel', or a lane id (the lane becomes flat).
  function walk(clip, ids, field, from, to) {
    if (field !== 'start' && field !== 'vel') laneIndex(field);
    const sel = timeOrder(selected(clip, ids)), N = sel.length;
    const val = new Map(sel.map((n, i) => [n.id, from + (to - from) * (N > 1 ? i / (N - 1) : 0)]));
    return editNotes(clip, sel.map(n => n.id), n => {
      const v = clamp(val.get(n.id), 0, 1);
      if (field === 'start') n.sample.start = Object.assign({}, n.sample.start, { mode: 'region', pos: v });
      else if (field === 'vel') n.vel = v;
      else n.c[field] = flat(v);
    });
  }
  // Chop every selected note into pieces of `rate` (from the note's start; the last piece may be short).
  // Piece 0 keeps the note's id (links into it stay); the note's own link moves to its last piece. Pieces
  // after the first have Restart sample here on (a chop retriggers). stepStart = Start step per piece in
  // Region fractions (pieces get mode 'region'); true = spread the pieces evenly across the Region.
  // Refuses (returns the input clip and refused:true) when the clip would hold more than maxNotes notes.
  function chop(clip, ids, rate, stepStart, maxNotes) {
    const maxN = maxNotes == null ? 1024 : maxNotes;
    const step = rateBeats(rate);
    const sel = selected(clip, ids);
    const set = new Set(sel.map(n => n.id));
    const pieces = n => Math.max(1, Math.ceil(n.l / step - 1e-6));
    const counts = sel.map(pieces);
    const total = clip.notes.length - sel.length + counts.reduce((a, b) => a + b, 0);
    if (total > maxN) return { clip, refused: true, count: total };
    const taken = new Set(clip.notes.map(n => n.id));
    const out = [];
    for (const n of clip.notes) {
      if (!set.has(n.id)) { out.push(n); continue; }
      const K = pieces(n);
      const dStart = stepStart === true ? 1 / K : num(stepStart, 0);
      for (let k = 0; k < K; k++) {
        const s = n.s + k * step, e = k === K - 1 ? n.s + n.l : Math.min(n.s + n.l, s + step);
        if (e - s < 1e-6) continue;
        const c = cloneNote(n);
        if (k > 0) {
          c.id = uniqueId(taken, 'chop:' + n.id, k);
          // a 'Different' random line keeps the original note's values (Y7: random stays until Re-roll)
          for (const L of LANES) { const g = c.gen[L.id]; if (g && g.perNote === 'different' && !g.key) c.gen[L.id] = Object.assign({}, g, { key: n.id }); }
        }
        c.s = s; c.l = e - s;
        const u0 = (s - n.s) / n.l, u1 = (e - n.s) / n.l;
        for (const L of LANES) c.c[L.id] = sliceNodes(noteLane(n, L.id), u0, u1);
        c.glideTo = k === K - 1 ? n.glideTo : null;
        if (k > 0) c.sample.restart = true;
        if (dStart) { const st = n.sample.start; const pos = st.pos + k * dStart; c.sample.start = Object.assign({}, st, { mode: 'region', pos: pos - Math.floor(pos + 1e-12) }); }
        out.push(c);
      }
    }
    return { clip: Object.assign({}, clip, { notes: out }), refused: false, count: total };
  }
  function laneEdit(clip, ids, laneId, fn) { laneIndex(laneId); return editNotes(clip, ids, n => { n.c[laneId] = fn(n.c[laneId], n); }); }
  const reverseLane = (clip, ids, laneId) => laneEdit(clip, ids, laneId, nd => reverseNodes(nd));
  // Flip: v -> 1 - v of what plays. The generator riding on the lane flips with the drawing (invert), so
  // clamp(1 - d - g) = 1 - clamp(d + g): on Travel (Speed) every sample changes direction.
  const flipLane = (clip, ids, laneId) => laneEdit(clip, ids, laneId, (nd, n) => {
    const g = n.gen[laneId];
    if (g) n.gen[laneId] = Object.assign({}, g, { invert: !g.invert });
    return flipNodes(nd);
  });
  // More / Less: values move away from / towards the lane default by `factor`; generator depth and
  // amount scale by the same factor. Travel (Speed kind) scales each direction on its own (forward values
  // around 1x = 0.75, backward values around -1x = 0.25) and never crosses the dead band, so More and Less
  // change how fast a note travels but never its direction, and never stop or start it.
  function scaleLane(clip, ids, laneId, factor) {
    const f = Math.max(0, num(factor, 1)), def = laneDef(laneId);
    return laneEdit(clip, ids, laneId, (nd, n) => {
      const g = n.gen[laneId];
      if (g) n.gen[laneId] = Object.assign({}, g, { d0: clamp(g.d0 * f, 0, 1), d1: clamp(g.d1 * f, 0, 1), amount: clamp(g.amount * f, 0, 1) });
      if (laneId === 'travel' && n.sample.travel !== 'path') {
        return nd.map(q => {
          const s = travelSpeed(Math.fround(q.v));
          const v = s > 0 ? clamp(0.75 + (q.v - 0.75) * f, V_FWD_MIN, 1) : s < 0 ? clamp(0.25 + (q.v - 0.25) * f, 0, V_BACK_MAX) : q.v;
          return { t: q.t, v, shape: q.shape, bend: q.bend };
        });
      }
      return scaleNodes(nd, def, f);
    });
  }
  // Ids (in time order). kind: 'pitch' (same p), 'length' (same l), 'bar' (starts in the same bar),
  // 'everyOther' (every other note of `ids`, keeping the ref's parity), 'invert' (notes not in `ids`).
  function selectSimilar(clip, refId, kind, ids) {
    const ref = clip.notes.find(n => n.id === refId);
    const all = timeOrder(clip.notes);
    switch (kind) {
      case 'pitch': return ref ? all.filter(n => n.p === ref.p).map(n => n.id) : [];
      case 'length': return ref ? all.filter(n => Math.abs(n.l - ref.l) < 1e-6).map(n => n.id) : [];
      case 'bar': { if (!ref) return []; const b = Math.floor(ref.s / BEATS_PER_BAR + 1e-9); return all.filter(n => Math.floor(n.s / BEATS_PER_BAR + 1e-9) === b).map(n => n.id); }
      case 'everyOther': {
        const set = ids == null || !idSet(clip, ids).size ? null : idSet(clip, ids);
        const list = all.filter(n => !set || set.has(n.id));
        const ri = list.findIndex(n => n.id === refId), par = ri < 0 ? 0 : ri % 2;
        return list.filter((_, i) => i % 2 === par).map(n => n.id);
      }
      case 'invert': { const set = ids == null ? new Set() : idSet(clip, ids); return all.filter(n => !set.has(n.id)).map(n => n.id); }
    }
    throw new Error('SLModel.selectSimilar: unknown kind ' + kind);
  }
  // Humanize: seeded offsets. time = max start shift (beats), vel = max velocity change, lanes = max lane
  // shift, either { laneId: amount } or one number for Volume and Tone.
  function humanize(clip, ids, amounts, seed) {
    const a = amounts || {}, sd = num(seed, 1) | 0;
    const time = Math.max(0, num(a.time, 0)), vel = Math.max(0, num(a.vel, 0));
    const lanes = typeof a.lanes === 'number' ? { amp: a.lanes, tone: a.lanes } : (a.lanes || {});
    Object.keys(lanes).forEach(laneIndex);
    return editNotes(clip, ids, n => {
      const h = idHash(n.id), r = k => rand01(hash32(h, k, sd)) * 2 - 1;
      if (time) n.s = Math.max(0, n.s + r(1) * time);
      if (vel) n.vel = clamp(n.vel + r(2) * vel, 0.05, 1);
      for (const id of Object.keys(lanes)) {
        const amt = Math.max(0, num(lanes[id], 0)); if (!amt) continue;
        const off = r(10 + laneIndex(id)) * amt;
        n.c[id] = n.c[id].map(q => ({ t: q.t, v: clamp(q.v + off, 0, 1), shape: q.shape, bend: q.bend }));
      }
    });
  }
  // Strum: chords (selected notes starting within 0.05 beat of each other) are spread by `rate` per
  // note, low to high ('up') or high to low ('down'); every note keeps its end.
  function strum(clip, ids, rate, dir) {
    const step = rateBeats(rate), sel = timeOrder(selected(clip, ids)), shift = new Map();
    for (let i = 0; i < sel.length;) {
      const s0 = sel[i].s; let j = i;
      while (j < sel.length && sel[j].s <= s0 + 0.05) j++;
      const grp = sel.slice(i, j).sort((a, b) => (dir === 'down' ? b.p - a.p : a.p - b.p) || timeCmp(a, b));
      grp.forEach((n, k) => shift.set(n.id, k * step));
      i = j;
    }
    return editNotes(clip, sel.map(n => n.id), n => {
      const d = shift.get(n.id) || 0; if (!d) return;
      const end = n.s + n.l;
      n.s = Math.max(n.s, Math.min(n.s + d, end - Math.min(MIN_LEN, n.l))); n.l = end - n.s;
    });
  }
  // Legato: each selected note reaches the start of the next later chord in the selection (extending or
  // shortening). Starts within 0.05 beat of a chord's first start belong to that chord (as Strum, Connect and
  // the auto glides group them), so a humanized chord still reaches the next chord.
  function legato(clip, ids) {
    const sel = timeOrder(selected(clip, ids));
    const chordStart = [], chordOf = new Map();
    for (const n of sel) {
      if (!chordStart.length || n.s > chordStart[chordStart.length - 1] + 0.05) chordStart.push(n.s);
      chordOf.set(n.id, chordStart.length - 1);
    }
    return editNotes(clip, sel.map(n => n.id), n => {
      const next = chordStart[chordOf.get(n.id) + 1];
      if (next != null) n.l = Math.max(MIN_LEN, next - n.s);
    });
  }

  // ======================================================================== note settings (S7)
  const SAMPLE_FIELDS = ['motion', 'travel', 'tape', 'rev', 'jump', 'engine', 'lock', 'restart'];
  function applyNotePatch(n, patch) {
    const ok = v => v !== undefined && v !== 'mixed';
    for (const k of SAMPLE_FIELDS) {
      if (!has(patch, k) || !ok(patch[k])) continue;
      const v = patch[k];
      if (k === 'motion' && (v === null || MOTIONS.includes(v))) n.sample.motion = v;
      else if (k === 'travel' && TRAVEL_KINDS.includes(v)) n.sample.travel = v;
      else if ((k === 'tape' || k === 'rev' || k === 'lock') && (v === null || v === true || v === false)) n.sample[k] = v;
      else if (k === 'jump' && (v === null || isRate(v))) n.sample.jump = v;
      else if (k === 'engine' && (v === null || ENGINES.includes(v))) n.sample.engine = v;
      else if (k === 'restart' && (v === true || v === false)) n.sample.restart = v;
    }
    if (patch.start && typeof patch.start === 'object') {
      const st = Object.assign({}, n.sample.start), p = patch.start;
      if (ok(p.mode) && START_MODES.includes(p.mode)) st.mode = p.mode;
      if (ok(p.pos) && Number.isFinite(p.pos)) st.pos = clamp(p.pos, 0, 1);
      if (ok(p.range) && Number.isFinite(p.range)) st.range = clamp(p.range, 0, 1);
      if (ok(p.seed) && Number.isFinite(p.seed)) st.seed = Math.trunc(p.seed) | 0;
      n.sample.start = st;
    }
    if (has(patch, 'vel') && ok(patch.vel) && Number.isFinite(patch.vel)) n.vel = clamp(patch.vel, 0, 1);
    if (has(patch, 'glide') && ok(patch.glide) && (patch.glide === null || Number.isFinite(patch.glide))) n.glide = patch.glide === null ? null : Math.max(0, patch.glide);
    if (has(patch, 'gc') && ok(patch.gc) && Number.isFinite(patch.gc)) n.gc = clamp(patch.gc, -1, 1);
    return n;
  }
  // patch: any of motion, travel, start:{mode,pos,range,seed}, tape, rev, jump, engine, lock, restart
  // (null = follow the instrument), plus vel, glide, gc. Invalid values and 'mixed' are ignored.
  function setNoteSample(clip, ids, patch) { return editNotes(clip, ids, n => { applyNotePatch(n, patch || {}); }); }
  // Per field the value all the notes share, or 'mixed'. null when no note is selected.
  function getMixed(clip, ids) {
    const sel = selected(clip, ids);
    if (!sel.length) return null;
    const common = get => { const v0 = get(sel[0]); return sel.every(n => get(n) === v0) ? v0 : 'mixed'; };
    const out = { count: sel.length, start: {} };
    for (const k of SAMPLE_FIELDS) out[k] = common(n => n.sample[k]);
    for (const k of ['mode', 'pos', 'range']) out.start[k] = common(n => n.sample.start[k]);
    for (const k of ['vel', 'glide', 'gc']) out[k] = common(n => n[k]);
    return out;
  }
  // Lane-mode rule: changes only that lane of that note.
  function setLaneNodes(clip, id, laneId, nodes) {
    laneIndex(laneId);
    const nd = normalizeNodes(nodes, laneDef(laneId));
    let hit = false;
    const notes = clip.notes.map(n => {
      if (n.id !== id) return n;
      hit = true;
      const c = Object.assign({}, n.c, { [laneId]: nd });
      return Object.assign({}, n, { c });
    });
    return hit ? Object.assign({}, clip, { notes }) : Object.assign({}, clip, { notes: clip.notes.slice() });
  }
  // Several notes' nodes on one lane at once (the Nodes box drag): map = { noteId: nodes }.
  function setLaneNodesMany(clip, laneId, map) {
    laneIndex(laneId);
    const notes = clip.notes.map(n => (map && has(map, n.id) ? Object.assign({}, n, { c: Object.assign({}, n.c, { [laneId]: normalizeNodes(map[n.id], laneDef(laneId)) }) }) : n));
    return Object.assign({}, clip, { notes });
  }

  // ======================================================================== glides and chains (E2)
  // v3's glide curve.
  function gShape(u, c) {
    u = clamp(u, 0, 1);
    const sm = u * u * (3 - 2 * u);
    if (c > 0) return sm * (1 - c) + (1 - Math.pow(1 - u, 1 + 4 * c)) * c;
    if (c < 0) return sm * (1 + c) + Math.pow(u, 1 - 4 * c) * (-c);
    return sm;
  }
  const startSemis = n => (noteLane(n, 'pitch')[0].v - 0.5) * 24;
  const endSemis = n => { const nd = noteLane(n, 'pitch'); return (nd[nd.length - 1].v - 0.5) * 24; };
  // Glide size: from where note n really ends (its p plus its last pitch node) to where m really starts
  // (its p plus its first pitch node), so the slide lands on the drawn start of m.
  const glideD = (n, m) => clamp((m.p + startSemis(m)) - (n.p + endSemis(n)), -48, 48);
  // v3's computeGlides, fixed: d aims at the target's real start; glide 0 = instant jump (len 0) that
  // still joins; null = inst.glideTime; explicit links work whatever the auto mode.
  function computeGlides(clip, inst) {
    const I = normalizeInst(inst);
    const notes = clip.notes, byId = new Map(notes.map(n => [n.id, n])), out = new Map();
    const glen = n => Math.max(0, n.glide == null ? I.glideTime : n.glide);
    for (const n of notes) {
      if (!n.glideTo) continue;
      const m = byId.get(n.glideTo);
      if (!m || m === n) continue;
      out.set(n.id, { d: glideD(n, m), len: Math.min(glen(n), n.l * 0.95), curve: n.gc || 0, toId: m.id, linked: true });
    }
    if (I.glideMode === 'off') return out;
    const tol = 0.13, reach = I.glideMode === 'always' ? 4 : tol;
    // a note an explicit link already leads into is not an auto target (one voice per target)
    const linkedTo = new Set([...out.values()].map(g => g.toId));
    const byP = (a, b) => a.p - b.p || timeCmp(a, b);
    // 1. every auto note's candidate group: the earliest following notes (within 0.05 beat) inside +-12 st
    const info = [];
    for (const n of notes) {
      if (out.has(n.id)) continue;
      const end = n.s + n.l, hi = end + reach;
      const cand = notes.filter(m => m !== n && !linkedTo.has(m.id) && m.s > n.s + 1e-3 && m.s >= end - tol && m.s <= hi && Math.abs(m.p - n.p) <= 12);
      if (!cand.length) continue;
      const s0 = Math.min(...cand.map(m => m.s));
      info.push({ n, end, s0, group: cand.filter(m => m.s <= s0 + 0.05).sort(byP) });
    }
    // 2. voice leading by rank among peers: notes that end together, lead into the same next chord and share at
    //    least one candidate (so a bass that can only reach the next bass note is not a peer of the chord above)
    const root = info.map((_, i) => i);
    const find = i => { while (root[i] !== i) i = root[i] = root[root[i]]; return i; };
    for (let i = 0; i < info.length; i++) for (let j = i + 1; j < info.length; j++) {
      const a = info[i], b = info[j];
      if (Math.abs(a.end - b.end) < 0.05 && Math.abs(a.s0 - b.s0) < 0.05 && a.group.some(m => b.group.includes(m))) root[find(i)] = find(j);
    }
    const clusters = new Map();
    info.forEach((a, i) => { const r = find(i); if (!clusters.has(r)) clusters.set(r, []); clusters.get(r).push(a); });
    for (const peers of clusters.values()) {
      peers.sort((a, b) => byP(a.n, b.n));
      const union = [...new Set([].concat(...peers.map(a => a.group)))].sort(byP);
      peers.forEach((a, r) => {
        const n = a.n, nearest = want => a.group.reduce((x, y) => (Math.abs(y.p - want) < Math.abs(x.p - want) ? y : x));
        let tgt;
        if (peers.length > 1 && union.length > 1) {
          const ranked = union[Math.round(r * (union.length - 1) / (peers.length - 1))];
          tgt = a.group.includes(ranked) ? ranked : nearest(ranked.p);
        } else tgt = nearest(n.p);
        out.set(n.id, { d: glideD(n, tgt), len: Math.min(glen(n), n.l * 0.95), curve: n.gc || 0, toId: tgt.id, linked: false });
      });
    }
    return out;
  }
  // Voices: arrays of note ids, each played by one voice. A glide joins two notes unless the target has
  // Restart sample here, or the target does not start after the source. A target joins at most one source:
  // an explicit link first, then the smallest glide, then the earliest source.
  function buildChains(clip, glides) {
    const notes = clip.notes, byId = new Map(notes.map(n => [n.id, n]));
    const G = glides || computeGlides(clip);
    const incoming = new Map();
    for (const [sid, g] of G) {
      const n = byId.get(sid), m = byId.get(g.toId);
      if (!n || !m || m === n || m.sample.restart || !(m.s > n.s)) continue;
      if (!incoming.has(m.id)) incoming.set(m.id, []);
      incoming.get(m.id).push({ n, g });
    }
    const next = new Map(), prev = new Map();
    for (const [mid, list] of incoming) {
      list.sort((a, b) => (b.g.linked - a.g.linked) || Math.abs(a.g.d) - Math.abs(b.g.d) || timeCmp(a.n, b.n));
      const src = list[0].n.id;
      next.set(src, mid); prev.set(mid, src);
    }
    const chains = [], seen = new Set();
    const walkFrom = h => { const ch = []; let id = h; while (id && !seen.has(id)) { seen.add(id); ch.push(id); id = next.get(id); } return ch; };
    for (const n of timeOrder(notes)) if (!prev.has(n.id) && !seen.has(n.id)) chains.push(walkFrom(n.id));
    for (const n of timeOrder(notes)) if (!seen.has(n.id)) chains.push(walkFrom(n.id));   // cycle guard
    return chains;
  }

  // ======================================================================== snapshot (BLUEPRINT 3.2)
  function compileSnapshot(clip, inst, opts) {
    const res = Math.max(8, Math.round(num(opts && opts.res, 256)));
    const I = normalizeInst(inst);
    const c = normalizeClip(clip);
    const glides = computeGlides(c, I), chains = buildChains(c, glides), chainOf = new Map();
    for (const ch of chains) ch.forEach((id, i) => chainOf.set(id, { id: ch[0], index: i, prevId: i > 0 ? ch[i - 1] : null, nextId: i < ch.length - 1 ? ch[i + 1] : null }));
    const notes = timeOrder(c.notes).map(n => {
      const sm = n.sample, engine = sm.engine || I.engine, g = glides.get(n.id);
      const lanes = {};
      for (const L of LANES) lanes[L.id] = laneTable(n, L.id, res, I);
      return {
        id: n.id, idHash: idHash(n.id), p: n.p, s: n.s, l: n.l, vel: n.vel,
        engine, lock: engine === 'cloud' ? (sm.lock == null ? I.lock : sm.lock) : false,
        motion: sm.motion || I.motion, travel: sm.travel === 'path' ? 'path' : 'speed',
        start: { mode: sm.start.mode, pos: sm.start.pos, range: sm.start.range, seed: sm.start.seed },
        tape: sm.tape == null ? I.tape : sm.tape, rev: sm.rev == null ? I.rev : sm.rev,
        jump: rateBeats(sm.jump || I.jump), restart: !!sm.restart,
        glide: g ? { d: g.d, len: g.len, curve: g.curve, toId: g.toId } : null,
        chain: chainOf.get(n.id),
        lanes,
      };
    });
    return { version: SNAPSHOT_VERSION, bars: c.bars, beatsPerBar: BEATS_PER_BAR, res, notes };
  }

  // ======================================================================== undo (History)
  // Stores immutable versions (JSON text, so later edits to the pushed objects cannot reach it) of any
  // plain state: a clip, or { clip, inst, ... } when an instrument change (Re-roll, Dice, a recipe)
  // should be one undo step too. Each note is kept as its own text, and a note that did not change
  // since the current version shares that version's text, so 200 steps cost about one clip plus the
  // changed notes. undo()/redo()/current() return fresh copies.
  function splitState(state) {
    const clip = state && Array.isArray(state.notes) ? state : (state && state.clip && Array.isArray(state.clip.notes) ? state.clip : null);
    if (!clip) return { whole: JSON.stringify(state === undefined ? null : state) };
    const inClip = clip !== state;
    const shell = inClip ? Object.assign({}, state, { clip: Object.assign({}, clip, { notes: null }) }) : Object.assign({}, state, { notes: null });
    return { shell: JSON.stringify(shell), notes: clip.notes.map(n => JSON.stringify(n)), inClip };
  }
  function joinState(v) {
    if (v.whole !== undefined) return JSON.parse(v.whole);
    const s = JSON.parse(v.shell), notes = v.notes.map(x => JSON.parse(x));
    if (v.inClip) s.clip.notes = notes; else s.notes = notes;
    return s;
  }
  function sameState(a, b) {
    if (!a || !b || a.whole !== b.whole || a.shell !== b.shell || a.inClip !== b.inClip) return false;
    if (a.whole !== undefined) return true;
    if (a.notes.length !== b.notes.length) return false;
    for (let i = 0; i < a.notes.length; i++) if (a.notes[i] !== b.notes[i]) return false;
    return true;
  }
  class History {
    constructor(initial, limit) {
      this.limit = Math.max(1, num(limit, 200) | 0);
      this.stack = []; this.labels = []; this.i = -1;
      if (initial !== undefined) this.push(initial, 'start');
    }
    push(state, label) {
      const v = splitState(state), cur = this.i >= 0 ? this.stack[this.i] : null;
      if (cur && sameState(cur, v)) return false;
      if (cur && cur.notes && v.notes) { const pool = new Map(cur.notes.map(s => [s, s])); v.notes = v.notes.map(s => pool.get(s) || s); }
      this.stack.length = this.i + 1; this.labels.length = this.i + 1;
      this.stack.push(v); this.labels.push(label || '');
      while (this.stack.length > this.limit + 1) { this.stack.shift(); this.labels.shift(); }
      this.i = this.stack.length - 1;
      return true;
    }
    canUndo() { return this.i > 0; }
    canRedo() { return this.i >= 0 && this.i < this.stack.length - 1; }
    undoLabel() { return this.canUndo() ? this.labels[this.i] : null; }
    redoLabel() { return this.canRedo() ? this.labels[this.i + 1] : null; }
    undo() { if (!this.canUndo()) return null; this.i--; return joinState(this.stack[this.i]); }
    redo() { if (!this.canRedo()) return null; this.i++; return joinState(this.stack[this.i]); }
    current() { return this.i >= 0 ? joinState(this.stack[this.i]) : null; }
    get size() { return this.stack.length; }
    // characters of note text actually held (shared text counted once), for memory checks
    storedChars() {
      const seen = new Set(); let n = 0;
      for (const v of this.stack) {
        n += (v.whole || v.shell || '').length;
        if (v.notes) for (const s of v.notes) if (!seen.has(s)) { seen.add(s); n += s.length; }
      }
      return n;
    }
    clear(state) { this.stack = []; this.labels = []; this.i = -1; if (state !== undefined) this.push(state, 'start'); }
  }

  // ======================================================================== recipes and dice (S8)
  // Every recipe is a complete set of Sample settings (Straight plus its own changes), so the result
  // does not depend on what was set before. Region, macros and the sound knobs are left alone.
  const STRAIGHT = {
    engine: 'voice', lock: false, motion: 'forward', speed: 1, heads: 1, spread: 0.25,
    grainVoice: 2, grainCloud: 140, grainLock: 4,
    refresh: { mode: 'smooth', ms: 120, rate: '1/16', lockToBeat: true, skip: 0 },
    spray: 0, tape: false, rev: false, drift: 0, headTune: 'same', jump: '1/8',
  };
  const recipeInst = patch => mergeInst(STRAIGHT, patch);
  // notes: per lane a Gen (an optional `base` sets the drawing flat first), a shape key (the drawing,
  // with that lane's generator cleared) or null (lane reset: flat default, no generator).
  const RECIPES = deepFreeze([
    { n: 1, name: 'Straight', inst: recipeInst({}), notes: { travel: null, heads: null, grain: null, refresh: null } },
    { n: 2, name: 'Backwards', inst: recipeInst({ motion: 'backward' }), notes: { travel: null } },
    { n: 3, name: 'Frozen choir', inst: recipeInst({ motion: 'freeze', heads: 4, spread: 0.15, headTune: 'detune', drift: 0.3, grainVoice: 3 }), notes: { travel: null } },
    { n: 4, name: 'Tape scratch', inst: recipeInst({ engine: 'tape' }), notes: { travel: { kind: 'pattern', shape: 'square', rate: '1/8', d0: 0.5, d1: 0.5, lockToBeat: true, base: 0.5 } } },
    { n: 5, name: 'Stutter 1/16', inst: recipeInst({ engine: 'tape', refresh: { mode: 'sync', ms: 120, rate: '1/16', lockToBeat: true, skip: 0 } }), notes: { refresh: null } },
    { n: 6, name: 'Slow bloom', inst: recipeInst({ engine: 'cloud', heads: 4, spread: 0.5, spray: 0.2, drift: 0.2, speed: 0.5 }), notes: { heads: 'up' } },
    { n: 7, name: 'Scatter rain', inst: recipeInst({ engine: 'cloud', motion: 'scatter', jump: '1/8T', grainCloud: 60, spray: 0.3, heads: 3, spread: 0.5 }), notes: null },
    { n: 8, name: 'Octave shimmer', inst: recipeInst({ engine: 'cloud', lock: true, heads: 3, headTune: 'octaves', spread: 0.3, drift: 0.2, grainLock: 4 }), notes: null },
  ]);
  // -> { clip, inst }. ids null = every note.
  function applyRecipe(clip, inst, n, ids) {
    const R = RECIPES.find(r => r.n === n);
    if (!R) throw new Error('SLModel.applyRecipe: no recipe ' + n);
    const outInst = normalizeInst(mergeInst(normalizeInst(inst), R.inst));
    let c = Object.assign({}, clip, { notes: clip.notes.slice() });
    if (R.notes) {
      for (const laneId of Object.keys(R.notes)) {
        const e = R.notes[laneId];
        if (e === null) c = editNotes(c, ids, q => { q.c[laneId] = flat(laneDef(laneId)); q.gen[laneId] = null; });
        else if (typeof e === 'string') c = clearGen(applyShape(c, ids, laneId, e, 'each'), ids, laneId);
        else {
          const g = normalizeGen(e);
          c = editNotes(c, ids, q => { if (e.base != null) q.c[laneId] = flat(clamp(e.base, 0, 1)); q.gen[laneId] = Object.assign({}, g); });
        }
      }
    }
    return { clip: c, inst: outInst };
  }
  const DICE_FIELDS = Object.freeze(['engine', 'lock', 'motion', 'speed', 'heads', 'spread', 'grain', 'refresh', 'spray', 'tape', 'rev', 'drift', 'headTune', 'jump']);
  // Dice: Sample settings rolled within musical limits. Each field has its own seeded roll, so pinning one
  // field never changes the others. pins: array / Set / { field: true } of DICE_FIELDS names ('grain'
  // pins grainVoice, grainCloud and grainLock; the exact inst key works too).
  function dice(inst, seed, pins) {
    const base = normalizeInst(inst), out = deepClone(base), sd = num(seed, 1) | 0;
    const pinned = new Set(pins instanceof Set ? [...pins] : Array.isArray(pins) ? pins : pins ? Object.keys(pins).filter(k => pins[k]) : []);
    const isPinned = k => pinned.has(k) || (['grainVoice', 'grainCloud', 'grainLock'].includes(k) && pinned.has('grain'));
    const r = (i, j) => rand01(hash32(0x0d1ce, i, j | 0, sd));
    const pick = (list, x) => list[Math.min(list.length - 1, Math.floor(x * list.length))];
    const round = (x, d) => Math.round(x * d) / d;
    if (!isPinned('engine')) out.engine = pick(['voice', 'voice', 'cloud', 'cloud', 'tape'], r(1));
    if (!isPinned('lock')) out.lock = out.engine === 'cloud' && r(2) < 0.4;
    if (!isPinned('motion')) out.motion = pick(out.engine === 'tape' ? ['forward', 'forward', 'backward', 'bounce', 'mirror', 'scatter'] : ['forward', 'forward', 'forward', 'backward', 'bounce', 'mirror', 'freeze', 'scatter'], r(3));
    if (!isPinned('speed')) out.speed = pick([0.25, 0.5, 0.5, 1, 1, 1, 2], r(4));
    if (!isPinned('heads')) out.heads = 1 + Math.floor(r(5) * 6);
    if (!isPinned('spread')) out.spread = round(r(6) * 0.6, 100);
    if (!isPinned('grainVoice')) out.grainVoice = pick([1, 1.5, 2, 2, 3, 4], r(7));
    if (!isPinned('grainCloud')) out.grainCloud = Math.round(40 * Math.pow(2, r(8) * 3.3));
    if (!isPinned('grainLock')) out.grainLock = pick([2, 3, 4, 4, 6, 8], r(9));
    if (!isPinned('refresh')) out.refresh = {
      mode: pick(['smooth', 'smooth', 'free', 'sync'], r(10)), ms: Math.round(30 * Math.pow(2, r(11) * 3.7)),
      rate: pick(['1/4', '1/8', '1/8T', '1/16', '1/16T', '1/32'], r(12)), lockToBeat: true, skip: pick([0, 0, 0.1, 0.25], r(13)),
    };
    if (!isPinned('spray')) out.spray = round(r(14) * 0.5, 100);
    if (!isPinned('tape')) out.tape = r(15) < 0.2;
    if (!isPinned('rev')) out.rev = r(16) < 0.15;
    if (!isPinned('drift')) out.drift = round(r(17) * 0.4, 100);
    if (!isPinned('headTune')) out.headTune = pick(['same', 'same', 'detune', 'octaves', 'fifths'], r(18));
    if (!isPinned('jump')) out.jump = pick(['1/4', '1/8', '1/8T', '1/16', '1/16T'], r(19));
    return normalizeInst(out);
  }

  // ======================================================================== presets (U4)
  const SOUND_PRESETS = deepFreeze([
    { id: 'init', name: 'Saw that opens up', sound: 'sawopen', inst: {} },
    { id: 'saw-tape', name: 'Plain saw on tape', sound: 'saw', inst: { engine: 'tape' } },
    { id: 'square-cloud', name: 'Square sweep cloud', sound: 'square', inst: { engine: 'cloud', heads: 2, spread: 0.3, grainCloud: 120 } },
    { id: 'pad-drift', name: 'Evolving pad, drifting', sound: 'pad', inst: { engine: 'cloud', heads: 3, spread: 0.4, drift: 0.3, spray: 0.15 } },
    { id: 'vowel-voice', name: 'Vowel voice', sound: 'vowel', inst: { engine: 'voice', grainVoice: 2 } },
    { id: 'pluck-tape', name: 'Pluck arpeggio on tape', sound: 'pluck', inst: { engine: 'tape', region: { atEnd: 'hold' } } },
    { id: 'beat-scatter', name: 'Beat loop scatter', sound: 'beat', inst: { engine: 'tape', motion: 'scatter', jump: '1/8' } },
    { id: 'noise-lock', name: 'Noise locked to the note', sound: 'noise', inst: { engine: 'cloud', lock: true, grainLock: 4 } },
  ]);
  // -> { sound, inst }: the sound id to load and the instrument with the preset's patch. Never touches a clip.
  // U4: a Sound preset is the sample, engine, Sample settings and macros, so it starts from Straight's Sample
  // settings, the full Region and centred macros (nothing leaks from the preset loaded before), then applies
  // its own patch. The sound knobs (pitch, root, glide, scale, wheels...) are left alone.
  const SOUND_BASE = Object.assign({}, STRAIGHT, { region: defaultInst().region, macros: defaultInst().macros });
  function applySoundPreset(inst, id) {
    const P = SOUND_PRESETS.find(p => p.id === id || p.name === id);
    if (!P) throw new Error('SLModel.applySoundPreset: no preset ' + id);
    return { sound: P.sound, inst: normalizeInst(mergeInst(mergeInst(normalizeInst(inst), SOUND_BASE), P.inst)) };
  }
  function presetBuilder(prefix) {
    const notes = []; let k = 0;
    const add = (p, s, l) => { const n = mkNote(p, s, l, prefix + '-' + (++k)); notes.push(n); return n; };
    return { notes, add };
  }
  // v3's demo(), with v3's nodes. (glide: 0 now means an instant jump that still joins, see NOTES-model.md)
  function strandDemo() {
    const { notes, add } = presetBuilder('sd');
    const sh = (k, lane) => shapeNodes(k, lane);
    [55, 60, 64, 67].forEach((p, k) => { const n = add(p, 0, 4); n.c.tone = sh('up', 'tone'); n.c.amp = nodesOf([[0, 0.35], [0.6, 0.9], [1, 0.75]]); if (k === 3) n.c.vib = nodesOf([[0, 0], [0.4, 0], [1, 0.55]]); });
    [57, 60, 64, 69].forEach((p, k) => { const n = add(p, 4, 4); n.c.hollow = sh('swell', 'hollow'); if (k === 3) n.c.vib = nodesOf([[0, 0.3], [1, 0.6]]); });
    const b1 = add(36, 0, 4), b2 = add(33, 4, 4);
    b1.c.formant = patternNodes({ shape: 'sine', cycles: 4, center: 0.5, d0: 0.1, d1: 0.7, acc: 0 });
    b2.glide = 0;
    [72, 76, 79, 76, 74, 72, 71, 74].forEach((p, k) => { const n = add(p, 8.5 + k * 0.5, 0.5); n.c.amp = sh('decay', 'amp'); n.c.tone = sh('down', 'tone'); n.glide = 0; });
    const c3 = [55, 59, 62, 67].map(p => add(p, 12, 4));
    c3.forEach(n => { n.c.tone = patternNodes({ shape: 'sine', cycles: 8, center: 0.55, d0: 0.05, d1: 0.8, acc: 0.6 }); n.c.amp = sh('down', 'amp').map(q => mkNode(q.t, 0.3 + 0.6 * q.v, q.shape, q.bend)); });
    const b3 = add(31, 8, 4), b4 = add(31, 12, 4);
    b3.c.pitch = nodesOf([[0, 0.5 - 5 / 24], [0.25, 0.5], [1, 0.5]]); b4.c.vib = nodesOf([[0, 0], [1, 0.4]]);
    const lead = add(84, 6, 2), lead2 = add(79, 10, 2);
    lead.glideTo = lead2.id; lead.glide = 1; lead2.c.vib = nodesOf([[0, 0.2], [1, 0.5]]);
    lead.c.pitch = nodesOf([[0, 0.5 - 2 / 24], [0.3, 0.5], [1, 0.5]]); lead.c.vib = nodesOf([[0, 0], [0.35, 0], [1, 0.7]]); lead.c.amp = sh('swell', 'amp');
    return { bars: 4, notes };
  }
  function clipPresetList() {
    const out = [];
    const def = (id, name, bars, build) => { const b = presetBuilder(id); build(b.add, b.notes); out.push({ id, name, bars, notes: b.notes }); };
    out.push(Object.assign({ id: 'strand-demo', name: 'Strand Demo' }, strandDemo()));
    def('pad-swell', 'Pad Swell', 4, add => {
      [[60, 64, 67, 71], [57, 60, 64, 67]].forEach((ch, i) => ch.forEach(p => { const n = add(p, i * 8, 8); n.c.amp = shapeNodes('swell', 'amp'); n.c.tone = nodesOf([[0, 0.45], [0.6, 0.85], [1, 0.6]]); }));
    });
    def('pluck-decay', 'Pluck Decay', 2, add => {
      [60, 64, 67, 72, 67, 64, 60, 55, 57, 60, 64, 69, 64, 60, 57, 52].forEach((p, k) => { const n = add(p, k * 0.5, 0.5); n.c.amp = shapeNodes('decay', 'amp'); n.c.tone = nodesOf([[0, 0.95], [0.3, 0.55], [1, 0.4]]); });
    });
    def('vowel-morph', 'Vowel Morph', 4, add => {
      [[48, 0, 8], [55, 0, 8], [53, 8, 8], [60, 8, 8]].forEach(([p, s, l], k) => {
        const n = add(p, s, l); n.c.formant = k % 2 ? shapeNodes('down', 'formant') : shapeNodes('up', 'formant');
        n.gen.formant = makeGen('pattern', { shape: 'squiggle', rate: '1/4', d0: 0.05, d1: 0.25, lockToBeat: true });
      });
    });
    def('legato-lead', 'Legato Lead', 4, add => {
      [[72, 0, 1.5], [74, 1.5, 0.5], [76, 2, 2], [79, 4, 1], [77, 5, 1], [76, 6, 2], [74, 8, 1.5], [72, 9.5, 0.5], [71, 10, 2], [72, 12, 4]].forEach(([p, s, l]) => {
        const n = add(p, s, l); n.c.vib = nodesOf([[0, 0], [0.4, 0], [1, 0.45]]); n.c.amp = nodesOf([[0, 0.7], [0.5, 0.85], [1, 0.75]]);
      });
    });
    def('hollow-drone', 'Hollow Drone', 4, add => {
      [36, 43].forEach(p => { const n = add(p, 0, 16); n.c.hollow = shapeNodes('swell', 'hollow'); n.gen.tone = makeGen('pattern', { shape: 'squiggle', rate: '1bar', d0: 0.3, d1: 0.3 }); n.c.tone = flat(0.6); });
    });
    def('saw-mirror-pad', 'Saw Mirror Pad', 4, add => {
      [48, 55, 60, 64].forEach(p => { const n = add(p, 0, 16); n.sample.motion = 'mirror'; n.c.heads = flat(0.72); n.c.amp = shapeNodes('swell', 'amp'); n.c.tone = nodesOf([[0, 0.5], [1, 0.9]]); });
    });
    def('backward-vowel', 'Backward Vowel', 4, add => {
      [[60, 0, 4], [63, 4, 4], [67, 8, 4], [65, 12, 4]].forEach(([p, s, l]) => { const n = add(p, s, l); n.sample.motion = 'backward'; n.c.formant = shapeNodes('down', 'formant'); n.c.amp = shapeNodes('swell', 'amp'); });
    });
    def('scatter-18t', 'Scatter 1/8T', 2, add => {
      [[48, 0, 8], [60, 0, 8], [67, 0, 8]].forEach(([p, s, l]) => { const n = add(p, s, l); n.sample.motion = 'scatter'; n.sample.jump = '1/8T'; n.sample.engine = 'cloud'; n.c.amp = nodesOf([[0, 0.6], [0.5, 0.85], [1, 0.6]]); });
    });
    def('tape-stop-lead', 'Tape Stop Lead', 4, add => {
      [[72, 0, 2], [75, 2, 2], [79, 4, 4], [77, 8, 2], [75, 10, 2], [72, 12, 4]].forEach(([p, s, l], k, arr) => {
        const n = add(p, s, l); n.sample.engine = 'tape';
        if (k === arr.length - 1 || k === 2) n.c.travel = shapeNodes('tapestop', 'travel', { kind: 'speed' });
      });
    });
    def('bloom', 'Bloom', 4, add => {
      [[48, 0, 16], [55, 0, 16], [64, 0, 16], [71, 4, 12]].forEach(([p, s, l]) => { const n = add(p, s, l); n.sample.engine = 'cloud'; n.c.heads = shapeNodes('up', 'heads'); n.c.amp = shapeNodes('swell', 'amp'); });
    });
    out.push(Object.assign({ id: 'walk-through', name: 'Walk Through' }, (() => {
      const b = presetBuilder('wt');
      [60, 60, 60, 60, 60, 60, 60, 60, 67, 67, 67, 67, 65, 65, 64, 64].forEach((p, k) => b.add(p, k, 1));
      const clip = walk({ bars: 4, notes: b.notes }, null, 'start', 0, 0.9375);
      return { bars: 4, notes: clip.notes };
    })()));
    return out;
  }
  const CLIP_PRESETS = deepFreeze(clipPresetList());
  // -> a fresh clip (bars + notes). Never touches the instrument.
  function loadClipPreset(id) {
    const P = CLIP_PRESETS.find(p => p.id === id || p.name === id);
    if (!P) throw new Error('SLModel.loadClipPreset: no preset ' + id);
    return normalizeClip({ bars: P.bars, notes: deepClone(P.notes) });
  }

  // ======================================================================== exports
  return {
    VERSION, SNAPSHOT_VERSION, MAX_NODES, BEATS_PER_BAR, MIN_LEN, BAKE_EPS, PITCH_GEN_SCALE,
    NODE_SHAPES, ENGINES, ENGINE_NAMES, MOTIONS, START_MODES, TRAVEL_KINDS, HEAD_TUNES, AT_END, REFRESH_MODES,
    GLIDE_MODES, GEN_KINDS, GEN_SHAPES, MACRO_NAMES, NAMES, SCALES, SHAPE_KEYS, SHAPE_NAMES, DICE_FIELDS,
    LANES, LANE, LANE_IDS, SAMPLE_LANES, MODES, laneIndex, laneDef,
    RATES, rateBeats, isRate, rateLabel, rateFromBeats,
    defaultInst, mergeInst, normalizeInst,
    hash32, rand01, idHash,
    noteName, inScale, snapScale,
    travelSpeed, travelSpeedToV, F32_EPS, V_FWD_MIN, V_BACK_MAX, bendShape, gShape,
    mkNode, flat, normalizeNodes, sampleNodes, sampleLane: sampleNodes, evalNodes, sliceNodes, reverseNodes, rdp, patternNodes, shapeNodes, shapeKeysFor,
    mkNote, newClip, normalizeNote, normalizeClip, cloneNote, cloneClip, addNote, removeNotes, updateNotes, duplicateNotes,
    makeGen, normalizeGen, genOffset, laneTable, tableLength, setGen, clearGen, bakeGen, reroll,
    mapLane, formatLane, applyShape,
    connect, disconnect, walk, chop, reverseLane, flipLane, scaleLane, selectSimilar, humanize, strum, legato,
    setNoteSample, getMixed, setLaneNodes, setLaneNodesMany,
    computeGlides, buildChains, compileSnapshot, History,
    RECIPES, applyRecipe, dice, SOUND_PRESETS, applySoundPreset, CLIP_PRESETS, loadClipPreset,
  };
})();
if (typeof module !== 'undefined') module.exports = SLModel;
