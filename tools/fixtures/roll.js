// tools/fixtures/roll.js: Tests/fixtures/roll/geometry.json and gestures.json (PLUGIN-PLAN 4 M4, 7.6 R1): the roll's
// geometry and its gestures, from the prototype's own page code. sl-ui.js runs here unmodified in a stub DOM (no
// canvas is ever drawn): its roll geometry functions are read from an in-memory copy with one name list added to
// its API, and its pointer handlers (rollDown, rollMove, rollUp, the context menu) are driven with made-up events.
// Each gesture case records, after every step, the clip, the selection, the undo steps, the toast and the readout.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const C = require('./common.js');

// ------------------------------------------------------------------------------------------- the page
// An element that accepts anything: every property read gives another such element (a number 0 or an empty string
// where a primitive is needed), every call returns one, and what is written is kept.
function universal() {
  const store = new Map();
  return new Proxy(function () {}, {
    get(t, k) {
      if (store.has(k)) return store.get(k);
      if (k === Symbol.toPrimitive) return hint => (hint === 'string' ? '' : 0);
      if (k === Symbol.iterator) return function* () {};
      if (k === 'then' || k === 'toJSON') return undefined;
      if (k === 'length') return 0;
      if (k === 'textContent' || k === 'innerHTML' || k === 'value' || k === 'className' || k === 'title') return '';
      const v = universal(); store.set(k, v); return v;
    },
    set(t, k, v) { store.set(k, v); return true; },
    apply() { return universal(); },
    construct() { return universal(); },
  });
}

// the source with one change made exactly once (common.js keeps its own injectOnce private)
function injectOnce(text, re, replacement, what) {
  const m = text.match(new RegExp(re.source, 'g'));
  if (!m || m.length !== 1) throw new Error('make-fixtures: roll: could not find the ' + what + ' anchor exactly once (found ' + (m ? m.length : 0) + ')');
  return text.replace(re, replacement);
}

let pageText = null;
function source() {
  if (pageText) return pageText;
  let t = fs.readFileSync(path.join(C.SRC, 'sl-ui.js'), 'utf8');
  t = injectOnce(t, /const api = \{\n/, 'const api = {\n    __roll: { cy, pitchAt, geom, glides, baseYAt, valueY, laneValue, nodeXY, segXY, glideHandles, noteBox, hitNote, hitNode, hitSeg, onNoteLine, handleNotes, ' +
    'app, setZoom: (bw, rh) => { BW = bw; RH = rh; } },\n', 'SLApp api');
  t = injectOnce(t, /function openMenu\(cx, cyy, items\) \{\n/, 'function openMenu(cx, cyy, items) {\n    if (window.__menuHook) window.__menuHook(items);\n', 'openMenu');
  pageText = t;
  return t;
}

// A fresh page: sl-ui.js after init (Strand Demo, the init sound preset, Build, grid 1/16, 96 bpm, BW 64, RH 14).
function page() {
  const { M, SND, AN } = C.load();
  const els = new Map(), listeners = [];
  const listen = target => (type, fn) => listeners.push({ target, type, fn });
  const scroller = { scrollLeft: 0, scrollTop: 0, clientWidth: 1e6, clientHeight: 1e6, style: {},
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 1e6, height: 1e6 }), addEventListener: listen('scroller'),
    setPointerCapture() {}, removeAttribute() {}, blur() {} };
  const $ = id => { if (id === 'scroller') return scroller; if (!els.has(id)) els.set(id, universal()); return els.get(id); };
  let now = 1e6;
  const RealDate = Date;
  class PageDate extends RealDate { static now() { return now; } }
  const doc = universal();
  const document = new Proxy({}, { get(t, k) { if (k === 'getElementById') return $; if (k === 'addEventListener') return listen('document'); if (k === 'activeElement') return null; return doc[k]; } });
  let menu = null;
  const window = { addEventListener: listen('window'), SLModel: M, SLSounds: SND, SLAnalysis: AN, document, innerWidth: 1100, innerHeight: 680, devicePixelRatio: 1, scrollY: 0,
    __menuHook: items => { menu = items; } };
  const ctx = vm.createContext({ window, document, console, Date: PageDate, Math, JSON, Object, Array, Set, Map, WeakMap, Float32Array, Int8Array, Uint8Array, Int32Array,
    Promise, Error, Number, String, Boolean, Symbol, RegExp, isFinite, parseFloat, parseInt, Infinity, NaN, navigator: {},
    setTimeout: () => 0, clearTimeout: () => {}, setInterval: () => 0, clearInterval: () => {}, requestAnimationFrame: () => 0,
    getComputedStyle: () => ({ getPropertyValue: () => '' }), performance: { now: () => now },
    Option: function (t, v) { this.text = t; this.value = v; }, Blob: function () {}, URL: { createObjectURL: () => '' },
    createStrandEngineModule: function () {}, strandWorkletGlue: function () {} });
  vm.runInContext(source() + '\n;this.SLApp = SLApp;\n', ctx, { filename: 'sl-ui.js' });
  const A = ctx.SLApp, R = A.__roll;
  const handler = (target, type, name) => listeners.find(l => l.target === target && l.type === type && (!name || l.fn.name === name)).fn;
  const rollDown = handler('scroller', 'pointerdown', 'rollDown'), rollMove = handler('scroller', 'pointermove', 'rollMove');
  const rollUp = handler('scroller', 'pointerup', 'rollUp'), context = handler('scroller', 'contextmenu');
  const event = (x, y, mods) => Object.assign({ clientX: x, clientY: y, button: 0, pointerType: 'mouse', pointerId: 1, shiftKey: false, ctrlKey: false,
    metaKey: false, altKey: false, preventDefault() {}, stopPropagation() {} }, mods || {});
  return {
    A, R, M, $,
    advance(ms) { now += ms; },
    down(x, y, mods) { rollDown(event(x, y, mods)); },
    move(x, y, mods) { rollMove(event(x, y, mods)); },
    up(x, y) { rollUp(event(x, y)); },
    contextMenu(x, y) { menu = null; context(event(x, y, { button: 2 })); return menu; },
  };
}

// ------------------------------------------------------------------------------------------- geometry
const INIT = () => { const { M } = C.load(); return M.applySoundPreset(M.defaultInst(), 'init').inst; };

function noteGeometry(P, n, lanes) {
  const { R, M } = P;
  const G = R.geom(n), app = R.app;
  const ts = [0, 0.13, 0.5, 0.77, 1, -0.2, 1.4];
  const at = [0, G.K >> 2, G.K >> 1, G.K - 2, G.K - 1];
  const out = { K: G.K, base: at.map(i => [i, G.base[i]]), vib: at.map(i => [i, G.vib[i]]), smin: G.smin, smax: G.smax,
    glide: G.glide ? { d: G.glide.d, len: G.glide.len, curve: G.glide.curve } : null,
    baseYAt: ts.map(t => [t, R.baseYAt(n, t)]),
    box: R.noteBox(n, false), boxRibbon: R.noteBox(n, true),
    glideHandles: R.glideHandles(n), lanes: {} };
  for (const L of lanes) {
    const nd = n.c[L];
    const o = { nodes: nd.map((q, k) => R.nodeXY(n, L, k)), segs: nd.slice(0, -1).map((q, k) => R.segXY(n, L, k)), valueY: [], laneValue: [] };
    for (const [t, v] of [[0, 0], [0.37, 0.42], [1, 1]]) o.valueY.push([t, v, R.valueY(n, L, t, v)]);
    const y0 = R.baseYAt(n, 0.5);
    for (const dy of [-50, -13.5, 7]) o.laneValue.push([0.5, y0 + dy, R.laneValue(n, L, 0.5, y0 + dy)]);
    out.lanes[L] = o;
  }
  void app; void M;
  return out;
}

function geometry() {
  const { M } = C.load();
  const named = C.namedClips();
  const clips = { 'preset:strand-demo': M.loadClipPreset('strand-demo'), 'voice-lead': named['voice-lead'], 'chop-src': named['chop-src'], gens: named.gens, shapes4: named.shapes4 };
  const zooms = [[64, 14], [83.2, 18], [12, 8], [400, 30]];
  const cases = [];
  const P = page();
  const { A, R } = P;
  for (const [BW, RH] of zooms) {
    R.setZoom(BW, RH);
    cases.push({ fn: 'rows', input: { BW, RH }, output: { laneH: A.geometry().laneH, cy: [24, 60, 61.4, 96].map(p => [p, R.cy(p)]),
      pitchAt: [-5, 0, 13.9, 14, RH * 36.5, RH * 72.99, RH * 73, 5000].map(y => [y, R.pitchAt(y)]) } });
  }
  const pick = { 'preset:strand-demo': ['sd-1', 'sd-9', 'sd-20'], 'voice-lead': ['vl-a1', 'vl-d3', 'vl-f2'], 'chop-src': ['cs-b', 'cs-d'], gens: ['gn-4'], shapes4: ['s4-c', 's4-d'] };
  const insts = { init: C.plain(INIT()), always: C.plain(M.normalizeInst(M.mergeInst(INIT(), { glideMode: 'always', glideTime: 0.75, vibRate: 7.25 }))) };
  for (const [instName, bpm] of [['init', 96], ['always', 133]]) {
    A.setInst(insts[instName], null);
    R.app.bpm = bpm;
    for (const [BW, RH] of zooms.slice(0, instName === 'init' ? 2 : 1)) {
      R.setZoom(BW, RH);
      for (const clipName of Object.keys(pick)) {
        A.setClip(clips[clipName], null);
        for (const id of pick[clipName]) {
          if (instName !== 'init' && !['sd-9', 'vl-a1', 'vl-d3', 'cs-b'].includes(id)) continue;
          const n = A.noteById(id);
          if (!n) throw new Error('make-fixtures: roll geometry: no note ' + id);
          cases.push({ fn: 'note', input: { clip: clipName, id, BW, RH, bpm, inst: instName }, output: noteGeometry(P, n, ['pitch', 'travel']) });
        }
      }
    }
  }
  // hit tests: a grid of points over Strand Demo's first two bars, with a selection, in Build and in Pitch and Volume
  A.setInst(insts.init, null); R.app.bpm = 96; R.setZoom(64, 14);
  A.setClip(clips['preset:strand-demo'], null);
  for (const [mode, sel, primary] of [[1, [], null], [1, ['sd-1', 'sd-5'], 'sd-5'], [2, ['sd-1', 'sd-2', 'sd-5'], 'sd-1'], [3, ['sd-5'], 'sd-5']]) {
    A.setMode(mode); A.setSelection(sel, primary);
    const AL = A.activeLane();
    const pts = [];
    for (let x = 2; x < 8 * 64; x += 43) for (let y = 380; y < 760; y += 23) pts.push([x, y]);
    const out = { note: [], line: [], node: [], seg: [] };
    for (const [x, y] of pts) {
      const h = R.hitNote(x, y);
      out.note.push(h ? h.id : null);
      out.line.push(h ? R.onNoteLine(h, x, y) : null);
      if (AL) {
        const hn = R.hitNode(x, y, AL);
        out.node.push(hn ? [hn.n.id, hn.k] : null);
        const P0 = A.primaryNote();
        out.seg.push(P0 ? R.hitSeg(P0, x, y, AL) : -1);
      }
    }
    cases.push({ fn: 'hit', input: { clip: 'preset:strand-demo', mode, sel, primary, BW: 64, RH: 14, bpm: 96, inst: 'init', points: pts }, output: out });
  }
  return { prototype: C.prototypeHashes(['src/sl-ui.js']),
    tolerance: { default: 'values within 1e-9 (absolute, pixels and lane values); base and vib (Float32 tables, [index, value] samples) within 2 Float32 ulps; ids and indices exact' },
    notes: ['geometry of sl-ui.js (SLApp) read from its own functions: cy, pitchAt, geom (K, base, vib, smin, smax, glide), baseYAt, noteBox, nodeXY, segXY, glideHandles, valueY, laneValue, hitNote, onNoteLine, hitNode, hitSeg',
      'input.clip names a clip: preset:<id> = loadClipPreset, others are in "clips"; input.inst names an entry of "insts"; bpm is the page tempo',
      'segs: null where the segment is too short for a diamond (under 20 px); glideHandles null when the note has no glide',
      'hit: points [x, y] in roll pixels; note = hitNote id; line = onNoteLine for that note; node = hitNode [id, k] in the active lane; seg = hitSeg on the primary note (-1 = none)'],
    clips: { 'voice-lead': C.clip(named['voice-lead']), 'chop-src': C.clip(named['chop-src']), gens: C.clip(named.gens), shapes4: C.clip(named.shapes4) },
    insts, cases };
}

// ------------------------------------------------------------------------------------------- gestures
// One case: a fresh page, a clip, a setup, then steps. The state after the setup is the case's start; each step lists
// only what it changed: the notes that differ from before the step (changed) and the ids that went (removed), the
// note order, the undo steps since the setup (hist, and label the newest), the selection (time order) and primary,
// the active node, the readout (null = hidden), the toast shown in that step (null = none) and Slide into's arming.
// What changed in a note (C.note shapes): { id, set: { field: value } }, where set.c and set.gen hold only the lanes that
// changed (null = back to flat at the default / no generator).
function notePatch(a, b) {
  const set = {};
  for (const k of ['p', 's', 'l', 'vel', 'glide', 'gc', 'glideTo', 'sample']) if (JSON.stringify(a[k]) !== JSON.stringify(b[k])) set[k] = b[k];
  for (const k of ['c', 'gen']) {
    const A0 = a[k] || {}, B0 = b[k] || {}, d = {};
    for (const lane of new Set([...Object.keys(A0), ...Object.keys(B0)])) if (JSON.stringify(A0[lane]) !== JSON.stringify(B0[lane])) d[lane] = B0[lane] === undefined ? null : B0[lane];
    if (Object.keys(d).length) set[k] = d;
  }
  return { id: b.id, set };
}

function gestureCase(name, clipName, clip, setup, script) {
  const { M } = C.load();
  const P = page(), { A, R, $ } = P;
  if (setup.inst) A.setInst(M.normalizeInst(M.mergeInst(A.getInst(), setup.inst)), null);
  if (setup.bpm) R.app.bpm = setup.bpm;
  A.setClip(clip, null);
  if (setup.mode) A.setMode(setup.mode);
  if (setup.subLane) A.setSubLane(setup.subLane);
  if (setup.snap !== undefined) R.app.snap = setup.snap;
  if (setup.free) R.app.free = true;
  if (setup.multi) R.app.multi = true;
  if (setup.sel) A.setSelection(setup.sel, setup.primary === undefined ? undefined : setup.primary);
  const base = A.debug.historyLength();
  const steps = [];
  let clock = 0;
  const state = () => {
    const c = A.getClip(), hist = A.debug.historyLength() - base, rd = $('readout'), nd = A.getActiveNode();
    return { notes: new Map(c.notes.map(n => [n.id, JSON.stringify(C.note(n))])), order: c.notes.map(n => n.id), bars: c.bars, hist, label: hist > 0 ? A.undoLabel() : null,
      sel: A.getSelection(), primary: A.getPrimary(), active: nd ? [nd.id, nd.lane, nd.k] : null,
      readout: rd.style.display === 'block' ? rd.textContent : null, toast: $('toast').textContent || null, linkArm: !!R.app.linkArm };
  };
  $('toast').textContent = '';
  let prev = state();
  const start = { order: prev.order, sel: prev.sel, primary: prev.primary, active: prev.active, linkArm: prev.linkArm };
  let quiet = false;
  const record = (step, extra) => {
    const now = state(), after = {};
    if (quiet) { now.notes = prev.notes; after.notes = false; }
    const changed = [], removed = [];
    for (const [id, j] of now.notes) if (prev.notes.get(id) !== j) changed.push(prev.notes.has(id) ? notePatch(JSON.parse(prev.notes.get(id)), JSON.parse(j)) : { id, note: JSON.parse(j) });
    for (const id of prev.notes.keys()) if (!now.notes.has(id)) removed.push(id);
    if (changed.length) after.changed = changed;
    if (removed.length) after.removed = removed;
    for (const k of ['order', 'bars', 'hist', 'label', 'sel', 'primary', 'active', 'readout', 'toast', 'linkArm'])
      if (JSON.stringify(now[k]) !== JSON.stringify(prev[k])) after[k] = now[k];
    prev = now;
    steps.push(Object.assign({}, step, { after: Object.assign(after, extra || {}) }));
  };
  const api = {
    A, R, M, P,
    quiet: on => { quiet = on; },
    at: ms => { P.advance(ms); clock += ms; },
    down(x, y, mods) { $('toast').textContent = ''; P.down(x, y, mods); record({ do: 'down', x, y, mods: mods || {}, t: clock }); },
    move(x, y, mods) { $('toast').textContent = ''; P.move(x, y, mods); record({ do: 'move', x, y, mods: mods || {}, t: clock }); },
    up(x, y) { $('toast').textContent = ''; P.up(x, y); record({ do: 'up', t: clock }); },
    click(x, y, mods) { api.down(x, y, mods); api.up(x, y); },
    menu(x, y, pick) {
      $('toast').textContent = '';
      const items = P.contextMenu(x, y) || [];
      const shown = [];
      for (const it of items) {
        if (!it) continue;
        if (it.sep) shown.push('-');
        else if (it.header != null) shown.push('#' + it.header);
        else if (it.row) shown.push('[' + it.row.map(r => (r.on ? '*' : '') + r.label).join('|') + ']');
        else shown.push((it.disabled ? '~' : '') + it.label);
      }
      let picked = null;
      if (pick != null) { const it = items.find(i => i && !i.sep && i.header == null && !i.row && i.label === pick); if (!it) throw new Error('make-fixtures: menu has no ' + pick); it.click(); picked = pick; }
      record({ do: 'menu', x, y, pick: picked, t: clock }, { menu: shown });
    },
    // a point on a note: its line at note time t (0..1), dy pixels above it
    lineAt(id, t, dy) { const n = A.noteById(id), BW = A.geometry().BW; return [n.s * BW + n.l * BW * t, R.baseYAt(n, t) - (dy || 0)]; },
    nodeAt(id, lane, k) { const n = A.noteById(id); return R.nodeXY(n, lane, k); },
    segAt(id, lane, k) { const n = A.noteById(id); return R.segXY(n, lane, k); },
    endAt(id, which) { const n = A.noteById(id), G = R.geom(n), BW = A.geometry().BW, RH = A.geometry().RH; return which === 'start' ? [n.s * BW + 3, R.cy(n.p) - G.base[0] * RH] : [(n.s + n.l) * BW - 3, R.cy(n.p) - G.base[G.K - 1] * RH]; },
    glideAt(id, which) { const gh = R.glideHandles(A.noteById(id)); return gh[which]; },
    rowY: p => R.cy(p),
  };
  script(api);
  return { name, input: { clip: clipName, setup: Object.assign({ mode: 1, subLane: 'travel', snap: 0.25, free: false, multi: false, sel: [], primary: null, bpm: 96 }, setup) }, start, steps };
}

function gestures() {
  const { M } = C.load();
  const named = C.namedClips();
  const demo = () => M.loadClipPreset('strand-demo');
  const cases = [];
  const G = (name, clipName, setup, script) => cases.push(gestureCase(name, clipName, clipName.startsWith('preset:') ? M.loadClipPreset(clipName.slice(7)) : named[clipName], setup, script));
  const BW = 64, RH = 14;
  const emptyY = p => (96 - p) * RH + 5;

  // ---- Build: new notes
  G('double-click empty space makes a 1-beat note on the grid (one undo step "Add note")', 'preset:strand-demo', {}, S => {
    S.click(9.3 * BW, emptyY(50)); S.at(100); S.click(9.3 * BW, emptyY(50));
  });
  G('a new note in a scale: scale lock snaps the pitch (C major, row of C#)', 'preset:strand-demo', { inst: { key: 0, scale: 'Major', scaleLock: true } }, S => {
    S.click(10.1 * BW, emptyY(49)); S.at(200); S.click(10.1 * BW, emptyY(49));
  });
  G('grid 1/8 and grid 1/2: the note is max (1 beat, grid) long, its start floored to the grid', 'preset:strand-demo', { snap: 0.5 }, S => {
    S.click(11.7 * BW, emptyY(45)); S.at(150); S.click(11.7 * BW, emptyY(45));
  });
  G('grid Free: the note starts where clicked', 'preset:strand-demo', { snap: 0 }, S => {
    S.click(12.37 * BW, emptyY(44)); S.at(150); S.click(12.37 * BW, emptyY(44));
  });
  G('grid 1 beat: a note 1 beat long', 'preset:strand-demo', { snap: 1 }, S => {
    S.click(13.37 * BW, emptyY(43)); S.at(150); S.click(13.37 * BW, emptyY(43));
  });
  G('the double-click stretches the new note while held (one step "Add note")', 'preset:strand-demo', {}, S => {
    S.click(9.1 * BW, emptyY(41)); S.at(120); S.down(9.1 * BW, emptyY(41)); S.move(10.6 * BW, emptyY(41)); S.move(11.9 * BW, emptyY(41) - 2); S.up(11.9 * BW, emptyY(41) - 2);
  });
  G('the new note held and dragged up past 0.4 row bends its end (one step)', 'preset:strand-demo', {}, S => {
    S.click(9.6 * BW, emptyY(40)); S.at(120); S.down(9.6 * BW, emptyY(40)); S.move(10.4 * BW, emptyY(40) - 5); S.move(10.4 * BW, emptyY(40) - 31); S.up(10.4 * BW, emptyY(40) - 31);
  });
  G('two clicks too slow (400 ms) or too far apart (30 px) make no note', 'preset:strand-demo', {}, S => {
    S.click(9.3 * BW, emptyY(50)); S.at(400); S.click(9.3 * BW, emptyY(50)); S.at(400); S.click(9.3 * BW + 30, emptyY(50));
  });
  G('a double-click past the clip end makes nothing', 'preset:strand-demo', {}, S => {
    S.click(16.2 * BW, emptyY(50)); S.at(100); S.click(16.2 * BW, emptyY(50));
  });
  G('a new note near the clip end is cut at the end', 'preset:strand-demo', { snap: 0 }, S => {
    S.click(15.6 * BW, emptyY(50)); S.at(100); S.click(15.6 * BW, emptyY(50));
  });
  G('a click on empty space clears the selection (no undo step)', 'preset:strand-demo', { sel: ['sd-1', 'sd-2'], primary: 'sd-2' }, S => {
    S.click(9.3 * BW, emptyY(30));
  });

  // ---- Build: selecting and moving
  G('a click on a note selects it; a click that moves nothing makes no undo step', 'preset:strand-demo', {}, S => {
    const [x, y] = S.lineAt('sd-5', 0.4, 0); S.click(x, y); S.at(500); const [x2, y2] = S.lineAt('sd-9', 0.6, 3); S.click(x2, y2);
  });
  G('Ctrl+click adds a note to the selection and removes it again', 'preset:strand-demo', { sel: ['sd-1'], primary: 'sd-1' }, S => {
    const [x, y] = S.lineAt('sd-5', 0.4, 0); S.click(x, y, { ctrlKey: true }); S.at(500);
    const [x2, y2] = S.lineAt('sd-1', 0.4, 0); S.click(x2, y2, { ctrlKey: true }); S.at(500);
    S.click(x, y, { ctrlKey: true }); S.at(500); S.click(x2, y2, { ctrlKey: true });
  });
  G('Multi on: a click adds to the selection', 'preset:strand-demo', { multi: true, sel: ['sd-1'], primary: 'sd-1' }, S => {
    const [x, y] = S.lineAt('sd-9', 0.5, 0); S.click(x, y);
  });
  G('drag a note: grid snap of the move and pitch by rows (one step "Move notes")', 'preset:strand-demo', {}, S => {
    const [x, y] = S.lineAt('sd-5', 0.3, 0); S.down(x, y); S.move(x + 3, y); S.move(x + 41, y - 16); S.move(x + 77, y - 45); S.up(x + 77, y - 45);
  });
  G('drag a selected chord: every selected note moves (one step)', 'preset:strand-demo', { sel: ['sd-1', 'sd-2', 'sd-3'], primary: 'sd-1' }, S => {
    const [x, y] = S.lineAt('sd-2', 0.5, 0); S.down(x, y); S.move(x - 20, y + 30); S.move(x + 140, y + 30); S.up(x + 140, y + 30);
  });
  G('a move with scale lock lands on scale notes', 'preset:strand-demo', { inst: { key: 2, scale: 'Minor', scaleLock: true } }, S => {
    const [x, y] = S.lineAt('sd-9', 0.5, 0); S.down(x, y); S.move(x, y - 15); S.move(x, y - 29); S.move(x + 10, y - 57); S.up(x + 10, y - 57);
  });
  G('a drag that goes out and comes back changes nothing: no undo step', 'preset:strand-demo', {}, S => {
    const [x, y] = S.lineAt('sd-5', 0.3, 0); S.down(x, y); S.move(x + 70, y - 30); S.move(x + 1, y); S.up(x + 1, y);
  });
  G('a move stops at the clip start and end', 'preset:strand-demo', { snap: 0 }, S => {
    const [x, y] = S.lineAt('sd-1', 0.5, 0); S.down(x, y); S.move(x - 900, y); S.move(x + 2000, y); S.up(x + 2000, y);
  });
  G('pitch moves stop at the roll edges', 'preset:strand-demo', {}, S => {
    const [x, y] = S.lineAt('sd-1', 0.5, 0); S.down(x, y); S.move(x, -400); S.move(x, 3000); S.up(x, 3000);
  });

  // ---- Build: end dots, start dots
  G('drag the end dot sideways: the length on the grid (one step "Change length")', 'preset:strand-demo', { sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.endAt('sd-5', 'end'); S.down(x, y); S.move(x + 30, y + 2); S.move(x + 70, y + 4); S.up(x + 70, y + 4);
  });
  G('the end dot moves the end pitch only after 0.4 row (5.6 px), then in semitones', 'preset:strand-demo', { sel: ['sd-9'], primary: 'sd-9' }, S => {
    const [x, y] = S.endAt('sd-9', 'end'); S.down(x, y); S.move(x, y - 5); S.move(x, y - 6); S.move(x, y - 20); S.move(x, y - 44, { shiftKey: true }); S.up(x, y - 44);
  });
  G('the end dot bends at most 12 semitones and the length never under the grid', 'preset:strand-demo', { sel: ['sd-9'], primary: 'sd-9' }, S => {
    const [x, y] = S.endAt('sd-9', 'end'); S.down(x, y); S.move(x - 900, y - 500); S.up(x - 900, y - 500);
  });
  G('drag the start dot: the start moves, the end stays (one step "Move note start")', 'preset:strand-demo', { sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.endAt('sd-5', 'start'); S.down(x, y); S.move(x - 40, y); S.move(x + 50, y + 25); S.up(x + 50, y + 25);
  });
  G('an end dot pressed and let go without moving: no undo step', 'preset:strand-demo', { sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.endAt('sd-5', 'end'); S.click(x, y);
  });

  // ---- Build: slides
  G('Shift+click a second note: the earlier one slides into it ("Slide into"), again removes it', 'preset:strand-demo', { sel: ['sd-1'], primary: 'sd-1' }, S => {
    const [x, y] = S.lineAt('sd-9', 0.5, 0); S.click(x, y, { shiftKey: true }); S.at(500);
    const [x1, y1] = S.lineAt('sd-1', 0.5, 0); S.click(x1, y1, { shiftKey: true });
  });
  G('Shift+click on the band above a note slides too; a Shift drag from the band does not', 'preset:strand-demo', { sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.lineAt('sd-14', 0.5, 25); S.click(x, y, { shiftKey: true });
  });
  G('the slide handles of a legato pair: length and curve (one step each)', 'voice-lead', { sel: ['vl-a1'], primary: 'vl-a1' }, S => {
    const [x, y] = S.glideAt('vl-a1', 'start'); S.down(x, y); S.move(x - 20, y); S.move(x - 50, y + 3); S.up(x - 50, y + 3); S.at(500);
    const [mx, my] = S.glideAt('vl-a1', 'mid'); S.down(mx, my); S.move(mx, my - 20); S.move(mx, my - 45); S.up(mx, my - 45);
  });
  G('right-click menu in Build with a selection: Slide into the next note I click, then a click', 'voice-lead', { sel: ['vl-a1'], primary: 'vl-a1' }, S => {
    const [x, y] = S.lineAt('vl-a1', 0.5, 0); S.menu(x, y, '\u{1F517} Slide into the next note I click'); S.at(300);
    const [x2, y2] = S.lineAt('vl-e4', 0.5, 0); S.click(x2, y2);
  });
  G('right-click menu: Remove slide links, Jump no slide, Duplicate, Delete', 'voice-lead', { sel: ['vl-d3', 'vl-f1'], primary: 'vl-d3' }, S => {
    const [x, y] = S.lineAt('vl-d3', 0.5, 0);
    S.menu(x, y, 'Remove slide links'); S.menu(x, y, 'Jump, no slide'); S.menu(x, y, 'Duplicate'); S.menu(x, y, 'Delete');
  });
  G('right-click on empty space: New note here', 'preset:strand-demo', {}, S => {
    S.menu(9.55 * BW, emptyY(47), 'New note here'); S.menu(9.55 * BW, emptyY(70), null);
  });
  G('right-click on a note selects it first', 'preset:strand-demo', { sel: ['sd-1'], primary: 'sd-1' }, S => {
    const [x, y] = S.lineAt('sd-14', 0.5, 0); S.menu(x, y, null);
  });

  // ---- lane modes: nodes
  G('Pitch: drag a node, it snaps to semitones (one step "Move node")', 'preset:strand-demo', { mode: 2, sel: ['sd-9'], primary: 'sd-9' }, S => {
    const [x, y] = S.nodeAt('sd-9', 'pitch', 1); S.down(x + 2, y - 1); S.move(x + 3, y); S.move(x + 9, y - 13); S.move(x + 12, y - 31); S.up(x + 12, y - 31);
  });
  G('Pitch: Shift keeps a node off the semitones', 'preset:strand-demo', { mode: 2, sel: ['sd-9'], primary: 'sd-9' }, S => {
    const [x, y] = S.nodeAt('sd-9', 'pitch', 1); S.down(x, y); S.move(x + 5, y - 9, { shiftKey: true }); S.up(x + 5, y - 9);
  });
  G('Volume: drag a node; the first node moves only up and down', 'preset:strand-demo', { mode: 3, sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.nodeAt('sd-5', 'amp', 0); S.down(x, y); S.move(x + 30, y - 10); S.up(x + 30, y - 10); S.at(500);
    const nd = S.A.noteById('sd-5').c.amp; const k = nd.length - 1; const [x2, y2] = S.nodeAt('sd-5', 'amp', k); S.down(x2, y2); S.move(x2 - 40, y2 + 8); S.up(x2 - 40, y2 + 8);
  });
  G('a node press that does not move (under 3 px) changes nothing and shows its value', 'preset:strand-demo', { mode: 3, sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.nodeAt('sd-5', 'amp', 0); S.down(x, y); S.move(x + 1, y + 1); S.up(x + 1, y + 1);
  });
  G('double-click on the line adds a node there; double-click a node deletes it', 'preset:strand-demo', { mode: 5, sel: ['sd-9'], primary: 'sd-9' }, S => {
    const [x, y] = S.lineAt('sd-9', 0.37, 10);
    S.click(x, y); S.at(100); S.down(x, y); S.move(x + 4, y - 8); S.up(x + 4, y - 8); S.at(800);
    const k = S.A.getActiveNode() ? S.A.getActiveNode().k : 1; const [nx, ny] = S.nodeAt('sd-9', 'tone', k); S.click(nx, ny); S.at(100); S.click(nx, ny);
  });
  G('double-click the first node: it stays (a toast)', 'preset:strand-demo', { mode: 3, sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.nodeAt('sd-5', 'amp', 0); S.click(x, y); S.at(100); S.click(x, y);
  });
  G('Formant: drag the segment diamond to bend it, double-click to straighten', 'preset:strand-demo', { mode: 6, sel: ['sd-20'], primary: 'sd-20' }, S => {
    const s = S.segAt('sd-20', 'formant', 0); if (!s) throw new Error('no segment');
    S.down(s[0], s[1]); S.move(s[0], s[1] + 12); S.move(s[0], s[1] + 33); S.up(s[0], s[1] + 33); S.at(800);
    const s2 = S.segAt('sd-20', 'formant', 0); S.click(s2[0], s2[1]); S.at(100); S.click(s2[0], s2[1]);
  });
  G('double-click a straight segment diamond adds a node on the line', 'preset:strand-demo', { mode: 4, sel: ['sd-14'], primary: 'sd-14' }, S => {
    const s = S.segAt('sd-14', 'vib', 0); S.click(s[0], s[1]); S.at(100); S.click(s[0], s[1]);
  });
  G('a lane mode: a click selects, a double-click on empty space is refused with a toast', 'preset:strand-demo', { mode: 7 }, S => {
    const [x, y] = S.lineAt('sd-5', 0.5, 0); S.click(x, y); S.at(500); S.click(9.3 * BW, emptyY(50)); S.at(100); S.click(9.3 * BW, emptyY(50));
  });
  G('Pitch: a note drag does not move the note', 'preset:strand-demo', { mode: 2, sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.lineAt('sd-9', 0.5, 0); S.down(x, y); S.move(x + 60, y - 40); S.up(x + 60, y - 40);
  });
  G('Pitch: the slide handles work, Shift+click links', 'voice-lead', { mode: 2, sel: ['vl-c1'], primary: 'vl-c1' }, S => {
    const [x, y] = S.glideAt('vl-c1', 'mid'); S.down(x, y); S.move(x, y + 30); S.up(x, y + 30); S.at(500);
    const [x2, y2] = S.lineAt('vl-e1', 0.5, 0); S.click(x2, y2, { shiftKey: true });
  });
  G('Sample mode, Travel: nodes snap near 1x, 1/8x and stopped unless Shift', 'preset:strand-demo', { mode: 8, subLane: 'travel', sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.nodeAt('sd-5', 'travel', 0); S.down(x, y); S.move(x, y + 9); S.move(x, y + 9.2); S.move(x, y + 16, { shiftKey: true }); S.up(x, y + 16);
  });
  G('Sample mode, Heads: drag a node with the readout', 'preset:strand-demo', { mode: 8, subLane: 'heads', sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.nodeAt('sd-5', 'heads', 1); S.down(x, y); S.move(x, y - 14); S.up(x, y - 14);
  });
  G('Refresh with Smooth: the readout says so', 'preset:strand-demo', { mode: 8, subLane: 'refresh', sel: ['sd-5'], primary: 'sd-5' }, S => {
    const [x, y] = S.nodeAt('sd-5', 'refresh', 1); S.down(x, y); S.move(x, y - 10); S.up(x, y - 10);
  });
  G('Refresh synced: the readout names the rate', 'preset:strand-demo', { mode: 8, subLane: 'refresh', sel: ['sd-5'], primary: 'sd-5', inst: { refresh: { mode: 'sync', rate: '1/8T' } } }, S => {
    const [x, y] = S.nodeAt('sd-5', 'refresh', 1); S.down(x, y); S.move(x, y - 10); S.up(x, y - 10);
  });
  G('a node of another selected note becomes the primary note', 'preset:strand-demo', { mode: 3, sel: ['sd-1', 'sd-5'], primary: 'sd-1' }, S => {
    const [x, y] = S.nodeAt('sd-5', 'amp', 1); S.down(x, y); S.up(x, y);
  });
  G('lane mode right-click menu: shapes and the common items', 'preset:strand-demo', { mode: 3, sel: ['sd-1', 'sd-5'], primary: 'sd-1' }, S => {
    const [x, y] = S.lineAt('sd-5', 0.5, 0); S.menu(x, y, 'Swell'); S.menu(x, y, null);
  });

  // ---- Freehand
  G('Freehand: paint across the note, it becomes nodes (RDP 0.012) on release (one step)', 'preset:strand-demo', { mode: 3, free: true, sel: ['sd-5'], primary: 'sd-5' }, S => {
    const n = S.A.noteById('sd-5'), x0 = n.s * BW, w = n.l * BW, yb = S.R.baseYAt(n, 0.5);
    S.quiet(true); S.down(x0 + 2, yb - 10); S.move(x0 + w * 0.2, yb - 30); S.move(x0 + w * 0.5, yb - 5); S.move(x0 + w * 0.45, yb - 20); S.quiet(false); S.move(x0 + w * 0.9, yb - 36); S.up(x0 + w * 0.9, yb - 36);
  });
  G('Freehand with no note selected: the note under the mouse is selected and painted', 'preset:strand-demo', { mode: 5, free: true }, S => {
    const n = S.A.noteById('sd-9'), x0 = n.s * BW, w = n.l * BW, yb = S.R.baseYAt(n, 0.3);
    S.quiet(true); S.down(x0 + w * 0.3, yb - 8); S.move(x0 + w * 0.6, yb - 30); S.quiet(false); S.up(x0 + w * 0.6, yb - 30);
  });
  G('Freehand in Pitch: the pitch line is painted in semitone space (no snap)', 'preset:strand-demo', { mode: 2, free: true, sel: ['sd-9'], primary: 'sd-9' }, S => {
    const n = S.A.noteById('sd-9'), x0 = n.s * BW, w = n.l * BW, yb = S.R.baseYAt(n, 0.3);
    S.quiet(true); S.down(x0 + w * 0.1, yb - 8); S.move(x0 + w * 0.8, yb + 20); S.quiet(false); S.up(x0 + w * 0.8, yb + 20);
  });

  // ---- seeded random sequences (each a few gestures) in every mode: the C++ must make the same clips
  const rnd = C.rng(4404);
  const ids = demo().notes.map(n => n.id);
  for (let mode = 1; mode <= 8; mode++) {
    for (let r = 0; r < 1; r++) {
      const sub = ['travel', 'heads', 'grain', 'refresh'][r];
      const selN = 1 + Math.floor(rnd() * 3);
      const sel = []; for (let i = 0; i < selN; i++) { const id = ids[Math.floor(rnd() * ids.length)]; if (!sel.includes(id)) sel.push(id); }
      G('seeded gestures, mode ' + mode + ' #' + r, 'preset:strand-demo', { mode, subLane: mode === 8 ? sub : undefined, snap: [0.25, 0.5, 0, 0.125][r], sel, primary: sel[0] }, S => {
        for (let g = 0; g < 7; g++) {
          const P0 = S.A.primaryNote() || S.A.noteById(sel[0]);
          const kind = rnd();
          let x, y;
          const AL = S.A.activeLane();
          if (AL && kind < 0.45 && P0) { const nd = P0.c[AL]; const k = Math.floor(rnd() * nd.length); [x, y] = S.nodeAt(P0.id, AL, k); }
          else if (AL && kind < 0.6 && P0 && P0.c[AL].length > 1) { const s = S.segAt(P0.id, AL, Math.floor(rnd() * (P0.c[AL].length - 1))); if (s) [x, y] = s; else [x, y] = S.lineAt(P0.id, rnd(), 0); }
          else if (mode === 1 && kind < 0.6 && P0) [x, y] = S.endAt(P0.id, rnd() < 0.5 ? 'end' : 'start');
          else if (kind < 0.85) { const n = S.A.noteById(ids[Math.floor(rnd() * ids.length)]); [x, y] = S.lineAt(n.id, rnd(), rnd() * 30 - 5); }
          else { x = rnd() * 16 * BW; y = 300 + rnd() * 500; }
          const mods = rnd() < 0.12 ? { shiftKey: true } : rnd() < 0.08 ? { ctrlKey: true } : {};
          const dbl = rnd() < 0.15;
          if (dbl) { S.click(x, y, mods); S.at(90); }
          S.down(x, y, mods);
          const moves = Math.floor(rnd() * 4);
          let mx = x, my = y;
          for (let m = 0; m < moves; m++) { mx += (rnd() - 0.5) * 120; my += (rnd() - 0.5) * 60; S.move(mx, my, rnd() < 0.2 ? { shiftKey: true } : {}); }
          S.up(mx, my);
          S.at(700);
        }
      });
    }
  }
  return { prototype: C.prototypeHashes(['src/sl-ui.js']),
    tolerance: { default: 'node values, starts, lengths and slide values within 1e-9; ids, order, counts, labels and texts exact' },
    notes: ['start: the selection, primary, active node, note order and arming after the setup; each step after lists only what changed in it: changed = {id, note} for a new note (C.note shape) or {id, set} with the fields that changed (set.c / set.gen: only the lanes that changed, null = flat at the default / none); removed = ids that went',
      'each case runs the prototype page (sl-ui.js in a stub DOM) from a fresh start: input.clip, then input.setup (mode, subLane, snap = the grid in beats with 0 = Free, free = Freehand on, multi, sel and primary, inst = a patch on the init sound preset, bpm); BW 64, RH 14, nothing scrolled',
      'steps: down / move / up (x, y in roll pixels, mods), menu (a right-click at x, y; pick = the item clicked, null = none; menu = what it showed: #header, - separator, ~disabled, [a|*b] a row with b on); t = the page clock in ms (two presses within 320 ms and 22 px on the same kind of thing are a double-click)',
      'after.notes = false: the notes were not recorded in that step (Freehand paints in between); the next step lists its changes against the last recorded notes',
      'after: order = every note id in clip order; changed notes in the C.note shape; hist = undo steps since the setup and label the newest; sel in time order; active = [id, lane, k] of the inspector node; readout and toast texts (null = none)'],
    clips: { 'voice-lead': C.clip(named['voice-lead']) },
    insts: { init: C.plain(INIT()) },
    cases };
}

module.exports = function roll() {
  return { 'roll/geometry.json': geometry(), 'roll/gestures.json': gestures() };
};
