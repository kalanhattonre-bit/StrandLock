/* StrandLock prototype v4: sl-ui-sample.js (pass 2, sample builder)
   Global: SLSampleUI. Loads after sl-ui.js (SLApp). Fills these shell.html containers:
     #sampleTab       Engine Tape / Voice / Cloud + Lock, Motion, Speed, Heads, Spread, Grain, Spray, Drift, Pitch,
                      Refresh (Smooth / Free time / Synced note value + Lock to the beat + Skip), Tape,
                      Each grain backwards, Head tune, At the end, Jump every, and the engine suggestion after a file loads (E3)
     #recipesBox      Recipes 1-8 (SLModel.applyRecipe on the selected notes, or every note) and Dice with pins (S8)
     #macrosBox       the 8 macro knobs (A2, D8) and the 5 whole-clip offsets (A3)
     #soundPresetMenu Sound presets (U4) + Save as my preset
     #clipPresetMenu  Clip presets (U4) + Save as my clip preset
     #partChips       suggested parts of the file (D10) and Use the whole file; a long file opens on a suggested part (U2)
   Rules kept: every edit is one undo step (knob drags patch the instrument live with no label and commit once
   on release); controls that do nothing with the current settings are faded, with the reason on hover and in
   the hint line; every colour comes from the theme tokens; storage is optional (try/catch, memory fallback);
   no alert/confirm/prompt; no computer-key shortcuts (a knob takes arrow keys only after Tab moves focus to it).

   The pure part (no DOM) is exported for node tests: contexts, fades, grainKind, headsMulti, macroText,
   offsetText, suggestEngine, pickPart, parseNum, KNOB_MATH. */
var SLSampleUI = (function () {
  'use strict';
  const M = typeof SLModel !== 'undefined' ? SLModel : require('./sl-model.js');

  // =============================================================================================== small helpers
  const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
  const num = (x, d) => (typeof x === 'number' && isFinite(x) ? x : d);
  const trimZ = s => (s.indexOf('.') >= 0 && s.indexOf('e') < 0 ? s.replace(/0+$/, '').replace(/\.$/, '') : s);
  const r6 = v => Math.round(v * 1e6) / 1e6;
  const sig = (v, d) => (v === 0 ? 0 : Number(v.toPrecision(d)));
  const signed = (x, d) => { const r = Number(x.toFixed(d)); return (r > 0 ? '+' : r < 0 ? '-' : '') + trimZ(Math.abs(r).toFixed(d)); };
  const fmtX = v => (v <= 0 ? 'stopped' : trimZ(v.toPrecision(3)) + 'x');
  const fmtPct = v => Math.round(v * 100) + '%';
  const semis = s => s + (s === '1' ? ' semitone' : ' semitones');
  const fmtSt = v => { const r = Math.round(v * 100) / 100; return Math.abs(r) < 0.005 ? '0 semitones' : (r > 0 ? '+' : '-') + semis(trimZ(Math.abs(r).toFixed(2))); };
  const fmtCycles = v => { const s = trimZ(v.toFixed(2)); return s + (s === '1' ? ' cycle' : ' cycles'); };
  const fmtMs = v => (v >= 1000 ? trimZ((v / 1000).toFixed(2)) + ' s' : Math.round(v) + ' ms');
  const fmtHeads = v => { const r = Math.round(v * 100) / 100; return trimZ(r.toFixed(2)) + (r === 1 ? ' head' : ' heads'); };
  function parseNum(t) {
    if (t == null) return null;
    const s = String(t).trim().replace(',', '.');
    const fr = s.match(/^([+-]?\d+(?:\.\d+)?)\s*\/\s*(\d+(?:\.\d+)?)/);
    if (fr) { const d = +fr[2]; return d ? +fr[1] / d : null; }
    const m = s.match(/[+-]?(?:\d+\.?\d*|\.\d+)/);
    return m ? parseFloat(m[0]) : null;
  }
  const parseMs = t => { const v = parseNum(t); if (v == null) return null; return /\d\s*s\b/i.test(String(t)) && !/ms/i.test(String(t)) ? v * 1000 : v; };
  const parsePct = t => { const v = parseNum(t); return v == null ? null : v / 100; };

  // =============================================================================================== knob maths
  // A knob maps its value to 0..1 (linear or logarithmic) and back; quantizers round while dragging
  // (Shift = fine) and pull towards useful values ("magnets").
  function toNorm(s, v) {
    v = num(v, s.def);
    if (s.log) { const a = Math.log(s.min), b = Math.log(s.max); return clamp((Math.log(Math.max(v, s.min)) - a) / (b - a), 0, 1); }
    return clamp((v - s.min) / (s.max - s.min), 0, 1);
  }
  function fromNorm(s, n) {
    n = clamp(n, 0, 1);
    return s.log ? Math.exp(Math.log(s.min) + n * (Math.log(s.max) - Math.log(s.min))) : s.min + n * (s.max - s.min);
  }
  function magnetLog(v, marks, tol) { for (const m of marks) if (Math.abs(Math.log2(v / m)) < tol) return m; return v; }
  const qLog = (marks, digits) => (v, fine) => (fine ? sig(v, 4) : magnetLog(sig(v, digits || 3), marks || [], 0.06));
  const qLin = (step, fineStep, marks, tol) => (v, fine) => {
    if (fine) return r6(Math.round(v / fineStep) * fineStep);
    let x = r6(Math.round(v / step) * step);
    if (marks) for (const m of marks) if (Math.abs(v - m) < tol) x = m;
    return x;
  };
  const KNOB_MATH = { toNorm, fromNorm, qLog, qLin, magnetLog };

  // =============================================================================================== what each control does now
  // A "context" is one way the instrument's settings get used: the instrument itself (live keys, and every
  // note that follows it), plus one entry per distinct mix of a note's own settings (Note settings, S7).
  // A control is faded when it does nothing in any context; its reason is written for the instrument.
  const kindOf = (engine, lock) => (engine === 'tape' ? 'tape' : engine === 'voice' ? 'voice' : lock ? 'lock' : 'cloud');
  const effMotion = (kind, motion) => (kind === 'tape' && motion === 'freeze' ? 'forward' : motion);   // Tape plays Freeze as Forward
  function contexts(I, clip) {
    const own = kindOf(I.engine, I.engine === 'cloud' && !!I.lock);
    const out = [{ who: 'inst', count: 0, kind: own, engine: I.engine, motion: effMotion(own, I.motion), travel: 'speed',
      from: { lock: I.engine === 'cloud', motion: true, tape: true, rev: true, jump: true } }];
    const seen = new Map();
    for (const n of (clip && clip.notes) || []) {
      const s = n.sample || {};
      const engine = s.engine || I.engine;
      const lock = engine === 'cloud' && (s.lock == null ? !!I.lock : !!s.lock);
      const kind = kindOf(engine, lock);
      const motion = effMotion(kind, s.motion || I.motion), travel = s.travel === 'path' ? 'path' : 'speed';
      const from = { lock: engine === 'cloud' && s.lock == null, motion: !s.motion, tape: s.tape == null, rev: s.rev == null, jump: !s.jump };
      const key = kind + '|' + engine + '|' + motion + '|' + travel + '|' + (+from.lock) + (+from.motion) + (+from.tape) + (+from.rev) + (+from.jump);
      const e = seen.get(key);
      if (e) e.count++;
      else { const c = { who: 'notes', count: 1, kind, engine, motion, travel, from }; seen.set(key, c); out.push(c); }
    }
    return out;
  }
  // Can more than one head ever sound? (Spread and Head tune only act on heads 2 to 8.)
  function headsMulti(I, clip) {
    const mac = (num(I.macros && I.macros[2], 0.5) - 0.5) * 14;
    if (I.heads + mac > 1 + 1e-4) return true;
    if ((I.mod1 && I.mod1.target === 'heads') || (I.mod2 && I.mod2.target === 'heads')) return true;
    for (const n of (clip && clip.notes) || []) {
      if (n.gen && n.gen.heads) return true;
      const nd = n.c && n.c.heads;
      if (nd) for (const q of nd) if (I.heads + (q.v - 0.5) * 14 + mac > 1 + 1e-4) return true;
    }
    return false;
  }
  const moves = c => c.kind === 'tape' || (c.travel === 'speed' && c.motion !== 'freeze');
  const SPEED_WHY = 'Freeze holds the heads still, so Speed does nothing.';
  const SPRAY_WHY = c => (c.kind === 'tape' ? 'Spray scatters grains, and Tape has none. Pick Cloud to use Spray.'
    : 'Voice keeps its grains in step with the pitch, so Spray does nothing. Pick Cloud to use Spray.');
  const GRAIN_WHY = 'Tape plays the sound straight through, with no grains. Pick Voice or Cloud to use Grain.';
  const SPREAD_WHY = 'Spread spaces out several heads. With 1 head it does nothing: raise Heads.';
  const RULES = {
    lock: { uses: c => c.engine === 'cloud' && c.from.lock, why: 'Lock works with Cloud only. Clicking it switches to Cloud with Lock.' },
    freeze: { uses: c => c.from.motion && c.kind !== 'tape', why: 'Freeze does nothing in Tape: Tape plays forwards instead. Pick Voice or Cloud to freeze.' },
    speed: { uses: moves, why: SPEED_WHY },
    drift: { uses: moves, why: 'Freeze holds the heads still, so Drift does nothing.' },
    spread: { multi: true, why: SPREAD_WHY },
    headTune: { multi: true, why: 'Head tune tunes heads 2 to 8. With 1 head it does nothing: raise Heads.' },
    grain: { uses: c => c.kind !== 'tape', why: GRAIN_WHY },
    spray: { uses: c => c.kind === 'cloud' || c.kind === 'lock', why: SPRAY_WHY },
    tape: { uses: c => c.from.tape && (c.kind === 'voice' || c.kind === 'cloud'),
      why: c => (c.kind === 'lock' ? 'Lock always plays in tune, so the Tape switch does nothing. Switch Lock off to use it.'
        : 'The Tape engine already ties pitch to speed, so this switch does nothing.') },
    rev: { uses: c => c.from.rev && (c.kind === 'voice' || c.kind === 'cloud'),
      why: c => (c.kind === 'lock' ? 'Lock repeats single waves, so turning grains round does nothing. Switch Lock off to use it.'
        : 'Tape has no grains to turn round. Pick Voice or Cloud.') },
    atEnd: { uses: c => c.motion !== 'bounce', why: 'Bounce turns round at the edges of the Region, so At the end does nothing.' },
    jump: { uses: c => c.from.jump && c.motion === 'scatter', why: 'Jump every sets how often Scatter jumps. Pick Scatter to use it.' },
    refreshMs: { inst: I => I.refresh.mode === 'free', why: 'This time is used only when Refresh is Free.' },
    refreshRate: { inst: I => I.refresh.mode === 'sync', why: 'The note value and Lock to the beat are used only when Refresh is Synced.' },
    skip: { inst: I => I.refresh.mode !== 'smooth', why: 'Skip leaves out some fresh grabs, so it needs Refresh set to Free or Synced.' },
    mSpeed: { uses: moves, why: SPEED_WHY },
    mSpread: { multi: true, why: SPREAD_WHY },
    mRefresh: { inst: I => I.refresh.mode !== 'smooth', why: 'Refresh is Smooth, so the Refresh macro does nothing. Pick Free or Synced in the Sample tab.' },
    mGrain: { uses: c => c.kind !== 'tape', why: GRAIN_WHY },
    mSpray: { uses: c => c.kind === 'cloud' || c.kind === 'lock', why: SPRAY_WHY },
    mTone: { both: (I, clip) => !(num(I.macros && I.macros[6], 0.5) > 0.5025 && toneAllOpen(I, clip)),
      why: 'Every note\'s Tone line is fully open already, so turning Tone up changes nothing. Turn it down to darken, or draw the Tone lines lower first.' },
    reroll: { both: (I, clip) => randomInUse(I, clip),
      why: 'Re-roll deals new Scatter jumps, Spray, Drift, Skip and random Starts. None of them is in use now.' },
  };
  // Is every Tone line (and the live keys' default) fully open, so a brighter Tone macro has nothing to open?
  function toneAllOpen(I, clip) {
    const o = num(I.offsets && I.offsets.tone, 0);
    if (1 + o < 0.999) return false;   // the live keys play the default line (fully open) plus the offset
    for (const n of (clip && clip.notes) || []) {
      if (n.gen && n.gen.tone) return false;
      const nd = n.c && n.c.tone;
      if (nd) for (const q of nd) if (q.v + o < 0.999) return false;
    }
    return true;
  }
  // Does anything random play now (what Re-roll changes: inst.seed)?
  function randomInUse(I, clip) {
    const cx = contexts(I, clip);
    if (cx.some(c => c.motion === 'scatter')) return true;
    if (I.spray > 0 && cx.some(c => c.kind === 'cloud' || c.kind === 'lock')) return true;
    if (I.drift > 0 && cx.some(moves)) return true;
    if (I.refresh.mode !== 'smooth' && I.refresh.skip > 0) return true;
    for (const n of (clip && clip.notes) || []) if (n.sample && n.sample.start && n.sample.start.mode === 'random') return true;
    return false;
  }
  // -> { ruleId: { faded, notes, reason } }. notes = how many notes use it when the instrument itself does not.
  function fades(I, clip) {
    const cx = contexts(I, clip), ic = cx[0];
    let multi = null;
    const out = {};
    for (const id of Object.keys(RULES)) {
      const R = RULES[id];
      let instUses, notes = 0;
      if (R.inst) instUses = !!R.inst(I);
      else if (R.both) instUses = !!R.both(I, clip);
      else if (R.multi) { if (multi === null) multi = headsMulti(I, clip); instUses = multi; }
      else {
        instUses = !!R.uses(ic);
        if (!instUses) for (let k = 1; k < cx.length; k++) if (R.uses(cx[k])) notes += cx[k].count;
      }
      out[id] = { faded: !instUses && notes === 0, notes: instUses ? 0 : notes, reason: instUses ? '' : (typeof R.why === 'function' ? R.why(ic) : R.why) };
    }
    return out;
  }
  // Which grain setting the Grain knob shows: the instrument's engine; in Tape, the grain engine most notes
  // use on their own (Note settings); null when nothing plays grains.
  function grainKind(I, clip) {
    const cx = contexts(I, clip);
    if (cx[0].kind !== 'tape') return cx[0].kind;
    const n = { voice: 0, cloud: 0, lock: 0 };
    for (let k = 1; k < cx.length; k++) if (cx[k].kind !== 'tape') n[cx[k].kind] += cx[k].count;
    const best = Object.keys(n).sort((a, b) => n[b] - n[a])[0];
    return n[best] > 0 ? best : null;
  }

  // =============================================================================================== readouts
  // Macros (BLUEPRINT 1.4): Speed x2^((m-0.5)*4); Spread +(m-0.5)*2; Heads +(m-0.5)*14; Refresh period / 2^((m-0.5)*6);
  // Grain x2^((m-0.5)*4); Spray +(m-0.5)*2; Tone lane +(m-0.5) (cutoff 8.3 octaves per lane unit); Formant lane +(m-0.5) (24 st per unit).
  function macroText(i, m) {
    const d = num(m, 0.5) - 0.5;
    if (Math.abs(d) < 0.0025) return 'no change';
    const times = (f, up, down) => (f >= 1 ? trimZ(f.toPrecision(3)) + 'x ' + up : trimZ((1 / f).toPrecision(3)) + 'x ' + down);
    switch (i) {
      case 0: return times(Math.pow(2, d * 4), 'faster', 'slower');
      case 1: return signed(d * 200, 0) + '% spread';
      case 2: return signed(d * 14, 1) + ' heads';
      case 3: return times(Math.pow(2, d * 6), 'faster', 'slower');
      case 4: return times(Math.pow(2, d * 4), 'longer', 'shorter');
      case 5: return signed(d * 200, 0) + '% spray';
      case 6: return signed(d * 8.3, 1) + ' oct ' + (d > 0 ? 'brighter' : 'darker');
      case 7: return fmtSt(d * 24) + ' formant';
    }
    return String(m);
  }
  // Whole-clip offsets (A3): -1..1 added to every note's lane value.
  function offsetText(k, o) {
    o = num(o, 0);
    if (Math.abs(o) < 0.0025) return 'no change';
    switch (k) {
      case 'amp': {
        // in dB for a note at the default Volume (0.8 on the lane; gain = v squared): the lane stops at its top,
        // so raising it adds at most +3.9 dB; lowering it to the bottom is silence
        const v0 = 0.8, v = clamp(v0 + o, 0, 1);
        if (v <= 0) return 'silent';
        const db = 40 * Math.log10(v / v0), r = Math.round(db * 10) / 10;
        return (r > 0 ? '+' : r < 0 ? '-' : '') + Math.abs(r).toFixed(1) + ' dB';
      }
      case 'vib': return signed(o, 2) + (Math.abs(Number(o.toFixed(2))) === 1 ? ' semitone' : ' semitones') + ' of vibrato';
      case 'tone': return signed(o * 8.3, 1) + ' oct ' + (o > 0 ? 'brighter' : 'darker');
      case 'formant': return fmtSt(o * 24);
      case 'hollow': return (Math.abs(o) * 28).toFixed(1) + ' dB ' + (o > 0 ? 'more hollow' : 'less hollow');
    }
    return String(o);
  }

  // =============================================================================================== suggestions
  // E3: one engine suggestion after a file loads. info = { pitch: bool, seconds, parts }.
  function suggestEngine(info) {
    const secs = num(info && info.seconds, 0), parts = (info && info.parts) || [];
    const beat = parts.some(p => p && p.kind === 'loop' && p.tempoFound);
    if (!(info && info.pitch)) {
      if (secs <= 20 && beat) return { engine: 'tape', text: 'Try Tape', why: 'A beat or loop with no single pitch: Tape keeps its punch.' };
      return { engine: 'cloud', text: 'Try Cloud', why: 'No single pitch (a chord, a song or a texture): Cloud plays it best. Switch Lock on to play it in tune.' };
    }
    if (secs > 20) return { engine: 'cloud', text: 'Try Cloud', why: 'A long recording: Cloud copes with whatever is in it.' };
    return { engine: 'voice', text: 'Try Voice', why: 'One clear pitch: Voice keeps its character as you play it up and down.' };
  }
  // U2: the part a long file opens on (a held note first, then a loop, a quiet texture, a hit).
  function pickPart(parts) {
    const list = (parts || []).filter(p => p && p.end > p.start);
    for (const k of ['steady', 'loop', 'quiet', 'hit']) { const p = list.find(q => q.kind === k); if (p) return p; }
    return list[0] || null;
  }
  const regionIsWhole = R => R.start <= 0.0005 && R.end >= 0.9995;
  const sameRegion = (R, p) => Math.abs(R.start - p.start) < 1e-4 && Math.abs(R.end - p.end) < 1e-4;

  const pure = { contexts, fades, grainKind, headsMulti, toneAllOpen, randomInUse, macroText, offsetText, suggestEngine, pickPart, parseNum, parseMs, parsePct, KNOB_MATH, RULES, kindOf };
  if (typeof window === 'undefined' || typeof document === 'undefined' || !window.SLApp) return pure;

  // =============================================================================================== UI
  const A = window.SLApp, AN = window.SLAnalysis || null, SND = window.SLSounds || null;
  const el = A.el, $ = id => document.getElementById(id);
  const ENGINE_NAME = { tape: 'Tape', voice: 'Voice', cloud: 'Cloud' };
  const MOTION_NAME = { forward: 'Forward', backward: 'Backward', bounce: 'Bounce', mirror: 'Mirror', freeze: 'Freeze', scatter: 'Scatter' };
  const PIN_NAME = { engine: 'Engine', lock: 'Lock', motion: 'Motion', speed: 'Speed', heads: 'Heads', spread: 'Spread', grain: 'Grain', refresh: 'Refresh',
    spray: 'Spray', tape: 'Tape switch', rev: 'Each grain backwards', drift: 'Drift', headTune: 'Head tune', jump: 'Jump every' };

  // ---- storage (optional; a memory copy keeps things working for this visit when the browser blocks it)
  const PIN_KEY = 'strandlock_v4_pins', MINE_KEY = 'strandlock_v4_mypresets';
  const memStore = {};
  function sget(k) {
    try { const s = window.localStorage.getItem(k); if (s != null) return JSON.parse(s); } catch (e) { /* blocked */ }
    return Object.prototype.hasOwnProperty.call(memStore, k) ? memStore[k] : null;
  }
  function sset(k, v) {
    memStore[k] = v;
    try { window.localStorage.setItem(k, JSON.stringify(v)); return true; } catch (e) { return false; }
  }

  // ---- hover help: the hint line shows what a control does (and why it is faded)
  function helpOn(node, textFn) {
    node.addEventListener('mouseenter', () => { const t = textFn(); if (t) A.setHint(t); });
    node.addEventListener('mouseleave', () => A.setHint(null));
  }
  const controls = [];   // every control: { sync(I, F) }
  let F = null, fKey = { I: null, C: null };
  function currentFades() {
    const I = A.getInst(), C = A.getClip();
    if (!F || fKey.I !== I || fKey.C !== C) { F = fades(I, C); F._grainKind = grainKind(I, C); fKey = { I, C }; }
    return F;
  }
  function fadeTitle(base, st) {
    if (!st) return base;
    if (st.faded) return 'Does nothing now: ' + st.reason + '\n\n' + base;
    if (st.notes) return base + '\n\n' + st.notes + (st.notes === 1 ? ' note uses' : ' notes use') + ' it through its own settings (Note settings).';
    return base;
  }
  function setAttrIf(node, k, v) { if (node.getAttribute(k) !== v) node.setAttribute(k, v); }

  // ---- pins for Dice
  let pins = new Set((sget(PIN_KEY) || []).filter(f => M.DICE_FIELDS.includes(f)));
  // a small push-pin drawn in the text colour (an emoji would keep its own colours in both themes)
  function pinIcon() {
    const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    s.setAttribute('viewBox', '0 0 16 16'); s.setAttribute('width', '10'); s.setAttribute('height', '10'); s.setAttribute('aria-hidden', 'true');
    const p = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    p.setAttribute('d', 'M5 1h6l-1 5 3 3H9.2L8 15l-1.2-6H3l3-3z'); p.setAttribute('fill', 'currentColor');
    s.appendChild(p);
    return s;
  }
  const pinButtons = [];
  function pinBtn(field) {
    const b = el('button', { type: 'button', class: 'sls-pin', 'aria-pressed': 'false', 'aria-label': 'Pin ' + PIN_NAME[field] }, pinIcon());
    b.addEventListener('click', e => { e.stopPropagation(); if (pins.has(field)) pins.delete(field); else pins.add(field); sset(PIN_KEY, [...pins]); syncPins(); });
    pinButtons.push({ b, field });
    return b;
  }
  function syncPins() {
    for (const { b, field } of pinButtons) {
      const on = pins.has(field);
      b.classList.toggle('on', on);
      setAttrIf(b, 'aria-pressed', on ? 'true' : 'false');
      setAttrIf(b, 'title', on ? PIN_NAME[field] + ' is pinned: Dice leaves it alone. Click to unpin.' : 'Pin ' + PIN_NAME[field] + ' so Dice leaves it alone');
    }
    if (diceInfo) {
      diceInfo.textContent = pins.size ? 'Pinned: ' + M.DICE_FIELDS.filter(f => pins.has(f)).map(f => PIN_NAME[f]).join(', ')
        : 'Pin a setting (the small pin next to its name) to keep it when you roll.';
      unpinBtn.hidden = !pins.size;
    }
  }

  // ---- knob
  const SVGNS = 'http://www.w3.org/2000/svg';
  function svg(tag, attrs) { const e = document.createElementNS(SVGNS, tag); for (const k of Object.keys(attrs || {})) e.setAttribute(k, String(attrs[k])); return e; }
  const A0 = 135, SWEEP = 270, KR = 15, KC = 20;
  const kpt = (deg, r) => { const a = deg * Math.PI / 180; return [KC + r * Math.cos(a), KC + r * Math.sin(a)]; };
  function arcD(n0, n1) {
    const a = A0 + SWEEP * Math.min(n0, n1), b = A0 + SWEEP * Math.max(n0, n1);
    if (b - a < 0.6) return 'M0 0';
    const p0 = kpt(a, KR), p1 = kpt(b, KR);
    return 'M' + p0[0].toFixed(2) + ' ' + p0[1].toFixed(2) + ' A' + KR + ' ' + KR + ' 0 ' + (b - a > 180 ? 1 : 0) + ' 1 ' + p1[0].toFixed(2) + ' ' + p1[1].toFixed(2);
  }
  /* spec: { label, title, color ('var(--...)'), min, max, log, def, bipolar, get(I), patch(v, I) -> inst patch, fmt(v, I),
             q(v, fine), parse(text) | null, tmin, tmax (typed range), rule (fade id) | ruleFn(F) -> state, pin, undo,
             dyn(I, F) -> spec overrides (or null = off), offWhy } */
  function knob(sp) {
    const dial = svg('svg', { viewBox: '0 0 40 40', class: 'sls-dial', role: 'slider', tabindex: '0', 'aria-label': sp.label, 'aria-valuemin': 0, 'aria-valuemax': 1 });
    dial.appendChild(svg('path', { class: 'trk', d: arcD(0, 1), fill: 'none', 'stroke-width': 4, 'stroke-linecap': 'round' }));
    const arc = svg('path', { class: 'arc', d: 'M0 0', fill: 'none', 'stroke-width': 4, 'stroke-linecap': 'round' });
    const cap = svg('circle', { class: 'cap', cx: KC, cy: KC, r: 10.5 });
    const dot = svg('circle', { class: 'dot', cx: KC, cy: KC - 7, r: 2.2 });
    dial.appendChild(arc); dial.appendChild(cap); dial.appendChild(dot);
    if (sp.bipolar) { const [mx, my] = kpt(A0 + SWEEP * 0.5, KR + 3.4); dial.appendChild(svg('circle', { class: 'mid', cx: mx.toFixed(2), cy: my.toFixed(2), r: 1.1 })); }
    const lbl = el('span', { class: 'sls-klbl' }, el('span', null, sp.label));
    if (sp.pin) lbl.appendChild(pinBtn(sp.pin));
    const val = el('span', { class: 'sls-kval' });
    const root = el('div', { class: 'sls-knob' }, lbl, dial, val);
    root.style.setProperty('--k-col', sp.color || 'var(--lane-sample)');
    let cur = sp, state = null, off = false, drag = null, lastText = '', lastD = '', editing = false, lastFs = null;
    const resolve = (I, Fs) => {
      if (!sp.dyn) return sp;
      const o = sp.dyn(I, Fs);
      return o ? Object.assign({}, sp, o) : null;
    };
    function sync(I, Fs) {
      lastFs = Fs;
      const s = resolve(I, Fs);
      off = !s;
      cur = s || sp;
      state = sp.ruleFn ? sp.ruleFn(Fs) : sp.rule ? Fs[sp.rule] : null;
      const faded = off || !!(state && state.faded);
      root.classList.toggle('sls-faded', faded);
      root.classList.toggle('sls-off', off);
      const title = off ? 'Does nothing now: ' + (sp.offWhy || '') + '\n\n' + (sp.title || '') : fadeTitle(cur.title || sp.title || '', state);
      setAttrIf(root, 'title', title);
      if (editing) return;
      if (off) {
        if (lastText !== '—') { val.textContent = '—'; lastText = '—'; }
        if (lastD !== 'off') { arc.setAttribute('d', 'M0 0'); lastD = 'off'; }
        setAttrIf(dial, 'aria-valuetext', 'not used');
        return;
      }
      const v = cur.get(I), n = toNorm(cur, v), n0 = cur.bipolar ? toNorm(cur, cur.def) : 0;
      const d = arcD(n0, n);
      if (d !== lastD) {
        arc.setAttribute('d', d); lastD = d;
        const p = kpt(A0 + SWEEP * n, 7);
        dot.setAttribute('cx', p[0].toFixed(2)); dot.setAttribute('cy', p[1].toFixed(2));
      }
      const t = cur.fmt(v, I);
      if (t !== lastText) { val.textContent = t; lastText = t; }
      setAttrIf(dial, 'aria-valuetext', t);
      setAttrIf(dial, 'aria-valuenow', String(Math.round(n * 1000) / 1000));
    }
    function set(v, label) {
      const I = A.getInst();
      if (off) return;
      if (v === cur.get(I)) return;
      A.patchInst(cur.patch(v, I), label || null);
    }
    dial.addEventListener('pointerdown', e => {
      if (e.button !== 0) return;
      e.preventDefault();
      if (off) { A.toast(sp.offWhy || 'Not used with these settings', 3500); return; }
      try { dial.setPointerCapture(e.pointerId); } catch (err) { /* ok */ }
      const v0 = cur.get(A.getInst());
      drag = { y0: e.clientY, n0: toNorm(cur, v0), v0, last: v0, moved: false };
      root.classList.add('sls-drag');
    });
    dial.addEventListener('pointermove', e => {
      if (!drag) return;
      const dy = drag.y0 - e.clientY;
      if (!drag.moved && Math.abs(dy) < 2) return;
      const n = clamp(drag.n0 + dy / 170 * (e.shiftKey ? 0.12 : 1), 0, 1);
      const v = clamp(cur.q(fromNorm(cur, n), e.shiftKey), cur.min, cur.max);
      // the knob under the mouse shows its value at once; the rest of the panel follows once per frame
      if (v !== drag.last) { drag.last = v; drag.moved = true; set(v, null); if (lastFs) sync(A.getInst(), lastFs); }
    });
    const end = () => {
      if (!drag) return;
      const d = drag;
      drag = null;
      root.classList.remove('sls-drag');
      try { dial.blur(); } catch (err) { /* ok */ }
      if (d.moved && d.last !== d.v0) A.commit(sp.undo || sp.label);
    };
    dial.addEventListener('pointerup', end);
    dial.addEventListener('pointercancel', end);
    dial.addEventListener('lostpointercapture', end);
    dial.addEventListener('dblclick', e => {
      e.preventDefault();
      if (off) return;
      set(cur.def, 'Reset ' + (sp.undo || sp.label));
    });
    dial.addEventListener('keydown', e => {
      // only reachable by Tab (a mouse click lets go of the focus), never the spacebar
      if (off || !['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) return;
      e.preventDefault(); e.stopPropagation();
      const I = A.getInst(), n = toNorm(cur, cur.get(I));
      const step = e.shiftKey ? 0.01 : 0.05, dir = e.key === 'ArrowUp' || e.key === 'ArrowRight' ? 1 : -1;
      const nn = e.key === 'Home' ? 0 : e.key === 'End' ? 1 : n + dir * step;
      set(clamp(cur.q(fromNorm(cur, nn), true), cur.min, cur.max), sp.undo || sp.label);
    });
    // click the value to type one
    val.addEventListener('click', () => {
      if (off || !cur.parse || editing) return;
      editing = true;
      const inp = el('input', { type: 'text', class: 'sls-kin', value: cur.fmt(cur.get(A.getInst()), A.getInst()), 'aria-label': sp.label });
      val.replaceWith(inp);
      inp.focus(); inp.select();
      let done = false;
      const finish = ok => {
        if (done) return;
        done = true; editing = false;
        inp.replaceWith(val);
        if (ok) {
          const v = cur.parse(inp.value);
          if (v == null || !isFinite(v)) A.toast('Type a number, for example ' + (cur.example || cur.fmt(cur.def, A.getInst())));
          else set(clamp(r6(v), num(cur.tmin, cur.min), num(cur.tmax, cur.max)), sp.undo || sp.label);
        }
        lastText = ''; sync(A.getInst(), currentFades());
      };
      inp.addEventListener('keydown', e => { e.stopPropagation(); if (e.key === 'Enter') { e.preventDefault(); finish(true); } else if (e.key === 'Escape') { e.preventDefault(); finish(false); } });
      inp.addEventListener('blur', () => finish(true));
    });
    helpOn(root, () => {
      if (off) return sp.label + ': does nothing now. ' + (sp.offWhy || '');
      const base = (cur.title || sp.title || '').split('\n')[0];
      const st = state;
      const tail = ' Drag up or down (Shift = fine), double-click = reset' + (cur.parse ? ', click the value to type it.' : '.');
      if (st && st.faded) return sp.label + ': does nothing now. ' + st.reason;
      return sp.label + ': ' + base + tail;
    });
    const c = { root, sync };
    controls.push(c);
    return root;
  }

  // ---- segmented buttons, toggles, selects
  function group(label, pin, parts, extra) {
    const l = el('span', { class: 'lbl' }, label);
    if (pin) l.appendChild(pinBtn(pin));
    return el('div', Object.assign({ class: 'sls-g' }, extra || {}), l, parts);
  }
  /* seg spec: { label, pin, title, rule, opts:[{v, text, title, rule}], get(I), set(v) } */
  function seg(sp) {
    const btns = sp.opts.map(o => {
      const b = el('button', { type: 'button', title: o.title || null }, o.text);
      b.addEventListener('click', () => sp.set(o.v));
      helpOn(b, () => {
        const Fs = currentFades(), st = o.rule ? Fs[o.rule] : sp.rule ? Fs[sp.rule] : null;
        return (st && st.faded ? sp.label + ' ' + o.text + ': does nothing now. ' + st.reason : o.text + ': ' + (o.title || ''));
      });
      return b;
    });
    const wrap = group(sp.label, sp.pin, el('div', { class: 'sls-seg', role: 'group', 'aria-label': sp.label }, btns));
    if (sp.title) wrap.firstChild.title = sp.title;
    controls.push({ sync(I, Fs) {
      const v = sp.get(I);
      const gst = sp.rule ? Fs[sp.rule] : null;
      sp.opts.forEach((o, i) => {
        const b = btns[i];
        b.classList.toggle('on', v === o.v);
        const st = o.rule ? Fs[o.rule] : null;
        b.classList.toggle('sls-faded', !!(st && st.faded));
        setAttrIf(b, 'title', fadeTitle(o.title || '', st || gst));
      });
      wrap.classList.toggle('sls-faded', !!(gst && gst.faded));
      if (sp.rule) setAttrIf(wrap, 'title', gst && gst.faded ? 'Does nothing now: ' + gst.reason : gst && gst.notes ? fadeTitle('', gst).trim() : '');
    } });
    return wrap;
  }
  /* toggle spec: { text, title, pin, rule, get(I), set() } */
  function toggle(sp) {
    const b = el('button', { type: 'button', 'aria-pressed': 'false' }, sp.text);
    b.addEventListener('click', () => sp.set());
    const wrap = sp.pin ? el('span', { class: 'sls-g' }, b, pinBtn(sp.pin)) : b;
    helpOn(b, () => { const st = sp.rule ? currentFades()[sp.rule] : null; return st && st.faded ? sp.text + ': does nothing now. ' + st.reason : sp.text + ': ' + sp.title; });
    controls.push({ sync(I, Fs) {
      const on = !!sp.get(I);
      b.classList.toggle('on', on);
      setAttrIf(b, 'aria-pressed', on ? 'true' : 'false');
      const st = sp.rule ? Fs[sp.rule] : null;
      b.classList.toggle('sls-faded', !!(st && st.faded));
      setAttrIf(b, 'title', fadeTitle(sp.title, st));
    } });
    return wrap;
  }
  /* rate select spec: { label, pin, title, rule, get(I), set(id) } */
  function rateSel(sp) {
    const s = el('select', { 'aria-label': sp.label });
    A.rateOptions(s, sp.get(A.getInst()));
    s.addEventListener('change', () => sp.set(s.value));
    const wrap = group(sp.label, sp.pin, s);
    helpOn(wrap, () => { const st = sp.rule ? currentFades()[sp.rule] : null; return st && st.faded ? sp.label + ': does nothing now. ' + st.reason : sp.label + ': ' + sp.title; });
    controls.push({ sync(I, Fs) {
      const v = sp.get(I);
      if (s.value !== v) s.value = v;
      const st = sp.rule ? Fs[sp.rule] : null;
      wrap.classList.toggle('sls-faded', !!(st && st.faded));
      setAttrIf(wrap, 'title', fadeTitle(sp.title, st));
    } });
    return wrap;
  }

  // =============================================================================================== Sample tab
  function setEngine(v) {
    const I = A.getInst();
    if (I.engine === v) return;
    A.patchInst({ engine: v }, 'Engine: ' + ENGINE_NAME[v]);
  }
  function toggleLock() {
    const I = A.getInst();
    if (I.engine !== 'cloud') { A.patchInst({ engine: 'cloud', lock: true }, 'Engine: Cloud with Lock'); A.toast('Lock works with Cloud: switched to Cloud with Lock'); return; }
    A.patchInst({ lock: !I.lock }, I.lock ? 'Lock off' : 'Lock on');
  }
  function setMotion(v) {
    const I = A.getInst();
    if (I.motion === v) return;
    const patch = { motion: v };
    let label = 'Motion: ' + MOTION_NAME[v];
    if (v === 'mirror') {   // S2: Mirror with 1 head sets Heads to 2 in the same click
      const need = 2 - (I.macros[2] - 0.5) * 14;
      if (I.heads + 1e-9 < need) { patch.heads = clamp(Math.ceil(need * 100) / 100, 1, 8); label += ' (2 heads)'; A.toast('Mirror needs 2 heads or more: Heads set to 2'); }
    }
    if (v === 'freeze' && I.engine === 'tape') A.toast('Freeze does nothing in Tape (it plays forwards): pick Voice or Cloud to freeze', 4000);
    A.patchInst(patch, label);
  }
  // Y7: Scatter jumps, Spray, Drift, Skip and random Starts stay the same on every loop; Re-roll deals new ones
  // (inst.seed + 1, one undo step; Undo brings the previous roll back)
  function rerollInst() {
    const I = A.getInst(), next = ((I.seed | 0) + 1) | 0;
    A.patchInst({ seed: next }, 'Re-roll Scatter, Spray, Drift and Skip');
    A.toast('Re-rolled: new Scatter jumps, Spray, Drift and Skip (roll ' + next + '). They stay like this until you Re-roll again.', 3500);
  }
  function rerollBtn() {
    const b = el('button', { type: 'button', class: 'sls-reroll' }, '\u21bb Re-roll');
    const base = 'New random spots for Scatter jumps, Spray, Drift, Skip and random Starts (one undo step). They stay the same on every loop until you Re-roll again.';
    b.addEventListener('click', rerollInst);
    helpOn(b, () => { const st = currentFades().reroll; return 'Re-roll: ' + (st && st.faded ? 'does nothing now. ' + st.reason : base); });
    controls.push({ sync(I, Fs) {
      const st = Fs.reroll;
      b.classList.toggle('sls-faded', !!(st && st.faded));
      setAttrIf(b, 'title', fadeTitle(base, st) + '\n\nRoll ' + (I.seed | 0));
    } });
    return b;
  }
  let sugLine = null, sugText = null, sugBtn = null;
  function buildSampleTab() {
    const box = $('sampleTab');
    if (!box) return;
    box.innerHTML = '';
    // E3 engine suggestion (hidden until a file suggests one)
    sugText = el('span', null);
    sugBtn = el('button', { type: 'button', onclick: acceptSuggestion }, 'Use it');
    sugLine = el('div', { class: 'sls-sugline', hidden: true, role: 'status' }, el('span', null, '\u{1F4A1}'), sugText, sugBtn,
      el('button', { type: 'button', title: 'Hide this suggestion', onclick: () => { if (suggestion) suggestion.done = true; syncSuggestion(); } }, '✕'));
    box.appendChild(sugLine);
    // engine and motion
    box.appendChild(el('div', { class: 'sls-row' },
      el('div', { class: 'sls-g' },
        seg({ label: 'Engine', pin: 'engine', get: I => I.engine, set: setEngine, opts: [
          { v: 'tape', text: 'Tape', title: 'Speed and pitch move together, like tape: backwards sounds reversed and slower drops the pitch.' },
          { v: 'voice', text: 'Voice', title: 'Stays in tune with the key you play and keeps the sound\'s character, slowed down, frozen or backwards. Best on one clear note.' },
          { v: 'cloud', text: 'Cloud', title: 'Grains that work on anything: chords, songs, noise. Switch Lock on to play any of it in tune.' }] }),
        toggle({ text: '\u{1F512} Lock', pin: 'lock', rule: 'lock', title: 'Cloud only: each grab repeats one wave of the note you play, so anything plays exactly in tune (GrainLock\'s trick).',
          get: I => I.engine === 'cloud' && I.lock, set: toggleLock })),
      seg({ label: 'Motion', pin: 'motion', get: I => I.motion, set: setMotion, opts: [
        { v: 'forward', text: '▶ Forward', title: 'The heads play forwards through the Region.' },
        { v: 'backward', text: '◀ Backward', title: 'The heads play backwards.' },
        { v: 'bounce', text: '⇄ Bounce', title: 'The heads turn round at the edges of the Region.' },
        { v: 'mirror', text: '↔ Mirror', title: 'Every other head plays backwards (needs 2 heads or more; picking it with 1 head sets Heads to 2).' },
        { v: 'freeze', text: '❄ Freeze', rule: 'freeze', title: 'The heads hold still where the note starts.' },
        { v: 'scatter', text: '⁙ Scatter', title: 'Each head jumps to a new spot every ‘Jump every’, locked to the beat.' }] }),
      rateSel({ label: 'Jump every', pin: 'jump', rule: 'jump', title: 'How often Scatter jumps, locked to the beat.',
        get: I => I.jump, set: id => A.patchInst({ jump: id }, 'Jump every ' + M.rateLabel(id)) }),
      rerollBtn()));
    // knobs (the last two belong to Refresh)
    const sand = 'var(--lane-sample)';
    box.appendChild(el('div', { class: 'sls-row sls-knobs' },
      knob({ label: 'Speed', pin: 'speed', rule: 'speed', color: sand, min: 0.125, max: 8, log: true, def: 1, tmin: 0, tmax: 16, undo: 'Speed',
        title: 'How fast the heads move through the sound: 1x = as recorded, 1/8x to 8x. Double-click = 1x.',
        get: I => I.speed, patch: v => ({ speed: v }), fmt: fmtX, q: qLog([0.25, 0.5, 1, 2, 4]), parse: parseNum, example: '0.5' }),
      knob({ label: 'Heads', pin: 'heads', color: sand, min: 1, max: 8, def: 1, undo: 'Heads',
        title: 'How many playheads read the sound at once, 1 to 8. 2.5 = two heads plus a third at half level. More heads don\'t get louder.',
        get: I => I.heads, patch: v => ({ heads: v }), fmt: fmtHeads, q: qLin(0.05, 0.01, [1, 2, 3, 4, 5, 6, 7, 8], 0.08), parse: parseNum, example: '2.5' }),
      knob({ label: 'Spread', pin: 'spread', rule: 'spread', color: sand, min: 0, max: 1, def: 0.25, undo: 'Spread',
        title: 'Spaces the heads out through the Region and across left and right.',
        get: I => I.spread, patch: v => ({ spread: v }), fmt: fmtPct, q: qLin(0.01, 0.001), parse: parsePct, example: '25' }),
      knob({ label: 'Grain', pin: 'grain', rule: 'grain', color: sand, min: 0.25, max: 16, log: true, def: 2, undo: 'Grain',
        offWhy: GRAIN_WHY, title: 'How long each grain is.',
        get: I => I.grainVoice, patch: v => ({ grainVoice: v }), fmt: fmtCycles, q: qLog([0.5, 1, 2, 4, 8]), parse: parseNum,
        dyn: (I, Fs) => {
          const k = Fs._grainKind;
          if (!k) return null;
          if (k === 'voice') return { min: 0.25, max: 16, log: true, def: 2, title: 'Voice: each grain is this many cycles (waves) of the sound\'s root note long. Shorter = buzzier, longer = smoother.',
            get: I2 => I2.grainVoice, patch: v => ({ grainVoice: v }), fmt: fmtCycles, q: qLog([0.5, 1, 2, 4, 8]), example: '2' };
          if (k === 'cloud') return { min: 5, max: 2000, log: true, def: 140, title: 'Cloud: each grain is this long. Short grains buzz, long grains smear.',
            get: I2 => I2.grainCloud, patch: v => ({ grainCloud: v }), fmt: v => Math.round(v) + ' ms', q: (v, fine) => (fine ? Math.round(v * 10) / 10 : magnetLog(Math.round(v), [20, 50, 100, 140, 200, 500, 1000], 0.04)), parse: parseMs, tmin: 5, tmax: 2000, example: '140' };
          return { min: 1, max: 12, log: false, def: 4, title: 'Cloud with Lock: each grab is this many waves (wavelengths) of the note you play. More waves = smoother, fewer = more detail.',
            get: I2 => I2.grainLock, patch: v => ({ grainLock: v }), fmt: v => { const t = trimZ(v.toFixed(1)); return t + (t === '1' ? ' wave' : ' waves'); }, q: qLin(1, 0.5), parse: parseNum, tmin: 1, tmax: 16, example: '4' };
        } }),
      knob({ label: 'Spray', pin: 'spray', rule: 'spray', color: sand, min: 0, max: 1, def: 0, undo: 'Spray',
        title: 'Scatters each grain a little around its head (Cloud).',
        get: I => I.spray, patch: v => ({ spray: v }), fmt: fmtPct, q: qLin(0.01, 0.001), parse: parsePct, example: '20' }),
      knob({ label: 'Drift', pin: 'drift', rule: 'drift', color: sand, min: 0, max: 1, def: 0, undo: 'Drift',
        title: 'Gives each head a slightly different speed, so they wander apart (and with Bounce, come back together).',
        get: I => I.drift, patch: v => ({ drift: v }), fmt: fmtPct, q: qLin(0.01, 0.001), parse: parsePct, example: '30' }),
      knob({ label: 'Pitch', color: 'var(--lane-pitch)', min: -24, max: 24, def: 0, bipolar: true, tmin: -48, tmax: 48, undo: 'Pitch',
        title: 'Transposes the whole instrument, in semitones (Shift = fine).',
        get: I => I.pitch, patch: v => ({ pitch: v }), fmt: fmtSt, q: qLin(1, 0.01), parse: parseNum, example: '-12' }),
      el('span', { class: 'sep sls-ksep' }),
      knob({ label: 'Refresh time', rule: 'refreshMs', color: sand, min: 5, max: 2000, log: true, def: 120, undo: 'Refresh time',
        title: 'Free Refresh: the time between fresh grabs, 5 ms to 2 s.',
        get: I => I.refresh.ms, patch: v => ({ refresh: { ms: v } }), fmt: fmtMs, q: (v, fine) => (fine ? Math.round(v * 10) / 10 : magnetLog(Math.round(v), [10, 20, 50, 100, 120, 250, 500, 1000], 0.04)), parse: parseMs, example: '120' }),
      knob({ label: 'Skip', rule: 'skip', color: sand, min: 0, max: 1, def: 0, undo: 'Skip',
        title: 'Refresh: the chance that a fresh grab is skipped, as in GrainLock. It skips the same grabs on every loop until you Re-roll.',
        get: I => I.refresh.skip, patch: v => ({ refresh: { skip: v } }), fmt: fmtPct, q: qLin(0.01, 0.001), parse: parsePct, example: '25' })));
    // refresh mode, switches, head tune, at the end
    box.appendChild(el('div', { class: 'sls-row' },
      el('div', { class: 'sls-g' },
        seg({ label: 'Refresh', pin: 'refresh', title: 'How often a head takes a fresh grab of the sound; in between, the last grab repeats. In Tape it repeats the last chunk (stutter, beat repeat).',
          get: I => I.refresh.mode, set: v => { const I = A.getInst(); if (I.refresh.mode !== v) A.patchInst({ refresh: { mode: v } }, 'Refresh: ' + ({ smooth: 'Smooth', free: 'Free', sync: 'Synced' })[v]); },
          opts: [
            { v: 'smooth', text: 'Smooth', title: 'Always fresh: the heads read the sound continuously.' },
            { v: 'free', text: 'Free', title: 'A fresh grab every Refresh time (5 ms to 2 s).' },
            { v: 'sync', text: 'Synced', title: 'A fresh grab every note value (1 bar to 1/64, triplet or dotted).' }] }),
        rateSel({ label: 'Every', rule: 'refreshRate', title: 'Synced Refresh: a fresh grab every this note value.',
          get: I => I.refresh.rate, set: id => A.patchInst({ refresh: { rate: id } }, 'Refresh every ' + M.rateLabel(id)) }),
        toggle({ text: 'Lock to the beat', rule: 'refreshRate', title: 'Synced Refresh lines up with the song\'s beat (off = counts from each note\'s start).',
          get: I => I.refresh.lockToBeat, set: () => { const I = A.getInst(); A.patchInst({ refresh: { lockToBeat: !I.refresh.lockToBeat } }, I.refresh.lockToBeat ? 'Refresh: free of the beat' : 'Refresh: locked to the beat'); } })),
      toggle({ text: 'Tape', pin: 'tape', rule: 'tape', title: 'Pitch follows speed in Voice and Cloud too: grains read faster or slower with the heads.',
        get: I => I.tape, set: () => { const I = A.getInst(); A.patchInst({ tape: !I.tape }, I.tape ? 'Tape switch off' : 'Tape switch on'); } }),
      toggle({ text: 'Each grain backwards', pin: 'rev', rule: 'rev', title: 'Plays what is inside each grain backwards, whichever way the heads travel.',
        get: I => I.rev, set: () => { const I = A.getInst(); A.patchInst({ rev: !I.rev }, I.rev ? 'Each grain backwards off' : 'Each grain backwards on'); } }),
      seg({ label: 'Head tune', pin: 'headTune', rule: 'headTune', get: I => I.headTune,
        set: v => { const I = A.getInst(); if (I.headTune !== v) A.patchInst({ headTune: v }, 'Head tune: ' + ({ same: 'Same', detune: 'Detune', octaves: 'Octaves', fifths: 'Fifths' })[v]); },
        opts: [
          { v: 'same', text: 'Same', title: 'Every head plays the same pitch.' },
          { v: 'detune', text: 'Detune', title: 'A gentle detune between the heads.' },
          { v: 'octaves', text: 'Octaves', title: 'Heads an octave apart.' },
          { v: 'fifths', text: 'Fifths', title: 'Heads a fifth and an octave apart.' }] }),
      seg({ label: 'At the end', rule: 'atEnd', get: I => I.region.atEnd,
        set: v => { const I = A.getInst(); if (I.region.atEnd !== v) A.patchInst({ region: { atEnd: v } }, 'At the end: ' + ({ loop: 'Loop', hold: 'Hold', stop: 'Stop' })[v]); },
        opts: [
          { v: 'loop', text: 'Loop', title: 'At the end of the Region, go round again (with a short crossfade).' },
          { v: 'hold', text: 'Hold', title: 'At the end of the Region, stay on the last bit.' },
          { v: 'stop', text: 'Stop', title: 'At the end of the Region, go quiet.' }] })));
  }

  // =============================================================================================== Recipes and Dice
  const RECIPE_TIPS = {
    1: 'Plays the sound straight through with one head (Voice).',
    2: 'Plays backwards.',
    3: 'Holds still with 4 slightly detuned heads.',
    4: 'Tape: the heads rock forwards and backwards every 1/8.',
    5: 'Tape: repeats the last 1/16 like a beat repeat.',
    6: 'Cloud at half speed: the heads grow from 1 to more across each note.',
    7: 'Cloud: 3 heads jump to new spots every 1/8 triplet.',
    8: 'Cloud with Lock: 3 heads an octave apart, in tune on anything.',
  };
  let recipeScope = null, diceInfo = null, unpinBtn = null, rolls = 0;
  function recipeIds() { const ids = A.getSelection(); return ids.length ? ids : null; }
  // one undo step that changes both the notes and the instrument
  function commitBoth(clip, inst, label) {
    const cur = A.getClip();
    const same = clip === cur || (clip.bars === cur.bars && clip.notes.length === cur.notes.length && clip.notes.every((n, i) => n === cur.notes[i]));
    if (same) { A.setInst(inst, label); return; }
    A.setInst(inst, null);
    A.setClip(clip, label);
  }
  function applyRecipeN(R) {
    const ids = recipeIds();
    let r;
    try { r = M.applyRecipe(A.getClip(), A.getInst(), R.n, ids); } catch (e) { console.warn('StrandLock: recipe failed', e); A.toast('That recipe could not be applied'); return; }
    commitBoth(r.clip, r.inst, 'Recipe ' + R.n + ' ' + R.name);
    const sel = ids ? 'the ' + ids.length + (ids.length === 1 ? ' selected note' : ' selected notes') : 'every note';
    A.toast('Recipe ' + R.n + ' ' + R.name + ': the Sample tab is set for every note' + (R.notes ? ', and it ' + recipeLanes(R) + ' of ' + sel : '') +
      '. A note keeps what it has in its own Note settings.', 5000);
  }
  function roll() {
    rolls++;
    const seed = ((Date.now() % 1000003) + rolls * 7919) | 0;   // a UI seed: the rolled values are stored, so they stay put (Y7)
    let inst;
    try { inst = M.dice(A.getInst(), seed, [...pins]); } catch (e) { console.warn('StrandLock: dice failed', e); return; }
    A.setInst(inst, 'Dice');
    const I = A.getInst();
    const parts = [ENGINE_NAME[I.engine] + (I.engine === 'cloud' && I.lock ? ' with Lock' : ''), MOTION_NAME[I.motion], fmtHeads(I.heads)];
    if (I.refresh.mode !== 'smooth') parts.push('Refresh ' + (I.refresh.mode === 'sync' ? M.rateLabel(I.refresh.rate) : fmtMs(I.refresh.ms)));
    A.toast('Dice: ' + parts.join(' · ') + '. Undo steps back.', 3500);
  }
  function buildRecipes() {
    const box = $('recipesBox');
    if (!box) return;
    box.innerHTML = '';
    const list = el('div', { class: 'sls-recipes' });
    for (const R of M.RECIPES) {
      const b = el('button', { type: 'button', title: (RECIPE_TIPS[R.n] || R.name) + (R.notes ? ' It also ' + recipeLanes(R) + '.' : '') }, el('b', null, String(R.n)), R.name);
      b.addEventListener('click', () => { A.setGhost(null); applyRecipeN(R); });
      b.addEventListener('mouseenter', () => {
        const ids = recipeIds(), on = ids ? 'the ' + ids.length + (ids.length === 1 ? ' selected note' : ' selected notes') : 'every note';
        A.setHint('Recipe ' + R.n + ' ' + R.name + ': ' + (RECIPE_TIPS[R.n] || '') + ' One click sets the Sample tab for every note' + (R.notes ? ' and ' + recipeLanes(R) + ' of ' + on : '') + '; the dashed lines preview the notes.');
        try { A.setGhost(M.applyRecipe(A.getClip(), A.getInst(), R.n, recipeIds()).clip); } catch (e) { A.setGhost(null); }
      });
      b.addEventListener('mouseleave', () => { A.setGhost(null); A.setHint(null); });
      list.appendChild(b);
    }
    recipeScope = el('span', { class: 'sls-sub' });
    box.appendChild(el('div', { class: 'sls-row' }, el('div', { class: 'sls-g' }, el('span', { class: 'lbl' }, 'Recipes'), list), recipeScope));
    const dice = el('button', { type: 'button', class: 'sls-dice', title: 'Roll new Sample settings within musical limits. Pinned settings stay. Each roll is one undo step and stays put until the next roll.' }, '\u{1F3B2} Dice');
    dice.addEventListener('click', roll);
    helpOn(dice, () => 'Dice: rolls new Sample settings (engine, motion, heads, grain, refresh...) within musical limits. A pinned setting stays. Undo steps back.');
    diceInfo = el('span', { class: 'sls-sub' });
    unpinBtn = el('button', { type: 'button', class: 'sls-reset', hidden: true, title: 'Let Dice roll every setting again' }, 'Unpin all');
    unpinBtn.addEventListener('click', () => { pins = new Set(); sset(PIN_KEY, []); syncPins(); });
    box.appendChild(el('div', { class: 'sls-row' }, dice, diceInfo, unpinBtn));
  }
  // what a recipe does to the notes' Sample lines, in words: 'resets the Travel, Heads, Grain and Refresh lines',
  // 'puts a pattern on the Travel line', 'shapes the Heads line'
  const LANE_WORD = { travel: 'Travel', heads: 'Heads', grain: 'Grain', refresh: 'Refresh' };
  function recipeLanes(R) {
    const list = w => (w.length > 1 ? w.slice(0, -1).join(', ') + ' and ' + w[w.length - 1] + ' lines' : w[0] + ' line');
    const reset = [], shaped = [], pattern = [];
    for (const k of Object.keys(R.notes || {})) { const e = R.notes[k]; (e === null ? reset : typeof e === 'string' ? shaped : pattern).push(LANE_WORD[k] || k); }
    const out = [];
    if (reset.length) out.push('resets the ' + list(reset));
    if (shaped.length) out.push('shapes the ' + list(shaped));
    if (pattern.length) out.push('puts a pattern on the ' + list(pattern));
    return out.join(' and ');
  }
  function syncRecipeScope() {
    if (!recipeScope) return;
    const n = A.getSelection().length;
    const t = n ? 'One click sets the Sample tab for every note. Some recipes also set Sample lines (Travel, Heads, Grain, Refresh): only on the ' + n + (n === 1 ? ' selected note.' : ' selected notes.')
      : 'One click sets the Sample tab for every note. Some recipes also set Sample lines (Travel, Heads, Grain, Refresh): on every note (select notes to limit that).';
    if (recipeScope.textContent !== t) recipeScope.textContent = t;
  }

  // =============================================================================================== Macros and offsets
  const MACRO_TIPS = [
    'Every note plays 1/4x to 4x faster.', 'Spreads the heads further apart, or closer together.', 'Adds or removes heads on every note (up to 8).',
    'Fresh grabs come faster or slower (works when Refresh is Free or Synced).', 'Longer or shorter grains on every note.', 'More or less Spray on every note.',
    'Brighter or darker: moves every note\'s Tone line.', 'Moves every note\'s Formant line up or down.'];
  const MACRO_RULE = ['mSpeed', 'mSpread', null, 'mRefresh', 'mGrain', 'mSpray', 'mTone', null];
  const OFFSETS = [['amp', 'Volume'], ['vib', 'Vibrato'], ['tone', 'Tone'], ['formant', 'Formant'], ['hollow', 'Hollow']];
  const OFFSET_TIPS = { amp: 'Added to every note\'s Volume line (the dB shown is for a note at the default volume).', vib: 'Added to every note\'s Vibrato line (in semitones of depth).', tone: 'Added to every note\'s Tone line.',
    formant: 'Added to every note\'s Formant line.', hollow: 'Added to every note\'s Hollow line.' };
  function buildMacros() {
    const box = $('macrosBox');
    if (!box) return;
    box.innerHTML = '';
    const mk = el('div', { class: 'sls-row sls-knobs sls-macros' });
    M.MACRO_NAMES.forEach((name, i) => {
      mk.appendChild(knob({ label: name, rule: MACRO_RULE[i], min: 0, max: 1, def: 0.5, bipolar: true, undo: 'Macro ' + name,
        color: i === 6 ? 'var(--lane-tone)' : i === 7 ? 'var(--lane-formant)' : 'var(--lane-sample)',
        title: name + ' macro: ' + MACRO_TIPS[i] + ' Centre = no change; double-click = centre. Moves every note at once (Quick Control ' + (i + 1) + ').',
        get: I => I.macros[i], patch: (v, I) => ({ macros: I.macros.map((x, k) => (k === i ? v : x)) }), fmt: v => macroText(i, v),
        q: (v, fine) => (fine ? r6(Math.round(v * 1000) / 1000) : Math.abs(v - 0.5) < 0.02 ? 0.5 : r6(Math.round(v * 200) / 200)), parse: null }));
    });
    const centre = el('button', { type: 'button', class: 'sls-reset', title: 'Put all 8 macros back in the middle (no change)' }, 'Centre all');
    centre.addEventListener('click', () => { const I = A.getInst(); if (I.macros.every(m => m === 0.5)) { A.toast('The macros are already centred'); return; } A.patchInst({ macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }, 'Centre the macros'); });
    box.appendChild(el('div', { class: 'sls-row' }, el('span', { class: 'lbl' }, 'Macros'), el('span', { class: 'sls-sub' }, 'Each knob moves every note at once. Centre = no change.'), centre));
    box.appendChild(mk);
    const ok = el('div', { class: 'sls-row sls-knobs sls-macros' });
    for (const [k, name] of OFFSETS) {
      ok.appendChild(knob({ label: name, min: -1, max: 1, def: 0, bipolar: true, undo: 'Offset ' + name, color: A.laneVar(k),
        title: 'Whole-clip ' + name + ': ' + OFFSET_TIPS[k] + ' Centre = no change; double-click = centre.',
        get: I => I.offsets[k], patch: v => ({ offsets: { [k]: v } }), fmt: v => offsetText(k, v),
        q: (v, fine) => (fine ? r6(Math.round(v * 1000) / 1000) : Math.abs(v) < 0.03 ? 0 : r6(Math.round(v * 100) / 100)), parse: null }));
    }
    const zero = el('button', { type: 'button', class: 'sls-reset', title: 'Set all 5 offsets back to no change' }, 'Reset all');
    zero.addEventListener('click', () => { const I = A.getInst(); if (OFFSETS.every(([k]) => I.offsets[k] === 0)) { A.toast('The offsets are already at no change'); return; } A.patchInst({ offsets: { amp: 0, vib: 0, tone: 0, formant: 0, hollow: 0 } }, 'Reset the offsets'); });
    box.appendChild(el('div', { class: 'sls-row' }, el('span', { class: 'lbl' }, 'Whole-clip offsets'), el('span', { class: 'sls-sub' }, 'Added to every note\'s line.'), zero));
    box.appendChild(ok);
  }

  // =============================================================================================== preset menus (U4)
  const SOUND_FIELDS = ['engine', 'lock', 'motion', 'speed', 'heads', 'spread', 'grainVoice', 'grainCloud', 'grainLock', 'refresh', 'spray', 'tape', 'rev', 'drift', 'headTune', 'jump', 'region', 'macros'];
  function mine() {
    const m = sget(MINE_KEY);
    return { sound: Array.isArray(m && m.sound) ? m.sound.filter(x => x && typeof x.name === 'string' && x.inst) : [],
      clip: Array.isArray(m && m.clip) ? m.clip.filter(x => x && typeof x.name === 'string' && Array.isArray(x.notes)) : [] };
  }
  function saveMine(kind, name) {
    const all = mine(), list = all[kind];
    name = String(name || '').trim().slice(0, 40) || (kind === 'sound' ? 'My sound ' : 'My clip ') + (list.length + 1);
    let item;
    if (kind === 'sound') {
      const I = A.getInst(), S = A.getSample(), inst = {};
      for (const k of SOUND_FIELDS) inst[k] = JSON.parse(JSON.stringify(I[k]));
      item = { name, sound: S && S.builtIn ? S.id : null, inst };
    } else {
      const C = A.getClip();
      item = { name, bars: C.bars, notes: JSON.parse(JSON.stringify(C.notes)) };
    }
    const i = list.findIndex(x => x.name === name);
    if (i >= 0) list[i] = item; else list.push(item);
    while (list.length > 40) list.shift();
    const ok = sset(MINE_KEY, all);
    A.toast((i >= 0 ? 'Replaced' : 'Saved') + ' “' + name + '”' + (ok ? ' in this browser' : ' for this visit only (this browser could not store it)'), 3000);
  }
  function deleteMine(kind, name) {
    const all = mine();
    all[kind] = all[kind].filter(x => x.name !== name);
    sset(MINE_KEY, all);
    A.toast('Deleted “' + name + '”');
  }
  const soundName = id => { const s = SND && SND.list.find(x => x.id === id); return s ? s.name : id; };
  // the preset loaded last is remembered across visits; the menu marks it while its sound is still the one loaded
  const LAST_KEY = 'strandlock_v4_lastpreset';
  let lastSoundPreset = (v => (typeof v === 'string' ? v : null))(sget(LAST_KEY)), confirmDel = null;
  function setLastPreset(id) { lastSoundPreset = id; sset(LAST_KEY, id); }
  function presetIsLoaded(id, soundId) {
    const S = A.getSample();
    if (lastSoundPreset !== id || !S) return false;
    return soundId ? !!S.builtIn && S.id === soundId : true;
  }
  function loadSoundPreset(p) {
    A.toast('Loading ' + p.name + '...');
    setTimeout(() => {
      let r;
      try { r = M.applySoundPreset(A.getInst(), p.id); } catch (e) { A.toast('Could not load that preset'); return; }
      try { A.loadSound(r.sound, { label: null }); } catch (e) { console.warn('StrandLock: could not make the sound', e); A.toast('Could not make that sound'); return; }
      const I = A.getInst();   // loadSound set the starter's root (C3); keep it
      A.setInst(M.mergeInst(r.inst, { root: I.root, cents: I.cents }), 'Sound preset: ' + p.name);
      setLastPreset(p.id);
      A.toast(p.name + ' loaded. Your notes stay; Undo brings the previous sound back.', 3000);
    }, 20);
  }
  function loadMySound(item) {
    A.toast('Loading ' + item.name + '...');
    setTimeout(() => {
      const S = A.getSample();
      if (item.sound && !(S && S.builtIn && S.id === item.sound)) {
        try { A.loadSound(item.sound, { label: null }); } catch (e) { A.toast('Could not make that sound'); return; }
      }
      A.setInst(M.mergeInst(A.getInst(), item.inst), 'My preset: ' + item.name);
      setLastPreset('mine:' + item.name);
      A.toast(item.name + ' loaded' + (item.sound ? '' : ' on the current sound') + '. Your notes stay.', 2500);
    }, 20);
  }
  function loadClip(clip, label) {
    const was = A.getClip(), playing = A.isPlaying();
    A.setClip(clip, label);
    A.setSelection([], null);
    const bars = $('bars');
    if (bars) bars.value = clip.bars;
    if (A.getStartBeat() > clip.bars * 4) A.setStartBeat(0);
    if (playing && clip.bars !== was.bars) { A.stop(); A.setStartBeat(0); A.play(); }
  }
  const menuEl = () => $('menu');
  // The shared #menu also opens from the lane arrows, the right-click menu and others, which never tell us: a click
  // on the button closes the menu only while it still shows the very menu this button opened (its first element).
  let menuOwner = null, menuMark = null;
  function ownMenu(btn) { const m = menuEl(); menuOwner = btn; menuMark = m ? m.firstChild : null; }
  function menuToggle(btn, open) {
    const m = menuEl();
    if (m && m.style.display === 'block' && menuOwner === btn && menuMark && m.firstChild === menuMark) { A.closeMenu(); menuOwner = null; menuMark = null; return; }
    open();
    ownMenu(btn);
  }
  function mineRows(kind, reopen, loadFn) {
    const items = [];
    for (const it of mine()[kind]) {
      if (confirmDel === kind + ':' + it.name) {
        items.push({ row: [
          { label: 'Delete “' + it.name + '”', click: () => { confirmDel = null; deleteMine(kind, it.name); } },
          { label: 'Keep', click: () => { confirmDel = null; } }] });
      } else {
        items.push({ row: [
          { label: it.name + (kind === 'sound' && !it.sound ? ' (current sound)' : ''), on: kind === 'sound' && presetIsLoaded('mine:' + it.name, it.sound),
            title: kind === 'sound' ? (it.sound ? 'Loads ' + soundName(it.sound) + ' with these settings' : 'Applies these settings to the sound that is loaded') : it.bars + (it.bars === 1 ? ' bar, ' : ' bars, ') + it.notes.length + ' notes',
            hover: kind === 'clip' ? () => { try { A.setGhost(M.normalizeClip({ bars: it.bars, notes: it.notes })); } catch (e) { A.setGhost(null); } } : null,
            click: () => loadFn(it) },
          { label: '✕', title: 'Delete this preset', keep: true, click: () => { confirmDel = kind + ':' + it.name; reopen(); } }] });
      }
    }
    return items;
  }
  function openSoundMenu(btn) {
    const r = btn.getBoundingClientRect();
    const reopen = () => { openSoundMenu(btn); ownMenu(btn); };
    const items = [{ header: 'Sound presets · your notes stay' }];
    for (const p of M.SOUND_PRESETS) items.push({ label: p.name, on: presetIsLoaded(p.id, p.sound), title: 'Loads ' + soundName(p.sound) + ' with its own engine and Sample settings; the macros go back to the middle', click: () => loadSoundPreset(p) });
    const my = mineRows('sound', reopen, loadMySound);
    if (my.length) items.push({ sep: true }, { header: 'My sound presets' }, ...my);
    items.push({ sep: true }, { label: '＋ Save this sound as my preset…', title: 'Keeps the sound (when it is a built-in one), the engine, the Sample settings, the Region and the macros', click: () => soundForm.open() });
    A.openMenu(r.left, r.bottom + 4, items);
  }
  // preset names in the pickers' words ('Scatter 1/8T' reads 'Scatter 1/8 triplet')
  const nice = name => String(name).replace(/\b(1\/\d+|1 bar)T\b/g, '$1 triplet').replace(/\b(1\/\d+|1 bar)D\b/g, '$1 dotted');
  function openClipMenu(btn) {
    const r = btn.getBoundingClientRect();
    const reopen = () => { openClipMenu(btn); ownMenu(btn); };
    const items = [{ header: 'Clip presets · the sound stays' }];
    for (const p of M.CLIP_PRESETS) {
      items.push({ label: nice(p.name), title: p.bars + (p.bars === 1 ? ' bar, ' : ' bars, ') + p.notes.length + ' notes. Replaces the notes (Undo brings them back).',
        hover: () => { try { A.setGhost(M.loadClipPreset(p.id)); } catch (e) { A.setGhost(null); } },
        click: () => { let c; try { c = M.loadClipPreset(p.id); } catch (e) { A.toast('Could not load that clip'); return; } loadClip(c, 'Clip preset: ' + nice(p.name)); A.toast(nice(p.name) + ' loaded. The sound stays; Undo brings your notes back.', 3000); } });
    }
    const my = mineRows('clip', reopen, it => { let c; try { c = M.normalizeClip({ bars: it.bars, notes: it.notes }); } catch (e) { A.toast('That clip could not be read'); return; } loadClip(c, 'My clip: ' + it.name); A.toast(it.name + ' loaded. Undo brings your notes back.', 3000); });
    if (my.length) items.push({ sep: true }, { header: 'My clip presets' }, ...my);
    items.push({ sep: true }, { label: '＋ Save these notes as my clip preset…', title: 'Keeps the notes and all their lanes, not the sound', click: () => clipForm.open() });
    A.openMenu(r.left, r.bottom + 4, items);
  }
  function saveForm(kind) {
    const inp = el('input', { type: 'text', maxlength: '40', placeholder: kind === 'sound' ? 'Name this sound' : 'Name these notes', 'aria-label': 'Preset name' });
    const ok = el('button', { type: 'button' }, 'Save');
    const no = el('button', { type: 'button' }, 'Cancel');
    const root = el('div', { class: 'sls-save', hidden: true }, inp, ok, no);
    const close = () => { root.hidden = true; };
    ok.addEventListener('click', () => { saveMine(kind, inp.value); close(); });
    no.addEventListener('click', close);
    inp.addEventListener('keydown', e => { e.stopPropagation(); if (e.key === 'Enter') { e.preventDefault(); ok.click(); } else if (e.key === 'Escape') { e.preventDefault(); close(); } });
    return { root, open() { root.hidden = false; inp.value = (kind === 'sound' ? 'My sound ' : 'My clip ') + (mine()[kind].length + 1); try { inp.focus(); inp.select(); } catch (e) { /* ok */ } } };
  }
  let soundForm = null, clipForm = null;
  function buildPresetMenus() {
    const sb = $('soundPresetMenu');
    if (sb) {
      sb.innerHTML = '';
      const btn = el('button', { type: 'button', class: 'sls-pbtn arrow', title: 'Sound presets: a sound with its engine, Sample settings and macros. Your notes stay.' }, '♫ Sound presets ▾');
      btn.addEventListener('click', () => menuToggle(btn, () => openSoundMenu(btn)));
      soundForm = saveForm('sound');
      sb.appendChild(btn); sb.appendChild(soundForm.root);
    }
    const cb = $('clipPresetMenu');
    if (cb) {
      cb.innerHTML = '';
      const btn = el('button', { type: 'button', class: 'sls-pbtn arrow', title: 'Clip presets: notes and their lanes. The sound stays.' }, '▤ Clips ▾');
      btn.addEventListener('click', () => menuToggle(btn, () => openClipMenu(btn)));
      clipForm = saveForm('clip');
      cb.appendChild(btn); cb.appendChild(clipForm.root);
    }
  }

  // =============================================================================================== part chips (D10, U2) and the engine suggestion (E3)
  let hoverPart = null, chipKey = '', suggestion = null, u2Seen = new WeakSet(), sugSample = null;
  function setRegionTo(start, end, label) {
    const R = A.getInst().region;
    if (Math.abs(R.start - start) < 1e-9 && Math.abs(R.end - end) < 1e-9) return;
    A.patchInst({ region: { start, end } }, label);
  }
  // zoom the strip so a small part fills about half of it (the view is not an undo step)
  function showPart(p) {
    if (!A.setStripView) return;
    const V = A.stripView(), w = p.end - p.start, vw = V.b - V.a;
    const outside = p.start < V.a - 1e-9 || p.end > V.b + 1e-9;
    if (!outside && w >= vw * 0.3) return;
    const m = w * 0.5;
    A.setStripView(p.start - m, p.end + m);
  }
  function usePart(p) {
    setRegionTo(p.start, p.end, 'Region: ' + p.label);
    showPart(p);
    if (A.suggestRoot) A.suggestRoot();
    if (p.kind === 'loop' && p.bpm && Math.abs(p.bpm - A.getBpm()) > 0.5) A.toast('This bar is ' + (Math.round(p.bpm * 10) / 10) + ' bpm: set the Tempo to ' + Math.round(p.bpm) + ' to keep it in time', 4500);
  }
  const PART_TIPS = { steady: 'A held, steady stretch: good for pads and leads.', hit: 'The biggest hit in the file.', quiet: 'A quiet texture.', loop: 'One bar at the tempo found in the file.' };
  function partChip(p, R) {
    const b = el('button', { type: 'button', class: 'chip sls-part' + (sameRegion(R, p) ? ' on' : ''), title: (PART_TIPS[p.kind] || '') + ' Click to set the Region to it (one undo step).' }, p.label);
    b.addEventListener('click', () => usePart(p));
    b.addEventListener('mouseenter', () => { hoverPart = p; A.redraw(); A.setHint('Suggested part: ' + p.label + '. ' + (PART_TIPS[p.kind] || '') + ' The dashed box on the waveform shows it.'); });
    b.addEventListener('mouseleave', () => { hoverPart = null; A.redraw(); A.setHint(null); });
    return b;
  }
  function renderChips(force) {
    const box = $('partChips');
    if (!box) return;
    const S = A.getSample(), I = A.getInst(), R = I.region;
    const parts = (S && S.parts) || [];
    const sugOn = !!(suggestion && !suggestion.done && (suggestion.engine !== I.engine || suggestion.match) && sugSample === S);
    const key = [S ? S.name + S.channels[0].length : '-', parts.length, R.start, R.end, sugOn ? suggestion.engine : '-'].join('|');
    if (!force && key === chipKey) return;
    chipKey = key;
    // the chip under the mouse is about to go: its mouseleave will never come
    if (hoverPart) { hoverPart = null; A.setHint(null); A.redraw(); }
    box.innerHTML = '';
    if (!S) return;
    if (sugOn && suggestion.match) {   // the engine that suits it is already on: say so once (a click hides it)
      const b = el('button', { type: 'button', class: 'chip sls-sug', title: suggestion.why + ' Click to hide this note.' }, '\u{1F4A1} ' + ENGINE_NAME[suggestion.engine] + ' suits this sound (on)');
      b.addEventListener('click', () => { suggestion.done = true; A.toast(ENGINE_NAME[suggestion.engine] + ': ' + suggestion.why, 4000); renderChips(true); });
      box.appendChild(b);
    } else if (sugOn) {
      const b = el('button', { type: 'button', class: 'chip sls-sug', title: suggestion.why + ' Click to switch the engine (one undo step).' }, '\u{1F4A1} ' + suggestion.text);
      b.addEventListener('click', acceptSuggestion);
      box.appendChild(b);
    }
    const whole = regionIsWhole(R);
    if (!parts.length) {
      if (!whole) box.appendChild(el('button', { type: 'button', class: 'chip', title: 'Set the Region back to the whole file (one undo step)', onclick: () => setRegionTo(0, 1, 'Region: whole file') }, 'Use the whole file'));
      return;
    }
    if (S.seconds > 8) {
      if (!whole) box.appendChild(el('button', { type: 'button', class: 'chip', title: 'Set the Region back to the whole file (one undo step)', onclick: () => { setRegionTo(0, 1, 'Region: whole file'); if (A.setStripView) A.setStripView(0, 1); if (A.suggestRoot) A.suggestRoot(); } }, 'Use the whole file'));
      for (const p of parts) box.appendChild(partChip(p, R));
      return;
    }
    // a short file (the starter sounds): one chip that opens the suggestions
    const on = parts.find(p => sameRegion(R, p));
    const b = el('button', { type: 'button', class: 'chip arrow' + (on ? ' sls-part on' : ''), title: 'Suggested parts of this sound' }, (on ? on.label : 'Parts') + ' ▾');
    b.addEventListener('click', () => menuToggle(b, () => {
      const r = b.getBoundingClientRect(), R2 = A.getInst().region;
      const items = [{ header: 'Suggested parts' }];
      for (const p of parts) items.push({ label: p.label, on: sameRegion(R2, p), title: PART_TIPS[p.kind] || '', click: () => usePart(p),
        hover: () => { hoverPart = p; A.redraw(); }, leave: () => { if (hoverPart === p) { hoverPart = null; A.redraw(); } } });
      items.push({ sep: true }, { label: 'Use the whole file', disabled: regionIsWhole(R2), click: () => { setRegionTo(0, 1, 'Region: whole file'); if (A.setStripView) A.setStripView(0, 1); } });
      A.openMenu(r.left, r.bottom + 4, items);
    }));
    box.appendChild(b);
  }
  function drawHoverPart(ctx, rect, info) {
    if (!hoverPart || !info.sample || info.sample !== A.getSample()) return;
    const x0 = info.xOf(hoverPart.start), x1 = info.xOf(hoverPart.end), T = info.theme;
    ctx.fillStyle = T['region-band']; ctx.globalAlpha = 0.35; ctx.fillRect(x0, 0, Math.max(1, x1 - x0), rect.h); ctx.globalAlpha = 1;
    ctx.setLineDash([5, 4]); ctx.strokeStyle = T['region-edge']; ctx.lineWidth = 1.5;
    ctx.strokeRect(x0 + 0.75, 0.75, Math.max(1, x1 - x0 - 1.5), rect.h - 1.5); ctx.setLineDash([]);
  }
  function acceptSuggestion() {
    if (!suggestion) return;
    const s = suggestion;
    s.done = true;
    if (A.getInst().engine !== s.engine) A.patchInst({ engine: s.engine }, 'Engine: ' + ENGINE_NAME[s.engine] + ' (suggested)');
    A.toast(ENGINE_NAME[s.engine] + ': ' + s.why, 4000);
    syncSuggestion(); renderChips(true);
  }
  function syncSuggestion() {
    if (!sugLine) return;
    const I = A.getInst();
    const on = !!(suggestion && !suggestion.done && !suggestion.match && suggestion.engine !== I.engine && sugSample === A.getSample());
    sugLine.hidden = !on;
    if (on) { sugText.textContent = suggestion.why; sugBtn.textContent = 'Use ' + ENGINE_NAME[suggestion.engine]; }
  }
  function onSample(S) {
    hoverPart = null;
    if (suggestion && sugSample !== S) suggestion = null;
    renderChips(true); syncSuggestion();
  }
  function onParts(parts) {
    const S = A.getSample();
    if (!S) return;
    // U2: a long file opens with the box around a short suggested part (Use the whole file is one click)
    if (!S.builtIn && S.seconds > 20 && parts && parts.length && !u2Seen.has(S) && regionIsWhole(A.getInst().region)) {
      u2Seen.add(S);
      const p = pickPart(parts);
      if (p) {
        // the part belongs to the load: one undo step brings the previous sound back (it is folded into 'Load <file>'
        // while that is still the newest step; otherwise it is a step of its own). The whole song stays in view (U2).
        A.patchInst({ region: { start: p.start, end: p.end } }, null);
        if (!(A.amendHistory && A.amendHistory('Load ' + S.name))) A.commit('Suggested part');
        if (A.suggestRoot) A.suggestRoot();
        setTimeout(() => { if (A.getSample() === S) A.toast('Long file: the Region is around a suggested part (' + p.label + '). Drag the box to choose your own; “Use the whole file” takes it all.', 5000); }, 4300);
      }
    }
    renderChips(true);
    // E3: suggest an engine for the user's own file (the built-ins are tuned C3 sounds)
    if (!S.builtIn && AN && sugSample !== S) {
      setTimeout(() => {
        if (A.getSample() !== S || sugSample === S) return;
        let m = null;
        if (S.analysed) m = S.pitch || null;   // sl-ui.js has already listened to this file
        else { try { m = AN.detectPitch(S.channels, S.sampleRate); } catch (e) { m = null; } }
        sugSample = S;
        const s = suggestEngine({ pitch: !!m, seconds: S.seconds, parts: S.parts || [] });
        const eng = A.getInst().engine;
        suggestion = s ? Object.assign({ done: false, shownEngine: eng, match: s.engine === eng }, s) : null;
        syncSuggestion(); renderChips(true);
      }, 150);
    }
  }

  // =============================================================================================== sync and wiring
  let rafPending = false;
  function syncAll() {
    rafPending = false;
    const I = A.getInst(), Fs = currentFades();
    for (const c of controls) { try { c.sync(I, Fs); } catch (e) { console.warn('StrandLock sample panel:', e); } }
    syncPins(); syncRecipeScope(); renderChips(false);
    // the suggestion goes once the engine is changed by any other means
    if (suggestion && !suggestion.done && suggestion.shownEngine !== I.engine) suggestion.done = true;
    syncSuggestion();
  }
  function syncSoon() {
    if (rafPending) return;
    rafPending = true;
    (window.requestAnimationFrame || (f => setTimeout(f, 16)))(syncAll);
  }
  function init() {
    buildSampleTab();
    buildRecipes();
    buildMacros();
    buildPresetMenus();
    A.addStripDecorator(drawHoverPart);
    // a finished change syncs at once; a live one (a knob or Region drag) once per frame at most, since the part chips
    // and the faded states read every note
    A.on('inst', e => { if (e && e.live) syncSoon(); else syncAll(); });
    A.on('clip', e => { if (!(e && e.live)) syncSoon(); });
    A.on('selection', syncRecipeScope);
    A.on('sample', onSample);
    A.on('parts', onParts);
    A.on('ready', syncSoon);
    A.onReady(() => { const S = A.getSample(); if (S && S.parts) onParts(S.parts); syncAll(); });
    syncAll();
  }
  try { init(); } catch (e) { console.warn('StrandLock: the sample panels could not start', e); }

  return Object.assign(pure, {
    sync: syncAll,
    pins: () => [...pins],
    setPins: list => { pins = new Set((list || []).filter(f => M.DICE_FIELDS.includes(f))); sset(PIN_KEY, [...pins]); syncPins(); },
    roll, applyRecipe: n => { const R = M.RECIPES.find(r => r.n === n); if (R) applyRecipeN(R); },
    loadSoundPreset: id => { const p = M.SOUND_PRESETS.find(x => x.id === id || x.name === id); if (p) loadSoundPreset(p); },
    loadClipPreset: id => { const c = M.loadClipPreset(id); loadClip(c, 'Clip preset: ' + nice((M.CLIP_PRESETS.find(p => p.id === id || p.name === id) || { name: id }).name)); },
    saveMine, myPresets: mine, suggestion: () => suggestion,
  });
})();
if (typeof module !== 'undefined') module.exports = SLSampleUI;
