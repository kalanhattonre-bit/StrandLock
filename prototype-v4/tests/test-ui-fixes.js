// StrandLock v4: checks for the fixes the panels integrator made to the core modules while joining the page
// together (8 Nov 2026). One line per check: PASS/FAIL name: measured vs limit.
'use strict';
const path = require('path');
const M = require(path.join(__dirname, '..', 'src', 'sl-model.js'));

let passes = 0, fails = 0;
function check(name, ok, msg) { console.log((ok ? 'PASS ' : 'FAIL ') + name + ': ' + msg); if (ok) passes++; else fails++; }

// rateLabel: every rate reads as plain words; '1 bar' keeps its space with triplet and dotted too
{
  const want = { '1bar': '1 bar', '1barT': '1 bar triplet', '1barD': '1 bar dotted', '1/8': '1/8', '1/8T': '1/8 triplet', '1/16D': '1/16 dotted', '1/64T': '1/64 triplet' };
  const bad = Object.keys(want).filter(id => M.rateLabel(id) !== want[id]).map(id => id + ' -> "' + M.rateLabel(id) + '"');
  check('rateLabel words for 1 bar, triplets and dotted', !bad.length, bad.length ? bad.join(', ') : Object.keys(want).map(id => M.rateLabel(id)).join(' | '));
  const all = M.RATES.map(M.rateLabel);
  check('no rate label runs a number into a word', all.every(t => !/\dbar/.test(t)) && new Set(all).size === M.RATES.length, all.join(', '));
}

// fix round 1 (browser tester): readouts in the pickers' words, and the fades of Re-roll and the Tone macro
{
  const P = require(path.join(__dirname, '..', 'src', 'sl-ui-panels.js'));
  const I = M.applySoundPreset(M.defaultInst(), 'init').inst;
  const t8 = P.laneText('travel', M.travelSpeedToV(0.125), { inst: I, travelKind: 'speed' });
  const t8b = P.laneText('travel', M.travelSpeedToV(-0.125), { inst: I, travelKind: 'speed' });
  check('a 1/8x travel node reads 1/8x, not 0.13x', t8 === '1/8x forwards' && t8b === '1/8x backwards', t8 + ' | ' + t8b);
  const half = P.laneText('travel', M.travelSpeedToV(0.5), { inst: I, travelKind: 'speed' });
  check('other speeds keep their numbers', half === '0.5x forwards', half);
  const sync = M.mergeInst(I, { refresh: { mode: 'sync', rate: '1/8T' } });
  const r = P.laneText('refresh', 0.5, { inst: sync, bpm: 96 });
  check('a synced Refresh reads 1/8 triplet, as its picker says', /^Refresh 1\/8 triplet \(\d+ ms at 96 bpm\)$/.test(r), r);
  const S = require(path.join(__dirname, '..', 'src', 'sl-ui-sample.js'));
  const demo = M.loadClipPreset('strand-demo'), inst = (p) => M.normalizeInst(M.mergeInst(I, p));
  check('Re-roll faded with nothing random, active with Scatter, Spray in Cloud, Drift, Skip',
    S.fades(inst({}), demo).reroll.faded && !S.fades(inst({ motion: 'scatter' }), demo).reroll.faded && !S.fades(inst({ engine: 'cloud', spray: 0.2 }), demo).reroll.faded
      && S.fades(inst({ engine: 'voice', spray: 0.2 }), demo).reroll.faded && !S.fades(inst({ drift: 0.3 }), demo).reroll.faded
      && !S.fades(inst({ refresh: { mode: 'free', skip: 0.25 } }), demo).reroll.faded, 'faded only when nothing random plays');
  const up = [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.8, 0.5], down = [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.2, 0.5];
  const open = M.updateNotes(demo, null, n => { n.c.tone = M.flat(1); n.gen.tone = null; });
  check('Tone macro up fades while every Tone line is fully open; down or a lower line keeps it active',
    S.fades(inst({ macros: up }), open).mTone.faded && !S.fades(inst({ macros: down }), open).mTone.faded && !S.fades(inst({ macros: up }), demo).mTone.faded
      && !S.fades(inst({ macros: up, offsets: { tone: -0.3 } }), open).mTone.faded, S.fades(inst({ macros: up }), open).mTone.reason);
}

// fix round 2 (browser tester): plain units in the readouts, the Volume offset in dB
{
  const P = require(path.join(__dirname, '..', 'src', 'sl-ui-panels.js'));
  const S = require(path.join(__dirname, '..', 'src', 'sl-ui-sample.js'));
  const I = M.applySoundPreset(M.defaultInst(), 'init').inst;
  const ctx = { inst: I, travelKind: 'speed' };
  const p3 = P.laneText('pitch', 0.5 + 3 / 24, ctx), p1 = P.laneText('pitch', 0.5 - 1 / 24, ctx), p0 = P.laneText('pitch', 0.5, ctx);
  check('pitch readouts say semitones (one is singular)', p3 === '+3 semitones' && p1 === '-1 semitone' && p0 === '0 semitones', p3 + ' | ' + p1 + ' | ' + p0);
  const vib = P.laneText('vib', 0.25, ctx), form = P.laneText('formant', 0.75, ctx);
  check('vibrato and formant readouts say semitones', vib === '0.25 semitones of vibrato' && form === '+6 semitones', vib + ' | ' + form);
  const g1 = P.laneText('grain', 0.5, { inst: M.normalizeInst(M.mergeInst(I, { engine: 'voice', grainVoice: 1 })) });
  const g2 = P.laneText('grain', 0.5, { inst: M.normalizeInst(M.mergeInst(I, { engine: 'voice', grainVoice: 2 })) });
  check('Grain at 1 reads "1 cycle", at 2 "2 cycles"', g1 === '1 cycle' && g2 === '2 cycles', g1 + ' | ' + g2);
  const back = P.laneValueFromText('pitch', p3, ctx);
  check('a "semitones" readout types back to the same value', Math.abs(back.v - (0.5 + 3 / 24)) < 1e-9, back.v);
  const a = S.offsetText('amp', -0.35), b = S.offsetText('amp', 0.5), c = S.offsetText('amp', -0.8);
  check('the whole-clip Volume offset reads in dB (for a note at the default volume)', a === '-10.0 dB' && b === '+3.9 dB' && c === 'silent', a + ' | ' + b + ' | ' + c);
}

console.log(`\n${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
