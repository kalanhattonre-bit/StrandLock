// node test for src/sl-ui-sample.js: pure logic, the exact SLModel calls the UI makes, and a smoke run of the
// real file under a minimal DOM with a fake SLApp that copies sl-ui.js's editing rules (one push per label).
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm');
const SRC = path.join(__dirname, '..', 'src');
const M = require(SRC + '/sl-model.js'), SND = require(SRC + '/sl-sounds.js'), AN = require(SRC + '/sl-analysis.js');
const { makeDocument, dispatch, Option, findAll } = require('./ui-helpers/minidom.js');
let pass = 0, fail = 0;
const check = (name, ok, info) => { if (ok) { pass++; console.log('PASS ' + name); } else { fail++; console.log('FAIL ' + name + (info !== undefined ? ': ' + (typeof info === 'string' ? info : JSON.stringify(info)) : '')); } };

// ============================================================ pure part
const P = require(SRC + '/sl-ui-sample.js');
const I0 = M.applySoundPreset(M.defaultInst(), 'init').inst;
const demo = M.loadClipPreset('strand-demo');
const fz = (patch, clip) => P.fades(M.normalizeInst(M.mergeInst(I0, patch)), clip || demo);
{
  let F = fz({ engine: 'tape' });
  check('tape: grain faded', F.grain.faded && F.grain.reason.includes('Tape'));
  check('tape: spray faded', F.spray.faded);
  check('tape: freeze faded', F.freeze.faded);
  check('tape: tape switch faded', F.tape.faded && F.tape.reason.includes('already'));
  check('tape: rev faded', F.rev.faded);
  check('tape: speed active (tape plays freeze forwards)', !fz({ engine: 'tape', motion: 'freeze' }).speed.faded);
  F = fz({ engine: 'voice' });
  check('voice: spray faded with Voice reason', F.spray.faded && F.spray.reason.includes('Voice'));
  check('voice: grain/tape/rev active', !F.grain.faded && !F.tape.faded && !F.rev.faded);
  check('voice: lock faded', F.lock.faded);
  F = fz({ engine: 'cloud' });
  check('cloud: spray, lock active', !F.spray.faded && !F.lock.faded);
  F = fz({ engine: 'cloud', lock: true });
  check('lock: tape and rev faded with Lock reason', F.tape.faded && F.rev.faded && F.tape.reason.includes('Lock'));
  check('lock: spray active', !F.spray.faded);
  check('heads 1: spread and headTune faded', fz({}).spread.faded && fz({}).headTune.faded);
  check('heads 2: spread active', !fz({ heads: 2 }).spread.faded);
  check('heads macro up: spread active', !fz({ macros: [0.5, 0.5, 0.6, 0.5, 0.5, 0.5, 0.5, 0.5] }).spread.faded);
  check('mod wheel on heads: spread active', !fz({ mod1: { target: 'heads' } }).spread.faded);
  check('freeze: speed and drift faded', fz({ motion: 'freeze' }).speed.faded && fz({ motion: 'freeze' }).drift.faded);
  check('bounce: at the end faded', fz({ motion: 'bounce' }).atEnd.faded);
  check('forward: jump faded, scatter: jump active', fz({}).jump.faded && !fz({ motion: 'scatter' }).jump.faded);
  check('smooth: refresh time/rate/skip faded, macro refresh faded', fz({}).refreshMs.faded && fz({}).refreshRate.faded && fz({}).skip.faded && fz({}).mRefresh.faded);
  check('free: time + skip active, rate faded', !fz({ refresh: { mode: 'free' } }).refreshMs.faded && !fz({ refresh: { mode: 'free' } }).skip.faded && fz({ refresh: { mode: 'free' } }).refreshRate.faded);
  check('sync: rate active', !fz({ refresh: { mode: 'sync' } }).refreshRate.faded);
  // note overrides: inst Tape, 3 notes on Cloud -> grain/spray matter through the notes
  const ids = demo.notes.slice(0, 3).map(n => n.id);
  const c3 = M.setNoteSample(demo, ids, { engine: 'cloud' });
  F = fz({ engine: 'tape' }, c3);
  check('notes on cloud: grain not faded, counted', !F.grain.faded && F.grain.notes === 3, F.grain);
  check('notes on cloud: spray not faded', !F.spray.faded && F.spray.notes === 3);
  check('grainKind follows the notes when the inst is Tape', P.grainKind(M.normalizeInst(M.mergeInst(I0, { engine: 'tape' })), c3) === 'cloud');
  check('grainKind null when everything is Tape', P.grainKind(M.normalizeInst(M.mergeInst(I0, { engine: 'tape' })), demo) === null);
  const cs = M.setNoteSample(demo, ids, { motion: 'scatter' });
  check('notes on scatter (jump from inst): jump active', !fz({}, cs).jump.faded && fz({}, cs).jump.notes === 3);
  const cj = M.setNoteSample(cs, ids, { jump: '1/4' });
  check('notes on scatter with own jump: inst jump faded', fz({}, cj).jump.faded);
  const ch = M.setLaneNodes(demo, ids[0], 'heads', [{ t: 0, v: 0.5, shape: 'smooth', bend: 0 }, { t: 1, v: 0.8, shape: 'smooth', bend: 0 }]);
  check('a note drawing more heads: spread active', !fz({}, ch).spread.faded);
  // readouts
  check('macroText centre', P.macroText(0, 0.5) === 'no change');
  check('macroText speed', P.macroText(0, 0.75) === '2x faster' && P.macroText(0, 0.25) === '2x slower', [P.macroText(0, 0.75), P.macroText(0, 0.25)]);
  check('macroText heads', P.macroText(2, 0.6) === '+1.4 heads', P.macroText(2, 0.6));
  check('macroText refresh', P.macroText(3, 1) === '8x faster', P.macroText(3, 1));
  check('macroText grain', P.macroText(4, 0) === '4x shorter', P.macroText(4, 0));
  check('macroText tone', P.macroText(6, 0.75) === '+2.1 oct brighter', P.macroText(6, 0.75));
  check('macroText formant', P.macroText(7, 0.75) === '+6 semitones formant', P.macroText(7, 0.75));
  check('macroText spread', P.macroText(1, 0.7) === '+40% spread', P.macroText(1, 0.7));
  check('offsetText', P.offsetText('formant', 0.25) === '+6 semitones' && P.offsetText('hollow', 0.5) === '14.0 dB more hollow' && P.offsetText('amp', -0.2) === '-5.0 dB' && P.offsetText('vib', 0) === 'no change',
    [P.offsetText('formant', 0.25), P.offsetText('hollow', 0.5), P.offsetText('amp', -0.2)]);
  check('suggestEngine no pitch long -> cloud', P.suggestEngine({ pitch: false, seconds: 200, parts: [] }).engine === 'cloud');
  check('suggestEngine short beat -> tape', P.suggestEngine({ pitch: false, seconds: 8, parts: [{ kind: 'loop', tempoFound: true }] }).engine === 'tape');
  check('suggestEngine pitched short -> voice', P.suggestEngine({ pitch: true, seconds: 5 }).engine === 'voice');
  check('pickPart prefers steady', P.pickPart([{ kind: 'hit', start: 0, end: 0.1 }, { kind: 'steady', start: 0.2, end: 0.3 }]).kind === 'steady');
  check('parseNum', P.parseNum('1/8') === 0.125 && P.parseNum('0.5x') === 0.5 && P.parseNum('-12 st') === -12 && P.parseNum('abc') === null);
  check('parseMs', P.parseMs('1.5 s') === 1500 && P.parseMs('120 ms') === 120 && P.parseMs('80') === 80);
  const K = P.KNOB_MATH, sp = { min: 0.125, max: 8, log: true, def: 1 };
  check('knob log maps 1x to the middle', Math.abs(K.toNorm(sp, 1) - 0.5) < 1e-12 && Math.abs(K.fromNorm(sp, 0.5) - 1) < 1e-12);
  check('knob magnet', K.qLog([0.5, 1, 2])(1.02, false) === 1 && K.qLog([0.5, 1, 2])(1.02, true) === 1.02);
}

// ============================================================ the SLModel calls the UI makes, with its exact arguments
{
  const I = I0, clip = demo, sel = clip.notes.slice(0, 2).map(n => n.id);
  for (const R of M.RECIPES) {
    const a = M.applyRecipe(clip, I, R.n, null), b = M.applyRecipe(clip, I, R.n, sel);
    const untouched = b.clip.notes.filter(n => !sel.includes(n.id)).every(n => clip.notes.find(m => m.id === n.id) === n);
    let snapOk = true; try { M.compileSnapshot(a.clip, a.inst); M.compileSnapshot(b.clip, b.inst); } catch (e) { snapOk = false; }
    check('recipe ' + R.n + ' on all / on a selection (others untouched) / compiles', a.inst && b.inst && untouched && snapOk);
  }
  const pinsList = ['engine', 'heads', 'grain'];
  let same = true;
  for (let s = 1; s < 40; s++) { const d = M.dice(I, s, pinsList); if (d.engine !== I.engine || d.heads !== I.heads || d.grainCloud !== I.grainCloud || d.grainVoice !== I.grainVoice) same = false; }
  check('dice keeps pinned fields (array of DICE_FIELDS names)', same);
  check('dice changes something over 10 seeds', [1, 2, 3, 4, 5, 6, 7, 8, 9, 10].some(s => JSON.stringify(M.dice(I, s, [])) !== JSON.stringify(I)));
  for (const p of M.SOUND_PRESETS) {
    const r = M.applySoundPreset(M.mergeInst(I, { root: 57, cents: 12, pitch: 3, macros: [0.2, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.9] }), p.id);
    const fin = M.mergeInst(r.inst, { root: 60, cents: 0 });
    check('sound preset ' + p.id + ': sound exists, macros centred, root kept by the merge', SND.list.some(s => s.id === r.sound) && r.inst.macros.every(m => m === 0.5) && fin.root === 60 && fin.cents === 0 && fin.pitch === 3);
  }
  for (const p of M.CLIP_PRESETS) {
    const c = M.loadClipPreset(p.id), round = M.normalizeClip({ bars: c.bars, notes: JSON.parse(JSON.stringify(c.notes)) });
    let ok = true; try { M.compileSnapshot(round, I); } catch (e) { ok = false; }
    check('clip preset ' + p.id + ' loads, survives a JSON round trip (my presets) and compiles', c.notes.length === p.notes.length && round.notes.length === c.notes.length && round.bars === p.bars && ok);
  }
  // every inst patch shape the controls send, through normalizeInst (as SLApp.setInst does)
  const shapes = [
    [{ speed: 0.125 }, I2 => I2.speed === 0.125], [{ speed: 16 }, I2 => I2.speed === 16], [{ heads: 2.25 }, I2 => I2.heads === 2.25], [{ spread: 0.4 }, I2 => I2.spread === 0.4],
    [{ grainVoice: 0.5 }, I2 => I2.grainVoice === 0.5], [{ grainCloud: 2000 }, I2 => I2.grainCloud === 2000], [{ grainLock: 12 }, I2 => I2.grainLock === 12],
    [{ refresh: { mode: 'free' } }, I2 => I2.refresh.mode === 'free' && I2.refresh.ms === I.refresh.ms && I2.refresh.rate === I.refresh.rate],
    [{ refresh: { ms: 37 } }, I2 => I2.refresh.ms === 37 && I2.refresh.mode === I.refresh.mode], [{ refresh: { rate: '1/8T' } }, I2 => I2.refresh.rate === '1/8T'],
    [{ refresh: { lockToBeat: false } }, I2 => I2.refresh.lockToBeat === false], [{ refresh: { skip: 0.25 } }, I2 => I2.refresh.skip === 0.25],
    [{ spray: 0.3 }, I2 => I2.spray === 0.3], [{ pitch: -12.5 }, I2 => I2.pitch === -12.5], [{ pitch: 48 }, I2 => I2.pitch === 48], [{ tape: true }, I2 => I2.tape === true], [{ rev: true }, I2 => I2.rev],
    [{ drift: 0.3 }, I2 => I2.drift === 0.3], [{ headTune: 'fifths' }, I2 => I2.headTune === 'fifths'], [{ region: { atEnd: 'hold' } }, I2 => I2.region.atEnd === 'hold' && I2.region.start === I.region.start && I2.region.end === I.region.end],
    [{ region: { start: 0.2, end: 0.3 } }, I2 => I2.region.start === 0.2 && I2.region.end === 0.3 && I2.region.atEnd === I.region.atEnd], [{ jump: '1/16T' }, I2 => I2.jump === '1/16T'],
    [{ engine: 'cloud', lock: true }, I2 => I2.engine === 'cloud' && I2.lock], [{ motion: 'mirror', heads: 2 }, I2 => I2.motion === 'mirror' && I2.heads === 2],
    [{ macros: [0.5, 0.5, 0.5, 0.75, 0.5, 0.5, 0.5, 0.5] }, I2 => I2.macros[3] === 0.75 && I2.macros.length === 8],
    [{ offsets: { tone: -0.4 } }, I2 => I2.offsets.tone === -0.4 && I2.offsets.amp === 0 && Object.keys(I2.offsets).length === 5],
    [{ offsets: { amp: 0, vib: 0, tone: 0, formant: 0, hollow: 0 } }, I2 => Object.values(I2.offsets).every(v => v === 0)],
  ];
  for (const [patch, ok] of shapes) { const I2 = M.normalizeInst(M.mergeInst(I, patch)); check('patch ' + JSON.stringify(patch), ok(I2)); }
  check('rate labels for the selects', M.RATES.length === 21 && M.rateLabel('1/8T') === '1/8 triplet' && M.rateLabel('1bar') === '1 bar');
}

// ============================================================ the real file under a minimal DOM and a fake SLApp
function boot(opts) {
  opts = opts || {};
  const doc = makeDocument();
  Option.doc = doc;
  const mk = (tag, id, parent) => { const n = doc.createElement(tag); if (id) n.id = id; (parent || doc.body).appendChild(n); return n; };
  ['sampleTab', 'recipesBox', 'macrosBox', 'soundPresetMenu', 'clipPresetMenu', 'partChips'].forEach(id => mk('div', id));
  const menuEl = mk('div', 'menu'); menuEl.style.display = 'none';
  const bars = mk('input', 'bars'); bars.value = '4';
  // timers we flush by hand
  let q = [];
  const setT = (f, ms) => { q.push({ f, at: ms || 0 }); return q.length; };
  const flush = () => { let guard = 0; while (q.length && guard++ < 1000) { q.sort((a, b) => a.at - b.at); const t = q.shift(); t.f(); } };
  // storage
  const store = new Map();
  const localStorage = opts.blocked ? { getItem() { throw new Error('blocked'); }, setItem() { throw new Error('blocked'); } }
    : { getItem: k => (store.has(k) ? store.get(k) : null), setItem: (k, v) => store.set(k, String(v)) };
  // fake SLApp (sl-ui.js's rules: a label = one history push; null = a live change)
  function el(tag, attrs) {
    const e = doc.createElement(tag);
    if (attrs) for (const k of Object.keys(attrs)) {
      const v = attrs[k];
      if (v == null || v === false) continue;
      if (k === 'class') e.className = v; else if (k === 'text') e.textContent = v; else if (k === 'html') e.innerHTML = v;
      else if (k === 'style' && typeof v === 'object') Object.assign(e.style, v);
      else if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2), v);
      else e.setAttribute(k, v === true ? '' : v);
    }
    for (let i = 2; i < arguments.length; i++) {
      const ch = arguments[i];
      if (ch == null || ch === false) continue;
      (Array.isArray(ch) ? ch : [ch]).forEach(x => e.appendChild(typeof x === 'string' || typeof x === 'number' ? doc.createTextNode(String(x)) : x));
    }
    return e;
  }
  const L = {};
  const emit = (n, d) => { for (const f of [...(L[n] || [])]) f(d); };
  const S = { clip: M.loadClipPreset('strand-demo'), inst: M.applySoundPreset(M.defaultInst(), 'init').inst, sel: new Set(), sample: null, ghost: null, hint: null,
    toasts: [], menu: null, pushes: [], live: 0, bpm: 96, playing: false, startBeat: 0, decos: [], ready: [], loads: [] };
  S.hist = new M.History({ clip: S.clip, inst: S.inst });
  const push = label => { S.hist.push({ clip: S.clip, inst: S.inst }, label); S.pushes.push(label); };
  const cache = new Map();
  function setSample(info) {
    const sm = { id: info.id, name: info.name, channels: info.channels, sampleRate: info.sampleRate, seconds: info.channels[0].length / info.sampleRate, builtIn: !!info.builtIn, parts: null };
    S.sample = sm; emit('sample', sm);
    setT(() => { if (S.sample !== sm) return; sm.parts = AN.suggestParts(sm.channels, sm.sampleRate) || []; emit('parts', sm.parts); }, 60);
  }
  const App = {
    el, getClip: () => S.clip, getInst: () => S.inst,
    setClip(c, label) { if (!c || !Array.isArray(c.notes)) throw new Error('not a clip'); S.clip = c; for (const id of [...S.sel]) if (!c.notes.some(n => n.id === id)) S.sel.delete(id); if (label) { push(label); emit('clip', { live: false, label }); } else { S.live++; emit('clip', { live: true, label: null }); } },
    setInst(i, label) { S.inst = M.normalizeInst(i); if (label) push(label); else S.live++; emit('inst', { live: !label, label: label || null }); },
    patchInst(p, label) { App.setInst(M.mergeInst(S.inst, p), label); },
    commit(label) { push(label || 'Edit'); emit('clip', { live: false, label }); },
    getSelection: () => S.clip.notes.filter(n => S.sel.has(n.id)).sort((a, b) => a.s - b.s || a.p - b.p).map(n => n.id),
    setSelection(ids) { S.sel = new Set(ids || []); emit('selection', { ids: App.getSelection() }); },
    getSample: () => S.sample,
    loadSound(id) { S.loads.push(id); let s = cache.get(id); if (!s) { s = SND.make(id); cache.set(id, s); } if (S.inst.root !== 60 || S.inst.cents !== 0) App.setInst(M.mergeInst(S.inst, { root: 60, cents: 0 }), null); setSample({ id, name: s.name, channels: s.channels, sampleRate: s.sampleRate, builtIn: true }); },
    loadFileChannels(name, channels, sr) { App.setInst(M.mergeInst(S.inst, { region: { start: 0, end: 1 } }), null); setSample({ id: 'file', name, channels, sampleRate: sr, builtIn: false }); },
    setGhost(c) { S.ghost = c || null; }, setHint(t) { S.hint = t || null; }, toast(m) { S.toasts.push(m); },
    openMenu(x, y, items) { S.menu = items; menuEl.style.display = 'block'; }, closeMenu() { menuEl.style.display = 'none'; S.menu = null; },
    rateOptions(sel, value) { sel.innerHTML = ''; for (const id of M.RATES) sel.add(new Option(M.rateLabel(id), id)); if (value) sel.value = value; return sel; },
    laneVar: id => 'var(--lane-' + (M.LANE[id] && M.LANE[id].sub ? 'sample' : id) + ')',
    addStripDecorator(fn) { S.decos.push(fn); return () => {}; }, redraw() {},
    on(n, f) { (L[n] || (L[n] = new Set())).add(f); return () => L[n].delete(f); }, off(n, f) { if (L[n]) L[n].delete(f); },
    onReady(fn) { S.ready.push(fn); }, getBpm: () => S.bpm, isPlaying: () => S.playing, stop() { S.playing = false; }, play() { S.playing = true; },
    getStartBeat: () => S.startBeat, setStartBeat(b) { S.startBeat = b; },
  };
  const win = { SLApp: App, SLModel: M, SLSounds: SND, SLAnalysis: AN, localStorage, requestAnimationFrame: f => setT(f, 16) };
  const ctx = vm.createContext(Object.assign(win, { window: null, document: doc, console, setTimeout: setT, clearTimeout() {}, SLModel: M, Date, Math, JSON, Object, Array, Set, Map, WeakSet, Number, String, isFinite }));
  ctx.window = ctx;
  vm.runInContext(fs.readFileSync(SRC + '/sl-ui-sample.js', 'utf8'), ctx, { filename: 'sl-ui-sample.js' });
  // the starter sound loads and SLApp is ready (as sl-ui.js does 30 ms after start)
  App.loadSound('sawopen'); S.ready.splice(0).forEach(f => f(App)); emit('ready');
  flush();
  return { doc, S, App, flush, menuEl, bars, store, UI: ctx.SLSampleUI, emit };
}
const byText = (root, t) => findAll(root, n => n.tagName === 'BUTTON' && n.textContent.trim() === t)[0];
const byTextStart = (root, t) => findAll(root, n => n.tagName === 'BUTTON' && n.textContent.trim().startsWith(t))[0];
const knobOf = (doc, label) => findAll(doc.body, n => n.tagName === 'SVG' && n.getAttribute('aria-label') === label)[0];
const rootOf = dial => dial.parentNode;
const valOf = dial => dial.parentNode.childNodes[2];
let T0 = null;
const clickMenu = (S, label) => {
  const all = [];
  for (const it of S.menu || []) { if (it.row) all.push(...it.row); else all.push(it); }
  const it = all.find(x => x.label && x.label.startsWith(label));
  if (!it) return false;
  if (!it.keep) { S.menu = null; T0.App.closeMenu(); }
  it.click();
  return true;
};
function drag(dial, dy, shift) {
  dispatch(dial, 'pointerdown', { clientY: 300, button: 0 });
  for (let k = 1; k <= 5; k++) dispatch(dial, 'pointermove', { clientY: 300 - dy * k / 5, shiftKey: !!shift });
  dispatch(dial, 'pointerup', { clientY: 300 - dy });
}
{
  const T = boot(); T0 = T;
  const { doc, S, App, flush } = T;
  check('UI built: Sample tab, recipes, macros, preset menus filled', doc.getElementById('sampleTab').childNodes.length >= 4 && doc.getElementById('recipesBox').childNodes.length === 2 && doc.getElementById('macrosBox').childNodes.length === 4 && doc.getElementById('soundPresetMenu').childNodes.length === 2 && doc.getElementById('clipPresetMenu').childNodes.length === 2);
  const isDial = n => n.tagName === 'SVG' && n.classList.contains('sls-dial');
  check('13 knobs on the Sample tab + 8 macros + 5 offsets', findAll(doc.body, isDial).length === 9 + 8 + 5, findAll(doc.body, isDial).length);   // (integrator) the pins are small SVGs too: count the dials
  check('no history pushed at start', S.pushes.length === 0, S.pushes);
  // engine
  let n0 = S.pushes.length;
  byText(doc.body, 'Cloud').click();
  check('Engine Cloud: one step', S.inst.engine === 'cloud' && S.pushes.length === n0 + 1 && S.pushes[n0] === 'Engine: Cloud', S.pushes);
  byText(doc.body, 'Cloud').click();
  check('Engine Cloud again: no step', S.pushes.length === n0 + 1);
  byText(doc.body, 'Voice').click();
  n0 = S.pushes.length;
  byTextStart(doc.body, '\u{1F512} Lock').click();
  check('Lock from Voice: Cloud with Lock in one step', S.inst.engine === 'cloud' && S.inst.lock && S.pushes.length === n0 + 1, S.pushes.slice(n0));
  byTextStart(doc.body, '\u{1F512} Lock').click();
  check('Lock off: one step', !S.inst.lock && S.pushes.length === n0 + 2);
  // motion: Mirror with 1 head sets Heads to 2 in the same click (S2)
  n0 = S.pushes.length;
  byText(doc.body, '\u2194 Mirror').click();
  check('Mirror with 1 head: heads 2, one step', S.inst.motion === 'mirror' && S.inst.heads === 2 && S.pushes.length === n0 + 1, [S.inst.heads, S.pushes.slice(n0)]);
  byText(doc.body, '\u25B6 Forward').click();
  // knob drag: live changes, one commit
  const speed = knobOf(doc, 'Speed');
  n0 = S.pushes.length; const live0 = S.live;
  drag(speed, 85);
  check('Speed drag: value up, several live changes, one step', S.inst.speed > 1.5 && S.live - live0 >= 3 && S.pushes.length === n0 + 1 && S.pushes[n0] === 'Speed', [S.inst.speed, S.live - live0, S.pushes.slice(n0)]);
  check('Speed readout shows the value', valOf(speed).textContent === P.KNOB_MATH && false || /x$/.test(valOf(speed).textContent), valOf(speed).textContent);
  n0 = S.pushes.length;
  dispatch(speed, 'pointerdown', { clientY: 300 }); dispatch(speed, 'pointerup', { clientY: 300 });
  check('Speed click without a move: no step', S.pushes.length === n0);
  dispatch(speed, 'dblclick', {});
  check('Speed double-click: 1x, one step', S.inst.speed === 1 && S.pushes.length === n0 + 1 && S.pushes[n0] === 'Reset Speed', S.pushes.slice(n0));
  dispatch(speed, 'dblclick', {});
  check('double-click at the default: no step', S.pushes.length === n0 + 1);
  // magnet: a small drag from 1x stays at 1x? drag a long way then back
  // type a value
  n0 = S.pushes.length;
  valOf(speed).click();
  const inp = findAll(doc.body, n => n.tagName === 'INPUT' && n.className === 'sls-kin')[0];
  check('click the value: an input appears', !!inp);
  if (inp) { inp.value = '1/2'; dispatch(inp, 'keydown', { key: 'Enter' }); }
  check('typed 1/2: speed 0.5, one step, input gone', S.inst.speed === 0.5 && S.pushes.length === n0 + 1 && !findAll(doc.body, n => n.className === 'sls-kin').length, [S.inst.speed, S.pushes.slice(n0)]);
  valOf(speed).click();
  const inp2 = findAll(doc.body, n => n.className === 'sls-kin')[0];
  if (inp2) { inp2.value = 'abc'; dispatch(inp2, 'keydown', { key: 'Enter' }); }
  check('typed rubbish: no step, a toast', S.pushes.length === n0 + 1 && /Type a number/.test(S.toasts[S.toasts.length - 1]));
  // heads: fine Shift drag, magnets
  const heads = knobOf(doc, 'Heads');
  n0 = S.pushes.length;
  drag(heads, 34);
  check('Heads drag: fractional value on a 0.05 grid or a whole number', S.inst.heads > 1 && Math.abs(S.inst.heads * 20 - Math.round(S.inst.heads * 20)) < 1e-9 && S.pushes.length === n0 + 1, S.inst.heads);
  check('Heads readout', /heads?$/.test(valOf(heads).textContent), valOf(heads).textContent);
  // fades on the real DOM
  byText(doc.body, 'Tape').click();   // the engine button (first 'Tape' button)
  const grainRoot = rootOf(knobOf(doc, 'Grain'));
  check('Tape: Grain knob faded and off, reason in the title', grainRoot.classList.contains('sls-faded') && grainRoot.classList.contains('sls-off') && /Tape plays the sound straight through/.test(grainRoot.title));
  check('Tape: Spray faded', rootOf(knobOf(doc, 'Spray')).classList.contains('sls-faded'));
  check('Tape: Freeze button faded', byText(doc.body, '\u2744 Freeze').classList.contains('sls-faded'));
  const tapeToggles = findAll(doc.body, n => n.tagName === 'BUTTON' && n.textContent === 'Tape');
  check('Tape: the Tape switch faded (second "Tape" button)', tapeToggles.length === 2 && tapeToggles[1].classList.contains('sls-faded'));
  check('Tape: Each grain backwards faded', byText(doc.body, 'Each grain backwards').classList.contains('sls-faded'));
  n0 = S.pushes.length;
  dispatch(knobOf(doc, 'Grain'), 'pointerdown', { clientY: 300 }); dispatch(knobOf(doc, 'Grain'), 'pointermove', { clientY: 200 }); dispatch(knobOf(doc, 'Grain'), 'pointerup', { clientY: 200 });
  check('Tape: dragging the off Grain knob does nothing (toast)', S.pushes.length === n0 && /Tape plays/.test(S.toasts[S.toasts.length - 1]));
  byText(doc.body, 'Cloud').click();
  check('Cloud: Grain in ms, not faded', !grainRoot.classList.contains('sls-faded') && / ms$/.test(valOf(knobOf(doc, 'Grain')).textContent), valOf(knobOf(doc, 'Grain')).textContent);
  n0 = S.pushes.length;
  drag(knobOf(doc, 'Grain'), -40);
  check('Cloud: Grain drag edits grainCloud, one step', S.inst.grainCloud < 140 && S.pushes.length === n0 + 1, S.inst.grainCloud);
  byTextStart(doc.body, '\u{1F512} Lock').click();
  check('Lock: Grain in waves', / waves?$/.test(valOf(knobOf(doc, 'Grain')).textContent), valOf(knobOf(doc, 'Grain')).textContent);
  byTextStart(doc.body, '\u{1F512} Lock').click();
  // refresh
  check('Smooth: Refresh time and Skip faded', rootOf(knobOf(doc, 'Refresh time')).classList.contains('sls-faded') && rootOf(knobOf(doc, 'Skip')).classList.contains('sls-faded'));
  n0 = S.pushes.length;
  byText(doc.body, 'Synced').click();
  check('Refresh Synced: one step', S.inst.refresh.mode === 'sync' && S.pushes.length === n0 + 1 && S.inst.refresh.ms === 120);
  const sels = findAll(doc.body, n => n.tagName === 'SELECT');
  check('two rate selects (Jump every, Every)', sels.length === 2 && sels.every(s => s.options.length === 21));
  const every = sels.find(s => s.getAttribute('aria-label') === 'Every');
  every.value = '1/8T'; dispatch(every, 'change', {});
  check('Refresh every 1/8 triplet: one step', S.inst.refresh.rate === '1/8T' && S.pushes[S.pushes.length - 1] === 'Refresh every 1/8 triplet', S.pushes.slice(-1));
  const jump = sels.find(s => s.getAttribute('aria-label') === 'Jump every');
  check('Jump every faded while Forward', jump.parentNode.classList.contains('sls-faded'));
  byText(doc.body, '\u2059 Scatter').click();
  check('Jump every active with Scatter', !jump.parentNode.classList.contains('sls-faded'));
  byText(doc.body, 'Lock to the beat').click();
  check('Lock to the beat toggles', S.inst.refresh.lockToBeat === false);
  byText(doc.body, 'Octaves').click(); byText(doc.body, 'Hold').click();
  check('Head tune + At the end', S.inst.headTune === 'octaves' && S.inst.region.atEnd === 'hold');
  byText(doc.body, '\u21C4 Bounce').click();
  check('Bounce: At the end faded', findAll(doc.body, n => n.className && n.className.includes('sls-g') && n.childNodes[0] && n.childNodes[0].textContent === 'At the end')[0].classList.contains('sls-faded'));
  // hover help
  dispatch(rootOf(knobOf(doc, 'Spread')), 'mouseenter', {});
  check('hover shows help in the hint line', typeof S.hint === 'string' && S.hint.startsWith('Spread'), S.hint);
  dispatch(rootOf(knobOf(doc, 'Spread')), 'mouseleave', {});
  check('leaving clears the hint', S.hint === null);
  // recipes
  n0 = S.pushes.length;
  const r3 = byTextStart(doc.getElementById('recipesBox'), '3');
  dispatch(r3, 'mouseenter', {});
  check('recipe hover: ghost clip + hint', S.ghost && Array.isArray(S.ghost.notes) && /Recipe 3/.test(S.hint));
  dispatch(r3, 'mouseleave', {});
  check('recipe leave: ghost cleared', S.ghost === null);
  r3.click();
  check('Recipe 3 on every note: Frozen choir settings, one step', S.inst.motion === 'freeze' && S.inst.heads === 4 && S.pushes.length === n0 + 1 && S.pushes[n0] === 'Recipe 3 Frozen choir', S.pushes.slice(n0));
  const sel = S.clip.notes.slice(0, 2).map(n => n.id);
  App.setSelection(sel);
  check('recipe scope text follows the selection', /2 selected notes/.test(findAll(doc.getElementById('recipesBox'), n => n.className === 'sls-sub')[0].textContent));
  const before = S.clip;
  n0 = S.pushes.length;
  byTextStart(doc.getElementById('recipesBox'), '4').click();
  const changed = S.clip.notes.filter(n => before.notes.find(m => m.id === n.id) !== n).map(n => n.id).sort();
  check('Recipe 4 on 2 selected notes: only they change, Tape engine, one step', JSON.stringify(changed) === JSON.stringify(sel.slice().sort()) && S.inst.engine === 'tape' && S.pushes.length === n0 + 1, [changed, S.pushes.slice(n0)]);
  n0 = S.pushes.length;
  byTextStart(doc.getElementById('recipesBox'), '8').click();
  check('Recipe 8 (instrument only): one step', S.inst.engine === 'cloud' && S.inst.lock && S.pushes.length === n0 + 1);
  App.setSelection([]);
  // undo through History restores the previous inst
  const prev = S.hist.undo();
  check('History undo of Recipe 8 gives the Recipe 4 instrument', prev && prev.inst.engine === 'tape');
  S.hist.redo();
  // pins + dice
  const enginePin = findAll(doc.body, n => n.className && n.className.includes('sls-pin') && /Engine/.test(n.title))[0];
  enginePin.click();
  check('pin Engine', T.UI.pins().includes('engine') && enginePin.classList.contains('on') && JSON.parse(T.store.get('strandlock_v4_pins')).includes('engine'));
  const eng = S.inst.engine;
  n0 = S.pushes.length;
  let diceSame = true;
  for (let k = 0; k < 6; k++) { byTextStart(doc.body, '\u{1F3B2} Dice').click(); if (S.inst.engine !== eng) diceSame = false; }
  check('Dice x6 with Engine pinned: engine kept, six steps', diceSame && S.pushes.length === n0 + 6 && S.pushes.slice(n0).every(l => l === 'Dice'), S.pushes.slice(n0));
  check('pinned summary shows Engine', /Pinned: Engine/.test(findAll(doc.getElementById('recipesBox'), n => n.className === 'sls-sub')[1].textContent));
  byText(doc.body, 'Unpin all').click();
  check('Unpin all', T.UI.pins().length === 0);
  // macros
  const m0 = knobOf(doc, 'Speed');   // the Sample tab Speed comes first in the document
  const macroSpeed = findAll(doc.getElementById('macrosBox'), n => n.tagName === 'SVG' && n.getAttribute('aria-label') === 'Speed')[0];
  n0 = S.pushes.length;
  drag(macroSpeed, 40);
  check('Macro Speed drag: macros[0] up, the others centred, one step', S.inst.macros[0] > 0.5 && S.inst.macros.slice(1).every(m => m === 0.5) && S.pushes.length === n0 + 1 && S.pushes[n0] === 'Macro Speed', [S.inst.macros, S.pushes.slice(n0)]);
  check('Macro readout', /faster$/.test(valOf(macroSpeed).textContent), valOf(macroSpeed).textContent);
  const macroRefresh = findAll(doc.getElementById('macrosBox'), n => n.tagName === 'SVG' && n.getAttribute('aria-label') === 'Refresh')[0];
  App.patchInst({ refresh: { mode: 'smooth' } }, 'x');
  check('Refresh macro faded while Smooth', rootOf(macroRefresh).classList.contains('sls-faded'));
  n0 = S.pushes.length;
  byText(doc.getElementById('macrosBox'), 'Centre all').click();
  check('Centre all: one step', S.inst.macros.every(m => m === 0.5) && S.pushes.length === n0 + 1);
  byText(doc.getElementById('macrosBox'), 'Centre all').click();
  check('Centre all again: no step', S.pushes.length === n0 + 1);
  const offTone = findAll(doc.getElementById('macrosBox'), n => n.tagName === 'SVG' && n.getAttribute('aria-label') === 'Tone')[1];
  n0 = S.pushes.length;
  drag(offTone, -60);
  check('Tone offset drag: negative, one step', S.inst.offsets.tone < 0 && S.pushes.length === n0 + 1 && S.pushes[n0] === 'Offset Tone', [S.inst.offsets, S.pushes.slice(n0)]);
  check('offset knob colour is the lane token', rootOf(offTone).style['--k-col'] === 'var(--lane-tone)');
  byText(doc.getElementById('macrosBox'), 'Reset all').click();
  check('Reset all offsets', Object.values(S.inst.offsets).every(v => v === 0));
  // sound presets
  const sbtn = byTextStart(doc.getElementById('soundPresetMenu'), '\u266B Sound presets');
  sbtn.click();
  check('Sound preset menu lists 8 presets + Save', S.menu && S.menu.filter(i => i.label && !i.label.startsWith('\uFF0B')).length === 8 && S.menu.some(i => i.label && i.label.startsWith('\uFF0B Save')));
  App.patchInst({ macros: [0.2, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5], pitch: 5 }, 'x');
  n0 = S.pushes.length;
  clickMenu(S, 'Plain saw on tape');
  flush();
  check('Sound preset: loads "saw", Tape, macros centred, pitch kept, one step', S.loads[S.loads.length - 1] === 'saw' && S.inst.engine === 'tape' && S.inst.macros[0] === 0.5 && S.inst.pitch === 5 && S.pushes.length === n0 + 1 && S.pushes[n0] === 'Sound preset: Plain saw on tape', [S.loads, S.pushes.slice(n0)]);
  // save as my preset
  sbtn.click();
  clickMenu(S, '\uFF0B Save');
  const form = findAll(doc.getElementById('soundPresetMenu'), n => n.className === 'sls-save')[0];
  check('Save form opens', form && !form.hidden);
  const fin = findAll(form, n => n.tagName === 'INPUT')[0];
  fin.value = 'Grainy saw';
  byText(form, 'Save').click();
  const mineSaved = JSON.parse(T.store.get('strandlock_v4_mypresets'));
  check('My preset saved in storage with the sound id and settings', form.hidden && mineSaved.sound.length === 1 && mineSaved.sound[0].name === 'Grainy saw' && mineSaved.sound[0].sound === 'saw' && mineSaved.sound[0].inst.engine === 'tape' && !('root' in mineSaved.sound[0].inst));
  App.patchInst({ engine: 'voice' }, 'x');
  sbtn.click();
  n0 = S.pushes.length;
  clickMenu(S, 'Grainy saw');
  flush();
  check('My preset loads: Tape again, one step', S.inst.engine === 'tape' && S.pushes.length === n0 + 1 && S.pushes[n0] === 'My preset: Grainy saw', S.pushes.slice(n0));
  sbtn.click();
  clickMenu(S, '\u2715');
  check('delete asks first (row with Delete and Keep)', S.menu && S.menu.some(i => i.row && i.row[0].label.startsWith('Delete')));
  clickMenu(S, 'Delete');
  check('deleted', JSON.parse(T.store.get('strandlock_v4_mypresets')).sound.length === 0);
  // clip presets
  const cbtn = byTextStart(doc.getElementById('clipPresetMenu'), '\u25A4 Clips');
  cbtn.click();
  check('Clip menu lists 12 presets', S.menu && S.menu.filter(i => i.label && !i.label.startsWith('\uFF0B')).length === M.CLIP_PRESETS.length);
  const pl = S.menu.find(i => i.label === 'Pluck Decay');
  pl.hover();
  check('Clip preset hover: ghost of the preset', S.ghost && S.ghost.notes.length === 16);
  n0 = S.pushes.length; const instBefore = S.inst;
  clickMenu(S, 'Pluck Decay');
  check('Clip preset: notes replaced, bars 2, Bars box updated, inst untouched, one step', S.clip.notes.length === 16 && S.clip.bars === 2 && T.bars.value === '2' && S.inst === instBefore && S.pushes.length === n0 + 1, [S.clip.bars, T.bars.value, S.pushes.slice(n0)]);
  cbtn.click(); clickMenu(S, '\uFF0B Save');
  const cform = findAll(doc.getElementById('clipPresetMenu'), n => n.className === 'sls-save')[0];
  findAll(cform, n => n.tagName === 'INPUT')[0].value = 'My plucks';
  dispatch(findAll(cform, n => n.tagName === 'INPUT')[0], 'keydown', { key: 'Enter' });
  check('Save clip with Enter', JSON.parse(T.store.get('strandlock_v4_mypresets')).clip[0].name === 'My plucks' && cform.hidden);
  cbtn.click(); clickMenu(S, 'Strand Demo');
  cbtn.click(); clickMenu(S, 'My plucks');
  check('My clip loads', S.clip.notes.length === 16 && S.clip.bars === 2);
  // part chips for the starter (short): one Parts chip with a menu
  App.loadSound('beat'); flush();
  const chips = doc.getElementById('partChips');
  const partsBtn = byTextStart(chips, 'Parts');
  check('short starter: one "Parts" chip', !!partsBtn && chips.childNodes.length === 1, chips.childNodes.map(c => c.textContent));
  partsBtn.click();
  check('Parts menu lists the suggestions', S.menu && S.menu.some(i => i.label && /One-bar loop/.test(i.label)));
  n0 = S.pushes.length;
  clickMenu(S, 'One-bar loop');
  check('choosing a part sets the Region, one step', S.inst.region.start === 0 && Math.abs(S.inst.region.end - 0.5) < 1e-6 && S.pushes.length === n0 + 1, [S.inst.region, S.pushes.slice(n0)]);
  check('the Parts chip now names the part', /One-bar loop/.test(chips.childNodes[0].textContent));
  // a long user file: U2 region + chips + E3 suggestion
  const sr = 22050, secs = 60, nS = sr * secs, Lc = new Float32Array(nS);
  let seed = 7; const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff * 2 - 1; };
  for (let i = 0; i < nS; i++) { const t = i / sr; Lc[i] = 0.3 * rnd() * (0.5 + 0.5 * Math.sin(t * 0.7)) + (t > 20 && t < 20.05 ? 0.9 * rnd() : 0); }
  App.patchInst({ engine: 'voice' }, 'x');
  n0 = S.pushes.length;
  App.loadFileChannels('song.wav', [Lc, Lc], sr);
  flush();
  const R = S.inst.region;
  check('U2: a long file opens on a suggested part (one step)', !(R.start <= 0.0005 && R.end >= 0.9995) && S.pushes.includes('Suggested part'), [R, S.pushes.slice(n0)]);
  check('long file: inline part chips + Use the whole file', !!byText(chips, 'Use the whole file') && S.sample.parts.every(p => !!byText(chips, p.label)), chips.childNodes.map(c => c.textContent));
  const sug = byTextStart(chips, '\u{1F4A1}');
  check('E3: noise gets a Cloud suggestion chip and line', sug && /Cloud/.test(sug.textContent) && !findAll(doc.body, n => n.className === 'sls-sugline')[0].hidden, chips.childNodes.map(c => c.textContent));
  n0 = S.pushes.length;
  sug.click();
  check('accepting the suggestion: Cloud, one step, chip gone', S.inst.engine === 'cloud' && S.pushes.length === n0 + 1 && !byTextStart(chips, '\u{1F4A1}') && findAll(doc.body, n => n.className === 'sls-sugline')[0].hidden, S.pushes.slice(n0));
  const pchip = byText(chips, S.sample.parts[0].label);
  dispatch(pchip, 'mouseenter', {});
  let drew = 0; const ctx2 = { set fillStyle(v) {}, set strokeStyle(v) {}, set lineWidth(v) {}, set globalAlpha(v) {}, fillRect() { drew++; }, strokeRect() { drew++; }, setLineDash() {} };
  S.decos.forEach(f => f(ctx2, { x: 0, y: 0, w: 400, h: 90 }, { sample: S.sample, xOf: p => p * 400, theme: { 'region-band': 'x', 'region-edge': 'y' } }));
  check('hovering a part chip draws its box on the strip', drew === 2);
  dispatch(pchip, 'mouseleave', {});
  n0 = S.pushes.length;
  byText(chips, 'Use the whole file').click();
  check('Use the whole file: one step', S.inst.region.start === 0 && S.inst.region.end === 1 && S.pushes.length === n0 + 1);
  check('no console-level throws (fake app emitted every event)', true);
}
{
  const T = boot({ blocked: true }); T0 = T;
  const { doc, S } = T;
  findAll(doc.body, n => n.className && n.className.includes('sls-pin'))[0].click();
  const sbtn = byTextStart(doc.getElementById('soundPresetMenu'), '\u266B Sound presets');
  sbtn.click(); clickMenu(S, '\uFF0B Save');
  byText(findAll(doc.getElementById('soundPresetMenu'), n => n.className === 'sls-save')[0], 'Save').click();
  check('storage blocked: pins and presets still work for this visit', T.UI.pins().length === 1 && T.UI.myPresets().sound.length === 1 && /for this visit/.test(S.toasts[S.toasts.length - 1]), S.toasts.slice(-1));
}
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
