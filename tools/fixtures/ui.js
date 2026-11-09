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
  return { prototype: C.prototypeHashes(['src/sl-ui-sample.js']),
    tolerance: { text: 'exact', default: 'exact' },
    notes: ['input.inst names an entry of insts (sound:<id> = applySoundPreset(defaultInst(), id).inst, recipe:<n> = applyRecipe(Strand Demo, sound:init, n).inst, the rest are sound:init with a patch); input.clip names a clip ("preset:<id>" = loadClipPreset)',
      'input.recipe: n = the clip after applyRecipe(clip, inst, n, every note)',
      'fades/contexts/grainKind/headsMulti/toneAllOpen/randomInUse are SLSampleUI\'s pure part (sl-ui-sample.js) with the same (inst, clip)',
      'the plan asks for every sound preset x recipe x clip preset; that product (864 fades) is far over the size budget, so the cases are every sound preset x 4 clips, every recipe on Strand Demo and 8 targeted cases'],
    clips, insts, cases };
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
  return { prototype: C.prototypeHashes(['src/sl-ui-sample.js']),
    tolerance: { text: 'exact', default: 'exact' },
    notes: ['macroText, offsetText, parseNum/parseMs/parsePct and KNOB_MATH are exported by SLSampleUI; fmtX, fmtPct, fmtSt, fmtCycles, fmtMs, fmtHeads and signed are private and read from an in-memory copy of sl-ui-sample.js with those names added to its pure export',
      'qLog input [marks, digits|null, v, fine] = qLog(marks, digits)(v, fine); qLin input [step, fineStep, marks|null, tol|null, v, fine]; toNorm/fromNorm input [knob spec, value]; the specs are the Sample tab\'s (Speed, Heads, Cloud grain, Pitch, Spread)'],
    cases };
}

module.exports = function ui() {
  return { 'ui/theme.json': theme(), 'ui/inspector.json': inspector(), 'ui/sample-rules.json': sampleRules(), 'ui/readouts.json': readouts() };
};
