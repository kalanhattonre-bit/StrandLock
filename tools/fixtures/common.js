// tools/fixtures/common.js: shared pieces of the golden-fixture generator (docs/PLUGIN-PLAN.md section 5).
// Loads the tested prototype exactly as prototype-v4/tests do, encodes values in the section 5.1 shapes, and builds
// the named input clips of section 5.2 (every id explicit). No npm packages, no Math.random, no Date.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..', '..');
const PROTO = path.join(ROOT, 'prototype-v4');
const SRC = path.join(PROTO, 'src');
const TESTS = path.join(PROTO, 'tests');

// ------------------------------------------------------------------------------------------- loading
let loaded = null;
function load() {
  if (loaded) return loaded;
  const M = require(path.join(SRC, 'sl-model.js'));
  const engMod = require(path.join(SRC, 'sl-engine.js'));
  // the engine as the worklet gets it: its own toString(), evaluated where no outside name exists (test-engine.js)
  const engText = engMod.createStrandEngineModule.toString();
  const SE = new Function('"use strict";\n' + engText + '\nreturn createStrandEngineModule();')();
  const SND = require(path.join(SRC, 'sl-sounds.js'));
  const AN = require(path.join(SRC, 'sl-analysis.js'));
  const PAN = require(path.join(SRC, 'sl-ui-panels.js'));      // no window: returns its pure helpers
  const SUI = require(path.join(SRC, 'sl-ui-sample.js'));      // no window: returns its pure part
  loaded = { M, SE, SND, AN, PAN, SUI, engText };
  return loaded;
}

// Constants the prototype keeps private (the engine's HANN/FADE tables, grain caps and salts; SLSampleUI's readout
// formatters). Read from an in-memory copy of the real source with one extra name list added to its return value;
// the files on disk are never changed, and every render uses the unmodified text above.
function injectOnce(text, re, replacement, what) {
  const m = text.match(new RegExp(re.source, 'g'));
  if (!m || m.length !== 1) throw new Error('make-fixtures: could not find the ' + what + ' anchor exactly once (found ' + (m ? m.length : 0) + ')');
  return text.replace(re, replacement);
}
let engInternals = null;
function engineInternals() {
  if (engInternals) return engInternals;
  const { engText } = load();
  const t = injectOnce(engText, /return \{\s*Engine, rateBeats, version,/,
    'return { __internals: { HANN, FADE, HANN_N, FADE_N, VOICE_GRAINS, CLOUD_GRAINS, SALT_JUMP, SALT_SKIP, SALT_SPRAY, SALT_DRIFT, SALT_TINY, SALT_START }, Engine, rateBeats, version,', 'engine export');
  engInternals = new Function('"use strict";\n' + t + '\nreturn createStrandEngineModule();')().__internals;
  return engInternals;
}
let suiInternals = null;
function sampleUiInternals() {
  if (suiInternals) return suiInternals;
  const { M } = load();
  const file = path.join(SRC, 'sl-ui-sample.js');
  const t = injectOnce(fs.readFileSync(file, 'utf8'), /const pure = \{ contexts,/,
    'const pure = { __fmt: { fmtX, fmtPct, fmtSt, fmtCycles, fmtMs, fmtHeads, signed, trimZ }, contexts,', 'SLSampleUI pure');
  const req = p => { if (/sl-model\.js$/.test(p)) return M; throw new Error('unexpected require ' + p); };
  suiInternals = new Function('require', 'module', 'window', 'document', t + '\nreturn SLSampleUI;')(req, undefined, undefined, undefined).__fmt;
  return suiInternals;
}

// A top-level function of a prototype test file, by name (brace matched), so support/signals.json and the engine
// renders use the tests' own signal generators (tests/test-engine.js, tests/test-engine-extra.js) word for word.
function testFunctionSource(file, name) {
  const text = fs.readFileSync(path.join(TESTS, file), 'utf8');
  const head = new RegExp('^function ' + name + '\\(', 'm').exec(text);
  if (!head) throw new Error('make-fixtures: ' + file + ' has no function ' + name);
  // skip the parameter list first (a default such as opts = {} holds braces), then match the body's braces
  let i = text.indexOf('(', head.index), depth = 0;
  for (; i < text.length; i++) { const ch = text[i]; if (ch === '(') depth++; else if (ch === ')' && --depth === 0) break; }
  i = text.indexOf('{', i); depth = 0;
  for (; i < text.length; i++) { const ch = text[i]; if (ch === '{') depth++; else if (ch === '}' && --depth === 0) break; }
  return text.slice(head.index, i + 1);
}
function testConstSource(file, name) {
  const text = fs.readFileSync(path.join(TESTS, file), 'utf8');
  const m = new RegExp('^const ' + name + ' = [^\\n]*$', 'm').exec(text);
  if (!m) throw new Error('make-fixtures: ' + file + ' has no const ' + name);
  return m[0];
}
let signalFns = null;
function signals() {
  if (signalFns) return signalFns;
  const E = 'test-engine.js', X = 'test-engine-extra.js';
  const text = [testConstSource(E, 'midiHz'), ...['lcg', 'tone', 'noise', 'formantTone', 'smoothNoise', 'chord', 'rampSource'].map(n => testFunctionSource(E, n)),
    testFunctionSource(X, 'dc')].join('\n');
  signalFns = new Function('"use strict";\n' + text + '\nreturn { lcg, tone, noise, chord, formantTone, smoothNoise, rampSource, dc };')();
  return signalFns;
}

// ------------------------------------------------------------------------------------------- hashes
function sha256OfFile(file) {
  const t = fs.readFileSync(file).toString('utf8').replace(/\r\n/g, '\n');
  return crypto.createHash('sha256').update(t, 'utf8').digest('hex');
}
const CORE_FILES = ['src/sl-model.js', 'src/sl-engine.js', 'src/sl-sounds.js', 'src/sl-analysis.js'];
// keys are paths inside prototype-v4 ("sl-model.js" etc. for the four core modules, as the plan writes them)
function prototypeHashes(extra) {
  const out = {};
  for (const f of CORE_FILES) out[path.basename(f)] = sha256OfFile(path.join(PROTO, f));
  for (const f of extra || []) out[f] = sha256OfFile(path.join(PROTO, f));
  return out;
}
// FNV-1a 32 over the little-endian bytes of Float32 arrays, in order (the prototype tests' own checksum, fnv())
function fnvF32(arrs) {
  let h = 0x811c9dc5;
  for (const a of arrs) {
    const u = new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
    for (let i = 0; i < u.length; i++) { h ^= u[i]; h = Math.imul(h, 0x01000193); }
  }
  return (h >>> 0).toString(16).padStart(8, '0');
}

// ------------------------------------------------------------------------------------------- encoding (5.1)
function f32(arr) {
  const a = arr instanceof Float32Array ? arr : Float32Array.from(arr);
  const b = Buffer.alloc(a.length * 4);
  for (let i = 0; i < a.length; i++) b.writeFloatLE(a[i], i * 4);
  return { f32: b.toString('base64') };
}
// Table = F32 | { n, flat }: a table whose values are all the same binary32 is written as its count and that value
function table(arr) {
  const n = arr.length;
  let same = n > 0;
  for (let i = 1; i < n && same; i++) if (!Object.is(arr[i], arr[0])) same = false;
  return same ? { n, flat: arr[0] } : f32(arr);
}
const SHAPE_CODE = { smooth: 0, linear: 1, bend: 2, step: 3 };
const nodes = nd => nd.map(q => [q.t, q.v, SHAPE_CODE[q.shape], q.bend]);
const isFlatDefault = (nd, def) => nd.length === 2 && nd[0].t === 0 && nd[1].t === 1 && nd[0].v === def && nd[1].v === def &&
  nd[0].shape === 'smooth' && nd[1].shape === 'smooth' && nd[0].bend === 0 && nd[1].bend === 0;
function gen(g) {
  return { kind: g.kind, shape: g.shape, rate: g.rate, d0: g.d0, d1: g.d1, warp: g.warp, lockToBeat: g.lockToBeat, amount: g.amount,
    smooth: g.smooth, perNote: g.perNote, seed: g.seed, invert: g.invert, key: g.key };
}
function note(n) {
  const { M } = load();
  const sm = n.sample, st = sm.start;
  const o = { id: n.id, p: n.p, s: n.s, l: n.l, vel: n.vel, glide: n.glide, gc: n.gc, glideTo: n.glideTo,
    sample: { motion: sm.motion, travel: sm.travel, start: { mode: st.mode, pos: st.pos, range: st.range, seed: st.seed },
      tape: sm.tape, rev: sm.rev, jump: sm.jump, engine: sm.engine, lock: sm.lock, restart: sm.restart } };
  const c = {}, g = {};
  let anyC = false, anyG = false;
  for (const L of M.LANES) {
    const nd = n.c && n.c[L.id];
    if (Array.isArray(nd) && nd.length && !isFlatDefault(nd, L.def)) { c[L.id] = nodes(nd); anyC = true; }
    const gg = n.gen && n.gen[L.id];
    if (gg) { g[L.id] = gen(gg); anyG = true; }
  }
  if (anyC) o.c = c;
  if (anyG) o.gen = g;
  return o;
}
const clip = c => ({ bars: c.bars, notes: c.notes.map(note) });
// a deep copy with fixed key order (JSON-safe; the prototype's own inst objects already have a fixed order)
const plain = x => JSON.parse(JSON.stringify(x));

// JSON text: numbers as JSON.stringify prints them (shortest round trip), NaN/Infinity as strings, typed arrays as F32
function replacer(k, v) {
  if (typeof v === 'number') {
    if (v !== v) return 'NaN';
    if (v === Infinity) return 'Infinity';
    if (v === -Infinity) return '-Infinity';
    return v;
  }
  if (v instanceof Float32Array) return f32(v);
  if (ArrayBuffer.isView(v)) throw new Error('make-fixtures: unexpected typed array ' + v.constructor.name + ' at key ' + k);
  if (v instanceof Map || v instanceof Set) throw new Error('make-fixtures: Map/Set at key ' + k + ' must be converted first');
  return v;
}
const S = x => JSON.stringify(x, replacer);
// one fixture file: header on the first line, one clip per line, one case per line
function fixtureText(name, f) {
  const head = { fixture: name.replace(/\.json$/, ''), version: 1, generator: 'tools/make-fixtures.js', prototype: f.prototype, tolerance: f.tolerance };
  if (f.notes && f.notes.length) head.notes = f.notes;
  let out = S(head).slice(0, -1);
  for (const key of ['clips', 'insts']) {
    const d = f[key];
    if (d && Object.keys(d).length) out += ',' + S(key) + ':{\n' + Object.keys(d).map(k => S(k) + ':' + S(d[k])).join(',\n') + '\n}';
  }
  out += ',"cases":[\n' + f.cases.map(S).join(',\n') + '\n]}\n';
  return out;
}

// ------------------------------------------------------------------------------------------- small deterministic helpers
// the prototype tests' rng: rand01(hash32(seed, i++))
function rng(seed) { const { M } = load(); let i = 0; return () => M.rand01(M.hash32(seed, i++)); }
// next / previous binary32 (bit pattern stepping, as SLModel's f32Next)
function f32Step(x, dir) { const f = new Float32Array([x]), i = new Int32Array(f.buffer); i[0] += dir; return f[0]; }
// clip lookups for cases that name a clip preset: "preset:<id>" = SLModel.loadClipPreset(id)
function clipRef(name, named, recipe) {
  const { M } = load();
  let c;
  if (name.startsWith('preset:')) c = M.loadClipPreset(name.slice(7));
  else if (named[name]) c = named[name];
  else throw new Error('make-fixtures: unknown clip ' + name);
  // a case's "recipe": n = that clip after applyRecipe(clip, inst, n, every note) (the clip half does not depend on the inst)
  if (recipe != null) c = M.applyRecipe(c, M.defaultInst(), recipe, null).clip;
  return c;
}

// ------------------------------------------------------------------------------------------- named clips (5.2)
// Each builder returns a clip already passed through SLModel.normalizeClip (so decoding it through the C++
// normalizeClip is the identity). Ids are explicit; nothing calls mkNote without an id.
function namedClips() {
  const { M } = load();
  const N = (p, s, l, id, f) => { const n = M.mkNote(p, s, l, id); if (f) f(n); return n; };
  const fin = (bars, notes) => M.normalizeClip({ bars, notes });
  const out = {};

  // shapes4: 0.5, 1, 2.75 (Path-kind Travel) and 4 beats; s4-a and s4-b are a chord, b->c and c->d touch
  out.shapes4 = fin(4, [
    N(60, 0, 0.5, 's4-a'),
    N(64, 0, 1, 's4-b', n => { n.c.amp = M.normalizeNodes([{ t: 0, v: 0.3 }, { t: 1, v: 0.9 }], 0.8); }),
    N(67, 1, 2.75, 's4-c', n => { n.sample.travel = 'path'; n.c.travel = M.normalizeNodes([{ t: 0, v: 0.1, shape: 'linear' }, { t: 1, v: 0.6 }], 0.75); }),
    N(72, 3.75, 4, 's4-d', n => { n.c.pitch = M.normalizeNodes([{ t: 0, v: 0.5 - 2 / 24 }, { t: 0.2, v: 0.5 }, { t: 1, v: 0.5 }], 0.5); }),
  ]);

  // connect-grid: chords, ties in pitch distance, the +-24 st limit, the 0.13-beat overlap edge, the 0.05-beat chord
  // window, and a link coming in from outside the selection (cg-x -> cg-21, cg-x is never selected)
  out['connect-grid'] = fin(4, [
    N(60, 0, 1, 'cg-01'), N(64, 0, 1, 'cg-02'), N(67, 0, 1, 'cg-03'),
    N(62, 1, 1, 'cg-11'), N(66, 1, 1, 'cg-12'),                    // 64 is 2 from both: a tie in pitch distance
    N(69, 1.03, 0.97, 'cg-13'),                                    // inside the 0.05 chord window of beat 1
    N(71, 1.06, 0.94, 'cg-14'),                                    // just outside it
    N(60, 2, 1, 'cg-21'),
    N(63, 2.87, 1, 'cg-22'),                                       // on the 0.13 overlap edge of cg-21 (ends at 3)
    N(65, 2.869, 1, 'cg-23'),                                      // just inside the overlap (not a candidate)
    N(60, 4, 1, 'cg-31'),
    N(84, 5, 1, 'cg-41'),                                          // +24 st: allowed
    N(85, 5, 1, 'cg-42'),                                          // +25 st: too far
    N(36, 5, 1, 'cg-43'),                                          // -24 st: allowed
    N(59, 1.5, 0.5, 'cg-x', n => { n.glideTo = 'cg-21'; }),
  ]);

  // voice-lead: chord changes whose bass can only reach the next bass (uppers are > 12 st away), explicit-link
  // targets beyond 12 st, glide 0 and a restart
  out['voice-lead'] = fin(4, [
    N(36, 0, 2, 'vl-b1'), N(60, 0, 2, 'vl-a1'), N(64, 0, 2, 'vl-a2'), N(67, 0, 2, 'vl-a3'),
    N(31, 2, 2, 'vl-b2'), N(59, 2, 2, 'vl-c1'), N(62, 2, 2, 'vl-c2'), N(67, 2, 2, 'vl-c3', n => { n.glide = 0; }),
    N(29, 4, 2, 'vl-b3'), N(57, 4, 2, 'vl-d1'), N(60, 4, 2, 'vl-d2', n => { n.gc = 0.6; }), N(65, 4, 2, 'vl-d3', n => { n.glideTo = 'vl-e4'; n.glide = 1; }),
    N(24, 6, 2, 'vl-b4'), N(55, 6, 2, 'vl-e1'), N(60, 6, 2, 'vl-e2', n => { n.sample.restart = true; }), N(64, 6, 2, 'vl-e3'), N(79, 6, 2, 'vl-e4'),
    N(48, 8, 2, 'vl-f1', n => { n.glide = 0; n.glideTo = 'vl-f2'; }), N(67, 10, 2, 'vl-f2', n => { n.c.pitch = M.normalizeNodes([{ t: 0, v: 0.5 + 1 / 24 }, { t: 0.3, v: 0.5 }, { t: 1, v: 0.5 }], 0.5); }),
  ]);

  // chop-src: a Different random generator, a Same pattern, a link, Start positions
  out['chop-src'] = fin(4, [
    N(60, 0, 2, 'cs-a', n => {
      n.gen.amp = M.makeGen('random', { perNote: 'different', seed: 3, amount: 0.8, rate: '1/8', smooth: 0.3 });
      n.c.tone = M.normalizeNodes([{ t: 0, v: 0.9 }, { t: 0.5, v: 0.4, shape: 'bend', bend: 0.4 }, { t: 1, v: 0.7 }], 1);
      n.c.travel = M.normalizeNodes([{ t: 0, v: 0.75, shape: 'step' }, { t: 0.6, v: 0.3 }, { t: 1, v: 0.3 }], 0.75);
      n.glideTo = 'cs-c';
      n.sample.start = { mode: 'region', pos: 0.2, range: 0.25, seed: 1 };
    }),
    N(64, 0, 1.5, 'cs-b', n => { n.gen.tone = M.makeGen('pattern', { shape: 'squiggle', rate: '1/16', d0: 0.2, d1: 0.5, perNote: 'same' }); n.c.vib = M.normalizeNodes([{ t: 0, v: 0 }, { t: 1, v: 0.6 }], 0); }),
    N(67, 2, 1, 'cs-c', n => { n.sample.start = { mode: 'region', pos: 0.9, range: 0.25, seed: 1 }; }),
    N(55, 3, 0.75, 'cs-d', n => { n.sample.start = { mode: 'random', pos: 0.1, range: 0.5, seed: 4 }; n.gen.pitch = M.makeGen('pattern', { shape: 'randsteps', rate: '1/32', d0: 0.5, perNote: 'different', seed: 9 }); }),
  ]);

  // humanized-chord: chords whose starts are spread a little (inside and on the edge of the 0.05 window)
  out['humanized-chord'] = fin(2, [
    N(48, 0, 1, 'hc-1'), N(55, 0.02, 0.98, 'hc-2'), N(60, 0.04, 0.96, 'hc-3'), N(64, 0.05, 0.95, 'hc-4'),
    N(50, 1, 1, 'hc-5'), N(57, 1.03, 0.97, 'hc-6'), N(62, 1.051, 0.949, 'hc-7'),
    N(53, 2, 2, 'hc-8'), N(60, 2.01, 1.99, 'hc-9'), N(65, 2.049, 1.951, 'hc-10'),
    N(52, 5, 1, 'hc-11'),
  ]);

  // gens: every generator kind on several lanes, a seed at the int32 edge, a key, invert, a random Start
  out.gens = fin(4, [
    N(60, 0, 2, 'gn-1', n => { n.gen.amp = M.makeGen('pattern', { shape: 'triangle', rate: '1/8', d0: 0.4, d1: 0.1, seed: 2147483647 }); n.gen.tone = M.makeGen('random', { seed: 5, smooth: 0.5 }); }),
    N(62, 2, 2, 'gn-2', n => { n.gen.pitch = M.makeGen('random', { perNote: 'different', seed: -2147483648, amount: 1 }); n.gen.formant = M.makeGen('pattern', { shape: 'sawup', rate: '1/4T', lockToBeat: false, warp: 0.8 }); }),
    N(64, 4, 1, 'gn-3', n => { n.gen.heads = M.makeGen('pattern', { shape: 'square', rate: '1/16', d0: 0.6 }); n.sample.start = { mode: 'random', pos: 0, range: 1, seed: 2147483647 }; }),
    N(65, 5, 3, 'gn-4', n => { n.gen.vib = M.makeGen('pattern', { shape: 'smoothrandom', rate: '1/8D', perNote: 'different', key: 'gn-1', invert: true }); n.gen.travel = M.makeGen('random', { seed: 7, amount: 0.3 }); }),
    N(67, 8, 4, 'gn-5', n => { n.sample.start = { mode: 'random', pos: 0.25, range: 0.5, seed: 1 }; }),
  ]);

  // mixed-settings: every per-note override, all four Start modes, restart
  out['mixed-settings'] = fin(4, [
    N(60, 0, 2, 'ms-1', n => { Object.assign(n.sample, { motion: 'backward', engine: 'tape', tape: true, jump: '1/16T' }); n.sample.start = { mode: 'region', pos: 0.4, range: 0.25, seed: 1 }; }),
    N(62, 2, 2, 'ms-2', n => { Object.assign(n.sample, { motion: 'bounce', engine: 'cloud', lock: true, rev: false }); n.sample.start = { mode: 'carry', pos: 0, range: 0.25, seed: 1 }; }),
    N(64, 4, 2, 'ms-3', n => { Object.assign(n.sample, { motion: 'scatter', engine: 'voice', tape: false, rev: true, jump: '1/8T' }); n.sample.start = { mode: 'song', pos: 0, range: 0.25, seed: 1 }; }),
    N(65, 6, 2, 'ms-4', n => { Object.assign(n.sample, { motion: 'freeze', engine: 'cloud', lock: false, travel: 'path' }); n.sample.start = { mode: 'random', pos: 0.1, range: 0.6, seed: 3 };
      n.c.travel = M.normalizeNodes([{ t: 0, v: 0.2, shape: 'linear' }, { t: 1, v: 0.8 }], 0.75); }),
    N(67, 8, 2, 'ms-5', n => { Object.assign(n.sample, { motion: 'mirror', restart: true }); }),
    N(69, 10, 2, 'ms-6', n => { Object.assign(n.sample, { engine: 'cloud', tape: true, rev: true }); n.c.heads = M.normalizeNodes([{ t: 0, v: 0.5 }, { t: 1, v: 0.75 }], 0.5); }),
    N(72, 12, 4, 'ms-7'),
  ]);
  return out;
}

module.exports = {
  ROOT, PROTO, SRC, TESTS, load, engineInternals, sampleUiInternals, signals, testFunctionSource,
  prototypeHashes, sha256OfFile, fnvF32, f32, table, nodes, note, clip, gen, plain, S, fixtureText, rng, f32Step, clipRef, namedClips,
  SHAPE_CODE,
};
