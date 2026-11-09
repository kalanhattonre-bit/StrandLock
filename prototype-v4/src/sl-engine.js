// sl-engine.js: StrandLock v4 sound engine (BLUEPRINT sections 1 and 3). Owner: engine builder.
// Self-contained: createStrandEngineModule() references no outside names, because the page ships
// createStrandEngineModule.toString() into an AudioWorklet. The same Engine runs in node (tests), in the
// worklet, and on the main thread as a fallback. No Math.random: every random value is a seeded hash.
// Naming follows Cubase: MIDI 60 = C3.

function createStrandEngineModule() {
  'use strict';
  const version = '4.0.0-engine.1';
  const TAU = Math.PI * 2;
  const CTRL = 32;                 // control rate: lanes, macros, heads and speeds update every 32 samples
  const MAX_HEADS = 8;
  const VOICE_GRAINS = 8;          // Voice engine: at most 8 grains per head (grains get shorter above that)
  const CLOUD_GRAINS = 5;          // Cloud: 3 overlap by design, 5 slots leave room while the grain size moves
  const LOCK_MAX_N = 12;           // Lock wavelengths cap: 8 heads x 2 captures x 12 taps = 192 = the 48 kHz budget
  const BUDGET_48K = 192;

  const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
  const fin = (x, d) => (typeof x === 'number' && x === x && x !== Infinity && x !== -Infinity ? x : d);

  // ------------------------------------------------------------------ seeds (BLUEPRINT 1.6), copied from SLModel
  // hash32(...ints): MurmurHash3 x86_32 over 32-bit integers (seed 0x9747b28c), finished with fmix32.
  function fmix32(h) {
    h ^= h >>> 16; h = Math.imul(h, 0x85ebca6b);
    h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35);
    h ^= h >>> 16;
    return h >>> 0;
  }
  function hash32() {
    let h = 0x9747b28c | 0;
    for (let i = 0; i < arguments.length; i++) {
      let k = Math.imul(arguments[i] | 0, 0xcc9e2d51);
      k = (k << 15) | (k >>> 17);
      k = Math.imul(k, 0x1b873593);
      h ^= k;
      h = (h << 13) | (h >>> 19);
      h = (Math.imul(h, 5) + 0xe6546b64) | 0;
    }
    h ^= arguments.length * 4;
    return fmix32(h);
  }
  function rand01(h) { return (h >>> 0) / 4294967296; }
  // FNV-1a 32-bit over the UTF-16 code units of the id string.
  function idHash(id) {
    const s = String(id);
    let h = 0x811c9dc5;
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193); }
    return h >>> 0;
  }
  // Salts that keep the random streams apart (part of the engine's seed contract).
  const SALT_JUMP = 0x5ca7, SALT_SKIP = 0x51c1, SALT_SPRAY = 0x5b7a, SALT_DRIFT = 0x0d71, SALT_TINY = 0x7177, SALT_START = 0x57a7;

  // ------------------------------------------------------------------ rates (BLUEPRINT 1.5)
  const RATE_BEATS = {};
  [['1bar', 4], ['1/2', 2], ['1/4', 1], ['1/8', 0.5], ['1/16', 0.25], ['1/32', 0.125], ['1/64', 0.0625]].forEach(([b, v]) => {
    RATE_BEATS[b] = v; RATE_BEATS[b + 'T'] = v * 2 / 3; RATE_BEATS[b + 'D'] = v * 1.5;
  });
  // Beats per rate step. A positive number passes through as beats; an unknown id reads as 1/16 (same as SLModel).
  function rateBeats(id) {
    if (typeof id === 'number' && id === id && id > 0 && id !== Infinity) return id;
    return Object.prototype.hasOwnProperty.call(RATE_BEATS, id) ? RATE_BEATS[id] : 0.25;
  }

  // ------------------------------------------------------------------ lanes (BLUEPRINT 1.2, 1.3)
  const LANE_IDS = ['pitch', 'amp', 'vib', 'tone', 'formant', 'hollow', 'travel', 'heads', 'grain', 'refresh'];
  const NL = LANE_IDS.length;
  const LANE_DEF = [0.5, 0.8, 0, 1.0, 0.5, 0, 0.75, 0.5, 0.5, 0.5];
  const LI = {}; LANE_IDS.forEach((id, i) => { LI[id] = i; });
  const L_PITCH = 0, L_AMP = 1, L_VIB = 2, L_TONE = 3, L_FORMANT = 4, L_HOLLOW = 5, L_TRAVEL = 6, L_HEADS = 7, L_GRAIN = 8, L_REFRESH = 9;
  // Lane thresholds hold at Float32 precision (same constant as SLModel.F32_EPS): the snapshot tables are
  // Float32 and Math.fround(0.48) = 0.4799999893, so a value drawn on a band edge plays as the edge.
  const F32_EPS = 2e-8;
  function travelSpeed(v) {
    if (v >= 0.48 - F32_EPS && v <= 0.52 + F32_EPS) return 0;
    if (v > 0.52) return Math.min(8, Math.pow(2, ((v - 0.75) / 0.23) * 3));
    return -Math.min(8, Math.pow(2, ((0.25 - v) / 0.23) * 3));
  }
  // v3's glide curve (same as SLModel.gShape).
  function gShape(u, c) {
    u = clamp(u, 0, 1);
    const sm = u * u * (3 - 2 * u);
    if (c > 0) return sm * (1 - c) + (1 - Math.pow(1 - u, 1 + 4 * c)) * c;
    if (c < 0) return sm * (1 + c) + Math.pow(u, 1 - 4 * c) * (-c);
    return sm;
  }
  // Head tune: semitone offset of head i.
  const HEAD_TUNE = {
    same: [0, 0, 0, 0, 0, 0, 0, 0],
    detune: [0, 0.07, -0.07, 0.14, -0.14, 0.21, -0.21, 0.28],
    octaves: [0, 12, -12, 24, 0, 12, -12, 24],
    fifths: [0, 7, 12, 19, -5, 7, 12, 19],
  };

  // ------------------------------------------------------------------ tables (built once, never on first use)
  const HANN_N = 4096;
  const HANN = new Float32Array(HANN_N + 2);   // 0.5 - 0.5 cos(2 pi x), x in 0..1
  for (let i = 0; i <= HANN_N + 1; i++) HANN[i] = 0.5 - 0.5 * Math.cos(TAU * Math.min(1, i / HANN_N));
  const FADE_N = 1024;
  const FADE = new Float32Array(FADE_N + 2);   // sin(pi/2 * w), w in 0..1 (equal-power fade in)
  for (let i = 0; i <= FADE_N + 1; i++) FADE[i] = Math.sin(Math.PI / 2 * Math.min(1, i / FADE_N));
  function hannTab(x) {   // x in 0..1
    if (x <= 0 || x >= 1) return 0;
    const f = x * HANN_N, i = f | 0;
    return HANN[i] + (f - i) * (HANN[i + 1] - HANN[i]);
  }
  function fadeIn(w) {   // w clamped to 0..1
    if (w <= 0) return 0;
    if (w >= 1) return 1;
    const f = w * FADE_N, i = f | 0;
    return FADE[i] + (f - i) * (FADE[i + 1] - FADE[i]);
  }

  // ------------------------------------------------------------------ instrument defaults (BLUEPRINT 1.4)
  function defaultInst() {
    return {
      engine: 'voice', lock: false, motion: 'forward', speed: 1, heads: 1, spread: 0.25,
      grainVoice: 2, grainCloud: 140, grainLock: 4,
      refresh: { mode: 'smooth', ms: 120, rate: '1/16', lockToBeat: true, skip: 0 },
      spray: 0, pitch: 0, tape: false, rev: false, drift: 0, headTune: 'same',
      region: { start: 0, end: 1, atEnd: 'loop', xfadeMs: 10 },
      jump: '1/8',
      macros: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5],
      offsets: { amp: 0, vib: 0, tone: 0, formant: 0, hollow: 0 },
      glideMode: 'legato', glideTime: 0.5, vibRate: 5.5, root: 60, cents: 0, key: 0, scale: 'Chromatic', scaleLock: false,
      velSens: 0.7, polyphony: 16, outGain: 0, bendRange: 2,
      mod1: { target: 'vib', cc: 1 }, mod2: { target: 'tone', cc: 2 },
      liveLegato: false, seed: 1,
    };
  }
  const ENGINES = { tape: 1, voice: 1, cloud: 1 };
  const MOTIONS = { forward: 1, backward: 1, bounce: 1, mirror: 1, freeze: 1, scatter: 1 };
  // Engine-side copy of the inst with every field present and finite (cheap; setInst may be called often).
  function normInst(src) {
    const d = defaultInst(), s = src || {};
    const o = Object.assign({}, d, s);
    o.engine = ENGINES[o.engine] ? o.engine : 'voice';
    o.lock = !!o.lock;
    o.motion = MOTIONS[o.motion] ? o.motion : 'forward';
    o.speed = clamp(fin(o.speed, 1), -16, 16);
    o.heads = clamp(fin(o.heads, 1), 1, MAX_HEADS);
    o.spread = clamp(fin(o.spread, 0.25), 0, 1);
    o.grainVoice = clamp(fin(o.grainVoice, 2), 0.25, 16);
    o.grainCloud = clamp(fin(o.grainCloud, 140), 5, 2000);
    o.grainLock = clamp(fin(o.grainLock, 4), 1, 64);
    const r = Object.assign({}, d.refresh, s.refresh || {});
    r.mode = r.mode === 'free' || r.mode === 'sync' ? r.mode : 'smooth';
    r.ms = clamp(fin(r.ms, 120), 5, 10000); r.lockToBeat = r.lockToBeat !== false; r.skip = clamp(fin(r.skip, 0), 0, 1);
    o.refresh = r;
    o.spray = clamp(fin(o.spray, 0), 0, 1);
    o.pitch = clamp(fin(o.pitch, 0), -48, 48);
    o.tape = !!o.tape; o.rev = !!o.rev;
    o.drift = clamp(fin(o.drift, 0), 0, 1);
    o.headTune = HEAD_TUNE[o.headTune] ? o.headTune : 'same';
    const g = Object.assign({}, d.region, s.region || {});
    g.start = clamp(fin(g.start, 0), 0, 1); g.end = clamp(fin(g.end, 1), 0, 1);
    if (g.end < g.start) { const t = g.start; g.start = g.end; g.end = t; }
    g.atEnd = g.atEnd === 'hold' || g.atEnd === 'stop' ? g.atEnd : 'loop';
    g.xfadeMs = clamp(fin(g.xfadeMs, 10), 0, 1000);
    o.region = g;
    const m = Array.isArray(s.macros) ? s.macros : d.macros;
    o.macros = [0, 1, 2, 3, 4, 5, 6, 7].map(i => clamp(fin(m[i], 0.5), 0, 1));
    const of = Object.assign({}, d.offsets, s.offsets || {});
    o.offsets = { amp: clamp(fin(of.amp, 0), -1, 1), vib: clamp(fin(of.vib, 0), -1, 1), tone: clamp(fin(of.tone, 0), -1, 1),
      formant: clamp(fin(of.formant, 0), -1, 1), hollow: clamp(fin(of.hollow, 0), -1, 1) };
    o.vibRate = clamp(fin(o.vibRate, 5.5), 0, 50);
    o.root = clamp(fin(o.root, 60), 0, 127); o.cents = clamp(fin(o.cents, 0), -100, 100);
    o.velSens = clamp(fin(o.velSens, 0.7), 0, 1);
    o.polyphony = clamp(Math.round(fin(o.polyphony, 16)), 1, 32);
    o.outGain = clamp(fin(o.outGain, 0), -60, 24);
    o.bendRange = clamp(fin(o.bendRange, 2), 0, 48);
    const mt = (x, dd) => { const y = Object.assign({}, dd, x || {}); y.target = (y.target === 'off' || LI[y.target] !== undefined) ? y.target : 'off'; return y; };
    o.mod1 = mt(s.mod1, d.mod1); o.mod2 = mt(s.mod2, d.mod2);
    o.liveLegato = !!o.liveLegato;
    o.seed = fin(o.seed, 1) | 0;
    return o;
  }

  // ------------------------------------------------------------------ source reading
  // A source is { L, R, n, sr, stereo }. Reads write the result into RL/RR (no allocation on the audio path).
  let RL = 0, RR = 0;
  // 4-point Hermite at absolute (fractional) frame index x, both channels at the same position. Every index is guarded.
  function readAbs(S, x) {
    if (!(x > -1e12 && x < 1e12)) x = 0;
    const n = S.n, L = S.L;
    const i = Math.floor(x), f = x - i;
    let i0, i1, i2, i3;
    if (i >= 1 && i < n - 2) { i0 = i - 1; i1 = i; i2 = i + 1; i3 = i + 2; }
    else {
      const m = n - 1;
      i0 = i - 1 < 0 ? 0 : i - 1 > m ? m : i - 1;
      i1 = i < 0 ? 0 : i > m ? m : i;
      i2 = i + 1 < 0 ? 0 : i + 1 > m ? m : i + 1;
      i3 = i + 2 < 0 ? 0 : i + 2 > m ? m : i + 2;
    }
    let a = L[i0], b = L[i1], c = L[i2], e = L[i3];
    RL = (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
    if (S.stereo) {
      const R = S.R;
      a = R[i0]; b = R[i1]; c = R[i2]; e = R[i3];
      RR = (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
    } else RR = RL;
  }
  // Region read at x (frames from the Region start; x is unbounded). Clamp mode: hold the edge frame.
  // Loop mode: the period is exactly L, the Region length, so a held note or a beat loop never drifts against the
  // transport. The seam crossfades X frames with material just outside the Region (equal-power fades with GrainLock's
  // correlation correction 1/sqrt(1 + 2 rho gi go), so correlated material keeps its level and uncorrelated material
  // its power):
  //  - post (preferred, when the file goes on after the Region end): every pass after the first starts with the
  //    Region start fading in while the file's own continuation past the Region end fades out. The first pass plays
  //    the true attack.
  //  - pre (when only material before the Region start exists): every pass ends by fading from the Region's last X
  //    frames into the X frames that lead into the Region start, landing exactly on frame 0.
  //  - none (the Region is the whole file, or there is no room on either side): a plain wrap, like v3's looped
  //    buffer. The starter sounds are seamless loops, so they wrap without a click.
  // Going backwards past the very first start (post) meets one discontinuity, the first time only.
  function readReg(S, G, x) {
    if (G.loop) {
      const L = G.L, X = G.X;
      if (x >= 0 && (G.post ? x < L : x < L - X)) { readAbs(S, G.rs + x); return; }   // first pass, plain stretch
      let v = x - L * Math.floor(x / L);
      if (v >= L) v -= L;
      if (X > 0) {
        if (G.post) {
          if (v < X && (x >= L || x < 0)) {
            const w = v * G.invX, gi = fadeIn(w), go = fadeIn(1 - w), kc = 1 / Math.sqrt(1 + 2 * G.rho * gi * go);
            readAbs(S, G.rs + L + v);
            const ol = RL, or = RR;
            readAbs(S, G.rs + v);
            RL = (RL * gi + ol * go) * kc; RR = (RR * gi + or * go) * kc;
            return;
          }
        } else if (v >= L - X) {
          const w = (v - (L - X)) * G.invX, gi = fadeIn(w), go = fadeIn(1 - w), kc = 1 / Math.sqrt(1 + 2 * G.rho * gi * go);
          readAbs(S, G.rs + v - L);
          const il = RL, ir = RR;
          readAbs(S, G.rs + v);
          RL = (RL * go + il * gi) * kc; RR = (RR * go + ir * gi) * kc;
          return;
        }
      }
      readAbs(S, G.rs + v);
    } else {
      const u = x < 0 ? 0 : x > G.L - 1 ? G.L - 1 : x;
      readAbs(S, G.rs + u);
    }
  }
  // Correlation of the two sides of the seam fade (both channels summed), clamped to 0..1.
  function seamRho(S, G) {
    const X = Math.floor(G.X);
    if (X < 2) return 0;
    const st = Math.max(1, Math.floor(X / 2048)), n1 = S.n - 1;
    let ab = 0, aa = 0, bb = 0;
    for (let t = 0; t < X; t += st) {
      let ia = G.post ? G.rs + t : G.rs - X + t, ib = G.post ? G.rs + G.L + t : G.rs + G.L - X + t;
      ia = ia < 0 ? 0 : ia > n1 ? n1 : ia; ib = ib < 0 ? 0 : ib > n1 ? n1 : ib;
      const a = S.L[ia] + S.R[ia], b = S.L[ib] + S.R[ib];
      ab += a * b; aa += a * a; bb += b * b;
    }
    const r = aa > 1e-20 && bb > 1e-20 ? ab / Math.sqrt(aa * bb) : 0;
    return r > 0 ? (r < 1 ? r : 1) : 0;
  }
  // Where a grain or a Lock capture reads from. In loop mode the head's own unwrapped position: reads that cross
  // a pass boundary then go through readReg's seam fade (a position wrapped to 0..L would cross the seam from the
  // wrong side, which is the first pass's hard start). Bounce folds; Hold and Stop clamp (as mapPos).
  function grabPos(G, x) { return G.loop ? x : mapPos(G, x); }
  // Where a head really is (0..L frames): Bounce folds, Loop wraps (as readReg), Hold and Stop clamp.
  function mapPos(G, x) {
    if (G.fold) { const P2 = 2 * G.L; let m = x - P2 * Math.floor(x / P2); return m < G.L ? m : P2 - m; }
    if (G.loop) { const v = x - G.L * Math.floor(x / G.L); return v < G.L ? v : v - G.L; }
    return x < 0 ? 0 : x > G.L - 1 ? G.L - 1 : x;
  }

  // ------------------------------------------------------------------ biquad (RBJ cookbook, transposed direct form II)
  class Biquad {
    constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; this.key = NaN; }
    reset() { this.z1 = 0; this.z2 = 0; }
    setPeak(sr, f, q, db) {
      const key = f * 1e3 + q * 7 + db * 13.7;
      if (key === this.key) return; this.key = key;
      const A = Math.pow(10, db / 40), w = TAU * clamp(f, 10, sr * 0.45) / sr, cs = Math.cos(w), al = Math.sin(w) / (2 * q);
      const a0 = 1 + al / A;
      this.b0 = (1 + al * A) / a0; this.b1 = (-2 * cs) / a0; this.b2 = (1 - al * A) / a0; this.a1 = (-2 * cs) / a0; this.a2 = (1 - al / A) / a0;
    }
    setLow(sr, f, q) {
      const key = f * 1e3 + q * 7 - 99;
      if (key === this.key) return; this.key = key;
      const w = TAU * clamp(f, 10, sr * 0.45) / sr, cs = Math.cos(w), al = Math.sin(w) / (2 * q), a0 = 1 + al;
      this.b0 = (1 - cs) / 2 / a0; this.b1 = (1 - cs) / a0; this.b2 = (1 - cs) / 2 / a0; this.a1 = (-2 * cs) / a0; this.a2 = (1 - al) / a0;
    }
    run(buf, n) {
      let z1 = this.z1, z2 = this.z2;
      const b0 = this.b0, b1 = this.b1, b2 = this.b2, a1 = this.a1, a2 = this.a2;
      for (let j = 0; j < n; j++) {
        const x = buf[j], y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        buf[j] = y;
      }
      if (!(z1 > -1e9 && z1 < 1e9 && z2 > -1e9 && z2 < 1e9)) { z1 = 0; z2 = 0; for (let j = 0; j < n; j++) if (!(buf[j] > -1e9 && buf[j] < 1e9)) buf[j] = 0; }
      this.z1 = z1; this.z2 = z2;
    }
  }

  // ------------------------------------------------------------------ heads and voices
  const K_TAPE = 0, K_VOICE = 1, K_CLOUD = 2, K_LOCK = 3;
  const ST_ATTACK = 1, ST_SUSTAIN = 2, ST_RELEASE = 3, ST_STEAL = 4;
  const DIR = { forward: 1, backward: -1, bounce: 1, freeze: 0, scatter: 1, mirror: 1 };

  class Head {
    constructor(i) {
      this.i = i;
      this.raw = 0; this.off = 0; this.dOff = 0; this.spd = 0; this.scat = 0;   // position = raw + off (frames from Region start)
      this.g0L = 0; this.g0R = 0; this.g1L = 0; this.g1R = 0; this.dL = 0; this.dR = 0;
      this.stopG = 1; this.level = 0; this.dir = 0; this.spray = 0; this.smooth = true;
      this.want = false; this.allowed = true; this.active = false; this.on = false; this.rendered = false;
      this.est = 2; this.grant = 0; this.peak = 0; this.segPeak = 0;
      this.P = 100; this.len = 100; this.step = 1; this.norm = 1; this.hop = 33; this.PR = 100; this.dth = 0.01; this.nT = 4;
      this.gT = new Float64Array(8); this.gX = new Float64Array(8); this.gS = new Float64Array(8);
      this.gN = new Float64Array(8); this.gA = new Float64Array(8);
      this.gc = 0; this.nextT = 0; this.gIdx = 0; this.needPrime = true;
      this.xRaw = 0; this.xLeft = 0; this.xLen = 1; this.grabRaw = 0;
      this.grab = 0; this.capA = 0; this.capB = 0; this.nA = 4; this.nB = 4; this.th = 0; this.xf = -1; this.periods = 0;
      this.drift = 0; this.tiny = 0; this.regrab = false; this.rho = 0;
    }
    clearGrains() { this.gc = 0; this.needPrime = true; this.xLeft = 0; this.xf = -1; }
  }

  class Voice {
    constructor(E, kind, note, U, m, kStart) {
      this.E = E; this.kind = kind; this.note = note; this.U = U; this.m = m; this.startK = kStart;
      this.serial = ++E.serial;
      this.heads = []; for (let i = 0; i < MAX_HEADS; i++) this.heads.push(new Head(i));
      this.vt = 0; this.ph = 0; this.first = true; this.posInit = false; this.startPos = null; this.pre = false;
      this.env = 0; this.st = ST_ATTACK; this.aStep = 1 / (0.008 * E.sr); this.rStep = 1 / (0.06 * E.sr); this.sStep = 1 / (0.005 * E.sr);
      this.released = false; this.done = false; this.bridging = false; this.sustained = false; this.key = null;
      this.vibPh = 0; this.refPh = 0; this.refIdx = 0; this.refInit = false; this.jIdx = 0; this.jInit = false;
      this.hL = new Biquad(); this.hR = new Biquad(); this.fL = new Biquad(); this.fR = new Biquad(); this.lL = new Biquad(); this.lR = new Biquad();
      this.g0 = 0; this.g1 = 0; this.dg = 0;
      this.bufL = new Float64Array(CTRL); this.bufR = new Float64Array(CTRL);
      this.reg = { rs: 0, L: 64, X: 0, Lp: 64, invX: 0, loop: true, fold: false, stop: false, post: true, rho: 0, cRs: -1, cL: -1, cX: -1, cPost: null, cSrc: null };
      this.lanes = new Float64Array(NL);
      this.kc = K_VOICE; this.lastKc = -1; this.Hf = 1; this.level = 0; this.nh = 0; this.tabs = null;
      this.engineName = 'voice'; this.motion = 'forward'; this.lpHz = 0; this.fmtDb = 0; this.fmtHz = 0; this.hollowDb = 0;
      this.setNote(note);
      // Drift and the tiny head offsets belong to the voice, seeded from its chain's first note (chain.id), so a
      // chain hand-over never bends or jumps a head (D2) and a chain plays exactly like one long note.
      const vh = (note.chain && note.chain.id != null ? idHash(note.chain.id) : this.nh) | 0, seed = E.inst.seed;
      for (const h of this.heads) {
        h.drift = rand01(hash32(vh, h.i, SALT_DRIFT, seed)) * 2 - 1;
        h.tiny = h.i > 0 ? rand01(hash32(vh, h.i, SALT_TINY, seed)) * 0.008 : 0;   // seconds of source
      }
    }
    setNote(X) {
      this.note = X;
      this.nh = (X.idHash != null ? X.idHash : idHash(X.id)) | 0;
      this.tabs = X.lanes ? LANE_IDS.map(id => X.lanes[id] || null) : null;
    }
    // Chain hand-over (D2): same voice, no new attack; playhead, grain phase and vibrato phase carry on.
    switchTo(X, U) { this.setNote(X); this.U = U; this.bridging = false; }
    release() {
      if (this.released) return;
      this.released = true;
      if (this.st !== ST_STEAL) this.st = ST_RELEASE;
    }
    steal() { this.released = true; this.st = ST_STEAL; }
    // Fast-forward (chase): run the control ticks from the chain's first note to kNow without sound, so the
    // playhead lands where it would have been had the note played from its start.
    chase(kNow) {
      const E = this.E;
      let kk = E.kOf(this.U);
      const maxTicks = 1 << 17;
      if (kNow - kk > maxTicks * CTRL) kk = kNow - maxTicks * CTRL;   // very long notes: the far past is approximate
      this.tick(kk, true);
      while (kk + CTRL <= kNow) {
        this.ffAdvance(CTRL); kk += CTRL;
        this.ffChain(kk);
        this.tick(kk, true);
      }
      this.ffAdvance(kNow - kk);
      this.first = true; this.vt = 0; this.ph = 0;
      this.aStep = 1 / (0.010 * E.sr);   // chase fade-in 10 ms
      for (const h of this.heads) { h.clearGrains(); h.active = false; }
    }
    ffAdvance(n) { for (const h of this.heads) { h.raw += h.spd * n; h.off += h.dOff * n; } }
    ffChain(kk) {
      const E = this.E, u = E.uAt(kk);
      for (let guard = 0; guard < 64; guard++) {
        const X = this.note, nid = X.chain && X.chain.nextId;
        if (!nid) return;
        const nx = E.byId.get(nid);
        if (!nx || !(nx.s > X.s)) return;
        const Un = nx.s + this.m * E.Lb;
        if (Un > u) return;
        this.switchTo(nx, Un);
      }
    }

    // ---------------------------------------------------------------- control tick (every 32 samples from voice start)
    tick(kAbs, ff) {
      const E = this.E, I = E.inst, S = E.src, sr = E.sr, ratio = S.sr / sr;
      const X = this.note;
      const clip = this.kind === 'clip';
      // settings: a clip note carries its own resolved settings (D6); a live voice follows the inst
      const engine = clip ? (ENGINES[X.engine] ? X.engine : I.engine) : I.engine;
      const lock = engine === 'cloud' && (clip ? !!X.lock : I.lock);
      let motion = clip ? (MOTIONS[X.motion] ? X.motion : I.motion) : I.motion;
      let travel = clip ? (X.travel === 'path' ? 'path' : 'speed') : 'speed';
      const tape = clip ? !!X.tape : I.tape;
      const jumpBeats = clip ? rateBeats(X.jump) : rateBeats(I.jump);
      const kc = engine === 'tape' ? K_TAPE : engine === 'voice' ? K_VOICE : lock ? K_LOCK : K_CLOUD;
      if (kc === K_TAPE) { if (motion === 'freeze') motion = 'forward'; if (travel === 'path') travel = 'tapepath'; }
      this.engineName = lock ? 'lock' : engine; this.motion = motion;
      this.rev = clip ? !!X.rev : I.rev;
      // region
      const G = this.reg, R = I.region, n = S.n;
      G.rs = Math.floor(R.start * n);
      G.L = Math.max(64, Math.min(n - G.rs, Math.floor((R.end - R.start) * n)));
      if (G.rs + G.L > n) G.rs = Math.max(0, n - G.L);
      G.fold = motion === 'bounce';
      G.loop = R.atEnd === 'loop' && !G.fold;
      G.stop = R.atEnd === 'stop' && !G.fold;
      // seam fade: post-Region material first, else pre-Region material, else a plain wrap (see readReg)
      const Xw = G.loop ? Math.min(R.xfadeMs * S.sr / 1000, G.L * 0.25) : 0, room1 = n - (G.rs + G.L) - 3, room0 = G.rs - 1;
      if (Xw <= 0) { G.X = 0; G.post = true; }
      else if (room1 >= Xw) { G.X = Xw; G.post = true; }
      else if (room0 >= Xw) { G.X = Xw; G.post = false; }
      else { G.post = room1 >= room0; G.X = Math.max(0, Math.floor(Math.max(room1, room0))); if (G.X < 2) G.X = 0; }
      G.Lp = G.L; G.invX = G.X > 0 ? 1 / G.X : 0;
      if (G.X > 0) {
        if (G.cRs !== G.rs || G.cL !== G.L || G.cX !== G.X || G.cPost !== G.post || G.cSrc !== S) {
          G.cRs = G.rs; G.cL = G.L; G.cX = G.X; G.cPost = G.post; G.cSrc = S; G.rho = seamRho(S, G);
        }
      } else G.rho = 0;
      const L = G.L;
      // time inside the note
      let t = 0, beat;
      if (clip) {
        const u = E.uAt(kAbs);
        if (!this.released && !ff) {
          const Uend = this.U + E.effLen(X);
          // compared in samples (kOf is a pure function of the anchor): a note end that lands exactly on a control
          // tick releases on the same tick in every loop pass, whatever the float rounding of the beat clock
          if (kAbs >= E.kOf(Uend)) {
            const nid = X.chain && X.chain.nextId, nx = nid ? E.byId.get(nid) : null;
            const Un = nx ? nx.s + this.m * E.Lb : 0;
            if (nx && nx.s > X.s && Un <= Uend + 4.5 && u < Un + 0.05) this.bridging = true;
            else this.release();
          } else if (u < this.U - 1e-6) this.release();
        }
        t = X.l > 0 ? clamp((u - this.U) / X.l, 0, 1) : 1;
        beat = E.loopBeat(u);
      } else beat = E.songBeat(kAbs);
      // lanes: table (drawing + generators) or defaults, then wheels (live voices), offsets and macros
      const lv = this.lanes, tabs = this.tabs;
      if (tabs) {
        for (let li = 0; li < NL; li++) {
          const tb = tabs[li];
          if (!tb || tb.length < 1) { lv[li] = LANE_DEF[li]; continue; }
          const fi = t * (tb.length - 1), i0 = Math.floor(fi), i1 = i0 + 1 < tb.length ? i0 + 1 : i0;
          lv[li] = tb[i0] + (fi - i0) * (tb[i1] - tb[i0]);
        }
      } else for (let li = 0; li < NL; li++) lv[li] = LANE_DEF[li];
      if (!clip) {
        if (E.wheelTouched[0] && I.mod1.target !== 'off') lv[LI[I.mod1.target]] = E.wheel[0];
        if (E.wheelTouched[1] && I.mod2.target !== 'off') lv[LI[I.mod2.target]] = E.wheel[1];
      }
      const O = I.offsets, M = I.macros;
      lv[L_AMP] += O.amp; lv[L_VIB] += O.vib; lv[L_TONE] += O.tone + (M[6] - 0.5); lv[L_FORMANT] += O.formant + (M[7] - 0.5); lv[L_HOLLOW] += O.hollow;
      for (let li = 0; li < NL; li++) lv[li] = fin(clamp(lv[li], 0, 1), LANE_DEF[li]);
      // pitch: f = 440*2^((p - 69 + pitchLane + glide + vibrato + bend + inst.pitch + headTune)/12); root = inst.root + cents/100
      let glide = 0;
      const gl = clip ? X.glide : null;
      if (gl && gl.len > 0) {
        if (this.bridging) glide = gl.d;
        else { const tb = t * X.l, st0 = X.l - gl.len; if (tb > st0) glide = gl.d * gShape((tb - st0) / gl.len, gl.curve || 0); }
      }
      this.vibPh += I.vibRate * CTRL / sr; this.vibPh -= Math.floor(this.vibPh);
      const vib = lv[L_VIB] * Math.sin(TAU * this.vibPh);
      const bend = clip ? 0 : E.bend * I.bendRange;
      const rootMidi = I.root + I.cents / 100, rootHz = 440 * Math.pow(2, (rootMidi - 69) / 12);
      const semis = fin(X.p - rootMidi + (lv[L_PITCH] - 0.5) * 24 + glide + vib + bend + I.pitch, 0);
      this.semis = semis;
      const vel = clamp(fin(X.vel, 0.8), 0, 1), velGain = 1 - I.velSens * (1 - vel);
      // macros and Sample lanes
      const mSpeed = Math.pow(2, (M[0] - 0.5) * 4);
      const spread = clamp(I.spread + (M[1] - 0.5) * 2, 0, 1);
      let H = clamp(I.heads + (lv[L_HEADS] - 0.5) * 14 + (M[2] - 0.5) * 14, 1, MAX_HEADS);
      // a whole count drawn on the Heads lane arrives as e.g. 2.000000358 from its Float32 table: snap it, so no
      // phantom extra head sounds at 1e-7 (and costs a full head's budget)
      const Hn = Math.round(H); if (Math.abs(H - Hn) < 1e-5) H = Hn;
      const grainMult = Math.pow(2, (lv[L_GRAIN] - 0.5) * 4) * Math.pow(2, (M[4] - 0.5) * 4);
      const spray = clamp(I.spray + (M[5] - 0.5) * 2, 0, 1);
      this.Hf = H;
      // refresh (GrainLock's quick/slow sampling)
      const RF = I.refresh, smooth = RF.mode === 'smooth' || lv[L_REFRESH] < 0.06 - F32_EPS;
      let refEvent = false;
      const refFirst = !smooth && !this.refInit;
      if (!smooth) {
        const scale = Math.pow(2, (lv[L_REFRESH] - 0.5) * 6) * Math.pow(2, (M[3] - 0.5) * 6);
        let idx;
        if (RF.mode === 'sync') {
          const pb = Math.max(1e-4, rateBeats(RF.rate) / scale);
          if (RF.lockToBeat) idx = Math.floor(beat / pb + 1e-9);
          else { this.refPh += (CTRL / sr) * (E.bpm / 60) / pb; idx = Math.floor(this.refPh); }
        } else {
          const per = Math.max(0.005, RF.ms / 1000 / scale);
          this.refPh += CTRL / (per * sr); idx = Math.floor(this.refPh);
        }
        if (this.refInit && idx !== this.refIdx) refEvent = true;
        this.refIdx = idx; this.refInit = true;
      } else { this.refInit = false; this.refPh = 0; }
      // scatter jumps (every jump-rate beats, locked to the song beat)
      let jumpEvent = false;
      if (motion === 'scatter') {
        const ji = Math.floor(beat / jumpBeats + 1e-9);
        if (this.jInit && ji !== this.jIdx) jumpEvent = true;
        this.jIdx = ji; this.jInit = true;
      } else this.jInit = false;
      // engine switch inside a chain: restart the grain schedulers (the playhead carries)
      if (kc !== this.lastKc) { for (const h of this.heads) h.clearGrains(); this.lastKc = kc; }
      this.kc = kc;
      // start position (first note of the voice; later chain notes carry the playhead)
      const seed = I.seed;
      const pathKind = travel === 'path';
      const pathPos = lv[L_TRAVEL] * L;
      if (!this.posInit) {
        const sp = X.start || { mode: 'region', pos: 0, range: 0 };
        let p0 = clamp(fin(sp.pos, 0), 0, 1) * L;
        if (sp.mode === 'song' && clip) {
          p0 = (X.s * 60 / E.bpm) * S.sr; p0 -= L * Math.floor(p0 / L);   // song beat inside the clip: every loop pass plays the same
        } else if (sp.mode === 'random') {
          p0 = (clamp(fin(sp.pos, 0), 0, 1) + rand01(hash32(this.nh, SALT_START, fin(sp.seed, 0) | 0, seed)) * clamp(fin(sp.range, 0), 0, 1)) * L;
        }
        if (this.startPos != null) p0 = clamp(this.startPos, 0, 1) * L;
        if (pathKind) p0 = pathPos;
        for (const h of this.heads) { h.raw = p0; h.scat = 0; }
      }
      // heads
      const Hc = Math.min(MAX_HEADS, Math.ceil(H - 1e-9)), Hfl = Math.floor(H + 1e-9), invSq = 1 / Math.sqrt(H);
      const ht = HEAD_TUNE[I.headTune] || HEAD_TUNE.same;
      const s = travel === 'tapepath' ? 1 : travelSpeed(lv[L_TRAVEL]);
      const base = s * I.speed * mSpeed;
      const offA = 1 - Math.exp(-CTRL / (0.02 * sr));
      const gStepMax = CTRL / (0.02 * sr);
      let level = 0;
      for (let i = 0; i < MAX_HEADS; i++) {
        const h = this.heads[i];
        // heads that did not render last block still move, so a head that comes in lands where it would be
        if (!this.first && !ff && !h.rendered) { h.raw += h.spd * CTRL; h.off += h.dOff * CTRL; }
        h.rendered = false;
        h.want = i < Hc;
        const frac = i < Hfl ? 1 : i < Hc ? H - Hfl : 0;
        h.level = frac * invSq;
        // spread offset (slewed so it never jumps) and pan +-(spread*0.75)
        const offT = (i / H) * spread * L + h.tiny * S.sr;
        if (!this.posInit) { h.off = offT; h.dOff = 0; }
        else {   // exponential approach, at most 0.2 frames per frame (a Tape head bends by 20% at most, no chirp)
          const cap = 0.2 * CTRL * ratio;
          const o1 = h.off + clamp((offT - h.off) * offA, -cap, cap); h.dOff = (o1 - h.off) / CTRL;
        }
        const pan = Hc > 1 ? (i / (Hc - 1) * 2 - 1) * spread * 0.75 : 0;
        let pl = 1, pr = 1;
        if (pan !== 0) { const a = (pan + 1) * Math.PI / 4; pl = Math.cos(a) * Math.SQRT2; pr = Math.sin(a) * Math.SQRT2; }
        // speed and pitch per head
        const dir = motion === 'mirror' ? (i % 2 === 1 ? -1 : 1) : DIR[motion];
        const eff = clamp(base * dir * (1 + I.drift * 0.25 * h.drift), -16, 16);
        const fr = Math.pow(2, (semis + ht[i]) / 12);
        if (kc === K_TAPE) h.spd = clamp(eff * ratio * fr, -64, 64);
        else if (pathKind) h.spd = (pathPos + h.scat - h.raw) / CTRL;
        else h.spd = eff * ratio;
        const fHz = clamp(rootHz * fr, 1, sr * 0.45);
        if (kc === K_VOICE) {
          const P = Math.max(2, sr / fHz), F = Math.pow(2, (lv[L_FORMANT] - 0.5) * 24 / 12);
          const len = clamp(I.grainVoice * grainMult * (sr / rootHz) / F, 4, VOICE_GRAINS * P - 1);
          h.P = P; h.len = len; h.step = F * ratio * (tape ? Math.abs(eff) : 1); h.norm = Math.min(2, 2 * P / len);
          h.est = Math.min(VOICE_GRAINS, Math.ceil(len / P) + 1);
        } else if (kc === K_CLOUD) {
          const len = clamp(I.grainCloud * grainMult * sr / 1000, 16, 4 * sr);
          h.len = len; h.hop = len / 3; h.step = fr * ratio * (tape ? Math.abs(eff) : 1); h.norm = 2 / 3;
          h.est = Math.min(CLOUD_GRAINS, Math.ceil(len / h.hop) + 1);
        } else if (kc === K_LOCK) {
          const P = Math.max(2, sr / fHz);
          h.P = P; h.PR = P * ratio; h.dth = 1 / P;
          h.nT = clamp(Math.round(I.grainLock * grainMult), 1, LOCK_MAX_N);
          h.est = 2 * Math.max(h.nT, h.nA, h.xf >= 0 ? h.nB : 0);
        } else h.est = 2;
        h.spray = spray * Math.min(S.sr, L / 2);
        h.smooth = smooth;
        // events: scatter jumps, refresh
        if (jumpEvent) {
          const target = rand01(hash32(this.nh, i, this.jIdx, seed, SALT_JUMP)) * L;
          if (pathKind) { h.scat = target; h.spd = (pathPos + h.scat - h.raw) / CTRL; }
          else {
            if (kc === K_TAPE && h.on && !ff) { h.xRaw = h.raw; h.xLen = Math.max(1, Math.round(0.005 * sr)); h.xLeft = h.xLen; }
            h.raw = target - h.off;
          }
          h.grabRaw = h.raw; h.grab = grabPos(G, h.raw + h.off); h.regrab = true;
        } else if (refEvent) {
          const skip = rand01(hash32(this.nh, i, this.refIdx, seed, SALT_SKIP)) < RF.skip;
          if (kc === K_TAPE) {   // stutter: jump back to where the last grab began; a skipped refresh starts a new grab here
            if (skip) h.grabRaw = h.raw;
            else if (!pathKind) {
              if (h.on && !ff) { h.xRaw = h.raw; h.xLen = Math.max(1, Math.round(0.005 * sr)); h.xLeft = h.xLen; }
              h.raw = h.grabRaw;
            }
          } else if (!skip) { h.grab = grabPos(G, h.raw + h.off); h.regrab = true; }
        }
        if (!this.posInit || refFirst) { h.grabRaw = h.raw; h.grab = grabPos(G, h.raw + h.off); }
        // gains: level x pan; thinned heads fade out over 20 ms; every change is slew-limited (no clicks)
        // a head sounds only with a grant from the budget planner; a new voice grants its own heads at its first
        // tick (the planner runs right after, before any sample is rendered)
        if (this.first && !ff) h.grant = h.want ? h.est : 0;
        const act = h.want && h.allowed && h.grant > 0;
        const tL = act ? h.level * pl : 0, tR = act ? h.level * pr : 0;
        if (this.first) { h.g0L = h.g1L = tL; h.g0R = h.g1R = tR; h.dL = 0; h.dR = 0; }
        else {
          h.g0L = h.g1L; h.g0R = h.g1R;
          h.g1L = h.g0L + clamp(tL - h.g0L, -gStepMax, gStepMax);
          h.g1R = h.g0R + clamp(tR - h.g0R, -gStepMax, gStepMax);
          h.dL = (h.g1L - h.g0L) / CTRL; h.dR = (h.g1R - h.g0R) / CTRL;
        }
        if (act && !h.active) { h.needPrime = true; }
        h.active = act;
        const wasOn = h.on;
        h.on = act || h.g0L > 0 || h.g0R > 0 || h.g1L > 0 || h.g1R > 0;
        if (!h.on) { if (wasOn) h.clearGrains(); h.grant = 0; }
        const pos = h.raw + h.off;
        if (!G.stop) h.stopG = 1;
        else if (this.first) h.stopG = (pos >= 0 && pos < L) ? 1 : 0;
        const ps = h.spd + h.dOff;
        h.dir = ps > 1e-9 ? 1 : ps < -1e-9 ? -1 : 0;
        if (h.on) level += h.level;
      }
      this.posInit = true;
      // per-voice chain: hollow cut -> (formant EQ: Tape and Cloud) -> low-pass (tone) -> gain
      if (!ff) {
        const hdb = -lv[L_HOLLOW] * 28;
        this.hL.setPeak(sr, 950, 0.8, hdb); this.hR.setPeak(sr, 950, 0.8, hdb);
        const fv = lv[L_FORMANT];
        const fdb = kc === K_VOICE ? 0 : 18 * Math.abs(fv - 0.5);
        const fHz = 250 * Math.pow(2, fv * 3.8);
        this.fL.setPeak(sr, fHz, 4, fdb); this.fR.setPeak(sr, fHz, 4, fdb);
        const lpHz = 60 * Math.pow(2, lv[L_TONE] * 8.3);
        this.lL.setLow(sr, lpHz, Math.SQRT1_2); this.lR.setLow(sr, lpHz, Math.SQRT1_2);
        this.lpHz = Math.min(lpHz, sr * 0.45); this.fmtDb = fdb; this.fmtHz = fHz; this.hollowDb = hdb;
        const gT = lv[L_AMP] * lv[L_AMP] * 1.3 * velGain;
        if (this.first) { this.g0 = this.g1 = gT; this.dg = 0; }
        else { this.g0 = this.g1; this.g1 = gT; this.dg = (this.g1 - this.g0) / CTRL; }
        this.level = level * gT;
        this.first = false;
      }
    }

    // ---------------------------------------------------------------- audio
    render(outL, outR, o, n, k0) {
      let j = 0;
      while (j < n && !this.done) {
        if (this.ph === 0) { if (this.pre) this.pre = false; else this.tick(k0 + j, false); }
        const run = Math.min(n - j, CTRL - this.ph);
        this.renderRun(outL, outR, o + j, run);
        this.ph += run; if (this.ph >= CTRL) this.ph = 0;
        this.vt += run; j += run;
      }
    }
    renderRun(outL, outR, o, run) {
      const bL = this.bufL, bR = this.bufR;
      for (let j = 0; j < run; j++) { bL[j] = 0; bR[j] = 0; }
      const kc = this.kc;
      for (let i = 0; i < MAX_HEADS; i++) {
        const h = this.heads[i];
        h.peak = 0;
        if (!h.on) continue;
        h.rendered = true;
        if (kc === K_TAPE) this.runTape(h, run);
        else if (kc === K_VOICE) this.runGrains(h, run, true);
        else if (kc === K_CLOUD) this.runGrains(h, run, false);
        else this.runLock(h, run);
        if (h.peak > h.segPeak) h.segPeak = h.peak;
      }
      this.hL.run(bL, run); this.hR.run(bR, run);
      this.fL.run(bL, run); this.fR.run(bR, run);
      this.lL.run(bL, run); this.lR.run(bR, run);
      let env = this.env, st = this.st;
      const ph = this.ph, g0 = this.g0, dg = this.dg, as = this.aStep, rs = this.rStep, ss = this.sStep;
      for (let j = 0; j < run; j++) {
        if (st === ST_ATTACK) { env += as; if (env >= 1) { env = 1; st = ST_SUSTAIN; } }
        else if (st === ST_RELEASE) { env -= rs; if (env <= 0) { env = 0; this.done = true; } }
        else if (st === ST_STEAL) { env -= ss; if (env <= 0) { env = 0; this.done = true; } }
        const g = (g0 + dg * (ph + j + 1)) * env;
        outL[o + j] += bL[j] * g; outR[o + j] += bR[j] * g;
      }
      this.env = env; this.st = st;
    }
    // Tape: Hermite read at rate (f/root) x speed; Scatter jumps and Refresh stutters crossfade 5 ms.
    runTape(h, run) {
      const S = this.E.src, G = this.reg, bL = this.bufL, bR = this.bufR, ph = this.ph, L = G.L, stp = G.stop;
      const sgStep = 1 / (0.02 * this.E.sr);
      let raw = h.raw, off = h.off, sg = h.stopG;
      const spd = h.spd, dOff = h.dOff, g0L = h.g0L, g0R = h.g0R, dL = h.dL, dR = h.dR;
      const mono = !S.stereo, SL = S.L, rs = G.rs, plainEnd = Math.min(G.loop ? (G.post ? G.L : G.L - G.X) : G.L - 1, S.n - 3 - rs), _lo = rs >= 1 ? 0 : 1 - rs;
      // inline reads (mono or stereo) on the plain stretch of any loop pass; the result equals readReg's exactly
      const SRc = S.R, loopM = G.loop, xLater = G.post ? G.X : 0;
      for (let j = 0; j < run; j++) {
        const kk = ph + j + 1, gl = g0L + dL * kk, gr = g0R + dR * kk;
        let x = raw + off;
        if (stp) { sg += (x >= 0 && x < L) ? sgStep : -sgStep; sg = sg < 0 ? 0 : sg > 1 ? 1 : sg; }
        if (G.fold) x = mapPos(G, x);
        let a, b;
        let xv = x;
        if (loopM && xv >= L) { xv -= L * Math.floor(xv / L); if (xv < xLater) xv = -1e9; }
        if (xv >= _lo && xv < plainEnd) {
          const xa = rs + xv, i = xa | 0, f = xa - i;
          let p0 = SL[i - 1], p1 = SL[i], p2 = SL[i + 1], p3 = SL[i + 2];
          a = (((0.5 * (p3 - p0) + 1.5 * (p1 - p2)) * f + (p0 - 2.5 * p1 + 2 * p2 - 0.5 * p3)) * f + 0.5 * (p2 - p0)) * f + p1;
          if (mono) b = a; else { p0 = SRc[i - 1]; p1 = SRc[i]; p2 = SRc[i + 1]; p3 = SRc[i + 2]; b = (((0.5 * (p3 - p0) + 1.5 * (p1 - p2)) * f + (p0 - 2.5 * p1 + 2 * p2 - 0.5 * p3)) * f + 0.5 * (p2 - p0)) * f + p1; }
        } else { readReg(S, G, x); a = RL; b = RR; }
        if (h.xLeft > 0) {
          const w = h.xLeft / h.xLen;
          let xo = h.xRaw + off; if (G.fold) xo = mapPos(G, xo);
          readReg(S, G, xo);
          const gi = fadeIn(1 - w), go = fadeIn(w);
          a = a * gi + RL * go; b = b * gi + RR * go;
          h.xRaw += spd; h.xLeft--;
          h.peak = 2;
        } else if (h.peak < 1) h.peak = 1;
        bL[j] += a * gl * sg; bR[j] += b * gr * sg;
        raw += spd; off += dOff;
      }
      h.raw = raw; h.off = off; h.stopG = sg;
    }
    // Voice (PSOLA, formant kept) and Cloud grains: Hann windows with fractional start times; at most `grant`
    // grains per head (the planner's share of the budget); when full, the oldest grain gives way.
    runGrains(h, run, voice) {
      const E = this.E, S = E.src, G = this.reg, bL = this.bufL, bR = this.bufR, ph = this.ph, L = G.L, stp = G.stop;
      const sgStep = 1 / (0.02 * E.sr);
      const Ts = S.sr / (440 * Math.pow(2, (E.inst.root + E.inst.cents / 100 - 69) / 12));
      const rev = this.rev, seed = E.inst.seed;
      let raw = h.raw, off = h.off, sg = h.stopG;
      const spd = h.spd, dOff = h.dOff, g0L = h.g0L, g0R = h.g0R, dL = h.dL, dR = h.dR;
      const gT = h.gT, gX = h.gX, gS = h.gS, gN = h.gN, gA = h.gA;
      const cap = Math.max(1, Math.min(voice ? VOICE_GRAINS : CLOUD_GRAINS, h.grant));
      const mono = !S.stereo, SL = S.L, rs = G.rs, plainEnd = Math.min(G.loop ? (G.post ? G.L : G.L - G.X) : G.L - 1, S.n - 3 - rs), _lo = rs >= 1 ? 0 : 1 - rs;
      // inline reads (mono or stereo) on the plain stretch of any loop pass; the result equals readReg's exactly
      const SRc = S.R, loopM = G.loop, xLater = G.post ? G.X : 0;
      const hop = voice ? h.P : h.hop;
      const t0 = this.vt;
      if (h.needPrime) {   // start as if the head had been running: the grains whose windows cover now are scheduled too
        h.needPrime = false; h.gc = 0;
        h.nextT = t0 - hop * Math.floor((h.len - 1e-6) / hop);
      }
      for (let j = 0; j < run; j++) {
        const t = t0 + j;
        const x = raw + off;
        if (stp) { sg += (x >= 0 && x < L) ? sgStep : -sgStep; sg = sg < 0 ? 0 : sg > 1 ? 1 : sg; }
        if (h.active) {
          let guard = 0;
          while (h.nextT <= t && guard++ < 16) {
            // a grain's middle plays half a grain after it starts: aim it where the head will be then (no lag)
            let anchor = h.smooth ? grabPos(G, x + (spd + dOff) * h.len * 0.5) : h.grab;
            let step = h.step;
            if (voice) { const b0 = G.loop ? G.L * Math.floor(anchor / G.L) : 0; anchor = b0 + Math.round((anchor - b0) / Ts) * Ts; }   // whole root cycles from this pass's Region start
            else if (h.spray > 0) anchor += h.spray * (rand01(hash32(this.nh, h.i, h.gIdx, seed, SALT_SPRAY)) * 2 - 1);
            const span = h.len * step;
            let x0 = anchor - span / 2;
            // first pass of a loop: a grain never reads before the Region start (that would be the Region's end),
            // so a note that starts at the start plays its true attack. Voice shifts by whole cycles (stays in phase).
            if (G.loop && x0 < 0 && x >= 0 && x < L) x0 += voice ? Math.ceil(-x0 / Ts) * Ts : -x0;
            if (rev) { x0 += span; step = -step; }
            let g = h.gc;
            if (g >= cap) {   // full: replace the oldest grain (never drop the new one: spacing is the pitch)
              g = 0; for (let q = 1; q < h.gc; q++) if (gT[q] < gT[g]) g = q;
            } else h.gc++;
            gT[g] = h.nextT; gX[g] = x0; gS[g] = step; gN[g] = h.len; gA[g] = h.norm;
            h.gIdx++;
            h.nextT += hop;
          }
        } else if (h.nextT <= t) h.nextT = t + hop;
        let sL = 0, sR = 0;
        for (let g = 0; g < h.gc; g++) {
          const a = t - gT[g], ln = gN[g];
          if (a >= ln) { const e = --h.gc; gT[g] = gT[e]; gX[g] = gX[e]; gS[g] = gS[e]; gN[g] = gN[e]; gA[g] = gA[e]; g--; continue; }
          if (a <= 0) continue;
          const fi = a / ln * HANN_N, ii = fi | 0, w = (HANN[ii] + (fi - ii) * (HANN[ii + 1] - HANN[ii])) * gA[g];
          const xr = gX[g] + a * gS[g];
          let xv = xr;
          if (loopM && xv >= L) { xv -= L * Math.floor(xv / L); if (xv < xLater) xv = -1e9; }
          if (xv >= _lo && xv < plainEnd) {   // fast path: plain stretch of this pass (same maths as readAbs)
            const xa = rs + xv, i = xa | 0, f = xa - i;
            let p0 = SL[i - 1], p1 = SL[i], p2 = SL[i + 1], p3 = SL[i + 2];
            const y = (((0.5 * (p3 - p0) + 1.5 * (p1 - p2)) * f + (p0 - 2.5 * p1 + 2 * p2 - 0.5 * p3)) * f + 0.5 * (p2 - p0)) * f + p1;
            sL += y * w;
            if (mono) sR += y * w; else { p0 = SRc[i - 1]; p1 = SRc[i]; p2 = SRc[i + 1]; p3 = SRc[i + 2]; sR += ((((0.5 * (p3 - p0) + 1.5 * (p1 - p2)) * f + (p0 - 2.5 * p1 + 2 * p2 - 0.5 * p3)) * f + 0.5 * (p2 - p0)) * f + p1) * w; }
          } else { readReg(S, G, xr); sL += RL * w; sR += RR * w; }
        }
        if (h.gc > h.peak) h.peak = h.gc;
        const kk = ph + j + 1;
        bL[j] += sL * (g0L + dL * kk) * sg; bR[j] += sR * (g0R + dR * kk) * sg;
        raw += spd; off += dOff;
      }
      h.raw = raw; h.off = off; h.stopG = sg;
    }
    // Cloud + Lock (GrainLock's trick): y = sum over k of w_k * seg[(theta + k) periods], which repeats exactly every
    // period P, so any material plays in tune. Hann weights across the N-period capture remove the seam.
    lockGrab(h, x) {
      let c = grabPos(this.reg, x);
      if (h.spray > 0) c += h.spray * (rand01(hash32(this.nh, h.i, h.gIdx, this.E.inst.seed, SALT_SPRAY)) * 2 - 1);
      h.gIdx++;
      return c;
    }
    // Correlation of the old and new capture over one period (16 phases, mono), clamped to 0..1.
    lockRho(h) {
      const S = this.E.src, G = this.reg, PR = h.PR;
      let ab = 0, aa = 0, bb = 0;
      for (let q = 0; q < 16; q++) {
        const th = (q + 0.5) / 16;
        let a = 0, b = 0;
        for (let k = 0; k < h.nA; k++) { const p = th + k, w = h.nA > 1 ? hannTab(p / h.nA) : 1; readReg(S, G, h.capA + p * PR); a += (RL + RR) * w; }
        for (let k = 0; k < h.nB; k++) { const p = th + k, w = h.nB > 1 ? hannTab(p / h.nB) : 1; readReg(S, G, h.capB + p * PR); b += (RL + RR) * w; }
        a *= h.nA > 1 ? Math.sqrt(2 / h.nA) : 1; b *= h.nB > 1 ? Math.sqrt(2 / h.nB) : 1;
        ab += a * b; aa += a * a; bb += b * b;
      }
      const r = aa > 1e-20 && bb > 1e-20 ? ab / Math.sqrt(aa * bb) : 0;
      return r > 0 ? (r < 1 ? r : 1) : 0;
    }
    runLock(h, run) {
      const E = this.E, S = E.src, G = this.reg, bL = this.bufL, bR = this.bufR, ph = this.ph, L = G.L, stp = G.stop;
      const sgStep = 1 / (0.02 * E.sr);
      const half = Math.max(1, h.grant >> 1);
      let raw = h.raw, off = h.off, sg = h.stopG;
      const spd = h.spd, dOff = h.dOff, g0L = h.g0L, g0R = h.g0R, dL = h.dL, dR = h.dR, PR = h.PR, dth = h.dth;
      // fast path bounds: frames 1 .. n-3 of the source inside the Region's plain stretch (loop or not)
      const mono = !S.stereo, SL = S.L, rs = G.rs, plainEnd = Math.min(G.loop ? (G.post ? G.L : G.L - G.X) : G.L - 1, S.n - 3 - rs), _lo = rs >= 1 ? 0 : 1 - rs;
      // inline reads (mono or stereo) on the plain stretch of any loop pass; the result equals readReg's exactly
      const SRc = S.R, loopM = G.loop, xLater = G.post ? G.X : 0;
      if (h.needPrime) { h.needPrime = false; h.capA = this.lockGrab(h, raw + off); h.nA = Math.min(h.nT, half); h.xf = -1; h.th = 0; h.periods = 0; h.regrab = false; }
      for (let j = 0; j < run; j++) {
        const x = raw + off;
        if (stp) { sg += (x >= 0 && x < L) ? sgStep : -sgStep; sg = sg < 0 ? 0 : sg > 1 ? 1 : sg; }
        if (h.regrab && h.xf < 0 && h.active) {
          h.regrab = false; h.capB = this.lockGrab(h, x); h.nB = Math.max(1, Math.min(h.nT, half)); h.xf = 0;
          h.rho = this.lockRho(h);
        }
        const th = h.th;
        let N = h.nA, aL = 0, aR = 0;
        for (let k = 0; k < N; k++) {
          const p = th + k;
          let w = 1;
          if (N > 1) { const fi = p / N * HANN_N, ii = fi | 0; w = HANN[ii] + (fi - ii) * (HANN[ii + 1] - HANN[ii]); }
          const xr = h.capA + p * PR;
          let xv = xr;
          if (loopM && xv >= L) { xv -= L * Math.floor(xv / L); if (xv < xLater) xv = -1e9; }
          if (xv >= _lo && xv < plainEnd) {
            const xa = rs + xv, i = xa | 0, f = xa - i;
            let a = SL[i - 1], b = SL[i], c = SL[i + 1], e = SL[i + 2];
            const y = (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
            aL += y * w;
            if (mono) aR += y * w; else { a = SRc[i - 1]; b = SRc[i]; c = SRc[i + 1]; e = SRc[i + 2]; aR += ((((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b) * w; }
          } else { readReg(S, G, xr); aL += RL * w; aR += RR * w; }
        }
        let gn = N > 1 ? Math.sqrt(2 / N) : 1;
        aL *= gn; aR *= gn;
        let load = N;
        if (h.xf >= 0) {
          N = h.nB; let bl = 0, br = 0;
          for (let k = 0; k < N; k++) {
            const p = th + k;
            let w = 1;
            if (N > 1) { const fi = p / N * HANN_N, ii = fi | 0; w = HANN[ii] + (fi - ii) * (HANN[ii + 1] - HANN[ii]); }
            const xr = h.capB + p * PR;
            let xv = xr;
            if (loopM && xv >= L) { xv -= L * Math.floor(xv / L); if (xv < xLater) xv = -1e9; }
            if (xv >= _lo && xv < plainEnd) {
              const xa = rs + xv, i = xa | 0, f = xa - i;
              let a = SL[i - 1], b = SL[i], c = SL[i + 1], e = SL[i + 2];
              const y = (((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b;
              bl += y * w;
              if (mono) br += y * w; else { a = SRc[i - 1]; b = SRc[i]; c = SRc[i + 1]; e = SRc[i + 2]; br += ((((0.5 * (e - a) + 1.5 * (b - c)) * f + (a - 2.5 * b + 2 * c - 0.5 * e)) * f + 0.5 * (c - a)) * f + b) * w; }
            } else { readReg(S, G, xr); bl += RL * w; br += RR * w; }
          }
          gn = N > 1 ? Math.sqrt(2 / N) : 1;
          const xw = h.xf, gi = fadeIn(xw), go = fadeIn(1 - xw);
          // flat-power correction from the two captures' correlation (GrainLock's seam rule): correlated
          // material crossfades at constant amplitude, uncorrelated at constant power
          const kc = 1 / Math.sqrt(1 + 2 * h.rho * gi * go);
          aL = (aL * go + bl * gn * gi) * kc; aR = (aR * go + br * gn * gi) * kc;
          load += N;
          h.xf += dth;
          if (h.xf >= 1) { h.capA = h.capB; h.nA = h.nB; h.xf = -1; h.periods = 0; }
        }
        if (load > h.peak) h.peak = load;
        h.th = th + dth;
        if (h.th >= 1) { h.th -= 1; h.periods++; if (h.smooth && h.periods >= h.nA && h.xf < 0) h.regrab = true; }
        const kk = ph + j + 1;
        bL[j] += aL * (g0L + dL * kk) * sg; bR[j] += aR * (g0R + dR * kk) * sg;
        raw += spd; off += dOff;
      }
      h.raw = raw; h.off = off; h.stopG = sg;
    }
  }

  // ------------------------------------------------------------------ the engine
  class Engine {
    constructor(opts) {
      opts = opts || {};
      this.sr = clamp(fin(opts.sampleRate, 48000), 8000, 384000);
      this.inst = normInst(opts.inst);
      this.outGainLin = 1;
      this.src = null; this.srcName = '';
      this.snap = null; this.notes = []; this.byId = new Map(); this.Lb = 16;
      this.voices = []; this.serial = 0; this.liveCount = 0; this.liveStack = [];
      this.k = 0;                                   // samples rendered since the engine was made
      this.playing = false; this.loop = true; this.bpm = 120; this.db = 2 / this.sr;
      this.anchorBeat = 0; this.anchorK = 0;        // unwrapped song beat at sample anchorK (playing)
      this.freeBeat0 = 0; this.freeK0 = 0;          // free-running beat clock while stopped (Scatter, synced Refresh on live notes)
      this.pendingChase = false; this.playStartK = 0;
      this.wheel = [0, 0]; this.wheelTouched = [false, false]; this.bend = 0; this.sustain = false;
      this.budget = Math.max(16, Math.floor(BUDGET_48K * 48000 / this.sr));
      this.limG = 1; this.limRel = 1 - Math.exp(-1 / (0.08 * this.sr));
      this.ev = []; this.waitList = [];
      this.st = { load: 0, maxLoad: 0, cuts: 0, steals: 0, thinned: 0 };
    }
    // ---------------------------------------------------------------- inputs
    setSample(s) {
      s = s || {};
      const ch = Array.isArray(s.channels) ? s.channels : [];
      const toF = a => (a instanceof Float32Array ? a : a && a.length ? Float32Array.from(a, x => fin(x, 0)) : null);
      const L = toF(ch[0]);
      for (const v of this.voices) v.steal();
      if (!L || L.length < 8) { this.src = null; return; }
      let R = toF(ch[1]);
      if (R && R.length !== L.length) R = null;
      let stereo = !!R && R !== L;
      if (stereo) { stereo = false; for (let i = 0; i < L.length; i++) if (L[i] !== R[i]) { stereo = true; break; } }
      this.src = { L, R: stereo ? R : L, n: L.length, sr: clamp(fin(s.sampleRate, this.sr), 4000, 768000), stereo };
      this.srcName = String(s.name || '');
    }
    setInst(inst) {
      this.inst = normInst(inst);
      this.outGainLin = Math.pow(10, this.inst.outGain / 20);
    }
    setSnapshot(snap) {
      this.snap = snap || null;
      const ok = n => n && n.id != null && isFinite(n.p) && isFinite(n.s) && isFinite(n.l) && n.l > 0;
      this.notes = snap && Array.isArray(snap.notes) ? snap.notes.filter(ok) : [];
      this.byId = new Map(this.notes.map(n => [n.id, n]));
      this.Lb = Math.max(1, fin(snap && snap.bars, 4)) * Math.max(1, fin(snap && snap.beatsPerBar, 4));
      for (const v of this.voices) {
        if (v.kind !== 'clip' || v.done) continue;
        const nx = this.byId.get(v.note.id);
        if (!nx) { v.release(); continue; }   // deleted notes release
        v.setNote(nx); v.U = nx.s + v.m * this.Lb;   // edited notes follow at once
      }
    }
    setTransport(t) {
      t = t || {};
      const playing = t.playing !== undefined ? !!t.playing : this.playing;
      const bpm = clamp(fin(t.bpm, this.bpm), 10, 999);
      const loop = t.loop !== undefined ? !!t.loop : this.loop;
      const beat = fin(t.beat, NaN);
      const wrap = b => (loop ? b - this.Lb * Math.floor(b / this.Lb) : Math.max(0, b));
      if (playing && !this.playing) {
        this.bpm = bpm; this.db = bpm / 60 / this.sr; this.loop = loop;
        this.anchorBeat = beat === beat ? wrap(beat) : 0; this.anchorK = this.k;
        this.playing = true; this.pendingChase = true; this.playStartK = this.k;
      } else if (!playing && this.playing) {
        const b = this.loopBeat(this.uAt(this.k));
        this.playing = false;
        for (const v of this.voices) if (v.kind === 'clip') v.release();   // stopping releases clip voices
        this.bpm = bpm; this.db = bpm / 60 / this.sr; this.loop = loop;
        this.freeBeat0 = beat === beat ? beat : b; this.freeK0 = this.k;
      } else if (playing) {
        const cur = this.uAt(this.k), curL = this.loopBeat(cur);
        if (beat === beat) {
          let d = beat - curL;
          if (loop) d -= this.Lb * Math.round(d / this.Lb);
          if (Math.abs(d) > 0.1) {   // a jump: release clip voices and chase at the new place
            for (const v of this.voices) if (v.kind === 'clip') v.release();
            this.bpm = bpm; this.db = bpm / 60 / this.sr; this.loop = loop;
            this.anchorBeat = wrap(beat); this.anchorK = this.k; this.pendingChase = true; this.playStartK = this.k;
            return;
          }
        }
        if (bpm === this.bpm && loop === this.loop) return;   // re-sent unchanged: the clock stays exactly as it is
        this.anchorBeat = cur; this.anchorK = this.k;   // tempo or loop change: the song position stays continuous
        this.bpm = bpm; this.db = bpm / 60 / this.sr; this.loop = loop;
      } else {
        if (!(beat === beat) && bpm === this.bpm && loop === this.loop) return;
        const b = this.songBeat(this.k);
        this.bpm = bpm; this.db = bpm / 60 / this.sr; this.loop = loop;
        this.freeBeat0 = beat === beat ? beat : b; this.freeK0 = this.k;
      }
    }
    noteOn(p, vel, opts) {
      p = clamp(Math.round(fin(p, 60)), 0, 127);
      vel = fin(vel, 0.8); if (vel > 1) vel /= 127; vel = clamp(vel, 0, 1);
      if (!this.src) return null;
      if (vel <= 0) { this.noteOff(p); return null; }
      const I = this.inst;
      if (I.liveLegato) {   // a new key while another is held carries its playhead
        let held = null;
        for (const v of this.voices) if (v.kind === 'live' && !v.released && !v.done && (!held || v.serial > held.serial)) held = v;
        if (held) {
          this.liveStack = this.liveStack.filter(q => q !== p); this.liveStack.push(p);
          held.note = Object.assign({}, held.note, { p, vel }); held.key = p;
          return held;
        }
      }
      for (const v of this.voices) if (v.kind === 'live' && v.key === p && !v.released) v.release();
      const note = { id: 'live:' + p, idHash: idHash('live:' + p), p, s: 0, l: Infinity, vel, start: { mode: 'region', pos: 0, range: 0 }, lanes: null, glide: null, chain: null };
      const v = new Voice(this, 'live', note, 0, 0, this.k);
      v.key = p;
      if (opts && opts.startPos != null) v.startPos = clamp(fin(opts.startPos, 0), 0, 1);
      this.addVoice(v);
      this.liveStack = this.liveStack.filter(q => q !== p); this.liveStack.push(p);
      return v;
    }
    noteOff(p) {
      p = clamp(Math.round(fin(p, 60)), 0, 127);
      this.liveStack = this.liveStack.filter(q => q !== p);
      for (const v of this.voices) {
        if (v.kind !== 'live' || v.released || v.key !== p) continue;
        if (this.inst.liveLegato && this.liveStack.length) {   // fall back to the last key still held, same playhead
          const q = this.liveStack[this.liveStack.length - 1];
          v.note = Object.assign({}, v.note, { p: q }); v.key = q;
        } else if (this.sustain) v.sustained = true;
        else v.release();
      }
    }
    allNotesOff() {
      for (const v of this.voices) v.steal();
      this.liveStack = [];
    }
    setController(name, value) {
      if (name === 'mod1' || name === 'mod2') {
        const i = name === 'mod1' ? 0 : 1;
        if (value == null || value !== value) { this.wheelTouched[i] = false; return; }   // null = let go (lanes rule again)
        this.wheel[i] = clamp(fin(value, 0), 0, 1); this.wheelTouched[i] = true;
      } else if (name === 'bend') this.bend = clamp(fin(value, 0), -1, 1);
      else if (name === 'sustain') {
        this.sustain = !!value && value >= 0.5;
        if (!this.sustain) for (const v of this.voices) if (v.kind === 'live' && v.sustained && !v.released) { v.sustained = false; if (!this.liveStack.includes(v.key)) v.release(); }
      }
    }
    // ---------------------------------------------------------------- clocks
    uAt(k) { return this.anchorBeat + (k - this.anchorK) * this.db; }
    loopBeat(u) { return this.loop ? u - this.Lb * Math.floor(u / this.Lb) : u; }
    songBeat(k) { return this.playing ? this.loopBeat(this.uAt(k)) : this.freeBeat0 + (k - this.freeK0) * this.db; }
    // first sample at or after unwrapped beat U (a pure function of the anchor, so block sizes never change it)
    kOf(U) { return this.anchorK + Math.ceil((U - this.anchorBeat) / this.db - 1e-7); }
    effLen(X) { return this.loop ? Math.min(X.l, this.Lb - X.s) : X.l; }
    // ---------------------------------------------------------------- voices
    addVoice(v, kNow) {
      const k0 = kNow == null ? this.k : kNow;
      const poly = this.inst.polyphony;
      let count = 0;
      for (const w of this.voices) if (!w.done && w.st !== ST_STEAL) count++;
      while (count >= poly) {   // steal the oldest (released ones first), 5 ms fade
        let victim = null;
        for (const w of this.voices) {
          if (w.done || w.st === ST_STEAL) continue;
          if (!victim || (w.released && !victim.released) || (w.released === victim.released && w.serial < victim.serial)) victim = w;
        }
        if (!victim) break;
        victim.steal(); this.st.steals++; count--;
      }
      this.voices.push(v);
      v.tick(k0, false); v.pre = true;   // first control tick at its own start sample, so the planner sees its heads
      this.plan();
    }
    scanStarts(k0, k1, out) {
      out.length = 0;
      if (!this.playing || !this.notes.length) return;
      const db = this.db, u0 = this.uAt(k0) - 2 * db, u1 = this.uAt(k1) + 2 * db, Lb = this.Lb;
      for (const X of this.notes) {
        if (this.loop && (X.s >= Lb || X.s < 0)) continue;
        let mA = 0, mB = 0;
        if (this.loop) { mA = Math.max(0, Math.floor((u0 - X.s) / Lb)); mB = Math.floor((u1 - X.s) / Lb); }
        for (let m = mA; m <= mB; m++) {
          const U = X.s + m * Lb;
          if (U < u0 || U > u1) continue;
          const kS = this.kOf(U);
          if (kS >= k0 && kS < k1 && kS >= this.playStartK) out.push({ kS, X, U, m });
        }
      }
      if (out.length > 1) out.sort((a, b) => a.kS - b.kS || a.X.s - b.X.s || a.X.p - b.X.p);
    }
    startClip(e) {
      const X = e.X, pid = !X.restart && X.chain && X.chain.prevId;
      if (pid) {   // a chain plays on ONE voice: hand over, no new attack (D2)
        for (const v of this.voices) {
          if (v.kind === 'clip' && !v.released && !v.done && v.note.id === pid && v.m === e.m) { v.switchTo(X, e.U); return v; }
        }
      }
      const v = new Voice(this, 'clip', X, e.U, e.m, e.kS);
      this.addVoice(v, e.kS);
      return v;
    }
    // Chase: starting the transport inside a note starts it part-way, at the right lane time and playhead position.
    doChase(k) {
      if (!this.playing || !this.notes.length) return;
      const u = this.uAt(k), Lb = this.Lb;
      for (const X of this.notes) {
        const eff = this.effLen(X);
        if (eff <= 0) continue;
        const m = this.loop ? Math.floor((u - X.s) / Lb) : 0;
        if (m < 0) continue;
        const U = X.s + m * Lb;
        if (!(u < U + eff) || this.kOf(U) >= k) continue;
        let first = X, Uf = U, cur;
        for (let g = 0; g < 256; g++) {
          const pv = first.chain && first.chain.prevId ? this.byId.get(first.chain.prevId) : null;
          if (!pv || !(pv.s < first.s)) break;
          first = pv; Uf = pv.s + m * Lb;
        }
        cur = first;
        for (let g = 0; g < 256; g++) {
          const nx = cur.chain && cur.chain.nextId ? this.byId.get(cur.chain.nextId) : null;
          if (!nx || !(nx.s > cur.s) || nx.s + m * Lb > u) break;
          cur = nx;
        }
        if (cur !== X) continue;
        const v = new Voice(this, 'clip', first, Uf, m, this.kOf(Uf));
        v.chase(k);
        v.ffChain(k);
        this.addVoice(v, k);
      }
    }
    // Budget (D3): at most 192 grains + Lock taps per 48 kHz sample (scaled by 48000/sr). Over budget, the top
    // (fractional first) heads of the oldest and quietest voices fade out over 20 ms; the newest note never thins.
    // Steady-state heads are planned to 75% of the budget so the 20 ms fade tails fit under the hard limit.
    plan() {
      const B = this.budget, soft = Math.floor(B * 0.75), vs = this.voices;
      let newest = null;
      for (const v of vs) if (!v.done && !v.released && (!newest || v.serial > newest.serial)) newest = v;
      if (!newest) for (const v of vs) if (!v.done && (!newest || v.serial > newest.serial)) newest = v;
      let steady = 0, fading = 0, thinned = 0;
      for (const v of vs) {
        if (v.done) continue;
        for (const h of v.heads) {
          if (h.want && h.allowed) steady += h.est;
          else if (h.on) fading += h.grant;
          if (h.want && !h.allowed) thinned++;
        }
      }
      if (thinned) {   // room again: bring thinned heads back, newest voices first (with hysteresis)
        const order = vs.filter(v => !v.done).sort((a, b) => b.serial - a.serial);
        for (const v of order) for (const h of v.heads) {
          if (h.want && !h.allowed && steady + h.est <= soft * 0.9) { h.allowed = true; steady += h.est; if (h.on) fading -= h.grant; }
        }
      }
      if (steady > soft) {
        // released voices first, then silent ones (Volume lane or envelope at -60 dB or less), then the oldest
        const quiet = v => (v.level * v.env < 1e-3 ? 1 : 0);
        const cand = vs.filter(v => !v.done && v !== newest).sort((a, b) =>
          (b.released - a.released) || (quiet(b) - quiet(a)) || (a.serial - b.serial));
        // a head that has not sounded yet (its voice starts at this very sample, e.g. the other notes of a chord) goes
        // silent at once: it has no tail to fade
        const thin = (h, v) => {
          h.allowed = false; steady -= h.est;
          if (!h.on) return;
          if (v.vt === 0) { h.on = false; h.active = false; h.grant = 0; h.g0L = h.g1L = h.g0R = h.g1R = 0; h.dL = h.dR = 0; h.clearGrains(); }
          else fading += h.grant;
        };
        outer1: for (const v of cand) for (let i = MAX_HEADS - 1; i >= 1; i--) {
          if (steady <= soft) break outer1;
          const h = v.heads[i]; if (h.want && h.allowed) thin(h, v);
        }
        for (const v of cand) { if (steady <= soft) break; const h = v.heads[0]; if (h.want && h.allowed) thin(h, v); }
        if (steady > B && newest) for (let i = MAX_HEADS - 1; i >= 1 && steady > B; i--) { const h = newest.heads[i]; if (h.want && h.allowed) thin(h, newest); }
      }
      // Grants. A head that is already sounding keeps its share. A head that has not sounded yet (a new voice's
      // heads, a head the Heads lane adds, a thinned head coming back) waits until the 20 ms fade tails leave
      // room, newest voice first, so thinning always fades and never cuts. A waiting head comes in within 20 ms
      // with its own 20 ms fade-in; it is not thinned. Head 1 of a voice that has not started never waits.
      let load = fading;
      const wait = this.waitList; wait.length = 0;
      for (const v of vs) {
        if (v.done) continue;
        for (const h of v.heads) {
          if (!(h.want && h.allowed)) continue;
          if (h.on && h.grant > 0 && v.vt > 0) { h.grant = h.est; load += h.est; } else wait.push(h, v);
        }
      }
      if (wait.length > 2) {
        const pairs = []; for (let q = 0; q < wait.length; q += 2) pairs.push([wait[q], wait[q + 1]]);
        pairs.sort((a, b) => b[1].serial - a[1].serial || a[0].i - b[0].i);
        wait.length = 0; for (const p of pairs) wait.push(p[0], p[1]);
      }
      for (let q = 0; q < wait.length; q += 2) {
        const h = wait[q], v = wait[q + 1];
        if (load + h.est <= B || (h.i === 0 && v.vt === 0)) { h.grant = h.est; load += h.est; }
        else if (h.grant > 0 || h.on) {
          h.grant = 0;
          if (v.vt === 0) { h.on = false; h.active = false; h.g0L = h.g1L = h.g0R = h.g1R = 0; h.dL = h.dR = 0; h.clearGrains(); }
        }
      }
      if (load > B) {   // last resort (only reachable when new voices' first heads alone do not fit): cut the oldest tails
        const order = vs.filter(v => !v.done).sort((a, b) => (b.released - a.released) || (a.serial - b.serial));
        outer2: for (const v of order) for (let i = MAX_HEADS - 1; i >= 0; i--) {
          if (load <= B) break outer2;
          const h = v.heads[i];
          if (h.on && !(h.want && h.allowed)) {
            load -= h.grant; h.on = false; h.active = false; h.grant = 0; h.g0L = h.g1L = h.g0R = h.g1R = 0; h.dL = h.dR = 0; h.clearGrains(); this.st.cuts++;
          }
        }
      }
      let th = 0;
      for (const v of vs) if (!v.done) for (const h of v.heads) if (h.want && !h.allowed) th++;
      this.st.thinned = th;
    }
    // ---------------------------------------------------------------- audio
    process(outL, outR, n) {
      n = n | 0;
      if (n <= 0) return;
      for (let j = 0; j < n; j++) { outL[j] = 0; outR[j] = 0; }
      if (!this.src) { this.k += n; return; }
      const ev = this.ev;
      let i = 0;
      while (i < n) {
        const k = this.k;
        if (this.pendingChase) { this.pendingChase = false; this.doChase(k); }
        const seg = Math.min(n - i, CTRL - (k % CTRL));
        this.scanStarts(k, k + seg, ev);
        let pos = 0, ei = 0, needPlan = k % CTRL === 0;
        while (pos < seg) {
          while (ei < ev.length && ev[ei].kS === k + pos) { this.startClip(ev[ei]); ei++; needPlan = false; }
          if (needPlan) { this.plan(); needPlan = false; }
          const nextE = ei < ev.length ? ev[ei].kS - k : seg;
          const len = nextE - pos;
          if (len > 0) {
            for (const v of this.voices) if (!v.done) v.render(outL, outR, i + pos, len, k + pos);
            this.measure();
          }
          pos = nextE;
        }
        let anyDone = false;
        for (const v of this.voices) if (v.done) { anyDone = true; break; }
        if (anyDone) { let w = 0; for (const v of this.voices) if (!v.done) this.voices[w++] = v; this.voices.length = w; }
        this.k += seg; i += seg;
      }
      // output: outGain, soft limiter (instant attack, 80 ms release, ceiling 0.9), never NaN/inf, peak <= 0.98
      const g = this.outGainLin, T = 0.9, rel = this.limRel;
      let lg = this.limG;
      for (let j = 0; j < n; j++) {
        let l = outL[j] * g, r = outR[j] * g;
        if (!(l > -1e6 && l < 1e6)) l = 0;
        if (!(r > -1e6 && r < 1e6)) r = 0;
        const al = l < 0 ? -l : l, ar = r < 0 ? -r : r, pk = al > ar ? al : ar;
        const tg = pk > T ? T / pk : 1;
        if (tg < lg) lg = tg; else lg += (tg - lg) * rel;
        l *= lg; r *= lg;
        outL[j] = l > 0.98 ? 0.98 : l < -0.98 ? -0.98 : l;
        outR[j] = r > 0.98 ? 0.98 : r < -0.98 ? -0.98 : r;
      }
      this.limG = lg;
    }
    // Measured load: the sum of each head's peak load inside the stretch just rendered (no planner action happens
    // inside a stretch), an upper bound of the true per-sample sum of active grains plus Lock taps.
    measure() {
      let load = 0;
      for (const v of this.voices) for (const h of v.heads) { load += h.segPeak; h.segPeak = 0; }
      this.st.load = load; if (load > this.st.maxLoad) this.st.maxLoad = load;
    }
    // ---------------------------------------------------------------- display
    getDisplay() {
      const S = this.src, voices = [], grains = [];
      for (const v of this.voices) {
        if (v.done || !S) continue;
        const G = v.reg, heads = [];
        for (const h of v.heads) {
          if (!h.on) continue;
          heads.push({ i: h.i, pos: (G.rs + mapPos(G, h.raw + h.off)) / S.n, dir: h.dir, level: 0.5 * (h.g1L + h.g1R) * h.stopG, thinned: h.want && !h.allowed });
          if (grains.length < 128) {
            if (v.kc === K_LOCK) grains.push({ pos: (G.rs + mapPos(G, h.capA)) / S.n, age: h.th });
            else for (let g = 0; g < h.gc && grains.length < 128; g++) {
              const a = Math.max(0, v.vt - h.gT[g]);
              grains.push({ pos: (G.rs + mapPos(G, h.gX[g] + a * h.gS[g])) / S.n, age: clamp(a / h.gN[g], 0, 1) });
            }
          }
        }
        voices.push({ noteId: v.note.id, p: v.note.p, kind: v.kind, engine: v.engineName, motion: v.motion, env: v.env, released: v.released, heads });
      }
      return { beat: this.songBeat(this.k), playing: this.playing, voices, grains };
    }
    stats() {
      let voices = 0; for (const v of this.voices) if (!v.done) voices++;
      return { voices, grains: this.st.load, thinnedHeads: this.st.thinned, maxGrains: this.st.maxLoad, budget: this.budget, cuts: this.st.cuts, steals: this.st.steals };
    }
    resetStats() { this.st.maxLoad = 0; this.st.cuts = 0; this.st.steals = 0; }
  }

  return {
    Engine, rateBeats, version,
    // shared definitions, exported for tests and the page (same maths as SLModel)
    hash32, rand01, idHash, travelSpeed, gShape, defaultInst, normInst, F32_EPS,
    LANE_IDS: LANE_IDS.slice(), LANE_DEF: LANE_DEF.slice(), HEAD_TUNE, CTRL, MAX_HEADS, BUDGET_48K, LOCK_MAX_N,
  };
}

// AudioWorklet glue: self-contained except for calling createStrandEngineModule(). The page builds a Blob of
// createStrandEngineModule.toString() + strandWorkletGlue.toString() + 'strandWorkletGlue();'.
function strandWorkletGlue() {
  const SE = createStrandEngineModule();
  registerProcessor('strand-engine', class extends AudioWorkletProcessor {
    constructor() {
      super();
      this.eng = new SE.Engine({ sampleRate });
      this.count = 0; this.every = Math.max(128, Math.round(sampleRate / 30));
      this.tmpR = new Float32Array(128);
      this.port.onmessage = e => {
        const m = e.data || {}, E = this.eng;
        try {
          switch (m.type) {
            case 'sample': E.setSample(m.sample || m); break;
            case 'inst': E.setInst(m.inst || m.data); break;
            case 'snapshot': E.setSnapshot(m.snapshot || m.snap || m.data); break;
            case 'transport': E.setTransport(m.transport || m); break;
            case 'noteOn': E.noteOn(m.p, m.vel, m.opts); break;
            case 'noteOff': E.noteOff(m.p); break;
            case 'ctrl': E.setController(m.name, m.value); break;
            case 'allOff': E.allNotesOff(); break;
          }
        } catch (err) { this.port.postMessage({ type: 'error', message: String(err && err.message || err) }); }
      };
    }
    process(inputs, outputs) {
      const out = outputs[0];
      if (!out || !out[0]) return true;
      const L = out[0];
      let R = out[1];
      if (!R) { if (this.tmpR.length < L.length) this.tmpR = new Float32Array(L.length); R = this.tmpR; }
      this.eng.process(L, R, L.length);
      this.count += L.length;
      if (this.count >= this.every) {
        this.count = 0;
        const d = this.eng.getDisplay();
        d.type = 'display'; d.stats = this.eng.stats();
        this.port.postMessage(d);
      }
      return true;
    }
  });
}

if (typeof module !== 'undefined') module.exports = { createStrandEngineModule, strandWorkletGlue };
