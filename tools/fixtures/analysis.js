// tools/fixtures/analysis.js: Tests/fixtures/analysis/detect.json (PLUGIN-PLAN 5.3): detectPitch, detectKey and
// suggestParts of sl-analysis.js on the starter sounds, the test signals and a few long "songs" joined from them.
'use strict';
const fs = require('fs');
const path = require('path');
const C = require('./common.js');
const { starter } = require('./sounds.js');
const { makeSignal } = require('./support.js');

// {song: {seconds, sampleRate, sections, hits}}: tests/test-analysis.js's own buildSong and hash, read from that file
// by name (as support.js reads the signal makers), over the starters at seed 1. Its rate is that file's SR.
let songMaker = null;
function songAudio(spec) {
  if (!songMaker) {
    const text = fs.readFileSync(path.join(C.TESTS, 'test-analysis.js'), 'utf8');
    const m = /^const SR = (\d+);$/m.exec(text);
    if (!m) throw new Error('make-fixtures: tests/test-analysis.js has no const SR');
    const snd = {};
    for (const s of C.load().SND.list) snd[s.id] = starter(s.id, 1);
    const sr = Number(m[1]);
    const build = new Function('SR', 'snd', C.testFunctionSource('test-analysis.js', 'hash') + '\n' +
      C.testFunctionSource('test-analysis.js', 'buildSong') + '\nreturn buildSong;')(sr, snd);
    songMaker = { sr, build };
  }
  if (spec.sampleRate !== songMaker.sr) throw new Error('make-fixtures: a song is made at ' + songMaker.sr + ' Hz');
  return { channels: songMaker.build(spec.seconds, spec.sections, spec.hits), sampleRate: songMaker.sr };
}

// input: {starter, seed?} | {signal, args} | {silence: seconds, sampleRate} | {concat: [input, ...]} | {song: {...}}
//        | {gain, frames, of: input} (M7 fix pass: the first frames of every channel times gain, as a Float32Array's *= does)
function inputAudio(inp) {
  if (inp.starter) { const s = starter(inp.starter, inp.seed == null ? 1 : inp.seed); return { channels: s.channels, sampleRate: s.sampleRate }; }
  if (inp.gain != null) {
    const a = inputAudio(inp.of), chans = a.channels.map(c => Float32Array.from(c));
    for (const c of chans) for (let i = 0; i < Math.min(inp.frames, c.length); i++) c[i] *= inp.gain;
    return { channels: chans, sampleRate: a.sampleRate };
  }
  if (inp.signal) return makeSignal(inp);
  if (inp.song) return songAudio(inp.song);
  if (inp.silence != null) return { channels: [new Float32Array(Math.round(inp.silence * inp.sampleRate))], sampleRate: inp.sampleRate };
  if (inp.concat) {
    const parts = inp.concat.map(inputAudio), sr = parts[0].sampleRate, nc = Math.max(...parts.map(p => p.channels.length));
    if (parts.some(p => p.sampleRate !== sr)) throw new Error('make-fixtures: concat parts must share one sample rate');
    const n = parts.reduce((a, p) => a + p.channels[0].length, 0), chans = Array.from({ length: nc }, () => new Float32Array(n));
    let at = 0;
    for (const p of parts) { for (let c = 0; c < nc; c++) chans[c].set(p.channels[Math.min(c, p.channels.length - 1)], at); at += p.channels[0].length; }
    return { channels: chans, sampleRate: sr };
  }
  throw new Error('make-fixtures: unknown analysis input ' + JSON.stringify(inp));
}

module.exports = function analysis() {
  const { AN, SND } = C.load();
  const inputs = SND.list.map(s => ({ starter: s.id }));
  const hz = m => 440 * Math.pow(2, (m - 69) / 12);
  for (const m of [36, 48, 57, 69, 84]) inputs.push({ signal: 'tone', args: [hz(m), 1.2, 48000] });
  inputs.push({ signal: 'tone', args: [hz(62) * Math.pow(2, 23 / 1200), 1.2, 44100, { partials: 1 }] });
  inputs.push({ signal: 'tone', args: [hz(43), 2, 48000, { partials: 24, seed: 9, amp: 0.2 }] });
  inputs.push({ signal: 'noise', args: [2, 48000, 3] });
  inputs.push({ signal: 'smoothNoise', args: [2, 48000, 5] });
  inputs.push({ signal: 'dc', args: [1.5, 48000, 0.5] });
  inputs.push({ signal: 'chord', args: [[48, 55, 60, 64], 2, 48000] });
  inputs.push({ signal: 'chord', args: [[57, 60, 64, 69], 2, 48000] });
  inputs.push({ signal: 'chord', args: [[50, 53, 57, 62], 1.5, 44100] });
  inputs.push({ signal: 'formantTone', args: [hz(48), 1.5, 48000, 800, 300] });
  inputs.push({ signal: 'rampSource', args: [2, 48000] });
  inputs.push({ silence: 2, sampleRate: 44100 });
  inputs.push({ concat: [{ starter: 'pad' }, { starter: 'beat' }, { silence: 2, sampleRate: 44100 }, { starter: 'pluck' }] });
  inputs.push({ concat: [{ starter: 'beat' }, { starter: 'beat' }, { starter: 'beat' }, { starter: 'vowel' }, { starter: 'noise' }] });
  inputs.push({ concat: [{ signal: 'tone', args: [hz(55), 3, 44100] }, { silence: 1.5, sampleRate: 44100 }, { signal: 'noise', args: [1, 44100, 4, 0.1] }, { starter: 'saw' }] });
  inputs.push({ concat: [{ starter: 'pluck' }, { starter: 'pluck', seed: 2 }, { starter: 'pad', seed: 2 }] });
  // appended in M7 V1 (docs/NOTES-M7.md): other sample rates, so the 11 kHz copy's step D is 9, 2 and 3 (every case
  // above has D = 4), and the 5-minute song of tests/test-analysis.js's speed check, the plan's long input
  inputs.push({ signal: 'chord', args: [[48, 55, 60, 64], 2, 96000] });
  inputs.push({ signal: 'tone', args: [hz(57), 1.5, 22050] });
  inputs.push({ signal: 'noise', args: [2, 32000, 11] });
  inputs.push({ concat: [{ signal: 'tone', args: [hz(50), 3, 96000] }, { silence: 1, sampleRate: 96000 }, { signal: 'noise', args: [1, 96000, 5, 0.1] }, { signal: 'chord', args: [[45, 52, 57, 61], 2.5, 96000] }] });
  inputs.push({ concat: [{ signal: 'chord', args: [[48, 52, 55], 2, 22050] }, { signal: 'noise', args: [0.5, 22050, 6, 0.3] }, { signal: 'tone', args: [hz(43), 3, 22050, { partials: 24, seed: 9, amp: 0.2 }] }] });
  inputs.push({ song: { seconds: 300, sampleRate: 44100,
    sections: [['noise', 0.06, 0, 30], ['beat', 0.9, 30, 90], ['pad', 0.12, 30, 90], ['saw', 0.55, 90, 130], ['noise', 0.01, 90, 130], ['pluck', 0.8, 130, 160],
      ['pad', 0.25, 160, 180], ['beat', 0.9, 180, 240], ['pad', 0.12, 180, 240], ['saw', 0.5, 240, 270], ['pluck', 0.7, 270, 300]],
    hits: [[165, 1], [250, 0.6]] } });
  // appended in the M7 fix pass (docs/NOTES-M7.md, Fix, F3): branches no case above reaches. A held G3 whose 13 (then 14)
  // loudest blocks are noise: detectPitch's half rule finds G3 (then gives no pitch); a file under 1.2 s (no parts); A3 at
  // 8 and 11.025 kHz (the 11 kHz step D is 1); two Beat loops and the Pad with the first 2000 frames x 1.4 (the hit taken is
  // the near-best one that leaves the best loop free)
  const g3 = (secs) => ({ signal: 'tone', args: [196, secs, 48000, { partials: 4, amp: 0.12 }] });
  for (const blocks of [13, 14])
    inputs.push({ concat: [g3(60 * 2048 / 48000), { signal: 'noise', args: [blocks * 2048 / 48000, 48000, 99, 0.9] }, g3(4000 / 48000)] });
  inputs.push({ signal: 'tone', args: [hz(57), 1, 48000] });
  inputs.push({ signal: 'tone', args: [hz(57), 3, 8000] });
  inputs.push({ signal: 'tone', args: [hz(57), 3, 11025] });
  inputs.push({ gain: 1.4, frames: 2000, of: { concat: [{ starter: 'beat' }, { starter: 'beat' }, { starter: 'pad' }] } });
  const cases = inputs.map(input => {
    const a = inputAudio(input);
    return { input, n: a.channels[0].length, channels: a.channels.length, sampleRate: a.sampleRate, hash: C.fnvF32(a.channels),
      detectPitch: AN.detectPitch(a.channels, a.sampleRate), detectKey: AN.detectKey(a.channels, a.sampleRate), suggestParts: AN.suggestParts(a.channels, a.sampleRate) };
  });
  return { 'analysis/detect.json': {
    prototype: C.prototypeHashes(['tests/test-engine.js', 'tests/test-engine-extra.js', 'tests/test-analysis.js']),
    tolerance: { text: 'detectPitch null-ness and MIDI exact, cents within 0.1, Hz within 1e-4 relative; detectKey exact; suggestParts kinds and labels exact, start/end within 1e-3 of the file',
      detectPitch: { nullness: 'exact', midi: 'exact', centsAbs: 0.1, hzRelative: 1e-4 }, detectKey: 'exact', suggestParts: { kind: 'exact', label: 'exact', startEndAbs: 1e-3, bpm: 'exact', tempoFound: 'exact' } },
    notes: ['input: {starter, seed?} (sounds/starter.json), {signal, args} (support/signals.json), {silence: seconds, sampleRate} (zeros, one channel) or {concat: [inputs]} (joined in order; all parts share a sample rate; a mono part fills every channel)',
      'n, channels, sampleRate and hash (FNV-1a over the Float32 bytes of every channel) describe the audio the three functions were given, so a C++ input mismatch is caught before any result is compared',
      'the concat inputs are long files (15-20 s) so that suggestParts has parts to find; the plan names no long input, these were chosen',
      'input {song: {seconds, sampleRate, sections: [[starter, gain, from s, to s], ...], hits: [[s, gain], ...]}} is tests/test-analysis.js\'s own buildSong (read from that file by name) over the starters at seed 1',
      'appended in M7 V1: the cases after the four concat inputs, at other sample rates (the 11 kHz step D is 4 at 44.1 and 48 kHz, 9 at 96 kHz, 2 at 22.05 kHz and 3 at 32 kHz), and last the 5-minute song of tests/test-analysis.js\'s speed check',
      'appended in the M7 fix pass, after the song: detectPitch\'s half rule (a held G3 whose loudest blocks are noise, found and not), a file under 1.2 s, A3 at 8 and 11.025 kHz (D = 1), and the rotated hit (input {gain, frames, of}: the first frames of every channel times gain, as a Float32Array\'s *= does)'],
    cases,
  } };
};
module.exports.inputAudio = inputAudio;
