/* StrandLock prototype v4: sl-ui.js (the page, pass 1: foundation)
   Global: SLApp. Loads after SLModel, SLSounds, SLAnalysis and the engine (createStrandEngineModule,
   strandWorkletGlue). Ports v3's page (layout, roll drawing and gestures, menus, hint bar, MIDI, file load,
   detection) onto the tested v4 modules, and adds the Sample mode, box select, node editing, the Sample
   editor strip and the real sound engine (AudioWorklet, with a ScriptProcessor backup).

   ===================================================================================================
   SLApp API for the pass-2 files (src/sl-ui-*.js load after this file, sorted by name)
   Everything is available as soon as this file has run. Clips are immutable: never mutate a clip, a
   note or an inst you got from SLApp; build a new one with SLModel (every SLModel function returns a
   new clip) and hand it back.

   State and editing (every edit = exactly one undo step)
     getClip()                      current clip
     setClip(clip, label)           label = undo step name (one step); null = live change (during a drag),
                                    then commit(label) once on mouse-up
     getInst() / setInst(inst, label|null)    same rule; null while a knob moves, commit(label) on release
     patchInst(patch, label|null)   setInst(SLModel.mergeInst(getInst(), patch), label)
     commit(label)                  push the current {clip, inst} as one undo step
     amendHistory(label) -> bool    fold the current state into the newest step when it is still called label
                                    (a follow-up the page makes by itself stays in the step that caused it)
     undo() / redo() / canUndo() / canRedo() / undoLabel()
     getSelection() -> ids (time order)   setSelection(ids, primaryId?)   selectionSet() -> Set (read only)
     getPrimary() / setPrimary(id)   primaryNote() -> note|null   selectedNotes() -> notes (time order)
     noteById(id) -> note|null
     getMode() -> 1..8   setMode(n)   getSubLane() / setSubLane('travel'|'heads'|'grain'|'refresh')
     activeLane() -> lane id, or null in Build
     getActiveNode() -> {id, lane, k}|null   the node last clicked or dragged (for the inspector)
     getNodeSelection() -> Map(noteId -> Set of node indices) picked with the Nodes switch
     getBpm() / setBpm(n)   isPlaying()   play()   stop()   getBeat()   getStartBeat() / setStartBeat(b)
     getSample() -> {id, name, channels, sampleRate, seconds, builtIn, parts, pitch, key, analysed}|null
                                    (pitch/key: the detection result for a user file, set once analysed is true)
     loadSound(starterId, {label, keepRegion}?)  (one undo step 'Sound: ...'; label null = no step of its own)
     loadFile(File)   sampleSeconds()   suggestRoot() (after the Region of a user file changes: suggest its root)
     formatLane(lane, v, ctx)       SLModel.formatLane in the pickers' words ('1/8x', '1/8 triplet')
     formatCtx(note) -> ctx for SLModel.formatLane / mapLane: {inst, bpm, travelKind}
     getScope() / setScope('each'|'across')  Each note / Across the selection, shared by every shape tool
   Drawing
     redraw()                       roll + strip on the next frame
     setGhost(clip|null)            faint preview of a clip on the roll (only notes that differ are drawn)
     addNoteDecorator(fn(ctx, note, rect, info)) -> remove fn. Called after each visible note is drawn;
        ctx is translated to roll content pixels; rect = {x0, x1, y0, y1, top, bottom};
        info = {selected, primary, mode, lane, BW, RH, laneH, theme, inst, bpm, K, table(laneId),
                yAt(t) (note line y at note time t), valueY(laneId, t, v)}
     addStripDecorator(fn(ctx, rect, info)) -> remove fn. rect = {x, y, w, h} (css px of the strip canvas);
        info = {view:{a,b}, xOf(pos), posOf(x), sample, display, inst, theme, region:{x0, x1}}
     laneColor(id) -> CSS colour of a lane in the current theme   theme() -> token map   isDark()
     stripView() -> {a, b}          the part of the file the strip shows (fractions); setStripView(a, b) changes it
   UI helpers
     toast(msg, ms?)   openMenu(clientX, clientY, items)   closeMenu()   el(tag, attrs, ...children)
        menu items: {header:'text'} | {sep:true} | {label, click(), hover(), leave(), on, disabled, keep, title, color}
                    | {row:[items]}  (hover() usually calls setGhost; leaving the item clears the ghost and runs
                    leave(), which also runs on a click and when the menu closes)
     showTab(name)   ('sample'|'macros'|'note'|'glide'|'copy'|'mods'|'sound')
     setHint(text|null)             replace the hint line (null = the mode's own hint)
     rateOptions(selectEl, value)   fill a <select> with every rate (1 bar .. 1/64, triplet, dotted)
   Events: on(name, fn) -> off fn; off(name, fn)
     'clip' {live, label}   'inst' {live, label}   'selection' {ids, primary}   'mode' {mode, subLane, lane}
     'display' (engine display message, ~30/s)   'sample' (sample info)   'parts' (suggested parts)
     'theme' {theme}   'history' {canUndo, canRedo, undoLabel, redoLabel}   'node' (active node)
     'transport' {playing, bpm, loop}   'ready'   'detected' {pitch, key} (a user file was analysed)   'tab' (name)
     'nodes' (getNodeSelection(): the nodes picked with the Nodes switch changed)
   Engine
     noteOn(p, vel 0..1, opts) / noteOff(p)   post(msg) (raw engine message)   audioPath()
   Test hooks: SLApp.debug = { meter() -> {peak, rms}, audioPath() -> 'worklet'|'fallback'|'off',
     lastDisplay(), snapshotsSent(), historyLength() (states held), historyIndex(), posts() (engine messages by type) }
   ===================================================================================================
   Notes
   - Root and cents belong to the loaded sound: they are not undo steps. Each undo step stores which sound was loaded
     (a starter id or 'file:N'); Undo of a sound change brings that sound back with its own root and cents.
   - Computer keys (notes and shortcuts) work only while the Keys toggle is on; the spacebar is never taken.
   - Sound starts after the first click anywhere (browser rule); the 'Click to turn sound on' chip shows until then. */
var SLApp = (function () {
  'use strict';
  if (typeof window === 'undefined' || typeof document === 'undefined') return null;
  const M = window.SLModel, SND = window.SLSounds, AN = window.SLAnalysis;

  // =============================================================================================== helpers
  const $ = id => document.getElementById(id);
  const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
  const num = (x, d) => (typeof x === 'number' && isFinite(x) ? x : d);
  const DPR = () => Math.min(2, window.devicePixelRatio || 1);
  const MINP = 24, MAXP = 96, ROWS = MAXP - MINP + 1;
  const SOUND_LANES = ['amp', 'vib', 'tone', 'formant', 'hollow'];
  const SAMPLE_LANES = M.SAMPLE_LANES.slice();
  const LANE = M.LANE;
  const noteName = M.noteName;
  // scales in full words (the model keeps its short names as ids: saved sessions use them)
  const SCALE_LABEL = { 'Harm. minor': 'Harmonic minor', 'Pent. major': 'Pentatonic major', 'Pent. minor': 'Pentatonic minor' };
  const scaleLabel = s => SCALE_LABEL[s] || s;
  const keyName = (k, s) => M.NAMES[k] + ' ' + scaleLabel(s).toLowerCase();
  // readouts in plain units: 'st' becomes 'semitones', and one of a unit is singular ('1 cycle', '1 semitone')
  const plainUnits = t => String(t)
    .replace(/(^|[^\w.])([+-]?)(\d+(?:\.\d+)?) st\b/g, (m, pre, s, n) => pre + s + n + (n === '1' ? ' semitone' : ' semitones'))
    .replace(/ (semitones?) vibrato\b/, ' $1 of vibrato')
    .replace(/(^|[^\w.])1 (cycles|wavelengths)\b/g, (m, pre, u) => pre + '1 ' + u.slice(0, -1));
  const isBlack = p => [1, 3, 6, 8, 10].includes(((p % 12) + 12) % 12);
  const SUB_HINT = {
    travel: 'Travel: above the middle line plays forwards, below plays backwards, on the line stops (1/8x to 8x). Path notes: the line is where in the Region the note is.',
    heads: 'Heads: raise the line for more playheads (1 to 8), lower it for fewer. Flat = the Heads knob.',
    grain: 'Grain: raise the line for longer grains, lower it for shorter ones. Flat = the Grain knob.',
    refresh: 'Refresh: raise the line for faster fresh grabs, lower it for slower; the very bottom = Smooth. The line works once Refresh on the Sample tab is Free or Synced (it looks dim until then).',
  };
  function el(tag, attrs) {
    const e = document.createElement(tag);
    if (attrs) for (const k of Object.keys(attrs)) {
      const v = attrs[k];
      if (v == null || v === false) continue;
      if (k === 'class') e.className = v;
      else if (k === 'text') e.textContent = v;
      else if (k === 'html') e.innerHTML = v;
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
  const store = {
    get(k) { try { const s = window.localStorage.getItem(k); return s == null ? null : JSON.parse(s); } catch (e) { return null; } },
    set(k, v) { try { window.localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* storage blocked: the page works without it */ } },
  };
  function sizeCanvas(cv, w, h) {
    const d = DPR(), W = Math.max(1, Math.round(w * d)), H = Math.max(1, Math.round(h * d));
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
    cv.style.width = w + 'px'; cv.style.height = h + 'px';
    const c = cv.getContext('2d');
    c.setTransform(d, 0, 0, d, 0, 0); c.clearRect(0, 0, w, h);
    return c;
  }
  const fmtTime = s => { s = Math.max(0, s); const m = Math.floor(s / 60), r = s - m * 60; return m + ':' + (r < 10 ? '0' : '') + r.toFixed(2); };
  const fmtCents = c => (c >= 0 ? '+' : '') + Math.round(c) + ' cents';
  const fmtBeats = x => { const r = Math.round(x * 1000) / 1000; return r + (r === 1 ? ' beat' : ' beats'); };
  // SLModel.formatLane in the picker's own words: '1/8x forwards' (not '0.13x'), 'Refresh 1/8 triplet (...)' (not '1/8T')
  function fmtLane(lane, v, ctx) {
    let t = M.formatLane(lane, v, ctx);
    if (lane === 'travel' && !(ctx && ctx.travelKind === 'path')) {
      const x = M.mapLane('travel', v, ctx || {});
      if (Math.abs(Math.abs(x) - 0.125) < 0.0007) t = t.replace(/^0\.13x/, '1/8x');
    } else if (lane === 'refresh') {
      // with Refresh set to Smooth on the Sample tab this line does nothing: say so, not just 'Smooth'
      const rm = ctx && ctx.inst && ctx.inst.refresh ? ctx.inst.refresh.mode : 'smooth';
      if (rm === 'smooth') return 'Smooth: set Refresh to Free or Synced on the Sample tab to use this line';
      t = t.replace(/^Refresh (\S+) \(/, (m, id) => (M.isRate(id) ? 'Refresh ' + M.rateLabel(id) + ' (' : m)).replace(/ beat \(/, ' beats (');
    }
    return plainUnits(t);
  }

  // =============================================================================================== events
  const listeners = {};
  function on(name, fn) { (listeners[name] || (listeners[name] = new Set())).add(fn); return () => off(name, fn); }
  function off(name, fn) { if (listeners[name]) listeners[name].delete(fn); }
  function emit(name, data) {
    const L = listeners[name];
    if (!L) return;
    for (const fn of [...L]) { try { fn(data); } catch (e) { console.warn('SLApp: a "' + name + '" listener failed:', e); } }
    // what the listeners redrew above the roll must not move the roll under the mouse
    if (rollHold && name !== 'display' && name !== 'transport') keepRollStill();
  }

  // =============================================================================================== state
  const SAVE_KEY = 'strandlock_v4', THEME_KEY = 'strandlock_v4_theme';
  const app = {
    clip: null, inst: null, hist: null,
    sel: new Set(), primary: null,
    mode: 1, subLane: 'travel',
    keys: false, nodesMode: false, free: false, multi: false, pan: false, linkArm: false,
    bpm: 96, playing: false, loop: true, snap: 0.25, startBeat: 0,
    scope: 'each', tab: 'sample', folded: false,
    sample: null, soundId: 'sawopen', soundRef: 'sawopen',   // soundRef: a starter id or 'file:N' (kept in every undo step)
    ghost: null,
    nodeSel: new Map(), nodeLen: new Map(), activeNode: null,   // nodeLen: the lane's node count when its nodes were picked
    clipboard: null, copyMask: new Set(M.LANE_IDS),
    oct: 60, learn: 0, wheels: [0, 0], wheelTouched: [false, false], bend: 0, sustain: false,
    rec: false, hintOverride: null,
  };
  let BW = 64, RH = 14;
  const laneH = () => RH * 2.6;
  const total = () => app.clip.bars * 4;
  const minLen = () => app.snap || 0.125;
  const snapR = b => (app.snap ? Math.round(b / app.snap) * app.snap : b);
  const scaleSnap = p => (app.inst.scaleLock ? M.snapScale(p, app.inst.key, app.inst.scale) : p);
  function activeLane() { if (app.mode === 1) return null; if (app.mode === 8) return app.subLane; return M.MODES[app.mode - 1].lanes[0]; }
  let idxClip = null, idxMap = null;
  function noteById(id) {
    if (idxClip !== app.clip) { idxClip = app.clip; idxMap = new Map(app.clip.notes.map(n => [n.id, n])); }
    return idxMap.get(id) || null;
  }
  const noteIn = (clip, id) => clip.notes.find(n => n.id === id) || null;
  const primaryNote = () => (app.primary ? noteById(app.primary) : null);
  const byTime = (a, b) => a.s - b.s || a.p - b.p || (a.id < b.id ? -1 : 1);
  function selectedNotes() { return app.clip.notes.filter(n => app.sel.has(n.id)).sort(byTime); }
  function formatCtx(n) { return { inst: app.inst, bpm: app.bpm, travelKind: n && n.sample && n.sample.travel === 'path' ? 'path' : 'speed' }; }
  const sampleSeconds = () => (app.sample ? app.sample.seconds : 6);

  // =============================================================================================== theme
  const TOKENS = ['bg', 'panel', 'panel2', 'line', 'txt', 'dim', 'acc', 'onacc', 'warn', 'btn', 'btnb', 'roll', 'hdr', 'menu', 'hover',
    'row', 'row-black', 'row-out', 'row-root', 'row-line', 'oct-line', 'grid-bar', 'grid-beat', 'grid-sub',
    'note-line', 'note-line-sel', 'start-ring', 'dot-fill', 'piano-white', 'piano-black', 'piano-text', 'piano-out', 'piano-held',
    'ruler-text', 'ruler-tick', 'link', 'handle-ring', 'playhead', 'box-fill', 'box-line', 'node-sel', 'ghost',
    'lane-pitch', 'lane-amp', 'lane-vib', 'lane-tone', 'lane-formant', 'lane-hollow', 'lane-sample', 'on-lane',
    'a-act', 'a-act-sel', 'a-idle', 'a-idle-sel',
    'strip-bg', 'wave', 'strip-shade', 'region-edge', 'region-band', 'head', 'head-txt', 'grain', 'strip-txt', 'strip-txt-b', 'start-mark',
    'meter-bg', 'meter-ok', 'meter-hot', 'tip-bg', 'tip-txt'];
  let TH = {};
  function readTheme() {
    const cs = getComputedStyle(document.documentElement), o = {};
    for (const k of TOKENS) o[k] = cs.getPropertyValue('--' + k).trim();
    for (const k of ['a-act', 'a-act-sel', 'a-idle', 'a-idle-sel']) o[k] = parseFloat(o[k]) || 0.3;
    TH = o;
  }
  const laneColor = id => TH[LANE[id] && LANE[id].sub ? 'lane-sample' : 'lane-' + id] || TH.acc;
  const laneVar = id => 'var(--' + (LANE[id] && LANE[id].sub ? 'lane-sample' : 'lane-' + id) + ')';
  const lightQuery = window.matchMedia ? window.matchMedia('(prefers-color-scheme: light)') : null;
  function effectiveTheme() {
    const t = document.documentElement.dataset.theme;
    if (t === 'light' || t === 'dark') return t;
    return lightQuery && lightQuery.matches ? 'light' : 'dark';
  }
  function applyTheme(name, remember) {
    if (name === 'light' || name === 'dark') document.documentElement.dataset.theme = name;
    if (remember) store.set(THEME_KEY, name);
    readTheme();
    const eff = effectiveTheme();
    $('themeBtn').innerHTML = eff === 'dark' ? '&#9728; Light' : '&#9790; Dark';
    $('themeBtn').title = eff === 'dark' ? 'Switch to the light theme' : 'Switch to the dark theme';
    paintModes();
    waveCache.key = '';
    redraw();
    emit('theme', { theme: eff });
  }

  // =============================================================================================== editing core
  let snapPending = false, snapTimer = 0, snapshotsSent = 0, liveEmit = { clip: false, inst: false };
  const COMPILE_KEYS = ['engine', 'lock', 'motion', 'tape', 'rev', 'jump', 'glideMode', 'glideTime'];
  const compileKey = I => COMPILE_KEYS.map(k => JSON.stringify(I[k])).join('|');
  function scheduleSnapshot() {
    snapPending = true;
    if (!snapTimer) snapTimer = setTimeout(() => { snapTimer = 0; if (snapPending) flushSnapshot(); }, 120);
  }
  function flushSnapshot() {
    snapPending = false;
    if (snapTimer) { clearTimeout(snapTimer); snapTimer = 0; }
    if (liveEmit.clip) { liveEmit.clip = false; emit('clip', { live: true, label: null }); }
    if (!audio.ready) return;
    let snap;
    try { snap = M.compileSnapshot(app.clip, app.inst); } catch (e) { console.warn('StrandLock: snapshot failed', e); return; }
    post({ type: 'snapshot', snapshot: snap });
    snapshotsSent++;
  }
  function setClip(clip, label) {
    if (!clip || !Array.isArray(clip.notes)) throw new Error('SLApp.setClip: not a clip');
    if (recPass && !recAdding) endRecPass();   // notes recorded so far are their own step, before this edit
    app.clip = clip;
    pruneSelection();
    validateNodes();
    if (label) { snapPending = true; flushSnapshot(); pushHistory(label); emit('clip', { live: false, label }); }
    else { liveEmit.clip = true; scheduleSnapshot(); }
    requestDraw(); dirty.strip = true;
    syncGlideUI(); updateSelInfo(); saveSoon();
  }
  function setInst(inst, label) {
    if (recPass && !recAdding) endRecPass();   // notes recorded so far are their own step, before this change
    const prevKey = app.inst ? compileKey(app.inst) : '';
    app.inst = M.normalizeInst(inst);
    const meta = soundMeta.get(app.soundRef);   // the root and cents belong to the loaded sound: remember them with it
    if (meta) { meta.root = app.inst.root; meta.cents = app.inst.cents; }
    postInst();
    if (compileKey(app.inst) !== prevKey) scheduleSnapshot();
    if (label) { if (snapPending) flushSnapshot(); pushHistory(label); }
    emit('inst', { live: !label, label: label || null });
    requestDraw(); dirty.strip = true;
    syncControls(); saveSoon();
  }
  const patchInst = (patch, label) => setInst(M.mergeInst(app.inst, patch), label);
  function commit(label) {
    if (snapPending) flushSnapshot();
    pushHistory(label || 'Edit');
    emit('clip', { live: false, label: label || 'Edit' });
  }
  // every undo step also remembers which sound was loaded, so Undo of a sound change brings the old sound back
  function pushHistory(label) {
    app.hist.push({ clip: app.clip, inst: app.inst, sound: app.soundRef }, label);
    historyChanged();
  }
  // Fold the state now into the newest undo step (when it is still called label and nothing was undone since): a
  // follow-up the page makes by itself (a long file's suggested part, a moment after the load) stays one step.
  function amendHistory(label) {
    if (!app.hist.canUndo() || app.hist.canRedo() || app.hist.undoLabel() !== label) return false;
    app.hist.undo();
    if (!app.hist.push({ clip: app.clip, inst: app.inst, sound: app.soundRef }, label)) app.hist.redo();
    historyChanged();
    return true;
  }
  function historyChanged() {
    $('undo').disabled = !app.hist.canUndo(); $('redo').disabled = !app.hist.canRedo();
    $('undo').title = app.hist.canUndo() ? 'Undo: ' + app.hist.undoLabel() : 'Nothing to undo';
    $('redo').title = app.hist.canRedo() ? 'Redo: ' + app.hist.redoLabel() : 'Nothing to redo';
    emit('history', { canUndo: app.hist.canUndo(), canRedo: app.hist.canRedo(), undoLabel: app.hist.undoLabel(), redoLabel: app.hist.redoLabel() });
  }
  function applyState(s, how) {
    if (!s) return false;
    const inst = M.normalizeInst(s.inst);
    if (s.sound && s.sound !== app.soundRef) restoreSound(s.sound);
    const meta = soundMeta.get(app.soundRef);   // the root belongs to the sound that is loaded now
    inst.root = meta ? meta.root : app.inst.root; inst.cents = meta ? meta.cents : app.inst.cents;
    const R0 = app.inst.region, regionMoved = R0.start !== inst.region.start || R0.end !== inst.region.end;
    app.clip = s.clip; app.inst = inst;
    if (regionMoved) suggestRoot();   // a root tip belongs to the Region it was measured on
    pruneSelection();
    // picked nodes and the inspector's node may point at other nodes in the restored clip: let go of them
    if (app.nodeSel.size) { app.nodeSel = new Map(); app.nodeLen.clear(); emit('nodes', app.nodeSel); }
    if (app.activeNode) setActiveNode(null);
    postInst(); scheduleSnapshot(); flushSnapshot();
    requestDraw(); dirty.strip = true;
    syncControls(); syncGlideUI(); updateSelInfo(); historyChanged(); saveSoon();
    emit('clip', { live: false, label: how }); emit('inst', { live: false, label: how });
    return true;
  }
  function undo() { endRecPass(); const lbl = app.hist.undoLabel(); if (!app.hist.canUndo()) { toast('Nothing to undo'); return false; } const ok = applyState(app.hist.undo(), 'undo'); if (ok) toast('Undo: ' + lbl); return ok; }
  function redo() { endRecPass(); const lbl = app.hist.redoLabel(); if (!app.hist.canRedo()) { toast('Nothing to redo'); return false; } const ok = applyState(app.hist.redo(), 'redo'); if (ok) toast('Redo: ' + lbl); return ok; }

  // ---- selection
  function setSel(ids, primary, quiet) {
    const set = new Set(ids == null ? [] : (typeof ids === 'string' ? [ids] : ids));
    for (const id of [...set]) if (!noteById(id)) set.delete(id);
    let p = primary !== undefined ? primary : app.primary;
    if (p && !noteById(p)) p = null;
    if (p && !set.has(p)) { if (primary !== undefined) set.add(p); else p = null; }
    if (!p && set.size) p = [...set][0];
    const changed = set.size !== app.sel.size || [...set].some(id => !app.sel.has(id)) || p !== app.primary;
    app.sel = set; app.primary = p || null;
    for (const id of [...app.nodeSel.keys()]) if (!set.has(id)) { app.nodeSel.delete(id); app.nodeLen.delete(id); }
    if (app.activeNode && !set.has(app.activeNode.id)) setActiveNode(null);
    requestDraw(); dirty.strip = true; updateSelInfo(); syncGlideUI();
    if (changed && !quiet) emitSelection();
  }
  const emitSelection = () => emit('selection', { ids: getSelection(), primary: app.primary });
  function getSelection() { return selectedNotes().map(n => n.id); }
  function pruneSelection() {
    const keep = [...app.sel].filter(id => noteById(id));
    const pOk = app.primary && noteById(app.primary);
    if (keep.length !== app.sel.size || (app.primary && !pOk)) setSel(keep, pOk ? app.primary : null);
  }
  function setPrimary(id) { if (id && !app.sel.has(id)) setSel([...app.sel, id], id); else if (id !== app.primary) { app.primary = id; requestDraw(); emitSelection(); syncGlideUI(); } }
  function setActiveNode(nd) {
    if (nd && nd.len == null) { const n = noteById(nd.id); nd = Object.assign({}, nd, { len: n && n.c[nd.lane] ? n.c[nd.lane].length : 0 }); }
    app.activeNode = nd; emit('node', nd);
  }
  // node picks are indices: when a note's node count changes (a node added or deleted, a shape, a recipe...),
  // its picks would point at other nodes, so they are dropped (the add/delete gestures shift them themselves)
  function setNodePicks(map) {
    const AL = activeLane();
    app.nodeSel = map; app.nodeLen = new Map();
    for (const id of map.keys()) { const n = noteById(id); app.nodeLen.set(id, n && AL && n.c[AL] ? n.c[AL].length : -1); }
  }
  function validateNodes() {
    const AL = activeLane();
    let changed = false;
    for (const id of [...app.nodeSel.keys()]) {
      const n = noteById(id), len = n && AL && n.c[AL] ? n.c[AL].length : -1;
      if (len < 0 || app.nodeLen.get(id) !== len) { app.nodeSel.delete(id); app.nodeLen.delete(id); changed = true; }
    }
    const a = app.activeNode;
    if (a) { const n = noteById(a.id); if (!n || !n.c[a.lane] || n.c[a.lane].length !== a.len || a.k >= n.c[a.lane].length) setActiveNode(null); }
    if (changed) emit('nodes', app.nodeSel);
  }
  function updateSelInfo() {
    const n = app.sel.size;
    $('selInfo').textContent = n ? n + (n === 1 ? ' note selected' : ' notes selected') : 'No notes selected';
    $('dupBtn').disabled = !n; $('delBtn').disabled = !n;
  }

  // =============================================================================================== audio
  const audio = { ctx: null, master: null, analyser: null, node: null, sp: null, eng: null, path: 'off', ready: false, starting: null, buf: null };
  // lastDisplay: the display the transport reads (cleared on Play and on a jump, so the beat is never read from before
  // it); newestDisplay: simply the newest message (the test hook)
  let lastDisplay = null, newestDisplay = null, lastDisplayAt = 0, errorToastAt = 0;
  function engineSource() {
    return createStrandEngineModule.toString() + '\n' + strandWorkletGlue.toString() + '\nstrandWorkletGlue();\n';
  }
  function audioRunning() { return !!(audio.ctx && audio.ctx.state === 'running' && audio.ready); }
  function initAudio() {
    if (audio.ctx) { if (audio.ctx.state !== 'running') audio.ctx.resume().catch(() => {}); return audio.starting; }
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) { $('engineStatus').textContent = 'Sound engine: not available in this browser'; return null; }
    let ctx;
    try { ctx = new AC({ latencyHint: 'interactive' }); } catch (e) { try { ctx = new AC(); } catch (e2) { $('engineStatus').textContent = 'Sound engine: could not start'; return null; } }
    audio.ctx = ctx;
    audio.master = ctx.createGain(); audio.master.gain.value = 0.85;
    audio.analyser = ctx.createAnalyser(); audio.analyser.fftSize = 4096; audio.analyser.smoothingTimeConstant = 0;
    audio.buf = new Float32Array(audio.analyser.fftSize);
    audio.master.connect(audio.analyser); audio.analyser.connect(ctx.destination);
    ctx.onstatechange = updateSoundChip;
    if (ctx.state !== 'running') ctx.resume().catch(() => {});
    audio.starting = (async () => {
      let ok = false;
      try {
        if (ctx.audioWorklet && typeof window.AudioWorkletNode === 'function') {
          const url = URL.createObjectURL(new Blob([engineSource()], { type: 'application/javascript' }));
          await Promise.race([ctx.audioWorklet.addModule(url), new Promise((_, rej) => setTimeout(() => rej(new Error('worklet load timed out')), 4000))]);
          const node = new window.AudioWorkletNode(ctx, 'strand-engine', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
          node.port.onmessage = e => onEngineMsg(e.data);
          node.onprocessorerror = () => { console.warn('StrandLock: the worklet stopped; switching to the backup engine'); switchToFallback(); };
          node.connect(audio.master);
          audio.node = node; audio.path = 'worklet'; ok = true;
        }
      } catch (err) {
        console.warn('StrandLock: AudioWorklet refused here (' + (err && err.message || err) + '); using the backup engine');
      }
      if (!ok) startFallback();
      audio.ready = true;
      sendAll();
      updateSoundChip();
      emit('transport', { playing: app.playing, bpm: app.bpm, loop: app.loop });
    })();
    updateSoundChip();
    return audio.starting;
  }
  function startFallback() {
    const ctx = audio.ctx, SE = createStrandEngineModule();
    const eng = new SE.Engine({ sampleRate: ctx.sampleRate });
    const sp = ctx.createScriptProcessor(2048, 1, 2);
    let cnt = 0;
    const every = Math.round(ctx.sampleRate / 30);
    sp.onaudioprocess = e => {
      const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
      try { eng.process(L, R, L.length); } catch (err) { L.fill(0); R.fill(0); reportEngineError(err); }
      cnt += L.length;
      if (cnt >= every) { cnt = 0; try { const d = eng.getDisplay(); d.type = 'display'; d.stats = eng.stats(); onEngineMsg(d); } catch (err) { /* display is optional */ } }
    };
    sp.connect(audio.master);
    audio.sp = sp; audio.eng = eng; audio.path = 'fallback';
  }
  function switchToFallback() {
    if (audio.path === 'fallback') return;
    try { if (audio.node) audio.node.disconnect(); } catch (e) { /* already gone */ }
    audio.node = null;
    startFallback(); sendAll(); updateSoundChip();
  }
  function reportEngineError(err) { onEngineMsg({ type: 'error', message: String(err && err.message || err) }); }
  function fallbackHandle(m) {
    const E = audio.eng;
    try {
      switch (m.type) {
        case 'sample': E.setSample(m); break;
        case 'inst': E.setInst(m.inst); break;
        case 'snapshot': E.setSnapshot(m.snapshot); break;
        case 'transport': E.setTransport(m); break;
        case 'noteOn': E.noteOn(m.p, m.vel, m.opts); break;
        case 'noteOff': E.noteOff(m.p); break;
        case 'ctrl': E.setController(m.name, m.value); break;
        case 'allOff': E.allNotesOff(); break;
      }
    } catch (err) { reportEngineError(err); }
  }
  const postCounts = {};
  function post(msg) {
    if (!audio.ready) return false;
    postCounts[msg.type] = (postCounts[msg.type] || 0) + 1;
    if (audio.path === 'worklet' && audio.node) audio.node.port.postMessage(msg);
    else if (audio.path === 'fallback' && audio.eng) fallbackHandle(msg);
    else return false;
    return true;
  }
  function postInst() { post({ type: 'inst', inst: app.inst }); }
  function postTransport(withBeat) {
    const m = { type: 'transport', playing: app.playing, bpm: app.bpm, loop: app.loop };
    if (withBeat != null) m.beat = withBeat;
    post(m);
  }
  function postSample() {
    const S = app.sample;
    if (!S) return;
    post({ type: 'sample', channels: S.channels, sampleRate: S.sampleRate, name: S.name });
  }
  function sendAll() {
    if (!audio.ready) return;
    postSample(); postInst();
    snapPending = true; flushSnapshot();
    postTransport(app.playing ? curBeat() : app.startBeat);
    for (const w of [0, 1]) if (app.wheelTouched[w]) post({ type: 'ctrl', name: 'mod' + (w + 1), value: app.wheels[w] });
  }
  function onEngineMsg(m) {
    if (!m) return;
    if (m.type === 'display') {
      lastDisplay = newestDisplay = m; lastDisplayAt = performance.now();
      dirty.strip = true;
      if (app.playing && !app.loop && m.playing && m.beat >= total()) { stop(); app.startBeat = 0; }
      emit('display', m);
    } else if (m.type === 'error') {
      console.warn('StrandLock engine:', m.message);
      const now = Date.now();
      if (now - errorToastAt > 3000) { errorToastAt = now; toast('Sound engine problem: ' + m.message); }
    }
  }
  function updateSoundChip() {
    const running = !!(audio.ctx && audio.ctx.state === 'running');
    $('soundOn').hidden = running && audio.ready;
    $('engineStatus').textContent = !audio.ready ? (audio.ctx ? 'Sound engine: starting' : 'Sound engine: off')
      : 'Sound engine: ' + (audio.path === 'worklet' ? 'worklet' : audio.path === 'fallback' ? 'backup' : 'off');
    $('engineStatus').title = audio.path === 'fallback' ? 'This page could not start the separate sound thread, so the sound runs on the page itself (a little more delay).' : '';
  }
  function meterNow() {
    if (!audio.analyser) return { peak: 0, rms: 0 };
    const b = audio.buf;
    audio.analyser.getFloatTimeDomainData(b);
    let pk = 0, ss = 0;
    for (let i = 0; i < b.length; i++) { const v = b[i]; const a = v < 0 ? -v : v; if (a > pk) pk = a; ss += v * v; }
    return { peak: pk, rms: Math.sqrt(ss / b.length) };
  }
  let meterHold = 0;
  function drawMeter() {
    const cv = $('meter'), w = 64, h = 10, c = sizeCanvas(cv, w, h);
    c.fillStyle = TH['meter-bg']; c.fillRect(0, 0, w, h);
    if (!audio.analyser) return;
    const m = meterNow(), db = m.peak > 1e-6 ? 20 * Math.log10(m.peak) : -90;
    const f = clamp((db + 60) / 60, 0, 1);
    meterHold = Math.max(f, meterHold - 0.015);
    c.fillStyle = db > -1 ? TH['meter-hot'] : TH['meter-ok'];
    c.fillRect(0, 0, Math.round(f * w), h);
    c.fillStyle = TH.txt; c.fillRect(Math.round(meterHold * w) - 1, 0, 1, h);
  }
  // live notes (keyboard, piano, MIDI, strip audition)
  const held = new Map();   // key -> p
  function noteOn(p, vel, opts) { if (!audio.ready) initAudio(); post({ type: 'noteOn', p, vel: vel == null ? 0.8 : vel, opts: opts || undefined }); }
  function noteOff(p) { post({ type: 'noteOff', p }); }
  function liveOn(key, p, vel) {
    if (held.has(key)) return;
    held.set(key, p);
    noteOn(p, vel);
    if (app.rec && app.playing) {
      // the same key again while the pedal still holds its last note: that note ends here
      const b = curBeat();
      for (let i = recSus.length - 1; i >= 0; i--) if (recSus[i].p === p) commitRec(recSus.splice(i, 1)[0], b);
      recLive.set(key, { p, vel: vel == null ? 0.8 : vel, start: b, samples: [] });
    }
    dirty.piano = true;
  }
  function liveOff(key) {
    if (!held.has(key)) return;
    const p = held.get(key);
    held.delete(key);
    if (![...held.values()].includes(p)) noteOff(p);
    const o = recLive.get(key);
    if (o) {
      recLive.delete(key);
      if (app.rec && app.playing) { if (app.sustain) recSus.push(o); else commitRec(o, curBeat()); }   // the pedal holds it on
    }
    dirty.piano = true;
  }
  function audition(p) {
    if (!audioRunning()) return;
    post({ type: 'noteOn', p, vel: 0.7 });
    setTimeout(() => { if (![...held.values()].includes(p)) post({ type: 'noteOff', p }); }, 220);
  }

  // =============================================================================================== transport
  function curBeat() {
    if (!app.playing) return app.startBeat;
    if (!lastDisplay || !lastDisplay.playing) return app.startBeat;
    let b = lastDisplay.beat + (performance.now() - lastDisplayAt) / 1000 * app.bpm / 60;
    if (app.loop) { const T = total(); b = b - T * Math.floor(b / T); }
    return b;
  }
  function play() {
    if (!audio.ctx) initAudio();
    if (audio.ctx && audio.ctx.state !== 'running') audio.ctx.resume().catch(() => {});
    if (app.startBeat >= total()) app.startBeat = 0;
    app.playing = true;
    lastDisplay = null;
    postTransport(app.startBeat);
    paintTransport(); emit('transport', { playing: true, bpm: app.bpm, loop: app.loop });
  }
  function stop() {
    if (!app.playing) return;
    const b = curBeat();
    app.playing = false;
    app.startBeat = clamp(b, 0, total());
    postTransport(app.startBeat);
    flushRec(b);   // a key (or the pedal) still down ends where the song stopped
    paintTransport(); requestDraw();
    emit('transport', { playing: false, bpm: app.bpm, loop: app.loop });
  }
  function paintTransport() {
    $('play').innerHTML = app.playing ? '&#9632; Stop' : '&#9654; Play';
    $('play').classList.toggle('on', app.playing);
    $('loopBtn').classList.toggle('on', app.loop);
  }
  function setBpm(v) {
    app.bpm = clamp(Math.round(num(+v, 96)), 40, 240);
    $('bpm').value = app.bpm;
    postTransport(null);
    requestDraw(); saveSoon();
    emit('transport', { playing: app.playing, bpm: app.bpm, loop: app.loop });
  }
  function setStartBeat(b) {
    app.startBeat = clamp(snapR(b), 0, total());
    if (app.playing) { lastDisplay = null; postTransport(app.startBeat); }
    requestDraw();
  }
  // a new sound silences every voice in the engine; while the song plays, stop and start the engine's transport at
  // the same beat so the notes already under way come back at once (the engine chases on Play), not at their next start
  function rechase() {
    if (!app.playing || !audio.ready) return;
    const b = curBeat();
    post({ type: 'transport', playing: false, bpm: app.bpm, loop: app.loop });
    post({ type: 'transport', playing: true, bpm: app.bpm, loop: app.loop, beat: b });
  }

  // =============================================================================================== sounds
  // Sounds belong to the undo history: each step stores app.soundRef (a starter id, or 'file:N' for a file the user
  // loaded). The sample objects are kept here (the last few files), so Undo and Redo can bring a sound back as it was.
  const soundCache = new Map();    // starter id -> SLSounds.make result (40-300 ms to make, so made once)
  const sampleObjs = new Map();    // sound ref -> the sample object the page uses (parts, peaks and detection kept)
  const soundMeta = new Map();     // sound ref -> { root, cents }: the root belongs to the sound, not to an undo step
  const FILES_KEPT = 3;
  let partsTimer = 0, fileCount = 0;
  const LONG_FILE = 20;            // seconds: a longer file is a song (U2 picks a part; the root is only suggested)
  function setSample(info, ref) {
    let S;
    if (info && info._ref) S = info;   // a sound loaded before (Undo / Redo / the same starter again)
    else {
      const ch = info.channels;
      S = { id: info.id, name: info.name, channels: ch, sampleRate: info.sampleRate, seconds: ch[0].length / info.sampleRate, builtIn: !!info.builtIn, parts: null, peaks: null,
        pitch: null, key: null, analysed: false,   // pitch and key: runDetect's result for a user file ({midi, cents} / {key, scale}), set when analysed
        rootSug: null, keySug: null, audioBuf: info.audioBuf || null, _ref: ref };
      sampleObjs.set(ref, S);
      if (!S.builtIn) {   // keep only the last few files in memory
        const files = [...sampleObjs.keys()].filter(k => k.startsWith('file:'));
        while (files.length > FILES_KEPT) { const k = files.shift(); if (k !== ref) { sampleObjs.delete(k); soundMeta.delete(k); } }
      }
    }
    stopAudition();
    app.sample = S; app.soundRef = S._ref;
    app.soundId = S.builtIn ? S.id : 'file';
    SV.a = 0; SV.b = 1; waveCache.key = '';
    postSample();
    rechase();
    fillSoundSel();
    updateStripText();
    dirty.strip = true;
    emit('sample', S);
    if (S.parts) { emit('parts', S.parts); return; }
    clearTimeout(partsTimer);
    partsTimer = setTimeout(() => {
      if (app.sample !== S) return;
      try { S.parts = AN.suggestParts(S.channels, S.sampleRate) || []; } catch (e) { console.warn('StrandLock: part suggestions failed', e); S.parts = []; }
      if (app.sample === S) emit('parts', S.parts);
    }, 60);
  }
  // Switch to a sound: its own root and cents, and the whole file as the Region (Region values are fractions of a
  // file, so the old file's Region means nothing for the new one). keepRegion: only for the saved session's starter.
  function switchSound(ref, info, keepRegion) {
    if (!soundMeta.has(ref)) soundMeta.set(ref, { root: 60, cents: 0 });   // the starters are rooted at C3 (MIDI 60)
    const meta = soundMeta.get(ref), patch = { root: meta.root, cents: meta.cents };
    if (!keepRegion) patch.region = { start: 0, end: 1 };
    app.soundRef = ref;   // first, so setInst files the root under the new sound
    setInst(M.mergeInst(app.inst, patch), null);
    setSample(info, ref);
  }
  function starterSample(id) {
    if (sampleObjs.has(id)) return sampleObjs.get(id);
    let s = soundCache.get(id);
    if (!s) { s = SND.make(id); soundCache.set(id, s); }
    return { id, name: s.name, channels: s.channels, sampleRate: s.sampleRate, builtIn: true };
  }
  // opts.label: the undo step's name (default 'Sound: <name>'); null = no step of its own (a Sound preset makes the
  // step once its settings are in); opts.keepRegion: keep the Region (only when the page opens on its saved starter)
  function loadSound(id, opts) {
    opts = opts || {};
    if (!SND.list.some(s => s.id === id)) { toast('No built-in sound called ' + id); return false; }
    const info = starterSample(id);
    switchSound(id, info, !!opts.keepRegion);
    if (opts.label !== null) pushHistory(opts.label || 'Sound: ' + app.sample.name);
    saveSoon();
    return true;
  }
  // a sound the history refers to (Undo / Redo); false when that file is no longer kept
  function restoreSound(ref) {
    if (ref.startsWith('file:')) {
      const S = sampleObjs.get(ref);
      if (!S) { toast('That sound file is no longer loaded here, so the current sound stays'); return false; }
      setSample(S, ref);
      return true;
    }
    if (!SND.list.some(s => s.id === ref)) return false;
    if (!soundMeta.has(ref)) soundMeta.set(ref, { root: 60, cents: 0 });
    setSample(starterSample(ref), ref);
    return true;
  }
  function decodeAudio(ab) {
    const ctx = audio.ctx || new (window.OfflineAudioContext || window.webkitOfflineAudioContext)(2, 1, 48000);
    return new Promise((res, rej) => { const p = ctx.decodeAudioData(ab, res, rej); if (p && p.then) p.then(res, rej); });
  }
  async function loadFile(f) {
    if (!f) return;
    toast('Loading ' + f.name + '...');
    try {
      const buf = await decodeAudio(await f.arrayBuffer());
      const chans = [];
      for (let c = 0; c < Math.min(2, buf.numberOfChannels); c++) chans.push(buf.getChannelData(c));
      if (!chans.length || chans[0].length < 64) throw new Error('empty');
      const ref = 'file:' + (++fileCount);
      soundMeta.set(ref, { root: 60, cents: 0 });   // until detection finds the file's own pitch
      switchSound(ref, { id: 'file', name: f.name, channels: chans, sampleRate: buf.sampleRate, builtIn: false, audioBuf: buf }, false);
      pushHistory('Load ' + f.name);   // one undo step: the file, the whole file as the Region
      saveSoon();
      updateStripText('analysing...');
      setTimeout(() => runDetect('auto'), 40);
    } catch (err) {
      toast('Could not read that file as sound');
    }
  }
  // the Region of a sound as its own channels (views, no copy)
  function regionChannels(S, R) {
    const n = S.channels[0].length, a = clamp(Math.floor(R.start * n), 0, n), b = clamp(Math.ceil(R.end * n), a, n);
    return S.channels.map(c => c.subarray(a, b));
  }
  const regionWhole = R => R.start <= 0.0005 && R.end >= 0.9995;
  const rootText = (r, c) => noteName(r) + (c ? ' ' + fmtCents(c) : '');
  // noise-like: the median spectral flatness of the file's louder moments is high (white noise ~0.56, the Noise
  // starter 0.15; held notes, chords and the other starters stay under 0.015). Measured once per sound.
  function noiseLike(S) {
    if (S.noisy != null) return S.noisy;
    let v = false;
    try {
      const f = AN._features && AN._features(S.channels, S.sampleRate);
      if (f && f.nc > 0) {
        let top = -1e9; const a = [];
        for (let c = 0; c < f.nc; c++) top = Math.max(top, f.Lc[c]);
        for (let c = 0; c < f.nc; c++) if (f.Lc[c] > top - 40 && Number.isFinite(f.flat[c])) a.push(f.flat[c]);
        a.sort((x, y) => x - y);
        v = a.length > 0 && a[a.length >> 1] > 0.1;
      }
    } catch (e) { v = false; }
    S.noisy = v;
    return v;
  }
  function pitchToRoot(m) { const r = Math.round(m.midi), c = clamp(Math.round(((m.midi - r) * 100) + (m.cents || 0)), -50, 50); return { root: clamp(r, 0, 127), cents: c }; }
  // Listen to a user file. how = 'auto' (it just loaded) or 'button' (Detect pitch & key).
  // The root belongs to the sound, so applying it is not an undo step. A short file's root is set at once (one clear
  // note); a song's whole-file pitch means nothing, so its root is only suggested from the part the notes play (S1).
  // The key is a suggestion with a one-click button (one undo step), except from the Detect button, which sets it
  // (also one undo step). Nothing here changes the instrument outside the undo history.
  function runDetect(how) {
    const S = app.sample;
    if (!S) return;
    if (S.builtIn) { if (how === 'button') toast('Built-in sounds are tuned to C3 already'); updateStripText(); return; }
    const R = app.inst.region, whole = regionWhole(R), long = S.seconds > LONG_FILE;
    const listen = whole ? S.channels : regionChannels(S, R);
    const lenS = listen[0].length / S.sampleRate;
    let m = null, k = null;
    if (!(long && whole) && lenS >= 0.05 && lenS <= 60) { try { m = AN.detectPitch(listen, S.sampleRate); } catch (e) { m = null; } }
    try { k = AN.detectKey(S.channels, S.sampleRate); } catch (e) { k = null; }
    // detectKey finds a 'key' in anything, noise too (white noise reads E minor): with no pitch and a noise-like
    // spectrum there is no key to offer
    const noisy = !m && noiseLike(S);
    if (noisy) k = null;
    // the message is plain sentences, e.g. 'This song has no single pitch, so pick a part and StrandLock suggests its root. Key: A minor.'
    const said = [], from = whole ? '' : ' (from the Region)';
    S.rootSug = null; S.keySug = null;
    if (m) {
      const rc = pitchToRoot(m);
      if (how === 'button' || !long) {
        setInst(M.mergeInst(app.inst, rc), null);
        said.push('Root set to ' + rootText(rc.root, rc.cents) + from + '.');
      } else {
        S.rootSug = Object.assign({ region: { start: R.start, end: R.end } }, rc);
        said.push('This part sounds like ' + rootText(rc.root, rc.cents) + ': the strip has a button to use it as the root.');
      }
    } else if (long && whole) said.push('This song has no single pitch, so pick a part and StrandLock suggests its root.');
    else if (noisy) said.push('No clear pitch or key (it sounds like noise), so the root stays ' + noteName(app.inst.root) + '.');
    else said.push('No clear pitch (a texture or a chord), so the root stays ' + noteName(app.inst.root) + '.');
    if (k) {
      const name = keyName(k.key, k.scale);
      if (k.key !== app.inst.key || k.scale !== app.inst.scale) {
        if (how === 'button') { patchInst({ key: k.key, scale: k.scale }, 'Key from the sound: ' + name); said.push('Key set to ' + name + '.'); }
        else { S.keySug = { key: k.key, scale: k.scale }; said.push('Key: ' + name + ' (the strip has a button to use it).'); }
      } else said.push('Key: ' + name + '.');
    }
    S.detected = k ? 'key ' + keyName(k.key, k.scale) : m ? '' : 'no clear pitch';
    S.pitch = m || null; S.key = k || null; S.analysed = true;   // kept on the sample, so other panels need not listen again
    updateStripText();
    toast(said.join(' '), 5000);
    emit('detected', { pitch: S.pitch, key: S.key });
  }
  // S1: a new part of a user file only suggests a root (one click to accept). Runs after the Region changes.
  // The old tip goes at once (it was measured on another Region); the new one follows a moment later. A tip is kept
  // with the Region it was measured on and shows only while that is the Region (Undo, Redo, Use the whole file...).
  let rootTimer = 0;
  const sugFits = (g, R) => !!(g && (!g.region || (Math.abs(g.region.start - R.start) < 1e-9 && Math.abs(g.region.end - R.end) < 1e-9)));
  function suggestRoot() {
    clearTimeout(rootTimer);
    const S0 = app.sample;
    if (S0 && S0.rootSug) { S0.rootSug = null; updateStripText(); }
    rootTimer = setTimeout(() => {
      const S = app.sample, R = app.inst.region;
      if (!S || S.builtIn || regionWhole(R)) return;
      const lenS = (R.end - R.start) * S.seconds;
      if (lenS < 0.05 || lenS > 30) return;
      let m = null;
      try { m = AN.detectPitch(regionChannels(S, R), S.sampleRate); } catch (e) { m = null; }
      const rc = m ? pitchToRoot(m) : null;
      const same = rc && Math.abs((rc.root * 100 + rc.cents) - (app.inst.root * 100 + app.inst.cents)) < 20;
      S.rootSug = rc && !same ? Object.assign({ region: { start: R.start, end: R.end } }, rc) : null;
      updateStripText();
    }, 250);
  }
  function useRootSug() {
    const S = app.sample, g = S && S.rootSug;
    if (!g || !sugFits(g, app.inst.region)) { if (S) S.rootSug = null; updateStripText(); return; }
    S.rootSug = null;
    setInst(M.mergeInst(app.inst, { root: g.root, cents: g.cents }), null);   // the root belongs to the sound: not an undo step
    toast('Root ' + rootText(g.root, g.cents) + ': the notes now play this part in tune');
    updateStripText();
  }
  function useKeySug() {
    const S = app.sample, g = S && S.keySug;
    if (!g) return;
    S.keySug = null;
    patchInst({ key: g.key, scale: g.scale }, 'Key from the sound: ' + keyName(g.key, g.scale));
    updateStripText();
  }

  // =============================================================================================== roll geometry
  const rollWrap = $('rollWrap'), rollCv = $('roll'), scroller = $('scroller'), spacer = $('spacer');
  const pianoCv = $('piano'), rulerCv = $('ruler');
  const cy = p => (MAXP - p) * RH + RH / 2;
  const pitchAt = y => clamp(MAXP - Math.floor(y / RH), MINP, MAXP);
  let glideCache = { clip: null, key: '', map: new Map() };
  function glides() {
    const key = app.inst.glideMode + '|' + app.inst.glideTime;
    if (glideCache.clip !== app.clip || glideCache.key !== key) glideCache = { clip: app.clip, key, map: M.computeGlides(app.clip, app.inst) };
    return glideCache.map;
  }
  const geoCache = new WeakMap();
  function geom(n, gmap) {
    const g = (gmap || glides()).get(n.id);
    const key = BW + '|' + app.bpm + '|' + app.inst.vibRate + '|' + (g ? g.d + ',' + g.len + ',' + g.curve : '-');
    const e = geoCache.get(n);
    if (e && e.key === key) return e.g;
    const res = Math.min(400, Math.max(BW / 3, 12 / Math.max(0.01, n.l)));
    const tabs = {};
    const tab = id => tabs[id] || (tabs[id] = M.laneTable(n, id, res));
    const P = tab('pitch'), V = tab('vib'), K = P.length;
    const base = new Float32Array(K), vib = new Float32Array(K), dur = n.l * 60 / app.bpm;
    let smin = Infinity, smax = -Infinity;
    for (let i = 0; i < K; i++) {
      const t = i / (K - 1);
      let s = (P[i] - 0.5) * 24;
      if (g && g.len > 0) { const st = n.l - g.len, tb = t * n.l; if (tb > st) s += g.d * M.gShape((tb - st) / g.len, g.curve); }
      base[i] = s;
      vib[i] = (V.length === K ? V[i] : 0) * Math.sin(2 * Math.PI * app.inst.vibRate * t * dur);
      const a = s + vib[i];
      if (a < smin) smin = a; if (a > smax) smax = a;
    }
    const G = { K, base, vib, tab, smin, smax, glide: g || null };
    geoCache.set(n, { key, g: G });
    return G;
  }
  function baseYAt(n, t, G) {
    G = G || geom(n);
    const fi = clamp(t, 0, 1) * (G.K - 1), i = Math.min(G.K - 2, Math.floor(fi)), fr = fi - i;
    return cy(n.p) - (G.base[i] + (G.base[i + 1] - G.base[i]) * fr) * RH;
  }
  function valueY(n, lane, t, v) { return lane === 'pitch' ? cy(n.p) - (v - 0.5) * 24 * RH : baseYAt(n, t) - v * laneH(); }
  function laneValue(n, lane, t, y) { return lane === 'pitch' ? 0.5 + ((cy(n.p) - y) / RH) / 24 : (baseYAt(n, t) - y) / laneH(); }
  function nodeXY(n, lane, k) {
    const nd = n.c[lane], q = nd[k], x0 = n.s * BW, x1 = (n.s + n.l) * BW;
    return [x0 + (x1 - x0) * q.t, valueY(n, lane, q.t, q.v)];
  }
  function segXY(n, lane, k) {
    const nd = n.c[lane], A = nd[k], B = nd[k + 1], x0 = n.s * BW, x1 = (n.s + n.l) * BW;
    if ((B.t - A.t) * (x1 - x0) < 20) return null;
    const t = (A.t + B.t) / 2, v = M.evalNodes(nd, t);
    return [x0 + (x1 - x0) * t, valueY(n, lane, t, v)];
  }
  function glideHandles(n) {
    const G = geom(n), g = G.glide;
    if (!g || !(g.len > 0)) return null;
    const st = n.s + n.l - g.len, tS = (st - n.s) / n.l, tM = (st + g.len / 2 - n.s) / n.l;
    return { g, start: [st * BW, baseYAt(n, tS, G)], mid: [(st + g.len / 2) * BW, baseYAt(n, tM, G)] };
  }
  function noteBox(n, withRibbon) {
    const G = geom(n), x0 = n.s * BW, x1 = (n.s + n.l) * BW;
    let top = cy(n.p) - G.smax * RH - RH * 0.5, bottom = cy(n.p) - G.smin * RH + RH * 0.5;
    if (withRibbon) top -= laneH();
    return { x0, x1, top, bottom };
  }

  // =============================================================================================== drawing
  const dirty = { roll: true, strip: true, piano: true };
  function requestDraw() { dirty.roll = true; dirty.piano = true; }
  function redraw() { requestDraw(); dirty.strip = true; }
  const noteDecorators = [], stripDecorators = [];
  let drag = null, sdrag = null;
  function sizeSpacer() {
    spacer.style.width = Math.ceil(total() * BW + 60) + 'px';
    spacer.style.height = Math.ceil(ROWS * RH) + 'px';
  }
  function drawRoll() {
    sizeSpacer();
    const W = scroller.clientWidth, H = scroller.clientHeight;
    if (!W || !H) return;
    const c = sizeCanvas(rollCv, W, H), sx = scroller.scrollLeft, sy = scroller.scrollTop;
    const T = total(), Wc = T * BW, I = app.inst, chrom = I.scale === 'Chromatic';
    c.save(); c.translate(-sx, -sy);
    // rows
    const pTop = clamp(MAXP - Math.floor(sy / RH), MINP, MAXP), pBot = clamp(MAXP - Math.floor((sy + H) / RH), MINP, MAXP);
    const xw = Math.max(0, Math.min(sx + W, Wc) - sx);
    for (let p = pBot; p <= pTop; p++) {
      const y = (MAXP - p) * RH, inS = M.inScale(p, I.key, I.scale), isRoot = !chrom && (((p - I.key) % 12) + 12) % 12 === 0;
      c.fillStyle = isRoot ? TH['row-root'] : !inS ? TH['row-out'] : isBlack(p) ? TH['row-black'] : TH.row;
      c.fillRect(sx, y, xw, RH);
      c.fillStyle = p % 12 === 0 ? TH['oct-line'] : TH['row-line'];
      c.fillRect(sx, y + RH - 1, xw, 1);
    }
    // grid
    const yT = (MAXP - pTop) * RH, yB = Math.min(ROWS * RH, (MAXP - pBot + 1) * RH);
    let sub = app.snap || 0.25;
    while (sub * BW < 6 && sub < 4) sub *= 2;
    for (let b = Math.max(0, Math.floor(sx / BW / sub) * sub); b <= Math.min(T, (sx + W) / BW) + 1e-6; b += sub) {
      const x = Math.round(b * BW), isBar = Math.abs(b / 4 - Math.round(b / 4)) < 1e-6, isBeat = Math.abs(b - Math.round(b)) < 1e-6;
      c.fillStyle = isBar ? TH['grid-bar'] : isBeat ? TH['grid-beat'] : TH['grid-sub'];
      c.fillRect(x, yT, 1, yB - yT);
    }
    // notes: unselected, then selected, then the primary on top
    const gm = glides(), AL = activeLane();
    const vis = [];
    for (const n of app.clip.notes) {
      const x0 = n.s * BW, x1 = (n.s + n.l) * BW;
      if (x1 < sx - 20 || x0 > sx + W + 20) continue;
      const G = geom(n, gm), top = cy(n.p) - G.smax * RH - laneH() - 8, bot = cy(n.p) - G.smin * RH + 8;
      if (bot < sy || top > sy + H) continue;
      vis.push(n);
    }
    vis.sort((a, b) => (app.sel.has(a.id) - app.sel.has(b.id)) || ((a.id === app.primary) - (b.id === app.primary)));
    for (const n of vis) {
      const isSel = app.sel.has(n.id), isPrim = n.id === app.primary;
      drawNote(c, n, geom(n, gm), isSel, AL);
      if (noteDecorators.length) runNoteDecorators(c, n, isSel, isPrim, AL);
    }
    drawLinks(c, gm);
    drawHandles(c, AL);
    drawGhost(c, AL);
    if (drag && drag.t === 'box' && drag.moved) {
      const x = Math.min(drag.x0, drag.x1), y = Math.min(drag.y0, drag.y1), w = Math.abs(drag.x1 - drag.x0), h = Math.abs(drag.y1 - drag.y0);
      c.fillStyle = TH['box-fill']; c.fillRect(x, y, w, h);
      c.setLineDash([4, 3]); c.strokeStyle = TH['box-line']; c.lineWidth = 1; c.strokeRect(x + 0.5, y + 0.5, w, h); c.setLineDash([]);
    }
    c.restore();
    drawRuler(); drawPiano();
  }
  function ribbonPath(c, n, G, arr, lh) {
    const x0 = n.s * BW, x1 = (n.s + n.l) * BW, K = G.K, base = cy(n.p);
    c.beginPath();
    for (let i = 0; i < K; i++) c.lineTo(x0 + (x1 - x0) * i / (K - 1), base - G.base[i] * RH - arr[i] * lh);
    for (let i = K - 1; i >= 0; i--) c.lineTo(x0 + (x1 - x0) * i / (K - 1), base - G.base[i] * RH);
    c.closePath();
  }
  function drawNote(c, n, G, isSel, AL) {
    const x0 = n.s * BW, x1 = (n.s + n.l) * BW, K = G.K, lh = laneH(), base = cy(n.p);
    const X = i => x0 + (x1 - x0) * i / (K - 1), BY = i => base - G.base[i] * RH;
    let order;
    if (app.mode === 1) order = SOUND_LANES;
    else if (app.mode === 8) order = SOUND_LANES.concat([AL]);
    else order = SOUND_LANES.filter(id => id !== AL).concat(AL === 'pitch' ? [] : [AL]);
    // Refresh does nothing while the Sample tab's Refresh is Smooth: draw that line dimmed (like the faded knobs)
    const dimRefresh = !!(app.inst && app.inst.refresh && app.inst.refresh.mode === 'smooth');
    for (const id of order) {
      const act = id === AL, a = G.tab(id);
      if (a.length !== K) continue;
      const dim = id === 'refresh' && dimRefresh ? 0.35 : 1;
      ribbonPath(c, n, G, a, lh);
      c.globalAlpha = dim * (act ? (isSel ? TH['a-act-sel'] : TH['a-act'])
        : app.mode === 1 ? (isSel ? TH['a-idle-sel'] : TH['a-idle']) : TH['a-idle'] * (app.mode === 8 ? 0.45 : 0.7));
      c.fillStyle = laneColor(id); c.fill();
      if (act) {
        c.globalAlpha = dim * (isSel ? 1 : 0.65); c.strokeStyle = laneColor(id); c.lineWidth = 1.6;
        c.beginPath(); for (let i = 0; i < K; i++) c.lineTo(X(i), BY(i) - a[i] * lh); c.stroke();
      }
    }
    // Travel (speed kind): the middle line is 'stopped'
    if (AL === 'travel' && isSel && n.sample.travel !== 'path') {
      c.globalAlpha = 0.7; c.setLineDash([3, 3]); c.strokeStyle = laneColor('travel'); c.lineWidth = 1;
      c.beginPath(); for (let i = 0; i < K; i++) c.lineTo(X(i), BY(i) - 0.5 * lh); c.stroke(); c.setLineDash([]);
    }
    c.globalAlpha = 1;
    // the note line: pitch + glide, vibrato drawn as a real squiggle
    c.beginPath(); for (let i = 0; i < K; i++) c.lineTo(X(i), BY(i) - G.vib[i] * RH);
    c.strokeStyle = isSel ? TH['note-line-sel'] : TH['note-line']; c.lineWidth = isSel ? 3.2 : 2.4; c.stroke();
    c.beginPath(); c.arc(x0 + 3, BY(0) - G.vib[0] * RH, isSel ? 6 : 4.5, 0, 7); c.fillStyle = TH['dot-fill']; c.fill(); c.lineWidth = 2.2; c.strokeStyle = TH['start-ring']; c.stroke();
    c.beginPath(); c.arc(x1 - 3, BY(K - 1) - G.vib[K - 1] * RH, isSel ? 6 : 4, 0, 7); c.fillStyle = isSel ? TH['note-line-sel'] : TH['note-line']; c.fill(); c.lineWidth = 2; c.strokeStyle = TH['dot-fill']; c.stroke();
  }
  function runNoteDecorators(c, n, isSel, isPrim, AL) {
    const G = geom(n), x0 = n.s * BW, x1 = (n.s + n.l) * BW;
    const rect = { x0, x1, y0: baseYAt(n, 0, G), y1: baseYAt(n, 1, G), top: cy(n.p) - G.smax * RH - laneH(), bottom: cy(n.p) - G.smin * RH };
    const info = { selected: isSel, primary: isPrim, mode: app.mode, lane: AL, BW, RH, laneH: laneH(), theme: TH, inst: app.inst, bpm: app.bpm, K: G.K,
      table: id => G.tab(id), yAt: t => baseYAt(n, t, G), valueY: (lane, t, v) => valueY(n, lane, t, v) };
    for (const fn of noteDecorators) { c.save(); try { fn(c, n, rect, info); } catch (e) { console.warn('SLApp note decorator failed:', e); } c.restore(); }
  }
  function drawLinks(c, gm) {
    c.save(); c.setLineDash([4, 4]); c.strokeStyle = TH.link; c.lineWidth = 1.5; c.fillStyle = TH.link; c.font = '11px sans-serif';
    for (const [id, g] of gm) {
      if (!g.linked) continue;
      const n = noteById(id), t = noteById(g.toId);
      if (!n || !t) continue;
      const x1 = (n.s + n.l) * BW, y1 = baseYAt(n, 1);
      const yT = cy(t.p) - (M.evalNodes(t.c.pitch, 0) - 0.5) * 24 * RH;
      c.beginPath(); c.moveTo(x1, y1); c.lineTo(t.s * BW, yT); c.stroke();
      c.fillText('\u{1F517}', x1 + 3, y1 - 4);
    }
    c.restore();
  }
  function diamond(c, x, y, r, fill, stroke) {
    c.save(); c.translate(x, y); c.rotate(Math.PI / 4);
    if (fill) { c.fillStyle = fill; c.fillRect(-r, -r, 2 * r, 2 * r); }
    c.strokeStyle = stroke; c.lineWidth = 2; c.strokeRect(-r, -r, 2 * r, 2 * r);
    c.restore();
  }
  function handleNotes() {
    const P = primaryNote();
    const sel = selectedNotes();
    if (sel.length <= 16) return sel;
    return P ? [P] : [];
  }
  function drawHandles(c, AL) {
    const P = primaryNote();
    if (P && !app.free && (app.mode === 1 || AL === 'pitch')) {
      const gh = glideHandles(P);
      if (gh) { diamond(c, gh.start[0], gh.start[1], 5, TH.link, TH.link); diamond(c, gh.mid[0], gh.mid[1], 5, TH['dot-fill'], TH.link); }
    }
    if (!AL) return;
    const col = laneColor(AL);
    for (const n of handleNotes()) {
      const nd = n.c[AL], ks = app.nodeSel.get(n.id);
      c.setLineDash([3, 3]); c.strokeStyle = col; c.lineWidth = 1; c.globalAlpha = 0.9;
      c.beginPath(); for (let k = 0; k < nd.length; k++) { const [x, y] = nodeXY(n, AL, k); c.lineTo(x, y); } c.stroke();
      c.setLineDash([]); c.globalAlpha = 1;
      if (nd.length > 64 && !ks) continue;
      // crowded nodes (a Squiggle on a short note): small dots, and below 6 px apart only the ends, so the note
      // stays readable; H+ (zoom in) brings the full handles back
      const gap = nd.length > 1 ? (n.l * BW) / (nd.length - 1) : 99, small = gap < 14, tiny = gap < 6;
      for (let k = 0; k < nd.length; k++) {
        const [x, y] = nodeXY(n, AL, k), picked = ks && ks.has(k);
        const isAct = app.activeNode && app.activeNode.id === n.id && app.activeNode.k === k && app.activeNode.lane === AL;
        if (tiny && !picked && !isAct && k !== 0 && k !== nd.length - 1) continue;
        c.beginPath(); c.arc(x, y, picked || isAct ? 6.5 : small ? 3 : 5.5, 0, 7); c.fillStyle = col; c.fill();
        c.lineWidth = small && !picked && !isAct ? 1 : 2; c.strokeStyle = picked ? TH['node-sel'] : TH['handle-ring']; c.stroke();
      }
    }
    if (P && !app.free && !(app.nodesMode && app.nodeSel.size)) {
      const nd = P.c[AL];
      for (let k = 0; k < nd.length - 1; k++) {
        const xy = segXY(P, AL, k);
        if (!xy) continue;
        const bent = nd[k].shape === 'bend' && Math.abs(nd[k].bend) > 1e-3;
        diamond(c, xy[0], xy[1], 3.6, bent ? col : TH['dot-fill'], col);
      }
    }
  }
  function drawGhost(c, AL) {
    const gc = app.ghost;
    if (!gc || !Array.isArray(gc.notes)) return;
    const gm = M.computeGlides(gc, app.inst), lh = laneH();
    c.save(); c.globalAlpha = 0.75; c.setLineDash([5, 4]); c.lineWidth = 1.6; c.strokeStyle = TH.ghost;
    for (const n of gc.notes) {
      if (noteById(n.id) === n) continue;   // unchanged note (shared object)
      const G = geom(n, gm), x0 = n.s * BW, x1 = (n.s + n.l) * BW, K = G.K, base = cy(n.p);
      const X = i => x0 + (x1 - x0) * i / (K - 1);
      if (AL && AL !== 'pitch') {
        const a = G.tab(AL);
        c.beginPath(); for (let i = 0; i < K; i++) c.lineTo(X(i), base - G.base[i] * RH - a[i] * lh); c.stroke();
      }
      c.beginPath(); for (let i = 0; i < K; i++) c.lineTo(X(i), base - G.base[i] * RH - G.vib[i] * RH); c.stroke();
    }
    c.restore();
  }
  function drawPiano() {
    const wrap = $('pianoWrap'), H = wrap.clientHeight, sy = scroller.scrollTop;
    if (!H) return;
    const c = sizeCanvas(pianoCv, 40, H), I = app.inst, heldP = new Set(held.values());
    c.translate(0, -sy);
    const pTop = clamp(MAXP - Math.floor(sy / RH), MINP, MAXP), pBot = clamp(MAXP - Math.floor((sy + H) / RH), MINP, MAXP);
    for (let p = pBot; p <= pTop; p++) {
      const y = (MAXP - p) * RH, bk = isBlack(p), hl = heldP.has(p);
      c.fillStyle = hl ? TH['piano-held'] : bk ? TH['piano-black'] : TH['piano-white']; c.fillRect(0, y, bk ? 26 : 40, RH - 1);
      if (bk) { c.fillStyle = hl ? TH['piano-held'] : TH['piano-white']; c.fillRect(26, y, 14, RH - 1); }
      if (!M.inScale(p, I.key, I.scale)) { c.fillStyle = TH['piano-out']; c.fillRect(0, y, 40, RH - 1); }
      if (p % 12 === 0) { c.fillStyle = TH['piano-text']; c.font = '9px sans-serif'; c.fillText(noteName(p), 3, y + RH - 3); }
    }
    c.fillStyle = TH['oct-line']; c.fillRect(39, sy, 1, H);
  }
  function drawRuler() {
    const wrap = $('rulerWrap'), W = wrap.clientWidth, sx = scroller.scrollLeft;
    if (!W) return;
    const c = sizeCanvas(rulerCv, W, 20);
    c.translate(-sx, 0); c.font = '10px sans-serif';
    const T = total(), step = BW < 10 ? 4 : 1;
    for (let b = Math.max(0, Math.floor(sx / BW)); b < Math.min(T, (sx + W) / BW + 1); b++) {
      if (b % step) continue;
      const x = b * BW, bar = b % 4 === 0;
      c.fillStyle = bar ? TH['ruler-text'] : TH['ruler-tick']; c.fillRect(x, bar ? 5 : 13, 1, 20);
      if (bar) c.fillText(String(b / 4 + 1), x + 4, 13);
    }
    const xs = app.startBeat * BW;
    c.fillStyle = TH.playhead; c.beginPath(); c.moveTo(xs - 5, 12); c.lineTo(xs + 5, 12); c.lineTo(xs, 20); c.closePath(); c.fill();
  }
  function movePH() {
    const b = app.playing ? curBeat() : app.startBeat, x = b * BW - scroller.scrollLeft;
    const ph = $('ph');
    if (x < -2 || x > scroller.clientWidth + 2) ph.style.display = 'none';
    else { ph.style.display = 'block'; ph.style.left = x + 'px'; }
  }

  // =============================================================================================== roll gestures
  let lastTap = { t: 0, x: 0, y: 0, what: null }, pressTimer = 0;
  function isDouble(x, y, what) {
    const now = Date.now(), d = now - lastTap.t < 320 && Math.hypot(x - lastTap.x, y - lastTap.y) < 22 && lastTap.what === what;
    lastTap = { t: d ? 0 : now, x, y, what };
    return d;
  }
  const dist = (x, y, p) => Math.hypot(x - p[0], y - p[1]);
  function rollPos(e) {
    const r = scroller.getBoundingClientRect(), cx = e.clientX - r.left, cyy = e.clientY - r.top;
    return { x: cx + scroller.scrollLeft, y: cyy + scroller.scrollTop, cx, cy: cyy, inBar: cx >= scroller.clientWidth || cyy >= scroller.clientHeight };
  }
  function hitNote(x, y) {
    const lh = laneH();
    const list = app.clip.notes.slice().reverse().sort((a, b) => app.sel.has(b.id) - app.sel.has(a.id));
    for (const n of list) {
      const x0 = n.s * BW, x1 = (n.s + n.l) * BW;
      if (x < x0 - 5 || x > x1 + 5) continue;
      const yl = baseYAt(n, (x - x0) / Math.max(1, x1 - x0));
      if (y >= yl - lh - 2 && y <= yl + RH * 0.8) return n;
    }
    return null;
  }
  function hitNode(x, y, AL) {
    const list = handleNotes();
    const P = primaryNote();
    if (P) list.sort((a, b) => (b.id === P.id) - (a.id === P.id));
    for (const n of list) {
      const nd = n.c[AL];
      for (let k = 0; k < nd.length; k++) if (dist(x, y, nodeXY(n, AL, k)) < 11) return { n, k };
    }
    return null;
  }
  function hitSeg(n, x, y, AL) {
    const nd = n.c[AL];
    for (let k = 0; k < nd.length - 1; k++) { const xy = segXY(n, AL, k); if (xy && dist(x, y, xy) < 6) return k; }   // only right on the ◇
    return -1;
  }
  function showReadout(x, y, text) {
    const r = $('readout');
    r.textContent = text; r.style.display = 'block';
    const sx = x - scroller.scrollLeft, sy = y - scroller.scrollTop, w = r.offsetWidth;
    r.style.left = clamp(sx + 14, 4, Math.max(4, scroller.clientWidth - w - 4)) + 'px';
    r.style.top = clamp(sy - 30, 4, Math.max(4, scroller.clientHeight - 24)) + 'px';
  }
  const hideReadout = () => { $('readout').style.display = 'none'; };
  const beatLabel = b => { const bar = Math.floor(b / 4) + 1, beat = b - Math.floor(b / 4) * 4 + 1; return 'bar ' + bar + ' beat ' + (Math.round(beat * 100) / 100); };
  function bendEndNodes(n, which, y, y0, v0, shift) {
    if (Math.abs(y - y0) < RH * 0.4) return null;
    let semi = (v0 - 0.5) * 24 + (y0 - y) / RH;
    if (app.snap && !shift) semi = Math.round(semi);
    semi = clamp(semi, -12, 12);
    const nd = n.c.pitch.map(q => Object.assign({}, q)), k = which === 'start' ? 0 : nd.length - 1;
    nd[k].v = 0.5 + semi / 24;
    return nd;
  }
  // a double-click on a line adds a node there (on the line itself when onLine), and starts dragging it
  function addNodeAt(n, AL, x, y, onLine) {
    const x0 = n.s * BW, x1 = (n.s + n.l) * BW, t = clamp((x - x0) / Math.max(1, x1 - x0), 0.001, 0.999);
    const nd = n.c[AL].map(q => Object.assign({}, q));
    let k = nd.findIndex(q => q.t > t); if (k <= 0) k = nd.length - 1;
    const prev = nd[k - 1];
    const v = onLine ? M.evalNodes(n.c[AL], t) : laneValue(n, AL, t, y);
    nd.splice(k, 0, { t, v: clamp(v, 0, 1), shape: prev.shape === 'step' ? 'step' : prev.shape === 'linear' ? 'linear' : 'smooth', bend: 0 });
    const ks = app.nodeSel.get(n.id), moved = ks ? new Set([...ks].map(q => (q >= k ? q + 1 : q))) : null;
    const clip = M.setLaneNodes(app.clip, n.id, AL, nd);
    setClip(clip, null);
    if (moved && moved.size) { app.nodeSel.set(n.id, moved); app.nodeLen.set(n.id, nd.length); emit('nodes', app.nodeSel); }
    setActiveNode({ id: n.id, lane: AL, k });
    drag = { t: 'node', id: n.id, lane: AL, k, nodes0: noteIn(clip, n.id).c[AL], clip0: clip, label: 'Add node', force: true };
  }
  function rollDown(e) {
    if (e.button === 2 || e.button === 1) return;
    if (app.pan) {   // Scroll: a touch swipes natively; a mouse or pen drags the roll around
      if (e.pointerType === 'mouse' || e.pointerType === 'pen') {
        e.preventDefault(); closeMenu();
        try { scroller.setPointerCapture(e.pointerId); } catch (err) { /* ok */ }
        drag = { t: 'pan', cx: e.clientX, cy: e.clientY, sl: scroller.scrollLeft, st: scroller.scrollTop };
      }
      return;
    }
    const P0 = rollPos(e);
    if (P0.inBar) return;
    e.preventDefault(); closeMenu();
    try { scroller.setPointerCapture(e.pointerId); } catch (err) { /* ok */ }
    // let go of a focused field or menu, so the computer keys reach the page again (the roll itself never takes the
    // focus: a focused scrolling box would scroll a page down when the spacebar is pressed)
    parkFocus();
    const { x, y } = P0, b = x / BW, AL = activeLane(), P = primaryNote();
    const addMode = app.multi || e.ctrlKey || e.metaKey;
    const linkMode = (e.shiftKey || app.linkArm) && (app.mode === 1 || AL === 'pitch');
    // 1. glide handles of the primary note
    if (P && !app.free && (app.mode === 1 || AL === 'pitch')) {
      const gh = glideHandles(P);
      if (gh) {
        if (dist(x, y, gh.start) < 12) { drag = { t: 'gstart', id: P.id, clip0: app.clip }; return; }
        if (dist(x, y, gh.mid) < 12) { drag = { t: 'gmid', id: P.id, clip0: app.clip, y0: y, c0: P.gc || 0, d: gh.g.d }; return; }
      }
    }
    // 2. lane nodes (and 3. segment diamonds) in lane modes
    if (AL && !app.free) {
      const h = hitNode(x, y, AL);
      if (h) {
        const n = h.n, nd = n.c[AL];
        if (isDouble(x, y, 'node')) {
          if (h.k > 0 && h.k < nd.length - 1) {
            // the picks after the deleted node move down one place, so they stay on the same nodes
            const ks = app.nodeSel.get(n.id), moved = ks ? new Set([...ks].filter(q => q !== h.k).map(q => (q > h.k ? q - 1 : q))) : null;
            setActiveNode(null);
            setClip(M.setLaneNodes(app.clip, n.id, AL, nd.filter((_, i) => i !== h.k)), 'Delete node');
            if (moved && moved.size) { app.nodeSel.set(n.id, moved); app.nodeLen.set(n.id, nd.length - 1); emit('nodes', app.nodeSel); requestDraw(); }
          } else toast('The first and last nodes stay: drag them up or down');
          return;
        }
        const ks = app.nodeSel.get(n.id);
        let count = 0; for (const s of app.nodeSel.values()) count += s.size;
        if (app.nodesMode && ks && ks.has(h.k) && count > 1) {
          drag = { t: 'nodes', x0: x, y0: y, clip0: app.clip, lane: AL, sel: new Map([...app.nodeSel].map(([id, s]) => [id, new Set(s)])) };
          return;
        }
        if (app.primary !== n.id) setPrimary(n.id);
        setActiveNode({ id: n.id, lane: AL, k: h.k });
        // the node keeps its distance to the pointer (a press just off its centre does not make it jump), and it moves
        // only once the mouse has moved a few pixels (a click only selects it)
        const [hx, hy] = nodeXY(n, AL, h.k);
        drag = { t: 'node', id: n.id, lane: AL, k: h.k, nodes0: nd, clip0: app.clip, label: 'Move node', x0: x, y0: y, gx: x - hx, gy: y - hy };
        showReadout(x, y, fmtLane(AL, nd[h.k].v, formatCtx(n)));
        requestDraw();
        return;
      }
      if (P && !(app.nodesMode && app.nodeSel.size)) {
        const k = hitSeg(P, x, y, AL);
        if (k >= 0) {
          const nd = P.c[AL], bent = nd[k].shape === 'bend' && Math.abs(nd[k].bend) > 1e-3;
          if (isDouble(x, y, 'seg')) {
            // a curved segment (filled ◆) straightens; on a straight one there is nothing to straighten, so the
            // double-click adds a node there, as anywhere else on the line
            if (!bent) { addNodeAt(P, AL, x, y, true); return; }
            const nn = nd.map(q => Object.assign({}, q)); nn[k].shape = 'linear'; nn[k].bend = 0;
            setClip(M.setLaneNodes(app.clip, P.id, AL, nn), 'Straighten segment');
            return;
          }
          drag = { t: 'seg', id: P.id, lane: AL, k, nodes0: nd, clip0: app.clip, y0: y, b0: nd[k].shape === 'bend' ? nd[k].bend : 0 };
          return;
        }
      }
    }
    // 4. Build: the end dots of selected notes (sideways = length, up/down = bend that end)
    if (app.mode === 1 && !linkMode) {
      for (const n of selectedNotes()) {
        const G = geom(n), x0 = n.s * BW, x1 = (n.s + n.l) * BW;
        if (dist(x, y, [x1 - 3, cy(n.p) - G.base[G.K - 1] * RH]) < 12) { drag = { t: 'end', id: n.id, y0: y, v0: n.c.pitch[n.c.pitch.length - 1].v, clip0: app.clip, label: 'Change length' }; return; }
        if (dist(x, y, [x0 + 3, cy(n.p) - G.base[0] * RH]) < 12) { drag = { t: 'start', id: n.id, y0: y, v0: n.c.pitch[0].v, end: n.s + n.l, clip0: app.clip }; return; }
      }
    }
    // 5. Freehand: paint the active lane of the primary note; with no note selected there (or none at all), the note
    // under the mouse is selected and painted in the same drag
    if (app.free && AL) {
      let F = P && x >= P.s * BW - 6 && x <= (P.s + P.l) * BW + 6 ? P : null;
      if (!F) { const hn = hitNote(x, y); if (hn) { setSel([hn.id], hn.id); F = noteById(hn.id); } }
      if (F) { drag = { t: 'free', id: F.id, lane: AL, arr: Array.from(M.sampleNodes(F.c[AL], 97)), li: null, lv: 0, clip0: app.clip }; paint(x, y); return; }
    }
    // 6. a note
    const h = hitNote(x, y);
    // Nodes switch: a drag that starts on a selected note (off its nodes) draws the node box
    if (h && app.nodesMode && AL && app.sel.has(h.id) && !app.multi) {
      // ... except a double-click on the line, which adds a node there (as with the switch off)
      if (!app.free && isDouble(x, y, 'note') && h.id === (P && P.id)) { addNodeAt(h, AL, x, y, false); return; }
      drag = { t: 'box', x0: x, y0: y, x1: x, y1: y, shift: e.shiftKey, ctrl: e.ctrlKey || e.metaKey, base: new Set(app.sel), nodes: true, clickNote: h.id,
        baseNodes: new Map([...app.nodeSel].map(([id, s]) => [id, new Set(s)])), moved: false };
      return;
    }
    if (h) {
      // with Shift or Ctrl held, a press on the faint band above a note's line waits to see what it is: a drag draws a
      // selection box (Shift adds, Ctrl adds or removes, as on empty space); a click does what a click on the line
      // does (Shift = slide into it, Ctrl = add or remove it)
      if ((e.shiftKey || e.ctrlKey || e.metaKey) && !app.linkArm && !onNoteLine(h, x, y)) {
        drag = { t: 'box', x0: x, y0: y, x1: x, y1: y, shift: e.shiftKey, ctrl: e.ctrlKey || e.metaKey, base: new Set(app.sel), nodes: false,
          baseNodes: new Map([...app.nodeSel].map(([id, s]) => [id, new Set(s)])), moved: false, clickAct: { id: h.id, link: linkMode, add: addMode } };
        return;
      }
      if (linkMode && P && h.id !== P.id) { linkNotes(P, h); return; }
      if (!pickNote(h, addMode)) return;
      if (AL && !app.free && isDouble(x, y, 'note') && h.id === (P && P.id)) { addNodeAt(h, AL, x, y, false); return; }
      if (app.mode !== 1) return;
      const ids = [...app.sel];
      drag = { t: 'move', b0: b, p0: pitchAt(y), clip0: app.clip, ids, orig: new Map(ids.map(id => { const n = noteById(id); return [id, { s: n.s, p: n.p }]; })), moved: false, lastP: h.p, hid: h.id };
      clearTimeout(pressTimer);
      if (e.pointerType !== 'mouse') pressTimer = setTimeout(() => { if (drag && drag.t === 'move' && !drag.moved) { drag = null; openContextMenu(e.clientX, e.clientY, h.id, { x, y }); } }, 550);
      return;
    }
    // 7. empty space: double-click makes a note (Build); a drag draws a selection box
    if (isDouble(x, y, 'empty')) {
      if (app.mode !== 1) { toast('Notes are locked in this mode: press 1 Build to add notes'); return; }
      const s = app.snap ? Math.floor(b / app.snap) * app.snap : b;
      if (s >= total()) return;
      const p = scaleSnap(pitchAt(y));
      const r = M.addNote(app.clip, p, Math.max(0, s), Math.min(Math.max(1, minLen()), total() - s));
      setClip(r.clip, null);
      setSel([r.id], r.id);
      audition(p);
      drag = { t: 'end', id: r.id, y0: y, v0: 0.5, clip0: r.clip, label: 'Add note', force: true };
      return;
    }
    const useNodes = app.nodesMode && !!AL && app.sel.size > 0;
    drag = { t: 'box', x0: x, y0: y, x1: x, y1: y, shift: e.shiftKey, ctrl: e.ctrlKey || e.metaKey, base: new Set(app.sel), nodes: useNodes,
      baseNodes: new Map([...app.nodeSel].map(([id, s]) => [id, new Set(s)])), moved: false };
  }
  // two clips the same to 7 decimals (what a drag back to its start gives)
  const clipText = c => JSON.stringify(c, (k, v) => (typeof v === 'number' ? Math.round(v * 1e7) / 1e7 : v));
  function sameClip(a, b) {
    if (a === b) return true;
    if (!a || !b || a.bars !== b.bars || a.notes.length !== b.notes.length) return false;
    try { return clipText(a) === clipText(b); } catch (e) { return false; }
  }
  // the line of a note (not the faint band of its lane above it)
  function onNoteLine(n, x, y) {
    const x0 = n.s * BW, x1 = (n.s + n.l) * BW, yl = baseYAt(n, clamp((x - x0) / Math.max(1, x1 - x0), 0, 1));
    return y >= yl - RH * 0.7 && y <= yl + RH * 0.8;
  }
  // Shift+click (or Slide into...) on a second note: the earlier note slides into the later one (again = remove it)
  function linkNotes(P, h) {
    const [a, z] = h.s >= P.s ? [P, h] : [h, P];
    let clip;
    if (a.glideTo === z.id) { clip = M.updateNotes(app.clip, a.id, { glideTo: null }); toast('Slide removed'); }
    else { clip = M.updateNotes(app.clip, a.id, { glideTo: z.id }); toast(noteName(a.p) + ' now slides into ' + noteName(z.p)); }
    app.linkArm = false; $('linkBtn').classList.remove('on');
    setClip(clip, 'Slide into');
    setSel([z.id], z.id); updateHint();
  }
  // a click on a note: select it (add = Ctrl or Multi: add it, or take it out). false when it was taken out.
  function pickNote(h, add) {
    if (add) {
      if (app.sel.has(h.id) && h.id !== app.primary) { const s = new Set(app.sel); s.delete(h.id); setSel(s); return false; }
      setSel([...app.sel, h.id], h.id);
    } else if (!app.sel.has(h.id)) setSel([h.id], h.id);
    else setPrimary(h.id);
    return true;
  }
  // a Shift or Ctrl press on a note's band that did not move: the click it was
  function bandClick(a) {
    const h = noteById(a.id), P = primaryNote();
    if (!h) return;
    if (a.link && P && h.id !== P.id) linkNotes(P, h);
    else pickNote(h, a.add);
  }
  function paint(x, y) {
    const n = noteIn(drag.clip0, drag.id), x0 = n.s * BW, x1 = (n.s + n.l) * BW, M2 = drag.arr.length;
    const i = clamp(Math.round((x - x0) / (x1 - x0) * (M2 - 1)), 0, M2 - 1), v = clamp(laneValue(n, drag.lane, i / (M2 - 1), y), 0, 1);
    if (drag.li == null) drag.arr[i] = v;
    else { const a = drag.li, st = a < i ? 1 : -1; for (let j = a; j !== i + st; j += st) { const t = a === i ? 1 : (j - a) / (i - a); drag.arr[j] = drag.lv + (v - drag.lv) * t; } }
    drag.li = i; drag.lv = v;
    const nd = drag.arr.map((vv, j) => ({ t: j / (M2 - 1), v: vv, shape: 'linear', bend: 0 }));
    setClip(M.setLaneNodes(drag.clip0, drag.id, drag.lane, nd), null);
  }
  function applyBox() {
    const x0 = Math.min(drag.x0, drag.x1), x1 = Math.max(drag.x0, drag.x1), y0 = Math.min(drag.y0, drag.y1), y1 = Math.max(drag.y0, drag.y1);
    const AL = activeLane();
    if (drag.nodes) {
      const hits = new Map();
      for (const n of selectedNotes()) {
        const nd = n.c[AL], s = new Set();
        for (let k = 0; k < nd.length; k++) { const [nx, ny] = nodeXY(n, AL, k); if (nx >= x0 && nx <= x1 && ny >= y0 && ny <= y1) s.add(k); }
        if (s.size) hits.set(n.id, s);
      }
      const out = new Map();
      if (drag.shift || drag.ctrl) for (const [id, s] of drag.baseNodes) out.set(id, new Set(s));
      for (const [id, s] of hits) {
        const cur = out.get(id) || new Set();
        for (const k of s) { if (drag.ctrl && cur.has(k)) cur.delete(k); else cur.add(k); }
        if (cur.size) out.set(id, cur); else out.delete(id);
      }
      setNodePicks(out);
      requestDraw();
      return;
    }
    const hits = new Set();
    for (const n of app.clip.notes) {
      const bx = noteBox(n, app.mode !== 1);
      if (bx.x1 >= x0 && bx.x0 <= x1 && bx.bottom >= y0 && bx.top <= y1) hits.add(n.id);
    }
    let out;
    if (drag.shift) out = new Set([...drag.base, ...hits]);
    else if (drag.ctrl) { out = new Set(drag.base); for (const id of hits) { if (out.has(id)) out.delete(id); else out.add(id); } }
    else out = hits;
    setSel(out, out.has(app.primary) ? app.primary : null, true);
  }
  function rollMove(e) {
    if (!drag) return;
    if (drag.t === 'pan') { scroller.scrollLeft = drag.sl - (e.clientX - drag.cx); scroller.scrollTop = drag.st - (e.clientY - drag.cy); return; }
    const { x, y } = rollPos(e), b = x / BW;
    switch (drag.t) {
      case 'move': {
        const db = snapR(b - drag.b0), dp = pitchAt(y) - drag.p0;
        if (!drag.moved && (Math.abs(b - drag.b0) * BW > 5 || dp)) drag.moved = true;
        if (!drag.moved) return;
        clearTimeout(pressTimer);
        const T = total();
        const clip = M.updateNotes(drag.clip0, drag.ids, n => {
          const o = drag.orig.get(n.id);
          n.p = clamp(scaleSnap(o.p + dp), MINP, MAXP);
          n.s = clamp(o.s + db, 0, Math.max(0, T - n.l));
        });
        setClip(clip, null);
        const h = noteIn(clip, drag.hid);
        if (h && h.p !== drag.lastP) { drag.lastP = h.p; audition(h.p); }
        if (h) showReadout(x, y, noteName(h.p) + ' · ' + beatLabel(h.s));
        break;
      }
      case 'end': {
        const n0 = noteIn(drag.clip0, drag.id);
        if (!n0) break;
        const l = Math.max(minLen(), Math.min(total() - n0.s, snapR(b) - n0.s));
        let clip = M.updateNotes(drag.clip0, drag.id, { l });
        const nd = bendEndNodes(n0, 'end', y, drag.y0, drag.v0, e.shiftKey);
        if (nd) clip = M.setLaneNodes(clip, drag.id, 'pitch', nd);
        setClip(clip, null);
        showReadout(x, y, 'Length ' + fmtBeats(l) + (nd ? ' · end ' + fmtLane('pitch', nd[nd.length - 1].v, formatCtx(n0)) : ''));
        break;
      }
      case 'start': {
        const n0 = noteIn(drag.clip0, drag.id);
        if (!n0) break;
        const ns = clamp(snapR(b), 0, drag.end - minLen());
        let clip = M.updateNotes(drag.clip0, drag.id, { s: ns, l: drag.end - ns });
        const nd = bendEndNodes(n0, 'start', y, drag.y0, drag.v0, e.shiftKey);
        if (nd) clip = M.setLaneNodes(clip, drag.id, 'pitch', nd);
        setClip(clip, null);
        showReadout(x, y, beatLabel(ns) + (nd ? ' · start ' + fmtLane('pitch', nd[0].v, formatCtx(n0)) : ''));
        break;
      }
      case 'node': {
        const n = noteIn(drag.clip0, drag.id);
        if (!n) break;
        if (!drag.moved && drag.x0 != null && Math.hypot(x - drag.x0, y - drag.y0) < 3) break;
        const nx = x - (drag.gx || 0), ny = y - (drag.gy || 0);
        const nd = drag.nodes0.map(q => Object.assign({}, q)), k = drag.k, last = nd.length - 1;
        const x0 = n.s * BW, x1 = (n.s + n.l) * BW;
        let t = (nx - x0) / Math.max(1, x1 - x0);
        t = k === 0 ? 0 : k === last ? 1 : clamp(t, nd[k - 1].t + 0.004, nd[k + 1].t - 0.004);
        let v = clamp(laneValue(n, drag.lane, t, ny), 0, 1);
        if (drag.lane === 'pitch' && !e.shiftKey) v = clamp(0.5 + Math.round((v - 0.5) * 24) / 24, 0, 1);
        else if (drag.lane === 'travel' && n.sample.travel !== 'path' && !e.shiftKey) { for (const s of [0.25, 0.5, 0.75]) if (Math.abs(v - s) < 0.012) v = s; }
        nd[k] = Object.assign({}, nd[k], { t, v });
        setClip(M.setLaneNodes(drag.clip0, drag.id, drag.lane, nd), null);
        drag.moved = true;
        showReadout(x, y, fmtLane(drag.lane, v, formatCtx(n)));
        break;
      }
      case 'nodes': {
        const dB = (x - drag.x0) / BW, dyPx = drag.y0 - y, map = {};
        let dv = drag.lane === 'pitch' ? dyPx / RH / 24 : dyPx / laneH();
        if (drag.lane === 'pitch' && !e.shiftKey) dv = Math.round(dv * 24) / 24;
        let shown = null;
        for (const [id, ks] of drag.sel) {
          const n = noteIn(drag.clip0, id);
          if (!n) continue;
          const src = n.c[drag.lane], last = src.length - 1, nd = src.map(q => Object.assign({}, q));
          // time: the moving nodes shift together, never past a node that stays put (first/last never move in time)
          const moving = [...ks].filter(k => k > 0 && k < last && k <= last);
          let lo = -Infinity, hi = Infinity;
          for (const k of moving) {
            if (!moving.includes(k - 1)) lo = Math.max(lo, src[k - 1].t - src[k].t + 0.003);
            if (!moving.includes(k + 1)) hi = Math.min(hi, src[k + 1].t - src[k].t - 0.003);
          }
          const dt = clamp(dB / Math.max(1e-3, n.l), lo, hi);
          for (const k of ks) {
            if (k > last) continue;
            nd[k].v = clamp(src[k].v + dv, 0, 1);
            if (moving.includes(k)) nd[k].t = src[k].t + (isFinite(dt) ? dt : 0);
            if (!shown) shown = { n, v: nd[k].v };
          }
          map[id] = nd;
        }
        setClip(M.setLaneNodesMany(drag.clip0, drag.lane, map), null);
        drag.moved = true;
        if (shown) showReadout(x, y, fmtLane(drag.lane, shown.v, formatCtx(shown.n)));
        break;
      }
      case 'seg': {
        const nd = drag.nodes0.map(q => Object.assign({}, q)), k = drag.k, A = nd[k], B = nd[k + 1];
        const sgn = Math.sign(B.v - A.v) || 1;
        const bend = clamp(drag.b0 + (y - drag.y0) / 60 * sgn, -1, 1);
        nd[k] = Object.assign({}, A, { shape: 'bend', bend });
        setClip(M.setLaneNodes(drag.clip0, drag.id, drag.lane, nd), null);
        drag.moved = true;
        showReadout(x, y, 'Curve ' + (bend > 0 ? '+' : '') + Math.round(bend * 100) + '%');
        break;
      }
      case 'free': paint(x, y); break;
      case 'gstart': {
        const n = noteIn(drag.clip0, drag.id);
        if (!n) break;
        const len = clamp(n.s + n.l - snapR(b), 0.0625, n.l * 0.95);
        setClip(M.updateNotes(drag.clip0, drag.id, { glide: len }), null);
        drag.moved = true;
        showReadout(x, y, 'Slide ' + fmtBeats(len));
        break;
      }
      case 'gmid': {
        const gc = clamp(drag.c0 + (drag.y0 - y) / 60 * Math.sign(drag.d || 1), -1, 1);
        setClip(M.updateNotes(drag.clip0, drag.id, { gc }), null);
        drag.moved = true;
        showReadout(x, y, 'Slide curve ' + (gc > 0 ? '+' : '') + Math.round(gc * 100) + '%');
        break;
      }
      case 'box': {
        drag.x1 = x; drag.y1 = y;
        if (!drag.moved && Math.hypot(x - drag.x0, y - drag.y0) > 4) drag.moved = true;
        if (drag.moved) applyBox();
        requestDraw();
        break;
      }
    }
  }
  const DRAG_LABELS = { move: 'Move notes', end: 'Change length', start: 'Move note start', node: 'Move node', nodes: 'Move nodes', seg: 'Bend segment', free: 'Draw lane by hand', gstart: 'Slide length', gmid: 'Slide curve' };
  function rollUp() {
    clearTimeout(pressTimer);
    hideReadout();
    const d = drag;
    drag = null;
    if (!d || d.t === 'pan') return;
    if (d.t === 'box') {
      try {
        if (!d.moved && d.clickAct) { bandClick(d.clickAct); requestDraw(); return; }
        if (!d.moved && d.clickNote) { if (!d.shift && !d.ctrl) setNodePicks(new Map()); setPrimary(d.clickNote); requestDraw(); return; }
        if (!d.moved) {
          if (!d.shift && !d.ctrl) {
            if (app.nodesMode && app.nodeSel.size) { setNodePicks(new Map()); requestDraw(); }
            else { setSel([], null); setActiveNode(null); }
          }
        } else if (!d.nodes) emitSelection();
        requestDraw();
        return;
      } finally { if (app.nodesMode) emit('nodes', app.nodeSel); }   // the picked nodes changed (for the inspector)
    }
    if (d.t === 'free') {
      const n = noteById(d.id);
      if (n) setClip(M.setLaneNodes(app.clip, d.id, d.lane, M.rdp(n.c[d.lane], 0.012).map(q => Object.assign({}, q, { shape: 'smooth' }))), null);
    }
    if (d.t === 'move' && !d.moved) { requestDraw(); return; }
    // a drag that ended where it started (a node dragged down and back) changes nothing: no undo step, and the
    // values go back exactly to what they were (the drag's arithmetic can leave a last-digit difference)
    if (!d.force && app.clip !== d.clip0 && sameClip(app.clip, d.clip0)) { setClip(d.clip0, null); requestDraw(); return; }
    if (app.clip !== d.clip0 || d.force) commit(d.label || DRAG_LABELS[d.t] || 'Edit');
    requestDraw();
  }

  // =============================================================================================== menus
  const menu = $('menu');
  let menuAt = null;
  function mkMenuItem(it) {
    const b = el('button', { type: 'button', class: it.on ? 'on' : null, title: it.title || null }, it.label);
    if (it.disabled) b.disabled = true;
    if (it.color) b.style.color = it.color;
    b.addEventListener('click', ev => { ev.stopPropagation(); setGhost(null); menuLeave(); if (!it.keep) closeMenu(); if (it.click) it.click(); });
    if (it.hover) { b.addEventListener('mouseenter', () => { menuLeave(); try { it.hover(); } catch (e) { setGhost(null); } if (it.leave) leaveFn = it.leave; }); b.addEventListener('mouseleave', () => { setGhost(null); menuLeave(); }); }
    return b;
  }
  // an item's leave() runs when the mouse leaves it, when it is clicked and when the menu closes (hover previews off the roll)
  let leaveFn = null;
  function menuLeave() { const f = leaveFn; leaveFn = null; if (f) { try { f(); } catch (e) { /* ok */ } } }
  function openMenu(cx, cyy, items) {
    menuLeave();
    menu.innerHTML = '';
    for (const it of items) {
      if (!it) continue;
      if (it.sep) { menu.appendChild(el('hr')); continue; }
      if (it.header != null) { const d = el('div', { class: 'h' }, it.header); if (it.color) d.style.color = it.color; menu.appendChild(d); continue; }
      if (it.row) { const r = el('div', { class: 'row' }); it.row.forEach(s => r.appendChild(mkMenuItem(s))); menu.appendChild(r); continue; }
      menu.appendChild(mkMenuItem(it));
    }
    // the cap comes from the visible window (100vh can be taller than it in a phone or a framed page), so a long
    // menu scrolls inside itself and its last item can always be reached
    const vw = window.innerWidth || document.documentElement.clientWidth, vh = window.innerHeight || document.documentElement.clientHeight;
    menu.style.maxHeight = Math.max(80, vh - 8) + 'px'; menu.style.maxWidth = Math.max(160, vw - 8) + 'px';
    menu.style.display = 'block';
    menu.scrollTop = 0;
    const w = menu.offsetWidth, h = menu.offsetHeight;
    menu.style.left = clamp(cx, 4, Math.max(4, window.innerWidth - w - 4)) + 'px';
    menu.style.top = clamp(cyy, 4, Math.max(4, window.innerHeight - h - 4)) + 'px';
    menuAt = { cx, cy: cyy };
  }
  function closeMenu() { menuLeave(); if (menu.style.display !== 'none') { menu.style.display = 'none'; setGhost(null); } }
  const menuOpen = () => menu.style.display === 'block';
  window.addEventListener('pointerdown', e => { if (menuOpen() && !menu.contains(e.target) && !(e.target.closest && e.target.closest('.arrow'))) closeMenu(); }, true);
  function shapeItems(laneId, ids, header) {
    const P = primaryNote();
    const kind = laneId === 'travel' ? (P ? P.sample.travel : 'speed') : null;
    const seed = (Date.now() % 100000) + 1;   // UI seed for 'Random' (the result is stored as nodes)
    const opts = () => ({ seed, bpm: app.bpm, sampleSeconds: sampleSeconds(), inst: app.inst });
    const items = [];
    if (header) items.push({ header, color: laneColor(laneId) });
    items.push({ row: [
      { label: 'Each note', on: app.scope === 'each', keep: true, title: 'The shape fits every note', click: () => { setScope('each'); reopen(); } },
      { label: 'Across', on: app.scope === 'across', keep: true, title: 'One shape across the whole selection', click: () => { setScope('across'); reopen(); } },
    ] });
    for (const k of M.shapeKeysFor(laneId, kind)) {
      items.push({ label: M.SHAPE_NAMES[k], hover: () => setGhost(M.applyShape(app.clip, ids, laneId, k, app.scope, opts())),
        click: () => setClip(M.applyShape(app.clip, ids, laneId, k, app.scope, opts()), M.SHAPE_NAMES[k] + ' (' + LANE[laneId].name + ')') });
    }
    return items;
  }
  let reopenFn = null;
  function reopen() { if (reopenFn) reopenFn(); }
  function openLaneMenu(laneId, cx, cyy) {
    const ids = getSelection();
    setModeForLane(laneId);
    if (!ids.length) { toast('Select notes first: click one, or drag a box on empty space'); return; }
    const L = LANE[laneId];
    const pat = M.makeGen('pattern', { shape: 'squiggle', rate: '1/16T', d0: 0.3, d1: 0.3 });
    const rnd = M.makeGen('random', { rate: '1/16T', amount: 0.5 });
    const items = shapeItems(laneId, ids, L.name + ' on ' + ids.length + (ids.length === 1 ? ' note' : ' notes'));
    items.push({ sep: true });
    items.push({ label: 'Pattern: squiggle 1/16 triplet', hover: () => setGhost(M.setGen(app.clip, ids, laneId, pat)), click: () => setClip(M.setGen(app.clip, ids, laneId, pat), 'Pattern (' + L.name + ')') });
    items.push({ label: 'Random 1/16 triplet', hover: () => setGhost(M.setGen(app.clip, ids, laneId, rnd)), click: () => setClip(M.setGen(app.clip, ids, laneId, rnd), 'Random (' + L.name + ')') });
    items.push({ label: 'Re-roll the random', click: () => setClip(M.reroll(app.clip, ids, laneId), 'Re-roll (' + L.name + ')') });
    items.push({ label: 'Remove pattern / random', hover: () => setGhost(M.clearGen(app.clip, ids, laneId)), click: () => setClip(M.clearGen(app.clip, ids, laneId), 'Remove pattern (' + L.name + ')') });
    items.push({ label: 'Turn into nodes', hover: () => setGhost(M.bakeGen(app.clip, ids, laneId)), click: () => setClip(M.bakeGen(app.clip, ids, laneId), 'Turn into nodes (' + L.name + ')') });
    reopenFn = () => openLaneMenu(laneId, cx, cyy);
    openMenu(cx, cyy, items);
  }
  function openContextMenu(cx, cyy, noteId, at) {
    const AL = activeLane(), ids = getSelection();
    const items = [];
    if (app.mode === 1) {
      items.push({ header: 'Build' });
      if (ids.length) {
        items.push({ label: '\u{1F517} Slide into the next note I click', click: () => armLink() });
        items.push({ label: 'Remove slide links', click: () => setClip(M.updateNotes(app.clip, ids, { glideTo: null }), 'Remove slide') });
        items.push({ label: 'Jump, no slide', click: () => setClip(M.updateNotes(app.clip, ids, { glide: 0 }), 'Slide: jump') });
      }
    } else if (ids.length) {
      reopenFn = () => openContextMenu(cx, cyy, noteId, at);
      items.push(...shapeItems(AL, ids, LANE[AL].name + ' shape'));
    }
    if (ids.length) {
      items.push({ sep: true });
      items.push({ label: 'Copy lanes', click: copyLanes });
      items.push({ label: 'Paste lanes', click: pasteLanes, disabled: !app.clipboard });
      items.push({ label: 'Duplicate', click: duplicateSel });
      items.push({ label: 'Delete', click: deleteSel });
    }
    if (!noteId && app.mode === 1 && at) {
      items.push({ sep: true });
      items.push({ label: 'New note here', click: () => {
        const s = app.snap ? Math.floor(at.x / BW / app.snap) * app.snap : at.x / BW;
        if (s >= total()) return;
        const r = M.addNote(app.clip, scaleSnap(pitchAt(at.y)), Math.max(0, s), Math.min(1, total() - s));
        setClip(r.clip, 'Add note'); setSel([r.id], r.id);
      } });
    }
    if (!items.length) items.push({ header: 'Select a note first' });
    openMenu(cx, cyy, items);
  }

  // =============================================================================================== actions
  function armLink() {
    if (!(app.mode === 1 || activeLane() === 'pitch')) { toast('Slides are made in Build (1) or Pitch (2)'); return; }
    if (!app.primary) { toast('Select the note that should slide first'); return; }
    app.linkArm = !app.linkArm;
    $('linkBtn').classList.toggle('on', app.linkArm);
    if (app.linkArm) toast('Now click the note it should slide into');
    updateHint();
  }
  function duplicateSel() {
    const ids = getSelection();
    if (!ids.length) return;
    const r = M.duplicateNotes(app.clip, ids);
    const T = total();
    const keep = r.ids.filter(id => noteIn(r.clip, id).s < T);
    let clip = M.removeNotes(r.clip, r.ids.filter(id => !keep.includes(id)));
    clip = M.updateNotes(clip, keep, n => { n.l = Math.min(n.l, T - n.s); });
    setClip(clip, 'Duplicate');
    setSel(keep, keep[0] || null);
    if (!keep.length) toast('No room after the selection: add bars first');
  }
  function deleteSel() {
    const ids = getSelection();
    if (!ids.length) return;
    setClip(M.removeNotes(app.clip, ids), ids.length === 1 ? 'Delete note' : 'Delete notes');
    setSel([], null);
  }
  function selectAll() { setSel(app.clip.notes.map(n => n.id), app.clip.notes.length ? app.primary || app.clip.notes[0].id : null); }
  function copyLanes() {
    const n = primaryNote();
    if (!n) { toast('Select a note to copy from'); return; }
    const cb = { c: {}, gen: {} };
    app.copyMask.forEach(id => { cb.c[id] = n.c[id].map(q => Object.assign({}, q)); cb.gen[id] = n.gen[id] ? Object.assign({}, n.gen[id]) : null; });
    app.clipboard = cb;
    const names = [...app.copyMask].map(id => LANE[id].name);
    $('clipInfo').textContent = 'holding: ' + (names.length > 4 ? names.length + ' lanes' : names.join(', '));
    toast('Copied ' + names.length + (names.length === 1 ? ' lane' : ' lanes'));
  }
  function pasteLanes() {
    const cb = app.clipboard;
    if (!cb) { toast('Nothing copied yet'); return; }
    const ids = getSelection();
    if (!ids.length) { toast('Select the notes to paste onto'); return; }
    setClip(M.updateNotes(app.clip, ids, n => { for (const id of Object.keys(cb.c)) { n.c[id] = cb.c[id].map(q => Object.assign({}, q)); n.gen[id] = cb.gen[id] ? Object.assign({}, cb.gen[id]) : null; } }), 'Paste lanes');
    toast('Pasted onto ' + ids.length + (ids.length === 1 ? ' note' : ' notes'));
  }
  function setScope(s) { app.scope = s === 'across' ? 'across' : 'each'; emit('scope', app.scope); }

  // =============================================================================================== modes
  function buildModeBar() {
    const bar = $('modeBar');
    bar.innerHTML = '';
    for (const m of M.MODES) {
      if (m.mode === 1) {
        bar.appendChild(el('button', { class: 'mode build', id: 'mode1', title: 'Build: add, move and stretch notes', onclick: () => setMode(1) }, el('kbd', null, '1'), '\u{1F9F1} Build'));
        bar.appendChild(el('span', { class: 'sep' }));
        continue;
      }
      const laneId = m.mode === 8 ? app.subLane : m.lanes[0];
      const color = m.mode === 8 ? 'var(--lane-sample)' : laneVar(laneId);
      const b = el('button', { class: 'lane', id: 'mode' + m.mode, title: m.mode === 8 ? 'Sample: how each note moves through the sound' : 'Draw ' + m.name + ' on the notes (notes stay put)', onclick: () => setMode(m.mode) }, el('kbd', null, m.key), m.name);
      b.style.borderColor = color;
      const ar = el('button', { class: 'lane arrow', title: 'Shapes, patterns and random for the selected notes', onclick: ev => { const r = ev.currentTarget.getBoundingClientRect(); openLaneMenu(m.mode === 8 ? app.subLane : m.lanes[0], r.left, r.bottom + 4); } }, '▾');
      ar.style.borderColor = color;
      bar.appendChild(el('span', { class: 'lanewrap' }, b, ar));
      if (m.mode === 8) {
        const chips = el('span', { class: 'grp', id: 'subChips' });
        for (const id of SAMPLE_LANES) chips.appendChild(el('button', { class: 'subchip', 'data-sub': id, title: SUB_HINT[id], onclick: () => setSubLane(id) }, LANE[id].name));
        bar.appendChild(chips);
      }
    }
    bar.appendChild(el('span', { class: 'sep' }));
    bar.appendChild(el('button', { id: 'nodesBtn', title: 'Nodes: a box on empty space picks nodes of the selected notes; then drag one to move them together', onclick: toggleNodes }, '◎ Nodes'));
    paintModes();
  }
  function paintModes() {
    const b1 = $('mode1');
    if (!b1) return;
    b1.classList.toggle('on', app.mode === 1);
    for (const m of M.MODES) {
      if (m.mode === 1) continue;
      const b = $('mode' + m.mode), on = app.mode === m.mode;
      const color = m.mode === 8 ? 'var(--lane-sample)' : laneVar(m.lanes[0]);
      b.style.background = on ? color : ''; b.style.color = on ? 'var(--on-lane)' : '';
      const ar = b.nextSibling; ar.style.background = on ? color : ''; ar.style.color = on ? 'var(--on-lane)' : '';
    }
    document.querySelectorAll('#subChips .subchip').forEach(c => c.classList.toggle('on', app.mode === 8 && c.dataset.sub === app.subLane));
    const nb = $('nodesBtn');
    if (nb) { nb.classList.toggle('on', app.nodesMode); nb.disabled = app.mode === 1; }
  }
  function setMode(m, quiet, laneChanged) {
    m = clamp(Math.round(num(+m, 1)), 1, 8);
    const changed = m !== app.mode;
    app.mode = m;
    if (m === 1) { app.free = false; $('freeBtn').classList.remove('on'); }
    if (app.linkArm && !(m === 1 || activeLane() === 'pitch')) { app.linkArm = false; $('linkBtn').classList.remove('on'); }
    // node picks belong to the lane shown: they go when the lane changes (and the inspector hears of it)
    if ((changed || laneChanged) && app.nodeSel.size) { app.nodeSel = new Map(); emit('nodes', app.nodeSel); }
    if (app.activeNode && app.activeNode.lane !== activeLane()) setActiveNode(null);
    paintModes(); updateHint(); requestDraw(); saveSoon();
    if (changed && !quiet) emit('mode', { mode: app.mode, subLane: app.subLane, lane: activeLane() });
  }
  function setSubLane(id) {
    if (!SAMPLE_LANES.includes(id)) return;
    const changed = id !== app.subLane || app.mode !== 8;
    app.subLane = id;
    app.mode = 8;
    setMode(8, true, changed);
    if (changed) emit('mode', { mode: app.mode, subLane: app.subLane, lane: activeLane() });
  }
  function setModeForLane(laneId) {
    if (SAMPLE_LANES.includes(laneId)) setSubLane(laneId);
    else { const m = M.MODES.find(x => x.lanes[0] === laneId); if (m && app.mode !== m.mode) setMode(m.mode); }
  }
  function toggleNodes() {
    if (app.mode === 1) { toast('Nodes works in a lane mode (2 to 8)'); return; }
    app.nodesMode = !app.nodesMode;
    app.nodeSel = new Map();
    if (app.nodesMode && !app.sel.size) toast('Select notes first, then drag a box around their nodes');
    paintModes(); updateHint(); requestDraw();
    emit('nodes', app.nodeSel);
  }

  // =============================================================================================== strip (Sample editor)
  const waveEl = $('wave');
  const SV = { a: 0, b: 1 };
  let waveCache = { key: '', canvas: null };
  const BAND = 12;
  const waveWrap = $('waveWrap');
  const stripW = () => waveWrap.clientWidth, stripH = () => waveWrap.clientHeight;   // the wrapper follows the layout; the canvas copies it
  const xOf = pos => (pos - SV.a) / (SV.b - SV.a) * stripW();
  const posOf = x => SV.a + x / Math.max(1, stripW()) * (SV.b - SV.a);
  function buildPeaks(S) {
    const L = S.channels[0], R = S.channels[1] || L, n = L.length, B = 256, nb = Math.ceil(n / B);
    const mn = new Float32Array(nb), mx = new Float32Array(nb);
    for (let b = 0; b < nb; b++) {
      let lo = Infinity, hi = -Infinity;
      const e = Math.min(n, (b + 1) * B);
      for (let i = b * B; i < e; i++) { const v = (L[i] + R[i]) * 0.5; if (v < lo) lo = v; if (v > hi) hi = v; }
      mn[b] = lo === Infinity ? 0 : lo; mx[b] = hi === -Infinity ? 0 : hi;
    }
    return { B, mn, mx };
  }
  function renderWave(S, W, H) {
    const d = DPR(), cv = document.createElement('canvas');
    cv.width = Math.max(1, Math.round(W * d)); cv.height = Math.max(1, Math.round(H * d));
    const c = cv.getContext('2d');
    c.setTransform(d, 0, 0, d, 0, 0);
    if (!S.peaks) S.peaks = buildPeaks(S);
    const L = S.channels[0], R = S.channels[1] || L, n = L.length, pk = S.peaks;
    const top = BAND + 2, mid = top + (H - top) / 2, amp = (H - top) * 0.46;
    c.fillStyle = TH.wave; c.globalAlpha = 0.8;
    for (let x = 0; x < W; x++) {
      const i0 = Math.floor((SV.a + (SV.b - SV.a) * x / W) * n), i1 = Math.max(i0 + 1, Math.floor((SV.a + (SV.b - SV.a) * (x + 1) / W) * n));
      let lo = Infinity, hi = -Infinity;
      if (i1 - i0 >= 2 * pk.B) {
        for (let b = Math.floor(i0 / pk.B); b < Math.min(pk.mn.length, Math.ceil(i1 / pk.B)); b++) { if (pk.mn[b] < lo) lo = pk.mn[b]; if (pk.mx[b] > hi) hi = pk.mx[b]; }
      } else {
        for (let i = i0; i < Math.min(n, i1); i++) { const v = (L[i] + R[i]) * 0.5; if (v < lo) lo = v; if (v > hi) hi = v; }
      }
      if (!(hi >= lo)) continue;
      c.fillRect(x, mid - hi * amp, 1, Math.max(1, (hi - lo) * amp));
    }
    return cv;
  }
  function drawStrip() {
    const W = stripW(), H = stripH();
    if (!W || !H) return;
    const c = sizeCanvas(waveEl, W, H);
    c.fillStyle = TH['strip-bg']; c.fillRect(0, 0, W, H);
    const S = app.sample;
    if (!S) { c.fillStyle = TH['strip-txt']; c.font = '12px sans-serif'; c.fillText('Loading the sound...', 10, H / 2); return; }
    const key = [S.name, S.channels[0].length, SV.a, SV.b, W, H, DPR(), TH.wave].join('|');
    if (waveCache.key !== key || !waveCache.canvas) waveCache = { key, canvas: renderWave(S, W, H) };
    c.drawImage(waveCache.canvas, 0, 0, W, H);
    const R = app.inst.region, rx0 = xOf(R.start), rx1 = xOf(R.end);
    c.fillStyle = TH['strip-shade'];
    if (rx0 > 0) c.fillRect(0, 0, Math.min(W, rx0), H);
    if (rx1 < W) c.fillRect(Math.max(0, rx1), 0, W - Math.max(0, rx1), H);
    c.fillStyle = TH['region-band']; c.fillRect(rx0, 0, rx1 - rx0, BAND);
    c.fillStyle = TH['region-edge'];
    c.fillRect(rx0 - 1, 0, 2, H); c.fillRect(rx1 - 1, 0, 2, H);
    c.fillRect(rx0, H / 2 - 9, 5, 18); c.fillRect(rx1 - 5, H / 2 - 9, 5, 18);
    if (rx1 - rx0 > 60) { c.font = 'bold 9px sans-serif'; c.fillStyle = TH['strip-txt']; c.fillText('REGION', rx0 + 6, 9); }
    // Start markers of the selected notes
    const span = R.end - R.start;
    c.fillStyle = TH['start-mark'];
    for (const n of selectedNotes().slice(0, 64)) {
      const st = n.sample.start;
      if (st.mode !== 'region' && st.mode !== 'random') continue;
      const x = xOf(R.start + st.pos * span);
      if (st.mode === 'random') { c.globalAlpha = 0.3; c.fillRect(x, H - 5, xOf(R.start + Math.min(1, st.pos + st.range) * span) - x, 4); c.globalAlpha = 1; }
      c.beginPath(); c.moveTo(x - 5, H); c.lineTo(x + 5, H); c.lineTo(x, H - 8); c.closePath(); c.fill();
    }
    // live heads (numbered lines with direction arrows) and grains (dots) from the engine
    const d = lastDisplay;
    if (d && d.voices) {
      c.font = 'bold 9px sans-serif'; c.textAlign = 'center';
      for (const v of d.voices) {
        for (const h of v.heads || []) {
          const x = xOf(h.pos);
          if (!(x > -12 && x < W + 12)) continue;
          const lv = clamp((h.level || 0) * (v.env == null ? 1 : v.env) * 1.6, 0, 1);
          c.globalAlpha = (v.released ? 0.45 : 1) * (0.35 + 0.65 * lv);
          c.fillStyle = TH.head; c.fillRect(x - 1, BAND, 2, H - BAND);
          c.beginPath(); c.arc(x, BAND + 8, 7, 0, 7); c.fill();
          c.beginPath();
          if (h.dir > 0) { c.moveTo(x + 2, H - 13); c.lineTo(x + 10, H - 8); c.lineTo(x + 2, H - 3); }
          else if (h.dir < 0) { c.moveTo(x - 2, H - 13); c.lineTo(x - 10, H - 8); c.lineTo(x - 2, H - 3); }
          else c.rect(x - 3, H - 11, 6, 6);
          c.fill();
          c.fillStyle = TH['head-txt']; c.fillText(String((h.i | 0) + 1), x, BAND + 11);
        }
      }
      c.textAlign = 'start';
      c.fillStyle = TH.grain;
      for (const g of d.grains || []) {
        const x = xOf(g.pos);
        if (!(x >= 0 && x <= W)) continue;
        const age = clamp(g.age || 0, 0, 1);
        c.globalAlpha = 0.9 - 0.6 * age;
        c.fillRect(x - 1.5, BAND + 20 + (H - BAND - 34) * age, 3, 3);
      }
      c.globalAlpha = 1;
    }
    // the file playing as recorded (held outside the Region): a moving line
    const rp = rawPos();
    if (rp != null && rp <= 1) {
      const x = xOf(rp);
      c.fillStyle = TH.head; c.globalAlpha = 0.9; c.fillRect(x - 1, BAND, 2, H - BAND);
      c.beginPath(); c.moveTo(x + 2, H - 13); c.lineTo(x + 10, H - 8); c.lineTo(x + 2, H - 3); c.fill(); c.globalAlpha = 1;
    }
    // overview of the view when zoomed in
    if (SV.b - SV.a < 0.999) {
      c.fillStyle = TH['strip-shade']; c.fillRect(0, H - 4, W, 4);
      c.fillStyle = TH['region-edge']; c.fillRect(SV.a * W, H - 4, Math.max(4, (SV.b - SV.a) * W), 4);
    }
    if (stripDecorators.length) {
      const info = { view: { a: SV.a, b: SV.b }, xOf, posOf, sample: S, display: lastDisplay, inst: app.inst, theme: TH, region: { x0: rx0, x1: rx1 } };
      for (const fn of stripDecorators) { c.save(); try { fn(c, { x: 0, y: 0, w: W, h: H }, info); } catch (e) { console.warn('SLApp strip decorator failed:', e); } c.restore(); }
    }
  }
  function updateStripText(extra) {
    const S = app.sample;
    if (!S) { $('stripTxt').textContent = 'Loading...'; return; }
    const I = app.inst;
    $('stripTxt').innerHTML = '';
    $('stripTxt').append(el('b', null, S.name), ' · ' + S.seconds.toFixed(2) + ' s · root ' + noteName(I.root) + (I.cents ? ' ' + fmtCents(I.cents) : '') + (S.builtIn ? ' (built-in)' : '') + (extra ? ' · ' + extra : S.detected ? ' · ' + S.detected : ''));
    $('stripTxt').title = $('stripTxt').textContent;
    const tip = $('stripTip');
    tip.innerHTML = '';
    // suggestions from listening to the file: one short button each, the sentence is its tooltip (nothing is changed
    // without that click). They share a row with the part chips, so the strip stays low on a laptop.
    if (!S.builtIn && S.rootSug && sugFits(S.rootSug, I.region)) {
      const g = S.rootSug;
      tip.append(el('button', { class: 'chip', title: (regionWhole(I.region) ? 'It' : 'This part') + ' sounds like ' + rootText(g.root, g.cents) +
        '. Use it as the root, so the notes play this part in tune (C3 then plays it as recorded)', onclick: useRootSug }, 'Use ' + noteName(g.root) + ' as the root'));
    }
    if (!S.builtIn && S.keySug && (S.keySug.key !== I.key || S.keySug.scale !== I.scale)) {
      const name = keyName(S.keySug.key, S.keySug.scale);
      tip.append(el('button', { class: 'chip', title: 'It sounds like ' + name + '. Click to set the Key and Scale (Sound tab) to ' + name + ' (one undo step)', onclick: useKeySug }, 'Use key ' + name));
    }
    if (!S.builtIn && I.region.start <= 0.0005 && I.region.end >= 0.9995 && S.seconds <= LONG_FILE && I.region.atEnd === 'loop') {
      tip.append(el('button', { class: 'chip', title: 'Tip: a slightly smaller Region loops cleanly. Click to pull the Region end in a little, so the loop crossfade has sound to use (one undo step)', onclick: trimRegion }, '✂ Loop cleanly'));
    }
  }
  function trimRegion() {
    const S = app.sample;
    if (!S) return;
    const R = app.inst.region, cut = Math.max(0.004, 3 * (R.xfadeMs || 10) / 1000 / Math.max(0.05, S.seconds));
    patchInst({ region: { start: Math.min(R.start, 1 - 2 * cut), end: Math.max(R.start + 0.01, Math.min(R.end, 1 - cut)) } }, 'Trim the Region');
    updateStripText();
  }
  function stripReadout(text) { const r = $('stripReadout'); if (text) { r.textContent = text; r.style.display = 'block'; } else r.style.display = 'none'; }
  function regionText(R) { const s = sampleSeconds(); return 'Region ' + fmtTime(R.start * s) + ' to ' + fmtTime(R.end * s) + ' (' + ((R.end - R.start) * s).toFixed(2) + ' s)'; }
  // Hold the mouse on the waveform to hear that spot. Inside the Region an engine voice plays it (its heads show on
  // the strip); outside the Region the engine cannot go, so the file itself plays from there, as recorded (picking
  // a part of a song means hearing what is outside the current one).
  let auditionP = null, rawAud = null;
  const STRIP_CLICK_MS = 300;   // a press held longer than this is listening, not a click
  function rawAuditionStart(pos) {
    const S = app.sample, ctx = audio.ctx;
    if (!S || !ctx || !audio.master) return false;
    let buf = S.audioBuf;
    if (!buf || buf.length !== S.channels[0].length) {
      try {
        buf = ctx.createBuffer(S.channels.length, S.channels[0].length, S.sampleRate);
        S.channels.forEach((c, i) => buf.copyToChannel(c, i));
        S.audioBuf = buf;
      } catch (e) { return false; }
    }
    try {
      const src = ctx.createBufferSource(), g = ctx.createGain(), t = ctx.currentTime;
      src.buffer = buf;
      g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(0.7, t + 0.012);
      src.connect(g); g.connect(audio.master);
      src.start(t, clamp(pos, 0, 1) * buf.duration);
      rawAud = { src, g, pos0: clamp(pos, 0, 1), t0: t, dur: buf.duration };
      return true;
    } catch (e) { return false; }
  }
  function rawAuditionStop() {
    if (!rawAud) return;
    const { src, g } = rawAud, ctx = audio.ctx;
    rawAud = null;
    try { const t = ctx.currentTime; g.gain.cancelScheduledValues(t); g.gain.setValueAtTime(g.gain.value, t); g.gain.linearRampToValueAtTime(0, t + 0.03); src.stop(t + 0.04); } catch (e) { /* already stopped */ }
    dirty.strip = true;
  }
  const rawPos = () => (rawAud && audio.ctx ? rawAud.pos0 + Math.max(0, audio.ctx.currentTime - rawAud.t0) / rawAud.dur : null);
  function stopAudition() { if (auditionP != null) { post({ type: 'noteOff', p: auditionP }); auditionP = null; } rawAuditionStop(); }
  function stripDown(e) {
    if (!app.sample || e.button === 2) return;
    e.preventDefault();
    const r = waveEl.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top, W = stripW(), H = stripH();
    try { waveEl.setPointerCapture(e.pointerId); } catch (err) { /* ok */ }
    const R = app.inst.region, rx0 = xOf(R.start), rx1 = xOf(R.end);
    const minW = Math.max(0.002, 0.01 / sampleSeconds());
    if (SV.b - SV.a < 0.999 && y > H - 7) { sdrag = { t: 'pan', w: SV.b - SV.a }; stripMove(e); return; }
    const d0 = Math.abs(x - rx0), d1 = Math.abs(x - rx1);
    if (Math.min(d0, d1) < 7) { sdrag = { t: d0 <= d1 ? 'rs' : 're', minW }; stripReadout(regionText(R)); return; }
    if (y < BAND + 2 && x > rx0 && x < rx1) { sdrag = { t: 'rbox', pos0: posOf(x), r0: Object.assign({}, R) }; stripReadout(regionText(R)); return; }
    const pos0 = clamp(posOf(x), 0, 1), inside = pos0 >= R.start - 1e-9 && pos0 <= R.end + 1e-9;
    sdrag = { t: 'click', x0: x, pos0, inside, moved: false, minW, at: performance.now() };
    if (!audio.ready) initAudio();
    stopAudition();
    if (inside) {
      auditionP = app.inst.root;
      const start = clamp((pos0 - R.start) / Math.max(1e-9, R.end - R.start), 0, 1);
      post({ type: 'noteOn', p: auditionP, vel: 0.8, opts: { startPos: start } });
      stripReadout(fmtTime(pos0 * sampleSeconds()));
    } else {
      rawAuditionStart(pos0);
      stripReadout(fmtTime(pos0 * sampleSeconds()) + ' · outside the Region: the file as recorded');
    }
  }
  function stripMove(e) {
    if (!sdrag) return;
    const r = waveEl.getBoundingClientRect(), x = e.clientX - r.left, p = clamp(posOf(x), 0, 1);
    const R = app.inst.region;
    let nr = null;
    switch (sdrag.t) {
      case 'pan': { const w = sdrag.w, a = clamp(x / Math.max(1, stripW()) - w / 2, 0, 1 - w); SV.a = a; SV.b = a + w; dirty.strip = true; return; }
      case 'rs': nr = { start: clamp(p, 0, R.end - sdrag.minW), end: R.end }; break;
      case 're': nr = { start: R.start, end: clamp(p, R.start + sdrag.minW, 1) }; break;
      case 'rbox': { const w = sdrag.r0.end - sdrag.r0.start, s = clamp(sdrag.r0.start + p - sdrag.pos0, 0, 1 - w); nr = { start: s, end: s + w }; break; }
      case 'click': {
        if (!sdrag.moved && Math.abs(x - sdrag.x0) > 5) { sdrag.moved = true; stopAudition(); }
        if (!sdrag.moved) return;
        let a = Math.min(sdrag.pos0, p), b = Math.max(sdrag.pos0, p);
        if (b - a < sdrag.minW) b = Math.min(1, a + sdrag.minW);
        nr = { start: a, end: b };
        break;
      }
    }
    if (nr) { patchInst({ region: nr }, null); stripReadout(regionText(app.inst.region)); }
  }
  function stripUp() {
    const d = sdrag;
    sdrag = null;
    stripReadout(null);
    if (!d) return;
    if (d.t === 'click' && !d.moved) {
      stopAudition();
      // holding the mouse is listening: only a short click sets the Start of the selected notes
      if (performance.now() - d.at > STRIP_CLICK_MS) return;
      const ids = getSelection();
      if (ids.length && d.inside) {
        const R = app.inst.region, pos = clamp((d.pos0 - R.start) / Math.max(1e-9, R.end - R.start), 0, 1);
        setClip(M.setNoteSample(app.clip, ids, { start: { mode: 'region', pos } }), 'Set start');
        toast('Start set for ' + ids.length + (ids.length === 1 ? ' note' : ' notes') + ' at ' + Math.round(pos * 100) + '% of the Region');
      } else if (ids.length) toast('That spot is outside the Region, and notes start inside it. Drag across the waveform to make a Region there.', 3500);
      return;
    }
    if (d.t === 'pan') return;
    commit('Region');
    updateStripText();
    suggestRoot();   // a new part of a user file: suggest its root (S1)
  }
  // show the part a..b of the file on the strip (fractions); a tiny or reversed range widens to the zoom limit
  function setStripView(a, b) {
    const n = app.sample ? app.sample.channels[0].length : 1, minW = Math.max(256 / n, 0.0005);
    a = clamp(num(a, 0), 0, 1); b = clamp(num(b, 1), 0, 1);
    if (b < a) { const t = a; a = b; b = t; }
    if (b - a < minW) { const c = (a + b) / 2; a = c - minW / 2; b = c + minW / 2; }
    if (a < 0) { b -= a; a = 0; }
    if (b > 1) { a = Math.max(0, a - (b - 1)); b = 1; }
    SV.a = a; SV.b = b; dirty.strip = true;
  }
  function zoomStrip(f, center) {
    const w0 = SV.b - SV.a, n = app.sample ? app.sample.channels[0].length : 1;
    const w = clamp(w0 * f, Math.max(256 / n, 0.0005), 1);
    const c = center == null ? (SV.a + SV.b) / 2 : center;
    let a = c - (c - SV.a) * (w / w0);
    a = clamp(a, 0, 1 - w);
    SV.a = a; SV.b = a + w; dirty.strip = true;
  }

  // =============================================================================================== panels
  function showTab(name) {
    app.tab = name;
    document.querySelectorAll('#tabs .tab').forEach(t => t.classList.toggle('on', t.dataset.t === name));
    document.querySelectorAll('#panels .pane').forEach(p => p.classList.toggle('on', p.dataset.p === name));
    if (app.folded) setFolded(false);
    saveSoon();
    emit('tab', name);
  }
  // The page fits the window: the bars, the waveform strip and the hint line always show, and the roll takes the
  // height that is left. An open tab panel and an open selection drawer (Shapes, Pattern...) share what the bars, the
  // strip and a usable roll leave over, and each scrolls inside itself beyond its share, so opening one never pushes
  // the strip or the hint off the window. On a phone (narrow) the page scrolls instead (the CSS lifts the caps).
  // When even that does not fit (a small laptop window with a user file's suggestions, a selection and a tab open),
  // the waveform gives up some of its height (down to WAVE_MIN) so the roll keeps ROLL_FLOOR; past that the page
  // scrolls a little rather than squash the roll below its CSS min-height (Kalan edits on the roll).
  const PART_MIN = 60, ROLL_FLOOR = 150, WAVE_MIN = 40;
  function setVar(node, name, v) { if (node.style.getPropertyValue(name) !== v) node.style.setProperty(name, v); }
  function fitLayout() {
    if (rollHold) return;   // the bars above the roll keep their size while the pointer is on the roll (holdEnd fits)
    const H = window.innerHeight || document.documentElement.clientHeight || 0;
    if (!H) return;
    const cs = getComputedStyle($('app'));
    let fixed = (parseFloat(cs.paddingTop) || 0) + (parseFloat(cs.paddingBottom) || 0) + 2;
    for (const id of ['topBar', 'modeBar', 'selRow', 'tabs', 'stripRow', 'hintBar']) { const e = $(id); if (e) fixed += e.getBoundingClientRect().height; }
    const drawer = document.querySelector('#selBar .slp-drawer');
    const dOpen = !!(drawer && drawer.getClientRects().length && drawer.childNodes.length);
    if (dOpen) fixed -= drawer.getBoundingClientRect().height;   // the selection row counts it: it is shared out below
    const pane = app.folded ? null : document.querySelector('#panels .pane.on');
    const pOpen = !!(pane && pane.getClientRects().length);
    const dNat = dOpen ? drawer.scrollHeight + 2 : 0, pNat = pOpen ? pane.scrollHeight : 0;   // their heights uncapped
    // the waveform's share: fixed was measured with the current cut, so the full-height figure comes first
    const ww = $('waveWrap'), cut0 = parseFloat(ww.style.getPropertyValue('--wave-cut')) || 0;
    const waveNat = parseFloat(getComputedStyle(ww).getPropertyValue('--wave-h')) || 0;
    const narrow = !!(window.matchMedia && window.matchMedia('(max-width:640px)').matches);   // a phone scrolls instead
    // the strip row is as tall as the taller of the sound column and the strip: a cut shortens the row only down to
    // the sound column
    const col = $('soundCol'), colTop = col.getBoundingClientRect().top;   // its content (the column itself stretches)
    let colH = 0;
    for (const k of col.children) { const r = k.getBoundingClientRect(); if (r.height) colH = Math.max(colH, r.bottom - colTop); }
    colH += parseFloat(getComputedStyle(col).paddingBottom) || 0;
    const stripNat = $('strip').getBoundingClientRect().height + cut0;
    const rowAt = c => Math.max(colH, stripNat - c);
    const fixedNat = fixed + rowAt(0) - rowAt(cut0);
    const need = fixedNat + ROLL_FLOOR + (pOpen ? Math.min(PART_MIN, pNat) : 0) + (dOpen ? Math.min(PART_MIN, dNat) : 0) + 3;
    const cut = narrow ? 0 : clamp(Math.ceil(need - H), 0, Math.max(0, waveNat - WAVE_MIN));
    if (Math.abs(cut - cut0) >= 1) { if (cut) ww.style.setProperty('--wave-cut', cut + 'px'); else ww.style.removeProperty('--wave-cut'); }
    fixed = fixedNat - (rowAt(0) - rowAt(cut));
    const rollMin = clamp(Math.round(H * 0.26), 140, 400);
    const room = Math.max(0, Math.floor(H - fixed - rollMin - 3));   // 3: the bottom borders of the panel and the roll
    let dCap, pCap;
    if (dOpen && pOpen) {
      dCap = Math.min(dNat, Math.max(PART_MIN, Math.floor(room / 2)));
      pCap = Math.max(PART_MIN, room - dCap);
      if (pCap > pNat) { pCap = Math.max(PART_MIN, pNat); dCap = Math.max(dCap, Math.min(dNat, room - pCap)); }
    } else {
      // only one is open: it may take all the room; a drawer opened later starts from half of it
      pCap = Math.max(PART_MIN, room);
      dCap = Math.max(PART_MIN, pOpen ? Math.floor(room / 2) : room);
    }
    setVar($('panels'), '--pane-max', pCap + 'px');
    setVar(document.documentElement, '--drawer-max', dCap + 'px');
  }

  // ---- keep the roll still under the mouse
  // Selecting or deselecting changes what the bars above the roll show (the selection tools, the inspector, a drawer,
  // Note settings). A change in their height would move the roll under the mouse, and the second click of a
  // double-click would land somewhere else. So while the pointer is on the roll (for a finger, until a moment after
  // the tap, so a double tap works), the selection row and the tab panel may grow but not shrink, and when they grow
  // the roll scrolls by the same amount: the notes stay where they were on the screen. The bars settle (and the page
  // is fitted to the window again) when the pointer leaves the roll.
  const HOLD_IDS = ['selRow', 'panels'];
  let rollHold = false, holdTimer = 0, rollTop = null, rollPageY = 0;
  function holdStart() {
    clearTimeout(holdTimer); holdTimer = 0;
    if (rollHold) return;
    rollHold = true;
    for (const id of HOLD_IDS) { const e = $(id); if (e) e.style.minHeight = e.getBoundingClientRect().height + 'px'; }
    rollTop = rollWrap.getBoundingClientRect().top; rollPageY = window.scrollY || 0;
  }
  function holdEnd(now) {
    clearTimeout(holdTimer); holdTimer = 0;
    if (!rollHold) return;
    const go = () => {
      holdTimer = 0;
      if (drag) { holdTimer = setTimeout(go, 200); return; }   // never in the middle of a drag
      rollHold = false; rollTop = null;
      for (const id of HOLD_IDS) { const e = $(id); if (e) e.style.minHeight = ''; }
      fitLayout();
    };
    if (now) go(); else holdTimer = setTimeout(go, 450);
  }
  function keepRollStill() {
    if (!rollHold) return;
    for (const id of HOLD_IDS) { const e = $(id); if (e) { const h = e.getBoundingClientRect().height; if (h > (parseFloat(e.style.minHeight) || 0) + 0.01) e.style.minHeight = h + 'px'; } }
    const t = rollWrap.getBoundingClientRect().top;
    // when the page itself scrolled (a phone: the user, or the browser keeping its place) the page moved, not the bars
    const py = window.scrollY || 0;
    if (rollTop != null && py === rollPageY && Math.abs(t - rollTop) > 0.5) scroller.scrollTop += t - rollTop;
    rollTop = t; rollPageY = py;
  }
  // the tab panel folds away to give the roll more room (click the open tab again, or Hide)
  function setFolded(on) {
    app.folded = !!on;
    $('panels').classList.toggle('folded', app.folded);
    const fb = $('foldBtn');
    if (fb) { fb.innerHTML = app.folded ? '&#9662; Show' : '&#9652; Hide'; fb.title = app.folded ? 'Show the ' + tabName(app.tab) + ' panel again' : 'Fold the panel away to give the roll more room (or click the open tab)'; }
    redraw(); saveSoon();
  }
  const tabName = t => { const b = document.querySelector('#tabs .tab[data-t="' + t + '"]'); return b ? b.textContent : t; };
  function rateOptions(sel, value) {
    sel.innerHTML = '';
    for (const id of M.RATES) sel.add(new Option(M.rateLabel(id), id));
    if (value) sel.value = value;
    return sel;
  }
  function fillSoundSel() {
    const s = $('soundSel');
    s.innerHTML = '';
    for (const it of SND.list) s.add(new Option(it.name, it.id));
    if (app.sample && !app.sample.builtIn) s.add(new Option('\u{1F4C4} ' + app.sample.name, 'file'));
    s.value = app.soundId;
  }
  function syncGlideUI() {
    const n = primaryNote();
    $('gMode').value = app.inst.glideMode;
    $('gTime').value = String(app.inst.glideTime);
    if (!n) { $('gSelV').textContent = '—'; $('gSel').disabled = true; return; }
    $('gSel').disabled = false;
    $('gSel').value = n.glide == null ? app.inst.glideTime : n.glide;
    $('gSelV').textContent = n.glide == null ? 'auto' : n.glide === 0 ? 'jump' : fmtBeats(n.glide);
  }
  function syncControls() {
    const I = app.inst;
    $('bpm').value = app.bpm; $('bars').value = app.clip.bars; $('snap').value = String(app.snap);
    $('root').value = String(clamp(Math.round(I.root), 0, 127));
    $('cents').textContent = fmtCents(I.cents);
    $('key').value = String(I.key); $('scale').value = I.scale;
    $('lockBtn').classList.toggle('on', I.scaleLock);
    $('vrate').value = I.vibRate; $('vrateV').textContent = I.vibRate.toFixed(1) + ' Hz';
    if (![...$('bendRange').options].some(o => +o.value === I.bendRange)) $('bendRange').add(new Option(I.bendRange + (I.bendRange === 1 ? ' semitone' : ' semitones'), I.bendRange));
    $('bendRange').value = String(I.bendRange);
    $('legatoBtn').classList.toggle('on', I.liveLegato);
    $('m1Lane').value = I.mod1.target; $('m2Lane').value = I.mod2.target;
    $('cc1').textContent = I.mod1.cc; $('cc2').textContent = I.mod2.cc;
    $('atEnd').value = I.region.atEnd;
    $('loopBtn').classList.toggle('on', app.loop);
    syncGlideUI();
    if (app.sample) updateStripText();
  }
  function wirePanels() {
    document.querySelectorAll('#tabs .tab[data-t]').forEach(t => t.addEventListener('click', () => { if (t.dataset.t === app.tab && !app.folded) setFolded(true); else showTab(t.dataset.t); }));
    if ($('foldBtn')) $('foldBtn').addEventListener('click', () => setFolded(!app.folded));
    // Glide
    $('gMode').onchange = e => patchInst({ glideMode: e.target.value }, 'Auto slide');
    $('gTime').onchange = e => patchInst({ glideTime: +e.target.value }, 'Slide time');
    let g0 = null;
    $('gSel').addEventListener('pointerdown', () => { g0 = app.clip; });
    $('gSel').addEventListener('input', e => { const ids = getSelection(); if (!ids.length) return; setClip(M.updateNotes(g0 || app.clip, ids, { glide: +e.target.value }), null); });
    $('gSel').addEventListener('change', () => { if (getSelection().length) commit('Slide time (notes)'); g0 = null; });
    $('gAuto').onclick = () => { const ids = getSelection(); if (!ids.length) { toast('Select notes first'); return; } setClip(M.updateNotes(app.clip, ids, { glide: null, gc: 0 }), 'Slide: auto'); };
    $('gOff').onclick = () => { const ids = getSelection(); if (!ids.length) { toast('Select notes first'); return; } setClip(M.updateNotes(app.clip, ids, { glide: 0 }), 'Slide: jump'); };
    // Copy / Paste
    const cc = $('copyChips');
    for (const L of M.LANES) {
      const b = el('button', { class: 'chip on', title: 'Copy ' + L.name });
      b.textContent = L.name; b.style.borderColor = laneVar(L.id);
      const paint = () => { const on = app.copyMask.has(L.id); b.classList.toggle('on', on); b.style.background = on ? laneVar(L.id) : ''; b.style.color = on ? 'var(--on-lane)' : ''; };
      b.onclick = () => { if (app.copyMask.has(L.id)) app.copyMask.delete(L.id); else app.copyMask.add(L.id); paint(); };
      paint(); cc.appendChild(b);
    }
    $('copyBtn').onclick = copyLanes; $('pasteBtn').onclick = pasteLanes;
    // Mod wheels
    for (const w of [1, 2]) {
      const s = $('m' + w + 'Lane');
      s.add(new Option('— off —', 'off'));
      for (const L of M.LANES) s.add(new Option((L.sub ? 'Sample: ' : '') + L.name, L.id));
      s.onchange = e => patchInst({ ['mod' + w]: { target: e.target.value } }, 'Mod ' + w + ' target');
      $('m' + w).addEventListener('input', e => setWheel(w - 1, +e.target.value));
      $('learn' + w).onclick = () => { app.learn = w; toast('Move the wheel or knob you want for Mod ' + w); };
      $('free' + w).onclick = () => { app.wheelTouched[w - 1] = false; post({ type: 'ctrl', name: 'mod' + w, value: null }); toast('Mod ' + w + ' let go: the lanes rule again'); };
    }
    const bend = $('bend');
    bend.addEventListener('input', e => setBend(+e.target.value));
    const spring = () => { bend.value = 0; setBend(0); };
    bend.addEventListener('pointerup', spring); bend.addEventListener('change', spring);
    $('sustainBtn').onclick = () => setSustain(!app.sustain);
    $('midiBtn2').onclick = connectMidi;
    // Sound
    const root = $('root');
    for (let p = 0; p <= 127; p++) root.add(new Option(noteName(p), p));
    root.onchange = e => { setInst(M.mergeInst(app.inst, { root: +e.target.value, cents: 0 }), null); };
    $('detectBtn').onclick = () => { if (!app.sample) return; toast('Listening...'); setTimeout(() => runDetect('button'), 30); };
    M.NAMES.forEach((n, i) => $('key').add(new Option(n, i)));
    Object.keys(M.SCALES).forEach(s => $('scale').add(new Option(scaleLabel(s), s)));
    $('key').onchange = e => patchInst({ key: +e.target.value }, 'Key');
    $('scale').onchange = e => patchInst({ scale: e.target.value }, 'Scale');
    $('lockBtn').onclick = () => patchInst({ scaleLock: !app.inst.scaleLock }, app.inst.scaleLock ? 'Scale lock off' : 'Scale lock on');
    $('vrate').addEventListener('input', e => patchInst({ vibRate: +e.target.value }, null));
    $('vrate').addEventListener('change', () => commit('Vibrato speed'));
    $('bendRange').onchange = e => patchInst({ bendRange: +e.target.value }, 'Bend range');
    $('legatoBtn').onclick = () => patchInst({ liveLegato: !app.inst.liveLegato }, app.inst.liveLegato ? 'Live legato off' : 'Live legato on');
    // selection row
    $('selAllBtn').onclick = selectAll; $('dupBtn').onclick = duplicateSel; $('delBtn').onclick = deleteSel;
    // two clicks: the first arms it for 2.5 s (one timer, restarted on each arming, so the label and the state agree)
    let clearArm = false, clearTimer = 0;
    const disarmClear = () => { clearTimeout(clearTimer); clearTimer = 0; clearArm = false; $('clearBtn').textContent = 'Clear all'; };
    $('clearBtn').onclick = () => {
      if (clearArm) { disarmClear(); setClip(Object.assign({}, app.clip, { notes: [] }), 'Clear all'); setSel([], null); return; }
      clearArm = true; $('clearBtn').textContent = 'Click again to clear';
      clearTimeout(clearTimer); clearTimer = setTimeout(disarmClear, 2500);
    };
  }
  function setWheel(i, v) {
    app.wheels[i] = clamp(v, 0, 1); app.wheelTouched[i] = true;
    $('m' + (i + 1)).value = app.wheels[i];
    post({ type: 'ctrl', name: 'mod' + (i + 1), value: app.wheels[i] });
  }
  function setBend(v) { app.bend = clamp(v, -1, 1); $('bend').value = app.bend; post({ type: 'ctrl', name: 'bend', value: app.bend }); }
  function setSustain(on) {
    app.sustain = !!on; $('sustainBtn').classList.toggle('on', app.sustain); post({ type: 'ctrl', name: 'sustain', value: app.sustain ? 1 : 0 });
    if (!app.sustain) releaseRecSus();
  }

  // =============================================================================================== record (v3's Rec)
  // One undo step per recorded pass: each played note joins the clip at once (live), and the pass becomes one
  // step 'Record (n notes)' when the loop goes round, on Stop, or when Rec is switched off.
  // Any other edit during a pass (a knob, a click on the roll) first makes the notes recorded so far their own step
  // (setClip / setInst call endRecPass), so Undo of that edit never takes the played notes with it.
  // A key let go while the sustain pedal is down keeps sounding, so its recorded note waits in recSus and ends when
  // the pedal comes up (or when the same key is played again). A note still sounding when the loop goes round is
  // written up to the loop end and carries on from the start of the next pass (as v3 did).
  const recLive = new Map();   // key -> { p, vel, start, samples }
  const recSus = [];           // notes held by the sustain pedal: { p, vel, start, samples }
  let recPass = 0, recLastBeat = null, recAdding = false;
  function endRecPass() {
    if (!recPass) return;
    const n = recPass;
    recPass = 0;
    commit('Record (' + n + (n === 1 ? ' note)' : ' notes)'));
  }
  const recSample = b => ({ b, m1: app.wheels[0], m2: app.wheels[1], bend: app.bend });
  setInterval(() => {
    if (!(app.rec && app.playing)) { recLastBeat = null; return; }
    const b = curBeat();
    if (recLastBeat != null && b < recLastBeat - 0.25) {   // the loop went round: that pass is one step
      const T = total();
      for (const o of [...recLive.values(), ...recSus]) { o.samples.push(recSample(T)); commitRec(o, T); o.start = 0; o.samples = []; }
      endRecPass();
    }
    recLastBeat = b;
    for (const o of recLive.values()) o.samples.push(recSample(b));
    for (const o of recSus) o.samples.push(recSample(b));
  }, 30);
  // the sustain pedal came up (or Rec / Play stopped): the notes it held end here
  function releaseRecSus(endB) {
    const list = recSus.splice(0);
    if (app.rec && app.playing) for (const o of list) commitRec(o, endB == null ? curBeat() : endB);
  }
  function commitRec(o, endB) {
    let s = o.start;
    if (endB < s) endB = total();
    if (app.snap) s = Math.round(s / app.snap) * app.snap;
    const l = Math.min(Math.max(app.snap || 0.0625, endB - s), total() - s);
    if (s >= total() || l <= 0) return;
    // the roll shows C0 to C6: a key played outside that lands an octave (or two) inside it, so it can be seen and edited
    let p = o.p;
    while (p > MAXP) p -= 12;
    while (p < MINP) p += 12;
    const r = M.addNote(app.clip, p, s, l, { vel: o.vel });
    let clip = r.clip;
    const toNodes = f => {
      const pts = o.samples.filter(q => q.b >= o.start).map(q => ({ t: clamp((q.b - s) / l, 0, 1), v: clamp(f(q), 0, 1) }));
      if (pts.length < 2) return null;
      pts[0].t = 0; pts[pts.length - 1].t = 1;
      return M.rdp(pts, 0.015).map(q => ({ t: q.t, v: q.v, shape: 'smooth', bend: 0 }));
    };
    for (const w of [0, 1]) {
      const tg = app.inst['mod' + (w + 1)].target;
      if (app.wheelTouched[w] && tg !== 'off') { const nd = toNodes(q => (w ? q.m2 : q.m1)); if (nd) clip = M.setLaneNodes(clip, r.id, tg, nd); }
    }
    if (o.samples.some(q => q.bend !== 0)) { const br = app.inst.bendRange; const nd = toNodes(q => 0.5 + q.bend * br / 24); if (nd) clip = M.setLaneNodes(clip, r.id, 'pitch', nd); }
    recAdding = true;
    try { setClip(clip, null); } finally { recAdding = false; }
    recPass++;
  }
  // Stop or Rec off: the notes still sounding end at endB (where it stopped; the clip end when endB is missing)
  function flushRec(endB) {
    const e = endB == null ? total() : endB;
    for (const [k, o] of recLive) { recLive.delete(k); if (app.rec) commitRec(o, e); }
    for (const o of recSus.splice(0)) if (app.rec) commitRec(o, e);
    endRecPass();
  }

  // =============================================================================================== MIDI
  const MIDI_MSG = 'MIDI keyboards work when you open the downloaded file in Chrome or Edge';
  function midiRefused() {
    $('midiMsg').textContent = MIDI_MSG;
    $('midiBtn').title = MIDI_MSG;
    toast(MIDI_MSG, 6000);
  }
  // The browser may ask the user first (a question near the address bar) or never answer: say at once that it is
  // asking, and again if no answer comes, so the button never looks dead.
  let midiAsking = 0;
  async function connectMidi() {
    if (!navigator.requestMIDIAccess) { midiRefused(); return; }
    const WAIT = 'Asking the browser for MIDI access...';
    $('midiMsg').textContent = WAIT;
    toast(WAIT, 3000);
    if (midiAsking) return;   // still waiting for the answer to the first click
    const token = midiAsking = setTimeout(() => {
      if (midiAsking !== token) return;
      midiAsking = 0;   // a later click asks again (an answer to this request still connects)
      const msg = 'No answer from the browser yet: look for a question about MIDI devices near the address bar. ' + MIDI_MSG + '.';
      $('midiMsg').textContent = msg; toast(msg, 7000);
    }, 4000);
    const done = () => { clearTimeout(token); if (midiAsking === token) midiAsking = 0; };
    try {
      let acc;
      try { acc = await navigator.requestMIDIAccess(); } finally { done(); }
      const hook = () => {
        let n = 0;
        acc.inputs.forEach(inp => { inp.onmidimessage = onMidi; n++; });
        const msg = n ? 'MIDI: ' + n + (n === 1 ? ' keyboard connected' : ' inputs connected') : 'No MIDI keyboard found: plug one in and click MIDI again';
        toast(msg); $('midiMsg').textContent = msg;
        $('midiBtn').classList.toggle('on', n > 0); $('midiBtn2').classList.toggle('on', n > 0);
      };
      hook(); acc.onstatechange = hook;
    } catch (err) { midiRefused(); }
  }
  function onMidi(m) {
    const [st, d1, d2] = m.data, t = st & 0xf0;
    if (t === 0x90 && d2 > 0) liveOn('midi' + d1, d1, d2 / 127);
    else if (t === 0x80 || (t === 0x90 && d2 === 0)) liveOff('midi' + d1);
    else if (t === 0xb0) {
      if (app.learn) { const w = app.learn; app.learn = 0; patchInst({ ['mod' + w]: { cc: d1 } }, 'Learn CC'); toast('Mod ' + w + ' now follows CC ' + d1); return; }
      if (d1 === 64) { setSustain(d2 >= 64); return; }
      if (d1 === app.inst.mod1.cc) setWheel(0, d2 / 127);
      else if (d1 === app.inst.mod2.cc) setWheel(1, d2 / 127);
    } else if (t === 0xe0) setBend(((d2 << 7 | d1) - 8192) / 8192);
  }

  // =============================================================================================== keyboard (only with Keys on; never the spacebar)
  const KMAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12, o: 13, l: 14, p: 15, ';': 16 };
  function typing(t) {
    if (!t || !t.tagName) return false;
    if (t.isContentEditable || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT') return true;
    return t.tagName === 'INPUT' && !['range', 'checkbox', 'button', 'radio'].includes(t.type);
  }
  // A menu (<select>) picked with the mouse keeps the focus, and its type-ahead would eat the Keys (pressing F jumps
  // the Grid to 'Free', A sets the Key to A...). Such a select lets go of the focus after a pick, and a key that
  // still reaches it goes to the page instead. A select reached with Tab keeps the keys (arrows, letters) as usual.
  let lastTabAt = 0;
  window.addEventListener('keydown', e => { if (e.key === 'Tab') lastTabAt = Date.now(); }, true);
  document.addEventListener('focusin', e => { const t = e.target; if (t && t.tagName === 'SELECT') t.dataset.kbFocus = Date.now() - lastTabAt < 500 ? '1' : ''; });
  document.addEventListener('change', e => {
    const t = e.target;
    if (!t || !t.tagName) return;
    if (t.tagName === 'SELECT' && !t.dataset.kbFocus) t.blur();
    else if (t.tagName === 'INPUT' && t.type === 'number' && document.activeElement === t) t.blur();   // Tempo, Bars: Enter commits and lets go
  }, true);
  window.addEventListener('keydown', e => {
    if (menuOpen() && e.key === 'Escape') { closeMenu(); e.preventDefault(); return; }
    const t = e.target, mouseSelect = !!(t && t.tagName === 'SELECT' && !t.dataset.kbFocus);
    // a menu picked with the mouse never takes a key, with Keys on or off: a stray letter must not change the Grid or
    // the Key behind the user's back (Space is still never taken)
    if (mouseSelect && !['Tab', 'Shift', 'Control', 'Alt', 'Meta'].includes(e.key)) {
      t.blur();
      if (!(e.key === ' ' || e.code === 'Space')) e.preventDefault();
    }
    if (!app.keys || (typing(t) && !mouseSelect)) return;
    if (e.key === ' ' || e.code === 'Space') return;   // the spacebar stays with Cubase
    const k = e.key.toLowerCase(), mod = e.ctrlKey || e.metaKey;
    if (mod) {
      if (k === 'z') { e.preventDefault(); if (e.shiftKey) redo(); else undo(); }
      else if (k === 'y') { e.preventDefault(); redo(); }
      else if (k === 'c') { e.preventDefault(); copyLanes(); }
      else if (k === 'v') { e.preventDefault(); pasteLanes(); }
      else if (k === 'd') { e.preventDefault(); duplicateSel(); }
      else if (k === 'a') { e.preventDefault(); selectAll(); }
      return;
    }
    if (e.altKey) return;
    if (e.repeat) { if (k in KMAP) e.preventDefault(); return; }
    if (k === 'delete' || k === 'backspace') { e.preventDefault(); deleteSel(); return; }
    if (k === 'escape') { setSel([], null); setNodePicks(new Map()); emit('nodes', app.nodeSel); closeMenu(); requestDraw(); return; }
    if (k in KMAP) { e.preventDefault(); if (!audio.ready) initAudio(); liveOn('k' + k, app.oct + KMAP[k], 0.8); return; }
    if (k === 'z') { app.oct = Math.max(24, app.oct - 12); toast('Keys play from ' + noteName(app.oct)); return; }
    if (k === 'x') { app.oct = Math.min(96, app.oct + 12); toast('Keys play from ' + noteName(app.oct)); return; }
    if (/^[1-8]$/.test(k)) { setMode(+k); return; }
    if (k === 'n') { toggleNodes(); return; }
    if (k === 'b') { armLink(); return; }
  });
  window.addEventListener('keyup', e => { const k = e.key.toLowerCase(); if (k in KMAP) liveOff('k' + k); });
  window.addEventListener('blur', () => { for (const key of [...held.keys()]) if (key[0] === 'k') liveOff(key); });

  // =============================================================================================== hint bar and toast
  function modeHint() {
    if (app.pan) return 'SCROLL: drag with the mouse (or swipe) to move around the roll. Click ✋ Scroll again to edit.';
    if (app.linkArm) return 'SLIDE INTO: click the note the selected note should slide into.';
    const AL = activeLane();
    if (app.mode === 1) return 'BUILD (1): double-click empty space = new note · drag a note to move it · drag its end dots sideways (length) or up/down (bend) · ◆ slide handles · Shift+click a 2nd note = slide into it · drag on empty space = box select (Shift adds, Ctrl toggles) · right-click = menu';
    if (app.free) return 'FREEHAND (' + LANE[AL].name + '): paint across the selected note; it turns into nodes when you let go.';
    const name = app.mode === 8 ? 'SAMPLE (8) · ' + LANE[AL].name.toUpperCase() : LANE[AL].name.toUpperCase() + ' (' + app.mode + ')';
    if (app.nodesMode) return name + ' · NODES: drag a box around nodes of the selected notes, then drag one of them to move them together (Shift adds, Ctrl toggles).';
    const extra = app.mode === 8 ? ' · ' + SUB_HINT[AL] : AL === 'pitch' ? ' · nodes snap to semitones (hold Shift for free) · ◆ slide handles' : '';
    return name + ': notes are locked, only ' + LANE[AL].name + ' changes · click a note to select it · drag ● nodes · double-click the line = add a node · double-click a node = delete · drag the small ◇ in a segment = curve it (it fills in), double-click a filled ◆ = straighten · ▾ on the lane button = shapes for every selected note' + extra;
  }
  // the mode's hint stays in the bar (it sets the bar's height); hover help covers it without changing any size
  function updateHint() {
    const h = $('hint'), t = modeHint();
    if (h.textContent !== t) h.textContent = t;
    const hv = $('hintHover');
    if (app.hintOverride) { if (hv.textContent !== app.hintOverride) hv.textContent = app.hintOverride; hv.hidden = false; }
    else if (!hv.hidden) { hv.hidden = true; hv.textContent = ''; }
  }
  function setHint(t) { app.hintOverride = t || null; updateHint(); }
  function toast(m, ms) {
    const t = $('toast');
    t.textContent = m; t.style.opacity = 1;
    clearTimeout(t._h); t._h = setTimeout(() => { t.style.opacity = 0; }, ms || 2400);
  }

  // =============================================================================================== saving (per browser, optional)
  let saveTimer = 0;
  function saveSoon() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => store.set(SAVE_KEY, { v: 4, clip: app.clip, inst: app.inst, bpm: app.bpm, snap: app.snap, loop: app.loop, mode: app.mode, subLane: app.subLane, tab: app.tab, folded: app.folded, sound: app.sample && app.sample.builtIn ? app.sample.id : 'sawopen', soundFile: !!(app.sample && !app.sample.builtIn) }), 400);
  }

  // =============================================================================================== wiring
  function wire() {
    // transport and top bar
    $('play').onclick = () => (app.playing ? stop() : play());
    $('loopBtn').onclick = () => { app.loop = !app.loop; paintTransport(); postTransport(null); saveSoon(); emit('transport', { playing: app.playing, bpm: app.bpm, loop: app.loop }); };
    $('rec').onclick = () => {
      if (app.rec) flushRec(app.playing ? curBeat() : null);   // the notes still sounding end here, then Rec goes off
      app.rec = !app.rec; $('rec').classList.toggle('on', app.rec);
      if (app.rec) toast('Armed: press Play, then play the keys and wheels');
    };
    $('undo').onclick = undo; $('redo').onclick = redo;
    $('freeBtn').onclick = () => {
      if (app.mode === 1 && !app.free) { toast('Freehand draws a lane: pick a lane mode (2 to 8) first'); return; }
      app.free = !app.free; $('freeBtn').classList.toggle('on', app.free); updateHint(); requestDraw();
    };
    $('linkBtn').onclick = armLink;
    // a hint cut to two lines (short windows) shows in full over the bar while the mouse is on it
    $('hint').addEventListener('mouseenter', () => { const h = $('hint'); if (h.scrollHeight > h.clientHeight + 2) setHint(modeHint()); });
    $('hint').addEventListener('mouseleave', () => { if (app.hintOverride === modeHint()) setHint(null); });
    $('multiBtn').onclick = () => { app.multi = !app.multi; $('multiBtn').classList.toggle('on', app.multi); $('multiBtn').innerHTML = (app.multi ? '&#9745;' : '&#9744;') + ' Multi'; };
    $('keysBtn').onclick = () => {
      app.keys = !app.keys; $('keysBtn').classList.toggle('on', app.keys);
      toast(app.keys ? 'Keys on: A to K play notes, Z / X change octave, 1 to 8 change mode, Ctrl+Z undo' : 'Keys off: every key goes to Cubase');
      if (!app.keys) for (const key of [...held.keys()]) if (key[0] === 'k') liveOff(key);
    };
    $('panBtn').onclick = () => { app.pan = !app.pan; $('panBtn').classList.toggle('on', app.pan); scroller.style.touchAction = app.pan ? 'auto' : 'none'; updateHint(); };
    $('bpm').onchange = e => setBpm(+e.target.value);
    $('bars').onchange = e => {
      const bars = clamp(Math.round(+e.target.value) || 4, 1, 32);
      e.target.value = bars;
      if (bars === app.clip.bars) return;
      if (app.playing) stop();
      setClip(Object.assign({}, app.clip, { bars }), 'Bars');
      if (app.startBeat > total()) app.startBeat = 0;
    };
    $('snap').onchange = e => { app.snap = +e.target.value; requestDraw(); saveSoon(); };
    $('themeBtn').onclick = () => applyTheme(effectiveTheme() === 'dark' ? 'light' : 'dark', true);
    if (lightQuery && lightQuery.addEventListener) lightQuery.addEventListener('change', () => applyTheme(null, false));
    const zoom = (h, v) => {
      const cx = (scroller.scrollLeft + scroller.clientWidth / 2) / BW, cyy = (scroller.scrollTop + scroller.clientHeight / 2) / RH;
      BW = clamp(BW * h, 12, 400); RH = clamp(Math.round(RH * v), 8, 30);
      sizeSpacer();
      scroller.scrollLeft = cx * BW - scroller.clientWidth / 2; scroller.scrollTop = cyy * RH - scroller.clientHeight / 2;
      redraw();
    };
    $('hz-').onclick = () => zoom(1 / 1.3, 1); $('hz+').onclick = () => zoom(1.3, 1);
    $('vz-').onclick = () => zoom(1, 0.8); $('vz+').onclick = () => zoom(1, 1.25);
    $('midiBtn').onclick = connectMidi;
    $('soundOn').onclick = () => initAudio();
    // the roll (it stays still under the mouse while the bars above it change: holdStart / keepRollStill)
    $('editor').addEventListener('pointerenter', holdStart);
    // a mouse lets go at once (the bars settle before it reaches anything up there); a finger a moment later, as it
    // "leaves" after every tap and a double tap needs the roll to stay put
    $('editor').addEventListener('pointerleave', e => holdEnd(e.pointerType === 'mouse' || e.pointerType === 'pen'));
    scroller.addEventListener('pointerdown', holdStart, true);
    scroller.addEventListener('pointerdown', rollDown);
    scroller.removeAttribute('tabindex');
    scroller.addEventListener('pointermove', rollMove);
    scroller.addEventListener('pointerup', rollUp);
    scroller.addEventListener('pointercancel', rollUp);
    scroller.addEventListener('lostpointercapture', () => { if (drag) rollUp(); });
    scroller.addEventListener('scroll', () => { requestDraw(); movePH(); });
    scroller.addEventListener('contextmenu', e => {
      e.preventDefault();
      const P0 = rollPos(e), h = hitNote(P0.x, P0.y);
      if (h) { if (!app.sel.has(h.id)) setSel([h.id], h.id); else setPrimary(h.id); }
      openContextMenu(e.clientX, e.clientY, h ? h.id : null, P0);
    });
    scroller.addEventListener('wheel', e => {
      if (!(e.ctrlKey || e.metaKey)) return;
      e.preventDefault();
      const r = scroller.getBoundingClientRect(), bx = (e.clientX - r.left + scroller.scrollLeft) / BW;
      BW = clamp(BW * (e.deltaY < 0 ? 1.15 : 1 / 1.15), 12, 400);
      sizeSpacer();
      scroller.scrollLeft = bx * BW - (e.clientX - r.left);
      redraw();
    }, { passive: false });
    // piano and ruler
    const pw = $('pianoWrap');
    pw.addEventListener('pointerdown', e => {
      e.preventDefault();
      try { pw.setPointerCapture(e.pointerId); } catch (err) { /* ok */ }
      const r = pw.getBoundingClientRect();
      liveOn('m' + e.pointerId, pitchAt(e.clientY - r.top + scroller.scrollTop), 0.8);
    });
    ['pointerup', 'pointercancel'].forEach(t => pw.addEventListener(t, e => liveOff('m' + e.pointerId)));
    pw.addEventListener('wheel', e => { scroller.scrollTop += e.deltaY; e.preventDefault(); }, { passive: false });
    $('rulerWrap').addEventListener('pointerdown', e => { const r = $('rulerWrap').getBoundingClientRect(); setStartBeat((e.clientX - r.left + scroller.scrollLeft) / BW); });
    // strip
    waveEl.addEventListener('pointerdown', stripDown);
    waveEl.addEventListener('pointermove', e => { if (sdrag) stripMove(e); else hoverStrip(e); });
    waveEl.addEventListener('pointerup', stripUp);
    waveEl.addEventListener('pointercancel', stripUp);
    waveEl.addEventListener('pointerleave', () => { if (!sdrag) waveEl.style.cursor = 'crosshair'; });
    waveEl.addEventListener('wheel', e => {
      if (!app.sample) return;
      e.preventDefault();
      const r = waveEl.getBoundingClientRect();
      if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) { const w = SV.b - SV.a, d = (e.shiftKey ? e.deltaY : e.deltaX) / 600 * w; SV.a = clamp(SV.a + d, 0, 1 - w); SV.b = SV.a + w; dirty.strip = true; return; }
      zoomStrip(e.deltaY < 0 ? 1 / 1.25 : 1.25, posOf(e.clientX - r.left));
    }, { passive: false });
    $('wz-').onclick = () => zoomStrip(1.6); $('wz+').onclick = () => zoomStrip(1 / 1.6);
    $('wzAll').onclick = () => { SV.a = 0; SV.b = 1; dirty.strip = true; };
    $('atEnd').onchange = e => patchInst({ region: { atEnd: e.target.value } }, 'At the end: ' + e.target.selectedOptions[0].text);
    $('soundSel').onchange = e => { if (e.target.value !== 'file') { toast('Loading ' + e.target.selectedOptions[0].text + '...'); setTimeout(() => loadSound(e.target.value), 20); } };
    $('browseBtn').onclick = () => $('file').click();
    $('file').onchange = e => { if (e.target.files[0]) loadFile(e.target.files[0]); e.target.value = ''; };
    // drop a file anywhere on the window
    let dragDepth = 0;
    const hasFiles = e => e.dataTransfer && [...(e.dataTransfer.types || [])].includes('Files');
    window.addEventListener('dragenter', e => { if (!hasFiles(e)) return; e.preventDefault(); dragDepth++; $('drop').style.display = 'flex'; });
    window.addEventListener('dragleave', () => { dragDepth = Math.max(0, dragDepth - 1); if (!dragDepth) $('drop').style.display = 'none'; });
    window.addEventListener('dragover', e => { if (hasFiles(e)) e.preventDefault(); });
    window.addEventListener('drop', e => { e.preventDefault(); dragDepth = 0; $('drop').style.display = 'none'; const f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]; if (f) loadFile(f); });
    // sound may only start after a click: resume on the first pointerdown anywhere
    window.addEventListener('pointerdown', () => { if (!audio.ctx) initAudio(); else if (audio.ctx.state !== 'running') audio.ctx.resume().then(updateSoundChip).catch(() => {}); }, true);
    window.addEventListener('keydown', () => { if (app.keys && audio.ctx && audio.ctx.state !== 'running') audio.ctx.resume().catch(() => {}); }, true);
    // a button clicked with the mouse lets go of the focus, so the spacebar never presses it again (Space stays with Cubase)
    // A click (not a keyboard press) parks the focus on the page wrapper, which never scrolls by itself: a clicked
    // button lets go of the focus (the spacebar never presses it again), and Chrome's spacebar scrolling, which goes to
    // the last clicked scrolling box (the roll, a tab panel), has no box to scroll. Space itself is never taken.
    document.addEventListener('click', e => {
      const t = e.target;
      if (e.detail === 0 || !t || !t.closest) return;
      const FIELD = 'input:not([type=button]):not([type=range]):not([type=checkbox]), select, textarea, [contenteditable="true"]';
      if (t.closest(FIELD)) return;
      const ae = document.activeElement;   // a field the click just opened (a knob's typed value, a name box) keeps the focus
      if (ae && ae !== t && ae.matches && ae.matches(FIELD)) return;
      parkFocus();
    });
    // sizes
    if (window.ResizeObserver) {
      const ro = new ResizeObserver(() => { fitLayout(); redraw(); });
      ro.observe(rollWrap); ro.observe($('waveWrap'));
      for (const id of ['topBar', 'modeBar', 'selRow', 'tabs', 'stripRow', 'hintBar']) if ($(id)) ro.observe($(id));
    }
    window.addEventListener('resize', () => { holdEnd(true); fitLayout(); redraw(); });
    // the roll never keeps the focus (a focused scrolling box scrolls when the spacebar is pressed; Space stays with Cubase)
    scroller.addEventListener('focus', () => scroller.blur());
    wirePanels();
  }
  function hoverStrip(e) {
    if (!app.sample) return;
    const r = waveEl.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top;
    const R = app.inst.region, rx0 = xOf(R.start), rx1 = xOf(R.end);
    waveEl.style.cursor = Math.min(Math.abs(x - rx0), Math.abs(x - rx1)) < 7 ? 'ew-resize' : (y < BAND + 2 && x > rx0 && x < rx1) ? 'grab' : 'crosshair';
  }

  // =============================================================================================== frame loop
  function frame() {
    try {
      if (rollHold) keepRollStill();
      if (snapPending) flushSnapshot();
      if (rawAud) dirty.strip = true;
      if (dirty.roll) { dirty.roll = false; dirty.piano = false; drawRoll(); }
      else if (dirty.piano) { dirty.piano = false; drawPiano(); }
      if (dirty.strip) { dirty.strip = false; drawStrip(); }
      movePH();
      drawMeter();
    } catch (e) { console.warn('StrandLock: drawing failed', e); }
    requestAnimationFrame(frame);
  }

  // =============================================================================================== start
  function parkFocus() {
    const ae = document.activeElement;
    if (ae && menu.contains(ae)) return;
    try { $('app').focus({ preventScroll: true }); } catch (e) { if (ae && ae.blur) ae.blur(); }
  }
  function setGhost(clip) { if (app.ghost === clip) return; app.ghost = clip || null; requestDraw(); }
  function addDecorator(list, fn) { list.push(fn); requestDraw(); dirty.strip = true; return () => { const i = list.indexOf(fn); if (i >= 0) list.splice(i, 1); redraw(); }; }
  function init() {
    readTheme();
    const th = store.get(THEME_KEY);
    if (th === 'light' || th === 'dark') document.documentElement.dataset.theme = th;
    readTheme();
    const saved = store.get(SAVE_KEY);
    let clip = null, inst = null;
    if (saved && saved.v === 4 && saved.clip) {
      try { clip = M.normalizeClip(saved.clip); inst = M.normalizeInst(saved.inst); } catch (e) { clip = null; inst = null; }
    }
    if (!clip) {
      clip = M.loadClipPreset('strand-demo'); inst = M.applySoundPreset(M.defaultInst(), 'init').inst;
      app.folded = (window.innerHeight || 900) < 900;   // first open on a laptop: the roll gets the room, the tabs stay one click away
    }
    else {
      app.bpm = clamp(num(saved.bpm, 96), 40, 240); app.snap = [1, 0.5, 0.25, 0.125, 0].includes(saved.snap) ? saved.snap : 0.25;
      app.loop = saved.loop !== false; app.mode = clamp(num(saved.mode, 1) | 0, 1, 8);
      if (SAMPLE_LANES.includes(saved.subLane)) app.subLane = saved.subLane;
      if (typeof saved.tab === 'string' && document.querySelector('#tabs .tab[data-t="' + saved.tab + '"]')) app.tab = saved.tab;
      app.folded = saved.folded === true;
    }
    const soundId = saved && saved.sound && SND.list.some(s => s.id === saved.sound) ? saved.sound : SND.defaultId || 'sawopen';
    app.clip = clip; app.inst = inst; app.soundRef = soundId;
    app.hist = new M.History({ clip, inst, sound: soundId });
    buildModeBar();
    wire();
    applyTheme(null, false);
    fillSoundSel();
    const wasFolded = app.folded;
    syncControls(); paintTransport(); updateSelInfo(); historyChanged(); showTab(app.tab); setFolded(wasFolded); setMode(app.mode, true); updateHint(); updateSoundChip();
    sizeSpacer();
    requestAnimationFrame(() => {
      const ps = app.clip.notes.map(n => n.p).sort((a, b) => a - b);
      const mid = ps.length ? ps[Math.floor(ps.length / 2)] + 5 : 66;   // centre on most of the notes
      scroller.scrollTop = Math.max(0, cy(mid) - scroller.clientHeight / 2);
      redraw();
    });
    requestAnimationFrame(frame);
    app.soundId = soundId; fillSoundSel(); updateStripText();
    fitLayout();
    // the saved starter keeps its saved Region; a session that ended on the user's own file opens on the starter with
    // the whole file as the Region (that file's Region means nothing for it). The history then starts here.
    setTimeout(() => { try { loadSound(soundId, { label: null, keepRegion: !(saved && saved.soundFile) }); app.hist.clear({ clip: app.clip, inst: app.inst, sound: app.soundRef }); historyChanged(); } catch (e) { console.warn('StrandLock: could not make the sound', e); toast('Could not make the starter sound'); } ready = true; for (const fn of readyFns.splice(0)) { try { fn(api); } catch (e) { console.warn(e); } } emit('ready'); }, 30);
  }
  let ready = false;
  const readyFns = [];

  // =============================================================================================== API
  const api = {
    version: '4.0.0-pass2',
    // state
    getClip: () => app.clip, setClip, getInst: () => app.inst, setInst, patchInst, commit, amendHistory,
    undoLabel: () => app.hist.undoLabel(),
    undo, redo, canUndo: () => app.hist.canUndo(), canRedo: () => app.hist.canRedo(),
    getSelection, setSelection: (ids, primary) => setSel(ids, primary === undefined ? undefined : primary), selectionSet: () => app.sel,
    getPrimary: () => app.primary, setPrimary, primaryNote, selectedNotes, noteById,
    getMode: () => app.mode, setMode: m => setMode(m), getSubLane: () => app.subLane, setSubLane, activeLane,
    getActiveNode: () => app.activeNode, setActiveNode, getNodeSelection: () => app.nodeSel,
    getBpm: () => app.bpm, setBpm, isPlaying: () => app.playing, play, stop, getBeat: curBeat,
    getStartBeat: () => app.startBeat, setStartBeat, isLooping: () => app.loop,
    getSample: () => app.sample, loadSound, loadFile, sampleSeconds, formatCtx, formatLane: fmtLane, suggestRoot,
    getScope: () => app.scope, setScope,
    // drawing
    redraw, setGhost, getGhost: () => app.ghost,
    addNoteDecorator: fn => addDecorator(noteDecorators, fn), addStripDecorator: fn => addDecorator(stripDecorators, fn),
    laneColor, laneVar, theme: () => TH, isDark: () => effectiveTheme() === 'dark',
    stripView: () => ({ a: SV.a, b: SV.b }), setStripView, stripX: xOf, geometry: () => ({ BW, RH, laneH: laneH(), MINP, MAXP }),
    // ui helpers
    toast, openMenu, closeMenu, el, showTab, setHint, rateOptions,
    // events
    on, off, onReady: fn => { if (ready) fn(api); else readyFns.push(fn); },
    // engine
    noteOn, noteOff, post, audioPath: () => audio.path, startAudio: initAudio,
    debug: {
      meter: meterNow,
      audioPath: () => (audio.ready ? audio.path : 'off'),
      lastDisplay: () => newestDisplay,
      snapshotsSent: () => snapshotsSent,
      historyLength: () => app.hist.size,
      historyIndex: () => app.hist.i,
      posts: () => Object.assign({}, postCounts),   // engine messages sent so far, by type
    },
  };
  init();
  return api;
})();
if (typeof module !== 'undefined') module.exports = SLApp;
