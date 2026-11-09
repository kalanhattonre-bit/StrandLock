'use strict';
const SRC = require('path').join(__dirname, '..', 'src') + '/';
const M = require(SRC + 'sl-model.js');
global.SLModel = M;
const P = require(SRC + 'sl-ui-panels.js');
let fails = 0, passes = 0;
const ok = (c, name, info) => { if (c) passes++; else { fails++; console.log('FAIL', name, info === undefined ? '' : JSON.stringify(info)); } };
const near = (a, b, e) => Math.abs(a - b) <= (e || 1e-9);

// positions
for (const [b, s] of [[0, '1.1.1'], [1, '1.2.1'], [4, '2.1.1'], [4.25, '2.1.2'], [5.5, '2.2.3'], [0.125, '1.1.1.60'], [15.75, '4.4.4'], [15.9375, '4.4.4.90']]) {
  ok(P.fmtPos(b) === s, 'fmtPos ' + b, P.fmtPos(b));
  ok(near(P.parsePos(s), b), 'parsePos ' + s, P.parsePos(s));
}
ok(P.parsePos('2') === 4 && P.parsePos('2.3') === 6, 'parsePos short forms');
ok(P.parsePos('0.1.1') === null && P.parsePos('abc') === null && P.parsePos('') === null, 'parsePos rejects');
// lengths
for (const [t, b] of [['0.5', 0.5], ['1.5 beats', 1.5], ['1/8', 0.5], ['1/16', 0.25], ['1/16T', 0.25 * 2 / 3], ['1/16t', 0.25 * 2 / 3], ['1/8 dotted', 0.75], ['1 bar', 4], ['3/4', 3], ['0.1.0.0', 1], ['1.0.2.0', 4.5], ['2', 2]])
  ok(near(P.parseBeats(t), b), 'parseBeats ' + t, P.parseBeats(t));
ok(P.parseBeats('x') === null && P.parseBeats('') === null, 'parseBeats rejects');
// pitch (Cubase: C3 = 60)
for (const [t, p] of [['C3', 60], ['c3', 60], ['C#3', 61], ['Db3', 61], ['B2', 59], ['bb2', 58], ['A-2', 9], ['A-1', 21], ['C-2', 0], ['G8', 127], ['60', 60], ['F#2', 54]])
  ok(P.parsePitch(t) === p, 'parsePitch ' + t, P.parsePitch(t));
ok(P.parsePitch('H3') === null && P.parsePitch('200') === null && P.parsePitch('G#8') === null, 'parsePitch rejects');
for (let p = 0; p <= 127; p++) if (P.parsePitch(M.noteName(p)) !== p) { ok(false, 'noteName round trip ' + p); break; }

// typed lane values: every formatLane readout parses back to (almost) the same value
const inst = M.defaultInst();
const instSync = M.mergeInst(inst, { refresh: { mode: 'sync', rate: '1/16' } });
const instFree = M.mergeInst(inst, { refresh: { mode: 'free', ms: 120 } });
const instCloud = M.mergeInst(inst, { engine: 'cloud' }), instLock = M.mergeInst(inst, { engine: 'cloud', lock: true }), instTape = M.mergeInst(inst, { engine: 'tape' });
let worst = 0;
for (const lane of M.LANE_IDS) {
  const ctxs = lane === 'refresh' ? [instSync, instFree] : lane === 'grain' ? [inst, instCloud, instLock, instTape] : [inst];
  for (const I of ctxs) for (const kind of (lane === 'travel' ? ['speed', 'path'] : ['speed'])) {
    const ctx = { inst: I, bpm: 96, travelKind: kind };
    for (let i = 0; i <= 40; i++) {
      const v = i / 40;
      const txt = M.formatLane(lane, v, ctx);
      const r = P.laneValueFromText(lane, txt, ctx);
      if (r.error) { ok(false, 'parse back ' + lane + ' ' + txt, r.error); continue; }
      const same = M.formatLane(lane, r.v, ctx) === txt;
      if (!same) {
        const a = M.mapLane(lane, v, ctx), b = M.mapLane(lane, r.v, ctx);
        const rel = Math.abs(a - b) / Math.max(1e-9, Math.abs(a));
        worst = Math.max(worst, rel);
        ok(rel < 0.06 || (lane === 'refresh' && v < 0.07), 'round trip ' + lane + ' (' + kind + ') v=' + v + ' "' + txt + '" -> "' + M.formatLane(lane, r.v, ctx) + '"', rel);
      } else ok(true);
    }
  }
}
console.log('worst relative difference after a readout round trip:', worst.toFixed(4));
// typed speeds: 1/8x must play (outside the dead band at Float32), stopped must stop
const sctx = { inst, bpm: 120, travelKind: 'speed' };
const tv = t => P.laneValueFromText('travel', t, sctx).v;
ok(M.travelSpeed(Math.fround(tv('1/8x'))) > 0, '1/8x plays forwards', M.travelSpeed(Math.fround(tv('1/8x'))));
ok(M.travelSpeed(Math.fround(tv('1/8x backwards'))) < 0, '1/8x backwards plays backwards');
ok(M.travelSpeed(Math.fround(tv('-0.125'))) < 0, '-0.125 plays backwards');
ok(M.travelSpeed(Math.fround(tv('stopped'))) === 0 && M.travelSpeed(Math.fround(tv('0'))) === 0, 'stopped stops');
ok(near(M.travelSpeed(Math.fround(tv('0.5x backwards'))), -0.5, 1e-5), '0.5x backwards', M.travelSpeed(Math.fround(tv('0.5x backwards'))));
ok(near(M.travelSpeed(Math.fround(tv('2x'))), 2, 1e-5) && near(M.travelSpeed(Math.fround(tv('8'))), 8, 1e-5), '2x and 8x');
ok(near(M.travelSpeed(Math.fround(tv('backwards'))), -1, 1e-5), '"backwards" alone = -1x');
ok(P.laneValueFromText('travel', '25%', { inst, travelKind: 'path' }).v === 0.25, 'path 25%');
ok(near(P.laneValueFromText('pitch', '+3 st', sctx).v, 0.5 + 3 / 24), 'pitch +3');
ok(near(P.laneValueFromText('tone', '2.4 kHz', sctx).v, Math.log2(2400 / 60) / 8.3), 'tone 2.4 kHz');
ok(P.laneValueFromText('amp', '-inf', sctx).v === 0, 'amp -inf');
ok(near(M.mapLane('heads', P.laneValueFromText('heads', '3', sctx).v, sctx), 3, 1e-9), 'heads 3');
ok(!!P.laneValueFromText('refresh', '60 ms', sctx).error, 'refresh in smooth mode explains');
ok(P.laneValueFromText('refresh', 'smooth', sctx).v === 0, 'refresh smooth');
const rs = P.laneValueFromText('refresh', '1/32', { inst: instSync, bpm: 120 });
ok(near(M.mapLane('refresh', rs.v, { inst: instSync, bpm: 120 }), 62.5, 0.01), 'refresh 1/32 at 120 bpm = 62.5 ms', rs);
ok(!!P.laneValueFromText('tone', 'bright', sctx).error && !!P.laneValueFromText('pitch', '', sctx).error, 'errors are sentences');

// simulate: forward 1x on a 6 s whole-file region over 1 beat at 120 bpm moves 0.5 s = 1/12 of the file
const n = M.mkNote(60, 0, 1, 'x');
const T = M.laneTable(n, 'travel', 64), Pt = M.laneTable(n, 'pitch', 64);
let s = P.simulate(n, M.normalizeInst(inst), 120, 6, T, Pt, 20);
ok(near(s.pos[20] - s.pos[0], 0.5 / 6, 1e-4) && s.dir[10] === 1 && near(s.sp[10], 1, 1e-6), 'simulate forward 1x', [s.pos[20] - s.pos[0], s.dir[10], s.sp[10]]);
s = P.simulate(Object.assign({}, n, { sample: Object.assign({}, n.sample, { motion: 'backward', start: { mode: 'region', pos: 0.5, range: 0.25, seed: 1 } }) }), M.normalizeInst(inst), 120, 6, T, Pt, 20);
ok(s.dir[5] === -1 && s.pos[20] < s.pos[0], 'simulate backward');
s = P.simulate(Object.assign({}, n, { sample: Object.assign({}, n.sample, { motion: 'freeze' }) }), M.normalizeInst(inst), 120, 6, T, Pt, 20);
ok(s.dir[5] === 0 && s.sp[5] === 0, 'simulate freeze');
s = P.simulate(Object.assign({}, n, { sample: Object.assign({}, n.sample, { motion: 'freeze' }) }), M.normalizeInst(instTape), 120, 6, T, Pt, 20);
ok(s.dir[5] === 1, 'Tape plays Freeze as Forward');
const tapeUp = Object.assign({}, n, { p: 72 });
s = P.simulate(tapeUp, M.normalizeInst(instTape), 120, 6, T, Pt, 20);
ok(near(s.sp[5], 2, 1e-6), 'Tape an octave up reads at 2x', s.sp[5]);
const stopInst = M.normalizeInst(M.mergeInst(inst, { region: { start: 0, end: 0.05, atEnd: 'stop' } }));
s = P.simulate(Object.assign({}, n, { l: 4 }), stopInst, 120, 6, M.laneTable(Object.assign({}, n, { l: 4 }), 'travel', 64), Pt, 40);
ok(Number.isNaN(s.pos[40]) && !Number.isNaN(s.pos[0]), 'At the end: Stop goes silent');
const sc = M.normalizeInst(M.mergeInst(inst, { motion: 'scatter', jump: '1/8' }));
s = P.simulate(Object.assign({}, n, { l: 2 }), sc, 120, 6, M.laneTable(Object.assign({}, n, { l: 2 }), 'travel', 64), Pt, 64);
ok(s.jump.reduce((a, b) => a + b, 0) === 3, 'Scatter 1/8 over 2 beats: 3 jumps inside the note', s.jump.reduce((a, b) => a + b, 0));
const pathN = M.setLaneNodes({ bars: 1, notes: [Object.assign({}, n, { sample: Object.assign({}, n.sample, { travel: 'path' }) })] }, 'x', 'travel', [{ t: 0, v: 0.2 }, { t: 1, v: 0.8 }]).notes[0];
s = P.simulate(pathN, M.normalizeInst(inst), 120, 6, M.laneTable(pathN, 'travel', 64), Pt, 20);
ok(near(s.pos[0], 0.2, 1e-6) && near(s.pos[20], 0.8, 1e-6) && s.dir[10] === 1, 'Path follows the line', [s.pos[0], s.pos[20], s.dir[10]]);
// thumbnail amps
const SND = require(SRC + 'sl-sounds.js');
const snd = SND.make('pluck');
const bins = P.peakBins(snd.channels);
ok(bins.length > 1000 && near(Math.max(...bins), 1, 1e-6), 'peak bins normalised', [bins.length, Math.max(...bins)]);
s = P.simulate(n, M.normalizeInst(inst), 120, 6, T, Pt, 20);
const amps = P.thumbAmps(s, bins, 6, 1);
ok(amps.length === 20 && amps.every(a => a >= 0 && a <= 1), 'thumb amps in range');
const nanCh = [new Float32Array(1000).fill(NaN), new Float32Array(1000)];
ok(P.peakBins(nanCh).every(x => x === 0), 'NaN samples give silent bins');
console.log((fails ? 'FAIL' : 'PASS') + ' pure helpers: ' + passes + ' passed, ' + fails + ' failed');
process.exit(fails ? 1 : 0);
