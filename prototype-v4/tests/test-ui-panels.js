'use strict';
// Runs src/sl-ui-panels.js in a fake DOM against a fake SLApp that follows sl-ui.js's rules (a label = one
// History push; null = a live change), with the real SLModel. Clicks every control and checks the undo steps.
const SRC = require('path').join(__dirname, '..', 'src') + '/';
const fs = require('fs'), vm = require('vm');
const M = require(SRC + 'sl-model.js');
const SND = require(SRC + 'sl-sounds.js');
const { makeDom } = require('./ui-helpers/fakedom.js');
const D = makeDom(), document = D.document, window = D.window;
let fails = 0, passes = 0;
const ok = (c, name, info) => { if (c) passes++; else { fails++; console.log('FAIL', name, info === undefined ? '' : JSON.stringify(info)); } };
const warns = [];

// ---- page containers (as in shell.html)
const selRow = D.reg('selRow'); D.reg('selFixed', 'div', selRow); D.reg('selBar', 'div', selRow); D.reg('inspector', 'div', selRow);
D.reg('noteSettings'); D.reg('tryThis');
const modeBar = D.reg('modeBar', 'nav');
const arrows = {};
for (const m of M.MODES) {
  if (m.mode === 1) continue;
  const wrap = document.createElement('span'); wrap.className = 'lanewrap';
  const b = document.createElement('button'); b.className = 'lane'; b.setAttribute('id', 'mode' + m.mode);
  const ar = document.createElement('button'); ar.className = 'lane arrow';
  let basic = 0; ar.addEventListener('click', () => { basic++; }); ar.basic = () => basic;
  wrap.append(b, ar); modeBar.appendChild(wrap); arrows[m.mode] = ar;
}

// ---- fake SLApp
function el(tag, attrs) {
  const e = document.createElement(tag);
  if (attrs) for (const k of Object.keys(attrs)) {
    const v = attrs[k];
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'text') e.textContent = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(e.style, v);
    else if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (let i = 2; i < arguments.length; i++) {
    const ch = arguments[i];
    if (ch == null || ch === false) continue;
    (Array.isArray(ch) ? ch : [ch]).forEach(x => e.appendChild(typeof x === 'string' || typeof x === 'number' ? document.createTextNode(String(x)) : x));
  }
  return e;
}
const L = {};
const on = (n, f) => { (L[n] = L[n] || new Set()).add(f); return () => L[n].delete(f); };
const emit = (n, d) => { for (const f of [...(L[n] || [])]) f(d); };
const snd = SND.make('sawopen');
const app = {
  clip: M.loadClipPreset('strand-demo'), inst: M.applySoundPreset(M.defaultInst(), 'init').inst,
  sel: new Set(), primary: null, mode: 1, sub: 'travel', scope: 'each', ghost: null, active: null, bpm: 96,
  hint: null, toasts: [], menu: null, decorators: [], ghostSets: 0,
  sample: { id: 'sawopen', name: snd.name, channels: snd.channels, sampleRate: snd.sampleRate, seconds: snd.channels[0].length / snd.sampleRate, builtIn: true },
};
app.hist = new M.History({ clip: app.clip, inst: app.inst });
const byTime = (a, b) => a.s - b.s || a.p - b.p || (a.id < b.id ? -1 : 1);
const noteById = id => app.clip.notes.find(n => n.id === id) || null;
function setSel(ids, primary) {
  const set = new Set(ids == null ? [] : typeof ids === 'string' ? [ids] : ids);
  for (const id of [...set]) if (!noteById(id)) set.delete(id);
  let p = primary !== undefined ? primary : app.primary;
  if (p && !noteById(p)) p = null;
  if (p && !set.has(p)) { if (primary !== undefined) set.add(p); else p = null; }
  if (!p && set.size) p = [...set][0];
  app.sel = set; app.primary = p || null;
  if (app.active && !set.has(app.active.id)) { app.active = null; emit('node', null); }
  emit('selection', { ids: A.getSelection(), primary: app.primary });
}
function prune() { const keep = [...app.sel].filter(noteById); if (keep.length !== app.sel.size) setSel(keep, noteById(app.primary) ? app.primary : null); }
const activeLane = () => (app.mode === 1 ? null : app.mode === 8 ? app.sub : M.MODES[app.mode - 1].lanes[0]);
const A = {
  getClip: () => app.clip,
  setClip(clip, label) { if (!clip || !Array.isArray(clip.notes)) throw new Error('setClip: not a clip'); app.clip = clip; prune(); if (label) { app.hist.push({ clip, inst: app.inst }, label); emit('clip', { live: false, label }); } else emit('clip', { live: true, label: null }); },
  getInst: () => app.inst,
  setInst(inst, label) { app.inst = M.normalizeInst(inst); if (label) app.hist.push({ clip: app.clip, inst: app.inst }, label); emit('inst', { live: !label, label }); },
  patchInst(p, label) { A.setInst(M.mergeInst(app.inst, p), label); },
  commit(label) { app.hist.push({ clip: app.clip, inst: app.inst }, label); emit('clip', { live: false, label }); },
  undo() { const s = app.hist.undo(); if (!s) return false; app.clip = s.clip; app.inst = M.normalizeInst(s.inst); prune(); emit('clip', { live: false, label: 'undo' }); emit('inst', { live: false, label: 'undo' }); return true; },
  getSelection: () => app.clip.notes.filter(n => app.sel.has(n.id)).sort(byTime).map(n => n.id),
  setSelection: (ids, p) => setSel(ids, p === undefined ? undefined : p),
  selectionSet: () => app.sel, getPrimary: () => app.primary, primaryNote: () => (app.primary ? noteById(app.primary) : null),
  selectedNotes: () => app.clip.notes.filter(n => app.sel.has(n.id)).sort(byTime), noteById,
  getMode: () => app.mode, setMode(m) { app.mode = m; if (app.active && app.active.lane !== activeLane()) app.active = null; emit('mode', { mode: m, subLane: app.sub, lane: activeLane() }); },
  getSubLane: () => app.sub, setSubLane(id) { app.sub = id; app.mode = 8; emit('mode', { mode: 8, subLane: id, lane: id }); }, activeLane,
  getActiveNode: () => app.active, setActiveNode(nd) { app.active = nd; emit('node', nd); },
  getNodeSelection: () => app.nodeSel || (app.nodeSel = new Map()),
  getBpm: () => app.bpm, sampleSeconds: () => app.sample.seconds, getSample: () => app.sample,
  formatCtx: n => ({ inst: app.inst, bpm: app.bpm, travelKind: n && n.sample && n.sample.travel === 'path' ? 'path' : 'speed' }),
  getScope: () => app.scope, setScope(s) { app.scope = s; emit('scope', s); },
  redraw() {}, setGhost(c) { app.ghost = c || null; if (c) app.ghostSets++; }, getGhost: () => app.ghost,
  addNoteDecorator(fn) { app.decorators.push(fn); return () => {}; },
  laneColor: id => '#abc', laneVar: id => 'var(--lane-' + id + ')',
  geometry: () => ({ BW: 64, RH: 14, laneH: 36.4, MINP: 24, MAXP: 96 }),
  toast(m) { app.toasts.push(m); }, openMenu(x, y, items) { app.menu = items; }, closeMenu() { app.menu = null; },
  el, setHint(t) { app.hint = t; }, on, off: () => {},
};
window.SLApp = A;

// ---- load the file the way the page does (a plain script, no require)
const ctx = vm.createContext({ window, document, SLModel: M, Option: D.Option, console: { log: console.log, warn: (...a) => { warns.push(a.map(String).join(' ')); console.log('WARN', ...a); } }, setTimeout: (f) => { pending.push(f); return 0; }, clearTimeout: () => {} });
const pending = [];
const flush = () => { while (pending.length) pending.shift()(); };
vm.runInContext(fs.readFileSync(SRC + 'sl-ui-panels.js', 'utf8'), ctx, { filename: 'sl-ui-panels.js' });
const P = ctx.SLPanels;
ok(P && typeof P.parsePos === 'function' && P.state, 'SLPanels loaded in the page path');
ok(app.decorators.length === 1, 'one note decorator added');

// ---- helpers
const walk = (n, f) => { for (const c of n.children || []) { f(c); walk(c, f); } };
const all = (root, pred) => { const out = []; walk(root, c => { if (pred(c)) out.push(c); }); return out; };
const text = e => e.textContent.trim();
const btn = (root, label) => all(root, c => c.tagName === 'BUTTON' && text(c).startsWith(label))[0];
const click = e => { if (!e) throw new Error('no element to click'); e.dispatchEvent({ type: 'click', bubbles: true, detail: 1 }); flush(); };
const hover = e => { e.dispatchEvent({ type: 'mouseenter', bubbles: false }); };
const leave = e => { e.dispatchEvent({ type: 'mouseleave', bubbles: false }); };
const inputs = (root, type) => all(root, c => c.tagName === 'INPUT' && c.type === type);
function slide(inp, values) { inp.dispatchEvent({ type: 'pointerdown', bubbles: true }); for (const v of values) { inp.value = v; inp.dispatchEvent({ type: 'input', bubbles: true }); } inp.dispatchEvent({ type: 'change', bubbles: true }); window.dispatchEvent({ type: 'pointerup', bubbles: true }); flush(); }
function typeIn(inp, t) { inp.focus(); inp.dispatchEvent({ type: 'focus', bubbles: false }); inp.value = t; inp.blur(); inp.dispatchEvent({ type: 'change', bubbles: true }); flush(); }
function selectVal(sel, v) { sel.value = v; sel.dispatchEvent({ type: 'change', bubbles: true }); flush(); }
const H = () => app.hist.i;
const bar = document.getElementById('selBar'), insp = document.getElementById('inspector'), ns = document.getElementById('noteSettings'), tryEl = document.getElementById('tryThis');
const drawer = () => bar.children[1];
const note = id => noteById(id);
// steps: run fn and expect exactly n new undo steps
function steps(n, name, fn) { const h0 = H(); fn(); ok(H() - h0 === n, name + ': ' + n + ' undo step' + (n === 1 ? '' : 's'), H() - h0); }

// ---- empty selection: hidden bar and inspector, note settings shows the hint
ok(bar.style.display === 'none' && insp.style.display === 'none', 'bar and inspector hidden with nothing selected');
ok(/Select notes/.test(text(ns)), 'note settings says to select notes');
ok(/Try this/.test(text(tryEl)) && /Strum/.test(text(tryEl)), 'Try this in Build mode', text(tryEl));

// ---- select the first chord
const chord = ['sd-1', 'sd-2', 'sd-3', 'sd-4'];
A.setSelection(chord, 'sd-1');
ok(bar.style.display === '' && insp.style.display === '', 'bar and inspector shown with a selection');
const laneSel = all(bar, c => c.tagName === 'SELECT')[0];
ok(laneSel.value === 'tone', 'lane select remembers Tone in Build');

// Shapes
click(btn(bar, 'Shapes'));
ok(app.mode === 5, 'opening Shapes shows the Tone lane (mode 5)', app.mode);
ok(btn(drawer(), 'Swell') && btn(drawer(), 'Ramp up') && btn(drawer(), 'Reset'), 'shape buttons present');
hover(btn(drawer(), 'Swell')); ok(app.ghost && app.ghost !== app.clip, 'hovering Swell shows a ghost');
leave(btn(drawer(), 'Swell')); ok(app.ghost === null, 'leaving clears the ghost');
steps(1, 'Swell', () => click(btn(drawer(), 'Swell')));
ok(JSON.stringify(note('sd-2').c.tone.map(q => q.v)) === JSON.stringify(M.shapeNodes('swell', 'tone').map(q => q.v)), 'Swell is on the tone lane');
click(btn(drawer(), 'Across'));
ok(app.scope === 'across', 'Across sets the shared scope');
steps(1, 'Ramp up across', () => click(btn(drawer(), 'Ramp up')));
steps(1, 'Random shape', () => click(btn(drawer(), 'Random')));
const r1 = JSON.stringify(note('sd-1').c.tone);
steps(1, 'Random shape again', () => click(btn(drawer(), 'Random')));
ok(JSON.stringify(note('sd-1').c.tone) !== r1, 'a second Random click deals a new one');
click(btn(drawer(), 'Each note'));

// Travel shapes in Sample mode
A.setSubLane('travel');
ok(laneSel.value === 'travel', 'lane select follows the Sample sub-lane');
const keys = all(drawer(), c => c.tagName === 'BUTTON').map(text);
for (const k of ['Tape stop', 'Tape start', 'Reverse halfway', 'Rewind']) ok(keys.includes(k), 'Travel speed shape ' + k);
ok(!keys.includes('Fit (1x)'), 'no Path shapes for Speed notes');
steps(1, 'Tape stop', () => click(btn(drawer(), 'Tape stop')));
ok(M.travelSpeed(note('sd-1').c.travel[note('sd-1').c.travel.length - 1].v) === 0, 'Tape stop ends stopped');

// Pattern: Apply is one step, slider moves are live, each release one step
A.setMode(5);
click(btn(bar, 'Pattern'));
const d0 = () => inputs(drawer(), 'range')[0];
hover(btn(drawer(), 'Apply')); ok(app.ghost && app.ghost.notes.some(n => n.gen.tone), 'hovering Apply ghosts the pattern'); leave(btn(drawer(), 'Apply'));
steps(1, 'Apply pattern', () => click(btn(drawer(), 'Apply')));
ok(chord.every(id => note(id).gen.tone && note(id).gen.tone.kind === 'pattern' && note(id).gen.tone.rate === '1/16T'), 'pattern on every selected note');
steps(1, 'depth slider (many moves, one release)', () => slide(d0(), [0.4, 0.5, 0.6, 0.7]));
ok(chord.every(id => Math.abs(note(id).gen.tone.d0 - 0.7) < 1e-9), 'depth 0.7 on every note', note('sd-1').gen.tone.d0);
A.undo();
ok(Math.abs(note('sd-1').gen.tone.d0 - 0.3) < 1e-9, 'undo goes back to the applied pattern in one step', note('sd-1').gen.tone.d0);
steps(1, 'depth slider after undo', () => slide(d0(), [0.2, 0.1]));
// shape chip and rate while editing: one step each
steps(1, 'pattern shape chip', () => click(btn(drawer(), 'Square')));
ok(note('sd-3').gen.tone.shape === 'square', 'shape changed live');
const rsel = all(drawer(), c => c.tagName === 'SELECT')[0];
steps(1, 'pattern rate', () => selectVal(rsel, '1/8'));
ok(note('sd-3').gen.tone.rate === '1/8T', 'rate 1/8 triplet', note('sd-3').gen.tone.rate);
steps(1, 'Dotted', () => click(btn(drawer(), 'Dotted')));
ok(note('sd-3').gen.tone.rate === '1/8D', 'rate 1/8 dotted');
steps(1, 'Lock to the beat off', () => click(btn(drawer(), 'Lock to the beat')));
ok(note('sd-3').gen.tone.lockToBeat === false, 'unlocked');
const warp = inputs(drawer(), 'range')[2];
steps(1, 'speed-up slider', () => slide(warp, [0.5, 1]));
ok(Math.abs(note('sd-3').gen.tone.warp - 1) < 1e-9, 'warp 1');
// a random pattern shape keeps each note's seed; Re-roll is one step
steps(1, 'Random steps chip', () => click(btn(drawer(), 'Random steps')));
steps(1, 'Different per note', () => click(btn(drawer(), 'Different per note')));
const seed0 = note('sd-2').gen.tone.seed;
steps(1, 'Re-roll pattern', () => click(btn(drawer(), 'Re-roll')));
ok(note('sd-2').gen.tone.seed === seed0 + 1, 'Re-roll adds 1 to the seed');
steps(1, 'depth after re-roll keeps the seed', () => slide(d0(), [0.5]));
ok(note('sd-2').gen.tone.seed === seed0 + 1, 'slider kept the re-rolled seed');

// Random drawer
click(btn(bar, 'Random'));
ok(/Random on Tone/.test(text(drawer())), 'Random drawer for Tone');
steps(1, 'Apply random', () => click(btn(drawer(), 'Apply')));
ok(chord.every(id => note(id).gen.tone.kind === 'random'), 'random replaced the pattern');
steps(1, 'amount slider', () => slide(inputs(drawer(), 'range')[0], [0.2, 0.8]));
steps(1, 'smooth slider', () => slide(inputs(drawer(), 'range')[1], [0.5]));
ok(Math.abs(note('sd-1').gen.tone.amount - 0.8) < 1e-9 && Math.abs(note('sd-1').gen.tone.smooth - 0.5) < 1e-9, 'amount and smooth set');
steps(1, 'Re-roll random', () => click(btn(drawer(), 'Re-roll')));
// Turn into nodes (bar button) and Remove
hover(btn(bar.children[0], 'Turn into nodes')); ok(!!app.ghost, 'Turn into nodes ghost'); leave(btn(bar.children[0], 'Turn into nodes'));
steps(1, 'Turn into nodes', () => click(btn(bar.children[0], 'Turn into nodes')));
ok(chord.every(id => !note(id).gen.tone && note(id).c.tone.length > 2), 'baked into nodes');
ok(btn(bar.children[0], 'Turn into nodes').disabled, 'Turn into nodes disabled with no pattern left');
steps(1, 'Apply random again', () => click(btn(drawer(), 'Apply')));
steps(1, 'Remove', () => click(btn(drawer(), 'Remove')));
ok(chord.every(id => !note(id).gen.tone), 'removed');

// Pattern on pitch in Build: Apply switches to the lane
A.setMode(1);
selectVal(laneSel, 'pitch');
ok(app.mode === 2, 'choosing Pitch in the lane select shows Pitch', app.mode);
click(btn(bar, 'Pattern'));
ok(/2 semitones/.test(inputs(drawer(), 'range')[0].title) || /st/.test(text(drawer())), 'pitch depth reads in semitones');

// Connect on the 8-note run
A.setMode(1);
const run = ['sd-11', 'sd-12', 'sd-13', 'sd-14', 'sd-15', 'sd-16', 'sd-17', 'sd-18'];
A.setSelection(run, 'sd-11');
click(btn(bar, 'Connect'));
hover(btn(drawer(), 'Nearest')); ok(app.ghost && app.ghost.notes.some(n => n.glideTo), 'Connect ghost has links');
// decorator draws the ghost links
const calls = { fillRect: 0, stroke: 0, arc: 0, fillText: 0 };
const fctx = new Proxy({}, { get: (t, k) => (k in t ? t[k] : (typeof k === 'string' && /^[a-z]/.test(k) ? (...a) => { calls[k] = (calls[k] || 0) + 1; } : undefined)), set: (t, k, v) => { t[k] = v; return true; } });
const TH = { 'lane-sample': '#e2c275', acc: '#b58cff', warn: '#ff5468', bg: '#151618', 'dot-fill': '#151618', ghost: '#fff', wave: '#5fffc8' };
const BW = 64, RH = 14, MAXP = 96, cy = p => (MAXP - p) * RH + RH / 2;
function infoFor(n, sel) {
  const res = Math.min(400, Math.max(BW / 3, 12 / Math.max(0.01, n.l))), tabs = {};
  const tab = id => tabs[id] || (tabs[id] = M.laneTable(n, id, res));
  return { selected: sel, primary: false, mode: app.mode, lane: activeLane(), BW, RH, laneH: RH * 2.6, theme: TH, inst: app.inst, bpm: app.bpm, K: tab('pitch').length,
    table: tab, yAt: t => cy(n.p) - (P.tabAt(tab('pitch'), t) - 0.5) * 24 * RH, valueY: () => 0 };
}
const rectFor = n => ({ x0: n.s * BW, x1: (n.s + n.l) * BW, y0: cy(n.p), y1: cy(n.p), top: 0, bottom: 0 });
const deco = app.decorators[0];
for (const n of app.clip.notes) deco(fctx, n, rectFor(n), infoFor(n, app.sel.has(n.id)));
const fr0 = calls.fillRect;
flush();
for (const n of app.clip.notes) deco(fctx, n, rectFor(n), infoFor(n, app.sel.has(n.id)));
ok(calls.fillRect - fr0 > 2 * fr0, 'thumbnails appear once the peaks are made (outside the frame)', [fr0, calls.fillRect]);
ok(calls.stroke > 50 && calls.fillRect > 200, 'decorator drew chevrons and thumbnails', calls);
leave(btn(drawer(), 'Nearest'));
steps(1, 'Connect nearest', () => click(btn(drawer(), 'Nearest')));
const links = run.filter(id => note(id).glideTo && run.includes(note(id).glideTo)).length;
ok(links >= 5, 'Connect made slides through the run', links);
ok(/slide/.test(app.toasts[app.toasts.length - 1]), 'toast says how many slides');
steps(1, 'Disconnect', () => click(btn(drawer(), 'Disconnect')));
ok(run.every(id => !note(id).glideTo || !run.includes(note(id).glideTo)), 'disconnected');
const two = ['sd-1', 'sd-2', 'sd-3', 'sd-4', 'sd-5', 'sd-6', 'sd-7', 'sd-8'];
A.setSelection(two, 'sd-1');
steps(1, 'Connect random on two chords', () => click(btn(drawer(), 'Random')));
const lk = () => JSON.stringify(two.map(id => note(id).glideTo));
const lk0 = lk(); let changed = false;
for (let i = 0; i < 6 && !changed; i++) { const h0 = H(); click(btn(drawer(), 'Re-roll')); ok(H() - h0 <= 1, 'a re-roll is at most one step'); changed = lk() !== lk0; }
ok(changed, 'Re-roll deals other slides');
ok(two.slice(0, 4).every(id => two.slice(4).includes(note(id).glideTo)), 'each note of chord 1 slides into chord 2');
steps(1, 'Disconnect chords', () => click(btn(drawer(), 'Disconnect')));

// warning mark for a link whose target starts well inside its source
const c0 = app.clip;
A.setClip(M.updateNotes(app.clip, 'sd-9', { glideTo: 'sd-25' }), 'test link');   // bass C1 0..4 -> lead at 6: outside. use sd-1 (0..4) -> sd-25 (6)? no:
A.setClip(M.updateNotes(app.clip, 'sd-9', { glideTo: null }), 'test unlink');
A.setClip(M.updateNotes(app.clip, 'sd-23', { glideTo: 'sd-11' }), 'test inside link');   // b3 at 8..12 -> run note at 8.5: inside
const before = calls.fill || 0;
const n23 = note('sd-23');
deco(fctx, n23, rectFor(n23), infoFor(n23, false));
ok((calls.fillText || 0) > 0 && (calls.fill || 0) > before, 'warning mark drawn for a slide into a note inside its source', calls);
A.setSelection(['sd-23', 'sd-11'], 'sd-23');
click(btn(bar, 'Connect')); click(btn(bar, 'Connect'));
ok(/cut short/.test(text(drawer())), 'Connect panel explains the cut', text(drawer()));

// Tools
A.setMode(1);
A.setSelection(chord, 'sd-1');
selectVal(laneSel, 'tone'); A.setMode(1);
click(btn(bar, 'Tools'));
ok(/Tone:/.test(text(drawer())), 'tools show the lane they change', text(drawer()).slice(0, 80));
for (const t of ['Reverse', 'Flip', 'More', 'Less']) { hover(btn(drawer(), t)); ok(!!app.ghost, t + ' ghost'); leave(btn(drawer(), t)); }
steps(1, 'Reverse', () => click(btn(drawer(), 'Reverse')));
ok(app.mode === 5, 'a lane tool shows its lane');
steps(1, 'Flip', () => click(btn(drawer(), 'Flip')));
steps(1, 'More', () => click(btn(drawer(), 'More')));
steps(1, 'Less', () => click(btn(drawer(), 'Less')));
A.setSelection(['sd-25', 'sd-26'], 'sd-25');
steps(1, 'Legato', () => click(btn(drawer(), 'Legato')));
ok(note('sd-25').s + note('sd-25').l === note('sd-26').s, 'legato reaches the next note');
steps(0, 'Legato again changes nothing and adds no undo step', () => click(btn(drawer(), 'Legato')));
ok(/Already touching/.test(app.toasts[app.toasts.length - 1]), 'Legato with nothing to stretch says so', app.toasts[app.toasts.length - 1]);
A.setSelection(chord, 'sd-1');
// Walk
click(btn(drawer(), 'Walk'));
const wsel = all(drawer(), c => c.tagName === 'SELECT')[0];
ok(wsel.value === 'start', 'Walk starts on Start');
hover(btn(drawer(), 'Walk')); leave(btn(drawer(), 'Walk'));
const walkBtn = all(drawer(), c => c.tagName === 'BUTTON' && text(c) === 'Walk')[0];
steps(1, 'Walk click', () => click(walkBtn));
const starts = A.selectedNotes().map(n => n.sample.start.pos);
ok(Math.abs(starts[0]) < 1e-9 && Math.abs(starts[3] - 0.9) < 1e-9, 'Walk spread the starts 0 to 0.9', starts);
steps(1, 'Walk slider live after the click', () => slide(inputs(drawer(), 'range')[1], [0.5, 0.6]));
ok(Math.abs(A.selectedNotes()[3].sample.start.pos - 0.6) < 1e-9, 'last note start 0.6');
selectVal(all(drawer(), c => c.tagName === 'SELECT')[0], 'heads');
steps(0, 'Walk slider before a click only previews', () => slide(inputs(drawer(), 'range')[1], [0.9]));
// Chop
click(btn(drawer(), 'Chop'));
ok(/pieces/.test(text(drawer())), 'Chop says how many pieces', text(drawer()));
const n0 = app.clip.notes.length;
steps(1, 'Chop 1/16', () => click(all(drawer(), c => c.tagName === 'BUTTON' && text(c) === 'Chop')[0]));
ok(app.clip.notes.length === n0 + 4 * 15, 'four 4-beat notes chopped into 16 pieces each', app.clip.notes.length - n0);
ok(A.getSelection().length === 64, 'the pieces are selected', A.getSelection().length);
A.undo(); A.setSelection(chord, 'sd-1');
selectVal(all(drawer(), c => c.tagName === 'SELECT')[0], '1/64');
ok(/limit/.test(text(drawer())) === false, 'four notes at 1/64 is under the limit');
// Humanize
click(btn(drawer(), 'Humanize'));
const hBtn = all(drawer(), c => c.tagName === 'BUTTON' && text(c) === 'Humanize')[0];
steps(1, 'Humanize', () => click(hBtn));
const hs1 = A.selectedNotes().map(n => n.s);
steps(1, 'Humanize slider', () => slide(inputs(drawer(), 'range')[0], [0.1, 0.2]));
steps(1, 'Humanize re-roll', () => click(btn(drawer(), 'Re-roll')));
ok(JSON.stringify(A.selectedNotes().map(n => n.s)) !== JSON.stringify(hs1), 'humanize moved the starts');
// Strum
click(btn(drawer(), 'Strum'));
steps(1, 'Strum up', () => click(btn(drawer(), 'Strum up')));
steps(1, 'Strum down', () => click(btn(drawer(), 'Strum down')));
// Select similar
click(btn(drawer(), 'Select similar'));
ok(app.menu && app.menu.length === 6, 'Select similar menu');
app.menu.find(i => i.label === 'Every other note').click(); flush();
ok(A.getSelection().length === 2, 'every other note of the chord', A.getSelection());
click(btn(drawer(), 'Select similar'));
app.menu.find(i => i.label === 'Flip the selection').click(); flush();
ok(A.getSelection().length === app.clip.notes.length - 2, 'flip the selection');
click(btn(drawer(), 'Select similar'));
app.menu.find(i => /^Same pitch/.test(i.label)).click(); flush();
ok(A.selectedNotes().every(n => n.p === A.primaryNote().p), 'same pitch');

// Lane ▾ menu replaces the basic one
A.setSelection(chord, 'sd-1');
arrows[6].dispatchEvent({ type: 'click', bubbles: true, detail: 1 }); flush();
ok(arrows[6].basic() === 0, 'the basic arrow handler did not run');
ok(app.mode === 6 && app.menu && app.menu[0].header === 'Formant on 4 notes', 'lane menu for Formant', app.menu && app.menu[0]);
const labels = app.menu.map(i => i.label || i.header || (i.row ? 'row' : i.sep ? 'sep' : '?'));
ok(labels.includes('Pattern settings\u2026') && labels.includes('Random settings\u2026') && labels.includes('Swell'), 'menu has shapes and the settings entries', labels);
app.menu.find(i => i.label === 'Swell').hover(); ok(!!app.ghost, 'menu hover ghost');
steps(1, 'menu Swell', () => app.menu.find(i => i.label === 'Swell').click());
arrows[6].dispatchEvent({ type: 'click', bubbles: true, detail: 1 });
steps(1, 'menu Pattern', () => app.menu.find(i => /^Pattern:/.test(i.label)).click());
arrows[6].dispatchEvent({ type: 'click', bubbles: true, detail: 1 });
app.menu.find(i => i.label === 'Pattern settings\u2026').click();
ok(P.state.drawer === 'pattern' && /Pattern on Formant/.test(text(drawer())), 'Pattern settings opens the drawer for Formant');
arrows[8].dispatchEvent({ type: 'click', bubbles: true, detail: 1 });
ok(app.mode === 8 && /Travel on/.test(app.menu[0].header), 'Sample arrow opens the sub-lane menu');

// Inspector
A.setMode(1); A.setSelection(['sd-25'], 'sd-25');
const ins = inputs(insp, 'text');
ok(ins.length === 6, 'inspector has 6 fields', ins.length);
ok(ins[0].value === '2.3.1' && ins[1].value === String(note('sd-25').l) && ins[2].value === 'C5' && ins[3].value === String(Math.round(0.8 * 127)), 'inspector shows start, length, pitch, velocity', ins.slice(0, 4).map(i => i.value));
steps(1, 'type start', () => typeIn(ins[0], '3.1.1'));
ok(note('sd-25').s === 8, 'start typed', note('sd-25').s);
steps(1, 'type length as a note value', () => typeIn(ins[1], '1/8'));
ok(note('sd-25').l === 0.5, 'length 1/8 = 0.5 beat');
steps(1, 'type pitch', () => typeIn(ins[2], 'D5'));
ok(note('sd-25').p === 86, 'pitch D5 = 86');
steps(1, 'type velocity', () => typeIn(ins[3], '64'));
ok(Math.abs(note('sd-25').vel - 64 / 127) < 1e-9, 'velocity 64');
steps(0, 'bad pitch', () => typeIn(ins[2], 'H9'));
ok(/note such as C3/.test(app.toasts[app.toasts.length - 1]), 'bad pitch explains');
// two notes: start and pitch move both by the same amount
A.setSelection(['sd-1', 'sd-5'], 'sd-1');
const p5 = note('sd-5').p, s5 = note('sd-5').s, s1 = note('sd-1').s;
steps(1, 'type pitch for two notes', () => typeIn(ins[2], 'A2'));
ok(note('sd-1').p === 57 && note('sd-5').p === p5 + 2, 'both moved up 2 semitones', [note('sd-1').p, note('sd-5').p]);
steps(1, 'type start for two notes', () => typeIn(ins[0], '1.2.1'));
ok(note('sd-1').s === 1 && Math.abs((note('sd-5').s - s5) - (1 - s1)) < 1e-9, 'both moved by the same amount', [s1, note('sd-1').s, s5, note('sd-5').s]);
// node fields
A.setSubLane('travel');
A.setSelection(['sd-26'], 'sd-26');
A.setClip(M.setLaneNodes(app.clip, 'sd-26', 'travel', [{ t: 0, v: 0.75 }, { t: 0.5, v: 0.75 }, { t: 1, v: 0.75 }]), 'test nodes');
A.setActiveNode({ id: 'sd-26', lane: 'travel', k: 1 });
ok(insp.children[1].style.display === '', 'node fields show for the active node');
ok(ins[5].value === '1x forwards' && ins[4].value === '1', 'node value and time readouts', [ins[4].value, ins[5].value]);
steps(1, 'type a speed', () => typeIn(ins[5], '1/8x backwards'));
ok(M.travelSpeed(Math.fround(note('sd-26').c.travel[1].v)) < 0, 'typed 1/8x backwards plays backwards');
ok(ins[5].value === '1/8x backwards', 'node field shows what plays, in the words it was typed in (1/8x, not 0.13x)', ins[5].value);
steps(1, 'type node time', () => typeIn(ins[4], '1/16'));
ok(Math.abs(note('sd-26').c.travel[1].t - 0.125) < 1e-9, 'node at 0.25 beat into a 2-beat note', note('sd-26').c.travel[1].t);
A.setActiveNode({ id: 'sd-26', lane: 'travel', k: 0 });
ok(ins[4].disabled && ins[4].value === 'start', 'first node time is fixed');
steps(0, 'bad value', () => typeIn(ins[5], 'fast'));
// (integrator) several nodes picked with the Nodes switch: one value field types them all, as one step
A.setSelection(['sd-25', 'sd-26'], 'sd-26');
app.nodeSel = new Map([['sd-25', new Set([0])], ['sd-26', new Set([0, 1])]]);
emit('nodes', app.nodeSel);
ok(text(insp.children[1]).startsWith('3 nodes (Travel)'), 'three picked nodes show one group field', text(insp.children[1]));
steps(1, 'type one value for the picked nodes', () => typeIn(ins[5], '2x'));
ok([note('sd-25').c.travel[0], note('sd-26').c.travel[0], note('sd-26').c.travel[1]].every(q => Math.abs(M.travelSpeed(Math.fround(q.v)) - 2) < 1e-6), 'all three picked nodes play 2x');
ok(Math.abs(M.travelSpeed(Math.fround(note('sd-26').c.travel[2].v)) - 1) < 1e-6, 'an unpicked node stays at 1x');
app.nodeSel = new Map(); emit('nodes', app.nodeSel);

// Note settings
A.setMode(1);
A.setSelection(chord, 'sd-1');
const nsB = l => btn(ns, l);
ok(/4 notes selected/.test(text(ns)), 'note settings header');
ok(all(ns, c => c.tagName === 'BUTTON' && /^Follow \(Voice\)/.test(text(c))).length === 1, 'Engine Follow shows the instrument engine');
const segOf = name => all(ns, c => c.classList.contains('slp-nsg') && text(c.children[0]) === name)[0];
steps(1, 'engine Cloud', () => click(btn(segOf('Engine'), 'Cloud')));
ok(chord.every(id => note(id).sample.engine === 'cloud'), 'engine set on all');
ok(!segOf('Lock').classList.contains('slp-faded'), 'Lock not faded with Cloud');
steps(1, 'lock on', () => click(btn(segOf('Lock'), 'On')));
steps(1, 'motion Mirror (with Heads to 2)', () => click(btn(segOf('Motion'), 'Mirror')));
ok(chord.every(id => note(id).sample.motion === 'mirror' && Math.abs(M.mapLane('heads', note(id).c.heads[0].v, { inst: app.inst }) - 2) < 1e-9), 'mirror and 2 heads');
ok(/Heads set to 2/.test(app.toasts[app.toasts.length - 1]), 'mirror toast');
A.setClip(M.setLaneNodesMany(app.clip, 'travel', { 'sd-1': M.flat(0.75), 'sd-2': M.flat(0.75), 'sd-3': [{ t: 0, v: 0.75 }, { t: 1, v: 0.9 }], 'sd-4': M.flat(0.75) }), 'test travel lines');
steps(1, 'travel Path', () => click(btn(segOf('Travel'), 'Path')));
ok(['sd-1', 'sd-2', 'sd-4'].every(id => { const nd = note(id).c.travel; return nd[0].v === 0 && nd[nd.length - 1].v > nd[0].v + 0.01; }), 'Path: a flat line starts as a playing line (Fit 1x), not a frozen 75%', note('sd-1').c.travel.map(q => +q.v.toFixed(3)));
ok(note('sd-3').c.travel.length === 2 && note('sd-3').c.travel[1].v === 0.9, 'Path: a shaped line stays as drawn');
ok(/Fit/.test(app.toasts[app.toasts.length - 1]) && /line you drew/.test(app.toasts[app.toasts.length - 1]), 'Path toast explains both lines', app.toasts[app.toasts.length - 1]);
steps(1, 'start Random', () => click(btn(segOf('Start'), 'Random')));
const sd0 = note('sd-1').sample.start.seed;
steps(1, 'start re-roll', () => click(btn(segOf('Start'), 'Re-roll')));
ok(note('sd-1').sample.start.seed === sd0 + 1, 'start seed +1');
steps(1, 'start spot slider', () => slide(inputs(segOf('Start'), 'range')[0], [0.2, 0.4]));
ok(chord.every(id => Math.abs(note(id).sample.start.pos - 0.4) < 1e-9), 'spot 0.4');
steps(1, 'range slider', () => slide(inputs(segOf('Start'), 'range')[1], [0.5]));
steps(1, 'tape on', () => click(btn(segOf('Tape'), 'On')));
steps(1, 'each grain backwards on', () => click(btn(segOf('Each grain backwards'), 'On')));
steps(1, 'jump 1/8T', () => selectVal(all(segOf('Jump every'), c => c.tagName === 'SELECT')[0], '1/8T'));
ok(note('sd-2').sample.jump === '1/8T', 'jump set');
steps(1, 'restart', () => click(nsB('Restart sample here')));
ok(chord.every(id => note(id).sample.restart === true), 'restart on');
const velIn = inputs(ns, 'range').find(i => i.attrs['aria-label'] === 'Velocity');
steps(1, 'velocity slider', () => slide(velIn, [20, 40, 100]));
ok(chord.every(id => Math.abs(note(id).vel - 100 / 127) < 1e-9), 'velocity 100');
// mixed: add a note that follows the instrument
A.setSelection(chord.concat(['sd-9']), 'sd-1');
ok(all(ns, c => c.className === 'slp-mixed' && !c.hidden).length >= 5, 'mixed tags show where notes differ', all(ns, c => c.className === 'slp-mixed' && !c.hidden).length);
ok(btn(segOf('Engine'), 'Cloud').classList.contains('on') === false, 'no engine button is on when mixed');
steps(1, 'follow the Sample tab for all', () => click(nsB('Follow the Sample tab')));
ok(app.clip.notes.filter(n => [...chord, 'sd-9'].includes(n.id)).every(n => n.sample.engine == null && n.sample.motion == null), 'own settings cleared');
// Tape fades Freeze, Path, grain and tape switch
A.setInst(M.mergeInst(app.inst, { engine: 'tape' }), 'test tape');
A.setSelection(chord, 'sd-1');
ok(segOf('Each grain backwards').classList.contains('slp-faded') && btn(segOf('Motion'), 'Freeze').classList.contains('faded'), 'Tape fades what does nothing');
ok(segOf('Jump every').classList.contains('slp-faded'), 'Jump every faded without Scatter');

// Try this per mode and Motion
A.setMode(5); ok(/Squiggle/.test(text(tryEl)), 'Try this in Tone');
A.setSubLane('heads'); ok(/Heads/.test(text(tryEl)), 'Try this in Heads');
A.setInst(M.mergeInst(app.inst, { motion: 'freeze' }), 'test freeze'); A.setSubLane('travel');
ok(/Freeze does nothing in Tape/.test(text(tryEl)) && !/Spray/.test(text(tryEl)), 'Try this for Freeze in Tape suggests no Spray', text(tryEl));
A.setInst(M.mergeInst(app.inst, { engine: 'voice' }), 'test voice'); A.setSubLane('heads'); A.setSubLane('travel');
ok(/Freeze: raise Heads to 4/.test(text(tryEl)) && /pick Cloud to add Spray/.test(text(tryEl)), 'Try this for Freeze in Voice: Spray only with Cloud', text(tryEl));
A.setInst(M.mergeInst(app.inst, { engine: 'cloud' }), 'test cloud'); A.setSubLane('heads'); A.setSubLane('travel');
ok(/Freeze: raise Heads to 4, then add Spray/.test(text(tryEl)), 'Try this for Freeze in Cloud', text(tryEl));
A.setInst(M.mergeInst(app.inst, { engine: 'tape' }), 'test tape again');

// undo after a live session ends it; the next slider move starts from the new clip
A.setMode(5); A.setSelection(chord, 'sd-1');
if (P.state.drawer !== 'pattern') click(btn(bar, 'Pattern'));
ok(P.state.drawer === 'pattern', 'pattern drawer open');
steps(1, 'apply', () => click(btn(drawer(), 'Apply')));
A.undo();
ok(!P.debug.live(), 'undo ends the live session');
ok(chord.every(id => !note(id).gen.tone), 'pattern undone');
steps(0, 'slider after undo previews only (no pattern on the notes)', () => slide(inputs(drawer(), 'range')[0], [0.6]));

// no exceptions, no warnings
ok(warns.length === 0, 'no console warnings', warns);
console.log((fails ? 'FAIL' : 'PASS') + ' panels UI: ' + passes + ' passed, ' + fails + ' failed');
process.exit(fails ? 1 : 0);
