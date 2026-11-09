// tools/fixtures/sounds.js: Tests/fixtures/sounds/starter.json (PLUGIN-PLAN 5.3): the 8 starter sounds of sl-sounds.js.
'use strict';
const C = require('./common.js');

// per-block RMS (double maths on the Float32 samples, stored as binary32), L R interleaved; the last block may be short
function blockRms(L, R, B) {
  const nb = Math.ceil(L.length / B), out = new Float32Array(nb * 2);
  for (let b = 0; b < nb; b++) {
    const a = b * B, e = Math.min(L.length, a + B);
    let sl = 0, sr = 0;
    for (let i = a; i < e; i++) { sl += L[i] * L[i]; sr += R[i] * R[i]; }
    out[2 * b] = Math.sqrt(sl / (e - a)); out[2 * b + 1] = Math.sqrt(sr / (e - a));
  }
  return out;
}
function peakOf(chs) { let p = 0; for (const x of chs) for (let i = 0; i < x.length; i++) { const v = Math.abs(x[i]); if (v > p) p = v; } return p; }
const win = (x, at, n) => x.slice(at, at + n);

let soundCache = new Map();
function starter(id, seed) {
  const key = id + '@' + seed;
  if (!soundCache.has(key)) soundCache.set(key, C.load().SND.make(id, { seed }));
  return soundCache.get(key);
}

module.exports = function sounds() {
  const { SND } = C.load();
  const cases = [];
  const list = SND.list.map(s => [s.id, 1]).concat([['pad', 2], ['beat', 2]]);
  for (const [id, seed] of list) {
    const s = starter(id, seed), [L, R] = s.channels, n = L.length;
    const ats = [0, Math.floor(n / 2), n - 512];
    cases.push({ id, seed, sampleRate: s.sampleRate, n, rootMidi: s.rootMidi, peak: peakOf([L, R]), hash: C.fnvF32([L, R]),
      blockRms: blockRms(L, R, 4096), windows: ats.map(at => ({ at, L: win(L, at, 512), R: win(R, at, 512) })) });
  }
  return { 'sounds/starter.json': {
    prototype: C.prototypeHashes(),
    tolerance: { text: 'n, rootMidi exact; windows within 2e-7 absolute; blockRms within 1e-6 relative; peak within 1e-7; hash printed (bit-exact share)',
      exact: ['n', 'rootMidi', 'sampleRate'], windowsAbs: 2e-7, blockRmsRelative: 1e-6, peakAbs: 1e-7, hash: 'printed' },
    notes: ['hash = FNV-1a 32 over the little-endian bytes of L then R (the prototype tests\' fnv), as 8 hex digits',
      'blockRms = sqrt(mean(x^2)) per 4096 frames in double, stored as binary32, L R interleaved; the last block is shorter',
      'windows: 512 frames at 0, floor(n/2) and n-512'],
    cases,
  } };
};
module.exports.starter = starter;
module.exports.blockRms = blockRms;
