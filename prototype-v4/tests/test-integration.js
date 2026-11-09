// tests/test-integration.js: the three modules working together (owner: integrator).
// SLModel builds clips -> SLModel.compileSnapshot -> the Engine (loaded from its own toString(), as the worklet
// loads it) renders them with SLSounds starter sounds; SLAnalysis measures pitch on the output.
// One line per check: PASS/FAIL name: measured vs limit. "  .. " lines are measurements. Exits 1 on any FAIL.
// Run: node tests/test-integration.js [section words]. Cubase naming: MIDI 60 = C3.
'use strict';
const path = require('path');
const SLModel = require(path.join(__dirname, '..', 'src', 'sl-model.js'));
const SLSounds = require(path.join(__dirname, '..', 'src', 'sl-sounds.js'));
const SLAnalysis = require(path.join(__dirname, '..', 'src', 'sl-analysis.js'));
const { createStrandEngineModule, strandWorkletGlue } = require(path.join(__dirname, '..', 'src', 'sl-engine.js'));
// exactly the text the page ships into the AudioWorklet: if the module used an outside name, this would throw
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
  try { fn(); } catch (e) { check(name + ' (ran without throwing)', false, String(e && e.stack || e).split('\n').slice(0, 5).join(' | '), 'no exception'); }
  info('section time ' + ((Date.now() - t0) / 1000).toFixed(2) + ' s');
}
const f2 = (x, d = 2) => (x === undefined || x === null || Number.isNaN(x) ? String(x) : Number(x).toFixed(d));
function dist(arr, d = 3) {
  const a = Array.from(arr).filter(Number.isFinite).sort((x, y) => x - y);
  if (!a.length) return '(none)';
  const q = p => a[Math.min(a.length - 1, Math.floor(p * (a.length - 1)))];
  return `n=${a.length} min ${f2(a[0], d)} p10 ${f2(q(0.1), d)} med ${f2(q(0.5), d)} p90 ${f2(q(0.9), d)} max ${f2(a[a.length - 1], d)}`;
}

// ------------------------------------------------------------------ material and rendering
const SR = 48000, BPM = 120;
const SOUND = {};
const sound = id => SOUND[id] || (SOUND[id] = SLSounds.make(id));
const beatsToSamples = (b, bpm = BPM) => Math.round(b * 60 / bpm * SR);
function lcg(seed) { let s = seed >>> 0 || 1; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }

// Render a compiled snapshot. hook(i, e) runs after every block (i = samples rendered so far).
function renderSnap(snd, snap, inst, secs, opts = {}) {
  const e = new SE.Engine({ sampleRate: SR });
  e.setInst(inst); e.setSample(snd); e.setSnapshot(snap);
  e.setTransport({ playing: true, bpm: opts.bpm || BPM, beat: opts.beat || 0, loop: opts.loop !== false });
  const n = Math.round(secs * SR), L = new Float32Array(n), R = new Float32Array(n);
  const bl = new Float32Array(1024), br = new Float32Array(1024);
  const blk = opts.block || 128, rnd = typeof blk === 'function';
  let i = 0;
  while (i < n) {
    const m = Math.min(rnd ? blk() : blk, n - i);
    e.process(bl, br, m);
    L.set(bl.subarray(0, m), i); R.set(br.subarray(0, m), i);
    i += m;
    if (opts.hook) opts.hook(i, e);
  }
  return { L, R, e };
}
function badCount(o) { let b = 0; for (let i = 0; i < o.L.length; i++) if (!Number.isFinite(o.L[i]) || !Number.isFinite(o.R[i])) b++; return b; }
function peak(o) { let p = 0; for (let i = 0; i < o.L.length; i++) p = Math.max(p, Math.abs(o.L[i]), Math.abs(o.R[i])); return p; }
function rms(x, a = 0, b = x.length) { a = Math.max(0, Math.floor(a)); b = Math.min(x.length, Math.floor(b)); let s = 0; for (let i = a; i < b; i++) s += x[i] * x[i]; return b > a ? Math.sqrt(s / (b - a)) : 0; }
function maxDiff(a, b) { let m = 0; for (let i = 0; i < Math.min(a.length, b.length); i++) m = Math.max(m, Math.abs(a[i] - b[i])); return m; }
const tabAt = (tab, u) => { const fi = Math.max(0, Math.min(1, u)) * (tab.length - 1), i = Math.floor(fi), j = Math.min(tab.length - 1, i + 1); return tab[i] + (fi - i) * (tab[j] - tab[i]); };
// pitch of out[a..b) in MIDI (SLAnalysis.detectPitch on the rendered output)
function pitchOf(o, a, b) {
  const seg = [o.L.slice(Math.floor(a), Math.floor(b))];
  const r = SLAnalysis.detectPitch(seg, SR);
  return r ? r.midi + r.cents / 100 : NaN;
}

// Windows (1/8 beat) where the snapshot says something must sound: a note covers the window (not its first
// 10 ms), its Volume and Tone lanes are open enough, and a Tape note is not stopped. Every such window must
// carry sound. Returns { expected, silent, list }.
function silentWhereExpected(o, snap, inst, bpm = BPM) {
  const I = SLModel.normalizeInst(inst);
  const W = 0.125, Lb = snap.bars * 4, n = Math.floor(o.L.length / beatsToSamples(W, bpm));
  let expected = 0, silent = 0; const list = [];
  for (let w = 0; w < n; w++) {
    const b0 = w * W, b1 = b0 + W, lb0 = b0 % Lb, lb1 = lb0 + W;
    if (lb1 > Lb + 1e-9) continue;
    const want = snap.notes.some(N => {
      const end = Math.min(N.s + N.l, Lb);
      if (!(N.s <= lb0 - 0.02 && end >= lb1)) return false;   // not the note's first 10 ms
      const u = ((lb0 + lb1) / 2 - N.s) / N.l;
      const amp = Math.min(1, Math.max(0, tabAt(N.lanes.amp, u) + I.offsets.amp)), tone = tabAt(N.lanes.tone, u) + I.offsets.tone;
      if (amp < 0.25 || tone < 0.35 || N.vel < 0.1) return false;
      if (N.engine === 'tape' && N.travel === 'speed' && SE.travelSpeed(tabAt(N.lanes.travel, u)) === 0) return false;
      return true;
    });
    if (!want) continue;
    expected++;
    const r = rms(o.L, beatsToSamples(b0, bpm), beatsToSamples(b1, bpm)) + rms(o.R, beatsToSamples(b0, bpm), beatsToSamples(b1, bpm));
    if (r < 1e-4) { silent++; if (list.length < 6) list.push(f2(b0, 2)); }
  }
  return { expected, silent, list };
}
function audioChecks(label, o, snap, inst) {
  const bad = badCount(o), pk = peak(o), s = silentWhereExpected(o, snap, inst);
  const ok = bad === 0 && pk <= 0.98 && s.silent === 0 && s.expected > 0;
  check(`${label}: finite, peak <= 0.98, sound in every window the clip expects`, ok,
    `${bad} bad samples, peak ${f2(pk, 3)}, rms ${f2(rms(o.L), 4)}, silent ${s.silent} of ${s.expected} expected windows${s.list.length ? ' (at beats ' + s.list.join(' ') + ')' : ''}`,
    '0, <= 0.98, 0 of > 0');
  return { pk, r: rms(o.L) };
}

// ------------------------------------------------------------------ BLUEPRINT 3.2 snapshot format
const LANE_IDS = ['pitch', 'amp', 'vib', 'tone', 'formant', 'hollow', 'travel', 'heads', 'grain', 'refresh'];
const NOTE_KEYS = ['id', 'idHash', 'p', 's', 'l', 'vel', 'engine', 'lock', 'motion', 'travel', 'start', 'tape', 'rev', 'jump', 'restart', 'glide', 'chain', 'lanes'];
const sameKeys = (o, keys) => o && typeof o === 'object' && Object.keys(o).length === keys.length && keys.every(k => Object.prototype.hasOwnProperty.call(o, k));
const isNum = x => typeof x === 'number' && Number.isFinite(x);
const isBool = x => x === true || x === false;
// Every field name, type, range and table length of BLUEPRINT 3.2. Returns a list of problems ([] = matches).
function snapshotProblems(snap, clip, res) {
  const P = [];
  const add = s => { if (P.length < 12) P.push(s); };
  if (!sameKeys(snap, ['version', 'bars', 'beatsPerBar', 'res', 'notes'])) add('top-level keys ' + Object.keys(snap).join(','));
  if (snap.version !== SLModel.SNAPSHOT_VERSION || !Number.isInteger(snap.version)) add('version ' + snap.version);
  if (!Number.isInteger(snap.bars) || snap.bars < 1 || snap.bars !== clip.bars) add('bars ' + snap.bars);
  if (snap.beatsPerBar !== 4) add('beatsPerBar ' + snap.beatsPerBar);
  if (snap.res !== res) add('res ' + snap.res);
  if (!Array.isArray(snap.notes) || snap.notes.length !== clip.notes.length) { add('notes length'); return P; }
  const byId = new Map(snap.notes.map(n => [n.id, n]));
  if (byId.size !== snap.notes.length) add('duplicate ids');
  for (let i = 1; i < snap.notes.length; i++) if (snap.notes[i].s < snap.notes[i - 1].s) add('notes not in start order');
  for (const n of snap.notes) {
    const w = s => add(n.id + ': ' + s);
    if (!sameKeys(n, NOTE_KEYS)) { w('keys ' + Object.keys(n).join(',')); continue; }
    if (typeof n.id !== 'string' || !n.id) w('id');
    if (n.idHash !== SE.idHash(n.id) || n.idHash !== SLModel.idHash(n.id) || !Number.isInteger(n.idHash) || n.idHash < 0 || n.idHash > 0xffffffff) w('idHash');
    if (!isNum(n.p) || n.p < 0 || n.p > 127 || !isNum(n.s) || n.s < 0 || !isNum(n.l) || n.l <= 0 || !isNum(n.vel) || n.vel < 0 || n.vel > 1) w('p/s/l/vel');
    if (!['tape', 'voice', 'cloud'].includes(n.engine)) w('engine ' + n.engine);
    if (!isBool(n.lock) || (n.lock && n.engine !== 'cloud')) w('lock ' + n.lock);
    if (!SLModel.MOTIONS.includes(n.motion)) w('motion ' + n.motion);
    if (!['speed', 'path'].includes(n.travel)) w('travel ' + n.travel);
    const st = n.start;
    if (!sameKeys(st, ['mode', 'pos', 'range', 'seed']) || !SLModel.START_MODES.includes(st.mode) || !isNum(st.pos) || st.pos < 0 || st.pos > 1 || !isNum(st.range) || st.range < 0 || st.range > 1 || !Number.isInteger(st.seed)) w('start ' + JSON.stringify(st));
    if (!isBool(n.tape) || !isBool(n.rev) || !isBool(n.restart)) w('tape/rev/restart');
    if (!isNum(n.jump) || n.jump <= 0 || !SLModel.RATES.some(r => Math.abs(SLModel.rateBeats(r) - n.jump) < 1e-12)) w('jump ' + n.jump);
    if (n.glide !== null) {
      const g = n.glide;
      if (!sameKeys(g, ['d', 'len', 'curve', 'toId']) || !isNum(g.d) || Math.abs(g.d) > 48 || !isNum(g.len) || g.len < 0 || g.len > n.l || !isNum(g.curve) || Math.abs(g.curve) > 1 || !byId.has(g.toId)) w('glide ' + JSON.stringify(g));
    }
    const c = n.chain;
    if (!sameKeys(c, ['id', 'index', 'prevId', 'nextId']) || typeof c.id !== 'string' || !Number.isInteger(c.index) || c.index < 0) { w('chain ' + JSON.stringify(c)); }
    else {
      if (c.index === 0 ? (c.prevId !== null || c.id !== n.id) : typeof c.prevId !== 'string') w('chain head ' + JSON.stringify(c));
      if (c.prevId !== null) { const pv = byId.get(c.prevId); if (!pv || pv.chain.nextId !== n.id || pv.chain.id !== c.id || pv.chain.index !== c.index - 1 || n.restart) w('chain prev'); }
      if (c.nextId !== null) {
        const nx = byId.get(c.nextId);
        if (!nx || nx.chain.prevId !== n.id || !(nx.s > n.s) || nx.restart) w('chain next');
        if (!n.glide || n.glide.toId !== c.nextId) w('chain join without its glide');
      }
    }
    if (!sameKeys(n.lanes, LANE_IDS) || Object.keys(n.lanes).join() !== LANE_IDS.join()) { w('lanes keys ' + Object.keys(n.lanes || {}).join(',')); continue; }
    const len = Math.max(2, Math.ceil(n.l * res) + 1);
    for (const id of LANE_IDS) {
      const t = n.lanes[id];
      if (!(t instanceof Float32Array) || t.length !== len) { w(`${id} table ${t && t.constructor && t.constructor.name} length ${t && t.length} (want ${len})`); continue; }
      for (let j = 0; j < t.length; j++) if (!(t[j] >= 0 && t[j] <= 1)) { w(`${id}[${j}] = ${t[j]}`); break; }
    }
  }
  return P;
}
function formatCheck(label, clip, inst, res = 256) {
  const snap = SLModel.compileSnapshot(clip, inst, { res });
  const pr = snapshotProblems(snap, clip, res);
  check(`${label}: snapshot matches BLUEPRINT 3.2 (fields, types, ranges, table lengths, chains)`, pr.length === 0, pr.length ? pr.join('; ') : `${snap.notes.length} notes, ${snap.notes.filter(n => n.chain.nextId).length} chain joins`, 'no problems');
  return snap;
}

// ================================================================== A. the shared definitions agree
section('A. model and engine share their definitions (BLUEPRINT 1)', () => {
  const dm = SLModel.defaultInst(), de = SE.defaultInst();
  check('defaultInst() is identical in model and engine', JSON.stringify(dm) === JSON.stringify(de), JSON.stringify(dm) === JSON.stringify(de) ? 'identical' : 'DIFFERENT', 'identical');
  const lm = SLModel.LANES.map(L => L.id + '=' + L.def).join(' '), le = SE.LANE_IDS.map((id, i) => id + '=' + SE.LANE_DEF[i]).join(' ');
  check('lane ids and defaults, registry order', lm === le, lm, le);
  const rb = SLModel.RATES.filter(r => SLModel.rateBeats(r) !== SE.rateBeats(r));
  check('rateBeats agrees on all 21 rates (and unknown ids read 1/16 in both)', rb.length === 0 && SLModel.rateBeats('x') === SE.rateBeats('x'), rb.length + ' differ', '0');
  let tsd = 0; for (let i = 0; i <= 20000; i++) { const v = Math.fround(i / 20000); if (SLModel.travelSpeed(v) !== SE.travelSpeed(v)) tsd++; }
  for (const v of [0.48, 0.52, 0.02, 0.98]) if (SLModel.travelSpeed(Math.fround(v)) !== SE.travelSpeed(Math.fround(v))) tsd++;
  check('travelSpeed agrees on 20001 Float32 values and the band edges', tsd === 0, tsd + ' differ', '0');
  let hd = 0; const r = lcg(9); for (let i = 0; i < 2000; i++) { const a = (r() * 2 ** 32) | 0, b = (r() * 2 ** 32) | 0, c = (r() * 1e6) | 0; if (SLModel.hash32(a, b, c) !== SE.hash32(a, b, c) || SLModel.idHash('n' + a) !== SE.idHash('n' + a)) hd++; }
  check('hash32 and idHash agree (2000 random cases)', hd === 0, hd + ' differ', '0');
  // every instrument the model can produce is taken as is by the engine (no field clamped differently)
  const ext = [{}, { polyphony: 64, speed: 16, heads: 8, grainLock: 16, outGain: 12, vibRate: 20, refresh: { ms: 2000 }, region: { xfadeMs: 500 } },
    { polyphony: 1, speed: 0, heads: 1, grainLock: 1, outGain: -60, vibRate: 0.1, refresh: { ms: 5 }, region: { xfadeMs: 0 } }];
  const diffs = [];
  for (const p of ext) {
    const m = SLModel.normalizeInst(p), e = SE.normInst(m);
    for (const k of ['polyphony', 'speed', 'heads', 'grainLock', 'outGain', 'vibRate']) if (m[k] !== e[k]) diffs.push(`${k} ${m[k]} -> ${e[k]}`);
    if (m.refresh.ms !== e.refresh.ms) diffs.push('refresh.ms'); if (m.region.xfadeMs !== e.region.xfadeMs) diffs.push('xfadeMs');
  }
  check('SLModel.normalizeInst ranges survive the engine\'s normInst unchanged', diffs.length === 0, diffs.join(', ') || 'all kept', 'all kept');
  const ids = new Set(SLSounds.list.map(s => s.id));
  const missing = SLModel.SOUND_PRESETS.filter(p => !ids.has(p.sound)).map(p => p.sound);
  check('every SOUND_PRESET names a built-in sound; the default sound is sawopen', missing.length === 0 && SLSounds.defaultId === 'sawopen' && SLModel.SOUND_PRESETS[0].sound === 'sawopen', missing.join(',') || 'all found', 'all found');
  const roots = SLSounds.list.map(s => sound(s.id).rootMidi);
  check('starter sounds report rootMidi 60 (= inst.root default, C3)', roots.every(x => x === dm.root), roots.join(' '), String(dm.root));
});

// ================================================================== B. every clip preset, compiled and played
section('B. every CLIP_PRESET compiles to a valid snapshot and plays (sawopen, the default sound)', () => {
  const inst = SLModel.defaultInst();
  const peaks = [];
  for (const P of SLModel.CLIP_PRESETS) {
    const clip = SLModel.loadClipPreset(P.id);
    const snap = formatCheck(P.name, clip, inst);
    const o = renderSnap(sound('sawopen'), snap, inst, snap.bars * 4 * 60 / BPM);
    peaks.push(audioChecks(P.name + ' (one loop pass, sawopen)', o, snap, inst).pk);
  }
  info('peak over the clip presets: ' + dist(peaks, 3));
  // the Strand Demo with the other starter sounds named in the integration brief
  const demo = SLModel.loadClipPreset('Strand Demo'), snap = SLModel.compileSnapshot(demo, inst);
  for (const id of ['saw', 'beat', 'noise']) audioChecks(`Strand Demo with "${id}"`, renderSnap(sound(id), snap, inst, 8), snap, inst);
});

// ================================================================== C. recipes 1-8
section('C. recipes 1-8 applied to the Strand Demo play in their engines', () => {
  const demo = SLModel.loadClipPreset('Strand Demo'), others = ['saw', 'beat', 'noise'];
  for (const R of SLModel.RECIPES) {
    const { clip, inst } = SLModel.applyRecipe(demo, SLModel.defaultInst(), R.n, null);
    const snap = formatCheck(`recipe ${R.n} ${R.name}`, clip, inst);
    const wantEng = inst.engine === 'cloud' && inst.lock ? 'lock' : inst.engine;
    for (const id of ['sawopen', others[(R.n - 1) % 3]]) {
      const engs = new Set();
      const o = renderSnap(sound(id), snap, inst, 4, { hook: (i, e) => { if (i % 4096 === 0) for (const v of e.getDisplay().voices) engs.add(v.engine + '/' + v.motion); } });
      audioChecks(`recipe ${R.n} ${R.name} with "${id}"`, o, snap, inst);
      const seen = [...engs];
      check(`recipe ${R.n} with "${id}": voices play in ${wantEng}, motion ${inst.motion}`, seen.length > 0 && seen.every(s => s === wantEng + '/' + (inst.engine === 'tape' && inst.motion === 'freeze' ? 'forward' : inst.motion)), seen.join(' ') || 'no voices', wantEng + '/' + inst.motion);
    }
  }
  // Dice and the sound presets: whatever they roll still plays
  const peaks = [];
  for (let s = 1; s <= 6; s++) {
    const inst = SLModel.dice(SLModel.defaultInst(), s, []);
    const snap = SLModel.compileSnapshot(demo, inst);
    const o = renderSnap(sound(['sawopen', 'pad', 'vowel', 'pluck', 'square', 'beat'][s - 1]), snap, inst, 3);
    peaks.push(audioChecks(`dice seed ${s} (${inst.engine}${inst.lock ? '+lock' : ''}, ${inst.motion}, ${f2(inst.heads, 0)} heads)`, o, snap, inst).pk);
  }
  for (const P of SLModel.SOUND_PRESETS) {
    const { sound: sid, inst } = SLModel.applySoundPreset(SLModel.defaultInst(), P.id);
    const snap = SLModel.compileSnapshot(demo, inst);
    audioChecks(`sound preset "${P.name}"`, renderSnap(sound(sid), snap, inst, 3), snap, inst);
  }
});

// ================================================================== D. every engine on its own note, in tune
section('D. one note per engine (Tape, Voice, Cloud, Cloud+Lock) through the model: engine shown, C3 in tune', () => {
  let clip = SLModel.newClip(2), ids = [];
  const set = [['tape', false], ['voice', false], ['cloud', false], ['cloud', true]];
  set.forEach(([eng, lock], k) => { const r = SLModel.addNote(clip, 60, k * 2, 2); clip = SLModel.setNoteSample(r.clip, r.id, { engine: eng, lock }); ids.push(r.id); });
  const inst = SLModel.defaultInst();
  const snap = formatCheck('four engines', clip, inst);
  const shown = [];
  const o = renderSnap(sound('saw'), snap, inst, 4, { hook: (i, e) => { for (let k = 0; k < 4; k++) if (i === beatsToSamples(k * 2 + 1) - (beatsToSamples(k * 2 + 1) % 128)) shown[k] = e.getDisplay().voices.filter(v => !v.released).map(v => v.engine).join(','); } });
  audioChecks('four engines with "saw"', o, snap, inst);
  const want = ['tape', 'voice', 'cloud', 'lock'];
  check('display shows each note\'s own engine (D6)', want.every((w, k) => shown[k] === w), shown.join(' | '), want.join(' | '));
  const cents = set.map((_, k) => (pitchOf(o, beatsToSamples(k * 2 + 0.6), beatsToSamples(k * 2 + 1.6)) - 60) * 100);
  check('each engine plays MIDI 60 as C3 (261.6 Hz) from a C3 sound: error in cents', cents.every(c => Math.abs(c) < 5), want.map((w, k) => w + ' ' + f2(cents[k], 2)).join(', '), '|err| < 5');
  // the pitch lane and inst.pitch through the snapshot: +7 st drawn, +12 st inst.pitch -> MIDI 79
  let c2 = SLModel.newClip(1); const r2 = SLModel.addNote(c2, 60, 0, 4); c2 = SLModel.setLaneNodes(r2.clip, r2.id, 'pitch', [{ t: 0, v: 0.5 + 7 / 24, shape: 'linear', bend: 0 }, { t: 1, v: 0.5 + 7 / 24, shape: 'linear', bend: 0 }]);
  for (const eng of ['tape', 'voice']) {
    const I = SLModel.normalizeInst({ engine: eng, pitch: 12 });
    const o2 = renderSnap(sound('saw'), SLModel.compileSnapshot(c2, I), I, 2);
    const m = pitchOf(o2, 0.4 * SR, 1.6 * SR);
    check(`${eng}: pitch lane +7 st and inst.pitch +12 on C3 play G4 (MIDI 79)`, Math.abs(m - 79) < 0.05, f2(m, 3), '79 +- 0.05');
  }
});

// ================================================================== E. connected chain: one voice, the playhead carries
section('E. a connected chain plays on one voice and carries its playhead across the joint (D2)', () => {
  const base = () => {
    let c = SLModel.newClip(2);
    const a = SLModel.addNote(c, 60, 0, 2); c = a.clip;
    const b = SLModel.addNote(c, 64, 2, 2); c = b.clip;
    c = SLModel.connect(c, [a.id, b.id], 'nearest', 1);
    return { c, A: a.id, B: b.id };
  };
  const { c, A, B } = base();
  const inst = SLModel.normalizeInst({ glideMode: 'off', glideTime: 0.5 });   // the explicit link alone joins them
  const snap = formatCheck('connected pair', c, inst);
  const sa = snap.notes.find(n => n.id === A), sb = snap.notes.find(n => n.id === B);
  check('connect() + compileSnapshot: A glides into B and they share a chain', sa.glide && sa.glide.toId === B && Math.abs(sa.glide.d - 4) < 1e-9 && sa.chain.nextId === B && sb.chain.prevId === A && sb.chain.id === A,
    `glide ${JSON.stringify(sa.glide)}, chain A ${JSON.stringify(sa.chain)}`, 'toId B, d +4, A -> B');
  const fileSecs = sound('sawopen').channels[0].length / sound('sawopen').sampleRate;
  const trace = (snp, I) => {
    const rows = [];
    const o = renderSnap(sound('sawopen'), snp, I, 3.5, { block: 32, hook: (i, e) => { const d = e.getDisplay(); rows.push({ i, n: d.voices.length, v: d.voices.map(v => ({ id: v.noteId, env: v.env, pos: v.heads.length ? v.heads[0].pos : NaN, rel: v.released })) }); } });
    return { o, rows };
  };
  const J = beatsToSamples(2), d = 480;   // 10 ms either side of the joint
  const at = (rows, i) => rows.find(r => r.i >= i);
  const T = trace(snap, inst);
  const pre = at(T.rows, J - d), post = at(T.rows, J + d);
  const live = r => r.v.filter(v => !v.rel);
  const maxVoices = Math.max(...T.rows.filter(r => r.i > 0.05 * SR && r.i < beatsToSamples(3.9)).map(r => live(r).length));
  const minEnv = Math.min(...T.rows.filter(r => r.i > J - 4 * d && r.i < J + 4 * d).map(r => live(r).length ? live(r)[0].env : 0));
  const moved = (post && pre) ? (live(post)[0].pos - live(pre)[0].pos) * fileSecs : NaN, want = 2 * d / SR;   // Voice: 1x through the file
  check('one voice through the joint, no new attack (envelope stays at 1)', maxVoices === 1 && minEnv > 0.999 && live(pre)[0].id === A && live(post)[0].id === B,
    `max voices ${maxVoices}, min envelope ${f2(minEnv, 4)}, ${live(pre)[0].id === A ? 'A' : '?'} -> ${live(post)[0].id === B ? 'B' : '?'}`, '1, > 0.999, A -> B');
  check('the playhead carries: head travel across the joint = 20 ms of source at 1x', Math.abs(moved - want) < 0.5e-3, f2(moved * 1000, 3) + ' ms', f2(want * 1000, 3) + ' +- 0.5 ms');
  const mA = pitchOf(T.o, beatsToSamples(0.3), beatsToSamples(1.2)), mB = pitchOf(T.o, beatsToSamples(2.6), beatsToSamples(3.6));
  check('the glide lands on B\'s pitch (C3 -> E3)', Math.abs(mA - 60) < 0.05 && Math.abs(mB - 64) < 0.05, `A ${f2(mA, 3)}, B ${f2(mB, 3)}`, '60 and 64 +- 0.05');
  // control: Restart sample here on B splits the chain, so a second voice starts at the Region start
  const cR = SLModel.setNoteSample(c, B, { restart: true }), snapR = formatCheck('connected pair, B restarts', cR, inst);
  const TR = trace(snapR, inst), postR = at(TR.rows, J + d);
  const nb = live(postR).find(v => v.id === B);
  check('control: with Restart on B, B gets its own voice that starts at the Region start', snapR.notes.find(n => n.id === A).chain.nextId === null && nb && nb.pos < 0.01,
    `B head at ${nb ? f2(nb.pos, 4) : 'none'} of the file (chain carried it to ${f2(live(post)[0].pos, 4)})`, '< 0.01');
});

// ================================================================== F. Path kind
section('F. a Path-kind note: head 1 follows the drawn line through the Region (1.3)', () => {
  let c = SLModel.newClip(1);
  const r = SLModel.addNote(c, 60, 0, 4); c = SLModel.setNoteSample(r.clip, r.id, { travel: 'path' });
  c = SLModel.applyShape(c, r.id, 'travel', 'playthrough', 'each');
  for (const [eng, region] of [['voice', { start: 0, end: 1 }], ['cloud', { start: 0.2, end: 0.6 }]]) {
    const inst = SLModel.normalizeInst({ engine: eng, speed: 3, motion: 'backward', region });
    const snap = formatCheck(`path note (${eng})`, c, inst);
    const errs = [];
    renderSnap(sound('sawopen'), snap, inst, 1.9, { hook: (i, e) => { if (i % 4800 === 0 && i > 0) { const h = e.getDisplay().voices[0].heads[0]; const u = i / beatsToSamples(4); errs.push(Math.abs(h.pos - (region.start + u * (region.end - region.start)))); } } });
    check(`${eng}, Region ${region.start}..${region.end}, speed 3, Backward: head 1 at Region start + line x Region length`, Math.max(...errs) < 0.003, '|error| (file fraction) ' + dist(errs, 4), '< 0.003');
  }
  // Tape treats Path as speed kind at 1x
  const inst = SLModel.normalizeInst({ engine: 'tape' });
  const snap = SLModel.compileSnapshot(c, inst), fileSecs = sound('sawopen').channels[0].length / sound('sawopen').sampleRate;
  const pos = [];
  renderSnap(sound('sawopen'), snap, inst, 1.2, { hook: (i, e) => { if (i === 9600 || i === 57600) pos.push(e.getDisplay().voices[0].heads[0].pos); } });
  const spd = (pos[1] - pos[0]) * fileSecs / 1.0;
  check('Tape: a Path note plays at 1x (BLUEPRINT 1.3)', Math.abs(spd - 1) < 1e-3, f2(spd, 5) + 'x', '1 +- 0.001');
});

// ================================================================== G. Scatter with jump 1/8T, Re-roll
section('G. Scatter, jump 1/8T: jumps land on the song grid; same seed = same sound; Re-roll changes it', () => {
  let c = SLModel.newClip(1);
  const r = SLModel.addNote(c, 60, 0, 4); c = SLModel.setNoteSample(r.clip, r.id, { motion: 'scatter', jump: '1/8T', engine: 'cloud' });
  const inst = SLModel.normalizeInst({ grainCloud: 60 });
  const snap = formatCheck('scatter note', c, inst);
  check('jump 1/8T compiles to 1/3 beat', Math.abs(snap.notes[0].jump - 1 / 3) < 1e-12, String(snap.notes[0].jump), '0.33333');
  const run = (I, opts = {}) => {
    const pos = [];
    const o = renderSnap(sound('saw'), SLModel.compileSnapshot(c, I), I, 2.05, Object.assign({ block: 32, hook: (i, e) => { const v = e.getDisplay().voices[0]; pos.push(v ? v.heads[0].pos : NaN); } }, opts));
    return { o, pos };
  };
  const A = run(inst);
  const jumps = []; for (let k = 1; k < A.pos.length; k++) if (Math.abs(A.pos[k] - A.pos[k - 1]) > 0.004) jumps.push(k * 32);
  const grid = beatsToSamples(1 / 3), lines = Math.floor((A.o.L.length - 64) / grid);
  const errs = jumps.map(s => { const g = Math.round(s / grid) * grid; return s - g; });
  check('every head jump sits within one control tick after a 1/3-beat grid line', errs.every(x => x >= 0 && x <= 64), 'samples after the line: ' + dist(errs, 0), '0..64 (32-sample ticks, display every 32)');
  check('a jump on (nearly) every grid line', jumps.length >= lines - 1 && jumps.length <= lines, `${jumps.length} jumps on ${lines} lines`, `${lines - 1}..${lines}`);
  const B = run(inst);
  const Cb = run(inst, { block: (() => { const q = lcg(4); return () => 1 + Math.floor(q() * 700); })() });
  check('two renders of the same clip are bit-identical (also with random block sizes 1..700)', maxDiff(A.o.L, B.o.L) === 0 && maxDiff(A.o.R, B.o.R) === 0 && maxDiff(A.o.L, Cb.o.L) === 0, `max diff ${maxDiff(A.o.L, B.o.L)} / ${maxDiff(A.o.L, Cb.o.L)}`, '0 / 0');
  const D = run(Object.assign({}, inst, { seed: inst.seed + 1 }));   // Re-roll for Scatter = inst.seed + 1 (one undo step in the UI)
  let movedJ = 0, cmpJ = 0;
  for (const s of jumps) { const k = Math.min(A.pos.length - 1, Math.floor(s / 32) + 2); if (k < D.pos.length) { cmpJ++; if (Math.abs(A.pos[k] - D.pos[k]) > 0.004) movedJ++; } }
  const corr = (() => { let ab = 0, aa = 0, bb = 0; for (let i = 0; i < A.o.L.length; i++) { ab += A.o.L[i] * D.o.L[i]; aa += A.o.L[i] ** 2; bb += D.o.L[i] ** 2; } return ab / Math.sqrt(aa * bb); })();
  check('Re-roll (inst.seed + 1) moves the jump targets and changes the sound', movedJ >= 0.8 * cmpJ && corr < 0.9, `${movedJ} of ${cmpJ} targets moved, output correlation ${f2(corr, 3)}`, '>= 80 %, < 0.9');
  // SLModel.reroll on a random Start changes where the note starts
  let c2 = SLModel.newClip(1); const r2 = SLModel.addNote(c2, 60, 0, 2); c2 = SLModel.setNoteSample(r2.clip, r2.id, { start: { mode: 'random', pos: 0, range: 1 } });
  const startPos = cc => { let p = NaN; renderSnap(sound('saw'), SLModel.compileSnapshot(cc, inst), inst, 0.02, { hook: (i, e) => { if (i === 256) p = e.getDisplay().voices[0].heads[0].pos; } }); return p; };
  const p1 = startPos(c2), p1b = startPos(c2), p2 = startPos(SLModel.reroll(c2, null, null));
  check('random Start: same clip = same start; SLModel.reroll moves it', p1 === p1b && Math.abs(p2 - p1) > 0.01, `start ${f2(p1, 4)}, again ${f2(p1b, 4)}, after Re-roll ${f2(p2, 4)}`, 'same, then moved');
});

// ================================================================== H. lane readouts agree with what plays
section('H. SLModel.formatLane / mapLane agree with what the engine plays (Heads, Travel)', () => {
  // Heads: whole counts drawn on the lane (as the model's Float32 table holds them) show exactly that many heads
  const rows = [];
  for (let k = 1; k <= 8; k++) {
    let c = SLModel.newClip(1); const r = SLModel.addNote(c, 60, 0, 4);
    const v = 0.5 + (k - 1) / 14;
    c = SLModel.setLaneNodes(r.clip, r.id, 'heads', [{ t: 0, v, shape: 'linear', bend: 0 }, { t: 1, v, shape: 'linear', bend: 0 }]);
    const inst = SLModel.normalizeInst({ heads: 1 });
    let shown = -1; renderSnap(sound('saw'), SLModel.compileSnapshot(c, inst), inst, 0.1, { hook: (i, e) => { if (i === 2048) shown = e.getDisplay().voices[0].heads.length; } });
    rows.push([SLModel.formatLane('heads', v, { inst }), shown, k]);
  }
  check('Heads lane: readout "k heads" shows exactly k heads (k = 1..8)', rows.every(r => r[1] === r[2] && r[0] === (r[2] === 1 ? '1 head' : r[2] + ' heads')), rows.map(r => `'${r[0]}' -> ${r[1]}`).join(', '), 'k -> k');
  // Travel speed readout vs measured head speed (Voice: 1x moves 1 s of source per second)
  const fileSecs = sound('saw').channels[0].length / sound('saw').sampleRate;
  const vs = [0.02, 0.25, Math.fround(0.48), 0.48, 0.5, 0.52, Math.fround(0.52), SLModel.travelSpeedToV(0.125), SLModel.travelSpeedToV(-0.125), 0.6, 0.75, 0.9, 0.98];
  const bad = [];
  for (const v of vs) {
    let c = SLModel.newClip(1); const r = SLModel.addNote(c, 60, 0, 4);
    c = SLModel.setLaneNodes(r.clip, r.id, 'travel', [{ t: 0, v, shape: 'linear', bend: 0 }, { t: 1, v, shape: 'linear', bend: 0 }]);
    c = SLModel.setNoteSample(c, r.id, { start: { mode: 'region', pos: 0.5 } });   // room to travel both ways
    const inst = SLModel.normalizeInst({ region: { start: 0.1, end: 0.9, atEnd: 'hold' } });
    const pos = []; renderSnap(sound('saw'), SLModel.compileSnapshot(c, inst), inst, 0.25, { hook: (i, e) => { if (i === 2048 || i === 6912) pos.push(e.getDisplay().voices[0].heads[0].pos); } });
    const got = (pos[1] - pos[0]) * fileSecs / (4864 / SR), exp = SLModel.mapLane('travel', v, { inst });
    if (!(Math.abs(got - exp) <= Math.max(1e-3, Math.abs(exp) * 2e-3))) bad.push(`v ${v}: readout '${SLModel.formatLane('travel', v, { inst })}' (${f2(exp, 4)}x), plays ${f2(got, 4)}x`);
  }
  check('Travel: the readout speed is the speed that plays (incl. the dead-band edges and travelSpeedToV(+-1/8))', bad.length === 0, bad.join('; ') || `all ${vs.length} agree`, 'all agree');
});

// ================================================================== J. busy patches and the loop period, through the model
section('J. a busy 8-head patch thins by fading (no tail cuts, budget kept); the beat loop keeps time over passes', () => {
  const demo = SLModel.loadClipPreset('Strand Demo');
  for (const [label, n, patch] of [['recipe 6 Slow bloom, heads 8', 6, { heads: 8 }], ['recipe 8 Octave shimmer, heads 8, Grain 12', 8, { heads: 8, grainLock: 12 }], ['recipe 3 Frozen choir, heads 8', 3, { heads: 8 }]]) {
    const r = SLModel.applyRecipe(demo, SLModel.defaultInst(), n, null), inst = SLModel.normalizeInst(Object.assign({}, r.inst, patch, { outGain: -12 }));
    const snap = SLModel.compileSnapshot(r.clip, inst);
    const go = big => { let e0 = null; const o = renderSnap(sound('pad'), snap, inst, 3, { hook: (i, e) => { if (!e0) { e0 = e; if (big) e.budget = 1e9; } } }); return { o, s: o.e.stats() }; };
    const A = go(false), B = go(true);
    const d2 = x => { let m = 0; for (let i = 2; i < x.length; i++) m = Math.max(m, Math.abs(x[i] - 2 * x[i - 1] + x[i - 2])); return m; };
    const mA = d2(A.o.L), mB = d2(B.o.L);
    check(`${label}: budget kept, no fade tail cut, thinning adds no click (max |2nd difference| vs an unlimited budget)`, A.s.maxGrains <= A.s.budget && A.s.cuts === 0 && mA <= 2 * mB + 1e-5,
      `max load ${A.s.maxGrains} of ${A.s.budget} (unlimited: ${B.s.maxGrains}), cuts ${A.s.cuts}, thinned now ${A.s.thinnedHeads}, 2nd diff ${mA.toExponential(2)} vs ${mB.toExponential(2)}`, '<= budget, 0, <= 2x');
  }
  // a 16-beat note on the 2-bar (4 s) Beat loop at 120 bpm, Tape at 1x: the head is at the same spot in every pass
  let c = SLModel.newClip(4); const rr = SLModel.addNote(c, 60, 0, 16); c = rr.clip;
  const inst = SLModel.normalizeInst({ engine: 'tape' }), snap = SLModel.compileSnapshot(c, inst);
  const pos = [];
  renderSnap(sound('beat'), snap, inst, 7.9, { hook: (i, e) => { if ([24064, 216064].includes(i)) pos.push(e.getDisplay().voices[0].heads[0].pos); } });
  const fileSecs = sound('beat').channels[0].length / sound('beat').sampleRate;
  const drift = pos.map(p => (p - pos[0]) * fileSecs * 1000);
  check('Beat loop (whole-file Region, 4 s) under one 8 s note: same head position 0.5 s into passes 1 and 2', drift.every(d => Math.abs(d) < 0.05),
    'offsets vs pass 1 ' + drift.map(d => f2(d, 3) + ' ms').join(', '), '|.| < 0.05 ms');
  // a partial Region (with material after it) loops with period = Region length too
  const inst2 = SLModel.normalizeInst({ engine: 'tape', region: { start: 0, end: 0.5 } });
  const pos2 = [];
  renderSnap(sound('beat'), SLModel.compileSnapshot(c, inst2), inst2, 7.9, { hook: (i, e) => { if ([24064, 120064, 216064, 312064].includes(i)) pos2.push(e.getDisplay().voices[0].heads[0].pos); } });
  const drift2 = pos2.map(p => (p - pos2[0]) * fileSecs * 1000);
  check('Region = first bar of the Beat loop (crossfaded seam, 2 s): same head position 0.5 s into passes 1-4', drift2.every(d => Math.abs(d) < 0.05), 'offsets vs pass 1 ' + drift2.map(d => f2(d, 3) + ' ms').join(', '), '|.| < 0.05 ms');
});

// ================================================================== I. the worklet path
section('I. the worklet text (Blob) plays a structured-cloned snapshot exactly like the Engine', () => {
  let reg = null; const posted = [];
  class FakeAWP { constructor() { this.port = { postMessage: m => posted.push(m), onmessage: null }; } }
  new Function('registerProcessor', 'AudioWorkletProcessor', 'sampleRate', createStrandEngineModule.toString() + '\n' + strandWorkletGlue.toString() + '\nstrandWorkletGlue();')((n, c) => { reg = { n, c }; }, FakeAWP, SR);
  check('the Blob text registers "strand-engine"', reg && reg.n === 'strand-engine', reg ? reg.n : 'nothing', 'strand-engine');
  const p = new reg.c(), send = m => p.port.onmessage({ data: structuredClone(m) });   // postMessage clones its data
  const demo = SLModel.loadClipPreset('Strand Demo'), inst = SLModel.applyRecipe(demo, SLModel.defaultInst(), 7, null);
  const snap = SLModel.compileSnapshot(inst.clip, inst.inst), snd = sound('sawopen');
  send({ type: 'sample', channels: snd.channels, sampleRate: snd.sampleRate, name: snd.name });
  send({ type: 'inst', inst: inst.inst }); send({ type: 'snapshot', snapshot: snap }); send({ type: 'transport', playing: true, bpm: BPM, beat: 0, loop: true });
  const n = 2 * SR, WL = new Float32Array(n);
  for (let i = 0; i < n; i += 128) { const o = [[new Float32Array(128), new Float32Array(128)]]; p.process([], o); WL.set(o[0][0], i); }
  const ref = renderSnap(snd, snap, inst.inst, 2);
  const disp = posted.filter(m => m.type === 'display'), errs = posted.filter(m => m.type === 'error');
  check('worklet output = direct Engine output (structured-cloned Float32 tables)', maxDiff(WL, ref.L) === 0 && rms(WL) > 1e-3, `max diff ${maxDiff(WL, ref.L)}, rms ${f2(rms(WL), 4)}`, '0, sounding');
  check('worklet posts ~30 displays a second with voices, heads and stats, and no errors', errs.length === 0 && disp.length >= 55 && disp.length <= 65 && disp.some(d => d.voices.length && d.voices[0].heads.length && d.stats && d.grains),
    `${disp.length} displays in 2 s, ${errs.length} errors`, '55..65, 0');
});

console.log(`\n${passed} passed, ${failed} failed`);
if (fails.length) { console.log('failures:'); fails.forEach(f => console.log('  - ' + f)); }
process.exit(failed ? 1 : 0);
