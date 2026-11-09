// tools/fixtures/analysis.js: Tests/fixtures/analysis/detect.json (PLUGIN-PLAN 5.3): detectPitch, detectKey and
// suggestParts of sl-analysis.js on the starter sounds, the test signals and a few long "songs" joined from them.
'use strict';
const C = require('./common.js');
const { starter } = require('./sounds.js');
const { makeSignal } = require('./support.js');

// input: {starter, seed?} | {signal, args} | {silence: seconds, sampleRate} | {concat: [input, ...]}
function inputAudio(inp) {
  if (inp.starter) { const s = starter(inp.starter, inp.seed == null ? 1 : inp.seed); return { channels: s.channels, sampleRate: s.sampleRate }; }
  if (inp.signal) return makeSignal(inp);
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
  const cases = inputs.map(input => {
    const a = inputAudio(input);
    return { input, n: a.channels[0].length, channels: a.channels.length, sampleRate: a.sampleRate, hash: C.fnvF32(a.channels),
      detectPitch: AN.detectPitch(a.channels, a.sampleRate), detectKey: AN.detectKey(a.channels, a.sampleRate), suggestParts: AN.suggestParts(a.channels, a.sampleRate) };
  });
  return { 'analysis/detect.json': {
    prototype: C.prototypeHashes(['tests/test-engine.js', 'tests/test-engine-extra.js']),
    tolerance: { text: 'detectPitch null-ness and MIDI exact, cents within 0.1, Hz within 1e-4 relative; detectKey exact; suggestParts kinds and labels exact, start/end within 1e-3 of the file',
      detectPitch: { nullness: 'exact', midi: 'exact', centsAbs: 0.1, hzRelative: 1e-4 }, detectKey: 'exact', suggestParts: { kind: 'exact', label: 'exact', startEndAbs: 1e-3, bpm: 'exact', tempoFound: 'exact' } },
    notes: ['input: {starter, seed?} (sounds/starter.json), {signal, args} (support/signals.json), {silence: seconds, sampleRate} (zeros, one channel) or {concat: [inputs]} (joined in order; all parts share a sample rate; a mono part fills every channel)',
      'n, channels, sampleRate and hash (FNV-1a over the Float32 bytes of every channel) describe the audio the three functions were given, so a C++ input mismatch is caught before any result is compared',
      'the concat inputs are long files (15-20 s) so that suggestParts has parts to find; the plan names no long input, these were chosen'],
    cases,
  } };
};
module.exports.inputAudio = inputAudio;
