/* StrandLock prototype v4: sl-ui-panels.js (pass 2: the selection tools, the inspector, Note settings,
   the roll decorations and the 'Try this' line).
   Global: SLPanels. Loads after sl-ui.js and uses only window.SLApp and SLModel. It builds:
   - #selBar, shown while notes are selected: Lane, Shapes (Each note / Across), Pattern, Random, Turn into
     nodes, Connect (Nearest / Furthest / Random / Re-roll / Disconnect) and Tools (Walk, Chop, Reverse, Flip,
     More, Less, Select similar, Humanize, Strum, Legato). Hovering a shape, pattern or tool draws a dashed
     ghost of the result on the selected notes (U6). After Apply, slider moves update the pattern live and
     each release is one undo step.
   - the ▾ menu on every lane button: Shape / Pattern / Random for that lane on the selected notes, plus
     'Pattern settings...' and 'Random settings...' that open the full panels.
   - #inspector (T6): type Start (bar.beat.sixteenth), Length (beats or a note value), Pitch (C3 = MIDI 60),
     Velocity, and the selected node's time and real value (typed speeds go through travelSpeedToV).
   - #noteSettings (S7): Engine, Lock, Motion, Travel kind, Start, Tape, Each grain backwards, Jump every,
     Restart sample here and Velocity for the selected notes, with 'mixed' where they differ.
   - roll decorations (SLApp.addNoteDecorator): direction and speed chevrons under each note (H4), a faint
     'what this note plays' waveform above it (U7, cached per note and sound), a dot on notes with their own
     settings, a warning where a slide's target starts well inside its source (the engine cuts the source
     short there), and the ghost's slide links (Connect preview).
   - #tryThis (U3): one suggestion per mode, and per Motion in Sample mode.
   Every edit is exactly one undo step: SLApp.setClip(clip, label) on a click or a release, null while a
   slider moves. No alert/confirm/prompt, no storage, no Math.random.
   The pure helpers (parsers, formatters, the head simulation) also load in node:
   require('./src/sl-ui-panels.js') returns them (SLModel is required from the same folder). */
var SLPanels = (function () {
  'use strict';
  const M = (typeof SLModel !== 'undefined' && SLModel) || (typeof require === 'function' ? require('./sl-model.js') : null);
  const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
  const fin = x => typeof x === 'number' && isFinite(x);
  const frac = x => x - Math.floor(x);
  const fmtNum = (x, d) => (fin(x) ? String(+x.toFixed(d == null ? 3 : d)) : '');

  // ================================================================================== pure helpers
  // Positions are Cubase style: bar.beat.sixteenth(.tick), all from 1 except ticks (0..119, 480 per beat).
  const TPB = 480, TP16 = 120;
  function fmtPos(b) {
    const tt = Math.max(0, Math.round(fin(b) ? b * TPB : 0));
    const bar = Math.floor(tt / (4 * TPB)) + 1, r1 = tt - (bar - 1) * 4 * TPB;
    const beat = Math.floor(r1 / TPB) + 1, r2 = r1 - (beat - 1) * TPB;
    const six = Math.floor(r2 / TP16) + 1, tick = r2 - (six - 1) * TP16;
    return bar + '.' + beat + '.' + six + (tick ? '.' + tick : '');
  }
  function clean(t) {
    return String(t == null ? '' : t).trim().toLowerCase().replace(/[−–—]/g, '-').replace(/,/g, '.').replace(/\s+/g, ' ');
  }
  // '2.3.1' -> beats from the clip start (bar 1 = 0). Missing parts count as 1 (ticks 0). null = not a position.
  function parsePos(text) {
    const s = clean(text);
    if (!s) return null;
    const parts = s.split(/[.:\s]+/).filter(x => x !== '');
    if (!parts.length || parts.length > 4 || parts.some(p => !/^\d+$/.test(p))) return null;
    const v = parts.map(Number), bar = v[0], beat = v.length > 1 ? v[1] : 1, six = v.length > 2 ? v[2] : 1, tick = v.length > 3 ? v[3] : 0;
    if (bar < 1 || beat < 1 || six < 1) return null;
    return (bar - 1) * 4 + (beat - 1) + (six - 1) / 4 + tick / TPB;
  }
  // '1/16t', '1/8 dotted', '1 bar' -> a rate id ('1/16T', '1/8D', '1bar'), or null
  function rateId(text) {
    let x = clean(text).replace(/\s*(triplets?|trip)$/, 't').replace(/\s*(dotted|dot)$/, 'd').replace(/^1\s*bars?$/, '1bar').replace(/^bars?$/, '1bar').replace(/\s+/g, '');
    if (/[td]$/.test(x)) x = x.slice(0, -1) + x.slice(-1).toUpperCase();
    return M.isRate(x) ? x : null;
  }
  // Beats from '0.5', '1.5 beats', a note value ('1/8' = 0.5 beat, '3/4' = 3 beats), a rate id ('1/16T', '1 bar')
  // or a Cubase length 'bars.beats.sixteenths(.ticks)' (from 0, e.g. '0.1.0.0' = 1 beat). null = not a length.
  function parseBeats(text) {
    const s = clean(text).replace(/\s*(beats?|b)$/, '');
    if (!s) return null;
    const r = rateId(s);
    if (r) return M.rateBeats(r);
    const f = /^(\d*\.?\d+)\s*\/\s*(\d*\.?\d+)$/.exec(s);
    if (f) { const v = 4 * parseFloat(f[1]) / parseFloat(f[2]); return fin(v) && v > 0 ? v : null; }
    if (/^\d+(\.\d+){2,3}$/.test(s)) { const p = s.split('.').map(Number); return p[0] * 4 + p[1] + (p[2] || 0) / 4 + (p[3] || 0) / TPB; }
    if (/^\d*\.?\d+$/.test(s)) { const v = parseFloat(s); return fin(v) ? v : null; }
    return null;
  }
  // 'C3' = 60 (Cubase), 'F#2', 'Eb4', 'bb1' or a MIDI number -> 0..127, or null
  const PC = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };
  function parsePitch(text) {
    const s = clean(text).replace(/\s+/g, '').replace(/♯/g, '#').replace(/♭/g, 'b');
    if (/^\d{1,3}$/.test(s)) { const p = +s; return p <= 127 ? p : null; }
    const m = /^([a-g])(#|b)?(-?\d+)$/.exec(s);
    if (!m) return null;
    const p = (parseInt(m[3], 10) + 2) * 12 + PC[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0);
    return p >= 0 && p <= 127 ? p : null;
  }
  function numPart(s) {
    const f = /(-?\d*\.?\d+)\s*\/\s*(\d*\.?\d+)/.exec(s);
    if (f) return parseFloat(f[1]) / parseFloat(f[2]);
    const m = /-?\d*\.?\d+(e-?\d+)?/.exec(s);
    return m ? parseFloat(m[0]) : NaN;
  }
  // SLModel.formatLane in the pickers' own words: '1/8x forwards' (not '0.13x'), 'Refresh 1/8 triplet (...)' (not '1/8T')
  function laneText(lane, v, ctx) {
    let t = M.formatLane(lane, v, ctx);
    if (lane === 'travel' && !(ctx && ctx.travelKind === 'path')) {
      const x = M.mapLane('travel', v, ctx || {});
      if (Math.abs(Math.abs(x) - 0.125) < 0.0007) t = t.replace(/^0\.13x/, '1/8x');
    } else if (lane === 'refresh') {
      t = t.replace(/^Refresh (\S+) \(/, (m, id) => (M.isRate(id) ? 'Refresh ' + M.rateLabel(id) + ' (' : m)).replace(/ beat \(/, ' beats (');
    }
    return plainUnits(t);
  }
  // readouts in plain units: 'st' becomes 'semitones', and one of a unit is singular ('1 cycle', '1 semitone')
  function plainUnits(t) {
    return String(t)
      .replace(/(^|[^\w.])([+-]?)(\d+(?:\.\d+)?) st\b/g, (m, pre, sg, n) => pre + sg + n + (n === '1' ? ' semitone' : ' semitones'))
      .replace(/ (semitones?) vibrato\b/, ' $1 of vibrato')
      .replace(/(^|[^\w.])1 (cycles|wavelengths)\b/g, (m, pre, u) => pre + '1 ' + u.slice(0, -1));
  }
  function grainBase(I) {
    if (I.engine === 'voice') return { base: I.grainVoice, unit: 'cycles' };
    if (I.engine === 'cloud') return I.lock ? { base: I.grainLock, unit: 'wavelengths' } : { base: I.grainCloud, unit: 'ms' };
    return { base: 1, unit: 'x' };
  }
  // Typed real value -> lane value v (0..1): the inverse of SLModel.mapLane. ctx = SLApp.formatCtx(note).
  // -> { v, text } (text = what formatLane shows for v) or { error } (a plain sentence for a toast).
  function laneValueFromText(lane, text, ctx) {
    const c = ctx || {}, I = M.normalizeInst(c.inst), s = clean(text);
    const err = e => ({ error: e });
    if (!s) return err('Type a value');
    const x = numPart(s), has = fin(x);
    let v;
    switch (lane) {
      case 'pitch': case 'formant':
        if (!has) return err('Type semitones, for example +3 or -2.5');
        v = 0.5 + x / 24; break;
      case 'vib':
        if (!has) return err('Type the vibrato depth in semitones, for example 0.3');
        v = x; break;
      case 'amp':
        if (/inf|off|silent/.test(s)) { v = 0; break; }
        if (!has) return err('Type a level in dB, for example -6');
        v = Math.sqrt(Math.pow(10, x / 20) / 1.3); break;
      case 'tone': {
        if (!has && /open/.test(s)) { v = 1; break; }
        if (!has) return err('Type a frequency, for example 2.4 kHz or 800 Hz');
        const hz = /k/.test(s) ? x * 1000 : x;
        if (!(hz > 0)) return err('Type a frequency above 0 Hz');
        v = Math.log2(hz / 60) / 8.3; break;
      }
      case 'hollow':
        if (!has) return err('Type how deep the cut is in dB, for example -12');
        v = Math.abs(x) / 28; break;
      case 'travel': {
        if (c.travelKind === 'path') {
          if (!has) return err('Type where in the Region the note is, for example 25%');
          v = /%/.test(s) || Math.abs(x) > 1 ? x / 100 : x; break;
        }
        if (/stop|still|freez/.test(s) || (has && x === 0)) { v = 0.5; break; }
        let sp = has ? x : 1;
        if (/back|rev/.test(s)) sp = -Math.abs(sp); else if (/forw/.test(s)) sp = Math.abs(sp);
        if (!has && !/back|rev|forw/.test(s)) return err('Type a speed such as 2x, 0.5x backwards or stopped');
        v = M.travelSpeedToV(sp); break;
      }
      case 'heads':
        if (!has) return err('Type how many heads, 1 to 8');
        v = 0.5 + (clamp(x, 1, 8) - I.heads) / 14; break;
      case 'grain': {
        if (!has || !(x > 0)) return err('Type a grain size above 0');
        v = 0.5 + Math.log2(x / grainBase(I).base) / 4; break;
      }
      case 'refresh': {
        if (/smooth/.test(s)) { v = 0; break; }
        if (I.refresh.mode === 'smooth') return err('Refresh is set to Smooth on the Sample tab: pick Free or Synced there first');
        const bpm = clamp(fin(c.bpm) ? c.bpm : 120, 1, 1000);
        // the readout reads 'Refresh 1/16 (156 ms at 96 bpm)' or 'Refresh 1.464 beat (915 ms ...)': ms wins, then beats, a rate, seconds
        const msM = /(\d*\.?\d+)\s*ms/.exec(s), beatM = /(\d*\.?\d+)\s*beats?/.exec(s), secM = /(\d*\.?\d+)\s*s(ec|econds?)?\b/.exec(s);
        const r = rateId(s.replace(/^refresh\s*/, '').replace(/\s*\(.*$/, ''));
        let ms;
        if (msM) ms = parseFloat(msM[1]);
        else if (beatM) ms = parseFloat(beatM[1]) * 60000 / bpm;
        else if (r) ms = M.rateBeats(r) * 60000 / bpm;
        else if (secM) ms = parseFloat(secM[1]) * 1000;
        else if (has) ms = x;
        else return err('Type a time in ms, a rate such as 1/16, or Smooth');
        if (!(ms > 0)) return err('Type a time above 0 ms');
        const base = I.refresh.mode === 'sync' ? M.rateBeats(I.refresh.rate) * 60000 / bpm : I.refresh.ms;
        v = Math.max(0.0601, 0.5 + Math.log2(base / ms) / 6); break;
      }
      default: return err('Unknown lane ' + lane);
    }
    if (!fin(v)) return err('That value does not work here');
    v = clamp(v, 0, 1);
    return { v, text: laneText(lane, v, c) };
  }
  function tabAt(a, t) {
    const L = a ? a.length : 0;
    if (L < 2) return L ? a[0] : 0.5;
    const f = clamp(t, 0, 1) * (L - 1), i = Math.min(L - 2, Math.floor(f));
    return a[i] + (a[i + 1] - a[i]) * (f - i);
  }
  // What head 1 of a note reads, approximately: no Drift and no Spread, Carry on read as the note's own Start, a
  // Random Start read as the middle of its range, and Scatter jumping to its own seeded spots (it shows the
  // chopped look, not the exact spots the engine picks). Returns N+1 points across the note: pos (fraction of
  // the file, NaN = silent after At the end: Stop), dir (-1, 0, 1), sp (|speed| in x real time) and jump flags.
  // I = a normalized inst; T and P = the note's travel and pitch tables (SLModel.laneTable).
  function simulate(n, I, bpm, secs, T, P, N) {
    const R = I.region, regLen = Math.max(1e-6, R.end - R.start), regSec = Math.max(1e-3, regLen * Math.max(0.01, secs));
    const noteSec = Math.max(1e-6, n.l * 60 / Math.max(1, bpm)), dt = noteSec / N;
    const engine = n.sample.engine || I.engine, tape = engine === 'tape';
    let motion = n.sample.motion || I.motion;
    if (tape && motion === 'freeze') motion = 'forward';
    const path = n.sample.travel === 'path' && !tape;
    const spd = I.speed * Math.pow(2, (I.macros[0] - 0.5) * 4);
    const st = n.sample.start;
    let u = st.mode === 'song' ? frac((n.s * 60 / Math.max(1, bpm)) / regSec) : st.mode === 'random' ? clamp(st.pos + st.range * 0.5, 0, 1) : st.pos;
    const jumpB = Math.max(1e-3, M.rateBeats(n.sample.jump || I.jump)), idh = M.idHash(n.id);
    let lastJ = Math.floor(n.s / jumpB + 1e-9), silent = false;
    const pos = new Float32Array(N + 1), dir = new Int8Array(N + 1), sp = new Float32Array(N + 1), jump = new Uint8Array(N + 1);
    for (let i = 0; i <= N; i++) {
      const t = i / N;
      let vel;
      if (path) {
        const ta = Math.max(0, t - 1 / N), tb = Math.min(1, t + 1 / N);
        u = tabAt(T, t);
        vel = (tabAt(T, tb) - tabAt(T, ta)) / Math.max(1e-9, (tb - ta) * noteSec) * regSec;
      } else {
        let s = n.sample.travel === 'path' ? 1 : M.travelSpeed(Math.fround(tabAt(T, t)));   // Tape plays Path as 1x
        s *= spd;
        if (motion === 'backward') s = -s; else if (motion === 'freeze') s = 0;
        if (tape) s *= Math.pow(2, (n.p + (tabAt(P, t) - 0.5) * 24 + I.pitch - I.root - I.cents / 100) / 12);
        vel = clamp(s, -16, 16);
        if (i > 0) u += vel * dt / regSec;
        if (motion === 'scatter' && i < N) {   // a jump exactly at the note's end is not inside it
          const k = Math.floor((n.s + t * n.l) / jumpB + 1e-9);
          if (k !== lastJ) { lastJ = k; u = M.rand01(M.hash32(idh, k, I.seed | 0, 0x5ca7)); jump[i] = 1; }
        }
      }
      let ue = u, d = vel > 1e-9 ? 1 : vel < -1e-9 ? -1 : 0;
      if (motion === 'bounce' && !path) { const w = u - 2 * Math.floor(u / 2); if (w > 1) { ue = 2 - w; d = -d; } else ue = w; }
      else if (R.atEnd === 'loop' && !path) ue = frac(u);
      else if (u > 1 || u < 0) { if (R.atEnd === 'hold' || path) { ue = clamp(u, 0, 1); d = 0; } else silent = true; }
      pos[i] = silent ? NaN : R.start + ue * regLen;
      dir[i] = silent ? 0 : d;
      sp[i] = silent || d === 0 ? 0 : Math.abs(vel);
    }
    return { pos, dir, sp, jump, N };
  }
  // A coarse peak envelope of a sound (max |mono| per bin, normalised to 1), for the note thumbnails.
  function peakBins(channels) {
    const L = channels[0], R = channels[1] || L, n = L.length;
    const NB = Math.max(1, Math.min(65536, Math.ceil(n / 128))), step = n / NB, bins = new Float32Array(NB);
    let mx = 0;
    for (let b = 0; b < NB; b++) {
      const i1 = Math.min(n, Math.floor((b + 1) * step));
      let pk = 0;
      for (let i = Math.floor(b * step); i < i1; i++) { let v = (L[i] + R[i]) * 0.5; if (v < 0) v = -v; if (v > pk) pk = v; }
      bins[b] = pk; if (pk > mx) mx = pk;
    }
    if (mx > 0) for (let b = 0; b < NB; b++) bins[b] /= mx;
    return bins;
  }
  // Thumbnail column heights (0..1, NaN = silent) from a simulation and the peak bins.
  function thumbAmps(sim, bins, secs, regLen) {
    const N = sim.N, out = new Float32Array(N), NB = bins.length;
    const sliver = Math.min(regLen, 0.06 / Math.max(0.01, secs));
    for (let i = 0; i < N; i++) {
      const a = sim.pos[i], b = sim.pos[i + 1];
      if (!(a === a)) { out[i] = NaN; continue; }
      let lo, hi;
      if (sim.sp[i] < 0.03) lo = hi = a + ((i % 5) / 5) * sliver;              // frozen: a short sliver repeats
      else if (!(b === b) || sim.jump[i + 1] || Math.abs(b - a) > regLen * 0.5) lo = hi = a;
      else { lo = Math.min(a, b); hi = Math.max(a, b); }
      const b0 = clamp(Math.floor(lo * NB), 0, NB - 1), b1 = clamp(Math.floor(hi * NB), b0, NB - 1);
      const stp = Math.max(1, Math.ceil((b1 - b0 + 1) / 24));
      let pk = 0;
      for (let j = b0; j <= b1; j += stp) if (bins[j] > pk) pk = bins[j];
      out[i] = pk;
    }
    return out;
  }
  const PURE = { fmtPos, parsePos, parseBeats, parsePitch, rateId, laneValueFromText, laneText, simulate, peakBins, thumbAmps, tabAt, fmtNum };
  if (typeof window === 'undefined' || typeof document === 'undefined' || !window.SLApp || !M) return PURE;

  // ================================================================================== page
  const A = window.SLApp, el = A.el, $ = id => document.getElementById(id);
  const LANE = M.LANE, SAMPLE_LANES = M.SAMPLE_LANES;
  const laneName = id => (LANE[id] ? LANE[id].name : String(id));
  const nNotes = k => k + (k === 1 ? ' note' : ' notes');
  const ids = () => A.getSelection();
  const sameIds = (a, b) => { if (a.length !== b.length) return false; const s = new Set(a); return b.every(x => s.has(x)); };
  const total = () => A.getClip().bars * 4;
  const fmtTime = s => { s = Math.max(0, s); const m = Math.floor(s / 60), r = s - m * 60; return m + ':' + (r < 10 ? '0' : '') + r.toFixed(2); };
  const GEN_NAMES = { squiggle: 'Squiggle', triangle: 'Triangle', sawup: 'Saw up', sawdown: 'Saw down', square: 'Square', randsteps: 'Random steps', smoothrandom: 'Smooth random' };
  const MOTION_NAMES = { forward: 'Forward', backward: 'Backward', bounce: 'Bounce', mirror: 'Mirror', freeze: 'Freeze', scatter: 'Scatter' };
  const SHAPE_TITLES = {
    up: 'Rises from the start of the note to the end', down: 'Falls from the start to the end', swell: 'Rises to the middle, then falls',
    dip: 'Falls to the middle, then rises', squiggle: 'A few smooth waves', random: 'A few random steps (each click makes a new one)',
    decay: 'Starts high and falls away quickly', reset: 'Back to the flat starting line (a pattern or random on top stays)',
    tapestop: 'Slows down to a stop, like a tape machine switched off', tapestart: 'Starts stopped and speeds up to normal',
    reversehalf: 'Plays forwards, then backwards from the middle', rewind: 'Plays, then rewinds fast and stops',
    playthrough: 'Plays the Region from its start to its end across the note', playthroughback: 'Plays the Region from its end to its start',
    fit: 'Plays through the Region at exactly normal speed (1x) at this tempo',
  };
  const S = {
    lane: 'tone', drawer: null, tool: null,
    pat: { shape: 'squiggle', rate: '1/16T', d0: 0.3, d1: 0.3, warp: 0, lockToBeat: true, perNote: 'same' },
    rnd: { rate: '1/16T', amount: 0.5, smooth: 0, perNote: 'same', lockToBeat: true },
    walk: { field: 'start', from: 0, to: 0.9 },
    chop: { rate: '1/16', step: false },
    hum: { time: 0.03, vel: 0.1, lanes: 0.05, seed: 1 },
    strum: { rate: '1/64' },
    connectSeed: 1, shapeSeed: 1,
  };
  const CHOP_MAX = 1024;
  let live = null;        // { key, base, ids, label, last, dirty }: a slider session that becomes one undo step on release
  let busy = 0;           // > 0 while this file's own setClip runs
  let dragging = null;    // the range input under the mouse
  let ghostOwner = null, hintOwner = null;

  // ---------------------------------------------------------------------------------- small helpers
  function lane() { return A.activeLane() || S.lane; }
  function showLane(id) {
    if (SAMPLE_LANES.includes(id)) { if (A.getMode() !== 8 || A.getSubLane() !== id) A.setSubLane(id); return; }
    const m = M.MODES.find(x => x.lanes[0] === id);
    if (m && A.getMode() !== m.mode) A.setMode(m.mode);
  }
  function travelKind() { const mx = M.getMixed(A.getClip(), ids()); return mx ? mx.travel : 'speed'; }
  function shapeKeys(L) {
    if (L !== 'travel') return M.shapeKeysFor(L);
    const k = travelKind();
    if (k === 'mixed') return M.SHAPE_KEYS.common.concat(M.SHAPE_KEYS.speed, M.SHAPE_KEYS.path);
    return M.shapeKeysFor('travel', k);
  }
  function shapeOpts() { return { seed: S.shapeSeed, bpm: A.getBpm(), sampleSeconds: A.sampleSeconds(), inst: A.getInst() }; }
  function primaryGen(L) { const P = A.primaryNote(); return P && P.gen && P.gen[L] ? P.gen[L] : null; }
  function anyGen(L, kindTest) { return A.selectedNotes().some(n => n.gen && n.gen[L] && (!kindTest || kindTest(n.gen[L]))); }
  const isRandomish = g => g.kind === 'random' || g.shape === 'randsteps' || g.shape === 'smoothrandom';
  function hasOwn(n) { const s = n.sample; return s.engine != null || s.motion != null || s.tape != null || s.rev != null || s.jump != null || s.lock != null; }

  function ghost(fn, owner) {
    let c = null;
    try { c = fn(); } catch (e) { c = null; }
    A.setGhost(c || null);
    ghostOwner = c ? owner || true : null;
  }
  function clearGhost() { if (ghostOwner) { ghostOwner = null; A.setGhost(null); } }
  function ghostOn(node, fn) { node.addEventListener('mouseenter', () => ghost(fn, node)); node.addEventListener('mouseleave', clearGhost); }
  function hintOn(node, text) {
    node.addEventListener('mouseenter', () => { hintOwner = node; A.setHint(typeof text === 'function' ? text() : text); });
    node.addEventListener('mouseleave', clearHint);
  }
  function clearHint() { if (hintOwner) { hintOwner = null; A.setHint(null); } }

  function button(label, o) {
    o = o || {};
    const b = el('button', { type: 'button', class: o.cls || null, title: o.title || null }, label);
    if (o.onClick) b.addEventListener('click', ev => { clearGhost(); try { o.onClick(ev); } catch (e) { console.warn('StrandLock panels:', e); A.toast('That did not work: ' + (e && e.message || e)); } });
    if (o.ghost) ghostOn(b, o.ghost);
    if (o.hint || o.title) hintOn(b, o.hint || o.title);
    return b;
  }
  const lbl = text => el('span', { class: 'lbl' }, text);
  const line = (...kids) => el('div', { class: 'slp-line' }, ...kids);
  const noteEl = (text, warn) => el('div', { class: warn ? 'slp-warn' : 'slp-note' }, text);
  const sep = () => el('span', { class: 'sep' });
  // segmented buttons: items [{v, label, title, ghost}] -> {el, set(v, mixed), get(), buttons}
  function seg(items, value, onPick) {
    const wrap = el('span', { class: 'slp-seg' });
    let cur = value;
    const bs = items.map(it => {
      const b = button(it.label, { title: it.title, ghost: it.ghost, onClick: () => { cur = it.v; paint(false); onPick(it.v); } });
      wrap.appendChild(b);
      return b;
    });
    function paint(mixed) { bs.forEach((b, i) => b.classList.toggle('on', !mixed && items[i].v === cur)); }
    paint(false);
    return { el: wrap, buttons: bs, items, get: () => cur, set(v, mixed) { cur = v; paint(!!mixed); } };
  }
  function toggle(label, on, onFlip, title) {
    const b = button(label, { title, onClick: () => { on = !on; b.classList.toggle('on', on); onFlip(on); } });
    b.classList.toggle('on', !!on);
    return b;
  }
  // slider with a label and a readout; onInput while dragging, onChange once on release
  function slider(label, o) {
    const inp = el('input', { type: 'range', min: o.min, max: o.max, step: o.step || 0.01, title: o.title || null, 'aria-label': label });
    inp.value = o.value;
    const fmt = v => (o.fmt ? o.fmt(v) : String(v));
    const val = el('span', { class: 'slp-val' }, fmt(+inp.value));
    inp.addEventListener('pointerdown', () => { dragging = inp; });
    inp.addEventListener('input', () => { const v = +inp.value; val.textContent = fmt(v); if (o.onInput) o.onInput(v); });
    inp.addEventListener('change', () => { if (dragging === inp) dragging = null; if (o.onChange) o.onChange(+inp.value); });
    const g = el('label', { class: 'slp-g', title: o.title || null }, lbl(label), inp, val);
    if (o.title) hintOn(g, o.title);
    return { el: g, input: inp, val, set(v, text) { if (dragging !== inp) inp.value = v; val.textContent = text != null ? text : fmt(+inp.value); } };
  }
  window.addEventListener('pointerup', () => { setTimeout(() => { dragging = null; }, 0); }, true);
  const RATE_BASES = [['1bar', '1 bar'], ['1/2', '1/2'], ['1/4', '1/4'], ['1/8', '1/8'], ['1/16', '1/16'], ['1/32', '1/32'], ['1/64', '1/64']];
  function splitRate(id) { const s = String(id).slice(-1); return s === 'T' || s === 'D' ? [id.slice(0, -1), s] : [id, '']; }
  // one rate picker (A1): 1 bar .. 1/64, then Straight / Triplet / Dotted
  function ratePicker(value, onPick, o) {
    o = o || {};
    const [b0, s0] = splitRate(value);
    const sel = el('select', { title: o.title || 'How often it repeats: 1 bar down to 1/64' });
    for (const [id, name] of RATE_BASES) if (!o.bases || o.bases.includes(id)) sel.add(new Option(name, id));
    sel.value = b0;
    if (sel.value !== b0 && sel.options.length) sel.value = sel.options[0].value;
    const kind = seg([{ v: '', label: 'Straight' }, { v: 'T', label: 'Triplet' }, { v: 'D', label: 'Dotted' }], s0, v => fire(sel.value + v));
    sel.addEventListener('change', () => fire(sel.value + kind.get()));
    function fire(id) { if (M.isRate(id)) onPick(id); }
    return { el: el('span', { class: 'slp-g' }, lbl(o.label || 'Rate'), sel, kind.el) };
  }

  // ---------------------------------------------------------------------------------- editing
  function act(clip, label, after) {
    if (!clip) return false;
    endLive();
    clearGhost();
    busy++;
    try { A.setClip(clip, label); } finally { busy--; }
    if (after) after();
    syncAll(true);
    return true;
  }
  function endLive() {
    if (live && live.dirty && A.getClip() === live.last) { busy++; try { A.commit(live.label); } finally { busy--; } }
    live = null;
  }
  // One slider session: every move rebuilds the result from the same base clip (shown live, no undo step);
  // the release (final) makes it one undo step. A click with final=true starts a session and is one step too.
  function liveRun(key, label, fn, final) {
    const cur = ids();
    if (!(live && live.key === key && sameIds(live.ids, cur) && A.getClip() === live.last)) {
      endLive();
      live = { key, base: A.getClip(), ids: cur, label, last: null, dirty: false };
    }
    const clip = fn(live.base);
    if (!clip) return;
    live.last = clip; live.label = label;
    clearGhost();
    busy++;
    try {
      if (final) { live.dirty = false; A.setClip(clip, label); }
      else { live.dirty = true; A.setClip(clip, null); }
    } finally { busy--; }
    if (final) syncAll(false); else syncInspector();
  }

  // ---------------------------------------------------------------------------------- generators
  function loadGenValues() {
    const g = primaryGen(lane());
    if (!g) return;
    if (g.kind === 'pattern') Object.assign(S.pat, { shape: g.shape, rate: g.rate, d0: g.d0, d1: g.d1, warp: g.warp, lockToBeat: g.lockToBeat, perNote: g.perNote });
    else Object.assign(S.rnd, { rate: g.rate, amount: g.amount, smooth: g.smooth, perNote: g.perNote, lockToBeat: g.lockToBeat });
  }
  function genOf(kind, over) { return Object.assign({ kind }, kind === 'pattern' ? S.pat : S.rnd, over || {}); }
  // set the generator on every note, keeping each note's own seed and 'Different' key (so random stays put, Y7)
  function putGen(clip, idList, L, kind, over) {
    const g = M.makeGen(kind, genOf(kind, over));
    return M.updateNotes(clip, idList, n => {
      const old = n.gen[L];
      n.gen[L] = M.normalizeGen(Object.assign({}, g, old ? { seed: old.seed, key: old.key, invert: old.kind === g.kind ? old.invert : false } : null));
    });
  }
  const isEditing = (kind, L) => { const g = primaryGen(L); return !!g && g.kind === kind; };
  const genWord = kind => (kind === 'pattern' ? 'Pattern' : 'Random');
  function genChanged(kind, final, what) {
    const L = lane();
    if (isEditing(kind, L)) liveRun('gen:' + kind + ':' + L, genWord(kind) + (what ? ' ' + what : '') + ' (' + laneName(L) + ')', base => putGen(base, ids(), L, kind), final);
    else ghost(() => putGen(A.getClip(), ids(), L, kind), D.drawer);   // a preview until Apply
  }
  function applyGen(kind) {
    const L = lane();
    if (A.activeLane() !== L) showLane(L);
    liveRun('gen:' + kind + ':' + L, genWord(kind) + ' (' + laneName(L) + ')', base => putGen(base, ids(), L, kind), true);
    renderDrawer();
  }
  function bake() {
    const L = lane();
    if (!anyGen(L)) { A.toast('No pattern or random on ' + laneName(L) + ' for the selected notes'); return; }
    if (A.activeLane() !== L) showLane(L);
    act(M.bakeGen(A.getClip(), ids(), L), 'Turn into nodes (' + laneName(L) + ')');
  }
  const depthText = (L, v) => (L === 'pitch' ? plainUnits('±' + fmtNum(v * 2, 2) + ' st') : Math.round(v * 100) + '%');
  const warpText = v => (Math.abs(v) < 0.03 ? 'even' : v > 0 ? 'speeds up' : 'slows down');
  const smoothText = v => (v < 0.02 ? 'steps' : v > 0.98 ? 'smooth' : Math.round(v * 100) + '%');

  // ---------------------------------------------------------------------------------- selection bar
  const bar = $('selBar'), insp = $('inspector'), nsBox = $('noteSettings'), tryEl = $('tryThis');
  const D = {};
  function buildBar() {
    bar.innerHTML = '';
    bar.classList.add('slp-bar');
    const laneSel = el('select', { class: 'slp-lanesel', title: 'The lane that Shapes, Pattern, Random, Turn into nodes, Reverse, Flip, More and Less change' });
    for (const L of M.LANES) laneSel.add(new Option((L.sub ? 'Sample: ' : '') + L.name, L.id));
    laneSel.addEventListener('change', () => { S.lane = laneSel.value; showLane(S.lane); loadGenValues(); syncAll(true); });
    D.laneSel = laneSel;
    const row = el('div', { class: 'slp-row' }, lbl('Lane'), laneSel);
    D.shapes = drawerBtn('shapes', 'Shapes', 'One-click shapes for the lane on every selected note: Ramp, Swell, Tape stop...');
    D.pattern = drawerBtn('pattern', 'Pattern', 'A repeating shape at a musical rate (1 bar to 1/64), riding on top of the drawing');
    D.random = drawerBtn('random', 'Random', 'Random movement that lands on a musical grid, and stays the same until you Re-roll');
    D.bake = button('Turn into nodes', { title: 'Turn the pattern or random on this lane into ordinary nodes you can move by hand',
      ghost: () => (anyGen(lane()) ? M.bakeGen(A.getClip(), ids(), lane()) : null), onClick: bake });
    D.connect = drawerBtn('connect', 'Connect', 'Join the selected notes with slides: Nearest, Furthest or Random');
    D.tools = drawerBtn('tools', 'Tools', 'Walk, Chop, Reverse, Flip, More, Less, Select similar, Humanize, Strum and Legato');
    row.append(D.shapes, D.pattern, D.random, D.bake, sep(), D.connect, D.tools);
    D.drawer = el('div', { class: 'slp-drawer' });
    D.drawer.addEventListener('mouseleave', () => { if (ghostOwner === D.drawer) clearGhost(); });
    bar.append(row, D.drawer);
  }
  const DRAWER_NAMES = { shapes: 'Shapes', pattern: 'Pattern', random: 'Random', connect: 'Connect', tools: 'the Tools' };
  function drawerBtn(k, label, title) {
    const b = button(label + ' ▾', { title, onClick: () => openDrawer(S.drawer === k ? null : k) });
    b.dataset.d = k;
    return b;
  }
  function openDrawer(k) {
    S.drawer = k;
    if (k === 'shapes' || k === 'pattern' || k === 'random') { showLane(lane()); loadGenValues(); }
    renderDrawer();
  }
  function paintBar() {
    const k = ids().length;
    // an open drawer stays open (faded) while nothing is selected, so clearing the selection never moves the roll
    bar.style.display = k || S.drawer ? '' : 'none';
    bar.classList.toggle('slp-idle', !k);
    insp.style.display = k ? '' : 'none';
    const L = lane();
    if (D.laneSel.value !== L) D.laneSel.value = L;
    D.laneSel.style.borderColor = A.laneVar(L);
    D.bake.disabled = !anyGen(L);
    for (const d of ['shapes', 'pattern', 'random', 'connect', 'tools']) {
      D[d].classList.toggle('on', S.drawer === d);
      D[d].firstChild.textContent = D[d].firstChild.textContent.replace(/[▾▴]$/, S.drawer === d ? '▴' : '▾');
    }
  }
  const DRAWERS = {};
  function renderDrawer() {
    const d = D.drawer;
    const has = ids().length > 0;
    // Nothing selected with a drawer open: the drawer keeps its last contents, faded and asleep, at the same height
    // (the roll below must not jump when the selection is cleared); the next selection wakes it up.
    const key = (S.drawer || '') + '|' + lane();
    if (!has && S.drawer && d.dataset.key === key && d.childNodes.length) {
      if (ghostOwner && ghostOwner !== true && d.contains(ghostOwner)) clearGhost();
      if (hintOwner && d.contains(hintOwner)) clearHint();
      bar.classList.add('slp-open');
      paintBar();
      return;
    }
    // a preview owned by a button that is about to be replaced goes; the drawer's own preview stays until the mouse leaves it
    if (ghostOwner && ghostOwner !== true && ghostOwner !== d && d.contains(ghostOwner)) clearGhost();
    if (hintOwner && d.contains(hintOwner)) clearHint();
    d.innerHTML = '';
    d.dataset.key = key;
    bar.classList.toggle('slp-open', !!S.drawer);
    paintBar();
    if (!S.drawer) return;
    if (!has) { d.appendChild(noteEl('Select notes (click one, or drag a box around several) to use ' + (DRAWER_NAMES[S.drawer] || 'this') + '.')); return; }
    try { DRAWERS[S.drawer](d); } catch (e) { console.warn('StrandLock panels: the ' + S.drawer + ' panel failed', e); d.appendChild(noteEl('This panel hit a problem: ' + (e && e.message || e), true)); }
  }
  function syncDrawer() { if (dragging && D.drawer.contains(dragging)) return; renderDrawer(); }

  // Shapes (Y2)
  DRAWERS.shapes = d => {
    const L = lane();
    const scope = seg([
      { v: 'each', label: 'Each note', title: 'The shape fits every selected note' },
      { v: 'across', label: 'Across', title: 'One long shape across the whole selection (each note gets its slice)' },
    ], A.getScope(), v => A.setScope(v));
    const btns = shapeKeys(L).map(k => button(M.SHAPE_NAMES[k], {
      title: SHAPE_TITLES[k],
      ghost: () => M.applyShape(A.getClip(), ids(), L, k, A.getScope(), shapeOpts()),
      onClick: () => {
        if (A.activeLane() !== L) showLane(L);
        act(M.applyShape(A.getClip(), ids(), L, k, A.getScope(), shapeOpts()), M.SHAPE_NAMES[k] + ' (' + laneName(L) + ')');
        if (k === 'random') S.shapeSeed++;
      },
    }));
    d.append(line(lbl('Shape ' + laneName(L)), scope.el), line(...btns));
    if (L === 'travel') d.append(noteEl(travelKind() === 'path' ? 'These notes use Path: the line is where in the Region each note is.'
      : travelKind() === 'mixed' ? 'Some notes use Speed, some Path: each shape changes only the notes of its kind.'
        : 'These notes use Speed: above the middle line plays forwards, below plays backwards.'));
  };

  // Pattern (Y3)
  DRAWERS.pattern = d => {
    const L = lane(), P = S.pat, editing = isEditing('pattern', L), k = ids().length;
    const shapes = seg(M.GEN_SHAPES.map(s => ({ v: s, label: GEN_NAMES[s], ghost: () => putGen(A.getClip(), ids(), L, 'pattern', { shape: s }) })), P.shape,
      v => { P.shape = v; genChanged('pattern', true, 'shape'); renderDrawer(); });
    const rate = ratePicker(P.rate, v => { P.rate = v; genChanged('pattern', true, 'rate'); });
    const dsl = (name, key) => slider(name, { min: 0, max: 1, step: 0.01, value: P[key], fmt: v => depthText(L, v),
      title: 'How far the pattern swings' + (L === 'pitch' ? ' (full = 2 semitones each way)' : ''),
      onInput: v => { P[key] = v; genChanged('pattern', false, 'depth'); }, onChange: v => { P[key] = v; genChanged('pattern', true, 'depth'); } });
    const warp = slider('Speed up / slow down', { min: -0.9, max: 3, step: 0.05, value: P.warp, fmt: warpText,
      title: 'The pattern speeds up or slows down across each note (only with Lock to the beat off)',
      onInput: v => { P.warp = v; genChanged('pattern', false, 'speed-up'); }, onChange: v => { P.warp = v; genChanged('pattern', true, 'speed-up'); } });
    warp.el.classList.toggle('slp-faded', P.lockToBeat);
    const lock = toggle('Lock to the beat', P.lockToBeat, on => { P.lockToBeat = on; genChanged('pattern', true, on ? 'locked to the beat' : 'unlocked'); renderDrawer(); },
      'On: every note\'s pattern stays in step with the bar. Off: it starts again at each note.');
    const rndShape = P.shape === 'randsteps' || P.shape === 'smoothrandom';
    const per = rndShape ? seg([{ v: 'same', label: 'Same for every note' }, { v: 'different', label: 'Different per note' }], P.perNote,
      v => { P.perNote = v; genChanged('pattern', true, 'per note'); }) : null;
    const apply = button('Apply', { cls: editing ? null : 'on', title: 'Put this pattern on the ' + laneName(L) + ' lane of the ' + nNotes(k) + ' (one undo step)',
      ghost: () => putGen(A.getClip(), ids(), L, 'pattern'), onClick: () => applyGen('pattern') });
    const reroll = button('Re-roll', { title: 'New random values (they stay the same until you re-roll again)',
      ghost: () => M.reroll(A.getClip(), ids(), L), onClick: () => act(M.reroll(A.getClip(), ids(), L), 'Re-roll (' + laneName(L) + ')') });
    reroll.disabled = !anyGen(L, isRandomish);
    d.append(
      line(lbl('Pattern on ' + laneName(L)), shapes.el),
      line(rate.el, dsl('Depth at start', 'd0').el, dsl('Depth at end', 'd1').el),
      line(lock, warp.el, per && per.el, apply, rndShape ? reroll : null, ...genButtons(L)),
      noteEl(editing ? 'Moving these changes the pattern on the ' + nNotes(k) + ' straight away: each change is one undo step.'
        : 'Choose the settings (the dashed line shows the result), then Apply.'));
  };
  // Random (Y4)
  DRAWERS.random = d => {
    const L = lane(), R = S.rnd, editing = isEditing('random', L), k = ids().length;
    const rate = ratePicker(R.rate, v => { R.rate = v; genChanged('random', true, 'rate'); }, { title: 'A new random value starts on this grid' });
    const amount = slider('Amount', { min: 0, max: 1, step: 0.01, value: R.amount, fmt: v => depthText(L, v), title: 'How far the random may move',
      onInput: v => { R.amount = v; genChanged('random', false, 'amount'); }, onChange: v => { R.amount = v; genChanged('random', true, 'amount'); } });
    const smooth = slider('Smooth', { min: 0, max: 1, step: 0.01, value: R.smooth, fmt: smoothText, title: 'Left: jumps in steps. Right: moves smoothly from one value to the next.',
      onInput: v => { R.smooth = v; genChanged('random', false, 'smooth'); }, onChange: v => { R.smooth = v; genChanged('random', true, 'smooth'); } });
    const per = seg([
      { v: 'same', label: 'Same for every note', title: 'A chord moves together' },
      { v: 'different', label: 'Different per note', title: 'Every note gets its own random line' },
    ], R.perNote, v => { R.perNote = v; genChanged('random', true, 'per note'); });
    const lock = toggle('Lock to the beat', R.lockToBeat, on => { R.lockToBeat = on; genChanged('random', true, on ? 'locked to the beat' : 'unlocked'); },
      'On: the random steps sit on the song grid. Off: they count from each note\'s start.');
    const apply = button('Apply', { cls: editing ? null : 'on', title: 'Put this random on the ' + laneName(L) + ' lane of the ' + nNotes(k) + ' (one undo step)',
      ghost: () => putGen(A.getClip(), ids(), L, 'random'), onClick: () => applyGen('random') });
    const reroll = button('Re-roll', { title: 'New random values (they stay the same on every loop until you re-roll again)',
      ghost: () => M.reroll(A.getClip(), ids(), L), onClick: () => act(M.reroll(A.getClip(), ids(), L), 'Re-roll (' + laneName(L) + ')') });
    reroll.disabled = !anyGen(L);
    d.append(
      line(lbl('Random on ' + laneName(L)), rate.el),
      line(amount.el, smooth.el, per.el, lock),
      line(apply, reroll, ...genButtons(L)),
      noteEl(editing ? 'Moving these changes the random on the ' + nNotes(k) + ' straight away: each change is one undo step.'
        : 'Choose the settings (the dashed line shows the result), then Apply. Random plays the same on every loop until you Re-roll.'));
  };
  function genButtons(L) {
    const rm = button('Remove', { title: 'Take the pattern or random off the ' + laneName(L) + ' lane (your drawing stays)',
      ghost: () => (anyGen(L) ? M.clearGen(A.getClip(), ids(), L) : null), onClick: () => act(M.clearGen(A.getClip(), ids(), L), 'Remove pattern (' + laneName(L) + ')') });
    const bk = button('Turn into nodes', { title: 'Turn it into ordinary nodes you can move by hand',
      ghost: () => (anyGen(L) ? M.bakeGen(A.getClip(), ids(), L) : null), onClick: bake });
    rm.disabled = bk.disabled = !anyGen(L);
    return [rm, bk];
  }

  // Connect (Y5)
  function linksAmong(clip, idList) { const set = new Set(idList); return clip.notes.filter(n => set.has(n.id) && n.glideTo && set.has(n.glideTo)); }
  function insideLinks(clip, idList) {
    const set = new Set(idList), by = new Map(clip.notes.map(n => [n.id, n]));
    return clip.notes.filter(n => set.has(n.id) && n.glideTo && by.has(n.glideTo) && (m => m.s > n.s + 1e-6 && m.s < n.s + n.l - 0.13)(by.get(n.glideTo)));
  }
  const CONNECT_NAMES = { nearest: 'nearest', furthest: 'furthest', random: 'random' };
  function doConnect(mode, label) {
    const clip = M.connect(A.getClip(), ids(), mode, S.connectSeed);
    const made = linksAmong(clip, ids()).length;
    act(clip, label || 'Connect: ' + CONNECT_NAMES[mode]);
    A.toast(made ? made + (made === 1 ? ' slide' : ' slides') + ' among the selected notes'
      : 'No slides made: a note slides into one that starts as it ends (up to 2 octaves away)', 3200);
  }
  DRAWERS.connect = d => {
    const idl = ids(), clip = A.getClip();
    const mkb = (mode, label, title) => button(label, { title, ghost: () => M.connect(A.getClip(), ids(), mode, S.connectSeed), onClick: () => doConnect(mode) });
    const reroll = button('Re-roll', { title: 'A new random set of slides',
      ghost: () => M.connect(A.getClip(), ids(), 'random', S.connectSeed + 1), onClick: () => { S.connectSeed++; doConnect('random', 'Connect: random (re-rolled)'); } });
    const links = linksAmong(clip, idl).length;
    const dis = button('Disconnect', { title: 'Remove the slides between the selected notes',
      ghost: () => (links ? M.disconnect(A.getClip(), ids()) : null), onClick: () => act(M.disconnect(A.getClip(), ids()), 'Disconnect') });
    dis.disabled = !links;
    const row = line(lbl('Connect'),
      mkb('nearest', 'Nearest', 'Each note slides into the closest pitch among the next notes'),
      mkb('furthest', 'Furthest', 'Each note slides into the widest jump among the next notes'),
      mkb('random', 'Random', 'Each note slides into any of the next notes'), reroll, dis);
    d.append(row);
    if (idl.length < 2) d.append(noteEl('Select two or more notes: Connect joins each note to one that starts as it ends.'));
    else d.append(noteEl(links ? links + (links === 1 ? ' slide' : ' slides') + ' among the selected notes. Each one can still be curved with its ◆ handles in Build or Pitch.'
      : 'Slides join each note to a note that starts as it ends, up to 2 octaves away, and play on one voice.'));
    const bad = insideLinks(clip, idl).length;
    if (bad) d.append(noteEl('⚠ ' + bad + (bad === 1 ? ' slide goes' : ' slides go') + ' into a note that starts well inside the note before it: that note is cut short where the next one starts (red mark on the roll). Move the second note later, or Disconnect.', true));
  };

  // Tools (T1 to T5)
  const WALK_DEFAULTS = { start: [0, 0.9], vel: [0.4, 1], pitch: [0.5, 1], amp: [0.35, 0.9], vib: [0, 0.5], tone: [0.35, 1], formant: [0.3, 0.7],
    hollow: [0, 0.6], heads: [0.5, 0.79], grain: [0.3, 0.7], refresh: [0.3, 0.7] };
  function walkDefaults(f) { if (f === 'travel') return [M.travelSpeedToV(1), M.travelSpeedToV(2)]; return WALK_DEFAULTS[f] || [0.2, 0.8]; }
  const walkName = f => (f === 'start' ? 'Start' : f === 'vel' ? 'Velocity' : laneName(f));
  function walkText(f, v) {
    if (f === 'start') { const R = A.getInst().region, s = A.sampleSeconds(); return Math.round(v * 100) + '% (' + fmtTime((R.start + v * (R.end - R.start)) * s) + ')'; }
    if (f === 'vel') return String(Math.max(1, Math.round(v * 127)));
    return laneText(f, v, A.formatCtx(A.primaryNote()));
  }
  const walkClip = base => M.walk(base, ids(), S.walk.field, S.walk.from, S.walk.to);
  function walkRun(final, click) {
    if (click || (live && live.key === 'walk')) liveRun('walk', 'Walk ' + walkName(S.walk.field), walkClip, final);
    else ghost(() => walkClip(A.getClip()), D.drawer);
  }
  const humClip = base => M.humanize(base, ids(), { time: S.hum.time, vel: S.hum.vel, lanes: S.hum.lanes }, S.hum.seed);
  function humRun(final, click, label) {
    if (click || (live && live.key === 'hum')) liveRun('hum', label || 'Humanize', humClip, final);
    else ghost(() => humClip(A.getClip()), D.drawer);
  }
  function doChop() {
    const before = A.getClip(), sel = ids(), C = S.chop;
    const r = M.chop(before, sel, C.rate, C.step ? true : 0, CHOP_MAX);
    if (r.refused) { A.toast('That would make ' + r.count + ' notes; the limit is ' + CHOP_MAX + '. Pick a slower rate or fewer notes.', 4000); return; }
    const old = new Set(before.notes.map(n => n.id));
    act(r.clip, 'Chop ' + M.rateLabel(C.rate), () => {
      const made = r.clip.notes.filter(n => !old.has(n.id)).map(n => n.id);
      A.setSelection(sel.concat(made), A.getPrimary());
      A.toast('Chopped into ' + nNotes(sel.length + made.length));
    });
  }
  function openSimilar(ev) {
    const r = ev.currentTarget.getBoundingClientRect(), P = A.primaryNote(), pid = P ? P.id : null;
    const pick = kind => {
      const res = M.selectSimilar(A.getClip(), pid, kind, ids());
      A.setSelection(res, res.includes(pid) ? pid : res[0] || null);
      A.toast(res.length ? nNotes(res.length) + ' selected' : 'No notes match');
    };
    A.openMenu(r.left, r.bottom + 4, [
      { header: P ? 'Like ' + M.noteName(P.p) + ' at ' + fmtPos(P.s) : 'Select' },
      { label: 'Same pitch' + (P ? ' (' + M.noteName(P.p) + ')' : ''), disabled: !P, click: () => pick('pitch') },
      { label: 'Same length', disabled: !P, click: () => pick('length') },
      { label: 'Every other note', click: () => pick('everyOther') },
      { label: 'Everything in this bar', disabled: !P, click: () => pick('bar') },
      { label: 'Flip the selection', title: 'Select every note that is not selected now', click: () => pick('invert') },
    ]);
  }
  function laneTool(name, title, make) {
    return button(name, { title, ghost: () => make(A.getClip(), lane()), onClick: () => {
      const L = lane();
      if (A.activeLane() !== L) showLane(L);
      act(make(A.getClip(), L), name + ' (' + laneName(L) + ')');
    } });
  }
  function toolBtn(k, label, title) {
    const b = button(label, { title, onClick: () => { S.tool = S.tool === k ? null : k; renderDrawer(); } });
    b.classList.toggle('on', S.tool === k);
    return b;
  }
  DRAWERS.tools = d => {
    const L = lane(), Ln = laneName(L);
    d.append(line(
      toolBtn('walk', 'Walk…', 'Spread a value from the first selected note to the last, e.g. each note reads further into the sound'),
      toolBtn('chop', 'Chop…', 'Split each selected note into repeats at a rate'),
      sep(), lbl(Ln + ':'),
      laneTool('Reverse', 'Play the ' + Ln + ' line backwards in time', (c, l) => M.reverseLane(c, ids(), l)),
      laneTool('Flip', 'Turn the ' + Ln + ' line upside down' + (L === 'travel' ? ' (on Speed, forwards becomes backwards)' : ''), (c, l) => M.flipLane(c, ids(), l)),
      laneTool('More', 'Make the ' + Ln + ' line move twice as far from its middle' + (L === 'travel' ? ' (never changes direction)' : ''), (c, l) => M.scaleLane(c, ids(), l, 2)),
      laneTool('Less', 'Make the ' + Ln + ' line move half as far from its middle' + (L === 'travel' ? ' (never stops a note)' : ''), (c, l) => M.scaleLane(c, ids(), l, 0.5)),
      sep(),
      button('Select similar ▾', { title: 'Select notes like the clicked one: same pitch, same length, every other note, this bar, or flip the selection', onClick: openSimilar }),
      toolBtn('humanize', 'Humanize…', 'Nudge timing, velocity, Volume and Tone by small amounts (they stay the same until you Re-roll)'),
      toolBtn('strum', 'Strum…', 'Spread the starts of each chord'),
      button('Legato', { title: 'Stretch each note to reach the next one (ready for Connect)', ghost: () => M.legato(A.getClip(), ids()),
        onClick: () => {
          const before = A.getClip(), after = M.legato(before, ids()), by = new Map(before.notes.map(n => [n.id, n]));
          if (!after.notes.some(n => { const o = by.get(n.id); return !o || Math.abs(o.l - n.l) > 1e-9; })) {
            // a note stretches to the next selected note (a chord to the next chord): one chord alone has nowhere to go
            const st = ids().map(id => by.get(id)).filter(Boolean).map(n => n.s);
            const oneChord = !st.length || Math.max(...st) - Math.min(...st) <= 0.05;
            A.toast(oneChord ? 'No later selected note to stretch to: select the notes that follow as well' : 'Already touching: nothing to stretch', 3500);
            return;
          }
          act(after, 'Legato'); A.toast('Each note now reaches the next one: try Connect ▸ Nearest');
        } })));
    if (S.tool && TOOL_PANELS[S.tool]) d.append(TOOL_PANELS[S.tool]());
  };
  const TOOL_PANELS = {
    walk() {
      const W = S.walk, f = W.field;
      const fsel = el('select', { title: 'What changes from the first note to the last' });
      fsel.add(new Option('Start in the sound', 'start')); fsel.add(new Option('Velocity', 'vel'));
      for (const L of M.LANES) fsel.add(new Option((L.sub ? 'Sample: ' : '') + L.name, L.id));
      fsel.value = f;
      fsel.addEventListener('change', () => { W.field = fsel.value; [W.from, W.to] = walkDefaults(W.field); if (live && live.key === 'walk') endLive(); renderDrawer(); });
      const fmt = v => walkText(W.field, v);
      const a = slider('First note', { min: 0, max: 1, step: 0.005, value: W.from, fmt, onInput: v => { W.from = v; walkRun(false); }, onChange: v => { W.from = v; walkRun(true); } });
      const b = slider('Last note', { min: 0, max: 1, step: 0.005, value: W.to, fmt, onInput: v => { W.to = v; walkRun(false); }, onChange: v => { W.to = v; walkRun(true); } });
      const go = button('Walk', { cls: 'on', title: 'Spread it across the selected notes (one undo step); then the sliders change it live', ghost: () => walkClip(A.getClip()), onClick: () => walkRun(true, true) });
      return el('div', { class: 'slp-sub' }, lbl('Walk'), fsel, a.el, b.el, go,
        noteEl(f === 'start' ? 'Each note starts a little further into the sound, so a run of notes walks through it.'
          : f === 'vel' ? 'Velocity goes from the first value to the last.' : 'Each note\'s ' + laneName(f) + ' line becomes flat at its step.'));
    },
    chop() {
      const C = S.chop, idl = ids(), clip = A.getClip();
      const rate = ratePicker(C.rate, v => { C.rate = v; renderDrawer(); }, { bases: ['1/4', '1/8', '1/16', '1/32', '1/64'], title: 'The length of each repeat' });
      const step = seg([
        { v: false, label: 'Same spot', title: 'Every repeat starts where the note started' },
        { v: true, label: 'Step through the part', title: 'Each repeat starts a little further into the Region' },
      ], C.step, v => { C.step = v; });
      const r = M.chop(clip, idl, C.rate, C.step ? true : 0, CHOP_MAX);
      const go = button('Chop', { cls: 'on', title: 'Split the selected notes (one undo step)', ghost: () => { const q = M.chop(A.getClip(), ids(), C.rate, C.step ? true : 0, CHOP_MAX); return q.refused ? null : q.clip; }, onClick: doChop });
      go.disabled = r.refused;
      const pieces = r.count - (clip.notes.length - idl.length);
      return el('div', { class: 'slp-sub' }, lbl('Chop'), rate.el, step.el, go,
        r.refused ? noteEl('⚠ That would make ' + r.count + ' notes; the limit is ' + CHOP_MAX + '. Pick a slower rate or fewer notes.', true)
          : noteEl('The ' + nNotes(idl.length) + ' become ' + pieces + ' pieces. Each piece after the first starts its sound again.'));
    },
    humanize() {
      const H = S.hum, bpm = A.getBpm();
      const time = slider('Timing', { min: 0, max: 0.25, step: 0.005, value: H.time, fmt: v => '±' + Math.round(v * 60000 / bpm) + ' ms',
        onInput: v => { H.time = v; humRun(false); }, onChange: v => { H.time = v; humRun(true); } });
      const vel = slider('Velocity', { min: 0, max: 0.4, step: 0.005, value: H.vel, fmt: v => '±' + Math.round(v * 127),
        onInput: v => { H.vel = v; humRun(false); }, onChange: v => { H.vel = v; humRun(true); } });
      const lanes = slider('Volume and Tone', { min: 0, max: 0.25, step: 0.005, value: H.lanes, fmt: v => '±' + Math.round(v * 100) + '%',
        onInput: v => { H.lanes = v; humRun(false); }, onChange: v => { H.lanes = v; humRun(true); } });
      const go = button('Humanize', { cls: 'on', title: 'Nudge the selected notes (one undo step); then the sliders change it live', ghost: () => humClip(A.getClip()), onClick: () => humRun(true, true) });
      const rr = button('Re-roll', { title: 'A different set of nudges (one undo step)', onClick: () => { H.seed++; humRun(true, true, 'Humanize: re-roll'); } });
      return el('div', { class: 'slp-sub' }, lbl('Humanize'), time.el, vel.el, lanes.el, go, rr,
        noteEl('The nudges stay the same every time you play, until you Re-roll.'));
    },
    strum() {
      const rate = ratePicker(S.strum.rate, v => { S.strum.rate = v; }, { bases: ['1/8', '1/16', '1/32', '1/64'], title: 'The gap between the notes of a chord', label: 'Gap' });
      const mk = (dir, label) => button(label, { title: label + ': each note of a chord starts a little after the one before (every note keeps its end)',
        ghost: () => M.strum(A.getClip(), ids(), S.strum.rate, dir), onClick: () => act(M.strum(A.getClip(), ids(), S.strum.rate, dir), label) });
      return el('div', { class: 'slp-sub' }, lbl('Strum'), rate.el, mk('up', 'Strum up'), mk('down', 'Strum down'),
        noteEl('Up starts from the lowest note, Down from the highest. Notes that start together count as a chord.'));
    },
  };

  // ---------------------------------------------------------------------------------- ▾ menu on the lane buttons
  function openLaneMenu(L, cx, cy) {
    showLane(L);
    const idl = ids();
    if (!idl.length) { A.toast('Select notes first: click one, or drag a box on empty space'); return; }
    const name = laneName(L), opts = shapeOpts();
    const items = [{ header: name + ' on ' + nNotes(idl.length), color: A.laneColor(L) }, { row: [
      { label: 'Each note', on: A.getScope() === 'each', keep: true, title: 'The shape fits every note', click: () => { A.setScope('each'); openLaneMenu(L, cx, cy); } },
      { label: 'Across', on: A.getScope() === 'across', keep: true, title: 'One shape across the whole selection', click: () => { A.setScope('across'); openLaneMenu(L, cx, cy); } },
    ] }];
    for (const k of shapeKeys(L)) {
      items.push({ label: M.SHAPE_NAMES[k], title: SHAPE_TITLES[k], hover: () => A.setGhost(M.applyShape(A.getClip(), idl, L, k, A.getScope(), opts)),
        click: () => { act(M.applyShape(A.getClip(), idl, L, k, A.getScope(), opts), M.SHAPE_NAMES[k] + ' (' + name + ')'); if (k === 'random') S.shapeSeed++; } });
    }
    const has = anyGen(L);
    items.push({ sep: true });
    items.push({ label: 'Pattern: ' + GEN_NAMES[S.pat.shape].toLowerCase() + ' ' + M.rateLabel(S.pat.rate), hover: () => A.setGhost(putGen(A.getClip(), idl, L, 'pattern')),
      click: () => act(putGen(A.getClip(), idl, L, 'pattern'), 'Pattern (' + name + ')') });
    items.push({ label: 'Pattern settings…', title: 'Rate, shape, depth, Lock to the beat', click: () => { S.lane = L; openDrawer('pattern'); } });
    items.push({ label: 'Random ' + M.rateLabel(S.rnd.rate), hover: () => A.setGhost(putGen(A.getClip(), idl, L, 'random')),
      click: () => act(putGen(A.getClip(), idl, L, 'random'), 'Random (' + name + ')') });
    items.push({ label: 'Random settings…', title: 'Rate, amount, smooth, same or different per note', click: () => { S.lane = L; openDrawer('random'); } });
    items.push({ label: 'Re-roll the random', disabled: !has, click: () => act(M.reroll(A.getClip(), idl, L), 'Re-roll (' + name + ')') });
    items.push({ label: 'Remove pattern / random', disabled: !has, hover: () => A.setGhost(M.clearGen(A.getClip(), idl, L)), click: () => act(M.clearGen(A.getClip(), idl, L), 'Remove pattern (' + name + ')') });
    items.push({ label: 'Turn into nodes', disabled: !has, hover: () => A.setGhost(M.bakeGen(A.getClip(), idl, L)), click: () => act(M.bakeGen(A.getClip(), idl, L), 'Turn into nodes (' + name + ')') });
    A.openMenu(cx, cy, items);
  }
  function hookLaneArrows() {
    const mb = $('modeBar');
    if (!mb) return;
    // runs before the arrow's own handler (capture), so this fuller menu replaces the basic one
    mb.addEventListener('click', e => {
      const ar = e.target && e.target.closest ? e.target.closest('.lanewrap .arrow') : null;
      if (!ar) return;
      const b = ar.previousElementSibling, m = b ? M.MODES.find(x => 'mode' + x.mode === b.id) : null;
      if (!m) return;
      e.stopPropagation(); e.preventDefault();
      if (e.detail > 0) ar.blur();
      const r = ar.getBoundingClientRect();
      try { openLaneMenu(m.mode === 8 ? A.getSubLane() : m.lanes[0], r.left, r.bottom + 4); } catch (err) { console.warn('StrandLock panels: lane menu failed', err); }
    }, true);
  }

  // ---------------------------------------------------------------------------------- inspector (T6)
  const IN = {};
  function field(label, title, onCommit, unit) {
    const inp = el('input', { type: 'text', class: 'slp-in', title, spellcheck: 'false', autocomplete: 'off', 'aria-label': label });
    // select() focuses the field again in Chrome, so only select while it still has the focus (a quick Enter may have left it)
    inp.addEventListener('focus', () => { inp.dataset.orig = inp.value; setTimeout(() => { if (document.activeElement === inp) try { inp.select(); } catch (e) { /* ok */ } }, 0); });
    inp.addEventListener('keydown', e => {
      if (e.key === 'Enter') { e.preventDefault(); inp.blur(); }
      else if (e.key === 'Escape') { e.preventDefault(); inp.value = inp.dataset.orig || ''; inp.blur(); }
      e.stopPropagation();
    });
    inp.addEventListener('change', () => {
      if (inp.value.trim() === (inp.dataset.orig || '').trim()) return;
      try { onCommit(inp.value); } catch (e) { console.warn('StrandLock panels:', e); A.toast('That value did not work'); }
      inp.dataset.orig = '';
      setTimeout(syncInspector, 0);   // after the blur, so the field shows the value as it plays
    });
    const wrap = el('label', { class: 'slp-field', title }, lbl(label), inp, unit ? el('span', { class: 'slp-unit' }, unit) : null);
    return { inp, wrap };
  }
  function buildInspector() {
    insp.innerHTML = '';
    insp.classList.add('slp-insp');
    IN.start = field('Start', 'Where the note starts, Cubase style bar.beat.sixteenth (e.g. 2.1.3). With several notes selected they all move by the same amount.', commitStart);
    IN.len = field('Length', 'In beats (0.5) or as a note value (1/8, 1/16T, 1 bar). With several notes selected they all get this length.', commitLen, 'beats');
    IN.pitch = field('Pitch', 'A note name such as C3 or F#2 (C3 = MIDI 60, as in Cubase) or a MIDI number. Several selected notes all move by the same amount.', commitPitch);
    IN.vel = field('Velocity', '1 to 127. With several notes selected they all get this velocity.', commitVel);
    IN.nodeLbl = lbl('Node');
    IN.ntime = field('at', 'How far into the note the node sits, in beats (or a note value such as 1/16)', commitNodeTime, 'beats in');
    IN.nval = field('value', 'Type the value in real units, e.g. +3 st, -6 dB, 2.4 kHz, 0.5x backwards, 3 heads', commitNodeVal);
    IN.node = el('span', { class: 'slp-g slp-node' }, IN.nodeLbl, IN.ntime.wrap, IN.nval.wrap);
    insp.append(el('span', { class: 'slp-g' }, IN.start.wrap, IN.len.wrap, IN.pitch.wrap, IN.vel.wrap), IN.node);
  }
  function setVal(f, v) { if (document.activeElement !== f.inp && f.inp.value !== v) f.inp.value = v; }
  // the nodes picked with the Nodes switch, on selected notes of the shown lane: [{n, ks:[k...]}], count
  function pickedNodes() {
    const L = A.activeLane(), map = A.getNodeSelection ? A.getNodeSelection() : null, out = [];
    let count = 0;
    if (!L || !map || !map.size) return { L, list: out, count };
    const sel = A.selectionSet();
    for (const [id, ks] of map) {
      const n = sel.has(id) ? A.noteById(id) : null;
      if (!n || !n.c[L]) continue;
      const v = [...ks].filter(k => k >= 0 && k < n.c[L].length);
      if (v.length) { out.push({ n, ks: v }); count += v.length; }
    }
    return { L, list: out, count };
  }
  function activeNodeCtx() {
    const g = pickedNodes();
    if (g.count >= 2) {   // several picked nodes: one value field types them all
      const nd = A.getActiveNode(), hit = nd && g.list.find(e => e.n.id === nd.id && e.ks.includes(nd.k));
      const ref = hit ? { n: hit.n, k: nd.k } : { n: g.list[0].n, k: g.list[0].ks[0] };
      return { n: ref.n, L: g.L, k: ref.k, nodes: ref.n.c[g.L], group: g };
    }
    const nd = A.getActiveNode(), L = A.activeLane();
    if (!nd || !L || nd.lane !== L || !A.selectionSet().has(nd.id)) return null;
    const n = A.noteById(nd.id);
    if (!n || !n.c[L] || nd.k < 0 || nd.k >= n.c[L].length) return null;
    return { n, L, k: nd.k, nodes: n.c[L] };
  }
  function syncInspector() {
    const P = A.primaryNote();
    if (!P || !IN.start) return;
    setVal(IN.start, fmtPos(P.s));
    setVal(IN.len, fmtNum(P.l, 4));
    setVal(IN.pitch, M.noteName(P.p));
    setVal(IN.vel, String(Math.max(1, Math.round(P.vel * 127))));
    const c = activeNodeCtx();
    IN.node.style.display = c ? '' : 'none';
    if (!c) return;
    const q = c.nodes[c.k], last = c.nodes.length - 1, ends = c.k === 0 || c.k === last;
    if (c.group) {
      IN.nodeLbl.textContent = c.group.count + ' nodes (' + laneName(c.L) + ')';
      IN.ntime.wrap.style.display = 'none';
      IN.nval.inp.title = 'Type one value for all ' + c.group.count + ' picked nodes, in real units (e.g. +3 st, -6 dB, 2.4 kHz, 0.5x backwards, 3 heads)';
      setVal(IN.nval, laneText(c.L, q.v, A.formatCtx(c.n)));
      fitVal(IN.nval);
      return;
    }
    IN.ntime.wrap.style.display = '';
    IN.nval.inp.title = 'Type the value in real units, e.g. +3 st, -6 dB, 2.4 kHz, 0.5x backwards, 3 heads';
    IN.nodeLbl.textContent = 'Node (' + laneName(c.L) + ')';
    IN.ntime.inp.disabled = ends;
    IN.ntime.inp.title = ends ? 'The first and last nodes stay at the start and end of the note' : 'How far into the note the node sits, in beats (or a note value such as 1/16)';
    setVal(IN.ntime, c.k === 0 ? 'start' : c.k === last ? 'end' : fmtNum(q.t * c.n.l, 3));
    setVal(IN.nval, laneText(c.L, q.v, A.formatCtx(c.n)));
    fitVal(IN.nval);
  }
  // the value box grows with its text ('25% of the Region', '0.5x backwards'), so the whole value can be read
  function fitVal(f) { const w = Math.min(190, Math.max(66, 14 + 6.6 * f.inp.value.length)); f.inp.style.width = Math.round(w) + 'px'; }
  function commitStart(text) {
    const b = parsePos(text), P = A.primaryNote();
    if (b == null || !P) { A.toast('Type a position like 2.1.3 (bar.beat.sixteenth)'); return; }
    const d = b - P.s, T = total();
    act(M.updateNotes(A.getClip(), ids(), n => { n.s = clamp(n.s + d, 0, Math.max(0, T - n.l)); }), 'Type start');
  }
  function commitLen(text) {
    const l = parseBeats(text);
    if (l == null || !(l > 0)) { A.toast('Type a length in beats (0.5) or a note value (1/8)'); return; }
    const T = total();
    act(M.updateNotes(A.getClip(), ids(), n => { n.l = clamp(l, M.MIN_LEN, Math.max(M.MIN_LEN, T - n.s)); }), 'Type length');
  }
  function commitPitch(text) {
    const p = parsePitch(text), P = A.primaryNote();
    if (p == null || !P) { A.toast('Type a note such as C3 or F#2, or a MIDI number'); return; }
    const G = A.geometry(), d = p - P.p;
    act(M.updateNotes(A.getClip(), ids(), n => { n.p = clamp(n.p + d, G.MINP, G.MAXP); }), 'Type pitch');
  }
  function commitVel(text) {
    const x = parseFloat(clean(text));
    if (!fin(x)) { A.toast('Type a velocity from 1 to 127'); return; }
    act(M.updateNotes(A.getClip(), ids(), { vel: clamp(Math.round(x), 1, 127) / 127 }), 'Type velocity');
  }
  function commitNodeTime(text) {
    const c = activeNodeCtx();
    if (!c || c.k === 0 || c.k === c.nodes.length - 1) return;
    const b = parseBeats(text);
    if (b == null) { A.toast('Type how many beats into the note, e.g. 0.5 or 1/16'); return; }
    const nd = c.nodes.map(q => Object.assign({}, q));
    nd[c.k].t = clamp(b / c.n.l, nd[c.k - 1].t + 0.004, nd[c.k + 1].t - 0.004);
    act(M.setLaneNodes(A.getClip(), c.n.id, c.L, nd), 'Type node time');
  }
  function commitNodeVal(text) {
    const c = activeNodeCtx();
    if (!c) return;
    if (c.group) {   // one value for every picked node (each note reads it with its own Travel kind)
      const map = {};
      for (const e of c.group.list) {
        const r = laneValueFromText(c.L, text, A.formatCtx(e.n));
        if (r.error) { A.toast(r.error, 3500); return; }
        const nd = e.n.c[c.L].map(q => Object.assign({}, q));
        for (const k of e.ks) nd[k].v = r.v;
        map[e.n.id] = nd;
      }
      act(M.setLaneNodesMany(A.getClip(), c.L, map), 'Type value for ' + c.group.count + ' nodes');
      return;
    }
    const r = laneValueFromText(c.L, text, A.formatCtx(c.n));
    if (r.error) { A.toast(r.error, 3500); return; }
    const nd = c.nodes.map(q => Object.assign({}, q));
    nd[c.k].v = r.v;
    act(M.setLaneNodes(A.getClip(), c.n.id, c.L, nd), 'Type node value');
  }

  // ---------------------------------------------------------------------------------- Note settings (S7)
  const NS = {};
  const triIn = v => (v === 'on' ? true : v === 'off' ? false : null);
  const triOut = v => (v === 'mixed' ? 'mixed' : v == null ? 'inst' : v ? 'on' : 'off');
  const onOff = b => (b ? 'on' : 'off');
  function setNS(patch, label) { act(M.setNoteSample(A.getClip(), ids(), patch), label); }
  function nsGroup(name, ...kids) {
    const tag = el('span', { class: 'slp-mixed', hidden: true, title: 'The selected notes differ here' }, 'mixed');
    const g = el('div', { class: 'slp-nsg' }, lbl(name), ...kids, tag);
    g._tag = tag;
    return g;
  }
  function setMotion(v) {
    let clip = M.setNoteSample(A.getClip(), ids(), { motion: v === 'inst' ? null : v });
    let extra = 0;
    if (v === 'mirror') {   // Mirror with 1 head gets 2 heads in the same click (S2)
      const I = A.getInst(), map = {};
      for (const n of A.selectedNotes()) {
        const mx = Math.max(...n.c.heads.map(q => q.v));
        if (!n.gen.heads && I.heads + (mx - 0.5) * 14 < 1.5) { map[n.id] = M.flat(clamp(0.5 + (2 - I.heads) / 14, 0, 1)); extra++; }
      }
      if (extra) clip = M.setLaneNodesMany(clip, 'heads', map);
    }
    act(clip, 'Note motion: ' + (v === 'inst' ? 'follow' : MOTION_NAMES[v]));
    if (extra) A.toast('Mirror needs 2 heads: Heads set to 2 on ' + nNotes(extra));
  }
  // The Travel line means a speed in one kind and a place in the Region in the other, so a flat line that switches
  // kind starts as the new kind's playing line: Path = Fit (1x through the Region), Speed = 1x forwards. A line
  // the user shaped stays as it is (the toast says how it now reads). One undo step with the switch.
  function setTravelKind(v) {
    const flatLine = n => !(n.gen && n.gen.travel) && n.c.travel.every(q => Math.abs(q.v - n.c.travel[0].v) < 1e-6);
    const sel = A.selectedNotes(), change = sel.filter(n => n.sample.travel !== v), reset = change.filter(flatLine).map(n => n.id);
    let clip = M.setNoteSample(A.getClip(), ids(), { travel: v });
    if (reset.length) clip = M.applyShape(clip, reset, 'travel', v === 'path' ? 'fit' : 'reset', 'each', shapeOpts());
    act(clip, 'Travel: ' + (v === 'path' ? 'Path' : 'Speed'));
    if (!change.length) return;
    const kept = change.length - reset.length;
    A.toast(v === 'path'
      ? (reset.length ? 'Path: the Travel line now plays through the Region at 1x (Fit). Flat = hold still, a step = jump.' : '')
        + (kept ? (reset.length ? ' ' : '') + 'Path: the line you drew now says where in the Region ' + (kept === 1 ? 'the note is' : 'the notes are') + ' (top = the end).' : '')
      : (reset.length ? 'Speed: the Travel line is back to 1x forwards.' : '')
        + (kept ? (reset.length ? ' ' : '') + 'Speed: above the middle line plays forwards, below plays backwards.' : ''), 4500);
  }
  function buildNoteSettings() {
    nsBox.innerHTML = '';
    nsBox.classList.add('slp-ns');
    NS.empty = noteEl('Select notes on the roll to give them their own settings. Notes without their own settings follow the Sample tab.');
    NS.headTxt = el('span');
    NS.clear = button('Follow the Sample tab for all', { title: 'Clear the own settings of the selected notes (engine, lock, motion, tape, each grain backwards, jump)',
      onClick: () => setNS({ engine: null, lock: null, motion: null, tape: null, rev: null, jump: null }, 'Note settings: follow the Sample tab') });
    NS.head = el('div', { class: 'slp-nshead' }, NS.headTxt, NS.clear);
    NS.engine = seg([{ v: 'inst', label: 'Follow' }, { v: 'tape', label: 'Tape', title: 'Speed and pitch move together, like tape' },
      { v: 'voice', label: 'Voice', title: 'Slowed, frozen or backwards, it stays in tune and keeps its character' },
      { v: 'cloud', label: 'Cloud', title: 'Clouds of grains that work on anything' }],
    'inst', v => setNS({ engine: v === 'inst' ? null : v }, 'Note engine: ' + (v === 'inst' ? 'follow' : M.ENGINE_NAMES[v])));
    NS.lock = seg([{ v: 'inst', label: 'Follow' }, { v: 'on', label: 'On', title: 'Every grab loops at exactly the note you play' }, { v: 'off', label: 'Off' }],
      'inst', v => setNS({ lock: triIn(v) }, 'Note lock: ' + (v === 'inst' ? 'follow' : v)));
    NS.motion = seg([{ v: 'inst', label: 'Follow' }].concat(M.MOTIONS.map(m => ({ v: m, label: MOTION_NAMES[m] }))), 'inst', setMotion);
    NS.travel = seg([{ v: 'speed', label: 'Speed', title: 'Above the middle line plays forwards, below plays backwards, on the line stops' },
      { v: 'path', label: 'Path', title: 'The line is where in the Region the note is: flat freezes, rising plays forwards, a step jumps' }],
    'speed', setTravelKind);
    const START_NAMES = { region: 'a spot in the part', carry: 'carry on', song: 'with the song', random: 'random' };
    NS.start = seg([{ v: 'region', label: 'A spot in the part', title: 'Starts at the spot below (or click the waveform)' },
      { v: 'carry', label: 'Carry on', title: 'Carries on from where the note before it was' },
      { v: 'song', label: 'With the song', title: 'The sound runs under the song like a tape: the note plays what is there at its time' },
      { v: 'random', label: 'Random', title: 'A random spot within the range below. It stays the same every time until you Re-roll.' }],
    'region', v => setNS({ start: { mode: v } }, 'Start: ' + START_NAMES[v]));
    const posFmt = v => { const R = A.getInst().region, s = A.sampleSeconds(); return Math.round(v * 100) + '% (' + fmtTime((R.start + v * (R.end - R.start)) * s) + ')'; };
    NS.pos = slider('Spot', { min: 0, max: 1, step: 0.001, value: 0, fmt: posFmt, title: 'Where in the Region the notes start',
      onInput: v => liveRun('ns:pos', 'Start spot', base => M.setNoteSample(base, ids(), { start: { pos: v } }), false),
      onChange: v => liveRun('ns:pos', 'Start spot', base => M.setNoteSample(base, ids(), { start: { pos: v } }), true) });
    NS.range = slider('Range', { min: 0, max: 1, step: 0.005, value: 0.25, fmt: v => Math.round(v * 100) + '% of the Region', title: 'How far past the spot a random start may land',
      onInput: v => liveRun('ns:range', 'Random range', base => M.setNoteSample(base, ids(), { start: { range: v } }), false),
      onChange: v => liveRun('ns:range', 'Random range', base => M.setNoteSample(base, ids(), { start: { range: v } }), true) });
    NS.reroll = button('Re-roll', { title: 'New random start spots (they stay until you re-roll again)', onClick: () => act(M.updateNotes(A.getClip(), ids(), n => {
      if (n.sample.start.mode === 'random') n.sample.start = Object.assign({}, n.sample.start, { seed: (n.sample.start.seed + 1) | 0 });
    }), 'Re-roll random start') });
    NS.tape = seg([{ v: 'inst', label: 'Follow' }, { v: 'on', label: 'On' }, { v: 'off', label: 'Off' }], 'inst', v => setNS({ tape: triIn(v) }, 'Note tape: ' + (v === 'inst' ? 'follow' : v)));
    NS.rev = seg([{ v: 'inst', label: 'Follow' }, { v: 'on', label: 'On' }, { v: 'off', label: 'Off' }], 'inst', v => setNS({ rev: triIn(v) }, 'Each grain backwards: ' + (v === 'inst' ? 'follow' : v)));
    NS.jump = el('select', { title: 'How often Scatter jumps to a new spot' });
    NS.jump.add(new Option('Follow', ''));
    for (const id of M.RATES) NS.jump.add(new Option(M.rateLabel(id), id));
    NS.jumpMixed = new Option('mixed', '__mixed');
    NS.jumpMixed.disabled = true;
    NS.jump.add(NS.jumpMixed);
    NS.jump.addEventListener('change', () => { const v = NS.jump.value; if (v === '__mixed') return; setNS({ jump: v || null }, 'Jump every ' + (v ? M.rateLabel(v) : ': follow')); });
    NS.restart = button('Restart sample here', { title: 'This note starts its sound again even when a slide joins it to the note before',
      onClick: () => { const mx = M.getMixed(A.getClip(), ids()); setNS({ restart: !(mx && mx.restart === true) }, 'Restart sample here: ' + onOff(!(mx && mx.restart === true))); } });
    NS.vel = slider('Velocity', { min: 1, max: 127, step: 1, value: 102, fmt: v => String(Math.round(v)),
      onInput: v => liveRun('ns:vel', 'Velocity', base => M.setNoteSample(base, ids(), { vel: v / 127 }), false),
      onChange: v => liveRun('ns:vel', 'Velocity', base => M.setNoteSample(base, ids(), { vel: v / 127 }), true) });
    NS.gEngine = nsGroup('Engine', NS.engine.el);
    NS.gLock = nsGroup('Lock', NS.lock.el);
    NS.gMotion = nsGroup('Motion', NS.motion.el);
    NS.gTravel = nsGroup('Travel', NS.travel.el);
    NS.gStart = nsGroup('Start', NS.start.el, NS.pos.el, NS.range.el, NS.reroll);
    NS.gTape = nsGroup('Tape', NS.tape.el);
    NS.gRev = nsGroup('Each grain backwards', NS.rev.el);
    NS.gJump = nsGroup('Jump every', NS.jump);
    NS.gRestart = nsGroup('', NS.restart);
    NS.gVel = nsGroup('', NS.vel.el);
    NS.body = el('div', { class: 'slp-nsbody' }, NS.gEngine, NS.gLock, NS.gMotion, NS.gTravel, NS.gStart, NS.gTape, NS.gRev, NS.gJump, NS.gRestart, NS.gVel);
    nsBox.append(NS.empty, NS.head, NS.body);
  }
  function segShow(s, g, v, followLabel) {
    const mixed = v === 'mixed';
    s.set(mixed ? null : v, mixed);
    if (followLabel != null && s.buttons[0]) s.buttons[0].textContent = followLabel;
    g._tag.hidden = !mixed;
  }
  function fade(node, on, why) { node.classList.toggle('slp-faded', !!on); node.title = on ? why : ''; }
  function syncNoteSettings() {
    if (!NS.body) return;
    const idl = ids(), mx = idl.length ? M.getMixed(A.getClip(), idl) : null, I = A.getInst();
    NS.empty.style.display = mx ? 'none' : '';
    NS.head.style.display = NS.body.style.display = mx ? '' : 'none';
    if (!mx) return;
    const notes = A.selectedNotes(), own = notes.filter(hasOwn).length, P = A.primaryNote() || notes[0];
    NS.headTxt.innerHTML = '';
    NS.headTxt.append(el('b', null, nNotes(idl.length) + ' selected'), own ? ' · ' + own + (own === 1 ? ' has its' : ' have their') + ' own settings (a dot on the roll marks them)' : ' · following the Sample tab');
    NS.clear.disabled = !own;
    const engines = new Set(notes.map(n => n.sample.engine || I.engine)), motions = new Set(notes.map(n => n.sample.motion || I.motion));
    const allTape = engines.size === 1 && engines.has('tape');
    segShow(NS.engine, NS.gEngine, mx.engine == null ? 'inst' : mx.engine, 'Follow (' + M.ENGINE_NAMES[I.engine] + ')');
    segShow(NS.lock, NS.gLock, triOut(mx.lock), 'Follow (' + onOff(I.lock) + ')');
    segShow(NS.motion, NS.gMotion, mx.motion == null ? 'inst' : mx.motion, 'Follow (' + MOTION_NAMES[I.motion] + ')');
    segShow(NS.travel, NS.gTravel, mx.travel);
    segShow(NS.start, NS.gStart, mx.start.mode);
    segShow(NS.tape, NS.gTape, triOut(mx.tape), 'Follow (' + onOff(I.tape) + ')');
    segShow(NS.rev, NS.gRev, triOut(mx.rev), 'Follow (' + onOff(I.rev) + ')');
    fade(NS.gLock, !engines.has('cloud'), 'Lock only works in the Cloud engine');
    fade(NS.gTape, allTape, 'The Tape engine always ties pitch to speed');
    fade(NS.gRev, allTape, 'The Tape engine has no grains');
    fade(NS.gJump, !motions.has('scatter'), 'Jump every only matters with Motion Scatter');
    const mi = M.MOTIONS.indexOf('freeze') + 1;
    NS.motion.buttons[mi].classList.toggle('faded', allTape);
    NS.motion.buttons[mi].title = allTape ? 'Tape cannot freeze: it plays Forward' : '';
    NS.travel.buttons[1].classList.toggle('faded', allTape);
    NS.travel.buttons[1].title = allTape ? 'Tape plays Path as Speed at 1x' : 'The line is where in the Region the note is: flat freezes, rising plays forwards, a step jumps';
    // Start
    const mode = mx.start.mode, posV = mx.start.pos === 'mixed' ? P.sample.start.pos : mx.start.pos, rangeV = mx.start.range === 'mixed' ? P.sample.start.range : mx.start.range;
    NS.pos.el.style.display = mode === 'region' || mode === 'random' || mode === 'mixed' ? '' : 'none';
    NS.range.el.style.display = NS.reroll.style.display = mode === 'random' ? '' : 'none';
    NS.pos.set(posV, mx.start.pos === 'mixed' ? 'mixed' : null);
    NS.range.set(rangeV, mx.start.range === 'mixed' ? 'mixed' : null);
    // Jump
    NS.jump.options[0].text = 'Follow (' + M.rateLabel(I.jump) + ')';
    NS.jumpMixed.hidden = mx.jump !== 'mixed';
    NS.jump.value = mx.jump === 'mixed' ? '__mixed' : mx.jump || '';
    NS.gJump._tag.hidden = mx.jump !== 'mixed';
    // Restart and velocity
    NS.restart.classList.toggle('on', mx.restart === true);
    NS.restart.textContent = 'Restart sample here' + (mx.restart === 'mixed' ? ' (some)' : '');
    const vel = mx.vel === 'mixed' ? P.vel : mx.vel;
    NS.vel.set(Math.round(vel * 127), mx.vel === 'mixed' ? 'mixed' : null);
    NS.gVel._tag.hidden = true;
  }

  // ---------------------------------------------------------------------------------- roll decorations
  const simCache = new WeakMap();
  let peakCache = { ch: null, bins: null };
  let binsTimer = 0;
  const binsReady = Sm => !!Sm && peakCache.ch === Sm.channels[0];
  // the peak envelope is made once per sound, outside the drawing frame (a long song takes a moment)
  function wantBins() {
    if (binsTimer) return;
    binsTimer = setTimeout(() => {
      binsTimer = 0;
      const Sm = A.getSample();
      if (!Sm || binsReady(Sm)) return;
      try { peakCache = { ch: Sm.channels[0], bins: peakBins(Sm.channels) }; } catch (e) { console.warn('StrandLock panels: no thumbnails for this sound', e); return; }
      A.redraw();
    }, 80);
  }
  function noteSim(n, info, W) {
    const Sm = A.getSample(), I = info.inst, secs = A.sampleSeconds(), R = I.region, ready = binsReady(Sm);
    if (Sm && !ready) wantBins();
    const N = clamp(Math.round(W / 3), 4, 200);
    const key = [Sm ? Sm.name + ':' + Sm.channels[0].length + (ready ? ':p' : '') : '-', secs, R.start, R.end, R.atEnd, I.speed, I.macros[0], I.motion, I.engine,
      I.jump, I.seed, I.root, I.cents, I.pitch, info.bpm, N].join('|');
    const e = simCache.get(n);
    if (e && e.key === key) return e;
    const sim = simulate(n, I, info.bpm, secs, info.table('travel'), info.table('pitch'), N);
    const out = { key, sim, amps: ready ? thumbAmps(sim, peakCache.bins, secs, R.end - R.start) : null, heads: info.table('heads') };
    simCache.set(n, out);
    return out;
  }
  function chevron(c, x, y, d, s) {
    c.beginPath(); c.moveTo(x - 1.7 * d * s, y - 3 * s); c.lineTo(x + 1.7 * d * s, y); c.lineTo(x - 1.7 * d * s, y + 3 * s); c.stroke();
  }
  let ghostIdx = { clip: null, map: null };
  function decorate(c, n, rect, info) {
    const TH = info.theme, W = rect.x1 - rect.x0, sel = info.selected, mode = info.mode, I = info.inst;
    const col = TH['lane-sample'] || TH.acc;
    if (W >= 12) {
      const e = noteSim(n, info, W), sim = e.sim, N = sim.N;
      // U7: what this note plays, faint, just above its line, in the waveform strip's colour
      if (e.amps) {
        const h = Math.max(6, info.RH * 1.1), cw = W / N;
        c.globalAlpha = mode === 8 || mode === 1 ? (sel ? 0.42 : 0.24) : (sel ? 0.22 : 0.11);
        c.fillStyle = TH.wave || col;
        for (let i = 0; i < N; i++) {
          const a = e.amps[i];
          if (!(a > 0.015)) continue;
          const yc = info.yAt((i + 0.5) / N) - 3 - h / 2, hh = Math.max(1, a * h);
          c.fillRect(rect.x0 + i * cw, yc - hh / 2, Math.max(1, cw - 0.7), hh);
        }
      }
      // H4: chevrons for direction and speed, just under the line
      const strong = mode === 8 ? (sel ? 1 : 0.8) : (sel ? 0.8 : 0.5);
      const motion = n.sample.motion || I.motion;
      c.strokeStyle = col; c.fillStyle = col; c.lineWidth = 1.4; c.lineCap = 'round'; c.lineJoin = 'round';
      let x = rect.x0 + 10;
      while (x < rect.x1 - 7) {
        const t = (x - rect.x0) / W, i = Math.min(N, Math.round(t * N)), y = info.yAt(t) + 6.5;
        if (!(sim.pos[i] === sim.pos[i])) { c.globalAlpha = strong; c.fillRect(x - 2.5, y - 2.5, 5, 5); break; }   // silent from here (At the end: Stop)
        const d = sim.dir[i], s = sim.sp[i];
        c.globalAlpha = strong;
        if (d === 0 || s < 0.02) { c.fillRect(x - 2.6, y - 3, 1.6, 6); c.fillRect(x + 1, y - 3, 1.6, 6); x += 24; continue; }   // stopped
        chevron(c, x, y, d, 1);
        if (s >= 2.5) chevron(c, x + 3.6 * d, y, d, 1);
        if (motion === 'mirror' && I.heads + (tabAt(e.heads, t) - 0.5) * 14 + (I.macros[2] - 0.5) * 14 >= 1.5) { c.globalAlpha = strong * 0.5; chevron(c, x, y + 6, -d, 0.75); }
        x += clamp(14 / Math.sqrt(s), 6, 40);
      }
      c.globalAlpha = strong * 0.85;
      for (let i = 1; i <= N; i++) if (sim.jump[i]) { const t = i / N, y = info.yAt(t) + 6.5; c.fillRect(rect.x0 + t * W - 0.6, y - 4.5, 1.2, 9); }   // Scatter jumps
    }
    // S7: a dot on notes with their own settings
    if (hasOwn(n)) {
      c.globalAlpha = 1; c.fillStyle = col; c.strokeStyle = TH['dot-fill']; c.lineWidth = 1.2;
      c.beginPath(); c.arc(rect.x0 + 3, rect.y0 - 11, 3.2, 0, 7); c.fill(); c.stroke();
    }
    // a slide whose target starts well inside its source: the engine cuts the source short there
    if (n.glideTo) {
      const m = A.noteById(n.glideTo);
      if (m && m.s > n.s + 1e-6 && m.s < n.s + n.l - 0.13) {
        const x = m.s * info.BW, y = info.yAt((m.s - n.s) / n.l);
        c.globalAlpha = 1; c.strokeStyle = TH.warn; c.lineWidth = 1.5; c.setLineDash([2, 2]);
        c.beginPath(); c.moveTo(x, y - 8); c.lineTo(x, y + 8); c.stroke(); c.setLineDash([]);
        c.fillStyle = TH.warn; c.beginPath(); c.moveTo(x, y - 22); c.lineTo(x + 6.5, y - 11); c.lineTo(x - 6.5, y - 11); c.closePath(); c.fill();
        c.fillStyle = TH.bg; c.font = 'bold 9px sans-serif'; c.textAlign = 'center'; c.fillText('!', x, y - 12.5); c.textAlign = 'start';
      }
    }
    // the ghost's slide links (Connect preview)
    const g = A.getGhost();
    if (g && Array.isArray(g.notes)) {
      if (ghostIdx.clip !== g) ghostIdx = { clip: g, map: new Map(g.notes.map(q => [q.id, q])) };
      const gn = ghostIdx.map.get(n.id);
      if (gn && gn.glideTo !== n.glideTo) {
        const Gm = A.geometry(), x1 = (n.s + n.l) * info.BW, y1 = info.yAt(1);
        c.globalAlpha = 0.9; c.strokeStyle = TH.ghost; c.fillStyle = TH.ghost; c.lineWidth = 1.6; c.setLineDash([5, 4]);
        const tg = gn.glideTo ? ghostIdx.map.get(gn.glideTo) : null;
        if (tg) {
          const yT = (Gm.MAXP - tg.p) * Gm.RH + Gm.RH / 2 - (M.evalNodes(tg.c.pitch, 0) - 0.5) * 24 * Gm.RH;
          c.beginPath(); c.moveTo(x1, y1); c.lineTo(tg.s * info.BW, yT); c.stroke(); c.setLineDash([]);
          c.beginPath(); c.arc(tg.s * info.BW, yT, 3, 0, 7); c.fill();
        } else if (n.glideTo) {
          const m = A.noteById(n.glideTo);
          if (m) {
            const yT = (Gm.MAXP - m.p) * Gm.RH + Gm.RH / 2, xm = (x1 + m.s * info.BW) / 2, ym = (y1 + yT) / 2;
            c.setLineDash([]); c.beginPath(); c.moveTo(xm - 4, ym - 4); c.lineTo(xm + 4, ym + 4); c.moveTo(xm + 4, ym - 4); c.lineTo(xm - 4, ym + 4); c.stroke();
          }
        }
        c.setLineDash([]);
      }
    }
    c.globalAlpha = 1;
  }

  // ---------------------------------------------------------------------------------- Try this (U3)
  const MODE_TIPS = {
    1: 'draw a chord, drag a box around it, then Tools ▸ Strum ▸ Strum up.',
    2: 'select a held note, choose Shapes ▸ Ramp up, then drag the small ◇ to curve the rise.',
    3: 'Pattern ▸ Square at 1/16 Triplet with both depths near 80%: an instant rhythmic gate.',
    4: 'Shapes ▸ Ramp up, so the vibrato grows across each note like a singer\'s.',
    5: 'Pattern ▸ Squiggle at 1/16 Triplet with Lock to the beat: a wah that grooves with the bar.',
    6: 'select a phrase, choose Across, then Shapes ▸ Ramp down: one long aah to ooh over all the notes.',
    7: 'Shapes ▸ Swell: each note goes hollow in the middle, then opens again.',
  };
  const SUB_TIPS = {
    heads: 'Shapes ▸ Ramp up on Heads: each note blooms from one playhead to many.',
    grain: 'Random at 1/16 with Smooth near 50% on Grain: grains that breathe.',
    refresh: 'set Refresh to Synced on the Sample tab, then Shapes ▸ Ramp up here for a stutter that speeds up.',
  };
  const MOTION_TIPS = {
    forward: 'Forward: draw the Travel line below the middle on one note, and that note plays backwards.',
    backward: 'Backward: add Shapes ▸ Swell on Volume, so each note fades in like reversed tape.',
    bounce: 'Bounce: raise Heads to 3 and add a little Drift; the heads meet and pass each other.',
    mirror: 'Mirror: give it 2 or more Heads; every other head runs backwards, so the sound breathes both ways.',
    freeze: 'Freeze: raise Heads to 4, then add Spray.',
    scatter: 'Scatter: set Jump every to 1/8 Triplet, then Random on Grain.',
  };
  // a suggestion must work in the engine the note plays: Spray needs Cloud, Grain and Freeze do nothing in Tape
  const ENGINE_TIPS = {
    freeze: { tape: 'Freeze does nothing in Tape (it plays forwards): pick Voice or Cloud on the Sample tab, then raise Heads to 4.',
      voice: 'Freeze: raise Heads to 4 and set Head tune to Detune for a frozen choir (pick Cloud to add Spray too).' },
    scatter: { tape: 'Scatter: set Jump every to 1/8 Triplet, then raise Heads to 3.' },
    grain: { tape: 'Grain does nothing in Tape: pick Voice or Cloud on the Sample tab, then try Random at 1/16 on Grain.' },
  };
  // A Motion tip whose suggestion is already done (a recipe, or the user, set it) gives way to the next step.
  const THEN_TIPS = {
    freeze: { done: (I, kind) => kind !== 'tape' && I.heads >= 4 && (kind === 'cloud' ? I.spray > 0 : I.headTune !== 'same'),
      text: 'Freeze: select a few notes and click a different spot on the waveform for each: every note holds its own moment of the sound.' },
    bounce: { done: (I, kind) => kind !== 'tape' && I.heads >= 3 && I.drift > 0,
      text: 'Bounce: drag the Region smaller on the waveform, so the heads turn round more often.' },
    mirror: { done: (I, kind) => kind !== 'tape' && I.heads >= 2,
      text: 'Mirror: raise Spread on the Sample tab, so the forward and backward heads read different spots.' },
    scatter: { done: I => I.jump === '1/8T',
      text: 'Scatter: press Re-roll on the Sample tab for new jump spots (the rhythm of the notes stays).' },
  };
  function updateTry() {
    if (!tryEl) return;
    const m = A.getMode(), sub = A.getSubLane(), P = A.primaryNote(), I = A.getInst();
    const eng = (P && P.sample.engine) || I.engine, lockOn = eng === 'cloud' && (P && P.sample.lock != null ? P.sample.lock : I.lock);
    const kind = eng === 'cloud' && lockOn ? 'lock' : eng;
    const pick = (key, base) => { const e = ENGINE_TIPS[key]; return (e && e[kind]) || base; };
    let text;
    if (m === 8) {
      if (sub === 'travel') {
        const mo = (P && P.sample.motion) || I.motion, then = THEN_TIPS[mo];
        text = then && then.done(I, kind) ? then.text : pick(mo, MOTION_TIPS[mo]);
      }
      else text = pick(sub, SUB_TIPS[sub]);
    } else text = MODE_TIPS[m];
    if (!text) { tryEl.innerHTML = ''; tryEl.dataset.t = ''; return; }
    if (tryEl.dataset.t === text) return;
    tryEl.dataset.t = text;
    tryEl.innerHTML = '';
    tryEl.append(el('b', null, 'Try this: '), text);
  }

  // ---------------------------------------------------------------------------------- sync and events
  function syncAll(rebuild) {
    paintBar();
    if (rebuild) syncDrawer();
    syncInspector();
    syncNoteSettings();
    updateTry();
  }
  function wire() {
    A.on('selection', () => { if (live && !sameIds(live.ids, ids())) endLive(); loadGenValues(); syncAll(true); });
    A.on('clip', e => {
      if (e && e.live) { syncInspector(); return; }
      if (busy) { paintBar(); syncInspector(); syncNoteSettings(); return; }
      if (live && A.getClip() !== live.last) live = null;   // something else changed the clip (undo, a drag on the roll)
      loadGenValues();
      syncAll(true);
    });
    A.on('inst', e => {
      if (e && e.live) { syncInspector(); syncNoteSettings(); return; }
      syncAll(!busy && !dragging);
    });
    A.on('mode', () => { const L = A.activeLane(); if (L) S.lane = L; endLive(); loadGenValues(); syncAll(true); });
    A.on('node', () => syncInspector());
    A.on('nodes', () => syncInspector());
    A.on('scope', () => { if (S.drawer === 'shapes') syncDrawer(); });
    A.on('transport', () => syncInspector());
    A.on('sample', () => { wantBins(); A.redraw(); });
    // touch has no mouseleave: a tap anywhere outside the selection bar ends a preview that a tap started
    window.addEventListener('pointerdown', e => { if (e.pointerType && e.pointerType !== 'mouse' && ghostOwner && !bar.contains(e.target)) clearGhost(); }, true);
  }
  function init() {
    if (!bar || !insp || !nsBox) { console.warn('StrandLock panels: a container is missing from the page'); return; }
    buildBar();
    buildInspector();
    buildNoteSettings();
    wire();
    hookLaneArrows();
    A.addNoteDecorator(decorate);
    const L = A.activeLane();
    if (L) S.lane = L;
    loadGenValues();
    syncAll(true);
  }
  try { init(); } catch (e) { console.warn('StrandLock panels could not start:', e); }

  return Object.assign({}, PURE, {
    state: S,
    openDrawer, openLaneMenu,
    debug: { live: () => live, drawer: () => S.drawer, tool: () => S.tool },
  });
})();
if (typeof module !== 'undefined') module.exports = SLPanels;
