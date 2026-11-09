// tools/fixtures/support.js: Tests/fixtures/support/signals.json (PLUGIN-PLAN 5.3): the prototype tests' seeded signal
// generators (tests/test-engine.js, tests/test-engine-extra.js), which the C++ ports to Tests/support/TestSignals.
'use strict';
const C = require('./common.js');

// A signal named by the fixtures: { signal: fn, args } -> { channels, sampleRate }. Mono generators give one channel.
function makeSignal(spec) {
  const G = C.signals();
  const fn = G[spec.signal];
  if (!fn || spec.signal === 'lcg') throw new Error('make-fixtures: unknown signal ' + spec.signal);
  const out = fn(...spec.args);
  if (out && out.channels) return { channels: out.channels, sampleRate: out.sampleRate };
  const sr = spec.signal === 'chord' || spec.signal === 'formantTone' || spec.signal === 'tone' ? spec.args[2] : spec.signal === 'noise' || spec.signal === 'smoothNoise' || spec.signal === 'dc' ? spec.args[1] : 48000;
  return { channels: [out], sampleRate: sr };
}

module.exports = function support() {
  const G = C.signals();
  const cases = [];
  for (const seed of [1, 7, 0]) {
    const r = G.lcg(seed), values = [];
    for (let i = 0; i < 64; i++) values.push(r());
    cases.push({ fn: 'lcg', args: [seed], n: 64, values });
  }
  const specs = [
    { signal: 'tone', args: [261.6255653005986, 1, 48000] },
    { signal: 'tone', args: [130.8127826502993, 0.5, 44100, { partials: 24, seed: 3, amp: 0.3 }] },
    { signal: 'noise', args: [1, 48000, 3] },
    { signal: 'noise', args: [0.5, 44100, 9, 0.25] },
    { signal: 'chord', args: [[48, 55, 60, 64], 1, 48000] },
    { signal: 'formantTone', args: [130.8127826502993, 1, 48000, 800, 300] },
    { signal: 'smoothNoise', args: [1, 48000, 5] },
    { signal: 'rampSource', args: [2, 48000] },
    { signal: 'dc', args: [1, 48000, 0.5] },
  ];
  for (const sp of specs) {
    const s = makeSignal(sp), x = s.channels;
    const n = x[0].length;
    const o = { fn: sp.signal, args: sp.args, n, channels: x.length, sampleRate: s.sampleRate, hash: C.fnvF32(x) };
    o.head = x.length > 1 ? x.map(c => c.slice(0, 256)) : x[0].slice(0, 256);
    o.tail = x.length > 1 ? x.map(c => c.slice(n - 256)) : x[0].slice(n - 256);
    cases.push(o);
  }
  return { 'support/signals.json': {
    prototype: C.prototypeHashes(['tests/test-engine.js', 'tests/test-engine-extra.js']),
    tolerance: { text: 'within 2 Float32 ulps', ulps: 2, lcgValues: 'exact' },
    notes: ['the generators are the prototype tests\' own functions, read from tests/test-engine.js (lcg, tone, noise, chord, formantTone, smoothNoise, rampSource, midiHz) and tests/test-engine-extra.js (dc)',
      'lcg returns doubles (not Float32), so its cases list the first 64 values exactly instead of head/tail',
      'head/tail = the first/last 256 samples; for the two-channel rampSource they are [L, R]',
      'hash = FNV-1a 32 over the Float32 bytes of every channel in order; an object argument is the opts of tone(f0, secs, sr, opts)',
      'the other fixture files name a signal as {signal, args} with these same functions; mono signals are one channel (the engine reads it as R = L)'],
    cases,
  } };
};
module.exports.makeSignal = makeSignal;
