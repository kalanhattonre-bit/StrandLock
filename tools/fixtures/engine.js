// tools/fixtures/engine.js: Tests/fixtures/engine/defs.json and engine/renders.json (PLUGIN-PLAN 5.3).
// Renders run the engine from its own toString() (as the worklet and the prototype tests do), with process() split at
// every host block boundary, every event and transport sample, and every observation point (as MidiRouter splits).
'use strict';
const C = require('./common.js');
const { starter } = require('./sounds.js');
const { makeSignal } = require('./support.js');

const COMPILE_KEYS = ['engine', 'lock', 'motion', 'tape', 'rev', 'jump', 'glideMode', 'glideTime'];   // sl-ui.js: a change recompiles
const STATS_EVERY = 1024, DISPLAY_EVERY = 4800, TRACE_EVERY = 32, WIN = 256, BLK = 256;

function defs() {
  const { SE } = C.load();
  const I = C.engineInternals();
  const budgets = {};
  for (const sr of [44100, 48000, 88200, 96000, 192000]) budgets[sr] = new SE.Engine({ sampleRate: sr }).budget;
  const cases = [{ kind: 'defs', CTRL: SE.CTRL, MAX_HEADS: SE.MAX_HEADS, VOICE_GRAINS: I.VOICE_GRAINS, CLOUD_GRAINS: I.CLOUD_GRAINS, LOCK_MAX_N: SE.LOCK_MAX_N,
    BUDGET_48K: SE.BUDGET_48K, budgets, HEAD_TUNE: SE.HEAD_TUNE, HANN_N: I.HANN_N, FADE_N: I.FADE_N, HANN: I.HANN, FADE: I.FADE,
    salts: { SALT_JUMP: I.SALT_JUMP, SALT_SKIP: I.SALT_SKIP, SALT_SPRAY: I.SALT_SPRAY, SALT_DRIFT: I.SALT_DRIFT, SALT_TINY: I.SALT_TINY, SALT_START: I.SALT_START },
    LANE_IDS: SE.LANE_IDS, LANE_DEF: SE.LANE_DEF, F32_EPS: SE.F32_EPS }];
  // the engine's own clamps (normInst: wider than the model's normalizeInst), for core::clampForEngine
  for (const input of [{}, null, { speed: -20, grainLock: 64.5, refresh: { ms: 20000, mode: 'fast' }, region: { start: 0.8, end: 0.2, xfadeMs: 2000, atEnd: 'bogus' }, vibRate: 60, outGain: 30 },
    { engine: 'grain', motion: 'spin', heads: 9, macros: [2, -1, NaN], offsets: { amp: 3 }, polyphony: 32.6, mod1: { target: 'nope' }, mod2: null, seed: 4294967297, headTune: 'thirds' },
    { speed: NaN, heads: Infinity, spread: -1, drift: 2, cents: -500, root: 300, bendRange: -3, velSens: 9, liveLegato: 1, tape: 'yes', rev: 0 }])
    cases.push({ kind: 'normInst', input, result: SE.normInst(input) });
  return { prototype: C.prototypeHashes(), tolerance: { text: 'constants exact; tables within 1 Float32 ulp', constants: 'exact', tableUlps: 1, normInst: 'exact' },
    notes: ['HANN, FADE, VOICE_GRAINS, CLOUD_GRAINS and the salts are private to the engine: they are read from an in-memory copy of its source with those names added to its return value',
      'budgets = new Engine({sampleRate}).budget; normInst cases give the engine\'s own clamps (core::clampForEngine), wider than the model\'s normalizeInst'],
    cases };
}

// ------------------------------------------------------------------------------------------------ the render harness
function blockSizes(block, n) {
  const sizes = [];
  if (typeof block === 'number') { for (let i = 0; i < n; i += block) sizes.push(Math.min(block, n - i)); return sizes; }
  if (Array.isArray(block)) { let k = 0; for (let i = 0; i < n;) { const m = Math.min(block[k++ % block.length], n - i); sizes.push(m); i += m; } return sizes; }
  const r = C.signals().lcg(block.random);
  for (let i = 0; i < n;) { const m = Math.min(block.min + Math.floor(r() * (block.max - block.min + 1)), n - i); sizes.push(m); i += m; }
  return sizes;
}
function soundOf(spec) {
  if (spec.starter) { const s = starter(spec.starter, spec.seed == null ? 1 : spec.seed); return { channels: s.channels, sampleRate: s.sampleRate, name: s.name }; }
  const s = makeSignal(spec); return { channels: s.channels, sampleRate: s.sampleRate, name: spec.signal };
}
function rawSnapOut(snap) {
  return { version: snap.version, bars: snap.bars, beatsPerBar: snap.beatsPerBar, res: snap.res, notes: snap.notes.map(n => {
    const lanes = {}; for (const id of Object.keys(n.lanes)) lanes[id] = C.table(n.lanes[id]);
    return Object.assign({}, n, { lanes });
  }) };
}
function render(spec, clips) {
  const { M, SE } = C.load();
  const sr = spec.sr || 48000, n = spec.n, res = spec.res || 256;
  const e = new SE.Engine({ sampleRate: sr });
  let inst = M.normalizeInst(M.mergeInst(M.defaultInst(), spec.inst || {}));
  const inst0 = inst;
  e.setInst(inst);
  e.setSample(soundOf(spec.sound));
  let clipNow = null;
  const compile = () => M.compileSnapshot(clipNow, inst, { res });
  if (spec.clip) { clipNow = C.clipRef(spec.clip, clips, spec.recipe); e.setSnapshot(compile()); }
  if (spec.snapshot) e.setSnapshot(spec.snapshot);
  const transport = spec.transport || [], events = spec.events || [];
  // split points: host blocks, transport and event samples, observation points
  const pts = new Set([0, n]);
  let acc = 0; for (const m of blockSizes(spec.block || 128, n)) { acc += m; pts.add(acc); }
  for (const t of transport) pts.add(t[0]);
  for (const ev of events) pts.add(ev[0]);
  for (let k = STATS_EVERY; k <= n; k += STATS_EVERY) pts.add(k);
  for (let k = DISPLAY_EVERY; k <= n; k += DISPLAY_EVERY) pts.add(k);
  if (spec.trace) for (let k = TRACE_EVERY; k <= n; k += TRACE_EVERY) pts.add(k);
  const order = [...pts].filter(p => p >= 0 && p <= n).sort((a, b) => a - b);
  const L = new Float32Array(n), R = new Float32Array(n);
  const stats = [], display = [], trace = [];
  let ti = 0, ei = 0;
  for (let q = 0; q < order.length; q++) {
    const p = order[q];
    if (p > 0) {   // observations see the state after rendering [0, p)
      if (p % STATS_EVERY === 0) { const s = e.stats(); stats.push([p, s.voices, s.grains, s.thinnedHeads, s.maxGrains, s.budget, s.cuts, s.steals]); }
      if (p % DISPLAY_EVERY === 0 && !spec.statsOnly) {
        const d = e.getDisplay();
        display.push([p, d.beat, d.playing, d.voices.map(v => [v.noteId, v.p, v.kind, v.engine, v.motion, v.env, v.released, v.heads.map(h => [h.i, h.pos, h.dir, h.level, h.thinned])]), d.grains.length]);
      }
      if (spec.trace && p % TRACE_EVERY === 0) {
        const d = e.getDisplay(), v = d.voices[0], h = v && v.heads.find(x => x.i === 0);
        trace.push(h ? h.pos : NaN);
      }
    }
    if (p === n) break;
    while (ti < transport.length && transport[ti][0] === p) e.setTransport(transport[ti++][1]);
    while (ei < events.length && events[ei][0] === p) {
      const ev = events[ei++];
      switch (ev[1]) {
        case 'noteOn': e.noteOn(ev[2], ev[3], ev[4]); break;
        case 'noteOff': e.noteOff(ev[2]); break;
        case 'ctrl': e.setController(ev[2], ev[3]); break;
        case 'allOff': e.allNotesOff(); break;
        case 'inst': {
          const prev = inst; inst = M.normalizeInst(M.mergeInst(inst, ev[2])); e.setInst(inst);
          if (clipNow && COMPILE_KEYS.some(k => JSON.stringify(prev[k]) !== JSON.stringify(inst[k]))) e.setSnapshot(compile());
          break;
        }
        case 'snapshot': clipNow = typeof ev[2] === 'string' ? C.clipRef(ev[2], clips) : M.normalizeClip(ev[2]); e.setSnapshot(compile()); break;
        default: throw new Error('make-fixtures: unknown event ' + ev[1]);
      }
    }
    const next = order[q + 1];
    e.process(L.subarray(p, next), R.subarray(p, next), next - p);
  }
  if (ti < transport.length || ei < events.length) throw new Error('make-fixtures: ' + spec.name + ' has events past its end');
  const out = { hash: C.fnvF32([L, R]) };
  if (!spec.statsOnly) {
    const nb = Math.ceil(n / BLK), rms = new Float32Array(nb * 2), pk = new Float32Array(nb * 2);
    for (let b = 0; b < nb; b++) {
      const a = b * BLK, z = Math.min(n, a + BLK);
      let sl = 0, sr2 = 0, pl = 0, pr = 0;
      for (let i = a; i < z; i++) { sl += L[i] * L[i]; sr2 += R[i] * R[i]; const al = Math.abs(L[i]), ar = Math.abs(R[i]); if (al > pl) pl = al; if (ar > pr) pr = ar; }
      rms[2 * b] = Math.sqrt(sl / (z - a)); rms[2 * b + 1] = Math.sqrt(sr2 / (z - a)); pk[2 * b] = pl; pk[2 * b + 1] = pr;
    }
    out.blockRms = rms; out.blockPeak = pk;
    out.windows = (spec.windows || []).map(at => { const a = Math.max(0, Math.min(n - WIN, at)); return { at: a, L: L.slice(a, a + WIN), R: R.slice(a, a + WIN) }; });
  }
  out.stats = stats;
  if (!spec.statsOnly) out.display = display;
  if (spec.trace) out.trace = { every: TRACE_EVERY, head0pos: Float32Array.from(trace) };
  // a no-op is not a pass: every case must make sound
  let energy = 0; for (let i = 0; i < n; i++) energy += L[i] * L[i] + R[i] * R[i];
  if (!(energy > 1e-6)) throw new Error('make-fixtures: render ' + spec.name + ' is silent');
  const o = { name: spec.name, needs: needsOf(spec, inst0, clips), sr, bpm: spec.bpm || 120, block: spec.block || 128, sound: spec.sound, inst: inst0 };
  if (spec.clip) { o.clip = spec.clip; if (spec.recipe != null) o.recipe = spec.recipe; o.res = res; }
  if (spec.snapshot) o.snapshot = rawSnapOut(spec.snapshot);
  o.transport = transport; o.events = events; o.n = n; o.out = out;
  return o;
}

// needs: 'tape' when every voice of the case plays the Tape engine (M2 runs these), else 'grains' (M3 and later)
function needsOf(spec, inst0, clips) {
  const { M } = C.load();
  const engines = new Set([inst0.engine]);
  const fromClip = cl => { for (const n of cl.notes) engines.add(n.sample && n.sample.engine ? n.sample.engine : inst0.engine); };
  if (spec.clip) fromClip(C.clipRef(spec.clip, clips, spec.recipe));
  if (spec.snapshot) for (const n of spec.snapshot.notes) engines.add(n.engine);
  for (const ev of spec.events || []) {
    if (ev[1] === 'inst') engines.add(M.normalizeInst(M.mergeInst(inst0, ev[2])).engine);
    if (ev[1] === 'snapshot') fromClip(typeof ev[2] === 'string' ? C.clipRef(ev[2], clips) : M.normalizeClip(ev[2]));
  }
  return engines.size === 1 && engines.has('tape') ? 'tape' : 'grains';
}

// ------------------------------------------------------------------------------------------------ the clips and cases
function renderClips() {
  const { M } = C.load();
  const N = (p, s, l, id, f) => { const n = M.mkNote(p, s, l, id); if (f) f(n); return n; };
  const fin = (bars, notes) => M.normalizeClip({ bars, notes });
  const nd = (list, lane) => M.normalizeNodes(list.map(a => ({ t: a[0], v: a[1], shape: a[2] })), M.laneDef(lane));
  return {
    'one-c3-4': fin(4, [N(60, 0, 4, 'n-c3')]),
    'one-c1-4': fin(4, [N(36, 0, 4, 'n-c1')]),
    'one-c5-4': fin(4, [N(84, 0, 4, 'n-c5')]),
    'two-notes': fin(4, [N(60, 0, 4, 'n-c3'), N(64, 0, 4, 'n-e3')]),
    'formant-up6': fin(4, [N(60, 0, 4, 'n-fu', n => { n.c.formant = nd([[0, 0.75], [1, 0.75]], 'formant'); })]),
    'formant-down6': fin(4, [N(60, 0, 4, 'n-fd', n => { n.c.formant = nd([[0, 0.25], [1, 0.25]], 'formant'); })]),
    'path-ramp': fin(4, [N(60, 0, 2, 'n-path', n => { n.sample.travel = 'path'; n.c.travel = nd([[0, 0.1, 'linear'], [1, 0.9]], 'travel'); })]),
    'chain-c3-g3': fin(4, [N(60, 0, 2, 'ch-1'), N(67, 2, 2, 'ch-2')]),
    'glide0-jump': fin(4, [N(60, 0, 1, 'gj-1', n => { n.glide = 0; n.glideTo = 'gj-2'; }), N(64, 1, 1, 'gj-2'), N(55, 2, 2, 'gj-3')]),
    'restart-chain': fin(4, [N(60, 0, 1, 'rs-1'), N(62, 1, 1, 'rs-2', n => { n.sample.restart = true; }), N(64, 2, 2, 'rs-3')]),
    'tape-to-cloud': fin(4, [N(60, 0, 1, 'tc-1', n => { n.sample.engine = 'tape'; n.glideTo = 'tc-2'; n.glide = 0.25; }), N(65, 1, 3, 'tc-2', n => { n.sample.engine = 'cloud'; })]),
    'four-quarters': fin(4, [N(60, 0, 1, 'q-1'), N(62, 1, 1, 'q-2'), N(64, 2, 1, 'q-3'), N(65, 3, 1, 'q-4'), N(67, 4, 2, 'q-5'), N(72, 6, 2, 'q-6')]),
    'sixteen-notes': fin(4, Array.from({ length: 16 }, (_, i) => N(36 + 2 * i, i * 0.05, 6, 'x' + i))),
    'one-bar-133': fin(1, [N(60, 0, 1, 'b-1'), N(67, 2, 1.5, 'b-2', n => { n.c.amp = nd([[0, 0.9], [1, 0.4]], 'amp'); })]),
  };
}
// a raw engine-only snapshot, built the way tests/test-engine.js's mkNote builds one (dense Float32 tables, resolved settings)
function rawSnapshot() {
  const { SE } = C.load();
  const mk = (o) => {
    const res = 256, len = Math.max(2, Math.ceil(o.l * res) + 1), lanes = {};
    SE.LANE_IDS.forEach((id, i) => { const t = new Float32Array(len), f = o.lanes && o.lanes[id]; for (let j = 0; j < len; j++) { const u = j / (len - 1); t[j] = f === undefined ? SE.LANE_DEF[i] : typeof f === 'function' ? f(u) : f; } lanes[id] = t; });
    return { id: o.id, idHash: SE.idHash(o.id), p: o.p, s: o.s, l: o.l, vel: o.vel == null ? 1 : o.vel, engine: o.engine, lock: !!o.lock, motion: o.motion || 'forward', travel: o.travel || 'speed',
      start: Object.assign({ mode: 'region', pos: 0, range: 0, seed: 0 }, o.start || {}), tape: !!o.tape, rev: !!o.rev, jump: o.jump || 0.5, restart: !!o.restart,
      glide: o.glide || null, chain: o.chain || { id: o.id, index: 0, prevId: null, nextId: null }, lanes };
  };
  const a = mk({ id: 'raw-a', p: 60, s: 0, l: 2, engine: 'tape', lanes: { travel: u => 0.75 + 0.2 * Math.sin(2 * Math.PI * u), amp: u => 0.5 + 0.4 * u }, glide: { d: 5, len: 0.5, curve: 0.3, toId: 'raw-b' },
    chain: { id: 'raw-a', index: 0, prevId: null, nextId: 'raw-b' } });
  const b = mk({ id: 'raw-b', p: 65, s: 2, l: 2, engine: 'voice', start: { mode: 'random', pos: 0.2, range: 0.5, seed: 3 }, lanes: { vib: 0.4, heads: 0.643 },
    chain: { id: 'raw-a', index: 1, prevId: 'raw-a', nextId: null } });
  const c = mk({ id: 'raw-c', p: 48, s: 1, l: 3, engine: 'cloud', lock: true, motion: 'scatter', jump: 0.25, lanes: { refresh: 0.7, grain: 0.3 } });
  return { version: 1, bars: 1, beatsPerBar: 4, res: 256, notes: [a, c, b] };
}

function renders() {
  const { M } = C.load();
  const clips = renderClips();
  const INIT = M.applySoundPreset(M.defaultInst(), 'init').inst;
  const demo = M.loadClipPreset('strand-demo');
  const recipe = {};
  for (const k of [3, 6, 8]) recipe[k] = M.applyRecipe(demo, INIT, k, null).inst;
  const SAW = { starter: 'saw', seed: 1 }, OPEN = { starter: 'sawopen', seed: 1 };
  const play = (bpm, beat) => [[0, { playing: true, bpm: bpm || 120, beat: beat || 0, loop: true }]];
  const S1 = 48000, S2 = 96000;
  const specs = [];
  const add = s => specs.push(Object.assign({ n: S1, block: 128, sound: SAW }, s));
  const tape = p => Object.assign({ engine: 'tape' }, p || {});
  // Tape
  add({ name: 'tape-forward-c3', inst: tape(), clip: 'one-c3-4', transport: play(), windows: [0, 12000, 47744] });
  add({ name: 'tape-forward-c5', inst: tape(), clip: 'one-c5-4', transport: play(), windows: [0, 24000] });
  add({ name: 'tape-backward', inst: tape({ motion: 'backward' }), clip: 'one-c3-4', transport: play(), windows: [0, 24000] });
  add({ name: 'tape-bounce-region', n: S2, inst: tape({ motion: 'bounce', region: { start: 0.2, end: 0.3 } }), clip: 'one-c3-4', transport: play(), windows: [0, 28672, 57472] });
  add({ name: 'tape-mirror-2heads', inst: tape({ motion: 'mirror', heads: 2 }), clip: 'one-c3-4', transport: play(), windows: [0, 24000] });
  for (const seed of [1, 2]) add({ name: 'tape-scatter-1/8T-87bpm-seed' + seed, bpm: 87, inst: tape({ motion: 'scatter', jump: '1/8T', seed }), clip: 'one-c3-4', transport: play(87), trace: true, windows: [10906, 21941] });
  add({ name: 'tape-refresh-sync-1/16', inst: tape({ refresh: { mode: 'sync', rate: '1/16' } }), clip: 'one-c3-4', transport: play(), windows: [5872, 11872] });
  add({ name: 'tape-region-post-seam', inst: tape({ region: { start: 0.3, end: 0.35, xfadeMs: 10 } }), clip: 'one-c3-4', transport: play(), windows: [14264, 28656] });
  add({ name: 'tape-region-pre-seam', inst: tape({ region: { start: 0.95, end: 1, xfadeMs: 10 } }), clip: 'one-c3-4', transport: play(), windows: [13800, 14265] });
  add({ name: 'tape-region-hold', inst: tape({ region: { start: 0.5, end: 0.55, atEnd: 'hold' } }), clip: 'one-c3-4', transport: play(), windows: [14265, 30000] });
  add({ name: 'tape-region-stop', inst: tape({ region: { start: 0.5, end: 0.55, atEnd: 'stop' } }), clip: 'one-c3-4', transport: play(), windows: [14265, 15000] });
  // Path kind (ramp source: L encodes the read position) and Tape's Path-as-1x
  const RAMP = { signal: 'rampSource', args: [2, 48000] };
  add({ name: 'cloud-path-ramp', sound: RAMP, inst: { engine: 'cloud' }, clip: 'path-ramp', transport: play(), windows: [12000, 36000] });
  add({ name: 'voice-path-ramp', sound: RAMP, inst: { engine: 'voice' }, clip: 'path-ramp', transport: play(), windows: [12000] });
  add({ name: 'tape-path-is-1x', sound: RAMP, inst: tape(), clip: 'path-ramp', transport: play(), windows: [12000, 36000] });
  // chains, glides, restart
  add({ name: 'chain-c3-g3-legato', n: S2, inst: {}, clip: 'chain-c3-g3', transport: play(), windows: [36000, 47872, 60000] });
  add({ name: 'tape-chain-c3-g3-legato', n: S2, inst: tape(), clip: 'chain-c3-g3', transport: play(), windows: [47872, 60000] });
  add({ name: 'glide0-jump', inst: tape(), clip: 'glide0-jump', transport: play(), windows: [23872] });
  add({ name: 'restart-breaks-chain', inst: tape(), clip: 'restart-chain', transport: play(), windows: [23872] });
  add({ name: 'tape-to-cloud-chain', inst: {}, clip: 'tape-to-cloud', transport: play(), windows: [17872, 23872] });
  // chase, tempo, jumps, loops
  add({ name: 'chase-at-1.37', inst: {}, clip: 'one-c3-4', transport: play(120, 1.37), windows: [0, 600] });
  add({ name: 'tape-chase-at-1.37', inst: tape(), clip: 'one-c3-4', transport: play(120, 1.37), windows: [0, 600, 24000] });
  add({ name: 'chase-inside-chain', inst: tape(), clip: 'chain-c3-g3', transport: play(120, 3), windows: [0, 600] });
  add({ name: 'tempo-120-to-87', n: 72000, inst: tape(), clip: 'four-quarters', transport: [[0, { playing: true, bpm: 120, beat: 0, loop: true }], [24000, { playing: true, bpm: 87 }]], windows: [23872, 56975] });
  add({ name: 'transport-jump', inst: tape(), clip: 'four-quarters', transport: [[0, { playing: true, bpm: 120, beat: 0, loop: true }], [30000, { playing: true, bpm: 120, beat: 2.5 }]], windows: [29872, 30200, 35872] });
  add({ name: 'transport-stop-start', inst: tape(), clip: 'four-quarters', transport: [[0, { playing: true, bpm: 120, beat: 0, loop: true }], [20000, { playing: false, bpm: 120 }], [33024, { playing: true, bpm: 120, beat: 1.5 }]], windows: [19872, 33024] });
  add({ name: 'loop-3-passes-133bpm', n: 259850, bpm: 133, inst: tape(), clip: 'one-bar-133', transport: play(133), statsOnly: true });
  // live keys
  add({ name: 'live-legato', n: 60000, inst: tape({ liveLegato: true }), events: [[0, 'noteOn', 60, 0.8], [12000, 'noteOn', 64, 0.8], [24000, 'noteOff', 64], [36000, 'noteOff', 60]], windows: [11872, 23872, 36000] });
  add({ name: 'live-sustain', inst: tape(), events: [[0, 'ctrl', 'sustain', 1], [0, 'noteOn', 60, 1], [6000, 'noteOff', 60], [24000, 'ctrl', 'sustain', 0]], windows: [5872, 23872, 26000] });
  add({ name: 'live-mod1-travel', inst: { mod1: { target: 'travel', cc: 1 } }, events: [[0, 'noteOn', 60, 1], [12000, 'ctrl', 'mod1', 0.5], [24000, 'ctrl', 'mod1', 0.25], [36000, 'ctrl', 'mod1', null]], windows: [12000, 24000, 36000] });
  add({ name: 'live-mod1-heads', inst: { mod1: { target: 'heads', cc: 1 } }, events: [[0, 'noteOn', 60, 1], [12000, 'ctrl', 'mod1', 0.9], [30000, 'ctrl', 'mod1', 0.6]], windows: [12000, 30000] });
  add({ name: 'tape-live-mod1-travel', inst: tape({ mod1: { target: 'travel', cc: 1 } }), events: [[0, 'noteOn', 60, 1], [12000, 'ctrl', 'mod1', 0.5], [24000, 'ctrl', 'mod1', 0.25], [36000, 'ctrl', 'mod1', null]], windows: [24000, 36000] });
  add({ name: 'tape-live-mod1-heads', inst: tape({ mod1: { target: 'heads', cc: 1 } }), events: [[0, 'noteOn', 60, 1], [12000, 'ctrl', 'mod1', 0.9], [30000, 'ctrl', 'mod1', 0.6]], windows: [12000, 30000] });
  // live Scatter while the host plays: the jump grid is the song's loop beat, not the free clock (plan 1.7)
  add({ name: 'tape-live-scatter-playing', inst: tape({ motion: 'scatter', jump: '1/8' }), transport: play(120, 0.3), events: [[1000, 'noteOn', 60, 1], [40000, 'noteOff', 60]], trace: true, windows: [4800, 16800] });
  add({ name: 'live-bend-range-7', inst: tape({ bendRange: 7 }), events: [[0, 'noteOn', 60, 1], [12000, 'ctrl', 'bend', 1], [30000, 'ctrl', 'bend', -0.5]], windows: [12000, 30000] });
  add({ name: 'live-poly4-6-keys', inst: tape({ polyphony: 4 }), events: [[0, 'noteOn', 60, 0.9], [2000, 'noteOn', 64, 0.7], [4000, 'noteOn', 67, 1], [6000, 'noteOn', 71, 0.5], [8000, 'noteOn', 74, 0.8], [10000, 'noteOn', 77, 127],
    [20000, 'noteOff', 74], [24000, 'noteOn', 60, 0], [30000, 'allOff']], windows: [7872, 9872, 30000] });
  // Voice
  for (const [nm, cl] of [['c1', 'one-c1-4'], ['c3', 'one-c3-4'], ['c5', 'one-c5-4']]) add({ name: 'voice-' + nm, inst: {}, clip: cl, transport: play(), windows: [0, 24000] });
  add({ name: 'voice-formant-up6', inst: {}, clip: 'formant-up6', transport: play(), windows: [24000] });
  add({ name: 'voice-formant-down6', inst: {}, clip: 'formant-down6', transport: play(), windows: [24000] });
  // Cloud
  const cloud = p => Object.assign({ engine: 'cloud' }, p || {});
  add({ name: 'cloud-default', inst: cloud(), clip: 'one-c3-4', transport: play(), windows: [0, 24000] });
  add({ name: 'cloud-spray-0.3', inst: cloud({ spray: 0.3 }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'cloud-skip-0.25', inst: cloud({ refresh: { mode: 'free', ms: 120, skip: 0.25 } }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'cloud-refresh-free-60ms', inst: cloud({ refresh: { mode: 'free', ms: 60 } }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'cloud-each-grain-backwards', inst: cloud({ rev: true }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'cloud-tape-switch-2x', inst: cloud({ tape: true, speed: 2 }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  // Lock
  add({ name: 'lock-from-noise', sound: { signal: 'noise', args: [2, 48000, 3] }, inst: cloud({ lock: true }), clip: 'one-c3-4', transport: play(), windows: [12000, 36000] });
  add({ name: 'lock-from-chord', sound: { signal: 'chord', args: [[48, 55, 60, 64], 2, 48000] }, inst: cloud({ lock: true }), clip: 'one-c3-4', transport: play(), windows: [12000, 36000] });
  // heads
  add({ name: 'heads-2.5', inst: { heads: 2.5 }, clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'heads-8-spread-0.6', inst: { heads: 8, spread: 0.6 }, clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'drift-0.6', inst: { heads: 3, drift: 0.6 }, clip: 'one-c3-4', transport: play(), windows: [24000] });
  for (const ht of ['octaves', 'fifths', 'detune']) add({ name: 'head-tune-' + ht, inst: { heads: 4, headTune: ht }, clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'tape-heads-2.5', inst: tape({ heads: 2.5 }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'tape-heads-8-spread-0.6', inst: tape({ heads: 8, spread: 0.6 }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'tape-drift-0.6', inst: tape({ heads: 3, drift: 0.6 }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  add({ name: 'tape-head-tune-octaves', inst: tape({ heads: 4, headTune: 'octaves' }), clip: 'one-c3-4', transport: play(), windows: [24000] });
  // the budget: 16 voices x 8 heads, Lock N = 12 (M3 takes these stats as the expected result: the prototype cuts fade
  // tails here, and at 96 kHz the newest note sounds 4 heads); live keys 256 samples apart
  const keys16 = Array.from({ length: 16 }, (_, k) => [k * 256, 'noteOn', 36 + 2 * k, 1]);
  add({ name: 'budget-16x8-lock12-48k', inst: cloud({ lock: true, heads: 8, grainLock: 12, polyphony: 16 }), events: keys16, statsOnly: true });
  add({ name: 'budget-16x8-lock12-96k', sr: 96000, n: 96000, inst: cloud({ lock: true, heads: 8, grainLock: 12, polyphony: 16 }), events: keys16, statsOnly: true });
  // the same as clip notes 0.05 beat apart (the prototype's own budget tests space their 16 notes like this)
  for (const sr of [44100, 48000, 96000]) add({ name: 'budget-16x8-lock12-clip-' + sr / 1000 + 'k', sr, n: sr, res: 64, inst: cloud({ lock: true, heads: 8, grainLock: 12, polyphony: 16 }), clip: 'sixteen-notes', transport: play(), statsOnly: true });
  // a 44.1 kHz source in a 48 kHz session
  add({ name: 'source-44k-at-48k', sound: { signal: 'tone', args: [261.6255653005986, 2, 44100] }, inst: tape(), clip: 'one-c3-4', transport: play(), windows: [24000] });
  // the default sound and recipes
  add({ name: 'strand-demo-2s', n: S2, sound: OPEN, inst: INIT, clip: 'preset:strand-demo', transport: play(), windows: [0, 12000, 47872, 95744] });
  for (const k of [3, 6, 8]) add({ name: 'strand-demo-recipe-' + k, sound: OPEN, inst: recipe[k], clip: 'preset:strand-demo', recipe: k, transport: play(), windows: [12000, 36000] });
  add({ name: 'strand-demo-random-blocks', sound: OPEN, inst: INIT, clip: 'preset:strand-demo', transport: play(), block: { random: 7, min: 1, max: 700 }, windows: [12000] });
  // edits while playing, an inst change, an engine-only snapshot
  add({ name: 'edit-delete-note', inst: tape(), clip: 'two-notes', transport: play(), events: [[24000, 'snapshot', 'one-c3-4']], windows: [23872, 26000] });
  add({ name: 'inst-change-macros', inst: tape(), clip: 'one-c3-4', transport: play(), events: [[24000, 'inst', { macros: [0.8, 0.6, 0.7, 0.5, 0.3, 0.5, 0.3, 0.6] }]], windows: [23872] });
  add({ name: 'raw-snapshot', inst: { glideMode: 'off' }, snapshot: rawSnapshot(), transport: play(), events: [[40000, 'allOff']], windows: [12000, 23872, 40000] });
  const cases = specs.map(s => render(s, clips));
  const out = {};
  for (const k of Object.keys(clips)) out[k] = C.clip(clips[k]);
  return { prototype: C.prototypeHashes(['tests/test-engine.js', 'tests/test-engine-extra.js']),
    tolerance: { text: 'if hash matches: bit-exact (printed). Otherwise windows within 1e-5 absolute (-100 dBFS); blockRms within 1e-6 + 1e-4 x rms; blockPeak within 1e-5; stats integers exact; display ids, kinds, engines, motions, released, dir, thinned exact, pos, level, env, beat within 1e-6; trace within 1e-6',
      hash: 'bit-exact if equal (printed)', windowsAbs: 1e-5, blockRms: { abs: 1e-6, rel: 1e-4 }, blockPeakAbs: 1e-5, stats: 'exact', display: { exact: ['noteId', 'p', 'kind', 'engine', 'motion', 'released', 'i', 'dir', 'thinned', 'grainCount', 'playing'], abs: 1e-6 }, traceAbs: 1e-6 },
    notes: ['render: engine = new Engine({sampleRate: sr}); setInst(inst); setSample(sound); setSnapshot(compileSnapshot(clip, inst, {res})) or the raw snapshot; then process() is split at every host block boundary, every transport and event sample, every 1024th sample (stats), every 4800th (display) and, with trace, every 32nd',
      'at a split point p: first the observations of the state after rendering [0, p) (stats, display, trace; none at p = 0), then the transport entries for p in order, then the events for p in order, then process up to the next point',
      'block: an int (fixed host blocks), an int list (cycled), or {random: seed, min, max}: sizes min + floor(lcg(seed)() * (max - min + 1)) with the tests\' lcg (support/signals.json)',
      'events: noteOn p vel [opts] -> noteOn(p, vel, opts) (vel > 1 is read as 0..127); ctrl name value|null -> setController; inst patch -> setInst(normalizeInst(mergeInst(current, patch))), and when a COMPILE_KEYS value (engine lock motion tape rev jump glideMode glideTime) changed and a clip is loaded, the clip is recompiled with the new inst, as sl-ui.js does; snapshot name|Clip -> setSnapshot(compileSnapshot(that clip, current inst, {res}))',
      'sound: {starter, seed} = SLSounds.make (sounds/starter.json); {signal, args} = the test generator (support/signals.json), mono = one channel',
      'out.hash = FNV-1a 32 over the Float32 bytes of L then R; blockRms/blockPeak per 256 samples, L R interleaved; windows = 256 frames at each at (clamped into the render)',
      'stats rows [sample, voices, grains, thinnedHeads, maxGrains, budget, cuts, steals] = engine.stats(); display rows [sample, beat, playing, voices, grainCount] from getDisplay(), voices [noteId, p, kind, engine, motion, env, released, heads], heads [i, pos, dir, level, thinned]',
      'trace.head0pos = the display pos of head 0 of the first display voice (NaN when there is none), every 32 samples from 32',
      'a case with recipe: n compiles applyRecipe(clip, inst, n, every note).clip; res is the compileSnapshot resolution (default 256)',
      'the budget cases record what the prototype does with 16 voices x 8 heads and Lock N = 12: it cuts fade tails (stats cuts > 0) and at 96 kHz the newest note sounds 4 heads; PLUGIN-PLAN M3 takes these stats as the expected result. Spacing: live keys 256 samples apart; clip notes 0.05 beat apart at 120 bpm (the layout of the prototype budget tests)',
      'needs: "tape" = every voice plays the Tape engine (M2 runs these cases), "grains" = a Voice, Cloud or Lock voice plays (M3 and later)',
      'transport starts in these cases fall on multiples of 32 samples, where the plugin control grid (anchored to the transport start, plan 1.8) and the prototype global one agree',
      'statsOnly cases (loops, budget) carry only hash and stats; the raw-snapshot case is an engine-only BLUEPRINT 3.2 snapshot (settings already resolved, lanes as Tables), the C++ converts it to its NoteSnap form',
      'every parity render keeps the prototype\'s conventions: beatsPerBar 4, offsetBeats 0, loop on, tieAcrossLoop off, ramp smoothing 0 ms (Engine::setRampMs(0))'],
    clips: out, cases };
}

module.exports = function engine() {
  return { 'engine/defs.json': defs(), 'engine/renders.json': renders() };
};
module.exports.render = render;
module.exports.renderClips = renderClips;
