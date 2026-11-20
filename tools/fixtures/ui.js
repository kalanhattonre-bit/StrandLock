// tools/fixtures/ui.js: Tests/fixtures/ui/theme.json, inspector.json, sample-rules.json, readouts.json (PLUGIN-PLAN 5.3):
// the theme tokens of shell.html and the pure helpers of sl-ui-panels.js (SLPanels) and sl-ui-sample.js (SLSampleUI).
'use strict';
const fs = require('fs');
const path = require('path');
const C = require('./common.js');
const { starter } = require('./sounds.js');

function theme() {
  const html = fs.readFileSync(path.join(C.SRC, 'shell.html'), 'utf8').replace(/\r\n/g, '\n');
  const block = sel => {
    const i = html.indexOf(sel + '{');
    if (i < 0) throw new Error('make-fixtures: shell.html has no ' + sel + ' block');
    return html.slice(i + sel.length + 1, html.indexOf('}', i));
  };
  const parse = text => {
    const tokens = {}, re = /--([a-z0-9-]+)\s*:\s*([^;]+);/g; let m;
    while ((m = re.exec(text))) tokens[m[1]] = m[2].trim();
    const cs = /color-scheme\s*:\s*([a-z]+)/.exec(text);
    return { colorScheme: cs ? cs[1] : null, tokens };
  };
  const dark = parse(block(':root')), light = parse(block(':root[data-theme="light"]')), media = parse(block(':root:not([data-theme="dark"])'));
  if (JSON.stringify(media) !== JSON.stringify(light)) throw new Error('make-fixtures: shell.html\'s two light blocks differ');
  const nd = Object.keys(dark.tokens).length, nl = Object.keys(light.tokens).length;
  if (nd !== nl || Object.keys(dark.tokens).some(k => !(k in light.tokens))) throw new Error('make-fixtures: the dark and light token sets differ');
  return { prototype: C.prototypeHashes(['src/shell.html']),
    tolerance: { text: 'exact strings', default: 'exact' },
    notes: ['tokens are the CSS custom properties of shell.html (without the leading --), values as written; ' + nd + ' tokens per theme',
      'dark = the bare :root block; light = :root[data-theme="light"] (checked equal to the prefers-color-scheme light block)'],
    cases: [{ theme: 'dark', colorScheme: dark.colorScheme, count: nd, tokens: dark.tokens }, { theme: 'light', colorScheme: light.colorScheme, count: nl, tokens: light.tokens }] };
}

function inspector() {
  const { M, PAN } = C.load();
  const named = C.namedClips();
  const cases = [];
  const K = (fn, input, output, ctx) => { const o = { fn, input }; if (ctx !== undefined) o.ctx = ctx; o.output = output; cases.push(o); };
  for (const b of [0, 1, 4, 4.25, 5.5, 0.125, 15.75, 15.9375, 0.0005, 0.001, 0.00104, -1, NaN, 1e-9, 63.999, 2 / 3, 1 / 3, 100, 7.123456, 0.5 + 1 / 480, 3.99999, 0.99999, 256.25])
    K('fmtPos', b, PAN.fmtPos(b));
  for (const s of ['1.1.1', '1.2.1', '2', '2.3', '2.1.2', '4.4.4.90', '0.1.1', 'abc', '', '1.1.1.479', '1.1.1.480', '  3 . 2 ', '1:2:3', '1 2 3 4', '1.2.3.4.5', '1.0.1', '-1.1', '2,3', '10.4.4.119', '1.5.1'])
    K('parsePos', s, PAN.parsePos(s));
  for (const s of ['0.5', '1.5 beats', '1/8', '1/16', '1/16T', '1/16t', '1/8 dotted', '1 bar', '3/4', '0.1.0.0', '1.0.2.0', '2', 'x', '', '1/4 triplet', '2 b', '.25', '1/0', '0/4', '1.2.3', '1 bars', 'bar', '1/32 trip', '1e3', '-1'])
    K('parseBeats', s, PAN.parseBeats(s));
  for (const s of ['C3', 'c3', 'C#3', 'Db3', 'B2', 'bb2', 'A-2', 'A-1', 'C-2', 'G8', '60', 'F#2', 'H3', '200', 'G#8', 'C♯3', 'E♭4', ' d 4 ', '127', '128', 'Cb-2', 'B#8', '', 'c'])
    K('parsePitch', s, PAN.parsePitch(s));
  for (let p = 0; p < 128; p += 7) K('parsePitch', M.noteName(p), PAN.parsePitch(M.noteName(p)));
  for (const s of ['1/16t', '1/8 dotted', '1 bar', 'bar', '1/4 triplet', '1/32 trip', '1/2 dot', '1/64', 'x', '1bar', '1/8T', '1 bars', '1/16D', '2bar'])
    K('rateId', s, PAN.rateId(s));
  for (const [x, d] of [[1.23456, 3], [2, null], [NaN, 2], [0.1 + 0.2, null], [-0.0004, 3], [1e21, 2], [0.125, 2], [Infinity, 1]])
    K('fmtNum', [x, d], PAN.fmtNum(x, d == null ? undefined : d));
  // typed lane values: every formatLane readout, then typed text
  const sync = { refresh: { mode: 'sync', rate: '1/16' } }, free = { refresh: { mode: 'free', ms: 120 } };
  for (const lane of M.LANE_IDS) {
    const ctxs = lane === 'refresh' ? [sync, free] : lane === 'grain' ? [{}, { engine: 'cloud' }, { engine: 'cloud', lock: true }, { engine: 'tape' }] : lane === 'heads' ? [{}, { heads: 3 }] : [{}];
    for (const inst of ctxs) for (const kind of (lane === 'travel' ? ['speed', 'path'] : ['speed'])) {
      const ctx = { inst, bpm: 96, travelKind: kind };
      for (let i = 0; i <= 10; i++) { const txt = M.formatLane(lane, i / 10, ctx); K('laneValueFromText', [lane, txt], PAN.laneValueFromText(lane, txt, ctx), ctx); }
    }
  }
  const typed = { pitch: ['+3', '-2.5 st', '12', 'abc', '', '−7', '1/2'], amp: ['-6', '-inf', 'off', '+3 dB', 'loud', '0'], vib: ['0.3', '1 st', 'x'], tone: ['2.4 kHz', '800 Hz', 'open', '0', '-5', '20k'],
    formant: ['+6', '-24'], hollow: ['-12', '12 dB', 'deep'], travel: ['2x', '0.5x backwards', 'stopped', 'reverse', 'forwards', '1/8x', '0', '-3', 'fast'],
    heads: ['2.5', '9', '0', 'many'], grain: ['3 cycles', '0', '-1', '200 ms'], refresh: ['Smooth', '1/16', '250 ms', '1.5 beats', '0.5 s', '80', 'never', '0 ms'] };
  for (const lane of Object.keys(typed)) for (const t of typed[lane]) for (const ctx of (lane === 'refresh' ? [{ inst: sync, bpm: 96, travelKind: 'speed' }, { inst: {}, bpm: 96, travelKind: 'speed' }] : lane === 'travel' ? [{ inst: {}, bpm: 96, travelKind: 'speed' }, { inst: {}, bpm: 96, travelKind: 'path' }] : [{ inst: {}, bpm: 96, travelKind: 'speed' }]))
    K('laneValueFromText', [lane, t], PAN.laneValueFromText(lane, t, ctx), ctx);
  // simulate (what head 1 reads, for the note thumbnails) and thumbAmps
  const ms = named['mixed-settings'];
  const INIT = M.applySoundPreset(M.defaultInst(), 'init').inst;
  const sims = {};
  for (const [label, patch] of [['init', {}], ['scatter', { motion: 'scatter', jump: '1/8' }], ['stop', { region: { start: 0.2, end: 0.4, atEnd: 'stop' }, speed: 2 }], ['bounce', { motion: 'bounce', region: { start: 0.1, end: 0.3 } }], ['tape-hold', { engine: 'tape', region: { atEnd: 'hold', start: 0.5, end: 0.6 } }]]) {
    const I = M.normalizeInst(M.mergeInst(INIT, patch));
    for (const n of ms.notes) {
      if (label !== 'init' && !['ms-1', 'ms-4', 'ms-7'].includes(n.id)) continue;
      const T = M.laneTable(n, 'travel', 256), P = M.laneTable(n, 'pitch', 256), N = 48, bpm = 120, secs = 6;
      const r = PAN.simulate(n, I, bpm, secs, T, P, N);
      const out = { pos: r.pos, dir: Array.from(r.dir), sp: r.sp, jump: Array.from(r.jump), N: r.N };
      sims[label + ':' + n.id] = { r, I, n };
      K('simulate', { clip: 'mixed-settings', note: n.id, inst: I, bpm, secs, res: 256, N }, out);
    }
  }
  const bins = PAN.peakBins(starter('sawopen', 1).channels), binsP = PAN.peakBins(starter('pluck', 1).channels);
  K('peakBins', { starter: 'sawopen', seed: 1 }, bins); K('peakBins', { starter: 'pluck', seed: 1 }, binsP);
  for (const [key, st, secs] of [['init:ms-1', 'sawopen', 5.997], ['scatter:ms-7', 'pluck', 6], ['stop:ms-4', 'pluck', 6], ['bounce:ms-1', 'sawopen', 5.997]]) {
    const s = sims[key], regLen = s.I.region.end - s.I.region.start;
    K('thumbAmps', { simulate: key, starter: st, seed: 1, secs, regLen }, PAN.thumbAmps(s.r, st === 'sawopen' ? bins : binsP, secs, regLen));
  }
  for (const [a, t] of [[[0.2, 0.8], 0.5], [[0.2, 0.8], 2], [[0.4], 0.3], [[], 0.3], [[0, 1, 0], 0.75]]) K('tabAt', [Float32Array.from(a), t], PAN.tabAt(Float32Array.from(a), t));
  return { prototype: C.prototypeHashes(['src/sl-ui-panels.js']),
    tolerance: { text: 'strings exact, values within 1e-9', strings: 'exact', values: 1e-9, tablesUlps: 2 },
    notes: ['fn names a pure helper of SLPanels (sl-ui-panels.js); input is its argument (or [lane, text], [x, d]); ctx = the formatCtx it gets ({inst: partial inst, bpm, travelKind})',
      'laneValueFromText output is {v, text} (text = laneText) or {error}; the readout texts come from SLModel.formatLane at v = 0, 0.1, ... 1',
      'simulate input names a note of the mixed-settings clip; T and P are laneTable(note, travel|pitch, res); the output pos (NaN = silent after Stop) and sp are Float32, dir and jump plain lists',
      'thumbAmps input names a simulate case (label:noteId) and the starter whose peakBins it reads; fmtNum(x, null) means the default 3 digits',
      'parsePos/parseBeats/parsePitch null = rejected'],
    clips: { 'mixed-settings': C.clip(ms) },
    cases };
}

function sampleRules() {
  const { M, SUI } = C.load();
  const named = C.namedClips();
  const insts = {}, clips = { 'mixed-settings': C.clip(named['mixed-settings']) }, clipObj = { 'mixed-settings': named['mixed-settings'] };
  const cases = [];
  const DEF = M.defaultInst();
  for (const p of M.SOUND_PRESETS) insts['sound:' + p.id] = M.applySoundPreset(DEF, p.id).inst;
  const demo = M.loadClipPreset('strand-demo');
  for (const R of M.RECIPES) insts['recipe:' + R.n] = M.applyRecipe(demo, insts['sound:init'], R.n, null).inst;
  const I0 = insts['sound:init'];
  const mk = (name, patch) => { insts[name] = M.normalizeInst(M.mergeInst(I0, patch)); return name; };
  const c3 = M.setNoteSample(demo, demo.notes.slice(0, 3).map(n => n.id), { engine: 'cloud' });
  clips['strand-demo+3-cloud'] = C.clip(c3); clipObj['strand-demo+3-cloud'] = c3;
  const getClip = (name, recipe) => (recipe == null && clipObj[name]) || C.clipRef(name, clipObj, recipe);
  const rule = (instName, clipName, recipe) => {
    const I = insts[instName], c = getClip(clipName, recipe), input = { inst: instName, clip: clipName };
    if (recipe != null) input.recipe = recipe;
    cases.push({ fn: 'fades', input, output: SUI.fades(I, c) });
    cases.push({ fn: 'contexts', input, output: SUI.contexts(I, c) });
    cases.push({ fn: 'grainKind', input, output: SUI.grainKind(I, c) });
    cases.push({ fn: 'headsMulti', input, output: SUI.headsMulti(I, c) });
    cases.push({ fn: 'toneAllOpen', input, output: SUI.toneAllOpen(I, c) });
    cases.push({ fn: 'randomInUse', input, output: SUI.randomInUse(I, c) });
  };
  for (const p of M.SOUND_PRESETS) for (const cl of ['preset:strand-demo', 'preset:scatter-18t', 'preset:tape-stop-lead', 'mixed-settings']) rule('sound:' + p.id, cl);
  for (const R of M.RECIPES) rule('recipe:' + R.n, 'preset:strand-demo', R.n);
  rule(mk('tape', { engine: 'tape' }), 'strand-demo+3-cloud');
  rule(mk('heads-macro-up', { macros: [0.5, 0.5, 0.6, 0.5, 0.5, 0.5, 0.5, 0.5] }), 'preset:strand-demo');
  rule(mk('mod-heads', { mod1: { target: 'heads' } }), 'preset:pad-swell');
  rule(mk('free-skip', { refresh: { mode: 'free', skip: 0.3 } }), 'preset:pad-swell');
  rule(mk('tone-up', { macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.9, 0.5] }), 'preset:walk-through');
  rule(mk('tone-up-dark', { macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.9, 0.5], offsets: { tone: -0.1 } }), 'preset:walk-through');
  rule(mk('cloud-spray-drift', { engine: 'cloud', spray: 0.2, drift: 0.4, motion: 'freeze' }), 'preset:bloom');
  rule(mk('lock', { engine: 'cloud', lock: true }), 'preset:hollow-drone');
  for (const [e, l] of [['tape', false], ['voice', false], ['cloud', false], ['cloud', true], ['voice', true], ['tape', true]]) cases.push({ fn: 'kindOf', input: [e, l], output: SUI.kindOf(e, l) });
  for (const info of [{ pitch: true, seconds: 5, parts: [] }, { pitch: false, seconds: 10, parts: [{ kind: 'loop', tempoFound: true }] }, { pitch: false, seconds: 30, parts: [{ kind: 'loop', tempoFound: true }] },
    { pitch: true, seconds: 25 }, null, { pitch: false }, { pitch: false, seconds: 20, parts: [{ kind: 'loop', tempoFound: false }] }, { pitch: false, seconds: 20, parts: [null, { kind: 'loop', tempoFound: true }] }])
    cases.push({ fn: 'suggestEngine', input: info, output: SUI.suggestEngine(info) });
  const pk = (kind, start, end) => ({ kind, label: kind, start, end });
  for (const parts of [[pk('hit', 0.1, 0.2), pk('quiet', 0.3, 0.4), pk('loop', 0.5, 0.7), pk('steady', 0.8, 0.9)], [pk('hit', 0, 0.1), pk('quiet', 0.2, 0.3)], [pk('steady', 0.5, 0.5), pk('hit', 0.1, 0.2)],
    [], null, [null, pk('other', 0.1, 0.4)], [pk('loop', 0.6, 0.2), pk('loop', 0.1, 0.3)]])
    cases.push({ fn: 'pickPart', input: parts, output: SUI.pickPart(parts) });
  // M6 T1: the plan's whole product, every sound preset x recipe (none, 1-8) x clip preset (864 pairs), one digest each
  cases.push({ fn: 'productClips', input: null, output: M.CLIP_PRESETS.map(cp => cp.id) });
  for (const p of M.SOUND_PRESETS) for (const n of [0, ...M.RECIPES.map(R => R.n)]) {
    const output = M.CLIP_PRESETS.map(cp => {
      let I = insts['sound:' + p.id], c = M.loadClipPreset(cp.id);
      if (n) { const r = M.applyRecipe(c, I, n, null); I = r.inst; c = r.clip; }
      return fnvText(productText(SUI.fades(I, c), SUI.contexts(I, c), SUI.grainKind(I, c)));
    }).join(' ');
    cases.push({ fn: 'product', input: [p.id, n], output });
  }
  return { prototype: C.prototypeHashes(['src/sl-ui-sample.js']),
    tolerance: { text: 'exact', default: 'exact' },
    notes: ['input.inst names an entry of insts (sound:<id> = applySoundPreset(defaultInst(), id).inst, recipe:<n> = applyRecipe(Strand Demo, sound:init, n).inst, the rest are sound:init with a patch); input.clip names a clip ("preset:<id>" = loadClipPreset)',
      'input.recipe: n = the clip after applyRecipe(clip, inst, n, every note)',
      'fades/contexts/grainKind/headsMulti/toneAllOpen/randomInUse are SLSampleUI\'s pure part (sl-ui-sample.js) with the same (inst, clip)',
      'the plan asks for every sound preset x recipe x clip preset; that product (864 fades) is far over the size budget, so the cases are every sound preset x 4 clips, every recipe on Strand Demo and 8 targeted cases',
      'product (M6 T1): the whole product as digests. Input [sound preset id, recipe n]: inst = sound:<id>, and with a recipe (n > 0) inst and clip are applyRecipe(clip, inst, n, every note); output: one digest per clip preset in productClips\' order, space-separated, each FNV-1a 32 over the UTF-8 bytes of the text as 8 lowercase hex digits',
      'product text: per fades rule in order "<id> <1|0 faded> <notes> <reason>\\n"; per context "<who> <count> <kind> <engine> <motion> <travel> <from: lock motion tape rev jump as 0/1, no spaces>\\n"; then "grainKind <kind|null>\\n"'],
    clips, insts, cases };
}

// The text a product digest is taken over (sampleRules' notes say how it is laid out).
function productText(F, cx, gk) {
  let t = '';
  for (const id of Object.keys(F)) t += id + ' ' + (F[id].faded ? 1 : 0) + ' ' + F[id].notes + ' ' + F[id].reason + '\n';
  for (const c of cx) t += [c.who, c.count, c.kind, c.engine, c.motion, c.travel, ['lock', 'motion', 'tape', 'rev', 'jump'].map(k => (c.from[k] ? 1 : 0)).join('')].join(' ') + '\n';
  return t + 'grainKind ' + (gk === null ? 'null' : gk) + '\n';
}
function fnvText(text) {
  const b = Buffer.from(text, 'utf8');
  let h = 0x811c9dc5;
  for (let i = 0; i < b.length; i++) { h ^= b[i]; h = Math.imul(h, 0x01000193); }
  return (h >>> 0).toString(16).padStart(8, '0');
}

// M6 T1: what the Sample and Macros tabs' controls do, from the real sl-ui-sample.js running under the prototype tests'
// own minimal DOM and fake SLApp (prototype-v4/tests/test-ui-sample.js boot (), word for word): a drag, a typed value
// and a double-click on every knob, the knobs' readouts, and every control's faded state, tooltip and hint line in a
// few settings on Strand Demo.
function sampleControls() {
  const { M, SND, AN } = C.load();
  const H = require(path.join(C.TESTS, 'ui-helpers', 'minidom.js'));
  const boot = new Function('fs', 'vm', 'M', 'SND', 'AN', 'makeDocument', 'Option', 'SRC',
    '"use strict";\n' + C.testFunctionSource('test-ui-sample.js', 'boot') + '\nreturn boot;')(fs, require('vm'), M, SND, AN, H.makeDocument, H.Option, C.SRC);
  const drag = new Function('dispatch', '"use strict";\n' + C.testFunctionSource('test-ui-sample.js', 'drag') + '\nreturn drag;')(H.dispatch);
  const T = boot();
  const { doc, S, App, flush } = T;
  const box = id => doc.getElementById(id);
  const dialsIn = id => H.findAll(box(id), n => n.tagName === 'SVG' && n.classList.contains('sls-dial'));
  const valOf = dial => dial.parentNode.childNodes[2];

  // the knobs, each with the inst field it edits (the Grain knob's depends on the engine)
  const OFFSETS = ['amp', 'vib', 'tone', 'formant', 'hollow'], OFFSET_NAMES = ['Volume', 'Vibrato', 'Tone', 'Formant', 'Hollow'];
  const GRAIN = { voice: 'grainVoice', cloud: 'grainCloud', lock: 'grainLock', tape: 'grainVoice' };
  const knobs = [
    ['Speed', I => I.speed, v => ({ speed: v })], ['Heads', I => I.heads, v => ({ heads: v })], ['Spread', I => I.spread, v => ({ spread: v })],
    ['Grain', null, null], ['Spray', I => I.spray, v => ({ spray: v })], ['Drift', I => I.drift, v => ({ drift: v })], ['Pitch', I => I.pitch, v => ({ pitch: v })],
    ['Refresh time', I => I.refresh.ms, v => ({ refresh: { ms: v } })], ['Skip', I => I.refresh.skip, v => ({ refresh: { skip: v } })],
    ...M.MACRO_NAMES.map((name, i) => ['Macro ' + name, I => I.macros[i], (v, I) => ({ macros: I.macros.map((x, k) => (k === i ? v : x)) })]),
    ...OFFSETS.map((k, j) => ['Offset ' + OFFSET_NAMES[j], I => I.offsets[k], v => ({ offsets: { [k]: v } })])];
  const dials = [...dialsIn('sampleTab'), ...dialsIn('macrosBox')];
  const shown = dials.map(d => d.getAttribute('aria-label'));
  const want = knobs.map(k => k[0].replace(/^(Macro|Offset) /, ''));
  if (JSON.stringify(shown) !== JSON.stringify(want)) throw new Error('make-fixtures: the Sample and Macros tabs\' knobs are not ' + want.join(', ') + ' but ' + shown.join(', '));

  // the settings: sound:init on Strand Demo, plus patches; each is in insts in full
  const I0 = S.inst, insts = {};
  const CTX = { voice: {}, cloud: { engine: 'cloud' }, lock: { engine: 'cloud', lock: true }, tape: { engine: 'tape' },
    busy: { engine: 'cloud', lock: true, motion: 'scatter', heads: 2.5, refresh: { mode: 'free', skip: 0.25 }, spray: 0.2, drift: 0.3, macros: [0.75, 0.5, 0.5, 0.5, 0.5, 0.5, 0.95, 0.5], offsets: { tone: 0.2 } },
    still: { engine: 'tape', motion: 'bounce', heads: 3, refresh: { mode: 'sync' }, headTune: 'fifths', tape: true, rev: true, region: { atEnd: 'hold' } } };
  for (const k of Object.keys(CTX)) insts[k] = M.normalizeInst(M.mergeInst(I0, CTX[k]));
  const getter = (k, ctx) => (knobs[k][1] || (I => I[GRAIN[ctx]]));
  const patcher = (k, ctx) => (knobs[k][2] || (v => ({ [GRAIN[ctx]]: v })));
  const setUp = (ctx, k, from) => {
    let I = insts[ctx];
    if (from != null) I = M.mergeInst(I, patcher(k, ctx)(from, I));
    App.setInst(I, null);
    flush();
  };
  const cases = [];
  const R = (fn, input, output) => cases.push({ fn, input, output });
  const result = (k, ctx, n0, t0) => {
    const o = [getter(k, ctx)(S.inst), valOf(dials[k]).textContent, S.pushes.length - n0, S.pushes.length > n0 ? S.pushes[n0] : null];
    if (S.toasts.length > t0) o.push(S.toasts[S.toasts.length - 1]);
    return o;
  };
  // where each knob starts: its default (or for Grain, the engine's grain default), and one more value (binary32-exact)
  const FROM = { Speed: [1, 0.25], Heads: [1, 4.5], Spread: [0.25, 0.75], Spray: [0, 0.5], Drift: [0, 0.375], Pitch: [0, -12], 'Refresh time': [120, 750], Skip: [0, 0.5] };
  const GRAIN_FROM = { voice: [2, 0.5], cloud: [140, 20], lock: [4, 11], tape: [2] };
  const DRAGS = [[1, false], [3, false], [17, false], [85, false], [-9, false], [-60, false], [5, true], [-23, true]];
  const TYPED = ['0.5', '4.3', '-3', 'abc', '2 s', '99999'];
  for (let k = 0; k < knobs.length; k++) {
    const name = knobs[k][0], grain = name === 'Grain';
    const ctxs = grain ? ['voice', 'cloud', 'lock', 'tape'] : ['voice'];
    for (const ctx of ctxs) {
      const froms = grain ? GRAIN_FROM[ctx] : (FROM[name] || [k < 17 ? 0.75 : -0.5]);
      for (const from of froms) {
        for (const [dy, shift] of DRAGS) {
          setUp(ctx, k, from);
          const n0 = S.pushes.length, t0 = S.toasts.length;
          drag(dials[k], dy, shift);
          flush();
          R('knobDrag', [name, ctx, from, dy, shift], result(k, ctx, n0, t0));
        }
        setUp(ctx, k, from);
        const n0 = S.pushes.length, t0 = S.toasts.length;
        H.dispatch(dials[k], 'dblclick', {});
        flush();
        R('knobReset', [name, ctx, from], result(k, ctx, n0, t0));
      }
      // typing (the macros and offsets take no typing: clicking their value does nothing)
      for (const text of (k < 9 ? TYPED : k === 9 || k === 17 ? ['0.7'] : [])) {
        setUp(ctx, k, null);
        const n0 = S.pushes.length, t0 = S.toasts.length;
        valOf(dials[k]).click();
        const inp = H.findAll(doc.body, n => n.tagName === 'INPUT' && n.className === 'sls-kin')[0];
        if (inp) { inp.value = text; H.dispatch(inp, 'keydown', { key: 'Enter' }); }
        flush();
        R('knobType', [name, ctx, text], [...result(k, ctx, n0, t0), !!inp]);
      }
    }
  }

  // every control's faded state, tooltip and hint line, in each setting
  const tab = box('sampleTab'), mac = box('macrosBox');
  const sugLine = tab.firstChild;
  const fadedIn = (n, top) => { for (let x = n; x && x !== top.parentNode; x = x.parentNode) if (x.classList && x.classList.contains('sls-faded')) return true; return false; };
  const controls = () => {
    const out = [];
    const segLabel = b => { const seg = b.parentNode; return seg.classList.contains('sls-seg') ? seg.parentNode.firstChild.textContent : null; };
    for (const b of H.findAll(tab, n => n.tagName === 'BUTTON' && !n.classList.contains('sls-pin') && !sugLine.contains(n))) {
      const g = segLabel(b);
      out.push([(g ? g + ': ' : '') + b.textContent, b, b, tab]);
    }
    for (const s of H.findAll(tab, n => n.tagName === 'SELECT')) out.push([s.getAttribute('aria-label'), s.parentNode, s.parentNode, tab]);
    for (let k = 0; k < dials.length; k++) out.push([knobs[k][0], dials[k].parentNode, dials[k].parentNode, k < 9 ? tab : mac]);
    for (const b of H.findAll(mac, n => n.tagName === 'BUTTON')) out.push([b.textContent, b, null, mac]);
    return out;
  };
  let first = null;
  for (const ctx of ['voice', 'cloud', 'busy', 'still']) {
    setUp(ctx, 0, null);
    App.setInst(S.inst, 'x');   // a finished change syncs every control at once
    flush();
    const out = {};
    for (const [name, node, hover, top] of controls()) {
      if (out[name]) throw new Error('make-fixtures: two controls named ' + name);
      let hint = null;
      if (hover) { H.dispatch(hover, 'mouseenter', {}); hint = S.hint; H.dispatch(hover, 'mouseleave', {}); }
      const knob = node.classList.contains('sls-knob');
      const o = [fadedIn(node, top) ? 1 : 0, knob && node.classList.contains('sls-off') ? 1 : 0, node.title, hint, knob ? node.childNodes[2].textContent : null];
      // after the first setting, a tooltip or hint the same as the first setting's is written as 0
      if (first) { if (o[2] === first[name][2]) o[2] = 0; if (o[3] === first[name][3] && hint !== null) o[3] = 0; }
      out[name] = o;
    }
    if (!first) first = out;
    R('controls', ctx, out);
  }
  return { insts, cases };
}

function readouts() {
  const { SUI } = C.load();
  const F = C.sampleUiInternals();
  const cases = [];
  const R = (fn, input, output) => cases.push({ fn, input, output });
  for (let i = 0; i < 8; i++) { for (let j = 0; j <= 40; j++) R('macroText', [i, j / 40], SUI.macroText(i, j / 40)); for (const m of [0.5025, 0.4975, 0.5024, NaN]) R('macroText', [i, m], SUI.macroText(i, m)); }
  R('macroText', [9, 0.7], SUI.macroText(9, 0.7));
  for (const k of ['amp', 'vib', 'tone', 'formant', 'hollow']) { for (let j = 0; j <= 40; j++) { const o = -1 + j / 20; R('offsetText', [k, o], SUI.offsetText(k, o)); } for (const o of [0.0025, -0.0024, NaN, 0.995, -0.995]) R('offsetText', [k, o], SUI.offsetText(k, o)); }
  R('offsetText', ['other', 0.4], SUI.offsetText('other', 0.4));
  for (const v of [0, -1, 0.125, 0.5, 1, 1.5, 2, 3.33333, 16, 0.0625, 10, 100.5, 0.999, 1.0049, 1.005, 99.95]) R('fmtX', v, F.fmtX(v));
  for (const v of [0, 0.25, 0.333, 0.995, 1, 0.005, 0.0049, -0.1, 1.5]) R('fmtPct', v, F.fmtPct(v));
  for (const v of [0, 0.004, -0.006, 1, -1, 12, 0.5, -24, 7.125, 1.005, 0.005, -0.005, 2.999]) R('fmtSt', v, F.fmtSt(v));
  for (const v of [1, 2, 0.25, 1.005, 16, 2.5, 0.999, 1.004]) R('fmtCycles', v, F.fmtCycles(v));
  for (const v of [5, 120, 999.4, 999.6, 1000, 1500, 2000, 12.5, 1234.5, 1995]) R('fmtMs', v, F.fmtMs(v));
  for (const v of [1, 1.004, 2, 2.5, 8, 3.333, 1.005, 0.999]) R('fmtHeads', v, F.fmtHeads(v));
  for (const [x, d] of [[0.5, 0], [-0.5, 0], [12.345, 1], [-0.04, 1], [0, 2], [1, 2], [-1.005, 2], [0.000001, 3]]) R('signed', [x, d], F.signed(x, d));
  for (const s of ['0.5', '1/8', '-3', '+2.5x', ',5', '1,5', 'abc', '', null, '120 ms', '1.5 s', '2s', '25%', '1 / 4', '.5', '3/0', '  7 ', '1e3', '-.25', '2.5 seconds', '500ms', '0.25 S']) {
    R('parseNum', s, SUI.parseNum(s)); R('parseMs', s, SUI.parseMs(s)); R('parsePct', s, SUI.parsePct(s));
  }
  const KM = SUI.KNOB_MATH;
  const SPECS = { speed: { min: 0.125, max: 8, log: true, def: 1 }, heads: { min: 1, max: 8, def: 1 }, cloud: { min: 5, max: 2000, log: true, def: 140 }, pitch: { min: -24, max: 24, def: 0 }, spread: { min: 0, max: 1, def: 0.25 } };
  for (const name of Object.keys(SPECS)) {
    const s = SPECS[name];
    for (const v of [s.min, s.max, s.def, (s.min + s.max) / 2, s.min - 1, s.max * 2, NaN, s.log ? s.min * 1.7 : s.min + (s.max - s.min) * 0.3]) R('toNorm', [s, v], KM.toNorm(s, v));
    for (const nn of [0, 0.25, 0.5, 0.75, 1, -0.2, 1.3]) R('fromNorm', [s, nn], KM.fromNorm(s, nn));
  }
  const qL = [[[0.25, 0.5, 1, 2, 4], undefined], [[0.5, 1, 2, 4, 8], undefined], [[], 2]];
  for (const [marks, digits] of qL) for (const v of [0.26, 0.5123, 1.04, 1.07, 3.333, 7.9, 0.123456]) for (const fine of [false, true])
    R('qLog', [marks, digits === undefined ? null : digits, v, fine], KM.qLog(marks, digits)(v, fine));
  const qN = [[0.05, 0.01, [1, 2, 3, 4, 5, 6, 7, 8], 0.08], [0.01, 0.001, null, null], [1, 0.01, null, null], [1, 0.5, null, null]];
  for (const [st, fst, marks, tol] of qN) for (const v of [1.04, 1.93, 2.5, 0.123456, 7.97, -3.3, 0.3333]) for (const fine of [false, true])
    R('qLin', [st, fst, marks, tol, v, fine], KM.qLin(st, fst, marks || undefined, tol == null ? undefined : tol)(v, fine));
  for (const [v, marks, tol] of [[1.03, [1, 2], 0.06], [1.5, [1, 2], 0.06], [190, [20, 50, 100, 140, 200, 500, 1000], 0.04], [205, [200], 0.04]]) R('magnetLog', [v, marks, tol], KM.magnetLog(v, marks, tol));
  const controls = sampleControls();
  for (const c of controls.cases) cases.push(c);
  return { prototype: C.prototypeHashes(['src/sl-ui-sample.js', 'tests/test-ui-sample.js', 'tests/ui-helpers/minidom.js']),
    tolerance: { text: 'exact', default: 'exact' },
    notes: ['macroText, offsetText, parseNum/parseMs/parsePct and KNOB_MATH are exported by SLSampleUI; fmtX, fmtPct, fmtSt, fmtCycles, fmtMs, fmtHeads and signed are private and read from an in-memory copy of sl-ui-sample.js with those names added to its pure export',
      'qLog input [marks, digits|null, v, fine] = qLog(marks, digits)(v, fine); qLin input [step, fineStep, marks|null, tol|null, v, fine]; toNorm/fromNorm input [knob spec, value]; the specs are the Sample tab\'s (Speed, Heads, Cloud grain, Pitch, Spread)',
      'M6 T1: knobDrag, knobReset, knobType and controls come from the real sl-ui-sample.js run under prototype-v4/tests/test-ui-sample.js\'s own boot () (its minimal DOM and fake SLApp) on Strand Demo, the instrument insts[ctx]',
      'a knob is named by its undo label (Speed ... Skip on the Sample tab, "Macro Speed" ..., "Offset Volume" ...); Grain edits grainVoice, grainCloud or grainLock as the engine asks (ctx voice, cloud, lock; in tape it does nothing)',
      'knobDrag input [knob, ctx, from, dy, shift]: the knob set to from, then the test file\'s drag (dial, dy, shift): pointer down at y 300, five moves to y 300 - dy k / 5 with Shift, up; knobReset input [knob, ctx, from]: a double-click; knobType input [knob, ctx, text]: a click on the value, text typed, Enter',
      'their output [value, readout, undo steps, the first step\'s label or null, the toast when one came]; knobType adds whether a text box opened',
      'controls input ctx, output {control: [faded 1|0 (it or a group around it is), off 1|0 (a knob with nothing to do), tooltip (title), hint line on hover (null: none), knob readout or null]}; a control is named by its text ("Engine: Tape" inside a group), a drop-down by its label, a knob as above; after the first case a tooltip or hint equal to the first case\'s is written as 0'],
    insts: controls.insts,
    cases };
}

module.exports = function ui() {
  return { 'ui/theme.json': theme(), 'ui/inspector.json': inspector(), 'ui/sample-rules.json': sampleRules(), 'ui/readouts.json': readouts() };
};
