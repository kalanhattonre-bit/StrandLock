// tools/fixtures/state.js: Tests/fixtures/state/prototype-*.json (four prototype saves) and state/import.json, what the
// plugin's M8 import makes of them (PLUGIN-PLAN section 3, "Prototype saves"). Owned by M8.
// The v4 path is the prototype's own (sl-ui.js init(): normalizeClip, normalizeInst, then loadSound(saved starter,
// {keepRegion: !soundFile}), whose switchSound puts the starter's root 60 and cents 0 into the inst and, when the session
// ended on a user file, the whole file as the Region). The v3 mapping is the plan's rule (no prototype code maps a v3
// save's instrument fields), applied before normalizeInst; every value then follows SLModel.
'use strict';
const C = require('./common.js');

// No real browser saves were available, so these four are written in each prototype's own save format (v3: the
// object its save() put under localStorage 'strandlock_v3'; v4: what sl-ui.js stores under 'strandlock_v4') with
// fixed ids, as a session would leave them. They are the import inputs; state/import.json holds what M8 makes of them.
function stateSaves(M) {
  const v3node = (t, v, lin) => (lin === undefined ? { t, v } : { t, v, lin });
  const v3note = (id, p, s, l, c, extra) => Object.assign({ id, p, s, l, glide: null, gc: 0, glideTo: null, c: Object.assign({
    pitch: [v3node(0, 0.5), v3node(1, 0.5)], amp: [v3node(0, 0.8), v3node(1, 0.8)], vib: [v3node(0, 0), v3node(1, 0)],
    tone: [v3node(0, 0.75), v3node(1, 0.75)], formant: [v3node(0, 0.5), v3node(1, 0.5)], hollow: [v3node(0, 0), v3node(1, 0)] }, c || {}) }, extra || {});
  const sine = (cyc, center, d0, d1) => { const steps = Math.round(cyc * 4), ph = [0, 1, 0, -1], out = []; for (let i = 0; i <= steps; i++) { const x = i / steps; out.push(v3node(x, center + ph[i % 4] * (d0 + (d1 - d0) * x) / 2, false)); } return out; };
  const tri = cyc => { const steps = Math.round(cyc * 4), ph = [0, 1, 0, -1], out = []; for (let i = 0; i <= steps; i++) { const x = i / steps; out.push(v3node(x, 0.5 + ph[i % 4] * 0.2, true)); } return out; };
  const v3a = { dark: true, bpm: 96, bars: 4, glideMode: 'legato', glideTime: 0.5, m1Lane: 'vib', m2Lane: 'tone', cc1: 1, cc2: 2, key: 0, scale: 'Chromatic', lock: false, engine: 'formant',
    notes: [
      v3note('k3j9x2a', 55, 0, 4, { tone: [v3node(0, 0.25), v3node(1, 0.9)], amp: [v3node(0, 0.35), v3node(0.6, 0.9), v3node(1, 0.75)] }),
      v3note('p0q8r7s', 60, 0, 4, { tone: [v3node(0, 0.25), v3node(1, 0.9)] }),
      v3note('b7c6d5e', 36, 0, 4, { formant: sine(4, 0.5, 0.1, 0.7) }),
      v3note('f4g3h2i', 33, 4, 4, {}, { glide: 0 }),
      v3note('lead001', 84, 6, 2, { pitch: [v3node(0, 0.5 - 2 / 24), v3node(0.3, 0.5), v3node(1, 0.5)], vib: [v3node(0, 0), v3node(0.35, 0), v3node(1, 0.7)] }, { glideTo: 'lead002', glide: 1 }),
      v3note('lead002', 79, 10, 2, { vib: [v3node(0, 0.2), v3node(1, 0.5)] }),
      v3note('m1n2o3p', 72, 8.5, 0.5, { amp: tri(2), tone: [v3node(0, 0.9, true), v3node(1, 0.3, true)] }, { glide: 0 }),
    ] };
  const v3b = { dark: false, bpm: 140, bars: 2, glideMode: 'always', glideTime: 0.25, m1Lane: 'formant', m2Lane: 'none', cc1: 11, cc2: 74, key: 9, scale: 'Minor', lock: true, engine: 'classic',
    notes: [
      v3note('aaaaaaa', 57, 0, 1, { hollow: [v3node(0, 0.05), v3node(0.5, 1), v3node(1, 0.05)] }),
      v3note('bbbbbbb', 60, 1, 1, {}, { gc: 0.5, glideTo: 'ccccccc' }),
      v3note('ccccccc', 64, 2, 2, { pitch: [v3node(0, 0.5), v3node(0.5, 0.6, true), v3node(1, 0.4)] }),
      v3note('ddddddd', 69, 4, 3.5, { amp: [v3node(0.2, 0.9), v3node(0, 0.1)] }),   // unsorted, as an edited v3 lane could be
      v3note('eeeeeee', 45, 6, 1.5, {}, { glideTo: 'gone123' }),                      // a link to a deleted note
    ] };
  const demo = M.loadClipPreset('strand-demo');
  const INIT = M.applySoundPreset(M.defaultInst(), 'init').inst;
  const v4a = { v: 4, clip: demo, inst: INIT, bpm: 96, snap: 0.25, loop: true, mode: 1, subLane: 'travel', tab: 'sample', folded: false, sound: 'sawopen', soundFile: false };
  const lead = M.applyRecipe(M.loadClipPreset('legato-lead'), M.applySoundPreset(M.defaultInst(), 'pad-drift').inst, 6, null);
  const lc = M.setNoteSample(lead.clip, ['legato-lead-3', 'legato-lead-4'], { engine: 'tape', start: { mode: 'random', seed: 3 } });
  // a session that ended on a user file: its detected root and its Region must not carry over to the starter
  const v4b = { v: 4, clip: lc, inst: M.mergeInst(lead.inst, { region: { start: 0.25, end: 0.6, atEnd: 'hold' }, root: 57, cents: -12, seed: 7, key: 2, scale: 'Dorian', scaleLock: true,
    mod1: { target: 'heads', cc: 21 }, macros: [0.7, 0.5, 0.5, 0.5, 0.5, 0.5, 0.3, 0.5] }),
    bpm: 300, snap: 0.3, loop: false, mode: 8, subLane: 'grain', tab: 'mods', folded: true, sound: 'notasound', soundFile: true };
  const raw = x => JSON.parse(JSON.stringify(x));   // what JSON.stringify left in localStorage
  const saves = { 'state/prototype-v3-demo.json': v3a, 'state/prototype-v3-edited.json': v3b, 'state/prototype-v4-demo.json': raw(v4a), 'state/prototype-v4-session.json': raw(v4b) };
  const files = {};
  for (const k of Object.keys(saves)) files[k] = { raw: JSON.stringify(saves[k]) + '\n' };   // compact, as localStorage holds it
  return { files, saves };
}

const GRID = [1, 0.5, 0.25, 0.125, 0];   // sl-ui.js: the snap values a v4 save may hold (0 = free)
const UI_TABS = ['sample', 'macros', 'note', 'glide', 'copy', 'mods', 'sound'];   // the data-t values of shell.html's tab buttons
const num = (x, d) => (typeof x === 'number' && isFinite(x) ? x : d), clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
// the plugin's clip settings for an import: the prototype had none of these but loop (plan 3: plugin defaults)
const clipSettings = loop => ({ beatsPerBar: 4, offsetBeats: 0, loop, tieAcrossLoop: true });
// where each inst field lands in the plugin: parameters (through ParamBridge::applyPatch) or the INST chunk (plan 2.2)
const STATE_FIELDS = ['seed', 'mod1', 'mod2', 'key', 'scale', 'scaleLock'];
function split(inst) {
  const params = {}, INST = {};
  for (const k of Object.keys(inst)) (STATE_FIELDS.includes(k) ? INST : params)[k] = inst[k];
  return { params: Object.keys(params), INST };
}
// the starter's sound rule (sl-ui.js switchSound): a starter is rooted at C3; keepRegion only for the saved starter
function afterSound(M, inst, keepRegion) {
  const patch = { root: 60, cents: 0 };
  if (!keepRegion) patch.region = { start: 0, end: 1 };
  return M.normalizeInst(M.mergeInst(inst, patch));
}
// v3 -> v4 (plan 3): engine formant -> voice, classic -> tape; lock -> scaleLock (v3's lock is the scale lock; v4's lock
// is Cloud's Lock); m1Lane/m2Lane -> mod1/mod2.target with 'none' -> 'off'; cc1/cc2 -> mod1/mod2.cc; glideMode,
// glideTime, key, scale as they are; dark -> the UIST theme copy (the per-user setting still wins); bpm ignored (Cubase
// sets the tempo); bars and notes -> normalizeClip.
function v3Patch(s) {
  const p = {};
  if (s.engine === 'formant') p.engine = 'voice'; else if (s.engine === 'classic') p.engine = 'tape';
  for (const k of ['glideMode', 'glideTime', 'key', 'scale']) if (k in s) p[k] = s[k];
  if ('lock' in s) p.scaleLock = !!s.lock;
  const lane = v => (v === 'none' ? 'off' : v);
  for (const [m, ln, cc] of [['mod1', 'm1Lane', 'cc1'], ['mod2', 'm2Lane', 'cc2']]) {
    if (!(ln in s) && !(cc in s)) continue;
    p[m] = {};
    if (ln in s) p[m].target = lane(s[ln]);
    if (cc in s) p[m].cc = s[cc];
  }
  return p;
}

module.exports = function state() {
  const { M, SND } = C.load();
  const st = stateSaves(M);
  const starterDefault = SND.defaultId || 'sawopen';
  const cases = [];
  for (const file of ['state/prototype-v3-demo.json', 'state/prototype-v3-edited.json']) {
    const s = JSON.parse(st.files[file].raw);
    const inst = afterSound(M, M.normalizeInst(M.mergeInst(M.defaultInst(), v3Patch(s))), false);
    cases.push({ kind: 'v3save', inputFile: file, result: {
      clip: C.clip(M.normalizeClip(s)), clipSettings: clipSettings(true), inst, where: split(inst), sound: starterDefault,
      ui: { theme: s.dark === false ? 'light' : 'dark' }, ignored: ['bpm'], soundFileBanner: false } });
  }
  for (const file of ['state/prototype-v4-demo.json', 'state/prototype-v4-session.json']) {
    const saved = JSON.parse(st.files[file].raw);
    const sound = saved.sound && SND.list.some(s => s.id === saved.sound) ? saved.sound : starterDefault;
    const inst = afterSound(M, M.normalizeInst(saved.inst), !saved.soundFile);
    cases.push({ kind: 'v4save', inputFile: file, result: {
      clip: C.clip(M.normalizeClip(saved.clip)), clipSettings: clipSettings(saved.loop !== false), inst, where: split(inst), sound,
      ui: { gridBeats: GRID.includes(saved.snap) ? saved.snap : 0.25, mode: clamp(num(saved.mode, 1) | 0, 1, 8),
        subLane: M.SAMPLE_LANES.includes(saved.subLane) ? saved.subLane : 'travel',
        tab: typeof saved.tab === 'string' && UI_TABS.includes(saved.tab) ? saved.tab : 'sample', folded: saved.folded === true },
      ignored: ['bpm', 'v'], soundFileBanner: saved.soundFile === true } });
  }
  const files = Object.assign({}, st.files);
  files['state/import.json'] = {
    prototype: C.prototypeHashes(['src/sl-ui.js', 'src/shell.html']),
    tolerance: { text: 'clip exact (node values within 1e-12); inst fields that pass through a parameter within (double)(float) of the value (plan 1.6 #12), the rest exact',
      clip: 'exact', nodeValues: 1e-12, paramFields: 'float32', other: 'exact' },
    notes: ['inputFile names a save in Tests/fixtures/state (the JSON text a prototype left in localStorage)',
      'v4save follows sl-ui.js init(): normalizeClip(saved.clip), normalizeInst(saved.inst), then the saved starter loads (switchSound): root 60, cents 0, and the whole file as the Region when soundFile is true (the session ended on a user file); a sound id that is not a starter falls back to ' + starterDefault,
      'v3save follows the plan 3 mapping (engine formant->voice classic->tape, lock->scaleLock, m1Lane/m2Lane->mod targets with none->off, cc1/cc2->mod ccs, glideMode glideTime key scale direct, dark->ui.theme, bpm ignored), merged into defaultInst() and normalized, then the default starter loads',
      'where.params = the inst fields that go to parameters (ParamBridge::applyPatch, one undo step "Import prototype save"); where.INST = the plan 2.2 state fields (seed, mod targets and CCs, key, scale, scaleLock)',
      'ui: the UIST fields the import sets (gridBeats = v4 snap; mode, subLane, tab, folded; theme from v3 dark); clipSettings = the plugin clip settings (loop from the save; beatsPerBar 4, offsetBeats 0, tieAcrossLoop on are plugin defaults)',
      'soundFileBanner: the save ended on a user file, which a prototype save never carries: the plugin shows "Load the sound file you used"'],
    cases,
  };
  return files;
};
